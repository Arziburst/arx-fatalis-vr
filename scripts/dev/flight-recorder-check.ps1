<#
.SYNOPSIS
	Flight recorder check (#545): end the game every way it can end and check that each way leaves one obvious report.

.DESCRIPTION
	A dedicated check, not a CI sequence: most cases kill the game on purpose, which a CI phase reads as a failure
	(the smoke runs inside it FAIL by design - this script judges the evidence afterwards, as exit-trace-check.ps1
	does). Run it by hand before the PR; every case is one launch through smoke-test.ps1 (D-014: the game lock, the
	fresh build, the launch ledger) except 'plain', which launches without the test channel on purpose, still under
	the game lock. Flat (--no-vr), level 10, this working copy's isolated user directory. Automatic bug filing is off
	for the runs (ARXVR_AUTOREPORT=0): the crashes are deliberate.

	The interface it checks (the formats are in scripts/dev/test-crash-report.ps1, tests/platform/FlightRecorderTest.h
	and tests/platform/ProcessResourcesTest.h):
	  - arx.exe --dump-helper: started by every launch, one per game, gone within 15 s after its game ended
	  - <user dir>\crashes\<utc>\ for every end of the game that is not a clean quit - exactly ONE new folder per death -
	    and <user dir>\hangs\<utc>\ for every stall the hang watchdog reports - one per stall; a clean quit adds neither.
	    Each holds report.txt (kind=, exit=, killer=, exception=, module=, dump=, stack:), arx-recorder.txt, arx-perf.log
	    and, unless the game was already gone (TerminateProcess), the dump named by dump=: a minidump with all threads
	    (written before the exit proceeds: at least 3 of them), ThreadInfo (stream 17), HandleData (12),
	    UnloadedModules (14) and the flight recorder as a user stream (a stream type above 0xffff holding the
	    recorder's text). Only the 5 newest folders of each are kept.
	  - no file of a report folder holds a user path (a drive, Users and a name) or the account or machine name, neither as text nor
	    as UTF-16 - the dump included
	  - <user dir>\arx-recorder.txt written on every exit (by the dump helper when the game could not)
	  - arx-perf.log: an R line every 5 s, "R t=<s> priv=<MiB> ws=<MiB> commit=<MiB> handles= gdi= user= threads= tex=
	    buf= xrsc=" (one decimal for MiB); perf-summary-lib's resource trend shows a `leak 64` as a step of priv
	  - the hang watchdog in every launch, also without --test-dir: arx.log "hang watchdog: watching the main thread,
	    threshold <n> ms"; a stall gets "[hang] dump written: hangs\<utc>\<name>.dmp" (scripts/dev/xrs/hang-dump.xrs)
	  - arx.exe --no-recorder: the same game without the flight recorder (the overhead baseline)
	  - the test channel commands (each acknowledged, the killing ones not):
	      crash             an access violation on the main thread (exists since #132)
	      abort             abort() on the main thread
	      terminate         std::terminate() on the main thread
	      terminate thread  std::terminate() on a new std::thread (a thread the game's handlers were never set up on)
	      heapcorrupt       a heap corruption the heap detects (STATUS_HEAP_CORRUPTION, 0xc0000374)
	      exitprocess <n>   CreateThread(ExitProcess, <n>): a thread that starts at ExitProcess, as a foreign kill does
	      stall <ms>        the main thread sleeps; 6000 is accepted (over the watchdog's default threshold)
	      leak <MiB>        allocate and touch <MiB> of private memory and keep it until the exit
	  - scripts/dev/crash-report.ps1 -Path <folder> -Cdb none prints the folder's report

	Cases (-Case, several allowed; 'all' = plain, clean and the 8 kills: 10 launches):
	  plain             a launch without --test-dir: the watchdog's start in arx.log, one dump helper, a WM_CLOSE quit
	                    is clean: exit 0, no new crash report, arx-recorder.txt written, the helper gone
	  clean             a 75 s run that passes smoke-test.ps1: `leak 64` shows as a step of priv in the R lines and in
	                    perf-summary; `stall 6000` gives exactly one hangs\ folder with its dump and a crash-report naming
	                    platform::stallThisThread; arx-recorder.txt has frame, state and thread events; no crash report
	  crash, abort, terminate, terminate-thread, heapcorrupt
	                    the channel command; one new crashes\ folder of that kind (heapcorrupt: kind=crash, exception
	                    0xc0000374; crash: 0xc0000005 and channel::...execute in the stack) with the dump, the command
	                    as the recorder's last command, a symbolised arx.exe frame in crash-report's stack; the killer is
	                    the main thread, except for terminate-thread
	  exitprocess       `exitprocess 143`: the game ends with exit code 143 within -ExitBoundSec of the command (the
	                    bounded dump wait never refuses the kill); kind=exit from outside, exit=143, killer=
	                    ...KERNEL32!ExitProcess, the dump written before the exit, the thread birth in the recorder
	  remote-exit       the same from another process: CreateRemoteThread(kernel32!ExitProcess, 143) - the #544 shape
	  stop-process      Stop-Process -Force (TerminateProcess): kind=terminated, exit= the code the game ended with, the
	                    recorder and the resource lines kept by the dump helper (no dump: the game is gone)
	  overhead          two 60 s runs, with and without --no-recorder (2 launches; run it on a quiet PC): the frame's busy
	                    time (mean - limit - vrwait) and self of the perf log's S lines, and private MiB of the game plus
	                    its dump helper; FAILS over 0.05 ms or 5 MiB. Prints the numbers for the PR body.
	  replay-crash      (#550; not in 'all') one run on the simulated headset (idle.vrsim, the head turning): a 25 s
	                    session, `stall 6000`, then `crash`. The hang's and the crash's report folders each hold
	                    arx-replay.vrsim - the replay ring's last 75 s of poses, written by the dump helper: the clip
	                    header (# arx-replay, # seed:, # save:), frames up to the report (its last frame within 3 s of the
	                    session time the command was sent at), at most 61 s of them - and crash-report.ps1 prints
	                    "Replay clip: arx-replay.vrsim"

	Exit code 0 on PASS, 1 on FAIL (also when crash-report.ps1 is missing: before any launch), 2 when a launch was
	skipped (the game lock stayed busy).

.PARAMETER Case
	The cases to run (default all).

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.PARAMETER ExitBoundSec
	How long an exit from outside may take from the command to the end of the process (default 15).

.EXAMPLE
	.\scripts\dev\flight-recorder-check.ps1
.EXAMPLE
	.\scripts\dev\flight-recorder-check.ps1 -Case exitprocess, remote-exit
#>
[CmdletBinding()]
param(
	[ValidateSet('all', 'plain', 'clean', 'crash', 'abort', 'terminate', 'terminate-thread', 'heapcorrupt', 'exitprocess', 'remote-exit', 'stop-process', 'overhead', 'replay-crash')]
	[string[]]$Case = @('all'),
	[switch]$Rebuild,
	[int]$ExitBoundSec = 15
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

$crashReport = Join-Path $PSScriptRoot 'crash-report.ps1'
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$pwsh = (Get-Process -Id $PID).Path

$kills = @('crash', 'abort', 'terminate', 'terminate-thread', 'heapcorrupt', 'exitprocess', 'remote-exit', 'stop-process')
$cases = if ($Case -contains 'all') { @('plain', 'clean') + $kills } else { @($Case) }

# Before any launch: the report command every case reads the reports with
if (-not (Test-Path -LiteralPath $crashReport)) {
	Write-Host 'flight recorder check: FAIL before any launch - scripts/dev/crash-report.ps1 does not exist (#545)' -ForegroundColor Red
	exit 1
}

$userDir = Get-ArxUserDir
$testDir = Get-ArxTestDir
$crashDir = Get-ArxCrashDir $userDir
$hangDir = Join-Path $userDir 'hangs'
$buildDir = Get-DefaultBuildDir $ArxSourceDir 'Release'
$exe = Join-Path $buildDir 'arx.exe'
$perfLog = Join-Path $userDir 'arx-perf.log'
$userRecorder = Join-Path $userDir 'arx-recorder.txt'

$script:failed = 0
$script:passed = 0
$script:skipped = $false
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green; $script:passed++ }
	else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failed++ }
}

# --- Personal data ---------------------------------------------------------------------------------------------------

# A user path, the account name and the machine name - never printed, only named
$script:PrivateRules = @(@{ Kind = 'a user path'; Rx = [regex]'(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b' })
$generic = 'SYSTEM', 'Administrator', 'Admin', 'User', 'Public', 'Default', 'Guest', 'WORKGROUP'
foreach ($pair in @(@('the account name', $env:USERNAME), @('the machine name', $env:COMPUTERNAME))) {
	$name = "$($pair[1])".TrimEnd('$')
	if ($name.Length -ge 4 -and $name -notin $generic) {
		$script:PrivateRules += @{ Kind = $pair[0]; Rx = [regex]::new("(?i)(?<![A-Za-z0-9_-])$([regex]::Escape($name))(?![A-Za-z0-9_-])") }
	}
}

function Find-ArxPrivateText([string]$Text, [string]$View = 'text') {
	foreach ($rule in $script:PrivateRules) { if ($rule.Rx.IsMatch($Text)) { "$($rule.Kind) ($View)" } }
}

# A file as bytes: as 8-bit text and as UTF-16 at both alignments (a dump holds its paths as UTF-16)
function Find-ArxPrivateBytes([string]$Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	$found = @(Find-ArxPrivateText ([Text.Encoding]::Latin1.GetString($bytes)) 'text')
	$found += @(Find-ArxPrivateText ([Text.Encoding]::Unicode.GetString($bytes)) 'UTF-16')
	if ($bytes.Length -gt 1) { $found += @(Find-ArxPrivateText ([Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1)) 'UTF-16') }
	@($found | Select-Object -Unique)
}

function Hide-ArxPrivateText([string]$Text) {
	foreach ($rule in $script:PrivateRules) { $Text = $rule.Rx.Replace($Text, '<private>') }
	$Text
}

# --- Files the game holds open ----------------------------------------------------------------------------------------

function Read-ArxSharedLines([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return @() }
	$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
	try { @(([IO.StreamReader]::new($stream)).ReadToEnd() -split "`r?`n" | Where-Object { $_ }) } finally { $stream.Dispose() }
}

function Wait-ArxResourceLines([int]$Count, [int]$TimeoutSec = 20) {
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	do {
		if (@(Read-ArxSharedLines $perfLog | Where-Object { $_ -like 'R t=*' }).Count -ge $Count) { return $true }
		Start-Sleep -Milliseconds 500
	} while ((Get-Date) -lt $deadline)
	$false
}

# --- Reports -----------------------------------------------------------------------------------------------------------

function Get-ArxReportNames([string]$Dir) {
	@(Get-ChildItem -LiteralPath $Dir -Directory -ErrorAction SilentlyContinue | Where-Object Name -match '^\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d' | Select-Object -ExpandProperty Name | Sort-Object)
}

function Read-ArxReportFields([string]$Folder) {
	$file = Join-Path $Folder 'report.txt'
	if (-not (Test-Path -LiteralPath $file)) { return $null }
	$fields = [ordered]@{}
	foreach ($line in Get-Content -LiteralPath $file) {
		if ($line -eq 'stack:') { break }
		if ($line -match '^([a-z]+)=(.*)$') { $fields[$Matches[1]] = $Matches[2] }
	}
	$fields
}

$script:EventRx = '^(\d+\.\d{3}) (\d+) ([a-z]+) (-?\d+)(?: (.*))?$'
function Read-ArxRecorderEvents([string]$File) {
	@(Read-ArxSharedLines $File | ForEach-Object {
		if ($_ -match $script:EventRx) { [pscustomobject]@{ T = [double]$Matches[1]; Tid = $Matches[2]; Kind = $Matches[3]; Value = [long]$Matches[4]; Text = "$($Matches[5])" } }
	})
}

# A minidump's stream directory (MINIDUMP_HEADER, MINIDUMP_DIRECTORY) and its thread count (ThreadListStream = 3)
function Read-ArxMinidump([string]$Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	$dump = [pscustomobject]@{ Valid = $false; Streams = @(); Threads = 0; Bytes = $bytes }
	if ($bytes.Length -lt 32 -or [BitConverter]::ToUInt32($bytes, 0) -ne 0x504D444D) { return $dump }
	$count = [BitConverter]::ToUInt32($bytes, 8)
	$dir = [long][BitConverter]::ToUInt32($bytes, 12)
	$streams = [Collections.Generic.List[object]]::new()
	for ($i = 0; $i -lt $count; $i++) {
		$o = $dir + 12 * $i
		if ($o + 12 -gt $bytes.Length) { break }
		$streams.Add([pscustomobject]@{ Type = [BitConverter]::ToUInt32($bytes, [int]$o); Size = [BitConverter]::ToUInt32($bytes, [int]$o + 4); Rva = [BitConverter]::ToUInt32($bytes, [int]$o + 8) })
	}
	$dump.Valid = $true
	$dump.Streams = $streams.ToArray()
	$list = @($streams | Where-Object Type -eq 3) | Select-Object -First 1
	if ($list -and [long]$list.Rva + 4 -le $bytes.Length) { $dump.Threads = [BitConverter]::ToUInt32($bytes, [int]$list.Rva) }
	$dump
}

function Get-ArxDumpStreamText($Dump, $Stream) {
	if ([long]$Stream.Rva + [long]$Stream.Size -gt $Dump.Bytes.Length) { return '' }
	[Text.Encoding]::UTF8.GetString($Dump.Bytes, [int]$Stream.Rva, [int]$Stream.Size)
}

# A stack frame of arx.exe with a C++ function name: "arx.exe+0x1234 ns::function" (report.txt) or "arx!ns::function" (cdb)
$script:ArxFrame = '(?m)^\s+.*\barx(\.exe)?[!+].*\w::\w'

function Invoke-ArxCrashReport([string]$Folder) {
	$output = @(& $pwsh -NoProfile -File $crashReport -Path $Folder -Cdb none 2>&1 | ForEach-Object { "$_" })
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = ($output -join "`n"); Lines = $output }
}

# Everything a report folder must hold; $Expect: What, Kind, ExitCode, KillerMatch, KillerNotMatch, Exception, Dump,
# Command, ThreadEvent, StackMatch
function Test-ArxReportFolder([string]$Folder, [hashtable]$Expect) {
	$what = $Expect.What
	$name = Split-Path -Leaf $Folder
	$fields = Read-ArxReportFields $Folder
	Check ($null -ne $fields) "$what`: $name has a report.txt"
	if ($null -eq $fields) { return }
	Write-Host "  --- $name\report.txt:"
	Get-Content -LiteralPath (Join-Path $Folder 'report.txt') -TotalCount 30 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText $_)" }

	Check ($fields['kind'] -eq $Expect.Kind) "$what`: kind=$($Expect.Kind) (kind=$($fields['kind']))"
	if ($Expect.Kind -ne 'hang') {
		$exitText = "$($fields['exit'])"
		$exitOk = $exitText -match '^(-?\d+)\b'
		if ($exitOk -and $null -ne $Expect.ExitCode) { $exitOk = ([long]$Matches[1] -eq [long]$Expect.ExitCode) }
		Check $exitOk "$what`: exit=$(if ($null -ne $Expect.ExitCode) { $Expect.ExitCode } else { '<code>' }) (exit=$exitText)"
	}
	Check ([bool]$fields['killer']) "$what`: a killer= line (killer=$($fields['killer']))"
	if ($Expect.KillerMatch) { Check ("$($fields['killer'])" -match $Expect.KillerMatch) "$what`: the killer matches /$($Expect.KillerMatch)/" }
	if ($Expect.KillerNotMatch) { Check ($fields['killer'] -and "$($fields['killer'])" -notmatch $Expect.KillerNotMatch) "$what`: the killer is not /$($Expect.KillerNotMatch)/" }
	if ($Expect.Exception) { Check ("$($fields['exception'])" -match $Expect.Exception) "$what`: exception /$($Expect.Exception)/ (exception=$($fields['exception']))" }

	# The flight recorder as the report has it
	$events = Read-ArxRecorderEvents (Join-Path $Folder 'arx-recorder.txt')
	Check (@($events | Where-Object Kind -eq 'frame').Count -gt 0) "$what`: the report's arx-recorder.txt has frame events ($($events.Count) events)"
	if ($Expect.Command) {
		$commands = @($events | Where-Object Kind -eq 'command')
		Check ($commands.Count -gt 0 -and $commands[-1].Text -eq $Expect.Command) "$what`: the recorder's last command is '$($Expect.Command)' ($(if ($commands) { $commands[-1].Text } else { 'none' }))"
	}
	if ($Expect.ThreadEvent) {
		Check (@($events | Where-Object { $_.Kind -eq 'thread' -and $_.Text -match $Expect.ThreadEvent }).Count -gt 0) "$what`: the recorder has the thread birth /$($Expect.ThreadEvent)/"
	}

	# The resource lines up to the report
	$rLines = @(Read-ArxSharedLines (Join-Path $Folder 'arx-perf.log') | Where-Object { $_ -like 'R t=*' })
	Check ($rLines.Count -ge 1) "$what`: the report's arx-perf.log has R lines ($($rLines.Count))"

	# The dump
	if ($Expect.Dump) {
		$dumpName = "$($fields['dump'])"
		$dumpFile = if ($dumpName -and $dumpName -ne 'none') { Join-Path $Folder $dumpName } else { $null }
		$haveDump = [bool]$dumpFile -and (Test-Path -LiteralPath $dumpFile -PathType Leaf)
		Check $haveDump "$what`: the dump named by dump= is in the folder (dump=$dumpName)"
		if ($haveDump) {
			$dump = Read-ArxMinidump $dumpFile
			$types = @($dump.Streams | ForEach-Object { [long]$_.Type })
			Check $dump.Valid "$what`: the dump is a minidump"
			Check ($dump.Threads -ge 3) "$what`: all threads in the dump - written before the exit ($($dump.Threads) threads)"
			Check ($types -contains 17) "$what`: the dump has ThreadInfo (stream 17)"
			Check ($types -contains 12) "$what`: the dump has HandleData (stream 12)"
			Check ($types -contains 14) "$what`: the dump has UnloadedModules (stream 14)"
			$recorderStreams = @($dump.Streams | Where-Object { [long]$_.Type -ge 0x10000 -and (Get-ArxDumpStreamText $dump $_) -match '(?m)^\d+\.\d{3} \d+ frame -?\d+' })
			Check ($recorderStreams.Count -ge 1) "$what`: the flight recorder is a user stream of the dump"
		}
	}

	# Nothing personal in any file of the folder, the dump included
	foreach ($file in @(Get-ChildItem -LiteralPath $Folder -File -Recurse)) {
		$found = @(Find-ArxPrivateBytes $file.FullName)
		Check ($found.Count -eq 0) "$what`: no user path or name in $($file.Name)$(if ($found) { ' - found ' + ($found -join ', ') })"
	}

	# The one command for the coordinator
	$report = Invoke-ArxCrashReport $Folder
	Write-Host "  --- crash-report.ps1 -Path $name -Cdb none (first 40 lines):"
	$report.Lines | Select-Object -First 40 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText $_)" }
	Check ($report.Exit -eq 0) "$what`: crash-report.ps1 exit 0 ($($report.Exit))"
	Check ($report.Text -match "(?m)^Kind: $([regex]::Escape($Expect.Kind))\s*$") "$what`: crash-report prints Kind: $($Expect.Kind)"
	if ($Expect.StackMatch) { Check ($report.Text -match $Expect.StackMatch) "$what`: crash-report's stack matches /$($Expect.StackMatch)/" }
	Check ($report.Text -match '(?m)^Last \d+ events') "$what`: crash-report prints the last events"
	Check ($report.Text -match '(?m)^Resource trend' -and $report.Text -match '(?m)^\s+priv\s.*first ') "$what`: crash-report prints the resource trend"
	$leaks = @(Find-ArxPrivateText $report.Text 'crash-report output')
	Check ($leaks.Count -eq 0) "$what`: no user path or name in crash-report's output$(if ($leaks) { ' - found ' + ($leaks -join ', ') })"
}

# --- Processes ---------------------------------------------------------------------------------------------------------

function Get-ArxDumpHelpers {
	@(Get-CimInstance Win32_Process -Filter "Name='arx.exe'" -ErrorAction SilentlyContinue | Where-Object {
		"$($_.CommandLine)" -match '--dump-helper' -and "$($_.ExecutablePath)" -ieq $exe
	})
}

function Wait-ArxDumpHelpersGone([int]$TimeoutSec = 15) {
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	do {
		if (@(Get-ArxDumpHelpers).Count -eq 0) { return $true }
		Start-Sleep -Milliseconds 500
	} while ((Get-Date) -lt $deadline)
	$false
}

# A batch for the test channel that is not waited for: a killing command never gets its ack
function Send-ArxCommandNoWait([string]$Line) {
	$file = Join-Path $testDir 'command.txt'
	$tmp = "$file.tmp"
	[IO.File]::WriteAllText($tmp, "$Line`n", [Text.UTF8Encoding]::new($false))
	Move-Item -LiteralPath $tmp -Destination $file -Force
}

# CreateRemoteThread(kernel32!ExitProcess, <code>) from this process: a foreign thread ends the game (#544: GNU timeout)
function Invoke-ArxRemoteExit([int]$ProcessId, [int]$Code) {
	if (-not ('ArxRemoteExitNative' -as [type])) {
		Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ArxRemoteExitNative {
  [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandleW(string name);
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi, ExactSpelling = true)] public static extern IntPtr GetProcAddress(IntPtr module, string name);
  [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stack, IntPtr start, IntPtr parameter, uint flags, out uint threadId);
  [DllImport("kernel32.dll", SetLastError = true)] public static extern bool CloseHandle(IntPtr handle);
}
'@
	}
	# PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ
	$process = [ArxRemoteExitNative]::OpenProcess(0x043A, $false, $ProcessId)
	if ($process -eq [IntPtr]::Zero) { throw "OpenProcess($ProcessId) failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
	try {
		# kernel32 is at the same address in every process of this boot
		$start = [ArxRemoteExitNative]::GetProcAddress([ArxRemoteExitNative]::GetModuleHandleW('kernel32.dll'), 'ExitProcess')
		$threadId = [uint32]0
		$thread = [ArxRemoteExitNative]::CreateRemoteThread($process, [IntPtr]::Zero, [UIntPtr]::Zero, $start, [IntPtr]$Code, 0, [ref]$threadId)
		if ($thread -eq [IntPtr]::Zero) { throw "CreateRemoteThread failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
		$null = [ArxRemoteExitNative]::CloseHandle($thread)
	} finally {
		$null = [ArxRemoteExitNative]::CloseHandle($process)
	}
}

# --- A run through smoke-test.ps1 on a thread of this process ------------------------------------------------------
# The smoke test runs beside this script (a thread job: its game and the game's dump helper stay in this process's job
# object until this script ends, so the helper can finish a report after the smoke test is done). Returns the run once
# the game is in GAMEPLAY, or $null (skipped or failed: $script:skipped / a failed check says which).

function Start-ArxCheckRun([string]$What, [int]$Seconds, [string[]]$ArxArgs = @('--loadlevel', '10'), [string]$VrSim) {
	Clear-ArxLaunchMarker $testDir
	$params = @{ Seconds = $Seconds; LockTimeoutSec = 600; ArxArgs = $ArxArgs }
	if ($VrSim) { $params.VrSim = $VrSim }
	if ($Rebuild -and -not $script:rebuilt) { $params.Rebuild = $true; $script:rebuilt = $true }
	$job = Start-ThreadJob -ScriptBlock {
		param($Smoke, $Params)
		& $Smoke @Params *>&1 | ForEach-Object { "$_" }
		"SMOKE-EXIT=$LASTEXITCODE"
	} -ArgumentList $smoke, $params
	$run = [pscustomobject]@{ What = $What; Job = $job; Process = $null; Launched = Get-Date; Output = @(); SmokeExit = $null }
	if (-not (Wait-ArxGameLaunched $job $testDir)) {
		Complete-ArxCheckRun $run
		if ($run.SmokeExit -eq 2) { Write-Host "  $What`: SKIPPED (the game lock stayed busy)" -ForegroundColor Yellow; $script:skipped = $true }
		else { Check $false "$What`: the game was launched (smoke-test exit $($run.SmokeExit))" }
		return $null
	}
	$marker = Get-Content -LiteralPath (Get-ArxLaunchMarker $testDir) -Raw
	if ($marker -match 'pid=(\d+)') {
		$run.Process = Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
		# A handle now, so the exit code can still be read after the process is gone
		if ($run.Process) { $null = $run.Process.Handle }
	}
	$run.Launched = Get-Date
	try {
		$null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' } -TimeoutSec 120 -Dir $testDir -What 'GAMEPLAY'
	} catch {
		Check $false "$What`: the game reached GAMEPLAY ($_)"
		Complete-ArxCheckRun $run
		return $null
	}
	$run
}

# Wait for the smoke test to finish and keep its output
function Complete-ArxCheckRun($Run, [int]$TimeoutSec = 240) {
	$null = Wait-Job $Run.Job -Timeout $TimeoutSec
	if ($Run.Job.State -eq 'Running') { Stop-Job $Run.Job }
	$Run.Output = @(Receive-Job $Run.Job -ErrorAction SilentlyContinue | ForEach-Object { "$_" })
	Remove-Job $Run.Job -Force -ErrorAction SilentlyContinue
	$exitLine = @($Run.Output | Where-Object { $_ -match '^SMOKE-EXIT=' }) | Select-Object -Last 1
	$Run.SmokeExit = if ($exitLine -match '^SMOKE-EXIT=(-?\d+)') { [int]$Matches[1] } else { $null }
	Write-Host "  --- smoke-test.ps1 ($($Run.What)), last lines:"
	$Run.Output | Where-Object { $_ -notmatch '^SMOKE-EXIT=' } | Select-Object -Last 14 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText $_)" }
}

# Wait until a new folder with a report.txt appears in $Dir (not one of $Before)
function Wait-ArxNewReport([string]$Dir, [string[]]$Before, [int]$TimeoutSec = 30) {
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	do {
		$new = @(Get-ArxReportNames $Dir | Where-Object { $_ -notin $Before })
		if ($new | Where-Object { Test-Path -LiteralPath (Join-Path (Join-Path $Dir $_) 'report.txt') }) { return $true }
		Start-Sleep -Milliseconds 500
	} while ((Get-Date) -lt $deadline)
	$false
}

function Test-ArxUserRecorder([string]$What, [DateTime]$Since, [string[]]$Kinds = @('frame')) {
	$fresh = (Test-Path -LiteralPath $userRecorder) -and (Get-Item -LiteralPath $userRecorder).LastWriteTime -ge $Since
	Check $fresh "$What`: arx-recorder.txt in the user directory was written at the exit"
	if (-not $fresh) { return }
	$events = Read-ArxRecorderEvents $userRecorder
	foreach ($kind in $Kinds) {
		Check (@($events | Where-Object Kind -eq $kind).Count -gt 0) "$What`: arx-recorder.txt has $kind events"
	}
	$found = @(Find-ArxPrivateBytes $userRecorder)
	Check ($found.Count -eq 0) "$What`: no user path or name in arx-recorder.txt$(if ($found) { ' - found ' + ($found -join ', ') })"
}

# --- The cases ---------------------------------------------------------------------------------------------------------

function Invoke-PlainCase {
	Write-Host '==> plain: a launch without the test channel' -ForegroundColor Cyan
	$copy = Get-ArxWorkingCopyInfo $ArxSourceDir
	$stamp = Assert-ArxFreshBuild -SourceDir $ArxSourceDir -BuildDir $buildDir -Config 'Release' -Head $copy.Head -Rebuild:($Rebuild -and -not $script:rebuilt)
	$script:rebuilt = $true
	$lock = Enter-ArxGameLock -What 'flight-recorder-check.ps1 plain' -Copy $ArxSourceDir -Branch $copy.Branch -TimeoutSec 600
	if (-not $lock) { Write-Host '  plain: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; $script:skipped = $true; return }
	$crashesBefore = Get-ArxReportNames $crashDir
	$log = Get-ArxLogPath $userDir
	New-Item -ItemType Directory -Force $userDir | Out-Null
	$argList = @('--windowed', '--no-vr', '--mute') + @(Get-ArxLaunchKindArgs) + @('--user-dir', "`"$userDir`"", '--config-dir', "`"$userDir`"", '--loadlevel', '10')
	$launchTime = Get-Date
	$process = $null
	try {
		$previousHint = Set-ArxBackgroundWindow
		$previousDialogEnv = Set-ArxDialogEnv -NoDialogs
		try {
			$process = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
		} finally { Restore-ArxBackgroundWindow $previousHint; Restore-ArxDialogEnv $previousDialogEnv }
		$null = Register-ArxGameProcess $process
		Set-ArxGameLockGame $lock $process.Id
		$null = $process.Handle

		$line = $null
		$deadline = (Get-Date).AddSeconds(120)
		while (-not $line -and (Get-Date) -lt $deadline -and -not $process.HasExited) {
			if ((Test-Path -LiteralPath $log) -and (Get-Item -LiteralPath $log).LastWriteTime -ge $launchTime) {
				$line = @(Read-ArxSharedLines $log | Where-Object { $_ -match 'hang watchdog: watching the main thread, threshold \d+ ms' }) | Select-Object -First 1
			}
			Start-Sleep -Milliseconds 500
		}
		Check ([bool]$line) "plain: arx.log names the hang watchdog's start without --test-dir ($line)"
		Check (@(Get-ArxDumpHelpers).Count -eq 1) "plain: one dump helper (arx.exe --dump-helper) runs with the game ($(@(Get-ArxDumpHelpers).Count))"
		Start-Sleep -Seconds 5
		Write-Host '  closing the window (WM_CLOSE)'
		$null = $process.CloseMainWindow()
		if (-not $process.WaitForExit(60000)) { Check $false 'plain: the game quit within 60 s of WM_CLOSE'; $process.Kill() }
		$process.WaitForExit()
		Check ($process.ExitCode -eq 0) "plain: exit code 0 ($($process.ExitCode))"
		Check ([bool](Read-ArxSharedLines $log | Where-Object { $_ -match 'Clean shutdown' })) "plain: 'Clean shutdown' in arx.log"
	} finally {
		if ($process) {
			if (-not $process.HasExited) { try { $process.Kill() } catch { } }
			Add-ArxLaunchRecord -Script 'flight-recorder-check.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -Build $stamp -Vr $false -GamePid $process.Id `
				-ExitCode $(if ($process.HasExited) { $process.ExitCode } else { $null }) -DurationSec ((Get-Date) - $launchTime).TotalSeconds -ArxArgs $argList -Time $launchTime
		}
		Exit-ArxGameLock $lock
	}
	Check (Wait-ArxDumpHelpersGone 15) 'plain: the dump helper is gone within 15 s of the exit'
	$newCrashes = @(Get-ArxReportNames $crashDir | Where-Object { $_ -notin $crashesBefore })
	Check ($newCrashes.Count -eq 0) "plain: a clean quit leaves no crash report ($($newCrashes -join ', '))"
	Test-ArxUserRecorder 'plain' $launchTime @('frame')
}

function Invoke-CleanCase {
	Write-Host '==> clean: a 75 s run with a leak and a stall' -ForegroundColor Cyan
	$crashesBefore = Get-ArxReportNames $crashDir
	$hangsBefore = Get-ArxReportNames $hangDir
	$run = Start-ArxCheckRun 'clean' 75
	if (-not $run) { return }
	$launched = $run.Launched
	Check (@(Get-ArxDumpHelpers).Count -eq 1) "clean: one dump helper runs with the game ($(@(Get-ArxDumpHelpers).Count))"
	Check (Wait-ArxResourceLines 2 20) 'clean: arx-perf.log has R lines while the game runs'

	$ack = Send-ArxCommand -Lines @('mark before the leak') -Dir $testDir
	$ack = Send-ArxCommand -Lines @('leak 64') -Dir $testDir
	Check ($ack -match 'errors=0 ') "clean: 'leak 64' is acknowledged ($ack)"
	Start-Sleep -Seconds 12

	try { $ack = Send-ArxCommand -Lines @('stall 6000') -Dir $testDir -TimeoutSec 20 } catch { $ack = "$_" }
	Check ($ack -match 'errors=0 ') "clean: 'stall 6000' is acknowledged ($ack)"
	Check (Wait-ArxNewReport $hangDir $hangsBefore 30) 'clean: the stall left a report in hangs\'

	Complete-ArxCheckRun $run
	Check ($run.SmokeExit -eq 0) "clean: smoke-test.ps1 passed - a clean exit, no crash report, no [E] line (exit $($run.SmokeExit))"
	Check (Wait-ArxDumpHelpersGone 15) 'clean: the dump helper is gone within 15 s of the exit'
	$newCrashes = @(Get-ArxReportNames $crashDir | Where-Object { $_ -notin $crashesBefore })
	Check ($newCrashes.Count -eq 0) "clean: no crash report ($($newCrashes -join ', '))"
	Test-ArxUserRecorder 'clean' $launched @('frame', 'state', 'thread')

	# The R lines of the whole run
	$lines = Read-ArxSharedLines $perfLog
	$rLines = @($lines | Where-Object { $_ -like 'R *' })
	$rx = '^R t=(\d+) priv=(\d+\.\d) ws=(\d+\.\d) commit=(\d+\.\d) handles=(\d+) gdi=(\d+) user=(\d+) threads=(\d+) tex=(\d+) buf=(\d+) xrsc=(\d+)$'
	$bad = @($rLines | Where-Object { $_ -notmatch $rx })
	Check ($rLines.Count -ge 12) "clean: an R line every 5 s over the run ($($rLines.Count))"
	Check ($bad.Count -eq 0) "clean: every R line has the ten fields$(if ($bad) { ' - e.g. ' + $bad[0] })"
	$parsed = @($rLines | Where-Object { $_ -match $rx } | ForEach-Object {
		$null = $_ -match $rx
		[pscustomobject]@{ T = [int]$Matches[1]; Priv = [double]$Matches[2]; Ws = [double]$Matches[3]; Commit = [double]$Matches[4]; Handles = [int]$Matches[5]; Threads = [int]$Matches[8]; Tex = [int]$Matches[9]; Xrsc = [int]$Matches[11] }
	})
	if ($parsed.Count) {
		$last = $parsed[-1]
		Check ($last.Priv -gt 0 -and $last.Ws -gt 0 -and $last.Commit -ge $last.Priv) "clean: priv, ws and commit (commit >= priv) ($($last.Priv) / $($last.Ws) / $($last.Commit) MiB)"
		Check ($last.Handles -gt 0 -and $last.Threads -gt 1) "clean: handles and threads ($($last.Handles) / $($last.Threads))"
		Check ($last.Tex -gt 0) "clean: GL textures counted in a loaded level ($($last.Tex))"
		Check (@($parsed | Where-Object Xrsc -ne 0).Count -eq 0) 'clean: no OpenXR swapchain in a flat run'
		$gaps = @(for ($i = 1; $i -lt $parsed.Count; $i++) { $parsed[$i].T - $parsed[$i - 1].T })
		Check (@($gaps | Where-Object { $_ -lt 4 -or $_ -gt 6 }).Count -eq 0) "clean: R lines 5 s apart ($($gaps -join ' '))"
	}

	# The leak as the trend shows it
	$log = Read-ArxPerfLog $lines
	$summary = Get-ArxPerfSummary $log
	$mark = @($log.Marks | Where-Object Text -eq 'before the leak') | Select-Object -First 1
	$priv = if ($summary.PSObject.Properties['Resources'] -and $summary.Resources) { $summary.Resources['priv'] } else { $null }
	Check ($priv -and $priv.Step -ge 60) "clean: leak 64 is a step of priv (+$(if ($priv) { $priv.Step }) MiB)"
	Check ($priv -and $mark -and $priv.StepT -gt $mark.t -and $priv.StepT -le $mark.t + 11) "clean: the step right after the leak (step at t=$(if ($priv) { $priv.StepT }), leak at t=$(if ($mark) { $mark.t }))"
	$text = @(& $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'perf-summary.ps1') -Path $perfLog 2>&1 | ForEach-Object { "$_" }) -join "`n"
	$stepShown = ($text -match '(?m)^\s+priv\s.*step \+(\d+)') -and [int]$Matches[1] -ge 60
	Check $stepShown 'clean: perf-summary.ps1 shows the step of priv'

	# The stall: one hang report, with its dump
	$logLines = Read-ArxSharedLines (Get-ArxLogPath $userDir)
	Check ([bool]($logLines | Where-Object { $_ -match '\[hang\] dump written: hangs[\\/]\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d-\d\d[\\/][^\\/\s]+\.dmp' })) 'clean: arx.log names the hang dump'
	$newHangs = @(Get-ArxReportNames $hangDir | Where-Object { $_ -notin $hangsBefore })
	Check ($newHangs.Count -eq 1) "clean: one hang report for one stall ($($newHangs.Count))"
	if ($newHangs) {
		Test-ArxReportFolder (Join-Path $hangDir $newHangs[-1]) @{ What = 'clean (hang)'; Kind = 'hang'; Dump = $true; Command = 'stall 6000'; StackMatch = 'platform::stallThisThread' }
	}
}

$script:KillSpecs = [ordered]@{
	'crash'            = @{ Command = 'crash'; Kind = 'crash'; Exception = '(?i)^0xc0000005\b'; KillerMatch = 'main thread'; StackMatch = '(?m)^\s+.*\barx(\.exe)?[!+].*channel::.*execute'; Dump = $true; BoundSec = 60 }
	'abort'            = @{ Command = 'abort'; Kind = 'abort'; KillerMatch = 'main thread'; StackMatch = $script:ArxFrame; Dump = $true; BoundSec = 60 }
	'terminate'        = @{ Command = 'terminate'; Kind = 'terminate'; KillerMatch = 'main thread'; StackMatch = $script:ArxFrame; Dump = $true; BoundSec = 60 }
	'terminate-thread' = @{ Command = 'terminate thread'; Kind = 'terminate'; KillerNotMatch = 'main thread'; StackMatch = $script:ArxFrame; Dump = $true; BoundSec = 60 }
	'heapcorrupt'      = @{ Command = 'heapcorrupt'; Kind = 'crash'; Exception = '(?i)^0xc0000374\b'; KillerMatch = 'main thread'; StackMatch = $script:ArxFrame; Dump = $true; BoundSec = 60 }
	'exitprocess'      = @{ Command = 'exitprocess 143'; Kind = 'exit from outside'; ExitCode = 143; GameExit = 143; KillerMatch = '(?i)kernel32(\.dll)?!ExitProcess'; ThreadEvent = 'ExitProcess'; Dump = $true; BoundSec = $ExitBoundSec }
	'remote-exit'      = @{ Remote = $true; Kind = 'exit from outside'; ExitCode = 143; GameExit = 143; KillerMatch = '(?i)kernel32(\.dll)?!ExitProcess'; ThreadEvent = 'ExitProcess'; Dump = $true; BoundSec = $ExitBoundSec }
	'stop-process'     = @{ Stop = $true; Kind = 'terminated'; Dump = $false; BoundSec = 15 }
}

function Invoke-KillCase([string]$Name) {
	$spec = $script:KillSpecs[$Name]
	Write-Host "==> $Name" -ForegroundColor Cyan
	$crashesBefore = Get-ArxReportNames $crashDir
	$run = Start-ArxCheckRun $Name 40
	if (-not $run) { return }
	$launched = $run.Launched
	Check ($null -ne $run.Process) "$Name`: the game's process"
	if (-not $run.Process) { Complete-ArxCheckRun $run; return }
	Check (@(Get-ArxDumpHelpers).Count -eq 1) "$Name`: one dump helper runs with the game"
	# Frames recorded and a resource line written before the end
	Check (Wait-ArxResourceLines 1 20) "$Name`: arx-perf.log has an R line before the end"

	$sent = Get-Date
	if ($spec.Remote) {
		Write-Host "  CreateRemoteThread(kernel32!ExitProcess, 143) into pid $($run.Process.Id)"
		Invoke-ArxRemoteExit $run.Process.Id 143
	} elseif ($spec.Stop) {
		Write-Host "  Stop-Process -Force pid $($run.Process.Id)"
		Stop-Process -Id $run.Process.Id -Force
	} else {
		Write-Host "  > $($spec.Command)"
		Send-ArxCommandNoWait $spec.Command
	}
	$ended = $run.Process.WaitForExit($spec.BoundSec * 1000)
	$took = ((Get-Date) - $sent).TotalSeconds
	Check $ended ("$Name`: the game ended within $($spec.BoundSec) s ({0:0.0} s)" -f $took)
	if (-not $ended) {
		Stop-Process -Id $run.Process.Id -Force -ErrorAction SilentlyContinue
		$run.Process.WaitForExit()
	}
	$exitCode = $run.Process.ExitCode
	Write-Host ("  the game ended with exit code {0} (0x{0:x8})" -f $exitCode)
	if ($null -ne $spec.GameExit) { Check ($exitCode -eq $spec.GameExit) "$Name`: the game's exit code is $($spec.GameExit) - the kill was not refused ($exitCode)" }

	Complete-ArxCheckRun $run
	$null = Wait-ArxNewReport $crashDir $crashesBefore 30
	Check (Wait-ArxDumpHelpersGone 30) "$Name`: the dump helper is gone within 30 s"
	Start-Sleep -Seconds 1
	$new = @(Get-ArxReportNames $crashDir | Where-Object { $_ -notin $crashesBefore })
	Check ($new.Count -eq 1) "$Name`: exactly one new report in crashes\ ($($new -join ', '))"
	if ($new) {
		$expect = @{ What = $Name; Kind = $spec.Kind; Dump = $spec.Dump; ExitCode = $spec.ExitCode; KillerMatch = $spec.KillerMatch
			KillerNotMatch = $spec.KillerNotMatch; Exception = $spec.Exception; Command = $spec.Command; ThreadEvent = $spec.ThreadEvent; StackMatch = $spec.StackMatch }
		# A kill from outside: the report has the code the process really ended with
		if ($spec.Stop) { $expect.ExitCode = $exitCode }
		Test-ArxReportFolder (Join-Path $crashDir $new[-1]) $expect
	}
	Test-ArxUserRecorder $Name $launched @('frame')
}

function Invoke-OverheadCase {
	Write-Host '==> overhead: 60 s with the flight recorder, 60 s with --no-recorder' -ForegroundColor Cyan
	$rows = @()
	foreach ($variant in 'recorder', 'no-recorder') {
		$arxArgs = @('--loadlevel', '10')
		if ($variant -eq 'no-recorder') { $arxArgs += '--no-recorder' }
		$run = Start-ArxCheckRun "overhead ($variant)" 60 $arxArgs
		if (-not $run) { return }
		Start-Sleep -Seconds 45
		$gamePriv = if ($run.Process -and -not $run.Process.HasExited) { $run.Process.Refresh(); $run.Process.PrivateMemorySize64 / 1MB } else { 0 }
		$helperPriv = 0.0
		foreach ($helper in Get-ArxDumpHelpers) {
			$p = Get-Process -Id $helper.ProcessId -ErrorAction SilentlyContinue
			if ($p) { $helperPriv += $p.PrivateMemorySize64 / 1MB }
		}
		Complete-ArxCheckRun $run
		Check ($run.SmokeExit -eq 0) "overhead ($variant): the run passes smoke-test.ps1 (exit $($run.SmokeExit))"
		$summary = Get-ArxPerfSummary (Read-ArxPerfLog (Read-ArxSharedLines $perfLog))
		$limit = if ($summary.Budget.Contains('limit')) { [double]$summary.Budget['limit'] } else { 0.0 }
		$vrwait = if ($summary.Budget.Contains('vrwait')) { [double]$summary.Budget['vrwait'] } else { 0.0 }
		$rows += [pscustomobject]@{
			Variant = $variant; Seconds = $summary.Run.Seconds; MeanMs = [math]::Round($summary.Run.MeanMs, 3)
			BusyMs = [math]::Round($summary.Run.MeanMs - $limit - $vrwait, 3); SelfUs = [math]::Round($summary.Run.SelfUs, 1)
			SelfMaxUs = [math]::Round($summary.Run.SelfMaxUs, 0); GameMiB = [math]::Round($gamePriv, 1); HelperMiB = [math]::Round($helperPriv, 1)
			TotalMiB = [math]::Round($gamePriv + $helperPriv, 1)
		}
	}
	Write-Host '  --- for the PR body:'
	$rows | Format-Table -AutoSize | Out-String -Width 200 | ForEach-Object { Write-Host $_ }
	if ($rows.Count -eq 2) {
		$busy = $rows[0].BusyMs - $rows[1].BusyMs
		$mem = $rows[0].TotalMiB - $rows[1].TotalMiB
		Check ($busy -le 0.05) ("overhead: the recorder adds {0:0.000} ms of busy time per frame (at most 0.05)" -f $busy)
		Check ($mem -le 5) ("overhead: the recorder adds {0:0.0} MiB private memory, game and dump helper (at most 5)" -f $mem)
	}
}

# The replay ring's clip in a report folder (#550): arx-replay.vrsim, its header, frames up to $AtSec of the session
function Test-ArxReplayClipInReport([string]$What, [string]$Folder, [double]$AtSec) {
	$file = Join-Path $Folder 'arx-replay.vrsim'
	$exists = Test-Path -LiteralPath $file
	Check $exists "$What`: the report folder holds arx-replay.vrsim (the last 75 s of poses, #574)"
	if (-not $exists) { return }
	$lines = @(Read-ArxSharedLines $file)
	Check ($lines.Count -gt 0 -and $lines[0] -like '# arx-replay*') "$What`: the clip starts with its header (# arx-replay)"
	Check (@($lines | Where-Object { $_ -match '^# seed: \d+$' }).Count -eq 1 -and @($lines | Where-Object { $_ -match '^# save: \S' }).Count -eq 1) "$What`: the clip's # seed: and # save:"
	$times = @($lines | ForEach-Object { if ($_ -match '^@(\d+)ms\s') { [long]$Matches[1] } })
	Check ($times.Count -ge 90) "$What`: the clip has frames ($($times.Count))"
	if ($times.Count) {
		$span = ($times[-1] - $times[0]) / 1000.0
		Check ($span -le 76) ("$What`: at most the last 75 s, the ring (#574) ({0:0.0} s)" -f $span)
		Check ([math]::Abs($times[-1] / 1000.0 - $AtSec) -le 3) ("$What`: its last frame at the report ({0:0.0} s into the session, the command at {1:0.0} s)" -f ($times[-1] / 1000.0), $AtSec)
	}
	$report = Invoke-ArxCrashReport $Folder
	Check ($report.Text -match '(?m)^Replay clip: arx-replay\.vrsim\b') "$What`: crash-report prints 'Replay clip: arx-replay.vrsim'"
}

function Invoke-ReplayCrashCase {
	Write-Host '==> replay-crash: a VR session on the simulated headset, a stall, then a crash' -ForegroundColor Cyan
	$crashesBefore = Get-ArxReportNames $crashDir
	$hangsBefore = Get-ArxReportNames $hangDir
	$run = Start-ArxCheckRun 'replay-crash' 90 @('--loadlevel', '10') 'idle'
	if (-not $run) { return }
	$sessionStart = Get-Date
	Check ($null -ne $run.Process) 'replay-crash: the game''s process'
	if (-not $run.Process) { Complete-ArxCheckRun $run; return }
	# Two lines of one batch (the channel does not split a line at ';' - only the .xrs runner does)
	try { $ack = Send-ArxCommand -Lines @('vr-sim @+0-20000ms head rot 60 10 0', 'vr-sim @+0-20000ms hand right pos 0.3 -0.3 -0.5') -Dir $testDir } catch { $ack = "$_" }
	Check ($ack -match 'errors=0 ') "replay-crash: the head and the hand move ($ack)"
	Start-Sleep -Seconds 25

	$stallAt = ((Get-Date) - $sessionStart).TotalSeconds
	try { $ack = Send-ArxCommand -Lines @('stall 6000') -Dir $testDir -TimeoutSec 20 } catch { $ack = "$_" }
	Check ($ack -match 'errors=0 ') "replay-crash: 'stall 6000' is acknowledged ($ack)"
	Check (Wait-ArxNewReport $hangDir $hangsBefore 30) 'replay-crash: the stall left a report in hangs\'
	Start-Sleep -Seconds 3

	$crashAt = ((Get-Date) - $sessionStart).TotalSeconds
	Write-Host '  > crash'
	Send-ArxCommandNoWait 'crash'
	$ended = $run.Process.WaitForExit(60000)
	Check $ended 'replay-crash: the game ended within 60 s of the crash'
	if (-not $ended) { Stop-Process -Id $run.Process.Id -Force -ErrorAction SilentlyContinue; $run.Process.WaitForExit() }
	Complete-ArxCheckRun $run
	$null = Wait-ArxNewReport $crashDir $crashesBefore 30
	Check (Wait-ArxDumpHelpersGone 30) 'replay-crash: the dump helper is gone within 30 s'
	Start-Sleep -Seconds 1

	$newHang = @(Get-ArxReportNames $hangDir | Where-Object { $_ -notin $hangsBefore }) | Select-Object -Last 1
	$newCrash = @(Get-ArxReportNames $crashDir | Where-Object { $_ -notin $crashesBefore }) | Select-Object -Last 1
	Check ([bool]$newHang) 'replay-crash: a new report in hangs\'
	Check ([bool]$newCrash) 'replay-crash: a new report in crashes\'
	if ($newHang) { Test-ArxReplayClipInReport 'replay-crash (hang)' (Join-Path $hangDir $newHang) $stallAt }
	if ($newCrash) {
		$folder = Join-Path $crashDir $newCrash
		Test-ArxReplayClipInReport 'replay-crash (crash)' $folder $crashAt
		# The rest of the report as every crash has it, the clip included in the scan for personal data
		Test-ArxReportFolder $folder @{ What = 'replay-crash'; Kind = 'crash'; Dump = $true; Command = 'crash'; Exception = '(?i)^0xc0000005\b'; KillerMatch = 'main thread' }
	}
}

# --- Run ---------------------------------------------------------------------------------------------------------------

$previousReport = $env:ARXVR_AUTOREPORT
$env:ARXVR_AUTOREPORT = '0'
$script:rebuilt = $false
try {
	foreach ($c in $cases) {
		switch ($c) {
			'plain' { Invoke-PlainCase }
			'clean' { Invoke-CleanCase }
			'overhead' { Invoke-OverheadCase }
			'replay-crash' { Invoke-ReplayCrashCase }
			default { Invoke-KillCase $c }
		}
	}
	if ($Case -contains 'all') {
		$crashCount = @(Get-ArxReportNames $crashDir).Count
		$hangCount = @(Get-ArxReportNames $hangDir).Count
		Check ($crashCount -le 5) "only the 5 newest crash reports are kept ($crashCount in crashes\)"
		Check ($hangCount -le 5) "only the 5 newest hang reports are kept ($hangCount in hangs\)"
	}
} finally {
	$env:ARXVR_AUTOREPORT = $previousReport
}

Write-Host ''
if ($script:failed -eq 0 -and $script:skipped) { Write-Host "flight recorder check: SKIPPED (a launch could not take the game lock; $($script:passed) checks passed)" -ForegroundColor Yellow; exit 2 }
if ($script:failed -eq 0) { Write-Host "flight recorder check: PASS ($($script:passed) checks)" -ForegroundColor Green; exit 0 }
Write-Host "flight recorder check: FAIL ($($script:failed) of $($script:failed + $script:passed) checks)" -ForegroundColor Red
exit 1
