<#
.SYNOPSIS
	Stress check of melee-damage.xrs's aim asserts (#613): the sequence N times beside a bounded CPU load; FAIL on any
	failed run, each failure named by its assert with the value it got, every swing hit's aim and the frame rate listed.

.DESCRIPTION
	melee-damage.xrs failed the gate twice on 2026-10-22 while the PC was busy (28-44 submits/s on 90 slots):
	line 128 `@assert vr.melee.last.aim eq {full}` got 0.454, line 107 `@assert vr.melee.last.aim ge 0.99` got 0.509.
	The cause (#613): the sequence's swings are ramps counted in frames (`vr-sim @+1-8 hand right pos ...`: 0.85 m over
	8 frames, 9.6 m/s at 90 frames/s), while the game measures a swing's speed over the frames' predicted display time
	(VRMelee's meleeClock). idle.vrsim, the sequences' @sim, is a 90 Hz cap: a late frame costs its real length. At
	30 frames/s the same 8 frames last 267 ms - a 3.2 m/s swing, aim 0.1 + 0.9 * (3.2 - 2) / 3 = 0.46.

	The check, by default: -Runs runs (10) of the sequence one after the other, each while -Cores busy loops (default:
	one per logical processor) run beside it at normal priority, started only once the PC is quiet (-QuietTimeoutMin) - the load a worker's compile puts on the PC, bounded
	(each loop ends by itself after -LoadMinutes, and with this script). -Load game: a busy second game instead (the
	playground on the idle simulated headset, as melee-damage-stress.ps1 runs it); -Load none: no load (the baseline).
	Each run is smoke-test.ps1 with the sequence's own @sim / @playground, a fresh user directory and its own test
	channel. After each run its arx.log gives every swing hit (`melee.hit source=player ... aim= speed=`) and the
	frame rate the VR pacing lines measured (the slowest one). Exit 0 when every run passed, 1 when one failed, 2 when
	a run was skipped (the game lock stayed busy) or the load could not start.

	The variants run a copy of the sequence, made in -OutDir (the shipped file is never changed):
	  -SlowRate <hz>    the old failure every time: the simulated headset capped at <hz> (`vr-sim rate <hz>`, idle's
	                    cap pacing) for each fast swing - a right-hand `pos` ramp of at most 12 frames or 150 ms - from
	                    3 frames before it until the swing settled (the next `@until vr.melee.swinging eq false` or
	                    `@frames`), then 90 Hz again. -SlowWhich all: every right-hand ramp (the medium swings and the
	                    set-up moves too). A sequence whose swings are the same motion at any frame rate passes it.
	                    -SlowSpan <a>-<b>: instead, the cap from before the original line a to after line b - e.g.
	                    241-250, group 8's fight kept through a frame-counted sweep and wait (line 250, the third
	                    sighting: `@assert playground.npcFighting eq true` false under load): its 135 frames are 1.5 s
	                    of game time at 90 Hz, 4.5 s at 30 - the goblin's own script has three times as long to act.
	  -NegativeControl  the broken game the check must still catch: right before the swing whose aim is asserted
	                    `ge 0.99` (the last right-hand ramp before the first such assert) the dagger's slash speed range
	                    is set to 2-12 m/s - a 9.6 m/s swing then deals about 0.78. Every run must FAIL on a
	                    `vr.melee.last.aim` assert of the sequence itself; a run that passes, or fails elsewhere, fails
	                    this script: the aim asserts would miss a weak full swing.

	Launches: one per run (-Load game: one more). The launch budget is 10 per task (WORKFLOW.md): agree a stress run
	with the coordinator first. Every launch follows D-014 (scripts/launch-lib.ps1); never starts SteamVR.

.PARAMETER Runs
	How many runs of the sequence (default 10).

.PARAMETER Load
	cpu (default): -Cores busy loops; game: a busy second game; none: no load.

.PARAMETER Cores
	How many busy loops for -Load cpu (default: the logical processor count).

.PARAMETER LoadMinutes
	Each busy loop ends by itself after this many minutes (default 90), should this script be killed outright.

.PARAMETER QuietTimeoutMin
	How long to wait for a quiet PC before a loaded run (default 120 min; then exit 2). A loaded run starts only while
	no CI job (the runner marker, Runner.Worker), no other game and no ci-local / ci-smoke / other stress script runs.

