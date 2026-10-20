<#
.SYNOPSIS
	Stress check of melee-damage.xrs (#532): the sequence N times on two games at once beside a busy third game; FAIL
	on any failed run. The hand-run proof that its line 210 flake (playground.npcFighting still true) is gone.

.DESCRIPTION
	main went red twice on `xrs-melee-damage` line 210, `@assert playground.npcFighting eq false` (true), while the
	same tree passed the merge gate (#532). What decides that line is the engine's damage roll of group 6's thrust:
	the playground goblin (npc/goblin_base: life 12, armour class 8, absorb 10, cowardice 8) is respawned at full life
	before group 6, the thrust usually takes it below 8, and when the mode lever's `attack_player` arrives the class
	script's ATTACK_PLAYER goes `IF (^LIFE < cowardice) GOTO FLEE` - a fleeing goblin, not a fighting one, so line 210
	passes. When the roll deals nothing (CI run 39328879039: `melee.hit ... damage=0.0 aim=1.00 ... kind=thrust`) the
	goblin keeps its full life, ATTACK_PLAYER starts a real fight (`BEHAVIOR -f MOVE_TO`, `SETTARGET -a PLAYER`), the
	resting dagger touches it at once and line 210 sees it fighting. Every passing run kept on the runner dealt 4.3 or
	6.5 with that thrust and had no touch between the lever and line 210 ({touches} = 0).

	The check, by default: -Runs runs (10) of the sequence, -Lanes (2) at once, while a third game - the playground on
	the idle simulated headset - runs the whole time (-NoBusy: without it). Each run is smoke-test.ps1 with the
	sequence's own @sim / @playground, a fresh user directory and its lane's own test channel, as ci-smoke.ps1 runs a
	phase. Exit 0 when every run passed, 1 when one failed, 2 when a run was skipped (the game lock stayed busy).
	As shipped the sequence fails about 1 run in 17 (the roll); the variants below make each case happen every time.

	The variants run a copy of the sequence, made in -OutDir (the shipped file is never changed); they combine:
	  -FullLife         the roll that dealt nothing, every time: right after group 6's thrust (the first `@log` of a
	                    kind=thrust hit) the goblin is respawned - full life, its peaceful stance - and placed back 60
	                    units ahead. A sequence that holds whatever the damage rolls were passes it.
	  -SlowFrames <n>   after every channel command `@frames <n>` more: each step's round trip costs n frames more, what
	                    a loaded PC does now and then. A sequence that waits on the state it asserts holds whatever n is
	                    (as shipped, n = 30 fails line 217: the resting dagger's touch ends the fight `playground fight`
	                    set before `@until playground.npcFighting eq true` reads it).
	  -NegativeControl  the broken game the check must still catch: right before group 7's not-fighting check (the first
	                    `@assert` / `@until playground.npcFighting eq false` after the first `playground pull mode`)
	                    the goblin is set fighting and nothing can end it (`melee noswing none`: a touch deals nothing,
	                    as in group 8). Every run must then FAIL on that check - an npcFighting step of the sequence
	                    itself; a run that passes, or fails elsewhere, fails this script: the check would miss a goblin
	                    that keeps fighting.

	Launches: one per run plus the busy game (default 11). The launch budget is 10 per task (WORKFLOW.md): agree a
	stress run with the coordinator first. Every launch follows D-014 (scripts/launch-lib.ps1); the game lock is
	raised to -Lanes + 1 games for this script's own launches only (ARXVR_MAX_GAMES), and CI's games still come first.

.PARAMETER Runs
	How many runs of the sequence (default 10).

.PARAMETER Lanes
	How many runs at once (default 2, as CI runs them).

.PARAMETER NoBusy
	No busy third game beside the lanes.

.PARAMETER FullLife
	The goblin at full life after group 6's thrust, as when the damage roll dealt nothing (see above).

.PARAMETER SlowFrames
	Frames added after every channel command of the sequence (default 0: the sequence as shipped).

.PARAMETER NegativeControl
	The goblin kept fighting at group 7's not-fighting check (see above): every run must FAIL on that check.

.PARAMETER Sequence
	The sequence (default melee-damage).

.PARAMETER OutDir
	Where the transcripts, the evidence of failed runs and the copied sequence go
	(default %TEMP%\melee-damage-stress-<time>).

.PARAMETER DryRun
	Write and parse the sequence copy, print what would run, launch nothing.

.EXAMPLE
	.\scripts\dev\melee-damage-stress.ps1
	# the contract's check: 10 runs, 2 lanes, a busy third game
.EXAMPLE
	.\scripts\dev\melee-damage-stress.ps1 -Runs 1 -NoBusy -FullLife
	# the line 210 failure, every time
.EXAMPLE
	.\scripts\dev\melee-damage-stress.ps1 -Runs 1 -NoBusy -SlowFrames 30
	# every channel step 30 frames slower
