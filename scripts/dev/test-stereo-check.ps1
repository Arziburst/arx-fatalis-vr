<#
.SYNOPSIS
	Self-test of the stereo check of effects (stereo-check-lib.ps1, the @stereo step of the .xrs sequences, #22).

.DESCRIPTION
	Renders made-up eye pictures of a canted, asymmetric headset (the Index's angles, 5 degrees between the eyes) with
	known geometry and feeds them to Test-ArxStereoChange, as a --vr-sim run's shots would come: a small sphere 1 m
	away and 30 degrees to the side passes every fx leg and is measured at 1 m; the same pictures with the eyes swapped
	fail the depth (a negative disparity); a square drawn in each eye's own screen space (the engine's sprite before
	#22) fails the fusion; a stale eye fails the pairing; a change over both whole eyes passes the view legs, over the
	middle of the view only (the flat frame quad) fails the coverage; a head-locked pattern at infinity passes the
	parallax, the same pattern stretched over each eye's own image fails it. No game, no network.
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'stereo-check-lib.ps1')
Add-Type -AssemblyName System.Drawing

$script:failures = 0
$script:checks = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	$script:checks++
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}

if (-not ('ArxStereoFixture' -as [type])) {
Add-Type @'
using System;
public static class ArxStereoFixture {
  static void Rotate(double qx, double qy, double qz, double qw, double vx, double vy, double vz,
                     out double rx, out double ry, out double rz) {
    double tx = 2 * (qy * vz - qz * vy), ty = 2 * (qz * vx - qx * vz), tz = 2 * (qx * vy - qy * vx);
    rx = vx + qw * tx + (qy * tz - qz * ty);
    ry = vy + qw * ty + (qz * tx - qx * tz);
    rz = vz + qw * tz + (qx * ty - qy * tx);
  }
  // What an eye sees: kind 0 = nothing (grey), 1 = a sphere (world), 2 = a square in the eye's own screen space
  // around the sphere's projection, its size from the eye's depth (the engine's sprite before #22), 3 = a pattern at
  // infinity by head direction, 4 = the same pattern by the eye's own image position, 5 = the whole image white,
  // 6 = white only in the middle of the head's view
  public static byte[] Render(int w, int h, double tl, double tr, double tu, double td, double[] q, double[] pos,
                              int kind, double[] center, double radius) {
    byte[] img = new byte[w * h * 4];
    double[] inv = { -q[0], -q[1], -q[2], q[3] };
    double sx = 0, sy = 0, half = 0;
    if (kind == 2) {
      double vx, vy, vz;
      Rotate(inv[0], inv[1], inv[2], inv[3], center[0] - pos[0], center[1] - pos[1], center[2] - pos[2], out vx, out vy, out vz);
      double ptx = vx / -vz, pty = vy / -vz;
      sx = (ptx - tl) / (tr - tl) * w; sy = (tu - pty) / (tu - td) * h;
      half = radius / -vz / (tr - tl) * w;
    }
    for (int y = 0; y < h; y++) {
      double ty = tu - (tu - td) * (y + 0.5) / h;
      for (int x = 0; x < w; x++) {
        double tx = tl + (tr - tl) * (x + 0.5) / w;
        double dx, dy, dz;
        Rotate(q[0], q[1], q[2], q[3], tx, ty, -1, out dx, out dy, out dz);
        double len = Math.Sqrt(dx * dx + dy * dy + dz * dz);
        dx /= len; dy /= len; dz /= len;
        double v = 40;
        if (kind == 1) {
          double ox = pos[0] - center[0], oy = pos[1] - center[1], oz = pos[2] - center[2];
          double bq = ox * dx + oy * dy + oz * dz;
          double c = ox * ox + oy * oy + oz * oz - radius * radius;
          if (bq * bq - c >= 0 && -bq > 0) v = 255;
        } else if (kind == 2) {
          if (Math.Abs(x + 0.5 - sx) <= half && Math.Abs(y + 0.5 - sy) <= half) v = 255;
        } else if (kind == 3 || kind == 4) {
          double az = kind == 3 ? Math.Atan2(dx, -dz) * 180 / Math.PI : ((x + 0.5) / w - 0.5) * 100;
          v = 120 + 50 * Math.Sin(az * 0.31) + 40 * Math.Sin(az * 0.173 + 1) + 30 * Math.Sin(az * 0.071 + 2);
        } else if (kind == 5) {
          v = 255;
        } else if (kind == 6) {
          double az = Math.Atan2(dx, -dz) * 180 / Math.PI, el = Math.Atan2(dy, Math.Sqrt(dx * dx + dz * dz)) * 180 / Math.PI;
          if (Math.Abs(az) < 34 && Math.Abs(el) < 26) v = 255;
        }
        int i = (y * w + x) * 4;
        img[i] = img[i + 1] = img[i + 2] = (byte)Math.Max(0, Math.Min(255, v));
        img[i + 3] = 255;
      }
    }
    return img;
  }
}
'@
}

