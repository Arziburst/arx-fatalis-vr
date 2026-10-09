<#
.SYNOPSIS
	The coordinator's usage plan (#217, D-022): the weekly and 5-hour limits, what tasks cost, how many workers to run.

.DESCRIPTION
	Reads the samples the status line writes (~/.claude/statusline/usage.jsonl) and the session ledger, records the cost
	of finished sessions per task (tasks.jsonl), and prints a short plan:
	  NOW       week % and 5-hour % with their resets, the burn rate, the calibration (% of a limit per dollar, with confidence)
	  BUDGET    % left to the caps (week 80, 5-hour 95), the projected % at reset
	  TASKS     median / p80 cost of a finished task per model, the forecast for the READY tasks of plan-next.ps1
	  ADVICE    workers to run now, the binding limit, how many queued tasks fit
	The coordinator runs it every cycle and before starting a worker; its one-line summary goes to the status page (#193).
	With no samples yet the weekly ratio comes from a rough prior (cost of this week's sessions / week %).

.PARAMETER Record
	Only record finished sessions into tasks.jsonl (no plan).
.PARAMETER Json
	Print the whole answer as JSON.
.PARAMETER Dir
	The statusline folder (default ~/.claude/statusline) - for what-if runs and the self-test.
.PARAMETER NoQueue
	Do not call plan-next.ps1 (no forecast for queued tasks).
.PARAMETER MaxWorkers
	Worker cap (default 3).
#>
[CmdletBinding()]
param([switch]$Record, [switch]$Json, [string]$Dir, [switch]$NoQueue, [int]$MaxWorkers = 3)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'usage-lib.ps1')
if (-not $Dir) { $Dir = Get-UsageDir }
$now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()

$last = $null
try { $last = Get-Content (Join-Path $Dir 'last.json') -Raw | ConvertFrom-Json } catch {}
$samples = @(Read-JsonLines (Join-Path $Dir 'usage.jsonl'))
$ledger = @(Get-ChildItem (Join-Path $Dir 'sessions') -Filter '*.json' -ErrorAction SilentlyContinue | ForEach-Object { try { Get-Content $_.FullName -Raw | ConvertFrom-Json } catch {} })

$weekCal = Get-UsageCalibration $samples 'wk'
$fiveCal = Get-UsageCalibration $samples 'fh'
$weekPerUsd = $weekCal.PctPerUsd; $fivePerUsd = $fiveCal.PctPerUsd
$weekNote = $weekCal.Confidence
if ($null -eq $weekPerUsd -and $last.rate_limits.seven_day) {
	# prior: the sessions of this week (ledger) against the week's percent - includes the owner's other use, so it is low confidence
	$weekStart = $last.rate_limits.seven_day.resets_at - 7 * 86400
	$usd = ($ledger | Where-Object { (ConvertTo-Epoch $_.updated) -ge $weekStart } | Measure-Object cost_usd -Sum).Sum
	$pct = [double]$last.rate_limits.seven_day.used_percentage
	if ($usd -gt 5 -and $pct -gt 0) { $weekPerUsd = $pct / $usd; $weekNote = 'prior (ledger cost / week %), no samples yet' }
}

# --- Finished sessions -> tasks.jsonl ---------------------------------------------------------------------
$tasksFile = Join-Path $Dir 'tasks.jsonl'
$launches = @{}
$launchLog = Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\launches.log'
if (Test-Path $launchLog) {
	foreach ($m in [regex]::Matches((Get-Content $launchLog -Raw), 'branch=ARXVR-(\d+)')) { $n = [int]$m.Groups[1].Value; $launches[$n] = 1 + [int]$launches[$n] }
}
$recorded = @(Read-JsonLines $tasksFile)
$new = @(Get-FinishedTaskRecords -Ledger $ledger -Recorded $recorded -Now $now -WeekPerUsd $weekPerUsd -FivePerUsd $fivePerUsd -Launches $launches)
if ($new.Count) {
	foreach ($r in $new) { Add-Content -Path $tasksFile -Value ($r | ConvertTo-Json -Compress) -Encoding utf8 }
	$recorded = @(Read-JsonLines $tasksFile)
}
if ($Record) { "recorded $($new.Count) finished session(s) into $tasksFile"; return }

# --- Now ---------------------------------------------------------------------------------------------------
$week = if ($last.rate_limits.seven_day) { @{ Pct = [double]$last.rate_limits.seven_day.used_percentage; ResetsAt = [double]$last.rate_limits.seven_day.resets_at } }
$five = if ($last.rate_limits.five_hour) { @{ Pct = [double]$last.rate_limits.five_hour.used_percentage; ResetsAt = [double]$last.rate_limits.five_hour.resets_at } }
$burn = Get-UsageBurn $samples $now 1
$burnSince = $null
if ($five) { $burnSince = Get-UsageBurn $samples $now ([math]::Max(0.1, 5 - ($five.ResetsAt - $now) / 3600)) }

$liveWorkers = @($ledger | Where-Object { $_.name -notmatch 'Coordinator|review' -and $_.branch -match '^ARXVR-' -and $now - (ConvertTo-Epoch $_.updated) -lt 600 })
$taskCosts = @(Get-TaskCosts $recorded)
$workerRates = @($taskCosts | Where-Object { $_.Hours -gt 0.2 } | ForEach-Object { $_.Usd / $_.Hours })
$workerUsdPerHour = Get-Median $workerRates; $rateBasis = "median of $($workerRates.Count) finished task(s)"
if ($null -eq $workerUsdPerHour) {
	$live = @($liveWorkers | Where-Object { $_.duration_ms -gt 600000 } | ForEach-Object { [double]$_.cost_usd / ([double]$_.duration_ms / 3600000) })
	$workerUsdPerHour = Get-Median $live; $rateBasis = "median of $($live.Count) live worker(s)"
}
if ($null -eq $workerUsdPerHour) { $workerUsdPerHour = 3.0; $rateBasis = 'default 3 USD/h, no data yet' }

# --- Queue forecast ----------------------------------------------------------------------------------------
$queue = @()
if (-not $NoQueue) {
	try {
		$plan = (& (Join-Path $PSScriptRoot 'plan-next.ps1') -Json) -join "`n" | ConvertFrom-Json
		foreach ($t in $plan.Ready) {
			$e = Get-TaskCostEstimate $taskCosts $t.Model $t.Effort
			if (-not $e) { $e = [pscustomobject]@{ Median = $workerUsdPerHour * 1.5; P80 = $workerUsdPerHour * 2.5; N = 0; Basis = 'default' } }
			$queue += [pscustomobject]@{ Number = $t.Number; Title = $t.Title; Model = $t.Model; Effort = $t.Effort; Median = $e.Median; P80 = $e.P80; Basis = $e.Basis; N = $e.N }
		}
	} catch { $queueError = $_.Exception.Message }
}

$rec = Get-UsageRecommendation -Week $week -FiveHour $five -WeekPerUsd $weekPerUsd -FivePerUsd $fivePerUsd `
	-WorkerUsdPerHour $workerUsdPerHour -BurnUsdPerHour ([double]$burn) -RunningWorkers $liveWorkers.Count `
	-QueueUsd @($queue | ForEach-Object P80) -Now $now -MaxWorkers $MaxWorkers

$out = [pscustomobject]@{
	Now = $now; Week = $week; FiveHour = $five; BurnUsdPerHour = $burn; BurnSinceWindowStart = $burnSince
	WeekCalibration = $weekCal; FiveCalibration = $fiveCal; WeekPerUsdUsed = $weekPerUsd; WeekCalibrationNote = $weekNote
	WorkerUsdPerHour = $workerUsdPerHour; WorkerRateBasis = $rateBasis; RunningWorkers = $liveWorkers.Count
	Tasks = $taskCosts; Queue = $queue; Recommendation = $rec
}
if ($Json) { $out | ConvertTo-Json -Depth 8; return }

function Fmt-Reset([double]$At) { $h = ($At - $now) / 3600; if ($h -ge 48) { '{0:0.0}d' -f ($h / 24) } else { '{0:0.0}h' -f $h } }
function Fmt-Cal($c, $perUsd, $note) {
	if ($null -eq $perUsd) { return 'no calibration yet' }
	'{0:0.00}%/USD ({1}; {2} intervals, {3:0.#}% seen{4})' -f $perUsd, $note, $c.Intervals, $c.PctSeen, $(if ($c.Spread) { ", spread x$($c.Spread)" } else { '' })
}
"NOW     week $(if ($week) { '{0:0}%, resets in {1}' -f $week.Pct, (Fmt-Reset $week.ResetsAt) } else { '?' }) | 5h $(if ($five) { '{0:0}%, resets in {1}' -f $five.Pct, (Fmt-Reset $five.ResetsAt) } else { '?' })"
$burnText = if ($null -ne $burn) { '{0:0.0} USD/h last hour' -f $burn } else { 'unknown (no samples in the last hour)' }
if ($null -ne $burnSince) { $burnText += ', {0:0.0} USD/h since the 5h window began' -f $burnSince }
"        burn $burnText; $($liveWorkers.Count) live worker(s), one worker ~ $('{0:0.0}' -f $workerUsdPerHour) USD/h ($rateBasis)"
"        week calibration: $(Fmt-Cal $weekCal $weekPerUsd $weekNote)"
"        5h   calibration: $(Fmt-Cal $fiveCal $fivePerUsd $fiveCal.Confidence)"
foreach ($k in 'Week', 'Five') {
	$b = $rec.$k; if (-not $b) { continue }
	"BUDGET  $(if ($k -eq 'Week') { 'week' } else { '5h  ' }) {0:0}% of cap {1}%: room {2:0.#}% in {3} h$(if ($null -ne $b.Workers) { "; at the current pace -> {0:0}% at reset; fits {1} worker(s)" -f $b.ProjectedAtReset, $b.Workers } else { '; not calibrated' })" -f $b.Pct, $b.Cap, $b.Room, $b.HoursToReset
}
if ($taskCosts.Count) {
	$byModel = $taskCosts | Group-Object { "$($_.Model) $($_.Effort)" }
	foreach ($g in $byModel) {
		$usd = @($g.Group | ForEach-Object Usd)
		"TASKS   {0}: {1} done, median {2:0.0} USD, p80 {3:0.0} USD{4}" -f $g.Name, $g.Count, (Get-Median $usd), (Get-Percentile $usd 0.8), $(if ($weekPerUsd) { ' (~{0:0.0}% / {1:0.0}% of the week)' -f ((Get-Median $usd) * $weekPerUsd), ((Get-Percentile $usd 0.8) * $weekPerUsd) } else { '' })
	}
} else { 'TASKS   no finished task recorded yet (sessions are recorded 20 min after their last activity)' }
foreach ($q in $queue) { "        next #{0} {1} {2}: ~{3:0.0} USD (p80 {4:0.0}; basis {5}, n={6})" -f $q.Number, $q.Model, $q.Effort, $q.Median, $q.P80, $q.Basis, $q.N }
if ($queueError) { "        queue forecast unavailable: $queueError" }
if ($null -eq $burn) { "NOTE    burn unknown: assuming the running workers burn at the median rate (conservative until usage.jsonl has an hour of samples)" }
if ($weekNote -like 'prior*') { "NOTE    week ratio is a rough prior (it includes the owner's other use); it is replaced by the sampled ratio as usage.jsonl fills" }
foreach ($n in $rec.Notes) { "NOTE    $n" }
"ADVICE  run $($rec.Workers) worker(s) now (running $($liveWorkers.Count); binding limit: $($rec.Binding))$(if ($null -ne $rec.Fit) { "; $($rec.Fit) of $($queue.Count) READY task(s) fit the budget at p80 cost" })"
"SUMMARY week $(if ($week) { '{0:0}%' -f $week.Pct }) / 5h $(if ($five) { '{0:0}%' -f $five.Pct }) | burn $(if ($null -ne $burn) { '{0:0.0}' -f $burn } else { '?' }) USD/h | workers $($liveWorkers.Count) -> $($rec.Workers) ($($rec.Binding))"
