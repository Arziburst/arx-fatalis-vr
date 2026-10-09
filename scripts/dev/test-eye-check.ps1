<#
.SYNOPSIS
	Self-test of the two-eyes check's judge (eye-check-lib.ps1, research row F-06, #112).

.DESCRIPTION
	Feeds made-up status.json contents of a --vr-sim run to Test-ArxEyeCheck: a good pair of eyes passes
	every leg, and each broken case fails the leg it must - swapped eyes the parallax, a black world the
	world leg, a stale eye the pairing, a head that does not reach the picture (before #24) the yaw, identical
	eyes the difference, a wrong IPD the separation. Also runs eye-check.ps1 -StatusFile on a fixture file
	(no launch). No game, no network. Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'eye-check-lib.ps1')

$script:failures = 0
$script:checks = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	$script:checks++
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}

# A good run: numbers like those of a real --loadlevel 10 run with a head-tracked world
$goodJson = @'
{
  "vr": {
    "enabled": true,
    "sim": {
      "timeline": "done", "pending": 0,
      "view": { "eyeWidth": 1440, "eyeHeight": 1600, "ipd": 0.064, "rate": 90 },
      "compositor": { "projectionFrames": 900, "pairedFrames": 900, "staleFrames": 0, "freshFrames": 900, "quadFrames": 900 },
      "shots": {
        "still-a": {
          "swapEyes": false,
          "left":  { "scene": { "verdict": "world" }, "rot": { "yaw": 0.0 } },
          "right": { "scene": { "verdict": "world" }, "rot": { "yaw": 0.0 } },
          "quad": { "shown": true, "stats": { "verdict": "world" } },
          "lr": { "diff": 4.5, "shift": 16, "shiftFraction": 0.1, "separation": 0.064 },
          "versus": { }
        },
        "still-b": {
          "swapEyes": false,
          "left":  { "scene": { "verdict": "world" }, "rot": { "yaw": 0.0 } },
          "right": { "scene": { "verdict": "world" }, "rot": { "yaw": 0.0 } },
          "quad": { "shown": true, "stats": { "verdict": "world" } },
          "lr": { "diff": 4.5, "shift": 16, "shiftFraction": 0.1, "separation": 0.064 },
          "versus": { "still-a": { "left": 0.2, "right": 0.25 } }
        },
        "yaw25": {
          "swapEyes": false,
          "left":  { "scene": { "verdict": "world" }, "rot": { "yaw": 25.0 } },
          "right": { "scene": { "verdict": "world" }, "rot": { "yaw": 25.0 } },
          "quad": { "shown": true, "stats": { "verdict": "world" } },
          "lr": { "diff": 4.4, "shift": 16, "shiftFraction": 0.1, "separation": 0.064 },
          "versus": { "still-a": { "left": 9.0, "right": 8.5 }, "still-b": { "left": 9.1, "right": 8.6 } }
        }
      }
    }
  }
}
'@

function New-Status { $goodJson | ConvertFrom-Json }
function Get-Leg($Legs, [string]$Name) { $Legs | Where-Object Leg -eq $Name }
function Failed($Legs) { @($Legs | Where-Object { -not $_.Pass } | ForEach-Object Leg) -join ',' }

Write-Host '--- a good pair of eyes'
$legs = @(Test-ArxEyeCheck (New-Status))
Check 'six legs' ($legs.Count -eq 6) $legs.Count
Check 'every leg passes' ((Failed $legs) -eq '') (Failed $legs)
Check 'legs in order' ((@($legs | ForEach-Object Leg) -join ',') -eq 'world,pairing,separation,difference,parallax,yaw') (@($legs | ForEach-Object Leg) -join ',')

Write-Host '--- swapped eyes'
$s = New-Status
foreach ($shot in 'still-a', 'still-b', 'yaw25') { $s.vr.sim.shots.$shot.lr.shift = -16; $s.vr.sim.shots.$shot.swapEyes = $true }
$legs = @(Test-ArxEyeCheck $s)
Check 'only the parallax fails' ((Failed $legs) -eq 'parallax') (Failed $legs)
Check 'the detail names the fault' ((Get-Leg $legs 'parallax').Detail -match 'swap-eyes') (Get-Leg $legs 'parallax').Detail

Write-Host '--- a black world (#215)'
$s = New-Status
$s.vr.sim.shots.'still-a'.left.scene.verdict = 'black'
$s.vr.sim.shots.'still-a'.right.scene.verdict = 'black'
$legs = @(Test-ArxEyeCheck $s)
Check 'the world leg fails' ((Failed $legs) -match '^world') (Failed $legs)
Check 'the detail says black' ((Get-Leg $legs 'world').Detail -match 'left black, right black') (Get-Leg $legs 'world').Detail