$dir = Join-Path ([IO.Path]::GetTempPath()) "arx-stereo-check-$PID"
New-Item -ItemType Directory -Force $dir | Out-Null

# The Index as the game logs it: 64 mm, L -54 R 40 U 44 D -55 (the right eye mirrored), 5 degrees between the eyes
$W = 720; $H = 800
$fov = @{
	left  = [pscustomobject]@{ left = -54.0; right = 40.0; up = 44.0; down = -55.0 }
	right = [pscustomobject]@{ left = -40.0; right = 54.0; up = 44.0; down = -55.0 }
}
$halfCant = 2.5 * [math]::PI / 180 / 2
$quat = @{
	left  = [pscustomobject]@{ x = 0.0; y = [math]::Sin($halfCant); z = 0.0; w = [math]::Cos($halfCant) }
	right = [pscustomobject]@{ x = 0.0; y = -[math]::Sin($halfCant); z = 0.0; w = [math]::Cos($halfCant) }
}
$position = @{
	left  = [pscustomobject]@{ x = -0.032; y = 0.0; z = 0.0 }
	right = [pscustomobject]@{ x = 0.032; y = 0.0; z = 0.0 }
}

# The sphere: 1 m away, 30 degrees right, 5 cm across - inside both eyes: with the Index's 40 degrees to the nose and
# the cant, the eyes overlap only within 37.5 degrees of the middle
$azimuth = 30.0
$rad = $azimuth * [math]::PI / 180
$center = [double[]]@([math]::Sin($rad), 0.0, -[math]::Cos($rad))

