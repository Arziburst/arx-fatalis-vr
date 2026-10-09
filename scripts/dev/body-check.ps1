<#
.SYNOPSIS
	Body check (#25): turn the simulated headset's head and give a PASS / FAIL verdict on how the
	body follows it - the numbers in status.json, in one run on the simulated VR backend.

.DESCRIPTION
	Runs smoke-test.ps1 -VrSim basic (no runtime, no SteamVR; every launch rule of D-014) on the test
	save mvp-new-game in a background job and, while it runs, drives the simulated head through the
	test channel and samples status.json every 50 ms:

	  ahead    vr-sim head rot 0 0 0      body = look = heading (player.yaw), offset 0
	  left     vr-sim head rot 40 0 0     beyond the 25° dead zone: the body is dragged to 15° at once,
	                                      then settles on the head (5% of the gap left after ~1.5 s);
	                                      the head never gets more than 25° from it; the camera does
	                                      not move although the model turned under it
	  right    vr-sim head rot -40 0 0    the same to the other side: dragged to -15°, settles on -40°
	  heading  setpos <here> <yaw + 90>   the stick / mouse heading turns: the body turns with it,
	                                      its offset from the heading stays
	  recenter recenter                   a new reference: the body faces forward again, offset 0

	Yaws are engine degrees (a larger yaw turns left); the head's turn is vr.head.yaw - refYaw.
	modelYaw is where the player model really faces: it must be the body yaw.

	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).
	One game launch.

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\body-check.ps1
#>
[CmdletBinding()]
param(
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ VrSim = 'basic'; Seconds = 40; Save = 'mvp-new-game' }
if ($Rebuild) { $jobArgs.Rebuild = $true }

# A status.json left by an earlier run must not be mistaken for this one
Remove-Item (Join-Path $testDir 'status.json') -ErrorAction SilentlyContinue

Write-Host "==> body check: simulated head through the test channel, sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

function Wrap180([double]$a) { $d = (($a % 360) + 360) % 360; if ($d -gt 180) { $d - 360 } else { $d } }
function AngleDiff([double]$a, [double]$b) { [math]::Abs((Wrap180 ($a - $b))) }

$samples = New-Object System.Collections.Generic.List[object]
$started = $null

function Sample([string]$Phase, [double]$Seconds) {
	$end = (Get-Date).AddSeconds($Seconds)
	$lastFrame = -1
	while ((Get-Date) -lt $end -and $job.State -eq 'Running') {
		$s = Read-ArxStatus $testDir
		if ($s -and $s.vr.head -and $s.player -and [int]$s.frame -ne $lastFrame) {
			$lastFrame = [int]$s.frame
			$h = $s.vr.head
			$samples.Add([pscustomobject]@{
				phase = $Phase; t = ((Get-Date) - $script:phaseStart).TotalSeconds; frame = [int]$s.frame
				head = (Wrap180 ([double]$h.yaw - [double]$h.refYaw)); source = [string]$h.source
				references = [int]$h.references
				heading = [double]$s.player.yaw; modelYaw = [double]$s.player.modelYaw
				look = [double]$h.lookYaw; body = [double]$h.bodyYaw; offset = [double]$h.bodyOffset
				dragged = [bool]$h.bodyDragged; drags = [int]$h.bodyDrags
				camYaw = [double]$s.camera.yaw; x = [double]$s.camera.x; z = [double]$s.camera.z
			})
		}
		Start-Sleep -Milliseconds 50
	}
}

function Phase([string]$Name, [string]$Command, [double]$Seconds) {
	if ($Command) {
		$ack = Send-ArxCommand -Lines @($Command) -Dir $testDir
		if ($ack -notmatch 'errors=0 ') { $script:reasons.Add("${Name}: '$Command' failed: $ack") }
		if ($ack -match 'frame=(\d+)') { $script:ackFrame[$Name] = [int]$Matches[1] }
	}
	$script:phaseStart = Get-Date
	Sample $Name $Seconds
}

