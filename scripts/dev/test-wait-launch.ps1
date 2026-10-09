<#
.SYNOPSIS
	Self-test of the launch marker (#258): a check that drives the game from outside waits for the game lock
	without running into its own gameplay timeout, and a smoke test that is skipped on the lock is a skip.

.DESCRIPTION
	Simulates the smoke test as a background job (the way body-check.ps1 starts it) that holds back its launch
	for a few seconds - a busy game lock - and then writes launched.txt:
	  - Wait-ArxGameLaunched keeps waiting past the time a gameplay timeout would have run out, then returns $true
	  - a stale marker of an earlier run is removed by Clear-ArxLaunchMarker and does not count
	  - a job that ends without launching (lock wait ran out, smoke-test.ps1 exit 2) gives $false at once
	  - smoke-test.ps1 itself writes the marker only after it started the game, and clears it per run (source check)
	  - a real smoke-test.ps1 run against held game locks ends SKIPPED (exit 2) and writes no marker
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
# A scratch lock folder, set before the libraries load: the real locks of the PC must not be touched
$dir = Join-Path ([IO.Path]::GetTempPath()) "arxvr-wait-launch-test-$PID"
$env:ARXVR_LOCK_DIR = Join-Path $dir 'locks'
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

New-Item -ItemType Directory -Force $dir | Out-Null
$marker = Get-ArxLaunchMarker $dir

try {
	Write-Host '==> a lock wait longer than a gameplay timeout'
	Clear-ArxLaunchMarker $dir
	$job = Start-Job -ScriptBlock { param($m) Start-Sleep -Seconds 4; Set-Content $m 'pid=1 run=1'; Start-Sleep -Seconds 1 } -ArgumentList $marker
	$watch = [Diagnostics.Stopwatch]::StartNew()
	# what body-check did before: a gameplay timeout (2 s here, 180 s there) counted from the job start
	$oldStyleTimedOut = $false
	try { $null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' } -TimeoutSec 2 -Dir $dir -What 'gameplay' } catch { $oldStyleTimedOut = $true }
	Check 'the old way (timeout from the job start) runs out during the lock wait' $oldStyleTimedOut
	$launched = Wait-ArxGameLaunched $job $dir 100
	$watch.Stop()
	Check 'Wait-ArxGameLaunched returns $true once the marker appears' ($launched -eq $true)
	Check 'it waited out the whole lock wait' ($watch.Elapsed.TotalSeconds -ge 3.5) "$([math]::Round($watch.Elapsed.TotalSeconds, 1)) s"
	Remove-Job $job -Force

	Write-Host '==> a stale marker does not count'
	Set-Content $marker 'pid=99 run=1'
	Clear-ArxLaunchMarker $dir
	Check 'Clear-ArxLaunchMarker removes it' (-not (Test-Path $marker))

	Write-Host '==> a job that ends without launching (skipped on the lock)'
	$job = Start-Job -ScriptBlock { Start-Sleep -Seconds 1; 'SMOKE_EXIT=2' }
	$watch = [Diagnostics.Stopwatch]::StartNew()
	$launched = Wait-ArxGameLaunched $job $dir 100
	Check 'returns $false when the job ends first' ($launched -eq $false)
	Check 'and does not wait on after the job ended' ($watch.Elapsed.TotalSeconds -lt 10)
	Remove-Job $job -Force

	Write-Host '==> smoke-test.ps1 writes the marker after the start and clears it per run'
	$text = Get-Content -Raw (Join-Path $PSScriptRoot 'smoke-test.ps1')
	$startAt = $text.IndexOf('Start-Process -FilePath $exe')
	$markAt = $text.IndexOf('Get-ArxLaunchMarker $TestDir')
	Check 'the marker is written after Start-Process' ($startAt -ge 0 -and $markAt -gt $startAt)
	Check "'launched.txt' is in the per-run cleanup" ($text -match "'stderr\.txt', 'launched\.txt'")

	Write-Host '==> a real smoke-test.ps1 against held game locks is skipped'
	function Get-ArxGamesOutsideLock { @() }
	$slots = @()
	while ($true) {
		$held = Enter-ArxGameLock -What 'test holder' -Copy $PSScriptRoot -Branch 'test' -TimeoutSec 1
		if (-not $held) { break }
		$slots += $held
	}
	try {
		Clear-ArxLaunchMarker $dir
		$pwsh = (Get-Process -Id $PID).Path
		$out = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'smoke-test.ps1') -LockTimeoutSec 3 -TestDir $dir 2>&1
		$exit = $LASTEXITCODE
		if (($out | Out-String) -match 'stale build') {
			# the lock is taken after the build check: CI and a fresh clone have no build of this copy
			Write-Host '  [skip] no fresh build of this working copy; build it to run this leg' -ForegroundColor Yellow
		} else {
			if ($exit -ne 2) { $out | Select-Object -Last 8 | ForEach-Object { Write-Host "    $_" } }
			Check 'exit code 2 (SKIPPED)' ($exit -eq 2) "exit $exit"
			Check 'no launch marker' (-not (Test-Path $marker))
		}
	} finally {
		foreach ($s in $slots) { Exit-ArxGameLock $s }
	}
} finally {
	Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-wait-launch: $($script:failures) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-wait-launch: all passed' -ForegroundColor Green
exit 0
