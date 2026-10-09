<#
.SYNOPSIS
	Self-test of the usage accounting (#217): calibration, burn, task costs and the worker recommendation on synthetic samples.

.DESCRIPTION
	No network, no `claude`, no real ~/.claude files: usage-lib.ps1 is fed made-up samples. Exit code 0 when everything
	passed, 1 otherwise. CI runs it through run-script-tests.ps1.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'usage-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}

$t0 = 1800000000
# A synthetic hour-by-hour history: two sessions, a minute-grained sample every 10 minutes.
# The week moves 1% per 5 USD (0.2 %/USD), the 5-hour window 1% per 0.5 USD (2 %/USD).
function New-Samples([double]$WkPerUsd, [double]$FhPerUsd, [int]$Count = 36, [double]$UsdPerStep = 1.0) {
	$list = @()
	for ($i = 0; $i -lt $Count; $i++) {
		$cost = $i * $UsdPerStep
		$list += [pscustomobject]@{
			t = $t0 + $i * 600
			fh = [math]::Floor(5 + $cost * $FhPerUsd); fhr = $t0 + 20000
			wk = [math]::Floor(30 + $cost * $WkPerUsd); wkr = $t0 + 400000
			s = [pscustomobject]@{ a = 100 + $cost * 0.6; b = 50 + $cost * 0.4 }
		}
	}
	$list
}

Write-Host 'calibration'
$s = New-Samples 0.2 2
$wk = Get-UsageCalibration $s 'wk'; $fh = Get-UsageCalibration $s 'fh'
Check 'week ratio ~0.2 %/USD' ([math]::Abs($wk.PctPerUsd - 0.2) -lt 0.03) $wk.PctPerUsd
Check '5-hour ratio ~2 %/USD' ([math]::Abs($fh.PctPerUsd - 2) -lt 0.3) $fh.PctPerUsd
Check 'enough data -> good confidence' ($fh.Confidence -eq 'good') $fh.Confidence
Check 'no samples -> no ratio, confidence none' ((Get-UsageCalibration @() 'wk').Confidence -eq 'none')
$roll = @(New-Samples 0.2 2 10) + @(New-Samples 0.2 2 10 | ForEach-Object { $_.t += 100000; $_.fhr += 100000; $_ })
$rc = Get-UsageCalibration $roll 'fh'
Check 'a window rollover is not counted as an interval' ($rc.PctSeen -lt 40) $rc.PctSeen
$new = @([pscustomobject]@{ t = $t0; fh = 10; fhr = 1; wk = 1; wkr = 1; s = [pscustomobject]@{ a = 5 } }, [pscustomobject]@{ t = $t0 + 600; fh = 12; fhr = 1; wk = 1; wkr = 1; s = [pscustomobject]@{ a = 5; b = 400 } })
Check 'a session seen for the first time adds nothing' ((Get-SampleCostDelta $new[0] $new[1]) -eq 0)

Write-Host 'burn'
$burn = Get-UsageBurn $s ($t0 + 35 * 600) 1
Check 'burn ~6 USD/h (1 USD per 10 min)' ([math]::Abs($burn - 6) -lt 0.5) $burn
Check 'burn is null with one sample' ($null -eq (Get-UsageBurn @($s[0]) ($t0 + 100) 1))

Write-Host 'task costs'
$recs = @(
	[pscustomobject]@{ session_id = 's1'; issue = 1; role = 'worker'; model = 'Sonnet 5.5'; effort = 'medium'; cost_usd = 4; duration_h = 1 }
	[pscustomobject]@{ session_id = 's2'; issue = 1; role = 'reviewer'; model = 'Opus 5.5'; effort = 'high'; cost_usd = 2; duration_h = 0.3 }
	[pscustomobject]@{ session_id = 's3'; issue = 2; role = 'worker'; model = 'Sonnet 5.5'; effort = 'medium'; cost_usd = 6; duration_h = 1.5 }
	[pscustomobject]@{ session_id = 's4'; issue = 3; role = 'worker'; model = 'Sonnet 5.5'; effort = 'medium'; cost_usd = 10; duration_h = 2 }
	[pscustomobject]@{ session_id = 's5'; issue = $null; role = 'coordinator'; model = 'Opus 5.5'; effort = 'high'; cost_usd = 160; duration_h = 14 }
)
$tc = @(Get-TaskCosts $recs)
Check 'three tasks; coordinator excluded' ($tc.Count -eq 3) $tc.Count
Check 'reviewer cost counts to its task, model of the worker' (($tc | Where-Object Issue -eq 1).Usd -eq 6 -and ($tc | Where-Object Issue -eq 1).Model -eq 'sonnet')
$est = Get-TaskCostEstimate $tc 'sonnet' 'medium'
Check 'median of 6, 6, 10 is 6; p80 10' ($est.Median -eq 6 -and $est.P80 -eq 10) "$($est.Median) $($est.P80)"
Check 'unknown model falls back to all tasks' ((Get-TaskCostEstimate $tc 'fable' 'xhigh').Basis -eq 'all models')

Write-Host 'recommendation (no pacing, ETA to the caps)'
function Rec($WeekPct, $WeekHours, $FivePct, $FiveHours, $Burn, $Running, $Ready = 10, $Queue = @(), $Cap = 0) {
	Get-UsageRecommendation -Week @{ Pct = $WeekPct; ResetsAt = $t0 + $WeekHours * 3600 } -FiveHour @{ Pct = $FivePct; ResetsAt = $t0 + $FiveHours * 3600 } `
		-WeekPerUsd 0.05 -FivePerUsd 0.5 -WorkerUsdPerHour 4 -BurnUsdPerHour $Burn -RunningWorkers $Running -ReadyCount $Ready -QueueUsd $Queue -Now $t0 -MaxWorkers $Cap
}
$idle = Rec 10 100 5 4 1 0
Check 'no worker cap: 10 READY tasks -> 10 workers' ($idle.Workers -eq 10 -and $idle.Binding -eq 'ready tasks') "$($idle.Workers) $($idle.Binding)"
$few = Rec 10 100 5 4 4 1 1
Check 'one running + one READY -> 2 workers, bound by ready tasks' ($few.Workers -eq 2 -and $few.Binding -eq 'ready tasks') "$($few.Workers) $($few.Binding)"
$capped3 = Rec 10 100 5 4 1 0 10 @() 3
Check 'an explicit -MaxWorkers 3 still caps' ($capped3.Workers -eq 3 -and $capped3.Binding -eq 'worker cap')
$young = Rec 20 120 5 4 4 1 5
Check 'a week at 20% with 5 days left still advises every READY task (no pacing)' ($young.Workers -eq 6) $young.Workers
$hot = Rec 60 48 20 4 12.5 2
Check 'a week on pace to exceed 90% at reset is NOT throttled' ($hot.Workers -eq 12) $hot.Workers
Check 'ETA to the weekly cap = room / burn rate (30% / (12.5 USD/h * 0.05) = 48 h)' ($hot.Week.EtaHours -eq 48) $hot.Week.EtaHours
$eta = Rec 10 100 5 4 4 1
Check 'ETA to the 5h cap = (95-5)% / (4 USD/h * 0.5 %/USD) = 45 h' ($eta.Five.EtaHours -eq 45) $eta.Five.EtaHours
Check 'ETA to the weekly cap = (90-10)% / (4 * 0.05) = 400 h' ($eta.Week.EtaHours -eq 400) $eta.Week.EtaHours
Check 'ETA with N workers is shorter than at the current burn' ($eta.EtaHoursWithN -lt $eta.EtaHours) "$($eta.EtaHoursWithN) vs $($eta.EtaHours)"
$full = Rec 20 150 96 3 0 0
Check 'over the 5-hour cap -> 0 workers' ($full.Workers -eq 0 -and $full.Binding -eq 'cap reached')
$wfull = Rec 91 150 10 3 0 0
Check 'over the weekly cap -> 0 workers' ($wfull.Workers -eq 0)
$fit = Rec 10 100 5 4 1 0 4 @(100, 100, 100, 5000)
Check 'queue: tasks that fit before the cap are counted in order (budget 80% / 0.05 = 1600 USD week; 5h 90 / 0.5 = 180 USD)' ($fit.Five.Fit -eq 1 -and $fit.Week.Fit -eq 3 -and $fit.Fit -eq 1) "$($fit.Five.Fit) $($fit.Week.Fit) $($fit.Fit)"
$unk = Get-UsageRecommendation -Week @{ Pct = 10; ResetsAt = $t0 + 360000 } -FiveHour @{ Pct = 5; ResetsAt = $t0 + 7200 } -WeekPerUsd 0.05 -FivePerUsd 0.5 `
	-WorkerUsdPerHour 4 -BurnUsdPerHour 0 -RunningWorkers 0 -Now $t0
Check 'READY count unknown -> only the running ones (0 here)' ($unk.Workers -eq 0)
$none = Get-UsageRecommendation -Week @{ Pct = 10; ResetsAt = $t0 + 360000 } -FiveHour @{ Pct = 5; ResetsAt = $t0 + 7200 } -WeekPerUsd $null -FivePerUsd $null `
	-WorkerUsdPerHour 4 -BurnUsdPerHour 0 -RunningWorkers 0 -ReadyCount 5 -Now $t0
Check 'no calibration -> still the READY tasks, no ETA' ($none.Workers -eq 5 -and $null -eq $none.EtaHours) "$($none.Workers) $($none.EtaHours)"
Write-Host 'finished sessions'
$ledger = @(
	[pscustomobject]@{ session_id = 'w1'; name = 'ARXVR-5 Something'; branch = 'ARXVR-5-feat-x'; model = 'Sonnet 5.5'; effort = 'high'; context_pct = 30; context_peak = 55; cost_usd = 5; duration_ms = 3600000; updated = (Get-Date '2026-10-09T10:00:00').ToString('s') }
	[pscustomobject]@{ session_id = 'r1'; name = 'ARXVR-5 review (PR #9)'; branch = ''; model = 'Opus 5.5'; effort = 'high'; context_pct = 10; cost_usd = 2; duration_ms = 600000; updated = (Get-Date '2026-10-09T10:05:00').ToString('s') }
	[pscustomobject]@{ session_id = 'c1'; name = 'ARXVR Coordinator'; branch = ''; model = 'Opus 5.5'; effort = 'high'; context_pct = 12; cost_usd = 160; duration_ms = 50000000; updated = (Get-Date '2026-10-09T10:05:00').ToString('s') }
	[pscustomobject]@{ session_id = 'live'; name = 'ARXVR-6 Live'; branch = 'ARXVR-6-feat-y'; model = 'Opus 5.5'; effort = 'high'; context_pct = 5; cost_usd = 1; duration_ms = 60000; updated = (Get-Date).ToString('s') }
	[pscustomobject]@{ session_id = 'misc'; name = 'scratch'; branch = ''; model = 'Opus 5.5'; effort = 'high'; context_pct = 5; cost_usd = 1; duration_ms = 60000; updated = (Get-Date '2026-10-09T10:00:00').ToString('s') }
)
$nowEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$out = @(Get-FinishedTaskRecords -Ledger $ledger -Recorded @([pscustomobject]@{ session_id = 'c1'; cost_usd = 160 }) -Now $nowEpoch -WeekPerUsd 0.2 -FivePerUsd 2 -Launches @{ 5 = 3 })
Check 'worker and reviewer recorded; coordinator already recorded, live and unrelated sessions skipped' ($out.Count -eq 2) $out.Count
$w = $out | Where-Object session_id -eq 'w1'
Check 'worker record: issue, peak context, launches, % of week and 5h' ($w.issue -eq 5 -and $w.ctx_peak_pct -eq 55 -and $w.launches -eq 3 -and $w.week_pct -eq 1 -and $w.five_hour_pct -eq 10) ($w | ConvertTo-Json -Compress)
Check 'reviewer counts to the PR task' (($out | Where-Object session_id -eq 'r1').issue -eq 5 -and ($out | Where-Object session_id -eq 'r1').role -eq 'reviewer')

Write-Host 'task size and the size-aware forecast'
$small = "Fix one thing.`n- a`n- b"
$big = "Goal`n" + (((1..14) | ForEach-Object { "- item $_ depends on #$($_ + 100)" }) -join "`n")
Check 'size label wins over the text' ((Get-TaskSize $big @('size:S')).Size -eq 'S')
Check 'short issue is S' ((Get-TaskSize $small).Size -eq 'S')
Check 'many bullets and references -> L' ((Get-TaskSize $big).Size -eq 'L') (Get-TaskSize $big).Score
Check 'no text, no label -> unknown size' ($null -eq (Get-TaskSize '').Size)
function Done($n, $model, $effort, $size, $usd) { [pscustomobject]@{ Issue = $n; Model = $model; Effort = $effort; Size = $size; Usd = $usd; Hours = 1 } }
$done = @(
	(Done 1 'sonnet' 'high' 'S' 2), (Done 2 'sonnet' 'high' 'S' 3), (Done 3 'sonnet' 'high' 'S' 4),
	(Done 4 'sonnet' 'high' 'L' 20), (Done 5 'sonnet' 'high' 'L' 24), (Done 6 'sonnet' 'high' 'L' 28),
	(Done 7 'opus' 'high' 'M' 10)
)
$eS = Get-TaskCostEstimate $done 'sonnet' 'high' 'S'; $eL = Get-TaskCostEstimate $done 'sonnet' 'high' 'L'
Check 'size S forecast is the S median' ($eS.Median -eq 3 -and $eS.Basis -eq 'sonnet high size S') "$($eS.Median) $($eS.Basis)"
Check 'size L forecast is the L median' ($eL.Median -eq 24 -and $eL.Basis -eq 'sonnet high size L') "$($eL.Median) $($eL.Basis)"
$eM = Get-TaskCostEstimate $done 'sonnet' 'high' 'M'
Check 'size with too few finished tasks falls back to the model median, and says so' ($eM.Basis -like 'sonnet high*' -and $eM.N -eq 6) "$($eM.Basis) $($eM.N)"
$eU = Get-TaskCostEstimate $done 'sonnet' 'high' ''
Check 'unknown size uses the model / effort pool' ($eU.N -eq 6 -and $eU.Basis -eq 'sonnet high') $eU.Basis
Check 'unseen model falls back to all tasks' ((Get-TaskCostEstimate $done 'fable' 'xhigh' 'S').Basis -eq 'all models')
$sz = @(Get-TaskCosts @([pscustomobject]@{ session_id = 'x'; issue = 9; role = 'worker'; model = 'Sonnet 5.5'; effort = 'high'; cost_usd = 3; duration_h = 1 }) @{ 9 = 'S' })
Check 'Get-TaskCosts carries the size of the issue' ($sz[0].Size -eq 'S')
if ($script:failures) { Write-Host "`n$script:failures check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host "`nall checks passed" -ForegroundColor Green
