# The stereo check of an effect (#22): what changed between two shots of the simulated headset, judged as the two
# eyes see it. Dot-sourced by xrs-lib.ps1 (the @stereo step) and by its self-test test-stereo-check.ps1.
#
# Every pixel of each eye becomes a direction in the head's frame - the eye's image tangents (its four FOV angles)
# turned by the eye's orientation as the shot recorded it (status.json vr.sim.shots.<name>.left/right: quat, fov,
# position), then into the head between the eyes - so the two eye images are compared as directions, not pixels:
# the eyes of a headset see different parts of the world (asymmetric, canted), the same pixel is not the same
# direction. The change from the base shot to the effect shot (luma above a noise threshold) is profiled along the
# azimuth; the shift that best aligns the left eye's profile with the right eye's is the disparity, and with the eye
# separation it is a distance. The modes:
#
#   fx        the test effect of `stereo-fx place` (status.json stereoFx: where it is from the head), in a window
#             around its direction. Legs: covered (both eyes see it change), depth (the disparity puts it at its
#             distance, in front: a positive disparity), fusion (both eyes see it at the size its distance from each
#             eye gives - per-eye screen squares on canted eyes differ by more - and at the same height)
#   view      something over the whole view (a fade): covered (each eye changed over most of its pixels) and equal
#             (both eyes changed by the same mean amount)
#   infinity  a head-locked overlay at infinity (Magic Sight): covered (both eyes see it), parallax (no disparity:
#             the pattern is at the same direction for both eyes; a pattern stretched over each eye's own image is
#             tens of degrees apart on an asymmetric headset, divergent)
# Every mode also has the leg pairing: every frame showed both eye images from one frame (the compositor's count).

$script:StereoNoise = 12          # luma levels: a change below this is the flicker of torches and fire between two frames
$script:StereoFxWindow = 15.0     # degrees around the test effect's direction
$script:StereoFxBin = 0.1         # degrees per azimuth bin for an effect
$script:StereoFxMaxShift = 12.0   # degrees: the largest disparity looked for (a thing 0.3 m from the eyes)
$script:StereoViewBin = 0.25      # degrees per bin for whole-view patterns
$script:StereoViewMaxShift = 40.0 # degrees: a pattern stretched over each eye's image lands this far apart
$script:StereoDepthTolerance = 0.2      # the measured distance within 20 % of where the effect is
$script:StereoFusionTolerance = 0.04    # the two eyes' sizes within 4 % of what their distances give
$script:StereoMaxVertical = 0.3         # degrees between the heights the two eyes see it at
$script:StereoViewCovered = 0.9         # share of each eye's pixels a whole-view effect changes
$script:StereoViewEqual = 0.1           # the two eyes' mean change within 10 %
$script:StereoInfinityCovered = 0.1     # share of each eye's pixels an overlay changes, at least
$script:StereoInfinityParallax = 0.5    # degrees of disparity an overlay at infinity may show