.EXAMPLE
	.\scripts\dev\melee-damage-stress.ps1 -Runs 1 -NoBusy -NegativeControl
	# the goblin kept fighting: every run must fail on playground.npcFighting
#>
[CmdletBinding()]
param(
	[ValidateRange(1, 100)]
	[int]$Runs = 10,
	[ValidateRange(1, 4)]
	[int]$Lanes = 2,
	[switch]$NoBusy,
	[switch]$FullLife,
	[ValidateRange(0, 600)]
	[int]$SlowFrames = 0,
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
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("$name-stress-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$lines = [Collections.Generic.List[string]]::new()
$origin = [Collections.Generic.List[int]]::new()
function Add-Line([string]$Text, [int]$From = 0) { $lines.Add($Text); $origin.Add($From) }
$n = 0
$lever = $false
$fullLifeHooked = $false
$controlHooked = $false
foreach ($raw in [IO.File]::ReadAllLines($source)) {
	$n++
	$code = ($raw -replace '\s+#(\s.*)?$', '').Trim()
	if ($code -match '^playground\s+pull\s+mode\b') { $lever = $true }
	if ($NegativeControl -and $lever -and -not $controlHooked -and $code -match '^@(assert|until)\s+playground\.npcFighting\s+eq\s+false\b') {
		# The goblin fights the player and nothing ends it: no touch deals anything, so no hit drops the fight
		Add-Line 'melee noswing none'
		Add-Line '@until vr.melee.params.noSwing eq none 5s'
		Add-Line 'playground fight'
		Add-Line '@until playground.npcFighting eq true 5s'
		$controlHooked = $true
	}
	Add-Line $raw $n
	if ($SlowFrames -gt 0 -and $code -and -not $code.StartsWith('#') -and -not $code.StartsWith('@')) {
		Add-Line "@frames $SlowFrames"
	}
	if ($FullLife -and -not $fullLifeHooked -and $code -match '^@log\s.*kind=thrust') {
		# The thrust's damage roll dealt nothing: the goblin at full life, where the thrust left it
		Add-Line 'playground pull respawn'
		Add-Line '@frames 5'
		Add-Line '@remember stresslifemax playground.npcLifeMax'
		Add-Line '@until playground.npcLife eq {stresslifemax} 5s'
		Add-Line 'playground place 60'
		Add-Line '@frames 10'
		$fullLifeHooked = $true
	}
}
if ($FullLife -and -not $fullLifeHooked) {
	throw "$source has no '@log ... kind=thrust' line: -FullLife has nothing to hook into - the sequence changed, ask the coordinator where the goblin's life is decided now"
}
if ($NegativeControl -and -not $controlHooked) {
	throw "$source has no '@assert/@until playground.npcFighting eq false' after a 'playground pull mode' line: the negative control has nothing to hook into - the sequence changed, ask the coordinator how the not-fighting check is set up now"
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
if ($FullLife) { $variant += 'the goblin at full life after the thrust' }
if ($SlowFrames) { $variant += "+$SlowFrames frames after every channel command" }
if ($NegativeControl) { $variant += 'negative control: the goblin kept fighting at the not-fighting check' }
$variantText = if ($variant) { $variant -join '; ' } else { 'as shipped' }
$busy = -not $NoBusy
$maxGames = $Lanes + [int]$busy
Write-Host "stress: $name ($variantText), $Runs run(s) on $Lanes lane(s)$(if ($busy) { ' beside a busy third game' }); $($Runs + [int]$busy) launch(es)"
Write-Host "stress: sequence copy $copy ($($parsed.Steps.Count) steps)"
Write-Host "stress: evidence in $OutDir"
if ($DryRun) { Write-Host 'stress: dry run - nothing launched'; exit 0 }

# --- The games --------------------------------------------------------------------------------------------------
# The lanes' jobs and their games die with this script (#526)
$null = Register-ArxLauncherChain
$env:ARXVR_MAX_GAMES = "$maxGames"
$env:ARXVR_AUTOREPORT = '0'
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$key = Get-WorkingCopyKey $ArxSourceDir
$userRoot = Join-Path (Get-ArxAppDataDir) "userdirs\$key-stress"
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

# The busy third game: the playground on the idle simulated headset, for as long as the runs take
$busyJob = $null
$busyDir = "$testRoot-stress-busy"
if ($busy) {
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

$results = @()
$running = @{}   # lane -> { Run; Job; Started }
$next = 1
try {
	while ($next -le $Runs -or $running.Count) {
		foreach ($lane in @($running.Keys)) {
			$r = $running[$lane]
			if ($r.Job.State -eq 'Running') { continue }
			$out = @(Receive-Job $r.Job -ErrorAction SilentlyContinue)
			Remove-Job $r.Job -Force
			$running.Remove($lane)
			$exit = if ($out.Count -and $out[-1] -is [int]) { [int]$out[-1] } else { 1 }
			$transcript = Join-Path $OutDir ('run{0:00}.log' -f $r.Run)
			$text = if (Test-Path -LiteralPath $transcript) { Get-Content -LiteralPath $transcript -Raw } else { '' }
			$reason = ''
			$line = 0
			if ($text -match "=> FAIL sequence [^:]+: line (\d+) '([^']*)': ([^\r\n]*)") {
				$line = Get-OriginalLine ([int]$Matches[1])
				$where = if ($line) { "line $line" } else { 'a step added by this script' }
				$reason = "$where '$($Matches[2])': $($Matches[3])"
			} elseif ($exit -ne 0) {
				$reason = (@($text -split "`r?`n" | Where-Object { $_ -match 'FAIL|SKIPPED|threw' }) | Select-Object -Last 1)
			}
			$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
			$seconds = ((Get-Date) - $r.Started).TotalSeconds
			Write-Host ('stress: run {0,2} lane {1}: {2} ({3:0} s){4}' -f $r.Run, $lane, $verdict, $seconds, $(if ($reason) { " - $reason" })) `
				-ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } elseif ($verdict -eq 'SKIPPED') { 'Yellow' } else { 'Red' })
			$results += [pscustomobject]@{ Run = $r.Run; Lane = $lane; Verdict = $verdict; Line = $line; Reason = $reason; Seconds = [int]$seconds }
		}
		while ($next -le $Runs -and $running.Count -lt $Lanes) {
			$lane = @(1..$Lanes | Where-Object { -not $running.ContainsKey($_) })[0]
			$laneDir = "$testRoot-stress-lane$lane"
			Clear-Channel $laneDir
			$p = @{ Config = $Config; Sequence = $copy; LockTimeoutSec = $LockTimeoutSec; TestDir = $laneDir
				UserDir = (New-ArxSequenceUserDir $userRoot ('run{0:00}' -f $next))
				EvidenceDir = (Join-Path $OutDir ('run{0:00}-evidence' -f $next)) } + $dataArgs
			if ($parsed.Sim) { $p.VrSim = $parsed.Sim } elseif ($parsed.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
			if ($parsed.Playground) { $p.Playground = $true }
			if ($parsed.Save) { $p.Save = $parsed.Save }
			if ($parsed.Args) { $p.ArxArgs = @($parsed.Args) }
			$j = Start-Job -ScriptBlock $job -ArgumentList $smoke, $p, (Join-Path $OutDir ('run{0:00}.log' -f $next)), $laneDir
			$running[$lane] = [pscustomobject]@{ Run = $next; Job = $j; Started = Get-Date }
			Write-Host "stress: run $next started on lane $lane"
			$next++
		}
		if ($running.Count) { Start-Sleep -Milliseconds 500 }
	}
} finally {
	if ($busyJob) {
		# Quit the busy game through its channel; whatever is left dies with this script's job object
		try { & (Join-Path $PSScriptRoot 'game-cmd.ps1') -Dir $busyDir quit *> $null } catch { }
		$null = Wait-Job $busyJob -Timeout 60
		Remove-Job $busyJob -Force -ErrorAction SilentlyContinue
	}
}

# --- The verdict ------------------------------------------------------------------------------------------------
$results = @($results | Sort-Object Run)
$passed = @($results | Where-Object Verdict -eq 'PASS').Count
$failed = @($results | Where-Object Verdict -eq 'FAIL')
$skipped = @($results | Where-Object Verdict -eq 'SKIPPED').Count
Write-Host ''
Write-Host "stress: $name ($variantText): $passed passed, $($failed.Count) failed, $skipped skipped of $Runs"
foreach ($f in $failed) { Write-Host ('  run {0,2}: {1}' -f $f.Run, $f.Reason) }
if ($skipped) { Write-Host 'stress: SKIPPED - a run did not get the game lock; a check that did not run is not a pass' -ForegroundColor Yellow; exit 2 }
if ($NegativeControl) {
	# Caught: the run failed on the sequence's own not-fighting check, not on a step this script added
	$caught = @($failed | Where-Object { $_.Line -gt 0 -and $_.Reason -match 'playground\.npcFighting' }).Count
	if ($caught -eq $Runs) {
		Write-Host 'stress: PASS - every run failed on the not-fighting check: it sees a goblin that keeps fighting' -ForegroundColor Green
		exit 0
	}
	Write-Host "stress: FAIL - $($Runs - $caught) of $Runs run(s) did not fail on the not-fighting check while the goblin kept fighting: the check misses it" -ForegroundColor Red
	exit 1
}
if ($failed.Count) {
	Write-Host "stress: FAIL - $($failed.Count) of $Runs run(s) failed; transcripts run<NN>.log, evidence run<NN>-evidence in $OutDir" -ForegroundColor Red
	exit 1
}
Write-Host "stress: PASS - $Runs of $Runs" -ForegroundColor Green
exit 0
