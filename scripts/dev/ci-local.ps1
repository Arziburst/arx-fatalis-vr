<#
.SYNOPSIS
	A worker's one command before every push (#522): build this working copy, run the script self-tests, then the game
	checks CI will run for this branch - so CI is not the first place a sequence fails.

.DESCRIPTION
	The steps, each with its transcript in -OutDir and its time in the summary:
	  1. fetch origin/main (the base of the selection; offline: the local origin/main, with a warning)
	  2. scripts\build-windows.ps1 -Config Release: the build with the host unit tests (arxtest); a failed build stops here
	  3. scripts\dev\run-script-tests.ps1: the PowerShell self-tests of the dev scripts (the impact map's coverage check
	     among them)
	  4. scripts\dev\ci-smoke.ps1 -Suite pr: the branch's selection - the core plus what its changes against origin/main
	     select in scripts\dev\xrs\impact.txt, exactly as a pull request's CI run - on 2 lanes (1 while another game of
	     the PC holds a slot of the game lock: the owner's, another worker's); -Full runs every phase, as the merge
	     gate does
	The selection is of the commits (origin/main...HEAD): commit first. A change not committed yet is built and run,
	but selects nothing; the script says so.

	Each game is a launch of this branch (the launch ledger counts them): a selection is 15-35 launches, the whole
	suite about 70, so the note of the launch budget (10 per task, WORKFLOW.md) is expected here - it is a warning, not
	a refusal. Never starts SteamVR: the sequences run flat or on the simulated headset (--vr-sim).

	Exit code 0 only when every step passed; 1 when one failed, 2 when the game checks could not run (the game lock
	stayed busy: re-run when the game is closed), 3 when this machine cannot run a game at all.

.PARAMETER Full
	Every game check, not the selection of the branch.

.PARAMETER OutDir
	The transcripts and the game checks' evidence (default %LOCALAPPDATA%\ArxFatalisVR\ci-local\<copy>-<time>; the last
	5 runs of the working copy stay there).

.PARAMETER Base
	The ref the branch is compared with (default origin/main).

.PARAMETER Lanes
	How many games at once (default 2, as CI).

.EXAMPLE
	.\scripts\dev\ci-local.ps1
.EXAMPLE
	.\scripts\dev\ci-local.ps1 -Full