if (-not ('ArxStereoCheck' -as [type])) {
Add-Type @'
using System;
public static class ArxStereoCheck {
  public sealed class Eye {
    public int Width, Height;
    public double TanLeft, TanRight, TanUp, TanDown;   // OpenXR: left and down negative
    public double Qx, Qy, Qz, Qw = 1;                  // the eye in the room (OpenXR: x right, y up, -z forward)
  }
  public sealed class Result {
    public long Pixels, Changed;
    public double Weight, MeanChange, AzMean, ElMean, AzStd, ElStd;
    public double[] Profile;
    public long[] Count;
  }
  const double Deg = 180.0 / Math.PI;
  public static void Rotate(double qx, double qy, double qz, double qw, double vx, double vy, double vz,
                            out double rx, out double ry, out double rz) {
    double tx = 2 * (qy * vz - qz * vy), ty = 2 * (qz * vx - qx * vz), tz = 2 * (qx * vy - qy * vx);
    rx = vx + qw * tx + (qy * tz - qz * ty);
    ry = vy + qw * ty + (qz * tx - qx * tz);
    rz = vz + qw * tz + (qx * ty - qy * tx);
  }
  static double Luma(byte[] p, int i) { return (p[i + 2] * 299 + p[i + 1] * 587 + p[i] * 114) / 1000.0; }
  // a, b: 32bpp BGRA rows from the top. head: the inverse of the head's orientation (x, y, z, w).
  // Only pixels whose direction is within winHalf degrees of (winAz, winEl) count (winHalf <= 0: all).
  public static Result Measure(byte[] a, byte[] b, Eye eye, double[] head, int noise,
                               double winAz, double winEl, double winHalf, double azMin, double binDeg, int bins) {
    var r = new Result { Profile = new double[bins], Count = new long[bins] };
    double[] binWeight = new double[bins];
    double sw = 0, saz = 0, saz2 = 0, sel = 0, sel2 = 0, sumChange = 0;
    for (int y = 0; y < eye.Height; y++) {
      double ty = eye.TanUp - (eye.TanUp - eye.TanDown) * (y + 0.5) / eye.Height;
      for (int x = 0; x < eye.Width; x++) {
        double tx = eye.TanLeft + (eye.TanRight - eye.TanLeft) * (x + 0.5) / eye.Width;
        double rx, ry, rz, hx, hy, hz;
        Rotate(eye.Qx, eye.Qy, eye.Qz, eye.Qw, tx, ty, -1, out rx, out ry, out rz);
        Rotate(head[0], head[1], head[2], head[3], rx, ry, rz, out hx, out hy, out hz);
        double az = Math.Atan2(hx, -hz) * Deg;
        double el = Math.Atan2(hy, Math.Sqrt(hx * hx + hz * hz)) * Deg;
        if (winHalf > 0 && (Math.Abs(az - winAz) > winHalf || Math.Abs(el - winEl) > winHalf)) continue;
        int i = (y * eye.Width + x) * 4;
        double d = Math.Abs(Luma(a, i) - Luma(b, i));
        r.Pixels++;
        sumChange += d;
        int bin = (int)Math.Floor((az - azMin) / binDeg);
        bool inBins = bin >= 0 && bin < bins;
        if (inBins) r.Count[bin]++;
        if (d <= noise) continue;
        double w = d - noise;
        r.Changed++;
        sw += w; saz += w * az; saz2 += w * az * az; sel += w * el; sel2 += w * el * el;
        if (inBins) binWeight[bin] += w;
      }
    }
    r.Weight = sw;
    r.MeanChange = r.Pixels > 0 ? sumChange / r.Pixels : 0;
    if (sw > 0) {
      r.AzMean = saz / sw; r.ElMean = sel / sw;
      r.AzStd = Math.Sqrt(Math.Max(0, saz2 / sw - r.AzMean * r.AzMean));
      r.ElStd = Math.Sqrt(Math.Max(0, sel2 / sw - r.ElMean * r.ElMean));
    }
    for (int k = 0; k < bins; k++) r.Profile[k] = r.Count[k] > 0 ? binWeight[k] / r.Count[k] : 0;
    return r;
  }
  // The shift s (bins, refined between bins) that best matches left(i) with right(i - s): the left profile moved
  // right by s. Pearson correlation over the bins both eyes see; corr is -2 when no shift had minPairs bins.
  public static double BestShift(double[] pl, long[] cl, double[] pr, long[] cr, int maxShift, int minPairs, out double corr) {
    int n = pl.Length;
    double[] c = new double[2 * maxShift + 1];
    int best = -1;
    for (int k = 0; k < c.Length; k++) {
      int s = k - maxShift;
      double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; int m = 0;
      for (int i = 0; i < n; i++) {
        int j = i - s;
        if (j < 0 || j >= n || cl[i] == 0 || cr[j] == 0) continue;
        double x = pl[i], yv = pr[j];
        sx += x; sy += yv; sxx += x * x; syy += yv * yv; sxy += x * yv; m++;
      }
      c[k] = -2;
      if (m >= minPairs) {
        double vx = sxx - sx * sx / m, vy = syy - sy * sy / m;
        if (vx > 1e-9 && vy > 1e-9) c[k] = (sxy - sx * sy / m) / Math.Sqrt(vx * vy);
      }
      if (c[k] > -2 && (best < 0 || c[k] > c[best])) best = k;
    }
    corr = best < 0 ? -2 : c[best];
    if (best < 0) return 0;
    double shift = best - maxShift;
    if (best > 0 && best < c.Length - 1 && c[best - 1] > -2 && c[best + 1] > -2) {
      double den = c[best - 1] - 2 * c[best] + c[best + 1];
      if (Math.Abs(den) > 1e-12) shift += 0.5 * (c[best - 1] - c[best + 1]) / den;
    }
    return shift;
  }
}
'@
}