$reasons = New-Object System.Collections.Generic.List[string]
$ackFrame = @{}
try {
	$null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' -and $s.vr.sim -and $s.vr.head.source -eq 'hmd' -and $s.player } `
		-TimeoutSec 180 -Dir $testDir -What 'gameplay on the simulated headset'
	# Past the level's fade-in and the first reference
	Start-Sleep -Seconds 3
	Phase 'ahead' 'vr-sim head rot 0 0 0' 1.5
	Phase 'left' 'vr-sim head rot 40 0 0' 3.5
	Phase 'right' 'vr-sim head rot -40 0 0' 3.5
	$s = Read-ArxStatus $testDir
	# setpos takes the feet; player.pos is the eye, 170 units above them standing. Invariant numbers: the
	# channel does not read a decimal comma
	$setpos = [string]::Format([cultureinfo]::InvariantCulture, 'setpos {0:0.###} {1:0.###} {2:0.###} {3:0.###}',
		[double]$s.player.x, ([double]$s.player.y + 170), [double]$s.player.z, (([double]$s.player.yaw + 90) % 360))
	Phase 'heading' $setpos 1.5
	Phase 'recenter' 'recenter' 1.5
} catch {
	$reasons.Add("$_")
}

$output = @(Receive-Job $job -Wait)
Remove-Job $job
$smokeExit = -1
foreach ($line in $output) {
	if ("$line" -match '^SMOKE_EXIT=(-?\d+)$') { $smokeExit = [int]$Matches[1] } else { Write-Host $line }
}
if ($smokeExit -eq 2) {
	Write-Host '==> SKIPPED: the smoke test was skipped'
	exit 2
}
if ($smokeExit -ne 0) { $reasons.Add("smoke test exit code $smokeExit") }

Write-Host ''
Write-Host ("{0,-9} {1,6} {2,8} {3,8} {4,8} {5,8} {6,8} {7,8} {8,8} {9,5} {10,5}" -f 'phase', 't', 'head', 'heading', 'look', 'body', 'model', 'offset', 'camYaw', 'drag', 'drags')
foreach ($r in $samples) {
	if ($r.t -lt 0.25 -or [int]($r.t * 10) % 5 -eq 0) {
		Write-Host ("{0,-9} {1,6:0.00} {2,8:0.00} {3,8:0.00} {4,8:0.00} {5,8:0.00} {6,8:0.00} {7,8:0.00} {8,8:0.00} {9,5} {10,5}" -f $r.phase, $r.t, $r.head, $r.heading, $r.look, $r.body, $r.modelYaw, $r.offset, $r.camYaw, $r.dragged, $r.drags)
	}
}

function Expect([string]$phase, [string]$what, [double]$expected, [double]$actual, [double]$tolerance, [switch]$Angle) {
	$diff = if ($Angle) { AngleDiff $expected $actual } else { [math]::Abs($expected - $actual) }
	if ($diff -gt $tolerance) { $script:reasons.Add(("{0}: {1} {2:0.###} (expected {3:0.###})" -f $phase, $what, $actual, $expected)) }
}

$log = Get-ArxLogPath
$dragEvents = @()
if (Test-Path $log) {
	$dragEvents = @(Select-String -Path $log -Pattern 'vr\.body\.drag state=start head=(-?[\d.]+) body=(-?[\d.]+)' | ForEach-Object {
		[pscustomobject]@{
			head = [double]::Parse($_.Matches[0].Groups[1].Value, [cultureinfo]::InvariantCulture)
			body = [double]::Parse($_.Matches[0].Groups[2].Value, [cultureinfo]::InvariantCulture)
		}
	})
}

$tol = 0.1
$by = @{}
foreach ($name in 'ahead', 'left', 'right', 'heading', 'recenter') {
	# Only the frames that already show the phase's head (the command lands a frame or two later)
	$rows = @($samples | Where-Object { $_.phase -eq $name })
	$by[$name] = $rows
	if ($rows.Count -lt 5) { $reasons.Add("phase ${name}: only $($rows.Count) samples") }
	foreach ($r in $rows) {
		if ($r.source -ne 'hmd') { $reasons.Add("phase ${name}: source $($r.source)"); break }
	}
}