.PARAMETER SlowRate
	Cap the simulated headset at this rate during the swings (default 0: the sequence as shipped). See above.

.PARAMETER SlowWhich
	fast (default): only the fast swings run at -SlowRate; all: every right-hand ramp.

.PARAMETER SlowSpan
	'<a>-<b>': -SlowRate from before the original line a to after line b instead of around the swings.

.PARAMETER NegativeControl
	The fast swing's speed range out of reach before the first `aim ge 0.99` assert: every run must FAIL on it.

.PARAMETER Sequence
	The sequence (default melee-damage).

.PARAMETER OutDir
	Where the transcripts, the evidence of failed runs and the copied sequence go
	(default %TEMP%\<sequence>-aim-stress-<time>).

.PARAMETER DryRun
	Write and parse the sequence copy, print what would run, launch nothing.

.EXAMPLE
	.\scripts\dev\stress-melee-aim.ps1
	# the contract's check: 10 runs beside one busy loop per logical processor
.EXAMPLE
	.\scripts\dev\stress-melee-aim.ps1 -Runs 3 -Load none
	# the baseline without load
.EXAMPLE
	.\scripts\dev\stress-melee-aim.ps1 -Runs 1 -Load none -SlowRate 30
	# the fast swings at 30 frames/s: the line 107 failure (aim about 0.46), every time
.EXAMPLE
	.\scripts\dev\stress-melee-aim.ps1 -Runs 1 -Load none -NegativeControl
	# a weak full swing: every run must fail on an aim assert