Write-Host '--- a stale eye'
$s = New-Status
$s.vr.sim.compositor.pairedFrames = 899
$s.vr.sim.compositor.staleFrames = 1
$legs = @(Test-ArxEyeCheck $s)
Check 'the pairing fails' ((Failed $legs) -eq 'pairing') (Failed $legs)

Write-Host '--- no frame at all'
$s = New-Status
$s.vr.sim.compositor.projectionFrames = 0
$s.vr.sim.compositor.pairedFrames = 0
Check 'the pairing fails without frames' ((Failed @(Test-ArxEyeCheck $s)) -eq 'pairing')

Write-Host '--- the head does not reach the picture (before #24)'
$s = New-Status
$s.vr.sim.shots.yaw25.versus.'still-a'.left = 0.3
$s.vr.sim.shots.yaw25.versus.'still-a'.right = 0.2
$legs = @(Test-ArxEyeCheck $s)
Check 'the yaw fails' ((Failed $legs) -eq 'yaw') (Failed $legs)
Check 'the detail has both eyes' ((Get-Leg $legs 'yaw').Detail -match 'left eye turned 25 deg.*right eye turned 25 deg') (Get-Leg $legs 'yaw').Detail

Write-Host '--- the eyes do not turn'
$s = New-Status
$s.vr.sim.shots.yaw25.right.rot.yaw = 0.0
Check 'the yaw fails when one eye does not turn' ((Failed @(Test-ArxEyeCheck $s)) -eq 'yaw')

Write-Host '--- a yaw across +-180 degrees'
$s = New-Status
$s.vr.sim.shots.'still-a'.left.rot.yaw = 170.0
$s.vr.sim.shots.'still-a'.right.rot.yaw = 170.0
$s.vr.sim.shots.yaw25.left.rot.yaw = -165.0
$s.vr.sim.shots.yaw25.right.rot.yaw = -165.0
Check 'the turn wraps' ((Failed @(Test-ArxEyeCheck $s)) -eq '')

Write-Host '--- identical eyes, and unrelated eyes'
$s = New-Status
$s.vr.sim.shots.'still-a'.lr.diff = 0.4
Check 'identical eyes fail the difference' ((Failed @(Test-ArxEyeCheck $s)) -eq 'difference')
$s = New-Status
$s.vr.sim.shots.'still-a'.lr.diff = 80
Check 'unrelated eyes fail the difference' ((Failed @(Test-ArxEyeCheck $s)) -eq 'difference')
$s = New-Status
$s.vr.sim.shots.'still-b'.versus.'still-a'.left = 2.0
Check 'a noisy floor raises the bar' ((Failed @(Test-ArxEyeCheck $s)) -match 'difference')

Write-Host '--- a wrong eye distance'
$s = New-Status
$s.vr.sim.shots.'still-a'.lr.separation = 0.070
Check 'the separation fails' ((Failed @(Test-ArxEyeCheck $s)) -eq 'separation')

Write-Host '--- missing pieces'
Check 'no vr.sim block' ((@(Test-ArxEyeCheck ([pscustomobject]@{ vr = [pscustomobject]@{ enabled = $false } }))[0].Leg) -eq 'simulator')
$s = New-Status
$s.vr.sim.shots.PSObject.Properties.Remove('yaw25')
$legs = @(Test-ArxEyeCheck $s)
Check 'a missing shot is named' ($legs.Count -eq 1 -and $legs[0].Detail -match 'yaw25') $legs[0].Detail

Write-Host '--- eye-check.ps1 -StatusFile (no launch)'
$dir = Join-Path ([IO.Path]::GetTempPath()) "arxvr-eye-check-$PID"
New-Item -ItemType Directory -Force $dir | Out-Null
try {
	$good = Join-Path $dir 'good.json'
	$goodJson | Set-Content -LiteralPath $good -Encoding UTF8
	$pwsh = (Get-Process -Id $PID).Path
	$output = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'eye-check.ps1') -StatusFile $good 2>&1 | Out-String
	Check 'a good status passes' ($LASTEXITCODE -eq 0 -and $output -match 'eye check: PASS') $output
	$swapped = New-Status
	$swapped.vr.sim.shots.'still-a'.lr.shift = -3
	$bad = Join-Path $dir 'bad.json'
	$swapped | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $bad -Encoding UTF8
	$output = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'eye-check.ps1') -StatusFile $bad 2>&1 | Out-String
	Check 'a bad status fails with exit 1' ($LASTEXITCODE -eq 1 -and $output -match 'eye check: FAIL \(parallax\)') $output
} finally {
	Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failures) {
	Write-Host "test-eye-check: $($script:failures) of $($script:checks) checks FAILED" -ForegroundColor Red
	exit 1
}
Write-Host "test-eye-check: all $($script:checks) checks passed" -ForegroundColor Green
exit 0
