# Usage accounting of the Claude limits (#217, D-022): the samples of the limits and costs, the calibration
# "% of a limit per dollar", the per-task cost ledger and the worker-count recommendation. Pure functions - the
# self-test (test-usage-plan.ps1) feeds them synthetic samples and a sample of the core's status JSON; usage-plan.ps1
# feeds them the real files.
#
# Input - the coordinator core's status line (coordinator/core/scripts/statusline.ps1, #346), in ~/.claude/coordinator/:
#   statusline.json     the last status JSON drawn by any session: rate_limits.{five_hour,seven_day}.{used_percentage,resets_at}
#   sessions/<id>.json  one status JSON per session: session_name, worktree.branch, model.display_name, effort.level,
#                       context_window.used_percentage, cost.total_cost_usd, cost.total_duration_ms; the file's write time
#                       is when the session last drew its status line
# Our own state - ~/.claude/arxvr-usage/ (the core keeps no history of the limits, so the samples are ours):
#   usage.jsonl  one line per minute at most: {t, fh, fhr, wk, wkr, s: {<session id>: cost_usd}}
#                t = epoch seconds, fh / wk = used % of the 5-hour / weekly limit, fhr / wkr = their reset (epoch seconds),
#                s = cumulative cost_usd of the sessions that were live (status file written within 10 minutes)
#   tasks.jsonl  one line per finished session: {issue, role, session_id, name, model, effort, cost_usd, ctx_peak_pct,
#                duration_h, launches, week_pct, five_hour_pct, ended}
# cost_usd is Claude Code's API-equivalent dollars, so it already normalises the models; the ratio to the limit's
# percent is what the calibration measures.
# (Until #346 a statusline.ps1 of this repository wrote ~/.claude/statusline/; since the user status line is the core's,
# those files no longer update and nothing reads them.)

function Get-UsageDir { Join-Path $env:USERPROFILE '.claude\arxvr-usage' }

function Get-CoreStatusDir { Join-Path $(if ($env:CLAUDE_CONFIG_DIR) { $env:CLAUDE_CONFIG_DIR } else { Join-Path $env:USERPROFILE '.claude' }) 'coordinator' }

# The core's sessions/<id>.json files as ledger records (the shape Get-FinishedTaskRecords reads). ctx peak is not kept by the core.
function Read-CoreSessions([string]$StatusDir) {
	foreach ($f in Get-ChildItem (Join-Path $StatusDir 'sessions') -Filter '*.json' -ErrorAction SilentlyContinue) {
		try { $j = Get-Content $f.FullName -Raw | ConvertFrom-Json } catch { continue }
		if (-not $j.session_id) { continue }
		[pscustomobject]@{
			session_id = $j.session_id; name = $j.session_name; branch = $j.worktree.branch; model = $j.model.display_name
			effort = $j.effort.level; context_pct = $j.context_window.used_percentage; cost_usd = $j.cost.total_cost_usd
			duration_ms = $j.cost.total_duration_ms; updated = $f.LastWriteTime.ToString('s')
		}
	}
}

# One usage.jsonl line from the core's last status JSON and the ledger; $null when the status JSON has no limits (API-key setup).
function New-UsageSample($Last, $Ledger, [double]$Now, [int]$LiveMinutes = 10) {
	$rl = $Last.rate_limits
	if (-not $rl -or -not ($rl.five_hour -or $rl.seven_day)) { return $null }
	$live = [ordered]@{}
	foreach ($s in $Ledger) {
		if ($null -ne $s.cost_usd -and $Now - (ConvertTo-Epoch $s.updated) -lt $LiveMinutes * 60) { $live[$s.session_id] = [math]::Round([double]$s.cost_usd, 4) }
	}
	[ordered]@{ t = $Now; fh = $rl.five_hour.used_percentage; fhr = $rl.five_hour.resets_at; wk = $rl.seven_day.used_percentage; wkr = $rl.seven_day.resets_at; s = $live }
}

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

