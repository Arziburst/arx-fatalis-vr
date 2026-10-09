<#
.SYNOPSIS
	Head check (#24): drive the first-person camera with a simulated head pose through the test
	channel in one flat run and give a PASS / FAIL verdict on the camera numbers in status.json.

.DESCRIPTION
	Runs smoke-test.ps1 (flat, isolated, every launch rule of D-014) on the test save mvp-new-game
	(the start cell, controls free) in a background job with a command list of phases, each one
	command followed by a few 'status' commands, while this script samples status.json every 100 ms:

	  flat    comfort on (no view bobbing)        no head: the camera is the engine's (yaw = body)
	  ahead   headpose 0 0 0  0 1.7 0             the reference is taken: camera = body, offset 0
	  turn    headpose 30 -20 10  0 1.7 0         yaw +30 (left), pitch 20 down, roll 10 left; no offset
	  move    headpose 0 0 0  0.3 1.5 0           30 cm right, 20 cm lower: offset (30, 20, 0) turned
	                                              with the body's yaw, the camera moved by it
	  lean    headpose 0 0 0  1.0 1.7 0           1 m to the right: clamped to the 0.6 m lean limit
	  recenter recenter                           a new reference from the leaning head: offset 0
	  off     headpose off                        no head again: the camera is the engine's

	A sample belongs to a phase when its cmdSeq points at a 'status' after the phase's command (the
	status of a frame shows the camera of the frame before, so the first 'status' is skipped too).
	The engine's angles are degrees in [0, 360): positive pitch looks down, the yaw grows to the left.
	'shot head-ahead' and 'shot head-turn' capture both views for a look by eye.

	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\head-check.ps1
#>
[CmdletBinding()]
param(
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
$settle = @(1..4 | ForEach-Object { 'status' })
$phases = [ordered]@{
	flat     = 'comfort on'
	ahead    = 'headpose 0 0 0 0 1.7 0'
	turn     = 'headpose 30 -20 10 0 1.7 0'
	move     = 'headpose 0 0 0 0.3 1.5 0'
	lean     = 'headpose 0 0 0 1.0 1.7 0'
	recenter = 'recenter'
	off      = 'headpose off'
}
$commands = New-Object System.Collections.Generic.List[string]
$phaseAt = @{}  # 1-based index of a phase's command -> phase name
foreach ($name in $phases.Keys) {
	$commands.Add($phases[$name])
	$phaseAt[$commands.Count] = $name
	$commands.AddRange([string[]]$settle)
	if ($name -in 'ahead', 'turn') { $commands.Add("shot head-$name") }
}

# Which phase a cmdSeq (commands acknowledged so far) belongs to: only a 'status' at least two after
# the phase's command counts
$phaseOfSeq = @{}
$current = $null
$since = 0
for ($i = 1; $i -le $commands.Count; $i++) {
	if ($phaseAt.ContainsKey($i)) { $current = $phaseAt[$i]; $since = 0; continue }
	$since++
	if ($commands[$i - 1] -eq 'status' -and $since -ge 2) { $phaseOfSeq[$i] = $current }
}

$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ Commands = $commands.ToArray(); Seconds = 1; Save = 'mvp-new-game' }
if ($Rebuild) { $jobArgs.Rebuild = $true }

Write-Host "==> head check: $($commands.Count) channel commands, sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

