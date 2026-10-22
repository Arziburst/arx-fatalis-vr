<#
.SYNOPSIS
	Stress check of one .xrs sequence (#566): the sequence N times, one run after another, while this script loads the
	PC on purpose (busy-loop processes on k cores, or a compile of this working copy); the failures by assert, with the
	values read and what arx-perf.log shows inside each failing record's window.

.DESCRIPTION
	CI's pacing sequences (refresh-rate.xrs, @solo) fail when the PC is busy with other work - worker builds next to the
	runner: `walk90.frames` 90 for >= 121 (VR pacing UNDER-SUBMITTING 43/s on 90 slots), `span90.frames` 4 for >= 9, and
	`walk120.walked` 133.4 for <= 133.1 after one 36.5 ms update frame at the start of the walk (#566). This script
	makes that load itself, so a fix can be measured before and after:
	  -Load cpu    (default) -LoadCores busy-loop processes (pwsh, normal priority), each spinning one core; they stop
	               themselves after -LoadMaxMinutes and are killed when the runs end; they start in each run once its
	               game is up (status.json), not while the run waits for the game lock
	  -Load build  a compile of this working copy into a scratch build directory (%LOCALAPPDATA%\ArxFatalisVR\
	               stress-load\<copy>, never the copy's own build): build-windows.ps1 once (configure, vcpkg from its
	               cache), then `cmake --build --target clean` and a full build again and again, every core busy as
	               a worker's build is; the runs start once the compile has started. Removed at the end
	               (-KeepLoadBuild: kept for the next stress run, which then skips the configure)
	  -Load none   no load: the same runs on a quiet PC (the control)
	Each run is smoke-test.ps1 with the sequence's own @sim / @save / @args / @playground, a fresh user directory and its
	own test channel, one run at a time (the sequence may be @solo; another game beside it is refresh-rate-stress.ps1's
	busy-game load). The load is bounded: it never outlives this script (the job object of launch-lib.ps1, #526) and
	stops by itself after -LoadMaxMinutes.

	Only a free PC is loaded (other tasks' checks come first): before the first run, and before every run, the script
	waits (up to -QuietWaitMinutes, rechecking every 30 s) while a CI job runs (the runner marker), another game runs
	(an arx.exe not from this copy's build), another copy's ci-local.ps1, ci-smoke.ps1 or build-windows.ps1 runs, or
	anything compiles (cl.exe, link.exe, ninja.exe, MSBuild.exe not started by this script) -
	with -Load none too, so the control is quiet. When one of them starts during a run the load stops at once and that
	run is named in the summary ("another task's check overlapped it", with -Load none too); the load comes back before
	the next run, once the PC is free again.

	The run executes a copy of the sequence in -OutDir (the shipped file is never changed). The copy adds, to every
	`motion reset` batch, `mark stress-reset <record>` - the channel's mark in the same batch, the same frame, no round
	trip more - so each record's window (from the reset, its `seconds` long) can be found in arx-perf.log (-NoMarks: the
	file as it is). The variants:
	  -StallAfter <regex> -StallMs <n>   a `stall <n>` step after every line matching the regex: one long main-thread
	                                     frame early in that measurement, every time (the old failure on purpose)
	  -Edit 'old=>new'                   the first line that is exactly old replaced (a negative control: a broken game the check must catch)
	  -ExpectFail <regex>                every run must FAIL on a step matching the regex, else this script fails
	                                     (the negative control's verdict)

	Printed after every run: PASS / FAIL with the failed step and its values, each record whose window had a hitch
	(missed slots, the longest frame, every H line with its cause and top phases), the frames of the failing record's
	window, the VR pacing reports that said UNDER-SUBMITTING, and the CPU load when the run started. At the end the
	failures by assert (how many runs, every value read). Transcripts, arx.log and arx-perf.log of every run stay in
	-OutDir. Exit 0 when every run passed (with -ExpectFail: every run failed as expected), 1 otherwise, 2 when a run was
	skipped (the game lock stayed busy).

	Launches: one per run. The launch budget is 10 per task (WORKFLOW.md): agree a stress run with the coordinator
	first. Every launch follows D-014 (scripts/launch-lib.ps1): the game lock is waited for, SteamVR is never started.

.PARAMETER Sequence
	The sequence: a name in scripts\dev\xrs or a path (default refresh-rate).

.PARAMETER Runs
	How many runs (default 10).

.PARAMETER Load
	cpu (default), build or none - see above.

.PARAMETER LoadCores
	How many busy-loop processes -Load cpu starts (default: every logical processor).

.PARAMETER LoadMaxMinutes
	The load stops by itself after this long (default: 4 minutes per run plus 20).

.PARAMETER KeepLoadBuild
	-Load build: keep the scratch build directory for the next stress run.

.PARAMETER NoMarks
	Run the sequence as it is, without the marks at its `motion reset` lines (no record windows then).

.PARAMETER StallAfter
	A regex: a `stall <StallMs>` step after every matching line of the sequence.

.PARAMETER StallMs
	The length of each added stall in ms (default 0: none).

.PARAMETER Edit
	'old=>new' line replacements: the first line that is exactly old (without its comment) becomes new.

.PARAMETER ExpectFail
	A regex: every run must fail on a step matching it (a negative control).

.PARAMETER QuietWaitMinutes
	How long to wait for a free PC before a run (default 240); then the script stops with an error.

.PARAMETER OutDir
	Where the copy, the transcripts and the logs go (default %TEMP%\<sequence>-stress-<load>-<time>).

.PARAMETER DryRun
	Write and parse the sequence copy, print the plan, start no load and launch nothing.

.EXAMPLE
	.\scripts\dev\stress-sequence.ps1 -Sequence refresh-rate -Runs 10 -Load cpu
	# 10 runs while every core spins: the before / after count of #566
.EXAMPLE
	.\scripts\dev\stress-sequence.ps1 -Sequence refresh-rate -Runs 10 -Load build
	# 10 runs while this copy compiles again and again
.EXAMPLE
	.\scripts\dev\stress-sequence.ps1 -Sequence refresh-rate -Runs 3 -Load none
	# the control on a quiet PC
.EXAMPLE
	.\scripts\dev\stress-sequence.ps1 -Runs 1 -Load none -StallAfter 'record walk90$' -StallMs 400
	# one 400 ms frame inside the 90 Hz walk, every time
.EXAMPLE
	.\scripts\dev\stress-sequence.ps1 -Runs 1 -Load none -Edit 'vr-sim rate 90 clock=>vr-sim rate 45 clock' -ExpectFail 'motion\.records\.\w+\.frames ge'
	# the negative control: the 90 Hz block paced at 45 Hz must fail a frames guard
#>
[CmdletBinding()]
param(
	[string]$Sequence = 'refresh-rate',
	[ValidateRange(1, 100)]
	[int]$Runs = 10,
	[ValidateSet('cpu', 'build', 'none')]
	[string]$Load = 'cpu',
	[ValidateRange(0, 256)]
	[int]$LoadCores = [Environment]::ProcessorCount,
	[ValidateRange(0, 600)]
	[int]$LoadMaxMinutes = 0,
	[switch]$KeepLoadBuild,
	[switch]$NoMarks,
	[string]$StallAfter = '',
	[ValidateRange(0, 5000)]
	[int]$StallMs = 0,
	[string[]]$Edit = @(),
	[string]$ExpectFail = '',
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 900,
	[ValidateRange(1, 1440)]
	[int]$QuietWaitMinutes = 240,
	[string]$OutDir,
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')
. (Join-Path $PSScriptRoot 'stress-sequence-lib.ps1')
$inv = [Globalization.CultureInfo]::InvariantCulture

if (($StallMs -gt 0) -ne [bool]$StallAfter) { throw '-StallAfter and -StallMs come together (a line regex and a length in ms)' }
if ($StallAfter) { $null = [regex]::new($StallAfter) }
if ($ExpectFail) { $null = [regex]::new($ExpectFail) }
if ($Load -eq 'cpu' -and $LoadCores -lt 1) { throw '-Load cpu needs -LoadCores 1 or more' }
if (-not $LoadMaxMinutes) { $LoadMaxMinutes = 4 * $Runs + 20 }

# --- The sequence copy ------------------------------------------------------------------------------------------
$source = Resolve-ArxSequence $Sequence
$name = [IO.Path]::GetFileNameWithoutExtension($source)
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("$name-stress-$Load-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$copyInfo = New-StressSequenceCopy -Lines ([IO.File]::ReadAllLines($source)) -Marks:(-not $NoMarks) -StallAfter $StallAfter -StallMs $StallMs -Edits $Edit
$copyDir = Join-Path $OutDir 'sequence'
New-Item -ItemType Directory -Force $copyDir | Out-Null
$copy = Join-Path $copyDir "$name.xrs"
[IO.File]::WriteAllLines($copy, $copyInfo.Lines)
$parsed = Read-ArxSequence $copy   # a typo must not cost a launch
$stalls = @($copyInfo.Origin | Where-Object { $_ -eq 0 }).Count
if ($StallAfter -and -not $stalls) { throw "-StallAfter '$StallAfter' matches no line of $source" }

$loadText = switch ($Load) {
	'cpu' { "$LoadCores busy-loop process(es) on $([Environment]::ProcessorCount) logical processors" }
	'build' { 'a compile of this working copy, again and again' }
	'none' { 'no load' }
}
$variant = @()
if ($stalls) { $variant += "a $StallMs ms stall after $stalls line(s) matching /$StallAfter/" }
foreach ($e in $Edit) { $variant += "edit '$e'" }
if ($ExpectFail) { $variant += "every run must fail on /$ExpectFail/" }
$variantText = if ($variant) { $variant -join '; ' } else { 'as shipped' }
Write-Host "stress: $name ($variantText), $Runs run(s) one at a time, load: $loadText (at most $LoadMaxMinutes min); $Runs launch(es)"
Write-Host "stress: sequence copy $copy ($($parsed.Steps.Count) steps; marked records: $(if ($copyInfo.Records.Count) { $copyInfo.Records -join ' ' } else { 'none' }))"
Write-Host "stress: evidence in $OutDir"
if ($DryRun) { Write-Host 'stress: dry run - no load, nothing launched'; exit 0 }

# --- The load ---------------------------------------------------------------------------------------------------
# Everything started from here on (the load, the runs' jobs, their games) dies with this script (#526)
$null = Register-ArxLauncherChain
$env:ARXVR_AUTOREPORT = '0'
$key = Get-WorkingCopyKey $ArxSourceDir
$pwsh = (Get-Process -Id $PID).Path
$loadStop = Join-Path $OutDir 'load-stop'
Remove-Item -LiteralPath $loadStop -Force -ErrorAction SilentlyContinue
$loadProcs = @()
$loadLog = Join-Path $OutDir 'load-build.log'
$loadBuildDir = Join-Path (Get-ArxAppDataDir) "stress-load\$key"

function Start-StressProcess([string]$Code) {
	$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($Code))
	Start-Process -FilePath $pwsh -ArgumentList '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded -WindowStyle Hidden -PassThru
}
function Get-StressCpuPercent {
	try { [int](Get-CimInstance Win32_Processor -ErrorAction Stop | Measure-Object LoadPercentage -Average).Average } catch { -1 }
}
function Test-StressLoadAlive { @($loadProcs | Where-Object { -not $_.HasExited }).Count }
function Stop-StressLoad {
	if (-not $loadProcs) { return }
	New-Item -ItemType File -Force $loadStop | Out-Null
	foreach ($p in $loadProcs) {
		if (-not $p.HasExited) { & taskkill.exe /PID $p.Id /T /F *> $null }
	}
	foreach ($p in $loadProcs) { try { $null = $p.WaitForExit(10000) } catch { } }
	$left = @($loadProcs | Where-Object { -not $_.HasExited })
	Write-Host "stress: load stopped$(if ($left) { " - $($left.Count) process(es) still running: $($left.Id -join ', ')" })" -ForegroundColor $(if ($left) { 'Yellow' } else { 'Gray' })
	$script:loadProcs = @()
}

$deadline = (Get-Date).ToUniversalTime().AddMinutes($LoadMaxMinutes).ToString('o')
$ownBuildDir = Get-DefaultBuildDir $ArxSourceDir $Config

# Why the PC is busy now ('' when free); this script's own games and load never count
function Get-StressBusyNow {
	$procs = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
	# This script with every process it started: its load (the build load's ninja, cl.exe, link.exe) and its runs
	$own = [Collections.Generic.HashSet[int]]::new()
	$null = $own.Add($PID)
	foreach ($l in $script:loadProcs) { $null = $own.Add($l.Id) }
	do {
		$grew = $false
		foreach ($p in $procs) { if ($own.Contains([int]$p.ParentProcessId) -and $own.Add([int]$p.ProcessId)) { $grew = $true } }
	} while ($grew)
	Get-StressBusyReason $procs (Get-ArxRunnerMarker) $ownBuildDir ([int[]]@($own))
}

# Wait until no CI job, other game, ci-local / ci-smoke or build runs: other tasks' checks come first, and a run
# measured beside them would not be the load this script makes
function Wait-StressQuiet([string]$What) {
	$since = Get-Date
	$said = [DateTime]::MinValue
	while ($true) {
		$busy = Get-StressBusyNow
		if (-not $busy) {
			if ($said -ne [DateTime]::MinValue) { Write-Host "stress: the PC is free after $([int]((Get-Date) - $since).TotalMinutes) min ($(Get-Date -Format 'HH:mm'))" }
			return
		}
		if ((Get-Date) - $since -gt [TimeSpan]::FromMinutes($QuietWaitMinutes)) { throw "the PC stayed busy for $QuietWaitMinutes min before $What ($busy)" }
		if ((Get-Date) - $said -gt [TimeSpan]::FromMinutes(5)) { Write-Host "stress: waiting before $What ($(Get-Date -Format 'HH:mm')) - the PC is busy: $busy"; $said = Get-Date }
		Start-Sleep -Seconds 30
	}
}

function Start-StressLoad {
	Remove-Item -LiteralPath $loadStop -Force -ErrorAction SilentlyContinue
	if ($Load -eq 'cpu') {
		$spin = @"
`$end = [DateTime]::Parse('$deadline').ToUniversalTime()
`$stop = '$loadStop'
`$n = 0
while ([DateTime]::UtcNow -lt `$end -and -not (Test-Path -LiteralPath `$stop)) {
	for (`$i = 0; `$i -lt 200000; `$i++) { `$n = (`$n * 7 + `$i) % 1000003 }
}
"@
		$script:loadProcs = @(1..$LoadCores | ForEach-Object { Start-StressProcess $spin })
		Start-Sleep -Seconds 2
		Write-Host "stress: load on - $(Test-StressLoadAlive) busy-loop process(es), CPU $(Get-StressCpuPercent) %"
	} elseif ($Load -eq 'build') {
		$build = Join-Path $ArxSourceDir 'scripts\build-windows.ps1'
		$builder = @"
`$ErrorActionPreference = 'Continue'
`$end = [DateTime]::Parse('$deadline').ToUniversalTime()
`$stop = '$loadStop'
`$log = '$loadLog'
`$dir = '$loadBuildDir'
& '$build' -Config Release -BuildDir `$dir -SkipTests *>&1 | ForEach-Object { "`$_" } | Out-File -LiteralPath `$log -Append -Encoding utf8
`$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not `$cmake) { `$cmake = Join-Path `$env:ProgramFiles 'CMake\bin\cmake.exe' }
`$round = 1
while ([DateTime]::UtcNow -lt `$end -and -not (Test-Path -LiteralPath `$stop)) {
	`$round++
	"==> load round `${round}: clean and build" | Out-File -LiteralPath `$log -Append -Encoding utf8
	& `$cmake --build `$dir --target clean *>&1 | Out-Null
	& `$cmake --build `$dir *>&1 | Select-Object -Last 3 | ForEach-Object { "`$_" } | Out-File -LiteralPath `$log -Append -Encoding utf8
}
"@
		$script:loadProcs = @(Start-StressProcess $builder)
		Write-Host "stress: load starting - a compile into $loadBuildDir (log $loadLog)"
		$waitUntil = (Get-Date).AddMinutes(30)
		while ($true) {
			$text = if (Test-Path -LiteralPath $loadLog) { Get-Content -LiteralPath $loadLog -Raw } else { '' }
			if ($text -match '==> Building|==> load round') { break }
			if (-not (Test-StressLoadAlive)) { throw "the build load ended before it compiled - see $loadLog" }
			if ((Get-Date) -gt $waitUntil) { throw "the build load did not start compiling within 30 min - see $loadLog" }
			Start-Sleep -Seconds 2
		}
		Start-Sleep -Seconds 10
		Write-Host "stress: load on - the compile runs, CPU $(Get-StressCpuPercent) %"
	}
}

$results = @()
try {
	Wait-StressQuiet 'the first run'

	# --- The runs ---------------------------------------------------------------------------------------------------
	$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
	$userRoot = Join-Path (Get-ArxAppDataDir) "userdirs\$key-stress"
	$testDir = "$(Get-ArxTestDir)-stress"
	$job = {
		param([string]$Smoke, [hashtable]$Params, [string]$Transcript, [string]$TestDir)
		$ErrorActionPreference = 'Stop'
		[Console]::OutputEncoding = [Text.Encoding]::UTF8
		$env:ARXVR_TEST_DIR = $TestDir
		$global:LASTEXITCODE = 1
		try {
			& $Smoke @Params *>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $Transcript -Encoding utf8
			$LASTEXITCODE
		} catch {
			"stress: the run threw: $_" | Out-File -LiteralPath $Transcript -Append -Encoding utf8
			1
		}
	}
	foreach ($run in 1..$Runs) {
		$tag = 'run{0:00}' -f $run
		New-Item -ItemType Directory -Force $testDir | Out-Null
		foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'launched.txt', 'hang.txt') {
			Remove-Item (Join-Path $testDir $f) -Force -ErrorAction SilentlyContinue
		}
		$runUserDir = New-ArxSequenceUserDir $userRoot $tag
		$p = @{ Config = $Config; Sequence = $copy; LockTimeoutSec = $LockTimeoutSec; TestDir = $testDir; UserDir = $runUserDir
			EvidenceDir = (Join-Path $OutDir "$tag-evidence") }
		if ($DataDir) { $p.DataDir = $DataDir }
		if ($parsed.Sim) { $p.VrSim = $parsed.Sim } elseif ($parsed.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
		if ($parsed.Playground) { $p.Playground = $true }
		if ($parsed.Save) { $p.Save = $parsed.Save }
		if ($parsed.Args) { $p.ArxArgs = @($parsed.Args) }
		# Load only a free PC: when another task's check started, the load stops until it is gone
		$busy = Get-StressBusyNow
		if ($busy) {
			Write-Host "stress: the PC is busy before run $run ($busy)"
			Stop-StressLoad
			Wait-StressQuiet "run $run"
		}
		if ($Load -eq 'build' -and -not (Test-StressLoadAlive)) { Start-StressLoad }
		$alive = if ($Load -eq 'build') { ", load processes alive $(Test-StressLoadAlive)" } else { '' }
		$cpu = Get-StressCpuPercent
		Write-Host "stress: run $run started (CPU $cpu %$alive)"
		$started = Get-Date
		$transcript = Join-Path $OutDir "$tag.log"
		$j = Start-Job -ScriptBlock $job -ArgumentList $smoke, $p, $transcript, $testDir
		# The load runs while this run's game runs: the busy-loop load starts once the game is up (its status.json; the
		# game lock may keep the run waiting first), the build load is already compiling. Another task's check that
		# starts meanwhile (recorded with -Load none too) stops the load, and the run is named in the summary.
		$runBusy = ''
		$gameAt = $null
		$statusFile = Join-Path $testDir 'status.json'
		while ($j.State -eq 'Running') {
			Start-Sleep -Seconds 2
			if (-not $gameAt) {
				if (-not (Test-Path -LiteralPath $statusFile)) { continue }
				$gameAt = Get-Date
			}
			if ($runBusy) { continue }
			$after = [int]((Get-Date) - $gameAt).TotalSeconds
			$busy = Get-StressBusyNow
			if ($busy) {
				$runBusy = "$busy, +$after s after the game started"
				Write-Host "stress: the PC got busy during run $run ($runBusy)$(if (Test-StressLoadAlive) { ' - the load stops' })"
				Stop-StressLoad
			} elseif ($Load -eq 'cpu' -and -not (Test-StressLoadAlive)) {
				Start-StressLoad
				Write-Host "         (the load came on +$after s after the game started)"
			}
		}
		$out = @(Receive-Job $j -Wait -AutoRemoveJob -ErrorAction SilentlyContinue)
		$exit = if ($out.Count -and $out[-1] -is [int]) { [int]$out[-1] } else { 1 }
		$text = if (Test-Path -LiteralPath $transcript) { Get-Content -LiteralPath $transcript -Raw } else { '' }
		$failure = Get-StressFailure $text $copyInfo.Origin
		$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
		$reason = if ($failure) {
			$where = if ($failure.Line) { "line $($failure.Line)" } else { 'a step added by this script' }
			"$where '$($failure.Step)': $($failure.Message)"
		} elseif ($exit -ne 0) {
			"$(@($text -split "`r?`n" | Where-Object { $_ -match 'FAIL|SKIPPED|threw' }) | Select-Object -Last 1)".Trim()
		}
		Write-Host ('stress: run {0,2}: {1} ({2:0} s){3}' -f $run, $verdict, ((Get-Date) - $started).TotalSeconds, $(if ($reason) { " - $reason" })) `
			-ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } elseif ($verdict -eq 'SKIPPED') { 'Yellow' } else { 'Red' })

		# What the logs say: the records, their windows in arx-perf.log, the VR pacing reports
		$windows = @()
		$under = @()
		$log = Join-Path $runUserDir 'arx.log'
		$perfPath = Join-Path $runUserDir 'arx-perf.log'
		$failedRecord = if ($failure -and $failure.Path -match '^motion\.records\.(\w+)\.') { $Matches[1] } else { '' }
		if (Test-Path -LiteralPath $log) {
			Copy-Item -LiteralPath $log -Destination (Join-Path $OutDir "$tag-arx.log")
			$logText = Get-Content -LiteralPath $log -Raw
			$records = Get-StressRecords $logText
			$frames = @($records | Where-Object { $_.Values.PSObject.Properties['frames'] } | ForEach-Object { "$($_.Name)=$($_.Values.frames)" })
			if ($frames) { Write-Host "         frames: $($frames -join ' ')" }
			$under = @([regex]::Matches($logText, '\[vr\] rate: [^\r\n]*= ([\d.]+)/s on ([\d.]+) slots/s[^\r\n]*UNDER-SUBMITTING') | ForEach-Object { "$($_.Groups[1].Value)/s on $($_.Groups[2].Value)" })
			if ($under) { Write-Host "         VR pacing UNDER-SUBMITTING $($under.Count)x: $($under -join ', ')" -ForegroundColor Yellow }
			if (Test-Path -LiteralPath $perfPath) {
				Copy-Item -LiteralPath $perfPath -Destination (Join-Path $OutDir "$tag-arx-perf.log")
				$perf = Read-ArxPerfLog ([IO.File]::ReadAllLines($perfPath))
				foreach ($r in $records) {
					if (-not $r.Values.PSObject.Properties['seconds']) { continue }
					$w = Get-StressWindow $perf $r.Name ([double]$r.Values.seconds)
					if (-not $w) { continue }
					$windows += [pscustomobject]@{ Record = $r.Name; Missed = $w.Missed; Longest = $w.Longest; Hitches = $w.Hitches.Count; Frames = [int]$r.Values.frames }
					if ($w.Hitches.Count -or $r.Name -eq $failedRecord) {
						Write-Host "         $(@(Format-StressWindow $w) -join "`n           ")" -ForegroundColor $(if ($r.Name -eq $failedRecord) { 'Red' } else { 'Gray' })
					}
				}
			}
		}
		$results += [pscustomobject]@{ Run = $run; Verdict = $verdict; Failure = $failure; Reason = $reason; Cpu = $cpu; Windows = $windows; Under = $under.Count; LoadCut = $runBusy; Started = $started }
	}
} finally {
	Stop-StressLoad
	if ($Load -eq 'build' -and -not $KeepLoadBuild -and (Test-Path -LiteralPath $loadBuildDir)) {
		Remove-Item -LiteralPath $loadBuildDir -Recurse -Force -ErrorAction SilentlyContinue
		if (Test-Path -LiteralPath $loadBuildDir) { Write-Host "stress: could not remove the scratch build $loadBuildDir" -ForegroundColor Yellow }
	}
}

# --- The verdict ------------------------------------------------------------------------------------------------
$passed = @($results | Where-Object Verdict -eq 'PASS').Count
$failed = @($results | Where-Object Verdict -eq 'FAIL')
$skipped = @($results | Where-Object Verdict -eq 'SKIPPED').Count
Write-Host ''
Write-Host "stress: $name ($variantText), load ${Load}: $passed passed, $($failed.Count) failed, $skipped skipped of $Runs"
Write-Host 'Failures by assert:'
foreach ($l in (Format-StressAssertTable (Get-StressAssertTable $results) $Runs)) { Write-Host "  $l" }
$hit = @($results | ForEach-Object { $_.Windows } | Where-Object { $_.Missed -gt 0 })
if ($hit) {
	Write-Host 'Record windows with missed display slots (run: record missed / longest frame):'
	foreach ($r in $results) {
		$ws = @($r.Windows | Where-Object { $_.Missed -gt 0 })
		if ($ws) { Write-Host ("  run {0,2} {1}: {2}" -f $r.Run, $r.Verdict, (($ws | ForEach-Object { [string]::Format($inv, '{0} {1:0} / {2:0.0} ms', $_.Record, $_.Missed, $_.Longest) }) -join ', ')) }
	}
}
$cut = @($results | Where-Object LoadCut)
foreach ($r in $cut) { Write-Host "  run $($r.Run): another task's check overlapped it, the load (if any) stopped ($($r.LoadCut))" -ForegroundColor Yellow }
Write-Host "Runs started: $((@($results | ForEach-Object { $_.Started.ToString('HH:mm') })) -join ' ')"
$underRuns = @($results | Where-Object { $_.Under -gt 0 }).Count
Write-Host "VR pacing UNDER-SUBMITTING in $underRuns of $($results.Count) run(s); CPU at the runs' start: $((@($results | ForEach-Object { "$($_.Cpu)%" })) -join ' ')"
if ($skipped) { Write-Host 'stress: SKIPPED - a run did not get the game lock; a check that did not run is not a pass' -ForegroundColor Yellow; exit 2 }
if ($ExpectFail) {
	$caught = @($failed | Where-Object { $_.Failure -and $_.Failure.Line -gt 0 -and $_.Failure.Step -match $ExpectFail }).Count
	if ($caught -eq $Runs) { Write-Host "stress: PASS - every run failed on /$ExpectFail/: the check catches it" -ForegroundColor Green; exit 0 }
	Write-Host "stress: FAIL - $($Runs - $caught) of $Runs run(s) did not fail on /$ExpectFail/: the check misses it" -ForegroundColor Red
	exit 1
}
if ($failed.Count) {
	Write-Host "stress: FAIL - $($failed.Count) of $Runs run(s) failed; transcripts run<NN>.log, logs run<NN>-arx*.log in $OutDir" -ForegroundColor Red
	exit 1
}
Write-Host "stress: PASS - $Runs of $Runs" -ForegroundColor Green
exit 0