# Task size S / M / L: a size:S|M|L label wins; otherwise a score from the issue text - scope bullets (list items), length
# (one point per 600 characters) and distinct #N references (dependencies / related tasks, one point each).
function Get-TaskSize([string]$Body, [string[]]$Labels = @()) {
	$label = @($Labels | Where-Object { $_ -match '^size:([SML])$' } | ForEach-Object { $Matches[1] }) | Select-Object -First 1
	if ($label) { return [pscustomobject]@{ Size = $label; Score = $null; Basis = "label size:$label" } }
	if (-not $Body) { return [pscustomobject]@{ Size = $null; Score = $null; Basis = 'no issue text' } }
	$bullets = ([regex]::Matches($Body, '(?m)^\s*(?:[-*]|\d+\.)\s+\S')).Count
	$refs = @([regex]::Matches($Body, '#(\d+)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique).Count
	$score = $bullets + [math]::Floor($Body.Length / 600) + $refs
	$size = if ($score -lt 8) { 'S' } elseif ($score -lt 16) { 'M' } else { 'L' }
	[pscustomobject]@{ Size = $size; Score = $score; Basis = "text score $score ($bullets bullets, $($Body.Length) chars, $refs refs)" }
}

# The cost of finished tasks by issue (workers and reviewers of a PR count to the task), in dollars.
function Get-TaskCosts($TaskRecords, $Sizes = @{}) {
	@($TaskRecords | Group-Object session_id | ForEach-Object { $_.Group[-1] }) | Where-Object { $_.issue -and $_.role -ne 'coordinator' } | Group-Object issue | ForEach-Object {
		$workers = @($_.Group | Where-Object role -eq 'worker')
		$first = if ($workers.Count) { $workers[0] } else { $_.Group[0] }
		[pscustomobject]@{
			Issue = [int]$_.Name; Size = $Sizes[[int]$_.Name]; Model = ("$($first.model)" -split ' ')[0].ToLower(); Effort = $first.effort
			Usd = [double](($_.Group | Measure-Object cost_usd -Sum).Sum)
			Hours = [double](($workers | Measure-Object duration_h -Sum).Sum)
		}
	}
}

# Median and p80 task cost (dollars) for a model / effort / size. The pool narrows from the most specific match (model +
# effort + size) to the model alone and finally to all tasks; a pool needs at least 3 finished tasks. The basis says which was used.
function Get-TaskCostEstimate($TaskCosts, [string]$Model, [string]$Effort, [string]$Size = '') {
	$tc = @($TaskCosts)
	$tiers = @()
	if ($Size) {
		$tiers += , @("$Model $Effort size $Size", @($tc | Where-Object { $_.Model -eq $Model -and $_.Effort -eq $Effort -and $_.Size -eq $Size }))
		$tiers += , @("$Model size $Size", @($tc | Where-Object { $_.Model -eq $Model -and $_.Size -eq $Size }))
	}
	$tiers += , @("$Model $Effort", @($tc | Where-Object { $_.Model -eq $Model -and $_.Effort -eq $Effort }))
	$tiers += , @("$Model (model median$(if (-not $Size) { '' } else { ', size unknown or too few' }))", @($tc | Where-Object { $_.Model -eq $Model }))
	$tiers += , @('all models', $tc)
	$pool = @(); $basis = ''
	foreach ($tier in $tiers) { if ($tier[1].Count -ge 3) { $pool = $tier[1]; $basis = $tier[0]; break } }
	if (-not $pool.Count) { $pool = $tc; $basis = 'all models' }
	if (-not $pool.Count) { return $null }
	$usd = @($pool | ForEach-Object Usd)
	[pscustomobject]@{ Median = Get-Median $usd; P80 = Get-Percentile $usd 0.8; N = $pool.Count; Basis = $basis }
}

# How many workers to keep running, and how far the caps are. NOT pacing (D-027): the week is not stretched to its reset -
# every runnable task runs until a cap stops it.
#   Week / FiveHour    @{ Pct; ResetsAt }  (epoch seconds)
#   WeekPerUsd / FivePerUsd   % of the limit per dollar (calibration); $null = unknown
#   WorkerUsdPerHour   what one worker burns
#   BurnUsdPerHour     what everything burns now (workers + coordinator + reviewers + the owner's own sessions)
#   RunningWorkers     workers running now
#   ReadyCount         runnable (READY) tasks not started yet; $null = unknown (then only the running ones are counted)
#   QueueUsd           the forecast cost (dollars, p80) of the READY tasks, in start order
# Workers = RunningWorkers + ReadyCount, bounded by MaxWorkers only when it is > 0 (default 0 = no cap, D-027); 0 when a cap is already reached.
# Per limit: EtaHours = hours until the cap at the current burn, EtaHoursWithN = the same with Workers workers on top of the
# base load (burn minus the running workers), Fit = how many READY tasks fit before the cap at their p80 cost.
function Get-UsageRecommendation {
	param(
		$Week, $FiveHour, $WeekPerUsd, $FivePerUsd, [double]$WorkerUsdPerHour, [double]$BurnUsdPerHour,
		[int]$RunningWorkers, $ReadyCount = $null, [double[]]$QueueUsd = @(), [double]$Now, [int]$MaxWorkers = 0,
		[double]$WeekCap = 90, [double]$FiveCap = 95
	)
	$base = [math]::Max(0.0, $BurnUsdPerHour - $RunningWorkers * $WorkerUsdPerHour)
	$ready = if ($null -ne $ReadyCount) { [int]$ReadyCount } else { $null }
	$wanted = if ($null -ne $ready) { $RunningWorkers + $ready } else { $RunningWorkers }; if ($MaxWorkers -gt 0) { $wanted = [int][math]::Min($MaxWorkers, $wanted) }
	$result = [ordered]@{ Workers = $wanted; Binding = $(if ($MaxWorkers -gt 0 -and $wanted -ge $MaxWorkers) { 'worker cap' } else { 'ready tasks' }); Week = $null; Five = $null; Fit = $null; EtaHours = $null; EtaHoursWithN = $null; Notes = @() }

	$limits = @(
		@{ Key = 'Week'; Name = 'weekly'; L = $Week; PerUsd = $WeekPerUsd; Cap = $WeekCap }
		@{ Key = 'Five'; Name = '5-hour'; L = $FiveHour; PerUsd = $FivePerUsd; Cap = $FiveCap }
	)
	$capped = $false
	foreach ($x in $limits) {
		if (-not $x.L) { $result.Notes += "$($x.Name) limit unknown"; continue }
		$room = [double]$x.Cap - [double]$x.L.Pct
		$info = [ordered]@{ Pct = [double]$x.L.Pct; Cap = $x.Cap; Room = $room; HoursToReset = [math]::Round(($x.L.ResetsAt - $Now) / 3600, 1); EtaHours = $null; EtaHoursWithN = $null; BudgetUsd = $null; Fit = $null; WorkerPctPerHour = $null }
		if ($room -le 0) { $capped = $true; $info.EtaHours = 0; $info.EtaHoursWithN = 0; $info.BudgetUsd = 0; $info.Fit = 0 }
		elseif ($null -eq $x.PerUsd) {
			$result.Notes += "$($x.Name): no calibration yet, no ETA to the cap"
		} else {
			$info.WorkerPctPerHour = [math]::Round($WorkerUsdPerHour * $x.PerUsd, 2)
			$cur = $BurnUsdPerHour * $x.PerUsd                                   # % of the limit per hour now
			$withN = ($base + $wanted * $WorkerUsdPerHour) * $x.PerUsd           # ... with the advised workers
			$info.EtaHours = if ($cur -gt 0) { [math]::Round($room / $cur, 1) } else { $null }
			$info.EtaHoursWithN = if ($withN -gt 0) { [math]::Round($room / $withN, 1) } else { $null }
			$info.BudgetUsd = $room / $x.PerUsd                                  # dollars until the cap
			$left = $info.BudgetUsd; $fit = 0
			foreach ($c in $QueueUsd) { if ($left -ge $c) { $left -= $c; $fit++ } else { break } }
			$info.Fit = $fit
		}
		$result[$x.Key] = [pscustomobject]$info
	}
	if ($capped) { $result.Workers = 0; $result.Binding = 'cap reached' }
	$etas = @($result.Week, $result.Five | Where-Object { $_ -and $null -ne $_.EtaHours } | ForEach-Object EtaHours)
	$etasN = @($result.Week, $result.Five | Where-Object { $_ -and $null -ne $_.EtaHoursWithN } | ForEach-Object EtaHoursWithN)
	if ($etas.Count) { $result.EtaHours = ($etas | Measure-Object -Minimum).Minimum }
	if ($etasN.Count) { $result.EtaHoursWithN = ($etasN | Measure-Object -Minimum).Minimum }
	$fits = @($result.Week, $result.Five | Where-Object { $_ -and $null -ne $_.Fit } | ForEach-Object Fit)
	if ($fits.Count) { $result.Fit = ($fits | Measure-Object -Minimum).Minimum }
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
