<#
.SYNOPSIS
	The two-eyes check (research row F-06, #112): are the two eyes two right pictures of one world?

.DESCRIPTION
	Runs the game once on the simulated headset (smoke-test.ps1 -VrSim eye-check: no runtime, no SteamVR,
	one launch) and judges the shots the script takes - two with the head still, one after a 25 degree yaw -
	from the numbers the game's simulated compositor writes into status.json (vr.sim). The legs:

	  world       both eyes see a world, not black and not the bare background (the black-world bug of #215)
	  pairing     every frame showed both eye images from one frame - no stale eye
	  separation  the eyes are the IPD apart
	  difference  left and right differ above the same-eye floor, and are not unrelated pictures
	  parallax    the left picture is shifted right of the right one; swapped eyes give the opposite sign
	  yaw         a 25 degree yaw turned both eyes by 25 degrees and changed both pictures

	This is the acceptance check of the per-eye render (#19) and of the per-eye re-projection (#72). While
	the game shows the flat frame on a head-locked quad and the camera does not follow the head (before #24),
	the yaw leg fails: the pictures do not change when the head turns.

	Exit code 0 when every leg passed, 1 when one failed (or the run failed), 2 when the run was skipped
	(the game lock was busy).

.PARAMETER Fault
	Inject a fault the check must catch: swap-eyes (each eye is shown the other eye's picture) - the
	parallax leg must fail. Proves the check can fail.

.PARAMETER StatusFile
	Judge an existing status.json instead of running the game (no launch).

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.EXAMPLE
	.\scripts\dev\eye-check.ps1
.EXAMPLE
	.\scripts\dev\eye-check.ps1 -Fault swap-eyes
.EXAMPLE
	.\scripts\dev\eye-check.ps1 -- --loadslot 0
	# any arx.exe arguments after --; the default is --loadlevel 10
#>
[CmdletBinding()]
param(
	[ValidateSet('', 'swap-eyes')]
	[string]$Fault = '',
	[string]$StatusFile,
	[switch]$Rebuild,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'eye-check-lib.ps1')

if ($StatusFile) {
	$status = Get-Content -Raw -LiteralPath $StatusFile | ConvertFrom-Json
} else {
	$smokeArgs = @{ VrSim = 'eye-check'; Seconds = 5; Rebuild = $Rebuild }
	if ($Fault) { $smokeArgs.Commands = @("vr-sim fault $Fault on") }
	& (Join-Path $PSScriptRoot 'smoke-test.ps1') @smokeArgs @ArxArgs
	$smokeExit = $LASTEXITCODE
	if ($smokeExit -eq 2) {
		Write-Host 'eye check: SKIPPED - the run was skipped (game lock busy); run it again later' -ForegroundColor Yellow
		exit 2
	}
	$status = Read-ArxStatus (Get-ArxTestDir)
	if ($smokeExit -ne 0) {
		Write-Host "eye check: the run itself failed (smoke test exit $smokeExit) - the legs below may be incomplete" -ForegroundColor Red
	}
}

Write-Host ''
Write-Host "eye check (F-06)$(if ($Fault) { " with the fault $Fault" }):" -ForegroundColor Cyan
$legs = @(Test-ArxEyeCheck $status)
foreach ($leg in $legs) {
	Write-Host ("  [{0}] {1,-11} {2}" -f $(if ($leg.Pass) { 'ok' } else { 'FAIL' }), $leg.Leg, $leg.Detail) `
		-ForegroundColor $(if ($leg.Pass) { 'Green' } else { 'Red' })
}
$failed = @($legs | Where-Object { -not $_.Pass })
if ($status -and $status.vr -and $status.vr.sim) {
	Write-Host "  shots: $(Join-Path (Get-ArxTestDir) 'capture') (still-a-left.bmp, still-a-right.bmp, ...)"
}
if ($failed -or ($smokeExit -and $smokeExit -ne 0)) {
	Write-Host "eye check: FAIL ($(@($failed | ForEach-Object Leg) -join ', '))" -ForegroundColor Red
	exit 1
}
Write-Host 'eye check: PASS' -ForegroundColor Green
exit 0
