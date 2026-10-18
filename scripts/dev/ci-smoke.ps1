<#
.SYNOPSIS
	CI gate (#127): the flat game checks of every pull request - smoke test with the draw check, the spell draw check
	and every .xrs sequence - on two games at once (#522), with the evidence collected into one folder.

.DESCRIPTION
	Runs on the self-hosted runner after the build (.github/workflows/build-windows.yml), and by hand. The phases:
	  1. smoke-test.ps1 -Seconds 3 -Commands 'loadlevel 1','loadlevel 10','drawcheck 120'
		                                                           flat, --loadlevel 10: start, two level changes (the first channel
		                                                           batch is a loadlevel, each ack must be its own, #142), draw check, quit
	  2. spell-drawcheck.ps1                                   the 26 spells of #71 cast, drawn twice per frame
	  3. every scripts\dev\xrs\*.xrs sequence (#255)           one launch each, with the @sim / @args / @save it declares; the
	                                                           test save library of this account is verified (or generated) first;
	                                                           an @playground sequence on a runner without node is NOT RUN, with
	                                                           a warning in the log and the job summary (#321)
	All are the same scripts the workers run (docs/vr/TESTING.md); a FAIL of any fails this script.

	Suites (#522): -Suite full runs every phase (a push to main, a manual run with suite=full, a pull request with the
	label full-ci, the merge gate); -Suite pr runs the selection of the change against -Base from the impact map
	scripts\dev\xrs\impact.txt (the core, the sequences of the changed paths, the changed .xrs files), and everything
	when the map cannot say (a path in no line, a line '-> *', no diff). The log names the suite and lists the selection.

	Lanes (#522): the phases run on up to -Lanes games at once (default 2), each lane with its own test channel
	folder; a scheduler hands the next phase to a free lane. The game lock decides how many may run: the lanes are
	capped at its maxGames (the workflow sets ARXVR_MAX_GAMES=2; on a PC without it a run by hand stays on one lane),
	and while a game that is not this run's holds a slot (the owner plays, a worker tests) only one lane runs. The
	save-library phase goes first and every sequence with a @save waits for it; smoke-test and spell-drawcheck share
	this working copy's user directory and never run at the same time; a read-back runs after its sequence, in its
	user directory. The order: the .xrs files the change touches first (git diff against -Base), then the rest by
	the last run's duration, longest first (ci-durations.json, written at the end of every run; a phase without a
	recorded time counts as the longest). The summary lists every phase in the serial order, whatever ran first.

	Evidence, in -OutDir: one transcript per phase (<phase>.log, printed to the log when the phase ends), and after
	each phase the game's arx.log files, status.json / ack.txt / stdout / stderr of the lane's test channel, the crash
	reports, the captures (draw check pairs, shots) and the run digest of the phase's game (picked by its pid) - what
	a failure needs, without the machine. CI keeps the folder on the runner (the last -KeepRuns runs) and uploads it
	only when the job failed.

	Exit code 0 when every phase passed, 1 when one failed, 2 when a phase was SKIPPED (the game lock stayed
	busy: the owner is playing, or another job runs a game) - a gate that did not run is not a pass, so the
	workflow fails it and says to re-run - and 3 when this machine cannot run a game at all (no data, no build).

	The runner service (a different Windows account than the owner's) has its own %LOCALAPPDATA%, so its build,
	user directory and test channel are separate from the owner's. The game data is found as for a player (the
	Steam libraries); when that fails, -DataDir / $env:ARXVR_DATA_DIR names the install folder.

	Never touches a game it did not start: the cleanup stops only arx.exe processes that run from this working
	copy's build directory.

.PARAMETER OutDir
	Where the evidence goes (default: ci-artifacts in the working copy). CI passes
	%LOCALAPPDATA%\ArxFatalisVR\ci-evidence\<run id>-<attempt>.

.PARAMETER DataDir
	The Arx Fatalis install folder (default: $env:ARXVR_DATA_DIR, else the game finds it in the Steam libraries).

.PARAMETER LockTimeoutSec
	How long a phase waits for the game lock before it is skipped (default 900).

.PARAMETER Config
	Which build to run (default Release, what the workflow builds).

.PARAMETER Phase
	smoke, spells, sequences, or all (default). One phase is for trying the CI setup in pieces.

.PARAMETER Lanes
	How many games may run at once (default 2), capped by the game lock's maxGames ($env:ARXVR_MAX_GAMES).

.PARAMETER Suite
	full (default): every phase. pr: the selection of a pull request (#522) - the core, the sequences the changed paths
	select in scripts\dev\xrs\impact.txt and the changed .xrs files themselves, against -Base; everything when a changed
	path is in no line of the map, a line says '-> *', or the diff cannot be made. The log says which suite ran and why,
	and lists the selected phases with the reason of each.

.PARAMETER ImpactMap
	The impact map of -Suite pr (default scripts\dev\xrs\impact.txt).

.PARAMETER Changed
	Repository paths that stand for the change instead of the diff against -Base: what -Suite pr runs for them (to try
	the map, or to time the selection of a past pull request).

.PARAMETER Base
	The ref the change is compared with for the changed sequences and the selection (default: origin/<the PR's base>
	in CI, none on a push; origin/main by hand).

.PARAMETER DurationsFile
	The phase times of the last run (default: %LOCALAPPDATA%\ArxFatalisVR\ci-evidence\ci-durations.json).

.PARAMETER KeepRuns
	When -OutDir is a run folder in %LOCALAPPDATA%\ArxFatalisVR\ci-evidence, how many run folders stay there (default 10).

.EXAMPLE
	.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence
.EXAMPLE
	$env:ARXVR_MAX_GAMES = 2; .\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence
	# two lanes by hand, as CI runs them
#>
[CmdletBinding()]
param(
	[string]$OutDir,
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 900,
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[ValidateSet('all', 'smoke', 'spells', 'sequences')]
	[string]$Phase = 'all',
	[ValidateRange(1, 8)]
	[int]$Lanes = 2,
	[ValidateSet('full', 'pr')]
	[string]$Suite = 'full',
	[string]$ImpactMap,
	[string[]]$Changed,
	[string]$Base = $(if ($env:GITHUB_BASE_REF) { "origin/$env:GITHUB_BASE_REF" } elseif (-not $env:GITHUB_ACTIONS) { 'origin/main' }),
	[string]$DurationsFile,
	[int]$KeepRuns = 10
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')
. (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')
$scriptDir = $PSScriptRoot
$libPath = Join-Path $PSScriptRoot 'ci-smoke-lib.ps1'

if (-not $OutDir) { $OutDir = Join-Path $ArxSourceDir 'ci-artifacts' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
if (-not $DurationsFile) { $DurationsFile = Join-Path (Get-ArxCiEvidenceRoot) 'ci-durations.json' }

# A CI run on a pull request branch must not file bugs on its own (#131): the failing step is the report
$env:ARXVR_AUTOREPORT = '0'

# --- Where is this job running ---------------------------------------------------------------------------------
$session = [Diagnostics.Process]::GetCurrentProcess().SessionId
Write-Host "ci-smoke: user $([Security.Principal.WindowsIdentity]::GetCurrent().Name), session id $session, interactive=$([Environment]::UserInteractive), host $env:COMPUTERNAME"
Write-Host "ci-smoke: working copy $ArxSourceDir"
Write-Host "ci-smoke: app data $(Get-ArxAppDataDir), lock folder $(Get-ArxLockDir)"

$buildDir = Get-DefaultBuildDir $ArxSourceDir $Config
$userDir = Get-ArxUserDir
$testDir = Get-ArxTestDir
if (-not (Test-Path (Join-Path $buildDir 'arx.exe'))) {
	Write-Host "ci-smoke: no arx.exe in $buildDir - build first" -ForegroundColor Red
	exit 3
}

# The data: the game looks in the Steam libraries of the account that runs it; a service account has none
$dataArgs = @{}
if ($DataDir) {
	if (-not (Test-Path $DataDir)) { Write-Host "ci-smoke: data dir $DataDir does not exist for this account" -ForegroundColor Red; exit 3 }
	$dataArgs.DataDir = $DataDir
	Write-Host "ci-smoke: game data $DataDir"
} else {
	Write-Host 'ci-smoke: game data: found by the game in the Steam libraries (no ARXVR_DATA_DIR)'
}

# --- Lanes (#522) -----------------------------------------------------------------------------------------------
# As many games at once as -Lanes asks and the game lock allows; each lane has its own test channel folder (the
# first is the copy's usual one, so game-status.ps1 still finds it)
$maxGames = (Get-ArxLaunchSettings).maxGames
$laneCount = [Math]::Min($Lanes, $maxGames)
Write-Host "ci-smoke: $laneCount lane(s) (-Lanes $Lanes; the game lock allows $maxGames game(s) at once$(if ($maxGames -lt $Lanes) { ', ARXVR_MAX_GAMES raises it' }))"
$laneTestDirs = @{}
for ($l = 1; $l -le $laneCount; $l++) {
	$laneTestDirs[$l] = if ($l -eq 1) { $testDir } else { "$testDir-lane$l" }
	Write-Host "ci-smoke: lane $l test channel $($laneTestDirs[$l])"
}
$lastLaneLimit = $laneCount

# --- Evidence ---------------------------------------------------------------------------------------------------
function Save-Evidence([string]$Name, [string]$UserDir, [string]$TestDir) {
	$dest = Join-Path $OutDir $Name
	New-Item -ItemType Directory -Force $dest | Out-Null
	foreach ($log in Get-ChildItem $UserDir -Filter 'arx*.log' -File -ErrorAction SilentlyContinue) { Copy-Item $log.FullName $dest -Force }
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'hang.txt') {
		$p = Join-Path $TestDir $f
		if (Test-Path $p) { Copy-Item $p $dest -Force }
	}
	$capture = Join-Path $TestDir 'capture'
	if (Test-Path $capture) { Copy-Item $capture (Join-Path $dest 'capture') -Recurse -Force }
	$crashes = Get-ArxCrashDir $UserDir
	if (Test-Path $crashes) { Copy-Item $crashes (Join-Path $dest 'crashes') -Recurse -Force }
	# The other lane writes digests too: the phase's own is the one of its game's pid, never just the newest
	$digest = Find-ArxRunDigest (Join-Path (Get-ArxAppDataDir) 'runs') (Get-ArxPhaseGamePid $TestDir)
	if ($digest) { Copy-Item $digest.FullName (Join-Path $dest 'digest') -Recurse -Force }
}

function Clear-LaneChannel([string]$TestDir) {
	# What the lane's previous phase (or an earlier job) left in the channel must not pass for this phase's evidence
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'launched.txt', 'hang.txt') {
		Remove-Item (Join-Path $TestDir $f) -Force -ErrorAction SilentlyContinue
	}
	Remove-Item (Join-Path $TestDir 'capture') -Recurse -Force -ErrorAction SilentlyContinue
}

# --- The suite (#522) -------------------------------------------------------------------------------------------
# pr: only the selection of the change from the impact map; full - or a selection the map cannot make: every phase
if (-not $ImpactMap) { $ImpactMap = Join-Path $PSScriptRoot 'xrs\impact.txt' }
# pwsh -File hands a list over as one string: a,b,c
$Changed = @($Changed | ForEach-Object { $_ -split ',' } | ForEach-Object Trim | Where-Object { $_ })
$selection = $null
$suiteWhy = 'asked for'
$changedSet = $null
if ($Suite -eq 'pr') {
	$changedSet = if ($Changed) { [pscustomobject]@{ Ok = $true; Files = $Changed } } else { Get-ArxChangedFileSet $ArxSourceDir $Base }
	if (-not $changedSet.Ok) { $suiteWhy = "the change is unknown ($(if ($Base) { "cannot diff against $Base" } else { 'no base' }))" }
	else {
		$impact = Read-ArxImpactMap $ImpactMap
		# The coverage check fails in the script self-tests; here it is only said
		foreach ($problem in @(Test-ArxImpactMap $impact (Get-ArxSequenceNames (Join-Path $PSScriptRoot 'xrs')))) { Write-Host "ci-smoke: impact map: $problem" -ForegroundColor Yellow }
		if ($impact.Errors) { $suiteWhy = "the impact map $ImpactMap has errors" }
		else {
			$sel = Get-ArxImpactSelection $impact $changedSet.Files
			if ($sel.Full) { $suiteWhy = "the change needs everything: $($sel.FullBecause -join '; ')" }
			else { $selection = $sel }
		}
	}
}
$suiteLine = if ($selection) { "suite pr - the core and what the $(@($changedSet.Files).Count) changed file(s) $(if ($Changed) { 'given by -Changed' } else { "against $Base" }) select ($ImpactMap)" }
	elseif ($Suite -eq 'pr') { "suite full - a pull request's selection was asked for, but $suiteWhy" }
	else { 'suite full - every phase (asked for)' }
Write-Host "ci-smoke: $suiteLine"
if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "**Game checks:** $suiteLine" }
function Test-Selected([string]$Name) { -not $script:selection -or $script:selection.Reasons.ContainsKey($Name) }

# --- The phases -------------------------------------------------------------------------------------------------
# In the serial order (the summary's); each runs as a background job on a lane
$phases = [Collections.Generic.List[object]]::new()
$results = @()
function Add-Phase([string]$Name, [string]$Command, [hashtable]$Params, [string]$UserDir, [string[]]$DependsOn = @(), [string]$Exclusive, $Gate, [string]$FreshUserDir) {
	$script:phases.Add([pscustomobject]@{
		Name = $Name; Index = $script:phases.Count; Command = $Command; Params = $Params; UserDir = $UserDir
		DependsOn = $DependsOn; Exclusive = $(if ($Exclusive) { $Exclusive } else { $null }); Gate = $Gate; FreshUserDir = $FreshUserDir
	})
}

if ($Phase -in 'all', 'smoke' -and (Test-Selected 'smoke-test')) {
	$p = @{ Config = $Config; Seconds = 3; Commands = @('loadlevel 1', 'loadlevel 10', 'drawcheck 120'); LockTimeoutSec = $LockTimeoutSec } + $dataArgs
	# smoke-test and spell-drawcheck both use this copy's isolated user directory: never at the same time
	Add-Phase 'smoke-test' 'smoke-test.ps1' $p $userDir -Exclusive 'copy-user-dir'
}
if ($Phase -in 'all', 'spells' -and (Test-Selected 'spell-drawcheck')) {
	$p = @{ LockTimeoutSec = $LockTimeoutSec } + $dataArgs
	Add-Phase 'spell-drawcheck' 'spell-drawcheck.ps1' $p $userDir -Exclusive 'copy-user-dir'
}
$units = @()
$sequenceUserDirs = Get-ArxCiUserDirRoot $ArxSourceDir
$seqUserDirsMade = @()
if ($Phase -in 'all', 'sequences') {
	# Every shipped sequence (#255), parsed first: a syntax error costs no launch
	$sequences = @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs') -Filter '*.xrs' | Sort-Object Name | ForEach-Object { Read-ArxSequence $_.FullName })
	# Of them the selection of a pull request (#522); the save library runs only when one of those loads a save
	$sequences = @($sequences | Where-Object { Test-Selected $_.Name })
	$needed = @($sequences | Where-Object Save | ForEach-Object Save | Sort-Object -Unique)
	if ($needed) {
		# The test save library of this account (D-023): verified, else generated (Invoke-ArxSaveLibraryPhase); its game
		# runs in make-test-saves.ps1's own user directory
		$libParams = @{ ScriptDir = $PSScriptRoot; Library = (Get-ArxSaveLibrary); Needed = $needed; LockTimeoutSec = $LockTimeoutSec; DataDir = $DataDir }
		Add-Phase 'save-library' 'Invoke-ArxSaveLibraryPhase' $libParams (Join-Path (Get-ArxAppDataDir) "userdirs\$(Get-WorkingCopyKey $ArxSourceDir)-savegen")
	}
	# A playground sequence needs the generated level (#321): generated once here, so two lanes never write it at the
	# same time; if that fails the playground sequences run one at a time and each reports the failure itself
	$playgroundExclusive = $null
	$node = [bool](Get-Command node -ErrorAction SilentlyContinue)
	if ($node -and @($sequences | Where-Object Playground)) {
		try {
			. (Join-Path $PSScriptRoot 'playground\playground-lib.ps1')
			$null = Initialize-ArxPlaygroundLevel
		} catch {
			Write-Host "ci-smoke: generating the playground level failed ($_) - the playground sequences run one at a time" -ForegroundColor Yellow
			$playgroundExclusive = 'playground'
		}
	}
	# A sequence whose save the library phase did not provide fails without a launch
	$saveGate = {
		param($Done, $Phase)
		$library = $Done['save-library']
		if (-not $library -or $library.Exit -eq 0) { return $null }
		Write-Host "ci-smoke: sequence '$($Phase.Name)' needs the save $($Phase.Params.Save), which the library phase did not provide - not run" -ForegroundColor Red
		[pscustomobject]@{ Name = $Phase.Name; Exit = 1; Verdict = 'FAIL' }
	}
	# A read-back runs only when its sequence passed
	$readbackGate = {
		param($Done, $Phase)
		$first = $Done[$Phase.DependsOn[0]]
		if ($first -and $first.Exit -eq 0) { $null } else { 'omit' }
	}
	foreach ($seq in $sequences) {
		# A playground sequence needs the generated level (#321), which needs node; a runner without it (the service
		# account may not see a per-user install) reports the sequence NOT RUN with a warning, never a pass
		if ($seq.Playground -and -not $node) {
			$why = "sequence '$($seq.Name)' runs in the test playground, whose level is generated with node - node is not on this runner's PATH, not run"
			Write-Host "ci-smoke: $why" -ForegroundColor Yellow
			Write-Host "::warning title=Playground sequence not run::$why"
			if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "> **Not run:** $why" }
			$results += [pscustomobject]@{ Name = "xrs-$($seq.Name)"; Exit = 0; Verdict = 'NOT RUN'; Index = $phases.Count }
			$phases.Add([pscustomobject]@{ Name = "xrs-$($seq.Name)"; Index = $phases.Count; Skip = $true })
			continue
		}
		$p = @{ Config = $Config; Sequence = $seq.Path; LockTimeoutSec = $LockTimeoutSec } + $dataArgs
		# A flat sequence says so (--no-vr in its @args, no @sim); the others run on the idle simulated headset
		if ($seq.Sim) { $p.VrSim = $seq.Sim } elseif ($seq.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
		# A fresh user dir per sequence (#296, made by New-ArxSequenceUserDir when the phase starts): nothing one
		# sequence leaves behind reaches the next, the order is free
		$seqUserDir = Join-Path $sequenceUserDirs $seq.Name
		$p.UserDir = $seqUserDir
		$seqUserDirsMade += $seqUserDir
		if ($seq.Save) { $p.Save = $seq.Save }
		if ($seq.Playground) { $p.Playground = $true }
		if ($seq.Player) { $p.Player = $true }
		if ($seq.Args) { $p.ArxArgs = @($seq.Args) }
		$name = "xrs-$($seq.Name)"
		Add-Phase $name 'smoke-test.ps1' $p $seqUserDir -DependsOn $(if ($seq.Save) { @('save-library') } else { @() }) `
			-Exclusive $(if ($seq.Playground) { $playgroundExclusive }) -Gate $(if ($seq.Save) { $saveGate }) -FreshUserDir $seqUserDir
		$unit = [pscustomobject]@{ Phases = @($name); Paths = @([IO.Path]::GetRelativePath($ArxSourceDir, $seq.Path)) }
		# A read-back (xrs\readback\<name>-readback.xrs, #394): a second launch in the same user dir checks what the
		# first one left behind (cfg.ini is written when the game quits); skipped when the first launch did not pass
		$readback = Join-Path $PSScriptRoot "xrs\readback\$($seq.Name)-readback.xrs"
		if (Test-Path $readback) {
			$rb = Read-ArxSequence $readback
			$rp = @{ Config = $Config; Sequence = $rb.Path; LockTimeoutSec = $LockTimeoutSec; UserDir = $seqUserDir } + $dataArgs
			if ($rb.Sim) { $rp.VrSim = $rb.Sim } elseif ($rb.Args -notcontains '--no-vr') { $rp.VrSim = 'idle' }
			if ($rb.Player) { $rp.Player = $true }
			if ($rb.Args) { $rp.ArxArgs = @($rb.Args) }
			Add-Phase "xrs-$($rb.Name)" 'smoke-test.ps1' $rp $seqUserDir -DependsOn @($name) -Gate $readbackGate
			$unit.Phases += "xrs-$($rb.Name)"
			$unit.Paths += [IO.Path]::GetRelativePath($ArxSourceDir, $rb.Path)
		}
		$units += $unit
	}
}

# --- The order --------------------------------------------------------------------------------------------------
# The save library first (the @save sequences wait for it), then the two short checks, then the sequences: the changed
# ones first, then by the last run's duration, longest first
$durations = Read-ArxPhaseDurations $DurationsFile
$changed = if ($Changed) { @($Changed) } else { @(Get-ArxChangedFiles $ArxSourceDir $Base) }
$changedXrs = @($changed | Where-Object { $_ -like 'scripts/dev/xrs/*.xrs' })
$sequenceOrder = @(Get-ArxPhaseOrder $units $changed $durations)
$byName = @{}
foreach ($ph in $phases) { $byName[$ph.Name] = $ph }
$dispatch = @(@('save-library', 'smoke-test', 'spell-drawcheck') + $sequenceOrder | Where-Object { $byName.ContainsKey($_) -and -not $byName[$_].Skip } | ForEach-Object { $byName[$_] })
$known = @($dispatch | Where-Object { $durations.ContainsKey($_.Name) }).Count
Write-Host "ci-smoke: $($dispatch.Count) phase(s) to run; changed $(if ($Changed) { '(given by -Changed)' } elseif ($Base) { "against $Base" } else { 'against nothing (no base)' }): $(if ($changedXrs) { $changedXrs -join ', ' } else { 'no .xrs' })"
Write-Host "ci-smoke: last run's durations from $DurationsFile$(if (-not (Test-Path -LiteralPath $DurationsFile)) { ' (none yet)' }): $known known, $($dispatch.Count - $known) unknown (counted as the longest)"
Write-Host "ci-smoke: order: $(($dispatch | ForEach-Object Name) -join ', ')"
if ($selection) {
	# The selected checks that exist here (a deleted .xrs selects nothing), each with why: core, or the path that chose it
	$picked = @(foreach ($name in $selection.Names) {
		if ($byName.ContainsKey($name) -or $byName.ContainsKey("xrs-$name")) { "$name ($($selection.Reasons[$name]))" }
	})
	$total = $ArxImpactChecks.Count + @(Get-ArxSequenceNames (Join-Path $PSScriptRoot 'xrs')).Count
	Write-Host "ci-smoke: selected $($picked.Count) of $total checks: $($picked -join ', ')"
	if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "Selected $($picked.Count) of $total checks: $($picked -join ', ')" }
}

# --- Running them -----------------------------------------------------------------------------------------------
# One phase in its own pwsh process (a background job): the lane's test channel goes to every script of the phase
# through ARXVR_TEST_DIR (Get-ArxTestDir; smoke-test.ps1 also gets it as -TestDir); the transcript is written as it runs
$phaseJob = {
	param([string]$Command, [hashtable]$Params, [string]$Transcript, [string]$TestDir, [string]$Lib)
	$ErrorActionPreference = 'Stop'
	[Console]::OutputEncoding = [Text.Encoding]::UTF8
	$env:ARXVR_TEST_DIR = $TestDir
	. $Lib
	$global:LASTEXITCODE = 1
	try {
		& $Command @Params *>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $Transcript -Encoding utf8
		$LASTEXITCODE
	} catch {
		"ci-smoke: the phase threw: $_" | Out-File -LiteralPath $Transcript -Append -Encoding utf8
		1
	}
}
$jobs = @()
$startPhase = {
	param($Phase, $Lane)
	$laneDir = $script:laneTestDirs[$Lane]
	New-Item -ItemType Directory -Force $laneDir | Out-Null
	Clear-LaneChannel $laneDir
	if ($Phase.FreshUserDir) { $null = New-ArxSequenceUserDir (Split-Path -Parent $Phase.FreshUserDir) (Split-Path -Leaf $Phase.FreshUserDir) }
	$params = @{} + $Phase.Params
	if ($Phase.Command -eq 'smoke-test.ps1') { $params.TestDir = $laneDir }
	$command = if ($Phase.Command -like '*.ps1') { Join-Path $script:scriptDir $Phase.Command } else { $Phase.Command }
	Write-Host ("--> ci-smoke phase '{0}' started on lane {1}" -f $Phase.Name, $Lane)
	$job = Start-Job -ScriptBlock $script:phaseJob -ArgumentList $command, $params, (Join-Path $script:OutDir "$($Phase.Name).log"), $laneDir, $script:libPath
	$script:jobs += $job
	$job
}
$finishPhase = {
	param($Phase, $Lane, $Job, $Seconds)
	$out = @(Receive-Job $Job -ErrorAction SilentlyContinue -ErrorVariable jobErrors)
	Remove-Job $Job -Force
	$exit = if ($out.Count -and $out[-1] -is [int]) { [int]$out[-1] } else { 1 }
	$transcript = Join-Path $script:OutDir "$($Phase.Name).log"
	Write-Host ''
	Write-Host "==> ci-smoke phase '$($Phase.Name)' (lane $Lane)" -ForegroundColor Cyan
	if (Test-Path -LiteralPath $transcript) { Get-Content -LiteralPath $transcript | ForEach-Object { Write-Host $_ } }
	foreach ($e in @($jobErrors)) {
		Write-Host "ci-smoke: phase '$($Phase.Name)': $e" -ForegroundColor Red
		"ci-smoke: $e" | Add-Content -LiteralPath $transcript
	}
	Save-Evidence $Phase.Name $Phase.UserDir $script:laneTestDirs[$Lane]
	$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
	Write-Host ("==> ci-smoke phase '{0}': {1} (exit {2}, {3:0} s) on lane {4}" -f $Phase.Name, $verdict, $exit, $Seconds, $Lane) -ForegroundColor $(if ($exit -eq 0) { 'Green' } else { 'Red' })
	[pscustomobject]@{ Name = $Phase.Name; Exit = $exit; Verdict = $verdict }
}
# One lane while a game that is not this run's holds a slot of the lock (the owner, a worker): they get the PC's share
$laneLimit = {
	$n = Get-ArxLaneCount $script:laneCount $ArxSourceDir $script:buildDir
	if ($n -ne $script:lastLaneLimit) {
		if ($n -lt $script:laneCount) { Write-Host "ci-smoke: 1 lane while another game of this PC runs: $((Get-ArxForeignGames $ArxSourceDir $script:buildDir) -join '; ')" -ForegroundColor Yellow }
		else { Write-Host "ci-smoke: no other game any more - back to $n lanes" -ForegroundColor Yellow }
		$script:lastLaneLimit = $n
	}
	$n
}

$checksStarted = Get-Date
$outcome = @()
# Nobody clicks a dialog on the runner (#501): every game of this job writes it to arx.log and stderr instead (the
# phases set it for their own launches too, and the lanes' jobs inherit it); the shell gets its variables back at the end
$previousDialogEnv = Set-ArxDialogEnv -NoDialogs
try {
	$outcome = @(Invoke-ArxPhaseSchedule -Phases $dispatch -Lanes $laneCount -Start $startPhase -Finish $finishPhase -LaneLimit $laneLimit)
} finally {
	Restore-ArxDialogEnv $previousDialogEnv
	# A job left behind (the run was interrupted) and its game: never leave a game behind - but only ours, the ones
	# that run from this working copy's build directory
	foreach ($job in $jobs) { if ($job.State -eq 'Running') { Stop-Job $job -ErrorAction SilentlyContinue }; Remove-Job $job -Force -ErrorAction SilentlyContinue }
	foreach ($proc in Get-Process arx -ErrorAction SilentlyContinue) {
		if ($proc.Path -and $proc.Path.StartsWith($buildDir, [StringComparison]::OrdinalIgnoreCase)) {
			Write-Host "ci-smoke: stopping the leftover arx.exe pid $($proc.Id)" -ForegroundColor Yellow
			Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
		}
	}
	foreach ($dir in $seqUserDirsMade) { Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue }
}
$wall = ((Get-Date) - $checksStarted).TotalSeconds
$sum = ($outcome | Where-Object { $null -ne $_.PSObject.Properties['Seconds'] } | Measure-Object Seconds -Sum).Sum
Write-Host ''
Write-Host ("ci-smoke: {0} phase(s) on {1} lane(s) in {2:0} s; their own times add up to {3:0} s" -f $outcome.Count, $laneCount, $wall, $sum)

# The next run's order; and on the runner only the last -KeepRuns evidence folders stay
try { Write-ArxPhaseDurations $DurationsFile $durations $outcome } catch { Write-Warning "could not write $DurationsFile ($($_.Exception.Message))" }
$evidenceRoot = Get-ArxCiEvidenceRoot
if ((Split-Path -Parent $OutDir) -eq $evidenceRoot.TrimEnd('\')) {
	$removed = @(Remove-ArxOldCiEvidence $evidenceRoot $KeepRuns)
	Write-Host "ci-smoke: evidence kept in $OutDir (the last $KeepRuns runs stay in $evidenceRoot$(if ($removed) { "; removed $($removed -join ', ')" }))"
}

# The summary, in the serial order
$index = @{}
foreach ($ph in $phases) { $index[$ph.Name] = $ph.Index }
$results = @(@($results) + @($outcome) | Sort-Object { $index[$_.Name] })
foreach ($r in $results) { Write-Host ("  {0,-8} {1}" -f $r.Verdict, $r.Name) -ForegroundColor $(if ($r.Verdict -eq 'NOT RUN') { 'Yellow' } elseif ($r.Exit -eq 0) { 'Green' } else { 'Red' }) }
if ($selection) { Write-Host "ci-smoke: suite pr - the checks the change does not select did not run; the full suite runs before the merge" }
if (@($results | Where-Object Verdict -eq 'FAIL')) { Write-Host 'ci-smoke: FAILED' -ForegroundColor Red; exit 1 }
if (@($results | Where-Object Verdict -eq 'SKIPPED')) {
	Write-Host "ci-smoke: SKIPPED - the game lock stayed busy for $LockTimeoutSec s; a gate that did not run is not a pass, re-run the job when the game is closed" -ForegroundColor Red
	exit 2
}
$notRun = @($results | Where-Object Verdict -eq 'NOT RUN')
if ($notRun) { Write-Host "ci-smoke: all phases that ran passed; $($notRun.Count) playground sequence(s) NOT RUN (no node on this runner)" -ForegroundColor Yellow; exit 0 }
Write-Host 'ci-smoke: all phases passed' -ForegroundColor Green
exit 0
