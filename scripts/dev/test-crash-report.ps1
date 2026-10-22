<#
.SYNOPSIS
	Self-test of scripts/dev/crash-report.ps1 (#545) on fixture report folders: no game, no debugger.

.DESCRIPTION
	The report folders the game writes (scripts/dev/flight-recorder-check.ps1 checks the real ones):

	  <user dir>\crashes\<utc>\   the game ended: a crash, abort, terminate, heap corruption, an exit from outside, a kill
	  <user dir>\hangs\<utc>\     the hang watchdog's report of a stall (the game ran on)

	<utc> is yyyy-MM-dd-HH-mm-ss-NN (as crashes\ has always been named), so the newest sorts last. A folder holds:

	  report.txt        key=value lines, then "stack:" and the stack as indented lines "  #<n> <module>+0x<offset>[ <function>]":
	                      kind=<crash | abort | terminate | exit from outside | terminated | hang>
	                      exit=<signed decimal> (0x<hex>)    the exit code; "running" for a hang
	                      exception=0x<code> <NAME>         when an exception or a fail-fast ended it
	                      killer=<tid> <thread>             the thread that ended it: "main thread", or "start <module>!<export>" /
	                                                        "start <module>+0x<offset>" for a thread the game did not make
	                      module=<module>+0x<offset>        the faulting (or killing) module
	                      dump=<file name> | none
	  arx-recorder.txt  the flight recorder (tests/platform/FlightRecorderTest.h has the format)
	  arx-perf.log      the run's performance log up to the report (its R lines are the resource trend)
	  <name>.dmp        the minidump, when the game was still there to dump

	crash-report.ps1 [-UserDir <dir>] [-Path <report folder>] [-Cdb <cdb.exe> | none] [-InstallCdb]
	  - without -Path: the newest folder of <user dir>\crashes and <user dir>\hangs (by name); -UserDir defaults to this
	    working copy's isolated user directory when it has reports, else the player's Saved Games\Arx Libertatis
	  - -Cdb: the debugger to symbolise the dump with; "none" never runs one; by default a cdb.exe that is installed
	    (Windows SDK Debuggers, WinDbg, the pinned one in the tools directory), else none. Without cdb (or without a
	    dump) the stack is report.txt's own. cdb gets -z <dump>, -y <archived arx.pdb folder>;srv*<tools>\symbols*<Microsoft's
	    symbol server>, -i <archived arx.exe folder> when the archive under report.txt's exehash= has them, and -c
	    ".ecxr; kn 64; q" for a report with exception=, else "~#kn 64; q" (#610)
	  - debugger-lib.ps1 (-InstallCdb): the pinned bundle (a file:/// URL in the test) is refused on a SHA-256 mismatch
	    or a cdb.exe without Microsoft's signature, installing nothing; else windbg-<version>\amd64\ is unpacked and the
	    bundle deleted
	  - prints, each section starting at the beginning of a line:
	      Report: <folder name>                       only the name, never the full path
	      Kind: <kind>
	      Exit code: <exit>
	      Killer thread: <killer>
	      Exception: <exception>                      when report.txt has one
	      Faulting module: <module>
	      Stack (cdb): / Stack (report):              then the frames, indented
	      Last <n> events (arx-recorder.txt...        then the last 100 events (or all, when fewer), indented, newest last
	      Resource trend (arx-perf.log...             then perf-summary-lib's resource rows (test-resource-trend.ps1)
	      Replay clip: arx-replay.vrsim, <n> frames, <s> s   the last 75 s of poses the report folder holds (#550:
	                                                  replay it with scripts/dev/replay.ps1), or "Replay clip: none"
	  - nothing it prints holds a user path or the account name (Protect-ArxText of log-digest-lib.ps1), even when a
	    report file has one
	  - exit 0 when it printed a report, 1 when there is none
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

$script = Join-Path $PSScriptRoot 'crash-report.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-crash-report-test-$PID"
$pwsh = (Get-Process -Id $PID).Path
$c = [Globalization.CultureInfo]::InvariantCulture
$eventLine = '^\s+\d+\.\d{3} \d+ [a-z]+ -?\d+( .*)?$'

function Write-Fixture([string]$Path, [string[]]$Lines) {
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllLines($Path, $Lines, [Text.UTF8Encoding]::new($false))
}

# A report folder: report.txt, and optionally the recorder, the performance log and a dump
function New-Report([string]$Dir, [string[]]$Report, [string[]]$Recorder, [switch]$Perf, [string]$Dump) {
	Write-Fixture (Join-Path $Dir 'report.txt') $Report
	if ($Recorder) { Write-Fixture (Join-Path $Dir 'arx-recorder.txt') $Recorder }
	if ($Perf) { Write-Fixture (Join-Path $Dir 'arx-perf.log') $script:PerfLines }
	if ($Dump) { [IO.File]::WriteAllBytes((Join-Path $Dir $Dump), [byte[]](0x4D, 0x44, 0x4D, 0x50) + [byte[]]::new(60)) }
}

# Frame events 1..N of the main thread (tid 1000), then whatever is added
function New-Recorder([int]$Frames, [string[]]$Extra = @()) {
	$lines = @('# flight recorder (#545): fixture', "# thread 1000 main thread: $Frames events, 0 overwritten")
	for ($i = 1; $i -le $Frames; $i++) { $lines += [string]::Format($c, '{0:0.000} 1000 frame {1}', $i * 0.011, $i) }
	$lines + $Extra
}

# A performance log: S lines and R lines every 5 s, priv steps up by 64 MiB at t=30
$script:PerfLines = @('# arx-perf.log (#327): fixture')
foreach ($t in 5, 10, 15, 20, 25, 30, 35, 40) {
	$script:PerfLines += "S t=$t frames=60 min=16.50 mean=16.67 p95=16.80 max=17.00 hz=60.0 period=16.67 div=1 vr=0 missed=0 hitches=0 | other=0.10 render=4.00 limit=11.00 | uploads=0 self=0.4 selfmax=3"
	$script:PerfLines += "R t=$t priv=$(if ($t -ge 30) { '476.0' } else { '412.0' }) ws=650.0 commit=16000.0 handles=$(500 + $t) gdi=45 user=31 threads=40 tex=900 buf=210 xrsc=0"
}

function Invoke-CrashReport([string[]]$Arguments) {
	$output = & $pwsh -NoProfile -File $script @Arguments 2>&1
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = (@($output | ForEach-Object { "$_" }) -join "`n"); Lines = @($output | ForEach-Object { "$_" }) }
}

# The event lines printed under "Last <n> events"
function Get-PrintedEvents($Run) {
	$lines = $Run.Lines
	$start = -1
	for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^Last \d+ events') { $start = $i; break } }
	if ($start -lt 0) { return , @() }
	$events = @()
	for ($i = $start + 1; $i -lt $lines.Count -and $lines[$i] -match $eventLine; $i++) { $events += $lines[$i] }
	, $events
}

function Test-Clean($Run, [string]$What) {
	Check ($Run.Text -notmatch '(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b') "$What`: no user path in the output"
	Check ($Run.Text -notmatch '(?i)(?<![\w-])fixtureuser(?![\w-])') "$What`: no account name in the output"
	Check (-not $Run.Text.Contains($root)) "$What`: no full path of the report, only its name"
}

if (-not (Test-Path -LiteralPath $script)) {
	Write-Host "  FAIL scripts/dev/crash-report.ps1 does not exist" -ForegroundColor Red
	Write-Host 'test-crash-report: FAILED' -ForegroundColor Red
	exit 1
}

# A home path of the made-up account, built here so the file itself names none (check-private-data.ps1)
$fixtureHome = 'C:\Users\' + 'fixtureuser'
$previousUser = $env:USERNAME
# The account name the scrubber hides; the fixtures carry it where a careless report would
$env:USERNAME = 'fixtureuser'
try {
	# --- One user directory: an old crash, a hang, and the newest, an exit from outside (the #544 shape) -----------------
	$user = Join-Path $root 'user'
	$crash = Join-Path $user 'crashes\2026-10-19-10-00-00-00'
	New-Report $crash -Dump 'arx.dmp' -Perf -Report @(
		'kind=crash'
		'exit=-1073741819 (0xc0000005)'
		'exception=0xc0000005 EXCEPTION_ACCESS_VIOLATION'
		'killer=1000 main thread'
		'module=arx.exe+0x1a2b3c'
		'dump=arx.dmp'
		'stack:'
		"  #0 arx.exe+0x1a2b3c channel::``anonymous namespace'::execute CommandChannel.cpp:2497"
		'  #1 arx.exe+0x1a3000 channel::update CommandChannel.cpp:3950'
		'  #2 arx.exe+0x0f0000 ArxGame::doFrame ArxGame.cpp:1800'
		('  #3 arx.exe+0x0e0000 main ' + $fixtureHome + '\src\arx\main.cpp:12')
	) -Recorder (New-Recorder 29 @('0.330 1000 command 0 crash'))
	New-Report (Join-Path $user 'hangs\2026-10-19-10-30-00-00') -Dump 'hang.dmp' -Report @(
		'kind=hang'
		'exit=running'
		'killer=1000 main thread'
		'module=arx.exe+0x2b3c4d'
		'dump=hang.dmp'
		'stack:'
		'  #0 ntdll.dll+0x9d4f4 NtDelayExecution'
		'  #1 arx.exe+0x2b3c4d platform::stallThisThread HangWatchdog.cpp:500'
	) -Recorder (New-Recorder 10)
	$newest = Join-Path $user 'crashes\2026-10-19-11-00-00-00'
	$events = New-Recorder 148 @('1.640 1000 command 0 exitprocess 143', '1.650 4242 thread 143 start KERNEL32!ExitProcess')
	# A path that should never be in a report, and is (an older build): the output still hides it
	$events[2 + 119] = '1.320 1000 state 0 level ' + $fixtureHome + '\Saved Games\Arx Libertatis\save0000'
	New-Report $newest -Dump 'arx.dmp' -Perf -Report @(
		'kind=exit from outside'
		'exit=143 (0x8f)'
		'killer=4242 start KERNEL32!ExitProcess'
		'module=KERNEL32.DLL+0x1e3c0'
		'dump=arx.dmp'
		'stack:'
		'  #0 KERNEL32.DLL+0x1e3c0 ExitProcess'
	) -Recorder $events

	Write-Host '==> the newest report'
	$run = Invoke-CrashReport @('-UserDir', $user, '-Cdb', 'none')
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Report: 2026-10-19-11-00-00-00\b') 'the newest folder, by its name'
	Check ($run.Text -match '(?m)^Kind: exit from outside\s*$') 'Kind: exit from outside'
	Check ($run.Text -match '(?m)^Exit code: 143\b') 'Exit code: 143'
	Check ($run.Text -match '(?m)^Killer thread: .*KERNEL32!ExitProcess') 'Killer thread: the thread started at KERNEL32!ExitProcess'
	Check ($run.Text -match '(?m)^Faulting module: KERNEL32\.DLL\+0x1e3c0') 'Faulting module'
	Check ($run.Text -match '(?m)^Stack \(report\):\s*\n\s+#0 KERNEL32\.DLL\+0x1e3c0 ExitProcess') 'Stack (report): report.txt''s own stack without cdb'
	$printed = Get-PrintedEvents $run
	Check ($printed.Count -eq 100) "the last 100 of 150 events ($($printed.Count))"
	Check ($printed.Count -gt 0 -and $printed[0] -match ' 1000 frame 51$') "the oldest printed is the 51st ($(if ($printed.Count) { $printed[0] }))"
	Check ($printed.Count -gt 0 -and $printed[-1] -match ' 4242 thread 143 start KERNEL32!ExitProcess$') 'the newest is last'
	Check ($run.Text -notmatch '(?m) frame 50$') 'nothing older than the last 100'
	Check ($run.Text -match '(?m)^Resource trend') 'a Resource trend section'
	Check ($run.Text -match '(?m)^\s+priv\s.*first 412(\.0)?\s.*last 476(\.0)?\s.*step \+64(\.0)? at t=30\b') 'the priv row with the step'
	Test-Clean $run 'the newest report'

	Write-Host '==> the replay clip of a report (#550)'
	# 60 s of frames, 12 ms apart, on the session's clock (5 s to 65 s), the account's path in a comment
	$clipLines = @('# arx-replay (#550): fixture', '# build: Arx Fatalis VR test', '# save: save0001 replay', '# seed: 7', '# rate: 90',
		('# note: ' + $fixtureHome + '\Saved Games\Arx Libertatis'), 'ipd 0.064', 'floor 1.7')
	for ($ms = 5000; $ms -le 65000; $ms += 12) { $clipLines += "@$($ms)ms head 0.0000 0.0000 0.0000 $([string]::Format($c, '{0:0.00}', $ms / 1000)) 0.00 0.00" }
	Write-Fixture (Join-Path $newest 'arx-replay.vrsim') $clipLines
	$run = Invoke-CrashReport @('-UserDir', $user, '-Cdb', 'none')
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Replay clip: arx-replay\.vrsim\b(?=.*\b5001 frames\b)(?=.*\b60(\.0+)? s\b)') 'Replay clip: arx-replay.vrsim, 5001 frames, 60 s'
	Test-Clean $run 'a report with a replay clip'

	Write-Host '==> a given report (-Path): a crash'
	$run = Invoke-CrashReport @('-Path', $crash, '-Cdb', 'none')
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Report: 2026-10-19-10-00-00-00\b') 'the folder given'
	Check ($run.Text -match '(?m)^Kind: crash\s*$') 'Kind: crash'
	Check ($run.Text -match '(?m)^Exit code: -1073741819\b') 'Exit code'
	Check ($run.Text -match '(?m)^Exception: 0xc0000005 EXCEPTION_ACCESS_VIOLATION') 'Exception'
	Check ($run.Text -match '(?m)^Killer thread: 1000 main thread') 'Killer thread: the main thread'
	Check ($run.Text -match '(?m)^\s+#0 arx\.exe\+0x1a2b3c channel::.*execute') 'the stack names the function'
	$printed = Get-PrintedEvents $run
	Check ($run.Text -match '(?m)^Last 30 events' -and $printed.Count -eq 30) "all 30 events when there are fewer than 100 ($($printed.Count))"
	Check ($printed.Count -gt 0 -and $printed[-1] -match ' command 0 crash$') 'the channel command that crashed it, last'
	Test-Clean $run 'a crash'

	Write-Host '==> the newest is a hang'
	$user2 = Join-Path $root 'user2'
	New-Report (Join-Path $user2 'crashes\2026-10-19-09-00-00-00') -Report @('kind=crash', 'exit=3 (0x3)', 'killer=1000 main thread', 'module=arx.exe+0x10', 'dump=none', 'stack:')
	New-Report (Join-Path $user2 'hangs\2026-10-19-12-00-00-00') -Dump 'hang.dmp' -Report @(
		'kind=hang', 'exit=running', 'killer=1000 main thread', 'module=arx.exe+0x2b3c4d', 'dump=hang.dmp', 'stack:'
		'  #0 ntdll.dll+0x9d4f4 NtDelayExecution', '  #1 arx.exe+0x2b3c4d platform::stallThisThread HangWatchdog.cpp:500'
	) -Recorder (New-Recorder 5 @('0.060 1000 command 0 stall 6000'))
	$run = Invoke-CrashReport @('-UserDir', $user2, '-Cdb', 'none')
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Report: 2026-10-19-12-00-00-00\b') 'the newest of crashes\ and hangs\ together'
	Check ($run.Text -match '(?m)^Kind: hang\s*$') 'Kind: hang'
	Check ($run.Text -match 'platform::stallThisThread') 'the stalled function'
	Check ($run.Text -match '(?m)^Replay clip: none\s*$') 'Replay clip: none (a flat run records no clip)'

	Write-Host '==> a kill: no dump, no recorder, no performance log'
	$user3 = Join-Path $root 'user3'
	New-Report (Join-Path $user3 'crashes\2026-10-19-13-00-00-00') -Report @('kind=terminated', 'exit=-1 (0xffffffff)', 'killer=outside the process (TerminateProcess)', 'module=none', 'dump=none', 'stack:')
	$run = Invoke-CrashReport @('-UserDir', $user3, '-Cdb', 'none')
	Check ($run.Exit -eq 0) "exit 0 on a bare report ($($run.Exit))"
	Check ($run.Text -match '(?m)^Kind: terminated\s*$') 'Kind: terminated'
	Check ($run.Text -match '(?m)^Exit code: -1\b') 'Exit code: -1'

	Write-Host '==> no report'
	$empty = Join-Path $root 'empty'
	New-Item -ItemType Directory -Force $empty | Out-Null
	$run = Invoke-CrashReport @('-UserDir', $empty, '-Cdb', 'none')
	Check ($run.Exit -eq 1) "exit 1 when there is no report ($($run.Exit))"

	Write-Host '==> with cdb'
	$fakeDir = Join-Path $root 'debugger'
	New-Item -ItemType Directory -Force $fakeDir | Out-Null
	$fakeCdb = Join-Path $fakeDir 'cdb.cmd'
	Set-Content -LiteralPath $fakeCdb -Encoding ascii -Value @(
		'@echo off'
		'echo %*> "%~dp0cdb-args.txt"'
		'echo  # Child-SP          RetAddr               Call Site'
		'echo 00 0000001a`2b3c4d50 00007ff6`12345678     arx!channel::execute+0x1a'
		'echo 01 0000001a`2b3c4d90 00007ff6`12345999     arx!ArxGame::doFrame+0x2c'
	)
	$run = Invoke-CrashReport @('-Path', $newest, '-Cdb', $fakeCdb)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Stack \(cdb\):') 'Stack (cdb): the debugger''s stack when there is one'
	Check ($run.Text -match 'arx!channel::execute\+0x1a') 'cdb''s frames are printed'
	$cdbArgs = Get-Content -LiteralPath (Join-Path $fakeDir 'cdb-args.txt') -Raw -ErrorAction SilentlyContinue
	Check ("$cdbArgs" -match 'arx\.dmp') "cdb was given the report's dump ($("$cdbArgs".Trim()))"
	Check ("$cdbArgs" -match '~#kn 64' -and "$cdbArgs" -notmatch '\.ecxr') 'no exception: the stack of the event''s thread (~#kn)'
	Check ("$cdbArgs" -match '-y \S*srv\*\S+\*https://msdl\.microsoft\.com/download/symbols') 'cdb gets Microsoft''s public symbols, cached in the tools directory'
	Test-Clean $run 'with cdb'

	# A crash (#610): the stack of the exception's context, the archived arx.pdb and arx.exe (-y, -i) of its exe hash
	$hash = 'ab' * 32
	$localApp = Join-Path $root 'local'
	$archive = Join-Path $localApp "ArxFatalisVR\symbols\$hash"
	Write-Fixture (Join-Path $archive 'arx.pdb') @('pdb')
	Write-Fixture (Join-Path $archive 'arx.exe') @('exe')
	$reportFile = Join-Path $crash 'report.txt'
	Write-Fixture $reportFile @(Get-Content -LiteralPath $reportFile | ForEach-Object { if ($_ -eq 'stack:') { "exehash=$hash" }; $_ })
	$previousLocal = $env:LOCALAPPDATA
	$env:LOCALAPPDATA = $localApp
	try { $run = Invoke-CrashReport @('-Path', $crash, '-Cdb', $fakeCdb) } finally { $env:LOCALAPPDATA = $previousLocal }
	$cdbArgs = "$(Get-Content -LiteralPath (Join-Path $fakeDir 'cdb-args.txt') -Raw -ErrorAction SilentlyContinue)"
	Check ($run.Exit -eq 0) "a crash with cdb: exit 0 ($($run.Exit))"
	Check ($cdbArgs -match '\.ecxr; kn 64') 'an exception: the stack of its context (.ecxr; kn)'
	Check ($cdbArgs.Contains("-i $archive")) 'the archived arx.exe is cdb''s image path (-i): the x64 unwind data'
	Check ($cdbArgs.Contains("-y $archive;srv*")) 'the archived arx.pdb first on the symbol path (-y)'

	# --- The pinned debugger (scripts/dev/debugger-lib.ps1): checked before it is given its name -------------------
	Write-Host '==> the pinned cdb'
	. (Join-Path $PSScriptRoot 'debugger-lib.ps1')
	$tools = Join-Path $root 'tools'
	# A stand-in bundle: a zip holding an x64 package, itself a zip holding amd64\cdb.exe
	function New-FakeBundle([string]$Path, [string]$CdbSource) {
		$stage = Join-Path $root "bundle-$([IO.Path]::GetFileNameWithoutExtension($Path))"
		New-Item -ItemType Directory -Force (Join-Path $stage 'pkg\amd64'), (Join-Path $stage 'bundle') | Out-Null
		Copy-Item -LiteralPath $CdbSource -Destination (Join-Path $stage 'pkg\amd64\cdb.exe')
		Set-Content -LiteralPath (Join-Path $stage 'pkg\amd64\dbgeng.dll') -Value 'engine'
		Compress-Archive -Path (Join-Path $stage 'pkg\*') -DestinationPath (Join-Path $stage 'bundle\windbg_win-x64.zip')
		Rename-Item (Join-Path $stage 'bundle\windbg_win-x64.zip') 'windbg_win-x64.msix'
		Compress-Archive -Path (Join-Path $stage 'bundle\*') -DestinationPath "$Path.zip"
		Move-Item "$Path.zip" $Path
		(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
	}
	$unsigned = Join-Path $root 'unsigned-cdb.exe'
	[IO.File]::WriteAllBytes($unsigned, [byte[]](0x4D, 0x5A) + [byte[]]::new(126))
	$bundle = Join-Path $root 'unsigned.msixbundle'
	$sha = New-FakeBundle $bundle $unsigned
	Check ($null -eq (Get-ArxToolsCdb $tools @{ Version = 'test' })) 'not installed: none found (and nothing downloaded)'
	$err = try { Install-ArxCdb $tools @{ Version = 'test'; Sha = ('0' * 64); Url = ([Uri]$bundle).AbsoluteUri }; '' } catch { "$_" }
	Check ($err -match 'SHA-256 mismatch') "a bundle that is not the pinned one is refused ($err)"
	Check (-not (Test-Path (Join-Path $tools 'windbg-test'))) 'a refused bundle installs nothing'
	$err = try { Install-ArxCdb $tools @{ Version = 'test'; Sha = $sha; Url = ([Uri]$bundle).AbsoluteUri }; '' } catch { "$_" }
	Check ($err -match 'not signed by Microsoft') "a cdb.exe that is not Microsoft's is refused ($err)"
	Check (-not (Test-Path (Join-Path $tools 'windbg-test'))) 'a refused cdb.exe installs nothing'
	Check (@(Get-ChildItem -LiteralPath $tools -Force -ErrorAction SilentlyContinue).Count -eq 0) 'no part file is left behind'
	# A package entry that climbs out of amd64\ (amd64/../..) is refused before anything is written
	Add-Type -AssemblyName System.IO.Compression
	$evilBundle = Join-Path $root 'evil.msixbundle'
	$evilPackage = Join-Path $root 'evil-x64.msix'
	foreach ($pair in @(@($evilPackage, 'amd64/../../escaped.txt'), @($evilBundle, 'windbg_win-x64.msix'))) {
		$stream = [IO.File]::Open($pair[0], 'Create')
		$archiveOut = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
		$writer = [IO.StreamWriter]::new($archiveOut.CreateEntry($pair[1]).Open())
		if ($pair[0] -eq $evilBundle) { $writer.BaseStream.Write([IO.File]::ReadAllBytes($evilPackage)) } else { $writer.Write('escaped') }
		$writer.Dispose(); $archiveOut.Dispose(); $stream.Dispose()
	}
	$sha = (Get-FileHash -LiteralPath $evilBundle -Algorithm SHA256).Hash.ToLowerInvariant()
	$err = try { Install-ArxCdb $tools @{ Version = 'test'; Sha = $sha; Url = ([Uri]$evilBundle).AbsoluteUri }; '' } catch { "$_" }
	Check ($err -match 'entry outside its folder') "a package entry with .. is refused ($err)"
	Check (-not (Test-Path (Join-Path $root 'escaped.txt')) -and -not (Test-Path (Join-Path $tools 'escaped.txt'))) 'nothing was written outside the folder'
	Check (-not (Test-Path (Join-Path $tools 'windbg-test'))) 'a refused package installs nothing'

	# A file Microsoft signed (Windows' own where.exe) stands in for the real cdb.exe
	$signed = Join-Path $env:SystemRoot 'System32\where.exe'
	if (Test-ArxMicrosoftSigned $signed) {
		$bundle = Join-Path $root 'signed.msixbundle'
		$sha = New-FakeBundle $bundle $signed
		$installed = Install-ArxCdb $tools @{ Version = 'test'; Sha = $sha; Url = ([Uri]$bundle).AbsoluteUri }
		Check ($installed -eq (Join-Path $tools 'windbg-test\amd64\cdb.exe') -and (Test-Path -LiteralPath $installed)) "the pinned bundle installs windbg-<version>\amd64\cdb.exe ($installed)"
		Check (Test-Path -LiteralPath (Join-Path $tools 'windbg-test\amd64\dbgeng.dll')) 'the rest of amd64\ comes with it'
		Check ((Get-ArxToolsCdb $tools @{ Version = 'test' }) -eq $installed) 'installed: found without a download'
		Check (@(Get-ChildItem -LiteralPath $tools -File -Force).Count -eq 0) 'the bundle is deleted after the install'
	} else {
		Write-Host '  (skipped: where.exe has no Microsoft signature here)'
	}
} finally {
	$env:USERNAME = $previousUser
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-crash-report: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-crash-report: all checks passed' -ForegroundColor Green
exit 0
