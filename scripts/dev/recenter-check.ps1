<#
.SYNOPSIS
	Recenter check (#28): the player's recenter - the calibration - on the simulated headset, with a
	PASS / FAIL verdict on the head numbers in status.json.

.DESCRIPTION
	One launch: smoke-test.ps1 -VrSim recenter (scripts\dev\vrsim\recenter.vrsim; no runtime, no SteamVR,
	every launch rule of D-014; level 10). While it runs this script samples status.json every 100 ms
	and sorts the samples by the simulator's timeline frame into three windows (D-018: recenter is the
	calibration - the head there is the character's eye, looking that way is the character's facing):

	  seated      head 25 cm right, 40 cm lower, 10 cm back,    calibrated standing at the start: offset
	              turned 40 left                                 48.2 units, 40 of them down; turned +40
	  (the left stick click held for 1 s: a long press = recenter)
	  recentered  the same head                                  reason "button", one more reference,
	                                                             offset 0, turned 0, eye height 0.40 lower
	  back        head back at the start                         the same reference; 26.9 units aside,
	                                                             turned -40; 40 cm above the seated eye:
	                                                             auto-raise (#27) lifts it - eye height 0, offset y 0

	"Turned" is the head's yaw against the reference's (the camera's yaw against the body's in first
	person; level 10 opens on a conversation camera, so the head numbers are compared, not the camera's).
	The engine's y grows downwards, so a lower head is a positive offsetY.
	-NoPress makes the press a 10-frame tap: no recenter may happen, so the check must fail (it can fail).

	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).

.PARAMETER NoPress
	Release the stick click 10 frames after the press (a tap): no recenter, the check must fail.

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\recenter-check.ps1
#>
[CmdletBinding()]
param(
	[switch]$NoPress,
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
$windows = [ordered]@{
	seated     = @(330, 370)
	recentered = @(490, 530)
	back       = @(570, 610)
}

$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ VrSim = 'recenter'; Seconds = 9 }
# A tap instead of the long press: an earlier release (the script's own release at 470 then changes nothing)
if ($NoPress) { $jobArgs.Commands = @('vr-sim @390 action thumbstick_click left 0') }
if ($Rebuild) { $jobArgs.Rebuild = $true }

Write-Host "==> recenter check$(if ($NoPress) { ' (-NoPress: a tap, no recenter - must FAIL)' }): sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

$samples = New-Object System.Collections.Generic.List[object]
$lastFrame = -1
while ($job.State -eq 'Running') {
	$s = Read-ArxStatus $testDir
	if ($s -and $s.state -eq 'GAMEPLAY' -and $s.vr.head -and $s.vr.sim -and [int]$s.frame -ne $lastFrame) {
		$lastFrame = [int]$s.frame
		$t = [int]$s.vr.sim.timelineFrame
		foreach ($name in $windows.Keys) {
			if ($t -ge $windows[$name][0] -and $t -le $windows[$name][1]) {
				$h = $s.vr.head
				$samples.Add([pscustomobject]@{
					window = $name; t = $t
					turned = [double]$h.yaw - [double]$h.refYaw; refY = [double]$h.refY
					source = [string]$h.source; references = [int]$h.references; reason = [string]$h.reason
					ox = [double]$h.offsetX; oy = [double]$h.offsetY; oz = [double]$h.offsetZ
				})
			}
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
	Write-Host 'recenter check: SKIPPED - the smoke test was skipped (game lock busy); run it again later' -ForegroundColor Yellow
	exit 2
}

$reasons = New-Object System.Collections.Generic.List[string]
if ($smokeExit -ne 0) { $reasons.Add("smoke test exit code $smokeExit") }

function AngleDiff([double]$a, [double]$b) {
	$d = [math]::Abs($a - $b) % 360
	if ($d -gt 180) { 360 - $d } else { $d }
}
function Expect([string]$window, [string]$what, [double]$expected, [double]$actual, [double]$tolerance, [switch]$Angle) {
	$diff = if ($Angle) { AngleDiff $expected $actual } else { [math]::Abs($expected - $actual) }
	if ($diff -gt $tolerance) { $reasons.Add(("{0}: {1} {2:0.###} (expected {3:0.###})" -f $window, $what, $actual, $expected)) }
}
function Length($r) { [math]::Sqrt($r.ox * $r.ox + $r.oy * $r.oy + $r.oz * $r.oz) }
function Aside($r) { [math]::Sqrt($r.ox * $r.ox + $r.oz * $r.oz) }

$tolAngle = 0.1
$tolUnits = 0.1
$tolMeters = 0.001
$last = @{}
Write-Host ''
Write-Host ("{0,-11} {1,3} {2,-4} {3,4} {4,-11} {5,7} {6,7} {7,8} {8,8} {9,8}" -f 'window', 'n', 'src', 'refs', 'reason', 'refY', 'turned', 'offX', 'offY', 'offZ')
foreach ($name in $windows.Keys) {
	$rows = @($samples | Where-Object { $_.window -eq $name })
	if ($rows.Count -eq 0) { $reasons.Add("no samples in the window $name (timeline frames $($windows[$name] -join '-'))"); continue }
	$r = $rows[-1]
	$last[$name] = $r
	Write-Host ("{0,-11} {1,3} {2,-4} {3,4} {4,-11} {5,7:0.000} {6,7:0.00} {7,8:0.00} {8,8:0.00} {9,8:0.00}" -f $name, $rows.Count, $r.source, $r.references, $r.reason, $r.refY, $r.turned, $r.ox, $r.oy, $r.oz)
	if ($r.source -ne 'hmd') { $reasons.Add("${name}: source $($r.source) (expected hmd: the simulated headset)") }
}

if ($last.Count -eq $windows.Count) {
	$seated = $last['seated']; $recentered = $last['recentered']; $back = $last['back']

	# Calibrated standing at the start; sat down: 25 cm right, 40 cm lower, 10 cm back = 48.2 units
	Expect 'seated' 'offset' ([math]::Sqrt(25 * 25 + 40 * 40 + 10 * 10)) (Length $seated) $tolUnits
	Expect 'seated' 'offset y' 40 $seated.oy $tolUnits
	Expect 'seated' 'turned' 40 $seated.turned $tolAngle -Angle
	Expect 'seated' 'eye height' 0 $seated.refY $tolMeters

	# The long press: calibrated seated - the seated, turned head is the eye and the facing now
	if ($recentered.reason -ne 'button') { $reasons.Add("recentered: reason '$($recentered.reason)' (expected button)") }
	Expect 'recentered' 'references' ($seated.references + 1) $recentered.references 0
	Expect 'recentered' 'offset' 0 (Length $recentered) $tolUnits
	Expect 'recentered' 'turned' 0 $recentered.turned $tolAngle -Angle
	Expect 'recentered' 'eye height' -0.40 $recentered.refY $tolMeters

	# Back at the start: measured from the seated calibration, the height raised with the head (#27)
	Expect 'back' 'references' $recentered.references $back.references 0
	Expect 'back' 'offset aside' ([math]::Sqrt(25 * 25 + 10 * 10)) (Aside $back) $tolUnits
	Expect 'back' 'turned' -40 $back.turned $tolAngle -Angle
	# 40 cm above the seated calibration: auto-raise (#27) took the eye height up with the head
	Expect 'back' 'eye height (auto-raise)' 0 $back.refY $tolMeters
	Expect 'back' 'offset down' 0 $back.oy $tolUnits
}

$log = Get-ArxLogPath
if (Test-Path $log) {
	Write-Host ''
	Write-Host "head reference events and recenter presses in $log"
	Select-String -Path $log -Pattern 'GameEvent:.*vr\.head\.reference|\[vr\] input: recenter' | ForEach-Object { Write-Host "  $($_.Line)" }
}

Write-Host ''
if ($reasons.Count -gt 0) {
	foreach ($reason in $reasons) { Write-Host "  $reason" -ForegroundColor Red }
	Write-Host 'recenter check: FAIL' -ForegroundColor Red
	exit 1
}
Write-Host 'recenter check: PASS' -ForegroundColor Green
exit 0
