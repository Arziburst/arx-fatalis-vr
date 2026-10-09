# Usage accounting of the Claude limits (#217, D-022): the samples written by statusline.ps1, the calibration
# "% of a limit per dollar", the per-task cost ledger and the worker-count recommendation. Pure functions - the
# self-test (test-usage-plan.ps1) feeds them synthetic samples; usage-plan.ps1 feeds them the real files.
#
# Files in ~/.claude/statusline/:
#   usage.jsonl  one line per minute at most: {t, fh, fhr, wk, wkr, s: {<session id>: cost_usd}}
#                t = epoch seconds, fh / wk = used % of the 5-hour / weekly limit, fhr / wkr = their reset (epoch seconds),
#                s = cumulative cost_usd of the sessions that were live (ledger updated within 10 minutes)
#   tasks.jsonl  one line per finished session: {issue, role, session_id, name, model, effort, cost_usd, ctx_peak_pct,
#                duration_h, launches, week_pct, five_hour_pct, ended}
# cost_usd is Claude Code's API-equivalent dollars, so it already normalises the models; the ratio to the limit's
# percent is what the calibration measures.

function Get-UsageDir { Join-Path $env:USERPROFILE '.claude\statusline' }

function Read-JsonLines([string]$Path) {
	if (-not (Test-Path $Path)) { return @() }
	@(Get-Content -Path $Path -Encoding UTF8 | Where-Object { $_.Trim() } | ForEach-Object { try { $_ | ConvertFrom-Json } catch {} })
}

# ConvertFrom-Json turns ISO date strings into DateTime; tests pass plain strings - accept both.
function ConvertTo-Epoch($Value) {
	$d = if ($Value -is [datetime]) { $Value } else { [datetime]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture) }
	[DateTimeOffset]::new($d).ToUnixTimeSeconds()
}

function Get-Median([double[]]$Values) {
	$v = @($Values | Sort-Object)
	if (-not $v.Count) { return $null }
	$m = [int][math]::Floor($v.Count / 2)
	if ($v.Count % 2) { $v[$m] } else { ($v[$m - 1] + $v[$m]) / 2 }
}

function Get-Percentile([double[]]$Values, [double]$P) {
	$v = @($Values | Sort-Object)
	if (-not $v.Count) { return $null }
	$v[[int][math]::Min($v.Count - 1, [math]::Ceiling($P * $v.Count) - 1)]
}

# Dollars spent between two samples: the growth of every session present in both (a session seen for the first time
# counts from its first value - what it spent before we looked is unknown and would otherwise inflate the interval).
function Get-SampleCostDelta($A, $B) {
	$d = 0.0
	foreach ($p in $B.s.PSObject.Properties) {
		$prev = $A.s.PSObject.Properties[$p.Name]
		if ($prev) { $d += [math]::Max(0.0, [double]$p.Value - [double]$prev.Value) }
	}
	$d
}

# Calibration of one limit ('fh' or 'wk'): percent of the limit per dollar, from the intervals between consecutive
# samples inside one window (same reset time). The percentage moves in whole steps, so the intervals are pooled into
# chunks of at least 2 percentage points and the spread is that of the chunk ratios.
function Get-UsageCalibration($Samples, [string]$Limit) {
	$resetKey = "${Limit}r"
	$sorted = @($Samples | Where-Object { $null -ne $_.$Limit -and $_.s } | Sort-Object t)
	$chunks = @(); $dPct = 0.0; $dUsd = 0.0; $intervals = 0; $totalPct = 0.0; $totalUsd = 0.0
	for ($i = 1; $i -lt $sorted.Count; $i++) {
		$a = $sorted[$i - 1]; $b = $sorted[$i]
		if ($a.$resetKey -ne $b.$resetKey) { $dPct = 0.0; $dUsd = 0.0; continue }   # the window rolled over
		$usd = Get-SampleCostDelta $a $b
		$pct = [double]$b.$Limit - [double]$a.$Limit
		if ($pct -lt 0) { continue }
		$intervals++; $dPct += $pct; $dUsd += $usd; $totalPct += $pct; $totalUsd += $usd
		if ($dPct -ge 2 -and $dUsd -gt 0.05) { $chunks += ($dPct / $dUsd); $dPct = 0.0; $dUsd = 0.0 }
	}
	$ratio = if ($totalUsd -gt 0.05 -and $totalPct -gt 0) { $totalPct / $totalUsd } else { $null }
	$spread = if ($chunks.Count -ge 2) { [math]::Round((Get-Percentile $chunks 0.8) / (Get-Percentile $chunks 0.2), 2) } else { $null }
	$confidence = if ($null -eq $ratio) { 'none' } elseif ($totalPct -ge 10 -and $chunks.Count -ge 4 -and $spread -le 2) { 'good' } elseif ($totalPct -ge 3) { 'rough' } else { 'weak' }
	[pscustomobject]@{ PctPerUsd = $ratio; Intervals = $intervals; PctSeen = $totalPct; UsdSeen = $totalUsd; Chunks = $chunks.Count; Spread = $spread; Confidence = $confidence }
}

