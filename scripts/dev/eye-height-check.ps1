<#
.SYNOPSIS
	Eye height check (#27, D-018): the owner's #24 run on the simulated headset - loaded with the headset
	on the desk, put on, crouch, auto-raise, off and on again seated - with a PASS / FAIL verdict on the
	numbers in status.json and the vr.head events in arx.log.

.DESCRIPTION
	Runs smoke-test.ps1 -VrSim eye-height (scripts\dev\vrsim\eye-height.vrsim; one launch, every launch
	rule of D-014, no SteamVR) on the test save mvp-new-game in a background job and samples status.json
	every 100 ms. Each sample belongs to a phase by the simulator's timeline frame:

	  desk      frames 100-199  the headset idle on the desk (session synchronized): not worn, only a
	                            provisional reference, a settled one still wanted
	  standing  frames 320-359  put on at 200 and lifted to the standing head: settled reference at the
	                            standing head (refY 0, 1.7 m above the simulated floor), camera offset 0,
	                            the view at the character's eye, the yaw back on the body
	  crouch    frames 400-434  head 20 cm lower: offset +20 units (y down), the view 20 units lower
	  raised    frames 475-509  head 10 cm above the reference: auto-raise lifts it (refY 0.1), offset 0
	  back      frames 550-584  head back at 0: 10 cm below the raised reference, offset +10
	  seated    frames 700-735  taken off at 586, put on seated (50 cm lower) at 646: a new settled
	                            reference "put on again" at refY -0.5 (1.2 m above the floor), offset 0

	The log must show the references in this order - provisional ones while on the desk, a settled one
	after it was put on, a settled "put on again" one after the second putting on - and one
	vr.head.raised event of 0.1 m.

	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\eye-height-check.ps1
#>
[CmdletBinding()]
param(
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
$phases = [ordered]@{
	desk     = @(100, 199)
	standing = @(320, 359)
	crouch   = @(400, 434)
	raised   = @(475, 509)
	back     = @(550, 584)
	seated   = @(700, 735)
}

$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ VrSim = 'eye-height'; Seconds = 1; Save = 'mvp-new-game' }
if ($Rebuild) { $jobArgs.Rebuild = $true }

Write-Host "==> eye height check: smoke-test.ps1 -VrSim eye-height (1 launch), sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

$samples = New-Object System.Collections.Generic.List[object]
$lastFrame = -1
while ($job.State -eq 'Running') {
	$s = Read-ArxStatus $testDir
	if ($s -and $s.state -eq 'GAMEPLAY' -and $s.camera -and $s.vr.head -and $s.vr.sim -and [int]$s.frame -ne $lastFrame) {
		$lastFrame = [int]$s.frame
		$tf = [int64]$s.vr.sim.timelineFrame
		$phase = $null
		foreach ($name in $phases.Keys) {
			if ($tf -ge $phases[$name][0] -and $tf -le $phases[$name][1]) { $phase = $name }
		}
		if ($phase) {
			$h = $s.vr.head
			$samples.Add([pscustomobject]@{
				phase = $phase; tf = $tf; session = [string]$s.vr.sessionState
				bodyYaw = [double]$s.player.yaw; yaw = [double]$s.camera.yaw
				viewAboveFeet = [double]$s.camera.eyeAboveFeet; eyeAboveFeet = [double]$h.eyeAboveFeet
				source = [string]$h.source; kind = [string]$h.referenceKind; pending = [bool]$h.pending; worn = [bool]$h.worn
				references = [int]$h.references; refY = [double]$h.refY; raised = [double]$h.raised
				refAboveFloor = [double]$h.refAboveFloor; aboveFloor = [double]$h.aboveFloor
				oy = [double]$h.offsetY; ox = [double]$h.offsetX; oz = [double]$h.offsetZ
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
function Expect([string]$phase, [string]$what, [double]$expected, [double]$actual, [double]$tolerance, [switch]$Angle) {
	$diff = if ($Angle) { AngleDiff $expected $actual } else { [math]::Abs($expected - $actual) }
	if ($diff -gt $tolerance) { $reasons.Add(("{0}: {1} {2:0.###} (expected {3:0.###})" -f $phase, $what, $actual, $expected)) }
}

$byPhase = @{}
Write-Host ''
Write-Host ("{0,-9} {1,3} {2,-12} {3,-12} {4,5} {5,5} {6,4} {7,7} {8,7} {9,7} {10,7} {11,8} {12,8} {13,8}" -f 'phase', 'n', 'session', 'kind', 'worn', 'pend', 'refs', 'refY', 'raised', 'refFlrM', 'flrM', 'offY', 'eyeFeet', 'viewFeet')
foreach ($name in $phases.Keys) {
	$rows = @($samples | Where-Object { $_.phase -eq $name })
	if ($rows.Count -eq 0) { $reasons.Add("no samples in phase $name"); continue }
	$last = $rows[-1]
	$byPhase[$name] = $last
	Write-Host ("{0,-9} {1,3} {2,-12} {3,-12} {4,5} {5,5} {6,4} {7,7:0.000} {8,7:0.000} {9,7:0.000} {10,7:0.000} {11,8:0.00} {12,8:0.00} {13,8:0.00}" -f $name, $rows.Count, $last.session, $last.kind, $last.worn, $last.pending, $last.references, $last.refY, $last.raised, $last.refAboveFloor, $last.aboveFloor, $last.oy, $last.eyeAboveFeet, $last.viewAboveFeet)
	# The head holds still in every phase: so does the camera offset
	foreach ($r in $rows) {
		if ([math]::Abs($r.oy - $last.oy) -gt 0.05 -or $r.kind -ne $last.kind) {
			$reasons.Add("phase ${name}: the reference or the offset changed within the phase (timeline frame $($r.tf))"); break
		}
	}
}

if ($byPhase.Count -eq $phases.Count) {
	$desk = $byPhase['desk']; $standing = $byPhase['standing']; $crouch = $byPhase['crouch']
	$raised = $byPhase['raised']; $back = $byPhase['back']; $seated = $byPhase['seated']

	if ($desk.session -ne 'synchronized') { $reasons.Add("desk: session $($desk.session) (expected synchronized)") }
	if ($desk.worn) { $reasons.Add('desk: worn') }
	if ($desk.kind -ne 'provisional') { $reasons.Add("desk: reference $($desk.kind) (expected provisional)") }
	if (-not $desk.pending) { $reasons.Add('desk: no settled reference wanted') }

	foreach ($p in 'standing', 'crouch', 'raised', 'back', 'seated') {
		$r = $byPhase[$p]
		if ($r.source -ne 'hmd') { $reasons.Add("${p}: source $($r.source) (expected hmd)") }
		if ($r.kind -ne 'settled' -or $r.pending -or -not $r.worn) { $reasons.Add("${p}: reference $($r.kind), pending $($r.pending), worn $($r.worn) (expected settled, false, true)") }
		if ($r.eyeAboveFeet -lt 120 -or $r.eyeAboveFeet -gt 190) { $reasons.Add("${p}: the character's eye $($r.eyeAboveFeet) units above its feet (expected 120-190)") }
		# The view above the feet = the character's eye minus the camera offset (y down)
		Expect $p 'view above the feet' ($r.eyeAboveFeet - $r.oy) $r.viewAboveFeet 0.5
	}

	Expect 'standing' 'refY' 0 $standing.refY 0.001
	Expect 'standing' 'offset y' 0 $standing.oy 0.05
	Expect 'standing' 'horizontal offset' 0 ([math]::Sqrt($standing.ox * $standing.ox + $standing.oz * $standing.oz)) 0.05
	Expect 'standing' 'head above the floor at the reference (m)' 1.7 $standing.refAboveFloor 0.005
	Expect 'standing' 'yaw = body yaw' $standing.bodyYaw $standing.yaw 0.05 -Angle

	Expect 'crouch' 'offset y' 20 $crouch.oy 0.05
	Expect 'crouch' 'view lower by' 20 ($standing.viewAboveFeet - $crouch.viewAboveFeet) 0.5
	Expect 'crouch' 'refY' 0 $crouch.refY 0.001

	Expect 'raised' 'refY' 0.1 $raised.refY 0.001
	Expect 'raised' 'raised (m)' 0.1 $raised.raised 0.001
	Expect 'raised' 'offset y' 0 $raised.oy 0.05
	Expect 'raised' 'view at the eye (above the feet)' $standing.viewAboveFeet $raised.viewAboveFeet 0.5

	Expect 'back' 'offset y' 10 $back.oy 0.05
	Expect 'back' 'refY' 0.1 $back.refY 0.001

	Expect 'seated' 'refY' -0.5 $seated.refY 0.001
	Expect 'seated' 'offset y' 0 $seated.oy 0.05
	Expect 'seated' 'head above the floor at the reference (m)' 1.2 $seated.refAboveFloor 0.005
	Expect 'seated' 'view at the eye (above the feet)' $standing.viewAboveFeet $seated.viewAboveFeet 0.5
	if ($seated.references -le $standing.references) { $reasons.Add("seated: no new reference ($($seated.references) after $($standing.references))") }
}

# The events: provisional while on the desk, settled once put on, settled "put on again", one raise of 0.1 m
$log = Get-ArxLogPath
$refLines = @()
$raisedLines = @()
if (Test-Path $log) {
	Write-Host ''
	Write-Host "vr.head events in $log"
	$lines = @(Select-String -Path $log -Pattern 'GameEvent:.*vr\.head' | ForEach-Object { $_.Line })
	$lines | ForEach-Object { Write-Host "  $_" }
	$refLines = @($lines | Where-Object { $_ -match 'vr\.head\.reference ' })
	$raisedLines = @($lines | Where-Object { $_ -match 'vr\.head\.raised ' })
} else {
	$reasons.Add("no arx.log at $log")
}
$kinds = @($refLines | ForEach-Object { if ($_ -match 'kind=(\w+)') { $Matches[1] } })
$settled = @($refLines | Where-Object { $_ -match 'kind=settled' })
if ($settled.Count -ne 2) { $reasons.Add("$($settled.Count) settled references (expected 2: put on, put on again): $($kinds -join ', ')") }
elseif ($settled[1] -notmatch 'reason="?put on again') { $reasons.Add("the second settled reference is not 'put on again': $($settled[1])") }
$firstSettled = [array]::IndexOf($kinds, 'settled')
if ($firstSettled -lt 1) { $reasons.Add("no provisional reference before the first settled one: $($kinds -join ', ')") }
if ($settled.Count -ge 1 -and $settled[0] -notmatch 'characterEyeM=1\.\d') { $reasons.Add("the settled reference does not name the character's eye height: $($settled[0])") }
if ($raisedLines.Count -ne 1 -or $raisedLines[0] -notmatch 'byM=0\.1(00)?\b') { $reasons.Add("vr.head.raised: expected one event of 0.1 m, got $($raisedLines.Count): $($raisedLines -join ' | ')") }

$capture = Join-Path $testDir 'capture'
Write-Host "screenshots: $capture (standing, seated)"

Write-Host ''
if ($reasons.Count -eq 0) {
	$eye = $byPhase['standing'].eyeAboveFeet
	Write-Host ("==> PASS eye height check: {0} samples; the desk gave only a provisional reference; put on, the standing head (1.70 m above the floor) is the character's eye ({1:0.0} units = {2:0.00} m above its feet); crouch 20 cm = 20 units lower; 10 cm above raised the reference; put on again seated (1.20 m) = the eye again" -f $samples.Count, $eye, ($eye / 100))
	exit 0
}
Write-Host "==> FAIL eye height check: $($reasons -join '; ')"
exit 1