function New-StereoLeg([string]$Name, [bool]$Pass, [string]$Detail) {
	[pscustomobject]@{ Leg = $Name; Pass = $Pass; Detail = $Detail }
}

function Format-StereoNumber($Value, [string]$Format = '0.###') {
	if ($null -eq $Value) { return '?' }
	return ([double]$Value).ToString($Format, [Globalization.CultureInfo]::InvariantCulture)
}

# A BMP / PNG as 32bpp BGRA bytes from the top row
function Read-StereoImage([string]$Path) {
	Add-Type -AssemblyName System.Drawing
	$image = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Path))
	try {
		$rect = New-Object System.Drawing.Rectangle 0, 0, $image.Width, $image.Height
		$data = $image.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
		$bytes = New-Object byte[] ($image.Width * $image.Height * 4)
		for ($y = 0; $y -lt $image.Height; $y++) {
			[System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $bytes, $y * $image.Width * 4, $image.Width * 4)
		}
		$image.UnlockBits($data)
		[pscustomobject]@{ Width = $image.Width; Height = $image.Height; Bytes = $bytes }
	} finally {
		$image.Dispose()
	}
}

# The eye of a shot as ArxStereoCheck needs it, from status.json's vr.sim.shots.<name>.left / .right
function ConvertTo-StereoEye($ShotEye, $Image) {
	$eye = New-Object ArxStereoCheck+Eye
	$eye.Width = $Image.Width; $eye.Height = $Image.Height
	$rad = [math]::PI / 180.0
	$eye.TanLeft = [math]::Tan([double]$ShotEye.fov.left * $rad)
	$eye.TanRight = [math]::Tan([double]$ShotEye.fov.right * $rad)
	$eye.TanUp = [math]::Tan([double]$ShotEye.fov.up * $rad)
	$eye.TanDown = [math]::Tan([double]$ShotEye.fov.down * $rad)
	$eye.Qx = [double]$ShotEye.quat.x; $eye.Qy = [double]$ShotEye.quat.y; $eye.Qz = [double]$ShotEye.quat.z; $eye.Qw = [double]$ShotEye.quat.w
	$eye
}

# The inverse of the head's orientation: the normalized mean of the eyes' quaternions (the head is halfway between)
function Get-StereoHeadInverse($Left, $Right) {
	$l = @([double]$Left.quat.x, [double]$Left.quat.y, [double]$Left.quat.z, [double]$Left.quat.w)
	$r = @([double]$Right.quat.x, [double]$Right.quat.y, [double]$Right.quat.z, [double]$Right.quat.w)
	$dot = $l[0] * $r[0] + $l[1] * $r[1] + $l[2] * $r[2] + $l[3] * $r[3]
	$sign = if ($dot -lt 0) { -1.0 } else { 1.0 }
	$q = @(0..3 | ForEach-Object { $l[$_] + $sign * $r[$_] })
	$len = [math]::Sqrt(($q | ForEach-Object { $_ * $_ } | Measure-Object -Sum).Sum)
	# The conjugate of the unit quaternion is its inverse
	# (each element in parentheses: the comma binds tighter than the division)
	[double[]]@(((0 - $q[0]) / $len), ((0 - $q[1]) / $len), ((0 - $q[2]) / $len), ($q[3] / $len))
}

<#
 Judge the change from shot $Base to shot $Shot.
 $Status: status.json after both shots (vr.sim.shots, vr.sim.compositor, stereoFx). $Files: a scriptblock giving the
 file of a shot and eye ({ param($Name, $Eye) ... }). Returns the legs in order, each with Leg, Pass and Detail.