# Dollars per hour of everything the samples saw in the last Hours hours (all live sessions together).
function Get-UsageBurn($Samples, [double]$Now, [double]$Hours = 1) {
	$recent = @($Samples | Where-Object { $_.t -ge $Now - $Hours * 3600 } | Sort-Object t)
	if ($recent.Count -lt 2) { return $null }
	$usd = 0.0
	for ($i = 1; $i -lt $recent.Count; $i++) { $usd += Get-SampleCostDelta $recent[$i - 1] $recent[$i] }
	$span = ($recent[-1].t - $recent[0].t) / 3600
	if ($span -lt 0.05) { return $null }
	$usd / $span
}

# The cost of finished tasks by issue (workers and reviewers of a PR count to the task), in dollars.
function Get-TaskCosts($TaskRecords) {
	@($TaskRecords | Group-Object session_id | ForEach-Object { $_.Group[-1] }) | Where-Object { $_.issue -and $_.role -ne 'coordinator' } | Group-Object issue | ForEach-Object {
		$workers = @($_.Group | Where-Object role -eq 'worker')
		$first = if ($workers.Count) { $workers[0] } else { $_.Group[0] }
		[pscustomobject]@{
			Issue = [int]$_.Name; Model = ("$($first.model)" -split ' ')[0].ToLower(); Effort = $first.effort
			Usd = [double](($_.Group | Measure-Object cost_usd -Sum).Sum)
			Hours = [double](($workers | Measure-Object duration_h -Sum).Sum)
		}
	}
}

# Median and p80 task cost (dollars) for a model / effort; falls back to the model, then to all tasks.
function Get-TaskCostEstimate($TaskCosts, [string]$Model, [string]$Effort) {
	$tc = @($TaskCosts)
	$pool = @($tc | Where-Object { $_.Model -eq $Model -and $_.Effort -eq $Effort }); $basis = "$Model $Effort"
	if ($pool.Count -lt 3) { $pool = @($tc | Where-Object { $_.Model -eq $Model }); $basis = $Model }
	if ($pool.Count -lt 3) { $pool = $tc; $basis = 'all models' }
	if (-not $pool.Count) { return $null }
	$usd = @($pool | ForEach-Object Usd)
	[pscustomobject]@{ Median = Get-Median $usd; P80 = Get-Percentile $usd 0.8; N = $pool.Count; Basis = $basis }
}