#>
[CmdletBinding()]
param(
	[switch]$Full,
	[string]$OutDir,
	[string]$Base = 'origin/main',
	[ValidateRange(1, 8)]
	[int]$Lanes = 2
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')

$root = Join-Path (Get-ArxAppDataDir) 'ci-local'
if (-not $OutDir) { $OutDir = Join-Path $root "$(Get-WorkingCopyKey $ArxSourceDir)-$((Get-Date).ToString('yyyyMMdd-HHmmss'))" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path
$pwsh = (Get-Process -Id $PID).Path
$steps = @()

# One step in its own pwsh, printed as it runs and kept in <OutDir>\<name>.log
function Invoke-Step([string]$Name, [string]$Label, [string]$Script, [string[]]$Arguments) {
	Write-Host ''
	Write-Host "==> ci-local: $Label" -ForegroundColor Cyan
	$started = Get-Date
	& $pwsh -NoProfile -File $Script @Arguments *>&1 | Tee-Object -FilePath (Join-Path $OutDir "$Name.log")
	$exit = $LASTEXITCODE
	$script:steps += [pscustomobject]@{ Name = $Name; Label = $Label; Exit = $exit; Seconds = ((Get-Date) - $started).TotalSeconds }
	$exit
}

# --- 1. The base ------------------------------------------------------------------------------------------------
$branch = git -C $ArxSourceDir rev-parse --abbrev-ref HEAD
$head = git -C $ArxSourceDir rev-parse --short=9 HEAD
if ($Base -like 'origin/*') {
	git -C $ArxSourceDir fetch --no-tags --quiet origin ($Base -replace '^origin/', '') 2>&1 | Out-Null
	if ($LASTEXITCODE -ne 0) { Write-Warning "could not fetch ${Base}: the selection is against the local $Base" }
}
$baseSha = git -C $ArxSourceDir rev-parse --short=9 $Base 2>$null
if ($LASTEXITCODE -ne 0) { Write-Warning "there is no ${Base} here: the game checks run everything"; $baseSha = '?' }
$global:LASTEXITCODE = 0
$dirty = @(git -C $ArxSourceDir status --porcelain --untracked-files=normal)
Write-Host "ci-local: $branch at $head against $Base ($baseSha); evidence in $OutDir"
if ($dirty) { Write-Host "ci-local: $($dirty.Count) change(s) not committed - built and run, but the selection is of the commits: commit, then run again before the push" -ForegroundColor Yellow }

# --- 2. to 4. ---------------------------------------------------------------------------------------------------
$buildExit = Invoke-Step 'build' 'build (Release, with the host unit tests)' (Join-Path $ArxSourceDir 'scripts\build-windows.ps1') @('-Config', 'Release')
$smokeExit = $null
if ($buildExit -eq 0) {
	$null = Invoke-Step 'script-tests' 'script self-tests' (Join-Path $PSScriptRoot 'run-script-tests.ps1') @()
	$suite = if ($Full) { 'full' } else { 'pr' }
	# Two games at once, as CI (ARXVR_MAX_GAMES for this run's process tree only; ci-smoke.ps1 drops to one lane while
	# another game of the PC holds a slot)
	$previousMax = $env:ARXVR_MAX_GAMES
	$env:ARXVR_MAX_GAMES = "$Lanes"
	try {
		$smokeArgs = @('-Suite', $suite, '-Base', $Base, '-Lanes', "$Lanes", '-OutDir', (Join-Path $OutDir 'game-checks'), '-DurationsFile', (Join-Path $root 'ci-durations.json'))
		$smokeExit = Invoke-Step 'game-checks' "game checks (suite $suite, $Lanes lane(s))" (Join-Path $PSScriptRoot 'ci-smoke.ps1') $smokeArgs
	} finally {
		$env:ARXVR_MAX_GAMES = $previousMax
	}
} else {
	Write-Host 'ci-local: the build failed - no tests, no game' -ForegroundColor Red
}
# The last 5 runs of this working copy stay (other copies' runs are theirs)
$mine = @(Get-ChildItem -LiteralPath $root -Directory -Filter "$(Get-WorkingCopyKey $ArxSourceDir)-*" | Sort-Object Name -Descending)
$removed = @($mine | Select-Object -Skip 5 | Where-Object { $_.FullName -ne $OutDir } | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue; $_.Name })

# --- The summary ------------------------------------------------------------------------------------------------
Write-Host ''
Write-Host "ci-local: $branch at $head against $Base ($baseSha)"
foreach ($s in $steps) {
	$ok = $s.Exit -eq 0
	Write-Host ("  [{0}] {1,-44} {2,5:0} s{3}" -f $(if ($ok) { 'ok' } else { 'FAIL' }), $s.Label, $s.Seconds, $(if ($ok) { '' } else { "  exit $($s.Exit)" })) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
$selected = Select-String -LiteralPath (Join-Path $OutDir 'game-checks.log') -Pattern '^ci-smoke: (suite .*|selected .*|\d+ phase\(s\) on .*)$' -ErrorAction SilentlyContinue | ForEach-Object { $_.Matches[0].Value }
foreach ($line in $selected) { Write-Host "  $line" }
Write-Host "ci-local: transcripts and evidence in $OutDir$(if ($removed) { " (older runs removed: $($removed -join ', '))" })"
$failed = @($steps | Where-Object Exit -ne 0)
if (-not $failed) { Write-Host 'ci-local: all steps passed - ready to push' -ForegroundColor Green; exit 0 }
Write-Host "ci-local: FAILED - $(($failed | ForEach-Object Name) -join ', ')" -ForegroundColor Red
if ($buildExit -eq 0 -and $smokeExit -in 2, 3 -and @($failed).Count -eq 1) { exit $smokeExit }
exit 1