if ($reasons.Count -eq 0) {
	# Every frame: the look is heading + head, the body is heading + offset, the model faces the body
	# (the model takes the body of the frame before: a frame's turn at most), the head never more than
	# 25° from the body, the camera looks where the look is. Not in the frame that ran a phase's command:
	# its status.json has the command's heading but the head of before it
	foreach ($r in $samples) {
		if ($ackFrame.ContainsKey($r.phase) -and $r.frame -le $ackFrame[$r.phase]) { continue }
		$ok = (AngleDiff $r.look ($r.heading + $r.head)) -le $tol -and (AngleDiff $r.body ($r.heading + $r.offset)) -le $tol -and
		      (AngleDiff $r.camYaw $r.look) -le $tol -and (AngleDiff $r.head $r.offset) -le 25.05
		if (-not $ok) { $reasons.Add(("{0} t={1:0.00}: look {2:0.00} body {3:0.00} offset {4:0.00} head {5:0.00} heading {6:0.00} camYaw {7:0.00}" -f $r.phase, $r.t, $r.look, $r.body, $r.offset, $r.head, $r.heading, $r.camYaw)); break }
	}

	$ahead = $by['ahead'][-1]
	Expect 'ahead' 'head' 0 $ahead.head $tol -Angle
	Expect 'ahead' 'offset' 0 $ahead.offset $tol -Angle
	Expect 'ahead' 'model yaw' $ahead.heading $ahead.modelYaw $tol -Angle

	foreach ($side in @(@{ name = 'left'; head = 40.0; drag = 15.0 }, @{ name = 'right'; head = -40.0; drag = -15.0 })) {
		$rows = @($by[$side.name] | Where-Object { (AngleDiff $_.head $side.head) -le $tol })
		if ($rows.Count -lt 5) { $reasons.Add("$($side.name): the head did not turn to $($side.head)"); continue }
		$last = $rows[-1]
		# Dragged at once to the dead zone's edge: the drag event of that frame names the body (the samples
		# come a few frames of relaxation later)
		$drag = $dragEvents | Where-Object { [math]::Abs($_.head - $side.head) -le $tol } | Select-Object -First 1
		if (-not $drag) { $reasons.Add("$($side.name): no vr.body.drag start event with head $($side.head)") }
		else { Expect $side.name 'body at the drag' $side.drag $drag.body $tol -Angle }
		# Then it turns toward the head without going back, and settles on it
		for ($i = 1; $i -lt $rows.Count; $i++) {
			if ((AngleDiff $rows[$i].offset $side.head) -gt (AngleDiff $rows[$i - 1].offset $side.head) + 0.01) {
				$reasons.Add("$($side.name): the body turned away from the head at t=$([math]::Round($rows[$i].t, 2))"); break
			}
		}
		Expect $side.name 'settled offset' $side.head $last.offset 0.3 -Angle
		Expect $side.name 'model yaw' $last.body $last.modelYaw $tol -Angle
		# 5% of the 25° gap left (1.25°) after about 1.5 s
		$settle = @($rows | Where-Object { (AngleDiff $_.offset $side.head) -le 1.25 })
		if ($settle.Count -eq 0) { $reasons.Add("$($side.name): never within 1.25° of the head") }
		else {
			# t counts from the acknowledgement of the head command: the turn was in that frame or the one before
			$t = $settle[0].t
			if ($t -lt 1.1 -or $t -gt 2.1) { $reasons.Add(("{0}: settled within 1.25° after {1:0.00} s (expected about 1.5)" -f $side.name, $t)) }
		}
		# The camera stays where the head is although the model turned under it (the eye compensation)
		$moved = [math]::Sqrt([math]::Pow($last.x - $ahead.x, 2) + [math]::Pow($last.z - $ahead.z, 2))
		if ($moved -gt 0.5) { $reasons.Add(("{0}: the camera moved {1:0.00} units while only the head turned" -f $side.name, $moved)) }
	}
	$right = $by['right'][-1]
	if ($right.drags -lt 2) { $reasons.Add("right: $($right.drags) drag episodes (expected 2 or more)") }

	$heading = $by['heading'][-1]
	Expect 'heading' 'heading' (($right.heading + 90) % 360) $heading.heading $tol -Angle
	Expect 'heading' 'offset' $right.offset $heading.offset 0.3 -Angle
	Expect 'heading' 'body' ($heading.heading - 40) $heading.body 0.3 -Angle
	Expect 'heading' 'model yaw' $heading.body $heading.modelYaw $tol -Angle

	$recenter = $by['recenter'][-1]
	Expect 'recenter' 'references' ($heading.references + 1) $recenter.references 0
	Expect 'recenter' 'head' 0 $recenter.head $tol -Angle
	Expect 'recenter' 'offset' 0 $recenter.offset $tol -Angle
	Expect 'recenter' 'model yaw' $recenter.heading $recenter.modelYaw $tol -Angle
}

if (Test-Path $log) {
	Write-Host ''
	Write-Host "vr.body / vr.head.reference events in $log"
	Select-String -Path $log -Pattern 'GameEvent:.*(vr\.body|vr\.head\.reference)' | ForEach-Object { Write-Host "  $($_.Line)" }
}

Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host "==> PASS body check: $($samples.Count) samples; head ±40° drags the body to ±15° at once, it settles on the head in ~1.5 s, the camera stays put, a heading turn carries the offset, recenter faces the body forward"
	exit 0
}
Write-Host "==> FAIL body check: $($reasons -join '; ')"
exit 1