$samples = New-Object System.Collections.Generic.List[object]
$lastFrame = -1
while ($job.State -eq 'Running') {
	$s = Read-ArxStatus $testDir
	if ($s -and $s.state -eq 'GAMEPLAY' -and $s.camera -and $s.vr.head -and [int]$s.frame -ne $lastFrame) {
		$lastFrame = [int]$s.frame
		$phase = $phaseOfSeq[[int]$s.cmdSeq]
		if ($phase) {
			$h = $s.vr.head
			$samples.Add([pscustomobject]@{
				phase = $phase; frame = [int]$s.frame
				bodyYaw = [double]$s.player.yaw
				x = [double]$s.camera.x; y = [double]$s.camera.y; z = [double]$s.camera.z
				pitch = [double]$s.camera.pitch; yaw = [double]$s.camera.yaw; roll = [double]$s.camera.roll
				source = [string]$h.source; references = [int]$h.references; clamped = [bool]$h.clamped
				ox = [double]$h.offsetX; oy = [double]$h.offsetY; oz = [double]$h.offsetZ
			})
		}
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

$reasons = New-Object System.Collections.Generic.List[string]
if ($smokeExit -ne 0) { $reasons.Add("smoke test exit code $smokeExit") }

function AngleDiff([double]$a, [double]$b) {
	$d = [math]::Abs($a - $b) % 360
	if ($d -gt 180) { 360 - $d } else { $d }
}
function Wrap([double]$a) { (($a % 360) + 360) % 360 }

$tolAngle = 0.05
$tolUnits = 0.5
$byPhase = @{}
Write-Host ''
Write-Host ("{0,-9} {1,4} {2,-5} {3,4} {4,8} {5,8} {6,8} {7,8} {8,8} {9,8} {10,8} {11,5}" -f 'phase', 'n', 'src', 'refs', 'bodyYaw', 'pitch', 'yaw', 'roll', 'offX', 'offY', 'offZ', 'clamp')
foreach ($name in $phases.Keys) {
	$rows = @($samples | Where-Object { $_.phase -eq $name })
	if ($rows.Count -eq 0) { $reasons.Add("no samples in phase $name"); continue }
	$last = $rows[-1]
	$byPhase[$name] = $last
	Write-Host ("{0,-9} {1,4} {2,-5} {3,4} {4,8:0.00} {5,8:0.00} {6,8:0.00} {7,8:0.00} {8,8:0.00} {9,8:0.00} {10,8:0.00} {11,5}" -f $name, $rows.Count, $last.source, $last.references, $last.bodyYaw, $last.pitch, $last.yaw, $last.roll, $last.ox, $last.oy, $last.oz, $last.clamped)
	# Every sample of a phase must agree with its last one (the camera is steady while the head is)
	foreach ($r in $rows) {
		if ((AngleDiff $r.yaw $last.yaw) -gt $tolAngle -or (AngleDiff $r.pitch $last.pitch) -gt $tolAngle -or
		    [math]::Abs($r.x - $last.x) -gt $tolUnits -or [math]::Abs($r.z - $last.z) -gt $tolUnits) {
			$reasons.Add("phase ${name}: the camera moved between samples (frame $($r.frame))"); break
		}
	}
}

function Expect([string]$phase, [string]$what, [double]$expected, [double]$actual, [double]$tolerance, [switch]$Angle) {
	$diff = if ($Angle) { AngleDiff $expected $actual } else { [math]::Abs($expected - $actual) }
	if ($diff -gt $tolerance) { $reasons.Add(("{0}: {1} {2:0.###} (expected {3:0.###})" -f $phase, $what, $actual, $expected)) }
}

if ($byPhase.Count -eq $phases.Count) {
	$flat = $byPhase['flat']; $ahead = $byPhase['ahead']; $turn = $byPhase['turn']; $move = $byPhase['move']
	$lean = $byPhase['lean']; $recenter = $byPhase['recenter']; $off = $byPhase['off']
	$body = $ahead.bodyYaw

	if ($flat.source -ne 'none') { $reasons.Add("flat: source $($flat.source) (expected none)") }
	Expect 'flat' 'yaw' $flat.bodyYaw $flat.yaw $tolAngle -Angle

	if ($ahead.source -ne 'sim') { $reasons.Add("ahead: source $($ahead.source) (expected sim)") }
	Expect 'ahead' 'yaw' $body $ahead.yaw $tolAngle -Angle
	Expect 'ahead' 'pitch' 0 $ahead.pitch $tolAngle -Angle
	Expect 'ahead' 'roll' 0 $ahead.roll $tolAngle -Angle
	Expect 'ahead' 'offset' 0 ([math]::Sqrt($ahead.ox * $ahead.ox + $ahead.oy * $ahead.oy + $ahead.oz * $ahead.oz)) 0.01

	Expect 'turn' 'yaw' (Wrap ($body + 30)) $turn.yaw $tolAngle -Angle
	Expect 'turn' 'pitch' 20 $turn.pitch $tolAngle -Angle
	Expect 'turn' 'roll' 350 $turn.roll $tolAngle -Angle
	Expect 'turn' 'camera x' $ahead.x $turn.x $tolUnits
	Expect 'turn' 'camera y' $ahead.y $turn.y $tolUnits
	Expect 'turn' 'camera z' $ahead.z $turn.z $tolUnits

	# 30 cm right and 20 cm down, turned by the body's yaw: x = 30 cos, z = 30 sin, y = +20 (y is down)
	$t = $body * [math]::PI / 180
	$ex = 30 * [math]::Cos($t); $ez = 30 * [math]::Sin($t)
	Expect 'move' 'offset x' $ex $move.ox 0.05
	Expect 'move' 'offset y' 20 $move.oy 0.05
	Expect 'move' 'offset z' $ez $move.oz 0.05
	Expect 'move' 'camera dx' $ex ($move.x - $ahead.x) $tolUnits
	Expect 'move' 'camera dy' 20 ($move.y - $ahead.y) $tolUnits
	Expect 'move' 'camera dz' $ez ($move.z - $ahead.z) $tolUnits
	Expect 'move' 'yaw' $body $move.yaw $tolAngle -Angle

	if (-not $lean.clamped) { $reasons.Add('lean: not clamped at 1 m') }
	Expect 'lean' 'horizontal offset' 60 ([math]::Sqrt($lean.ox * $lean.ox + $lean.oz * $lean.oz)) 0.05

	Expect 'recenter' 'references' ($lean.references + 1) $recenter.references 0
	Expect 'recenter' 'offset' 0 ([math]::Sqrt($recenter.ox * $recenter.ox + $recenter.oy * $recenter.oy + $recenter.oz * $recenter.oz)) 0.01
	if ($recenter.clamped) { $reasons.Add('recenter: still clamped') }

	if ($off.source -ne 'none') { $reasons.Add("off: source $($off.source) (expected none)") }
	Expect 'off' 'yaw' $off.bodyYaw $off.yaw $tolAngle -Angle
	Expect 'off' 'roll' 0 $off.roll $tolAngle -Angle
}

$log = Get-ArxLogPath
if (Test-Path $log) {
	Write-Host ''
	Write-Host "vr.head events in $log"
	Select-String -Path $log -Pattern 'GameEvent:.*vr\.head' | ForEach-Object { Write-Host "  $($_.Line)" }
}
$capture = Join-Path $testDir 'capture'
Write-Host "screenshots: $capture\head-ahead.bmp, $capture\head-turn.bmp"

Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host "==> PASS head check: $($samples.Count) samples; the simulated head turns (yaw +30, pitch 20, roll 350), moves (offset $([math]::Round($ex, 2)), 20, $([math]::Round($ez, 2))), is clamped at 0.6 m and recentred; without it the camera is the engine's"
	exit 0
}
Write-Host "==> FAIL head check: $($reasons -join '; ')"
exit 1