# How many workers to run, from the budget and the cost rates.
#   Week / FiveHour    @{ Pct; ResetsAt }  (epoch seconds)
#   WeekPerUsd / FivePerUsd   % of the limit per dollar (calibration); $null = unknown
#   WorkerUsdPerHour   what one worker burns
#   BurnUsdPerHour     what everything burns now (workers + coordinator + reviewers + the owner's own sessions)
#   RunningWorkers     workers running now
#   QueueUsd           the forecast cost (dollars) of the queued tasks, in start order
# The base load (burn minus the running workers) is kept fixed; the workers are added on top of it. The week may use
# up to WeekCap % by its reset, the 5-hour window up to FiveCap % by its reset - but at least MinHours of spread, so
# a window about to roll over does not look like an unlimited budget.
function Get-UsageRecommendation {
	param(
		$Week, $FiveHour, $WeekPerUsd, $FivePerUsd, [double]$WorkerUsdPerHour, [double]$BurnUsdPerHour,
		[int]$RunningWorkers, [double[]]$QueueUsd = @(), [double]$Now, [int]$MaxWorkers = 3,
		[double]$WeekCap = 80, [double]$FiveCap = 95, [double]$MinHours = 1
	)
	$base = [math]::Max(0.0, $BurnUsdPerHour - $RunningWorkers * $WorkerUsdPerHour)
	$result = [ordered]@{ Workers = $MaxWorkers; Binding = 'none'; Week = $null; Five = $null; Fit = $null; Notes = @() }

	$limits = @(
		@{ Key = 'Week'; Name = 'weekly'; L = $Week; PerUsd = $WeekPerUsd; Cap = $WeekCap }
		@{ Key = 'Five'; Name = '5-hour'; L = $FiveHour; PerUsd = $FivePerUsd; Cap = $FiveCap }
	)
	foreach ($x in $limits) {
		if (-not $x.L) { $result.Notes += "$($x.Name) limit unknown"; continue }
		$hours = [math]::Max($MinHours, ([double]$x.L.ResetsAt - $Now) / 3600)
		$room = [double]$x.Cap - [double]$x.L.Pct
		$info = [ordered]@{ Pct = [double]$x.L.Pct; Cap = $x.Cap; Room = $room; HoursToReset = [math]::Round(($x.L.ResetsAt - $Now) / 3600, 1); Workers = $null; ProjectedAtReset = $null }
		if ($null -eq $x.PerUsd) {
			$result.Notes += "$($x.Name): no calibration yet, limit not used for the count"
		} else {
			$workerRate = $WorkerUsdPerHour * $x.PerUsd                      # % of the limit per hour per worker
			$baseRate = $base * $x.PerUsd
			$allowed = $room / $hours                                         # % per hour the budget allows
			$info.Workers = if ($room -le 0) { 0 } elseif ($workerRate -le 0) { $MaxWorkers } else { [int][math]::Max(0.0, [math]::Floor(($allowed - $baseRate) / $workerRate)) }
			$info.ProjectedAtReset = [math]::Round([double]$x.L.Pct + ($baseRate + $RunningWorkers * $workerRate) * (($x.L.ResetsAt - $Now) / 3600), 0)
			$info.BudgetUsd = [math]::Max(0.0, ($room - $baseRate * $hours) / $x.PerUsd)   # dollars of worker spend left before the cap
			$info.WorkerPctPerHour = [math]::Round($workerRate, 2)
		}
		$result[$x.Key] = [pscustomobject]$info
	}
	$counts = @(); foreach ($k in 'Week', 'Five') { if ($result[$k] -and $null -ne $result[$k].Workers) { $counts += [pscustomobject]@{ Key = $k; N = $result[$k].Workers } } }
	if ($counts.Count) {
		$min = ($counts | Measure-Object N -Minimum).Minimum
		$result.Workers = [int][math]::Min($MaxWorkers, $min)
		$bound = @($counts | Where-Object N -eq $min | ForEach-Object Key)
		$result.Binding = if ($min -ge $MaxWorkers) { 'worker cap' } elseif ($bound.Count -gt 1) { 'both' } elseif ($bound[0] -eq 'Week') { 'weekly' } else { '5-hour' }
	} else {
		$result.Workers = [math]::Min($MaxWorkers, 1); $result.Binding = 'unknown'
		$result.Notes += 'no calibration: running one worker until the samples cover a few percent'
	}
	# queued tasks that fit before the weekly reset (the tighter of the two budgets)
	$budgets = @($result.Week, $result.Five | Where-Object { $_ -and $null -ne $_.Workers } | ForEach-Object BudgetUsd)
	if ($budgets.Count) {
		$left = ($budgets | Measure-Object -Minimum).Minimum; $fit = 0
		foreach ($c in $QueueUsd) { if ($left -ge $c) { $left -= $c; $fit++ } else { break } }
		$result.Fit = $fit
	}
	[pscustomobject]$result
}

# The finished session records (tasks.jsonl lines) from the session ledger: sessions of a task that were not updated for
# IdleMinutes and are not recorded yet. Returns the new records; the caller appends them.
function Get-FinishedTaskRecords {
	param($Ledger, $Recorded, [double]$Now, $WeekPerUsd, $FivePerUsd, [int]$IdleMinutes = 20, $Launches = @{})
	$done = @{}; foreach ($r in $Recorded) { $done[$r.session_id] = [double]$r.cost_usd }
	foreach ($s in $Ledger) {
		if ($done.ContainsKey($s.session_id) -and [double]$s.cost_usd -le $done[$s.session_id] + 0.01) { continue }
		$updated = ConvertTo-Epoch $s.updated
		if ($Now - $updated -lt $IdleMinutes * 60) { continue }
		$issue = $null; $role = 'worker'
		if ($s.name -match 'Coordinator') { $role = 'coordinator' }
		elseif ($s.name -match '^ARXVR-(\d+)\s+review') { $issue = [int]$Matches[1]; $role = 'reviewer' }
		elseif ($s.branch -match '^ARXVR-(\d+)') { $issue = [int]$Matches[1] }
		elseif ($s.name -match '^ARXVR-(\d+)') { $issue = [int]$Matches[1] }
		if ($role -ne 'coordinator' -and -not $issue) { continue }
		[pscustomobject][ordered]@{
			issue = $issue; role = $role; session_id = $s.session_id; name = $s.name; model = $s.model; effort = $s.effort
			cost_usd = [double]$s.cost_usd; ctx_peak_pct = if ($null -ne $s.context_peak) { $s.context_peak } else { $s.context_pct }
			duration_h = [math]::Round([double]$s.duration_ms / 3600000, 2)
			launches = if ($issue -and $Launches.ContainsKey($issue)) { $Launches[$issue] } else { 0 }
			week_pct = if ($null -ne $WeekPerUsd) { [math]::Round([double]$s.cost_usd * $WeekPerUsd, 2) } else { $null }
			five_hour_pct = if ($null -ne $FivePerUsd) { [math]::Round([double]$s.cost_usd * $FivePerUsd, 2) } else { $null }
			ended = $s.updated
		}
	}
}
