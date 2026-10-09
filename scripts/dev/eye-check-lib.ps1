# The two-eyes check's judge (research row F-06, #112): from the simulated headset's status.json
# (vr.sim, written by the game's simulated compositor) to one verdict per leg. Shared by eye-check.ps1
# and its self-test test-eye-check.ps1; no game, no files.
#
# The legs, in order:
#   world       both eyes see a world in the still shot (not black, not the bare background) - the
#               black-world bug of #215 fails here
#   pairing     every submitted frame showed both eye images from one frame (no stale eye, F-116)
#   separation  the eyes are the IPD apart (0.5 mm)
#   difference  the left and right pictures differ above the same-eye floor (how much one eye's
#               picture changes by itself between two still shots), and are not two unrelated pictures
#   parallax    the left picture is shifted right of the right one (> 0); swapped eyes give < 0
#   yaw         a 25 degree head yaw turned both eyes by 25 degrees and changed both pictures above the floor

# The shots eye-check.vrsim takes
$EyeCheckShots = @{ Still = 'still-a'; Again = 'still-b'; Yaw = 'yaw25' }
# The yaw of the third shot, degrees
$EyeCheckYaw = 25.0
# How far the picture difference must rise above the floor, and the least difference that counts at all (luma levels)
$EyeCheckFloorFactor = 3.0
$EyeCheckMinDiff = 0.5
# Two pictures that differ by more than this (mean luma levels) are not two views of one scene
$EyeCheckMaxDiff = 60.0

function New-EyeCheckLeg([string]$Name, [bool]$Pass, [string]$Detail) {
	[pscustomobject]@{ Leg = $Name; Pass = $Pass; Detail = $Detail }
}

function Format-EyeNumber($Value) {
	if ($null -eq $Value) { return '?' }
	return ([double]$Value).ToString('0.###', [Globalization.CultureInfo]::InvariantCulture)
}

# Wrap a yaw difference into -180..180
function Get-EyeAngleDelta([double]$To, [double]$From) {
	$d = ($To - $From) % 360.0
	if ($d -gt 180.0) { $d -= 360.0 }
	if ($d -lt -180.0) { $d += 360.0 }
	return $d
}

# $Status: the parsed status.json of a --vr-sim run with eye-check.vrsim. Returns the legs, in order.
function Test-ArxEyeCheck($Status) {

	$sim = if ($Status -and $Status.vr) { $Status.vr.sim } else { $null }
	if (-not $sim) {
		return @(New-EyeCheckLeg 'simulator' $false 'status.json has no vr.sim block - the game did not run on the simulated headset')
	}
	$shots = $sim.shots
	$missing = @($EyeCheckShots.Values | Where-Object { -not ($shots -and $shots.PSObject.Properties[$_]) })
	if ($missing) {
		return @(New-EyeCheckLeg 'shots' $false "missing shots: $($missing -join ', ') (timeline: $($sim.timeline), $($sim.pending) lines pending)")
	}
	$still = $shots.($EyeCheckShots.Still)
	$again = $shots.($EyeCheckShots.Again)
	$yaw = $shots.($EyeCheckShots.Yaw)
	$legs = @()

	# world
	$verdicts = "left $($still.left.scene.verdict), right $($still.right.scene.verdict), flat frame $($still.quad.stats.verdict)"
	$legs += New-EyeCheckLeg 'world' ($still.left.scene.verdict -eq 'world' -and $still.right.scene.verdict -eq 'world') $verdicts

	# pairing
	$c = $sim.compositor
	$frames = [int64]$c.projectionFrames
	$paired = [int64]$c.pairedFrames
	$rate = if ($frames -gt 0) { $paired / $frames } else { 0.0 }
	$legs += New-EyeCheckLeg 'pairing' ($frames -gt 0 -and $paired -eq $frames -and [int64]$c.staleFrames -eq 0) `
		("{0} of {1} frames paired ({2:P1}), {3} with a stale eye" -f $paired, $frames, $rate, $c.staleFrames)

	# separation
	$ipd = [double]$sim.view.ipd
	$separation = [double]$still.lr.separation
	$legs += New-EyeCheckLeg 'separation' ([math]::Abs($separation - $ipd) -le 0.0005) `
		"$(Format-EyeNumber ($separation * 1000)) mm apart, IPD $(Format-EyeNumber ($ipd * 1000)) mm"

	# difference
	$floor = [math]::Max([double]$again.versus.($EyeCheckShots.Still).left, [double]$again.versus.($EyeCheckShots.Still).right)
	$threshold = [math]::Max($EyeCheckFloorFactor * $floor, $EyeCheckMinDiff)
	$lr = [double]$still.lr.diff
	$legs += New-EyeCheckLeg 'difference' ($lr -gt $threshold -and $lr -lt $EyeCheckMaxDiff) `
		"left vs right $(Format-EyeNumber $lr), same-eye floor $(Format-EyeNumber $floor) (needs > $(Format-EyeNumber $threshold) and < $EyeCheckMaxDiff)"

	# parallax
	$shift = [int]$still.lr.shift
	$legs += New-EyeCheckLeg 'parallax' ($shift -gt 0) `
		"left picture $shift columns right of the right one ($(Format-EyeNumber ([double]$still.lr.shiftFraction * 100)) % of the width; > 0 expected$(if ($still.swapEyes) { ', swap-eyes fault on' }))"

	# yaw
	$moved = @(foreach ($eye in 'left', 'right') {
		[pscustomobject]@{
			Eye = $eye
			Diff = [double]$yaw.versus.($EyeCheckShots.Still).$eye
			Turn = Get-EyeAngleDelta ([double]$yaw.$eye.rot.yaw) ([double]$still.$eye.rot.yaw)
		}
	})
	$turned = @($moved | Where-Object { [math]::Abs($_.Turn - $EyeCheckYaw) -le 0.5 }).Count -eq 2
	$changed = @($moved | Where-Object { $_.Diff -gt $threshold }).Count -eq 2
	$legs += New-EyeCheckLeg 'yaw' ($turned -and $changed) `
		(($moved | ForEach-Object { "$($_.Eye) eye turned $(Format-EyeNumber $_.Turn) deg, picture changed $(Format-EyeNumber $_.Diff)" }) -join '; ') + " (needs $EyeCheckYaw deg and > $(Format-EyeNumber $threshold))"

	return $legs
}
