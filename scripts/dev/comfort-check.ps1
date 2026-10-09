<#
.SYNOPSIS
	Comfort check (#76): measure screen shake, FOV zoom and view bobbing with the VR comfort policy
	off and on, in one flat run, and give a PASS / FAIL verdict on the numbers.

.DESCRIPTION
	Runs smoke-test.ps1 (flat, --loadlevel 10, isolated, every launch rule of D-014) in a background
	job with a command list that plays the same scene twice through the test channel:
	  phase "off"  comfort off, quake 300 3000 400, Magic Sight cast on the player   (cfg.ini decides)
	  phase "on"   comfort on,  quake 300 3000 400, the spell still active           (the VR rules)
	Meanwhile this script samples status.json every 100 ms and groups the samples by comfort.mode.
	Per phase it prints max camera.shake (the quake amplitude applied), max |camera.roll|, the
	camera.fov range and max camera.bob (distance of the camera from the stable head position).

	PASS when the smoke test passed and, in every sample of phase "on", shake == 0, roll == 0,
	fov == cfg.ini fov (-Fov, 75 by default) and bob == 0 - and, so the check can fail, phase "off"
	has shake > 0 and fov != cfg.ini fov in at least one sample. Exit code 0 on PASS, 1 on FAIL, 2
	when the smoke test was skipped (the game lock stayed busy).

.PARAMETER Fov
	The fov value of the isolated cfg.ini (default 75, the engine default).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.PARAMETER Vr
	Run the scene in VR instead (SteamVR must already run; the policy is then on in phase "auto" too).

.EXAMPLE
	.\scripts\dev\comfort-check.ps1
.EXAMPLE
	.\scripts\dev\comfort-check.ps1 -Rebuild
#>
[CmdletBinding()]
param(
	[double]$Fov = 75,
	[switch]$Rebuild,
	[switch]$Vr
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
$quake = 'quake 300 3000 400'
# -m with -s: launched at once. Without -m a spellcast on the player is queued for the rune drawing,
# which skips the player - the spell never starts (TRAPS.md)
$sight = 'spellcast -fsmx 5 magic_sight self'
$settle = @(1..10 | ForEach-Object { 'status' })  # each ack takes ~0.25 s: 2.5 s of samples
$commands = @('comfort off', $quake) + $settle + @($sight) + $settle[0..5] +
            @('comfort on', $quake) + $settle + @($sight) + $settle[0..5]

$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ Commands = $commands; Seconds = 2 }
if ($Rebuild) { $jobArgs.Rebuild = $true }
if ($Vr) { $jobArgs.Vr = $true }

Write-Host "==> comfort check: $($commands.Count) channel commands, sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

# Sample while the game runs; one row per distinct status write (frame)
$samples = New-Object System.Collections.Generic.List[object]
$lastFrame = -1
while ($job.State -eq 'Running') {
	$s = Read-ArxStatus $testDir
	# The status is written at the start of a frame, with the camera of the frame before: the write that
	# acknowledges a 'comfort' command still shows the old policy's camera, so those samples are skipped
	if ($s -and $s.state -eq 'GAMEPLAY' -and $s.camera -and [int]$s.frame -ne $lastFrame -and
	    "$($s.lastCmd)" -notlike 'comfort *') {
		$lastFrame = [int]$s.frame
		$samples.Add([pscustomobject]@{
			frame  = [int]$s.frame
			cmdSeq = [int]$s.cmdSeq
			lastCmd = [string]$s.lastCmd
			mode   = [string]$s.comfort.mode
			active = [bool]$s.comfort.active
			shake  = [double]$s.camera.shake
			roll   = [double]$s.camera.roll
			fov    = [double]$s.camera.fov
			bob    = [double]$s.camera.bob
		})
	}
	Start-Sleep -Milliseconds 100
}

$output = @(Receive-Job $job)
Remove-Job $job
$smokeExit = -1
foreach ($line in $output) {
	if ("$line" -match '^SMOKE_EXIT=(-?\d+)$') { $smokeExit = [int]$Matches[1] } else { Write-Host $line }
}

if ($smokeExit -eq 2) {
	Write-Host '==> SKIPPED: the smoke test was skipped'
	exit 2
}

$reasons = @()
if ($smokeExit -ne 0) { $reasons += "smoke test exit code $smokeExit" }

Write-Host ''
Write-Host ("{0,-6} {1,8} {2,10} {3,10} {4,8} {5,8} {6,10}" -f 'phase', 'samples', 'shake max', 'roll max', 'fov min', 'fov max', 'bob max')
$byPhase = @{}
foreach ($phase in 'auto', 'off', 'on') {
	$rows = @($samples | Where-Object { $_.mode -eq $phase })
	if ($rows.Count -eq 0) { continue }
	$stats = [pscustomobject]@{
		count   = $rows.Count
		shake   = ($rows | Measure-Object shake -Maximum).Maximum
		roll    = ($rows | ForEach-Object { [math]::Abs($_.roll) } | Measure-Object -Maximum).Maximum
		fovMin  = ($rows | Measure-Object fov -Minimum).Minimum
		fovMax  = ($rows | Measure-Object fov -Maximum).Maximum
		bob     = ($rows | Measure-Object bob -Maximum).Maximum
	}
	$byPhase[$phase] = $stats
	Write-Host ("{0,-6} {1,8} {2,10:0.000} {3,10:0.000} {4,8:0.00} {5,8:0.00} {6,10:0.000}" -f $phase, $stats.count, $stats.shake, $stats.roll, $stats.fovMin, $stats.fovMax, $stats.bob)
}

$eps = 0.01
$on = $byPhase['on']
$off = $byPhase['off']
if (-not $on) { $reasons += 'no samples with comfort on' }
else {
	if ($on.count -lt 10) { $reasons += "only $($on.count) samples with comfort on" }
	if ($on.shake -ne 0) { $reasons += "shake $($on.shake) with comfort on (expected 0)" }
	if ($on.roll -ne 0) { $reasons += "roll $($on.roll) with comfort on (expected 0)" }
	if ([math]::Abs($on.fovMin - $Fov) -gt $eps -or [math]::Abs($on.fovMax - $Fov) -gt $eps) {
		$reasons += "fov $($on.fovMin)..$($on.fovMax) with comfort on (expected $Fov)"
	}
	if ($on.bob -ne 0) { $reasons += "bob $($on.bob) with comfort on (expected 0)" }
}
if (-not $off) { $reasons += 'no samples with comfort off' }
else {
	# The check must be able to fail: the same scene with the engine defaults moves the camera
	if ($off.shake -le 0) { $reasons += 'no shake measured with comfort off - the quake did not reach the camera' }
	if ([math]::Abs($off.fovMax - $Fov) -le $eps -and [math]::Abs($off.fovMin - $Fov) -le $eps) {
		$reasons += 'fov did not change with comfort off - Magic Sight did not zoom'
	}
}

$log = Get-ArxLogPath
if (Test-Path $log) {
	Write-Host ''
	Write-Host "[vr] comfort lines in $log"
	Select-String -Path $log -Pattern '\[vr\] comfort:' | ForEach-Object { Write-Host "  $($_.Line)" }
}

Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host "==> PASS comfort check: $($samples.Count) samples, comfort on keeps shake 0, roll 0, fov $Fov, bob 0; comfort off shakes (max $($off.shake)) and zooms (fov $($off.fovMin)..$($off.fovMax))"
	exit 0
}
Write-Host "==> FAIL comfort check: $($reasons -join '; ')"
exit 1