function Save-Shot([string]$Name, [int]$Kind, [switch]$Swap) {
	foreach ($eye in 'left', 'right') {
		$source = if ($Swap) { if ($eye -eq 'left') { 'right' } else { 'left' } } else { $eye }
		$f = $fov[$source]; $q = $quat[$source]; $p = $position[$source]
		$d2r = [math]::PI / 180
		$bytes = [ArxStereoFixture]::Render($W, $H, [math]::Tan($f.left * $d2r), [math]::Tan($f.right * $d2r), [math]::Tan($f.up * $d2r),
			[math]::Tan($f.down * $d2r), [double[]]@($q.x, $q.y, $q.z, $q.w), [double[]]@($p.x, $p.y, $p.z), $Kind, $center, 0.025)
		$bitmap = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
		$data = $bitmap.LockBits((New-Object System.Drawing.Rectangle 0, 0, $W, $H), [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
		for ($y = 0; $y -lt $H; $y++) {
			[System.Runtime.InteropServices.Marshal]::Copy($bytes, $y * $W * 4, [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $W * 4)
		}
		$bitmap.UnlockBits($data)
		$bitmap.Save((Join-Path $dir "$Name-$eye.bmp"), [System.Drawing.Imaging.ImageFormat]::Bmp)
		$bitmap.Dispose()
	}
}

function New-Status([string[]]$Shots, [int]$Stale = 0) {
	$shotTable = [ordered]@{}
	foreach ($name in $Shots) {
		$shotTable[$name] = [pscustomobject]@{
			left  = [pscustomobject]@{ quat = $quat.left; fov = $fov.left; position = $position.left }
			right = [pscustomobject]@{ quat = $quat.right; fov = $fov.right; position = $position.right }
		}
	}
	[pscustomobject]@{
		vr = [pscustomobject]@{ sim = [pscustomobject]@{
			compositor = [pscustomobject]@{ projectionFrames = 600; pairedFrames = 600 - $Stale; staleFrames = $Stale }
			shots = [pscustomobject]$shotTable
		} }
		stereoFx = [pscustomobject]@{ kind = 'billboard'; azimuth = $azimuth; elevation = 0.0; distance = 1.0 }
	}
}

$files = { param($Name, $Eye) Join-Path $dir "$Name-$Eye.bmp" }
function Leg($Legs, [string]$Name) { $Legs | Where-Object Leg -eq $Name | Select-Object -First 1 }
function Show($Legs) { ($Legs | ForEach-Object { "$($_.Leg) $(if ($_.Pass) { 'pass' } else { 'FAIL' }): $($_.Detail)" }) -join ' | ' }

try {
	Save-Shot 'base' 0
	Save-Shot 'sphere' 1
	Save-Shot 'swapped' 1 -Swap
	Save-Shot 'screen-square' 2
	Save-Shot 'white' 5
	Save-Shot 'middle' 6
	Save-Shot 'locked' 3
	Save-Shot 'stretched' 4
	$status = New-Status @('base', 'sphere', 'swapped', 'screen-square', 'white', 'middle', 'locked', 'stretched')

	Write-Host 'fx: a sphere 1 m away, 30 degrees right, on canted eyes'
	$legs = Test-ArxStereoChange $status 'base' 'sphere' 'fx' $files
	Check 'every fx leg passes' (@($legs | Where-Object { -not $_.Pass }).Count -eq 0 -and $legs.Count -eq 4) (Show $legs)
	$depth = Leg $legs 'depth'
	Check 'the depth leg names the distance near 1 m' ($depth.Detail -match ': (0\.9\d|1\.0\d) m') $depth.Detail
	Write-Host "        $((Leg $legs 'fusion').Detail)" -ForegroundColor DarkGray

	Write-Host 'fx: the eyes swapped'
	$legs = Test-ArxStereoChange $status 'base' 'swapped' 'fx' $files
	Check 'the depth leg fails' (-not (Leg $legs 'depth').Pass) (Show $legs)

	Write-Host "fx: a square in each eye's own screen space"
	$legs = Test-ArxStereoChange $status 'base' 'screen-square' 'fx' $files
	Check 'the depth leg still passes (its middle is the point)' (Leg $legs 'depth').Pass (Show $legs)
	Check 'the fusion leg fails' (-not (Leg $legs 'fusion').Pass) (Show $legs)
	Write-Host "        $((Leg $legs 'fusion').Detail)" -ForegroundColor DarkGray

	Write-Host 'pairing: a stale eye'
	$legs = Test-ArxStereoChange (New-Status @('base', 'sphere') -Stale 3) 'base' 'sphere' 'fx' $files
	Check 'the pairing leg fails' (-not (Leg $legs 'pairing').Pass) (Show $legs)

	Write-Host 'view: both whole eyes, and only the middle'
	$legs = Test-ArxStereoChange $status 'base' 'white' 'view' $files
	Check 'every view leg passes' (@($legs | Where-Object { -not $_.Pass }).Count -eq 0) (Show $legs)
	$legs = Test-ArxStereoChange $status 'base' 'middle' 'view' $files
	Check 'the middle only fails the coverage' (-not (Leg $legs 'covered').Pass) (Show $legs)

	Write-Host 'infinity: head-locked, and stretched over each eye'
	$legs = Test-ArxStereoChange $status 'base' 'locked' 'infinity' $files
	Check 'every infinity leg passes' (@($legs | Where-Object { -not $_.Pass }).Count -eq 0) (Show $legs)
	$legs = Test-ArxStereoChange $status 'base' 'stretched' 'infinity' $files
	Check 'the stretched pattern fails the parallax' (-not (Leg $legs 'parallax').Pass) (Show $legs)

	Write-Host 'no test effect placed'
	$none = New-Status @('base', 'sphere'); $none.stereoFx = [pscustomobject]@{ kind = 'none' }
	$legs = Test-ArxStereoChange $none 'base' 'sphere' 'fx' $files
	Check 'fails, naming the missing effect' (@($legs | Where-Object { -not $_.Pass }).Count -eq 1 -and (Leg $legs 'fx').Detail -match 'stereo-fx place') (Show $legs)
} finally {
	Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failures) { Write-Host "test-stereo-check: $script:failures of $script:checks checks FAILED" -ForegroundColor Red; exit 1 }
Write-Host "test-stereo-check: all $script:checks checks passed" -ForegroundColor Green
exit 0