#>
[CmdletBinding()]
param(
	[ValidateRange(1, 100)]
	[int]$Runs = 10,
	[ValidateSet('cpu', 'game', 'none')]
	[string]$Load = 'cpu',
	[ValidateRange(1, 256)]
	[int]$Cores = [Environment]::ProcessorCount,
	[ValidateRange(1, 600)]
	[int]$LoadMinutes = 90,
	[ValidateRange(1, 600)]
	[int]$QuietTimeoutMin = 120,
	[ValidateRange(0, 144)]
	[int]$SlowRate = 0,
	[ValidateSet('fast', 'all')]
	[string]$SlowWhich = 'fast',
	[ValidatePattern('^(\d+-\d+)?$')]
	[string]$SlowSpan = '',
	[switch]$NegativeControl,
	[string]$Sequence = 'melee-damage',
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 600,
	[string]$OutDir,
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')

# --- The sequence copy ------------------------------------------------------------------------------------------
# Each line of the copy with the line of the original it is (0: a line added here), so a failure names the original
$source = Resolve-ArxSequence $Sequence
$name = [IO.Path]::GetFileNameWithoutExtension($source)
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("$name-aim-stress-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$sourceLines = [IO.File]::ReadAllLines($source)
$codes = @($sourceLines | ForEach-Object { ($_ -replace '\s+#(\s.*)?$', '').Trim() })

# A right-hand position ramp on a line: its span and unit, $null when the line has none
$rampPattern = 'vr-sim\s+@\+?(\d+)-(\d+)(ms)?\s+hand\s+right\s+pos\b'
function Get-Ramp([string]$Code) {
	if ($Code.StartsWith('#') -or $Code -notmatch $rampPattern) { return $null }
	$span = [int]$Matches[2] - [int]$Matches[1]
	$ms = [bool]$Matches[3]
	[pscustomobject]@{ Span = $span; Ms = $ms; Fast = $(if ($ms) { $span -le 150 } else { $span -le 12 }) }
}

# The negative control's hook: the last right-hand ramp before the first `@assert vr.melee.last.aim ge 0.99`
$controlAt = 0
if ($NegativeControl) {
	$assertAt = 0
	for ($i = 0; $i -lt $codes.Count; $i++) {
		if ($codes[$i] -match '^@assert\s+vr\.melee\.last\.aim\s+ge\s+0\.99\b') { $assertAt = $i + 1; break }
	}
	for ($i = $assertAt - 1; $i -ge 1 -and -not $controlAt; $i--) {
		if (Get-Ramp $codes[$i - 1]) { $controlAt = $i }
	}
	if (-not $controlAt) {
		throw "$source has no right-hand ramp before an '@assert vr.melee.last.aim ge 0.99' line: the negative control has nothing to hook into - the sequence changed, ask the coordinator which assert proves a full swing now"
	}
}

$lines = [Collections.Generic.List[string]]::new()
$origin = [Collections.Generic.List[int]]::new()
function Add-Line([string]$Text, [int]$From = 0) { $lines.Add($Text); $origin.Add($From) }
$slowOpen = $false
$slowSwings = 0
$spanFrom = 0
$spanTo = 0
if ($SlowSpan) {
	$spanFrom, $spanTo = $SlowSpan -split '-' | ForEach-Object { [int]$_ }
	if ($spanFrom -lt 1 -or $spanTo -lt $spanFrom -or $spanTo -gt $sourceLines.Count) { throw "-SlowSpan $SlowSpan is not a span of lines of $source (1-$($sourceLines.Count))" }
}
for ($i = 0; $i -lt $sourceLines.Count; $i++) {
	$n = $i + 1
	$code = $codes[$i]
	$ramp = Get-Ramp $code
	if ($NegativeControl -and $n -eq $controlAt) {
		# The broken game: a full-speed swing deals about 0.78, under the full strike the asserts ask for
		Add-Line 'melee slash dagger 0 0 2 12'
		Add-Line '@until vr.melee.params.slash.speedMax eq 12 5s'
	}
	if ($SlowRate -gt 0 -and $spanFrom -and $n -eq $spanFrom) {
		Add-Line "vr-sim rate $SlowRate"
		Add-Line '@frames 3'
		$slowSwings++
	}
	if ($SlowRate -gt 0 -and -not $spanFrom -and $ramp -and ($SlowWhich -eq 'all' -or $ramp.Fast) -and -not $slowOpen) {
		Add-Line "vr-sim rate $SlowRate"
		Add-Line '@frames 3'
		$slowOpen = $true
		$slowSwings++
	}
	Add-Line $sourceLines[$i] $n
	if ($SlowRate -gt 0 -and $spanFrom -and $n -eq $spanTo) {
		Add-Line 'vr-sim rate 90'
		Add-Line '@frames 3'
	}
	if ($slowOpen -and -not $ramp -and $code -match '^@(until\s+vr\.melee\.swinging\s+eq\s+false|frames)\b') {
		Add-Line 'vr-sim rate 90'
		Add-Line '@frames 3'
		$slowOpen = $false
	}
}
if ($slowOpen) { Add-Line 'vr-sim rate 90' }
if ($SlowRate -gt 0 -and -not $slowSwings) {
	throw "$source has no right-hand ramp ($SlowWhich): -SlowRate has nothing to slow down - the sequence changed, ask the coordinator how its swings are made now"
}
$copyDir = Join-Path $OutDir 'sequence'
New-Item -ItemType Directory -Force $copyDir | Out-Null
$copy = Join-Path $copyDir "$name.xrs"
[IO.File]::WriteAllLines($copy, $lines)
$parsed = Read-ArxSequence $copy   # a typo must not cost a launch

# The original's line of a line of the copy; 0 for a line added here
function Get-OriginalLine([int]$CopyLine) {
	if ($CopyLine -lt 1 -or $CopyLine -gt $origin.Count) { return 0 }
	$origin[$CopyLine - 1]
}

$variant = @()
if ($SlowRate -and $spanFrom) { $variant += "lines $SlowSpan at $SlowRate Hz" }
elseif ($SlowRate) { $variant += "$slowSwings $SlowWhich swing(s) at $SlowRate Hz" }
if ($NegativeControl) { $variant += "negative control: slash speed range 2-12 before line $controlAt" }
$variantText = if ($variant) { $variant -join '; ' } else { 'as shipped' }
$loadText = switch ($Load) { 'cpu' { "$Cores busy loop(s)" } 'game' { 'a busy second game' } default { 'no load' } }
Write-Host "stress: $name ($variantText), $Runs run(s) beside $loadText; $($Runs + [int]($Load -eq 'game')) launch(es)"
Write-Host "stress: sequence copy $copy ($($parsed.Steps.Count) steps)"
Write-Host "stress: evidence in $OutDir"
if ($DryRun) { Write-Host 'stress: dry run - nothing launched'; exit 0 }

# --- The load and the runs --------------------------------------------------------------------------------------
# The runs, their games and the busy loops die with this script (#526)
$null = Register-ArxLauncherChain
$env:ARXVR_AUTOREPORT = '0'
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$key = Get-WorkingCopyKey $ArxSourceDir
$userRoot = Join-Path (Get-ArxAppDataDir) "userdirs\$key-aim-stress"
$testRoot = Get-ArxTestDir
$dataArgs = @{}
if ($DataDir) { $dataArgs.DataDir = $DataDir }

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

function Clear-Channel([string]$Dir) {
	New-Item -ItemType Directory -Force $Dir | Out-Null
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'launched.txt', 'hang.txt') {
		Remove-Item (Join-Path $Dir $f) -Force -ErrorAction SilentlyContinue
	}
}

# What a run's arx.log says: every swing hit's aim and speed, the slowest frame rate of the VR pacing lines
function Read-RunLog([string]$Log) {
	$hits = @()
	$rates = @()
	if (Test-Path -LiteralPath $Log) {
		foreach ($line in [IO.File]::ReadAllLines($Log)) {
			if ($line -match 'melee\.hit source=player\b.*?\baim=([\d.]+)(?:\s+speed=([\d.]+))?') {
				$hits += if ($Matches[2]) { "$($Matches[1])@$($Matches[2])" } else { $Matches[1] }
			}
			if ($line -match '= ([\d.]+)/s on ([\d.]+) slots/s') {
				$rates += [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
			}
		}
	}
	[pscustomobject]@{
		Hits = $hits
		MinFps = $(if ($rates) { ($rates | Measure-Object -Minimum).Minimum } else { $null })
	}
}

# The PC is loaded on purpose only while nothing else runs on it (the coordinator, 2026-10-22: other tasks' checks fail
# from load): no live CI runner marker, no CI runner job, no other game, no ci-local / ci-smoke / other stress script
function Get-BusyReason {
	$reasons = @()
	$marker = Get-ArxRunnerMarker
	if ($marker) { $reasons += "the CI runner marker is up ($($marker.What))" }
	if (Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue) { $reasons += 'a CI runner job runs' }
	$games = @(Get-Process -Name 'arx' -ErrorAction SilentlyContinue)
	if ($games) { $reasons += "$($games.Count) game(s) run" }
	$shells = @(Get-CimInstance Win32_Process -Filter "Name='pwsh.exe' OR Name='powershell.exe'" -ErrorAction SilentlyContinue)
	# This script and the shells that started it are not someone else's run
	$mine = @($PID)
	$parent = $PID
	for ($i = 0; $i -lt 10 -and $parent; $i++) {
		$parent = (Get-CimInstance Win32_Process -Filter "ProcessId=$parent" -ErrorAction SilentlyContinue).ParentProcessId
		if ($parent) { $mine += $parent }
	}
	$scripts = @($shells | Where-Object { $mine -notcontains $_.ProcessId } | ForEach-Object {
		if ("$($_.CommandLine)" -match '\b(ci-local|ci-smoke|stress-[\w-]+|[\w-]+-stress)\.ps1') { $Matches[1] }
	} | Sort-Object -Unique)
	if ($scripts) { $reasons += "$($scripts -join ', ') run(s)" }
	$reasons -join '; '
}

function Wait-QuietPc {
	$deadline = (Get-Date).AddMinutes($QuietTimeoutMin)
	$said = [DateTime]::MinValue
	while ($reason = Get-BusyReason) {
		if ((Get-Date) -gt $deadline) {
			Write-Host "stress: SKIPPED - the PC stayed busy for $QuietTimeoutMin min ($reason): no load put on it" -ForegroundColor Yellow
			exit 2
		}
		if (((Get-Date) - $said).TotalSeconds -ge 60) { Write-Host "stress: waiting for a quiet PC - $reason"; $said = Get-Date }
		Start-Sleep -Seconds 10
	}
}

# Bounded busy loops: each one a pwsh spinning until its own deadline, killed after the run, in `finally` and with
# this script
function Start-CpuLoad {
	$loop = "`$end = [DateTime]::UtcNow.AddMinutes($LoadMinutes); `$x = 0; while ([DateTime]::UtcNow -lt `$end) { for (`$i = 0; `$i -lt 200000; `$i++) { `$x++ } }"
	$pwsh = (Get-Process -Id $PID).Path
	$procs = @()
	for ($c = 0; $c -lt $Cores; $c++) {
		$procs += Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', $loop) -WindowStyle Hidden -PassThru
	}
	Start-Sleep -Seconds 2
	$alive = @($procs | Where-Object { -not $_.HasExited }).Count
	if ($alive -ne $Cores) { Stop-CpuLoad $procs; Write-Host "stress: only $alive of $Cores busy loops ran" -ForegroundColor Red; exit 2 }
	$procs
}

function Stop-CpuLoad($Procs) {
	foreach ($proc in @($Procs)) { try { if ($proc -and -not $proc.HasExited) { $proc.Kill() } } catch { } }
}

$loadProcs = @()
$busyJob = $null
$busyDir = "$testRoot-aim-stress-busy"
$results = @()
try {
	if ($Load -eq 'game') {
		Wait-QuietPc
		$env:ARXVR_MAX_GAMES = '2'
		Clear-Channel $busyDir
		$p = @{ Config = $Config; Playground = $true; VrSim = 'idle'; Seconds = 7200; LockTimeoutSec = $LockTimeoutSec
			UserDir = (New-ArxSequenceUserDir $userRoot 'busy'); TestDir = $busyDir } + $dataArgs
		$busyJob = Start-Job -ScriptBlock $job -ArgumentList $smoke, $p, (Join-Path $OutDir 'busy.log'), $busyDir
		Write-Host "stress: busy game starting (channel $busyDir)"
		$deadline = (Get-Date).AddSeconds($LockTimeoutSec + 120)
		while ($true) {
			$state = $null
			try { $state = (Get-Content -LiteralPath (Join-Path $busyDir 'status.json') -Raw -ErrorAction Stop | ConvertFrom-Json).state } catch { }
			if ($state -eq 'GAMEPLAY') { break }
			if ($busyJob.State -ne 'Running') { Write-Host "stress: the busy game ended before it was playing - see $OutDir\busy.log" -ForegroundColor Red; exit 2 }
			if ((Get-Date) -gt $deadline) { Write-Host 'stress: the busy game is not playing after the lock timeout' -ForegroundColor Red; exit 2 }
			Start-Sleep -Milliseconds 500
		}
		Write-Host 'stress: busy game playing'
	}

	$laneDir = "$testRoot-aim-stress"
	for ($run = 1; $run -le $Runs; $run++) {
		# A quiet PC for the baseline too: its count is the sequence's own, not the other tasks' load
		if ($Load -ne 'game') { Wait-QuietPc }
		if ($Load -eq 'cpu') {
			$loadProcs = @(Start-CpuLoad)
			Write-Host "stress: run $run - $Cores busy loop(s) running"
		}
		Clear-Channel $laneDir
		$userDir = New-ArxSequenceUserDir $userRoot ('run{0:00}' -f $run)
		$p = @{ Config = $Config; Sequence = $copy; LockTimeoutSec = $LockTimeoutSec; TestDir = $laneDir; UserDir = $userDir
			EvidenceDir = (Join-Path $OutDir ('run{0:00}-evidence' -f $run)) } + $dataArgs
		if ($parsed.Sim) { $p.VrSim = $parsed.Sim } elseif ($parsed.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
		if ($parsed.Playground) { $p.Playground = $true }
		if ($parsed.Player) { $p.Player = $true }   # first-run: a player's launch, as xrs-run.ps1 starts it
		if ($parsed.Save) { $p.Save = $parsed.Save }
		if ($parsed.Args) { $p.ArxArgs = @($parsed.Args) }
		$transcript = Join-Path $OutDir ('run{0:00}.log' -f $run)
		$started = Get-Date
		$out = @(& $job $smoke $p $transcript $laneDir)
		$exit = if ($out.Count -and $out[-1] -is [int]) { [int]$out[-1] } else { 1 }
		$text = if (Test-Path -LiteralPath $transcript) { Get-Content -LiteralPath $transcript -Raw } else { '' }
		$reason = ''
		$line = 0
		$step = ''
		if ($text -match "=> FAIL sequence [^:]+: line (\d+) '([^']*)': ([^\r\n]*)") {
			$line = Get-OriginalLine ([int]$Matches[1])
			$step = $Matches[2]
			$where = if ($line) { "line $line" } else { 'a step added by this script' }
			$reason = "$where '$step': $($Matches[3])"
		} elseif ($exit -ne 0) {
			$reason = (@($text -split "`r?`n" | Where-Object { $_ -match 'FAIL|SKIPPED|threw' }) | Select-Object -Last 1)
		}
		$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
		Stop-CpuLoad $loadProcs
		$loadProcs = @()
		$log = Read-RunLog (Get-ArxLogPath $userDir)
		$seconds = ((Get-Date) - $started).TotalSeconds
		$fps = if ($null -ne $log.MinFps) { $log.MinFps.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture) } else { '?' }
		Write-Host ('stress: run {0,2}: {1} ({2:0} s, slowest {3} frames/s; hits aim@speed {4}){5}' -f $run, $verdict, $seconds, $fps,
			$(if ($log.Hits) { $log.Hits -join ' ' } else { 'none' }), $(if ($reason) { " - $reason" })) `
			-ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } elseif ($verdict -eq 'SKIPPED') { 'Yellow' } else { 'Red' })
		$results += [pscustomobject]@{ Run = $run; Verdict = $verdict; Line = $line; Step = $step; Reason = $reason
			MinFps = $log.MinFps; Hits = ($log.Hits -join ' '); Seconds = [int]$seconds }
	}
} finally {
	Stop-CpuLoad $loadProcs
	if ($busyJob) {
		# Quit the busy game through its channel; whatever is left dies with this script's job object
		try { & (Join-Path $PSScriptRoot 'game-cmd.ps1') -Dir $busyDir quit *> $null } catch { }
		$null = Wait-Job $busyJob -Timeout 60
		Remove-Job $busyJob -Force -ErrorAction SilentlyContinue
	}
}

# --- The verdict ------------------------------------------------------------------------------------------------
$results | Export-Csv -LiteralPath (Join-Path $OutDir 'results.csv') -NoTypeInformation -Encoding utf8
$passed = @($results | Where-Object Verdict -eq 'PASS').Count
$failed = @($results | Where-Object Verdict -eq 'FAIL')
$skipped = @($results | Where-Object Verdict -eq 'SKIPPED').Count
Write-Host ''
Write-Host "stress: $name ($variantText, $loadText): $passed passed, $($failed.Count) failed, $skipped skipped of $Runs"
# The failures per assert, with the values they got
foreach ($group in @($failed | Group-Object { if ($_.Line) { "line $($_.Line) '$($_.Step)'" } else { $_.Reason } })) {
	Write-Host "  $($group.Count) x $($group.Name)"
	foreach ($f in $group.Group) { Write-Host ('      run {0,2}: {1}' -f $f.Run, $f.Reason) }
}
if ($skipped) { Write-Host 'stress: SKIPPED - a run did not get the game lock; a check that did not run is not a pass' -ForegroundColor Yellow; exit 2 }
if ($NegativeControl) {
	# Caught: the run failed on an aim assert of the sequence itself, not on a step this script added
	$caught = @($failed | Where-Object { $_.Line -gt 0 -and $_.Step -match 'vr\.melee\.last\.aim' }).Count
	if ($caught -eq $Runs) {
		Write-Host 'stress: PASS - every run failed on an aim assert: the asserts see a weak full swing' -ForegroundColor Green
		exit 0
	}
	Write-Host "stress: FAIL - $($Runs - $caught) of $Runs run(s) did not fail on an aim assert with the swing's speed range out of reach: the check misses it" -ForegroundColor Red
	exit 1
}
if ($failed.Count) {
	Write-Host "stress: FAIL - $($failed.Count) of $Runs run(s) failed; transcripts run<NN>.log, evidence run<NN>-evidence in $OutDir" -ForegroundColor Red
	exit 1
}
Write-Host "stress: PASS - $Runs of $Runs" -ForegroundColor Green
exit 0
