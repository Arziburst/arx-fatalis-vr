<#
.SYNOPSIS
	CI gate (#127): the flat game checks of every pull request - smoke test with the draw check, then the
	spell draw check - with the evidence collected into one folder for the workflow to upload.

.DESCRIPTION
	Runs on the self-hosted runner after the build (.github/workflows/build-windows.yml), and by hand:
	  1. smoke-test.ps1 -Seconds 3 -Commands 'loadlevel 1','loadlevel 10','drawcheck 120'
		                                                           flat, --loadlevel 10: start, two level changes (the first channel
		                                                           batch is a loadlevel, each ack must be its own, #142), draw check, quit
	  2. spell-drawcheck.ps1                                   the 26 spells of #71 cast, drawn twice per frame
	  3. every scripts\dev\xrs\*.xrs sequence (#255)           one launch each, with the @sim / @args / @save it declares; the
	                                                           test save library of this account is verified (or generated) first;
	                                                           an @playground sequence on a runner without node is NOT RUN, with
	                                                           a warning in the log and the job summary (#321)
	All are the same scripts the workers run (docs/vr/TESTING.md); a FAIL of any fails this script.

	Evidence, in -OutDir: one transcript per phase (smoke-test.log, spell-drawcheck.log), and after each phase the
	game's arx.log files, status.json / ack.txt / stdout / stderr of the test channel, the crash reports, the
	captures (draw check pairs) and the run digests - what a failure needs, without the machine.

	Exit code 0 when every phase passed, 1 when one failed, 2 when a phase was SKIPPED (the game lock stayed
	busy: the owner is playing, or another job runs a game) - a gate that did not run is not a pass, so the
	workflow fails it and says to re-run - and 3 when this machine cannot run a game at all (no data, no build).

	The runner service (a different Windows account than the owner's) has its own %LOCALAPPDATA%, so its build,
	user directory and test channel are separate from the owner's. The game data is found as for a player (the
	Steam libraries); when that fails, -DataDir / $env:ARXVR_DATA_DIR names the install folder.

	Never touches a game it did not start: the cleanup stops only arx.exe processes that run from this working
	copy's build directory.

.PARAMETER OutDir
	Where the evidence goes (default: ci-artifacts in the working copy, which the workflow uploads).

.PARAMETER DataDir
	The Arx Fatalis install folder (default: $env:ARXVR_DATA_DIR, else the game finds it in the Steam libraries).

.PARAMETER LockTimeoutSec
	How long a phase waits for the game lock before it is skipped (default 900).

.PARAMETER Config
	Which build to run (default Release, what the workflow builds).

.PARAMETER Phase
	smoke, spells, sequences, or all (default). One phase is for trying the CI setup in pieces.

.EXAMPLE
	.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence
#>
[CmdletBinding()]
param(
	[string]$OutDir,
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 900,
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[ValidateSet('all', 'smoke', 'spells', 'sequences')]
	[string]$Phase = 'all'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')

if (-not $OutDir) { $OutDir = Join-Path $ArxSourceDir 'ci-artifacts' }
New-Item -ItemType Directory -Force $OutDir | Out-Null

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
$crashDir = Get-ArxCrashDir $userDir
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

# --- Evidence ---------------------------------------------------------------------------------------------------
function Save-Evidence([string]$Name, [string]$UserDir = $userDir) {
	$dest = Join-Path $OutDir $Name
	New-Item -ItemType Directory -Force $dest | Out-Null
	foreach ($log in Get-ChildItem $UserDir -Filter 'arx*.log' -File -ErrorAction SilentlyContinue) { Copy-Item $log.FullName $dest -Force }
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt') {
		$p = Join-Path $testDir $f
		if (Test-Path $p) { Copy-Item $p $dest -Force }
	}
	$capture = Join-Path $testDir 'capture'
	if (Test-Path $capture) { Copy-Item $capture (Join-Path $dest 'capture') -Recurse -Force }
	$crashes = Get-ArxCrashDir $UserDir
	if (Test-Path $crashes) { Copy-Item $crashes (Join-Path $dest 'crashes') -Recurse -Force }
	$runs = Join-Path (Get-ArxAppDataDir) 'runs'
	if (Test-Path $runs) {
		$latest = Get-ChildItem $runs -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
		if ($latest) { Copy-Item $latest.FullName (Join-Path $dest 'digest') -Recurse -Force }
	}
}

function Clear-OldEvidence {
	# A leftover capture folder from an earlier job must not pass for this job's evidence
	Remove-Item (Join-Path $testDir 'capture') -Recurse -Force -ErrorAction SilentlyContinue
}

function Invoke-Phase([string]$Name, $Script, [hashtable]$Params, [string]$EvidenceUserDir = $userDir) {
	Write-Host ''
	Write-Host "==> ci-smoke phase '$Name'" -ForegroundColor Cyan
	Clear-OldEvidence
	$transcript = Join-Path $OutDir "$Name.log"
	$started = Get-Date
	$exit = 1
	try {
		# A script name with its parameters, or a script block (the save library phase: verify, else generate)
		$run = if ($Script -is [scriptblock]) { $Script } else { { & (Join-Path $PSScriptRoot $Script) @Params } }
		& $run *>&1 | Tee-Object -FilePath $transcript | ForEach-Object { Write-Host $_ }
		$exit = $LASTEXITCODE
	} catch {
		Write-Host "ci-smoke: phase '$Name' threw: $_" -ForegroundColor Red
		"threw: $_" | Add-Content $transcript
		$exit = 1
	}
	Save-Evidence $Name $EvidenceUserDir
	$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
	Write-Host ("==> ci-smoke phase '{0}': {1} (exit {2}, {3:0} s)" -f $Name, $verdict, $exit, ((Get-Date) - $started).TotalSeconds) -ForegroundColor $(if ($exit -eq 0) { 'Green' } else { 'Red' })
	[pscustomobject]@{ Name = $Name; Exit = $exit; Verdict = $verdict }
}

$results = @()
try {
	if ($Phase -in 'all', 'smoke') {
		$p = @{ Config = $Config; Seconds = 3; Commands = @('loadlevel 1', 'loadlevel 10', 'drawcheck 120'); LockTimeoutSec = $LockTimeoutSec } + $dataArgs
		$results += Invoke-Phase 'smoke-test' 'smoke-test.ps1' $p
	}
	if ($Phase -in 'all', 'spells') {
		$p = @{ LockTimeoutSec = $LockTimeoutSec } + $dataArgs
		$results += Invoke-Phase 'spell-drawcheck' 'spell-drawcheck.ps1' $p
	}
	if ($Phase -in 'all', 'sequences') {
		# Every shipped sequence (#255), parsed first: a syntax error costs no launch
		$sequences = @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs') -Filter '*.xrs' | Sort-Object Name | ForEach-Object { Read-ArxSequence $_.FullName })
		$needed = @($sequences | Where-Object Save | ForEach-Object Save | Sort-Object -Unique)
		$libraryOk = $true
		if ($needed) {
			# The test save library of this account (D-023, derived from the game's data, never committed): checked by a
			# fresh game that loads every save (-Verify, one launch); generated again when it is missing, lacks a save a
			# sequence names, or no longer loads (an engine save format change)
			$library = Get-ArxSaveLibrary
			$libScript = {
				$verifyArgs = @{ Library = $library; LockTimeoutSec = $LockTimeoutSec } + $dataArgs
				$have = @()
				$manifest = Join-Path $library 'manifest.json'
				if (Test-Path $manifest) { $have = @(Get-Content -Raw $manifest | ConvertFrom-Json | Where-Object { Test-Path (Join-Path $library "$($_.name)\gsave.sav") } | ForEach-Object name) }
				$missing = @($needed | Where-Object { $_ -notin $have })
				if ($missing) { Write-Host "ci-smoke: the save library $library lacks $($missing -join ', ') - generating it" }
				else {
					& (Join-Path $PSScriptRoot 'make-test-saves.ps1') -Verify @verifyArgs
					if ($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq 2) { return }
					Write-Host 'ci-smoke: the saves of the library no longer load - generating it again'
				}
				& (Join-Path $PSScriptRoot 'make-test-saves.ps1') @verifyArgs
			}
			$libResult = Invoke-Phase 'save-library' $libScript @{}
			$results += $libResult
			$libraryOk = ($libResult.Exit -eq 0)
		}
		$sequenceUserDirs = Join-Path (Get-ArxAppDataDir) 'ci-userdirs'
		foreach ($seq in $sequences) {
			# A playground sequence needs the generated level (#321), which needs node; a runner without it (the service
			# account may not see a per-user install) reports the sequence NOT RUN with a warning, never a pass
			if ($seq.Playground -and -not (Get-Command node -ErrorAction SilentlyContinue)) {
				$why = "sequence '$($seq.Name)' runs in the test playground, whose level is generated with node - node is not on this runner's PATH, not run"
				Write-Host "ci-smoke: $why" -ForegroundColor Yellow
				Write-Host "::warning title=Playground sequence not run::$why"
				if ($env:GITHUB_STEP_SUMMARY) { Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value "> **Not run:** $why" }
				$results += [pscustomobject]@{ Name = "xrs-$($seq.Name)"; Exit = 0; Verdict = 'NOT RUN' }
				continue
			}
			if ($seq.Save -and -not $libraryOk) { Write-Host "ci-smoke: sequence '$($seq.Name)' needs the save $($seq.Save), which the library phase did not provide - not run" -ForegroundColor Red; $results += [pscustomobject]@{ Name = "xrs-$($seq.Name)"; Exit = 1; Verdict = 'FAIL' }; continue }
			$p = @{ Config = $Config; Sequence = $seq.Path; LockTimeoutSec = $LockTimeoutSec } + $dataArgs
			# A flat sequence says so (--no-vr in its @args, no @sim); the others run on the idle simulated headset
			if ($seq.Sim) { $p.VrSim = $seq.Sim } elseif ($seq.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
			# A fresh user dir per sequence (#296): nothing one sequence leaves behind reaches the next, the order is free
			$seqUserDir = New-ArxSequenceUserDir $sequenceUserDirs $seq.Name
			$p.UserDir = $seqUserDir
			if ($seq.Save) { $p.Save = $seq.Save }
			if ($seq.Playground) { $p.Playground = $true }
			if ($seq.Args) { $p.ArxArgs = @($seq.Args) }
			$results += Invoke-Phase "xrs-$($seq.Name)" 'smoke-test.ps1' $p $seqUserDir
			Remove-Item -LiteralPath $seqUserDir -Recurse -Force -ErrorAction SilentlyContinue
		}
	}
} finally {
	# Never leave a game behind - but only ours: the ones that run from this working copy's build directory
	foreach ($proc in Get-Process arx -ErrorAction SilentlyContinue) {
		if ($proc.Path -and $proc.Path.StartsWith($buildDir, [StringComparison]::OrdinalIgnoreCase)) {
			Write-Host "ci-smoke: stopping the leftover arx.exe pid $($proc.Id)" -ForegroundColor Yellow
			Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
		}
	}
}

Write-Host ''
foreach ($r in $results) { Write-Host ("  {0,-8} {1}" -f $r.Verdict, $r.Name) -ForegroundColor $(if ($r.Verdict -eq 'NOT RUN') { 'Yellow' } elseif ($r.Exit -eq 0) { 'Green' } else { 'Red' }) }
if (@($results | Where-Object Verdict -eq 'FAIL')) { Write-Host 'ci-smoke: FAILED' -ForegroundColor Red; exit 1 }
if (@($results | Where-Object Verdict -eq 'SKIPPED')) {
	Write-Host "ci-smoke: SKIPPED - the game lock stayed busy for $LockTimeoutSec s; a gate that did not run is not a pass, re-run the job when the game is closed" -ForegroundColor Red
	exit 2
}
$notRun = @($results | Where-Object Verdict -eq 'NOT RUN')
if ($notRun) { Write-Host "ci-smoke: all phases that ran passed; $($notRun.Count) playground sequence(s) NOT RUN (no node on this runner)" -ForegroundColor Yellow; exit 0 }
Write-Host 'ci-smoke: all phases passed' -ForegroundColor Green
exit 0
