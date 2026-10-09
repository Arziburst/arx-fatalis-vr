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

Write-Host 'recommendation'
function Rec($WeekPct, $WeekHours, $FivePct, $FiveHours, $Burn, $Running, $Queue = @()) {
	Get-UsageRecommendation -Week @{ Pct = $WeekPct; ResetsAt = $t0 + $WeekHours * 3600 } -FiveHour @{ Pct = $FivePct; ResetsAt = $t0 + $FiveHours * 3600 } `
		-WeekPerUsd 0.05 -FivePerUsd 0.5 -WorkerUsdPerHour 4 -BurnUsdPerHour $Burn -RunningWorkers $Running -QueueUsd $Queue -Now $t0 -MaxWorkers 3
}
$idle = Rec 10 100 5 4 1 0
Check 'idle week -> the worker cap' ($idle.Workers -eq 3 -and $idle.Binding -eq 'worker cap') "$($idle.Workers) $($idle.Binding)"
$pace = Rec 60 48 20 4 12.5 2
Check 'week on pace to 90% -> fewer workers (0), weekly binds' ($pace.Workers -eq 0 -and $pace.Binding -eq 'weekly') "$($pace.Workers) $($pace.Binding)"
Check 'the pace is projected above the cap' ($pace.Week.ProjectedAtReset -gt 80) $pace.Week.ProjectedAtReset
$burst = Rec 20 150 88 3 6 1
Check 'a burst that would cross 95% in the window -> 5-hour binds' ($burst.Workers -lt 3 -and $burst.Binding -eq '5-hour') "$($burst.Workers) $($burst.Binding)"
$full = Rec 20 150 96 3 0 0
Check 'over the 5-hour cap -> 0 workers' ($full.Workers -eq 0)
$tail = Rec 20 150 50 0.05 0 0
Check 'a window about to roll over does not look unlimited (MinHours)' ($tail.Five.Workers -lt 100)
$fit = Rec 10 100 5 4 1 0 @(30, 30, 30, 500)
Check 'queue: tasks that fit are counted in order' ($fit.Fit -ge 1 -and $fit.Fit -le 3) $fit.Fit
$none = Get-UsageRecommendation -Week @{ Pct = 10; ResetsAt = $t0 + 360000 } -FiveHour @{ Pct = 5; ResetsAt = $t0 + 7200 } -WeekPerUsd $null -FivePerUsd $null `
	-WorkerUsdPerHour 4 -BurnUsdPerHour 0 -RunningWorkers 0 -Now $t0 -MaxWorkers 3
Check 'no calibration -> one worker, binding unknown' ($none.Workers -eq 1 -and $none.Binding -eq 'unknown') "$($none.Workers) $($none.Binding)"

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

if ($script:failures) { Write-Host "`n$script:failures check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host "`nall checks passed" -ForegroundColor Green