#>
function Test-ArxStereoChange($Status, [string]$Base, [string]$Shot, [string]$Mode, [scriptblock]$Files) {

	$sim = if ($Status -and $Status.vr) { $Status.vr.sim } else { $null }
	if (-not $sim) { return @(New-StereoLeg 'simulator' $false 'status.json has no vr.sim block - not a run on the simulated headset') }
	foreach ($name in $Base, $Shot) {
		if (-not ($sim.shots -and $sim.shots.PSObject.Properties[$name])) { return @(New-StereoLeg 'shots' $false "no shot '$name' in status.json") }
	}
	$shotEyes = $sim.shots.$Shot
	if (-not $shotEyes.left.quat -or -not $shotEyes.left.fov) { return @(New-StereoLeg 'shots' $false "shot '$Shot' has no eye orientation / FOV (a game from before #22)") }
	$legs = @()

	# pairing
	$c = $sim.compositor
	$legs += New-StereoLeg 'pairing' ([int64]$c.projectionFrames -gt 0 -and [int64]$c.pairedFrames -eq [int64]$c.projectionFrames -and [int64]$c.staleFrames -eq 0) `
		"$($c.pairedFrames) of $($c.projectionFrames) frames paired, $($c.staleFrames) with a stale eye"

	$head = Get-StereoHeadInverse $shotEyes.left $shotEyes.right
	$p0 = $shotEyes.left.position; $p1 = $shotEyes.right.position
	$separation = [math]::Sqrt([math]::Pow([double]$p1.x - [double]$p0.x, 2) + [math]::Pow([double]$p1.y - [double]$p0.y, 2) + [math]::Pow([double]$p1.z - [double]$p0.z, 2))

	switch ($Mode) {
		'fx' {
			$fx = $Status.stereoFx
			if (-not $fx -or $fx.kind -eq 'none' -or $null -eq $fx.azimuth) { return $legs + @(New-StereoLeg 'fx' $false 'no test effect placed (stereo-fx place)') }
			$winAz = [double]$fx.azimuth; $winEl = [double]$fx.elevation; $distance = [double]$fx.distance
			$bin = $script:StereoFxBin
			$azMin = $winAz - $script:StereoFxWindow - $script:StereoFxMaxShift
			$bins = [int][math]::Ceiling((2 * ($script:StereoFxWindow + $script:StereoFxMaxShift)) / $bin)
			$m = @{}
			foreach ($eyeName in 'left', 'right') {
				$a = Read-StereoImage (& $Files $Base $eyeName)
				$b = Read-StereoImage (& $Files $Shot $eyeName)
				$eye = ConvertTo-StereoEye $shotEyes.$eyeName $b
				$m[$eyeName] = [ArxStereoCheck]::Measure($a.Bytes, $b.Bytes, $eye, $head, $script:StereoNoise, $winAz, $winEl, $script:StereoFxWindow, $azMin, $bin, $bins)
			}
			$l = $m.left; $r = $m.right

			# covered
			$ratio = if ($r.Weight -gt 0) { $l.Weight / $r.Weight } else { 0 }
			$legs += New-StereoLeg 'covered' ($l.Changed -ge 50 -and $r.Changed -ge 50 -and $ratio -gt 0.5 -and $ratio -lt 2) `
				"$($fx.kind) changed $($l.Changed) / $($r.Changed) pixels (left / right) within $($script:StereoFxWindow) deg of its direction, change $(Format-StereoNumber $ratio '0.00') x the right eye's (needs 50 each, 0.5-2 x)"

			# depth
			$corr = 0.0
			$shiftBins = [ArxStereoCheck]::BestShift($l.Profile, $l.Count, $r.Profile, $r.Count, [int]($script:StereoFxMaxShift / $bin), 20, [ref]$corr)
			$disparity = $shiftBins * $bin
			$rad = [math]::PI / 180.0
			$measured = if ($disparity -gt 0) { $separation * [math]::Cos($winAz * $rad) / ([math]::Tan($disparity * $rad) * [math]::Cos($winEl * $rad)) } else { [double]::PositiveInfinity }
			$depthOk = $corr -gt 0.5 -and $disparity -gt 0 -and [math]::Abs($measured / $distance - 1) -le $script:StereoDepthTolerance
			$legs += New-StereoLeg 'depth' $depthOk `
				"disparity $(Format-StereoNumber $disparity '0.00') deg (left image $(if ($disparity -ge 0) { 'right' } else { 'LEFT' }) of the right one, correlation $(Format-StereoNumber $corr '0.00')): $(Format-StereoNumber $measured '0.00') m with the eyes $(Format-StereoNumber ($separation * 1000) '0.0') mm apart; the effect is $(Format-StereoNumber $distance '0.00') m away at $(Format-StereoNumber $winAz '0.0') deg (needs within $($script:StereoDepthTolerance * 100) %, in front)"

			# fusion: each eye sees it at the size its own distance gives - the left eye is nearer by about half the
			# eye separation times the sine of the azimuth - and at one height
			$expected = 1.0 - $separation * [math]::Sin($winAz * $rad) * [math]::Cos($winEl * $rad) / $distance
			$sizeRatio = if ($r.AzStd -gt 0) { $l.AzStd / $r.AzStd } else { 0 }
			$vertical = [math]::Abs($l.ElMean - $r.ElMean)
			$fusionOk = [math]::Abs($sizeRatio / $expected - 1) -le $script:StereoFusionTolerance -and $vertical -le $script:StereoMaxVertical
			$legs += New-StereoLeg 'fusion' $fusionOk `
				"width left / right $(Format-StereoNumber $sizeRatio '0.000') (spread $(Format-StereoNumber $l.AzStd '0.00') / $(Format-StereoNumber $r.AzStd '0.00') deg), $(Format-StereoNumber $expected '0.000') from the distances (needs within $($script:StereoFusionTolerance * 100) %); heights $(Format-StereoNumber $l.ElMean '0.00') / $(Format-StereoNumber $r.ElMean '0.00') deg, $(Format-StereoNumber $vertical '0.00') apart (needs <= $($script:StereoMaxVertical))"
		}
		'view' {
			$m = @{}
			foreach ($eyeName in 'left', 'right') {
				$a = Read-StereoImage (& $Files $Base $eyeName)
				$b = Read-StereoImage (& $Files $Shot $eyeName)
				$eye = ConvertTo-StereoEye $shotEyes.$eyeName $b
				$m[$eyeName] = [ArxStereoCheck]::Measure($a.Bytes, $b.Bytes, $eye, $head, $script:StereoNoise, 0, 0, 0, -90, 1.0, 180)
			}
			$l = $m.left; $r = $m.right
			$shareL = if ($l.Pixels) { $l.Changed / $l.Pixels } else { 0 }
			$shareR = if ($r.Pixels) { $r.Changed / $r.Pixels } else { 0 }
			$legs += New-StereoLeg 'covered' ($shareL -ge $script:StereoViewCovered -and $shareR -ge $script:StereoViewCovered) `
				"changed $(Format-StereoNumber ($shareL * 100) '0.0') % / $(Format-StereoNumber ($shareR * 100) '0.0') % of the left / right eye's pixels (needs $($script:StereoViewCovered * 100) % each)"
			$equal = if ($r.MeanChange -gt 0) { $l.MeanChange / $r.MeanChange } else { 0 }
			$legs += New-StereoLeg 'equal' ([math]::Abs($equal - 1) -le $script:StereoViewEqual) `
				"mean change $(Format-StereoNumber $l.MeanChange '0.0') / $(Format-StereoNumber $r.MeanChange '0.0') luma levels (needs within $($script:StereoViewEqual * 100) %)"
		}
		'infinity' {
			$bin = $script:StereoViewBin
			$bins = [int](180 / $bin)
			$m = @{}
			foreach ($eyeName in 'left', 'right') {
				$a = Read-StereoImage (& $Files $Base $eyeName)
				$b = Read-StereoImage (& $Files $Shot $eyeName)
				$eye = ConvertTo-StereoEye $shotEyes.$eyeName $b
				$m[$eyeName] = [ArxStereoCheck]::Measure($a.Bytes, $b.Bytes, $eye, $head, $script:StereoNoise, 0, 0, 0, -90, $bin, $bins)
			}
			$l = $m.left; $r = $m.right
			$shareL = if ($l.Pixels) { $l.Changed / $l.Pixels } else { 0 }
			$shareR = if ($r.Pixels) { $r.Changed / $r.Pixels } else { 0 }
			$legs += New-StereoLeg 'covered' ($shareL -ge $script:StereoInfinityCovered -and $shareR -ge $script:StereoInfinityCovered) `
				"changed $(Format-StereoNumber ($shareL * 100) '0.0') % / $(Format-StereoNumber ($shareR * 100) '0.0') % of the left / right eye's pixels (needs $($script:StereoInfinityCovered * 100) % each)"
			$corr = 0.0
			$shiftBins = [ArxStereoCheck]::BestShift($l.Profile, $l.Count, $r.Profile, $r.Count, [int]($script:StereoViewMaxShift / $bin), 40, [ref]$corr)
			$disparity = $shiftBins * $bin
			$legs += New-StereoLeg 'parallax' ($corr -gt 0.5 -and [math]::Abs($disparity) -le $script:StereoInfinityParallax) `
				"the pattern's disparity $(Format-StereoNumber $disparity '0.00') deg (correlation $(Format-StereoNumber $corr '0.00'); negative: the eyes would have to diverge; needs within $($script:StereoInfinityParallax) deg of 0: at infinity)"
		}
		default { return $legs + @(New-StereoLeg 'mode' $false "unknown mode '$Mode' (fx, view, infinity)") }
	}
	return $legs
}
