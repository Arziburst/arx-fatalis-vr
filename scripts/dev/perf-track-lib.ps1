# CI performance tracking (#552): every CI game phase leaves its arx-perf.log in its evidence folder; after the phases
# ran they become per-phase numbers (perf.json in the evidence), a rolling baseline on the runner (perf-baseline.json
# beside ci-history.json, fed only by green main and gate runs) and a non-blocking flag on a pull request whose phase
# got clearly slower. Dot-source it:
#   . (Join-Path $PSScriptRoot 'perf-track-lib.ps1')
# Used by perf-track.ps1 (which ci-smoke.ps1 runs at its end) and perf-noise.ps1; tested by test-perf-track.ps1 (its
# header is the interface). docs/vr/TESTING.md, "Performance tracking in CI".

. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

# The thresholds, from the noise of the green 4-lane runs measured in #552's pull request (perf-noise.ps1; the table
# is in TESTING.md). A metric is flagged above median + max(K x spread, median x FloorPct %, its absolute floor).
# Flagged: the frames-weighted mean and median frame time - a phase's run-to-run spread is 0.4 % (p90 1.1 %) of them.
# Shown, never flagged: p95 is bimodal on the short CI phases (the 95 % rank lands in or out of the level-load second,
# 14 or 300+ ms), the max is the level-load frame, hitches and missed slots jitter by whole units.
$ArxPerfK = 4
$ArxPerfFloorPct = 10
$ArxPerfMetrics = @('meanMs', 'medianMs')
$ArxPerfInfoMetrics = @('p95Ms', 'maxMs', 'hitches', 'missed')
# Counts jitter by whole units on a median of 0 or a few: their own absolute floor (when they are flagged by -Metrics)
$ArxPerfAbsFloor = @{ hitches = 3; missed = 10 }
$ArxPerfMinRuns = 5
# A phase of fewer whole seconds is shown, never flagged: in 1-2 seconds a deliberate stall or a level load is the
# number (hang-dump, level-load-hang, vr-session-end, vr-session-loss gave 6 of the 8 measured false flags)
$ArxPerfMinSeconds = 3
$ArxPerfRuns = 10
# The baseline file keeps more runs than one lane count's window: other lane counts' runs sit between them
$ArxPerfKeep = 40
# The numbers a baseline run stores per phase
$ArxPerfBaselineMetrics = @('meanMs', 'medianMs', 'p95Ms', 'maxMs', 'hitches', 'missed', 'peakPrivMiB')

$ArxPerfCulture = [Globalization.CultureInfo]::InvariantCulture

function ConvertTo-ArxPerfNumber($Value) { if ($null -eq $Value) { $null } else { [math]::Round([double]$Value, 3) } }

# The first item of $Items (sorted by $Key) whose running frame count reaches $Share of all frames: a frames-weighted
# nearest-rank percentile of the per-second values
function Get-ArxWeightedRank($Seconds, [string]$Key, [double]$Share) {
	$total = 0.0
	foreach ($s in $Seconds) { $total += $s.frames }
	$running = 0.0
	foreach ($s in ($Seconds | Sort-Object { [double]$_.$Key })) {
		$running += $s.frames
		if ($running -ge $Share * $total) { return [double]$s.$Key }
	}
	$null
}

# One phase's numbers from its arx-perf.log (the interface in test-perf-track.ps1); never throws
function Get-ArxPhasePerfStats([string]$Path, [string]$Phase) {
	$rec = [ordered]@{
		phase = $Phase; valid = $false; reason = ''; seconds = 0; frames = 0
		meanMs = $null; medianMs = $null; p95Ms = $null; maxMs = $null
		hitches = 0; missed = 0; causes = [ordered]@{}; busiestCause = $null; busiestCauseHitches = 0
		peakPrivMiB = $null; ended = $false; unknownLines = 0
	}
	$lines = $null
	if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { $rec.reason = 'no arx-perf.log' }
	else {
		try { $lines = @(Get-Content -LiteralPath $Path -ErrorAction Stop) } catch { $rec.reason = "unreadable: $($_.Exception.Message)" }
	}
	if ($null -eq $lines) { return [pscustomobject]$rec }
	if ($lines.Count -eq 0) { $rec.reason = 'empty log'; return [pscustomobject]$rec }

	$log = Read-ArxPerfLog $lines
	$rec.ended = [bool]$log.Ended
	$rec.unknownLines = [int]$log.Unknown
	# A whole S line has its numbers (a line cut mid-write is already Unknown: it lacks its ' | ' parts)
	$seconds = @($log.Seconds | Where-Object { $_.frames -is [double] -and $_.frames -gt 0 -and $_.mean -is [double] -and $_.p95 -is [double] -and $_.max -is [double] })
	$hitches = @($log.Hitches | Where-Object { $_.ms -is [double] })
	$rec.hitches = $hitches.Count
	foreach ($h in $hitches) {
		$cause = if ($h.cause) { "$($h.cause)" } else { 'unknown' }
		if (-not $rec.causes.Contains($cause)) { $rec.causes[$cause] = 0 }
		$rec.causes[$cause]++
	}
	if ($hitches.Count) {
		# The most H lines; a tie goes to the larger total ms
		$busiest = $rec.causes.Keys | Sort-Object @{ Expression = { $rec.causes[$_] }; Descending = $true },
			@{ Expression = { $c = $_; ($hitches | Where-Object { "$($_.cause)" -eq $c } | Measure-Object ms -Sum).Sum }; Descending = $true } | Select-Object -First 1
		$rec.busiestCause = $busiest
		$rec.busiestCauseHitches = $rec.causes[$busiest]
	}
	$privs = @($log.Resources | Where-Object { $_.priv -is [double] } | ForEach-Object priv)
	if ($privs.Count) { $rec.peakPrivMiB = ConvertTo-ArxPerfNumber ($privs | Measure-Object -Maximum).Maximum }
	if ($seconds.Count -eq 0) {
		$rec.reason = "no whole S line ($($log.Unknown) unknown line(s))"
		return [pscustomobject]$rec
	}

	$frames = 0.0; $time = 0.0; $missed = 0.0; $max = 0.0
	foreach ($s in $seconds) {
		$frames += $s.frames
		$time += $s.frames * $s.mean
		if ($s.missed -is [double]) { $missed += $s.missed }
		if ($s.max -gt $max) { $max = $s.max }
	}
	foreach ($h in $hitches) { if ($h.ms -gt $max) { $max = $h.ms } }
	$rec.valid = $true
	$rec.seconds = $seconds.Count
	$rec.frames = [int]$frames
	$rec.missed = [int]$missed
	$rec.meanMs = ConvertTo-ArxPerfNumber ($time / $frames)
	$rec.medianMs = ConvertTo-ArxPerfNumber (Get-ArxWeightedRank $seconds 'mean' 0.5)
	$rec.p95Ms = ConvertTo-ArxPerfNumber (Get-ArxWeightedRank $seconds 'p95' 0.95)
	$rec.maxMs = ConvertTo-ArxPerfNumber $max
	[pscustomobject]$rec
}

# Every phase of a ci-smoke evidence folder: { phases = phase name -> record }, by name. Only arx-perf.log - the
# arx-perf.1.log .. .9.log beside it are earlier games of the same user directory
function Get-ArxRunPerf([string]$EvidenceDir) {
	$phases = [ordered]@{}
	if ($EvidenceDir -and (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
		foreach ($d in (Get-ChildItem -LiteralPath $EvidenceDir -Directory -ErrorAction SilentlyContinue | Sort-Object Name)) {
			$phases[$d.Name] = Get-ArxPhasePerfStats -Path (Join-Path $d.FullName 'arx-perf.log') -Phase $d.Name
		}
	}
	[pscustomobject]@{ phases = $phases }
}

# The run's record as <EvidenceDir>\perf.json; returns its path
function Write-ArxRunPerf([string]$EvidenceDir, $RunPerf, [string]$Kind, [int]$Lanes, [switch]$Green) {
	$phases = [ordered]@{}
	if ($RunPerf -and $RunPerf.phases) {
		foreach ($name in (Get-ArxPerfPhaseNames $RunPerf)) { $phases[$name] = Get-ArxPerfPhase $RunPerf $name }
	}
	$json = [ordered]@{ schema = 1; time = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'); kind = $Kind; lanes = $Lanes; green = [bool]$Green; phases = $phases }
	New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
	$path = Join-Path $EvidenceDir 'perf.json'
	[IO.File]::WriteAllText($path, (ConvertTo-Json $json -Depth 8), [Text.UTF8Encoding]::new($false))
	$path
}

# perf.json back: { kind; lanes; green; time; phases = phase name -> record }; $null when missing or broken
function Read-ArxRunPerf([string]$Path) {
	if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
	try {
		$json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -ErrorAction Stop
		if (-not $json -or -not $json.PSObject.Properties['phases']) { return $null }
		$phases = [ordered]@{}
		if ($json.phases) { foreach ($p in $json.phases.PSObject.Properties) { $phases[$p.Name] = $p.Value } }
		[pscustomobject]@{ kind = $json.kind; lanes = $json.lanes; green = $json.green; time = $json.time; phases = $phases }
	} catch { $null }
}

# The phase names of a run record (its phases a dictionary, or an object from json)
function Get-ArxPerfPhaseNames($RunPerf) {
	if ($null -eq $RunPerf -or $null -eq $RunPerf.phases) { return @() }
	if ($RunPerf.phases -is [Collections.IDictionary]) { @($RunPerf.phases.Keys) } else { @($RunPerf.phases.PSObject.Properties.Name) }
}
function Get-ArxPerfPhase($RunPerf, [string]$Name) {
	if ($RunPerf.phases -is [Collections.IDictionary]) { $RunPerf.phases[$Name] } else { $RunPerf.phases.$Name }
}
# A field of a record: a hashtable, an ordered dictionary or an object
function Get-ArxPerfField($Record, [string]$Name) {
	if ($null -eq $Record) { return $null }
	if ($Record -is [Collections.IDictionary]) { if ($Record.Contains($Name)) { return $Record[$Name] } else { return $null } }
	$p = $Record.PSObject.Properties[$Name]
	if ($p) { $p.Value } else { $null }
}

# What a CI run is, for the baseline: main (a push to main), gate (a manual run with suite full - the merge gate, on
# whichever branch), pr (a pull request whatever its suite, a manual run with suite pr), local (no event: by hand,
# ci-local.ps1), other
function Get-ArxCiRunKind([string]$EventName, [string]$RefName, [string]$Suite) {
	switch ($EventName) {
		'' { 'local' }
		'push' { if ($RefName -in 'main', 'refs/heads/main') { 'main' } else { 'other' } }
		'workflow_dispatch' { if ($Suite -eq 'full') { 'gate' } else { 'pr' } }
		'pull_request' { 'pr' }
		default { 'other' }
	}
}

# perf-baseline.json beside the durations file and ci-history.json (a file in the evidence root: the run folders'
# cleanup keeps it)
function Get-ArxPerfBaselinePath([string]$DurationsFile) { Join-Path (Split-Path -Parent $DurationsFile) 'perf-baseline.json' }

# The runs of the baseline file, oldest first; a missing or broken file is none
function Read-ArxPerfBaselineRuns([string]$Path) {
	if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
	try {
		$json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -ErrorAction Stop
		@($json.runs | Where-Object { $_ -and $_.PSObject.Properties['phases'] -and $_.PSObject.Properties['lanes'] })
	} catch { @() }
}

# Append a green main or gate run to the baseline file, keep its last -Keep runs; anything else leaves the file as it
# is (a missing one stays missing). Returns { updated; why }
function Update-ArxPerfBaseline([string]$Path, $RunPerf, [string]$Kind, [int]$Lanes, [switch]$Green, [int]$Keep = $ArxPerfKeep) {
	if ($Kind -notin 'main', 'gate') { return [pscustomobject]@{ updated = $false; why = "a $Kind run never feeds the baseline (only green main and gate runs)" } }
	if (-not $Green) { return [pscustomobject]@{ updated = $false; why = "a red $Kind run does not feed the baseline" } }
	$runs = [Collections.Generic.List[object]]::new()
	foreach ($r in (Read-ArxPerfBaselineRuns $Path)) { $runs.Add($r) }
	$phases = [ordered]@{}
	foreach ($name in (Get-ArxPerfPhaseNames $RunPerf)) {
		$rec = Get-ArxPerfPhase $RunPerf $name
		if (-not (Get-ArxPerfField $rec 'valid')) { continue }
		$m = [ordered]@{}
		foreach ($metric in $ArxPerfBaselineMetrics) {
			$v = Get-ArxPerfField $rec $metric
			if ($null -ne $v) { $m[$metric] = ConvertTo-ArxPerfNumber $v }
		}
		$phases[$name] = $m
	}
	$runs.Add([pscustomobject][ordered]@{ time = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'); kind = $Kind; lanes = $Lanes; phases = $phases })
	while ($runs.Count -gt [math]::Max(1, $Keep)) { $runs.RemoveAt(0) }
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	$tmp = "$Path.tmp"
	[IO.File]::WriteAllText($tmp, (ConvertTo-Json ([ordered]@{ schema = 1; runs = @($runs) }) -Depth 8), [Text.UTF8Encoding]::new($false))
	Move-Item -LiteralPath $tmp -Destination $Path -Force
	[pscustomobject]@{ updated = $true; why = "a green $Kind run on $Lanes lane(s): $($phases.Count) phase(s) added, $($runs.Count) run(s) kept" }
}

# The plain median (an even count averages the two middle values)
function Get-ArxMedian([double[]]$Values) {
	$sorted = @($Values | Sort-Object)
	$n = $sorted.Count
	if ($n -eq 0) { return $null }
	if ($n % 2) { $sorted[($n - 1) / 2] } else { ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2 }
}

# The rolling baseline: of the last -Runs runs with this lane count, per phase and metric { median; spread (the
# unscaled median absolute deviation); samples }. An empty dictionary when there is none
function Get-ArxPerfBaseline([string]$Path, [int]$Lanes, [int]$Runs = $ArxPerfRuns) {
	$baseline = [ordered]@{}
	$window = @(Read-ArxPerfBaselineRuns $Path | Where-Object { [int]$_.lanes -eq $Lanes } | Select-Object -Last ([math]::Max(1, $Runs)))
	$samples = @{}
	foreach ($run in $window) {
		if (-not $run.phases) { continue }
		foreach ($p in $run.phases.PSObject.Properties) {
			if (-not $samples.ContainsKey($p.Name)) { $samples[$p.Name] = [ordered]@{} }
			foreach ($m in $p.Value.PSObject.Properties) {
				if ($null -eq $m.Value) { continue }
				if (-not $samples[$p.Name].Contains($m.Name)) { $samples[$p.Name][$m.Name] = [Collections.Generic.List[double]]::new() }
				$samples[$p.Name][$m.Name].Add([double]$m.Value)
			}
		}
	}
	foreach ($phase in ($samples.Keys | Sort-Object)) {
		$metrics = [ordered]@{}
		foreach ($metric in $samples[$phase].Keys) {
			$values = [double[]]$samples[$phase][$metric].ToArray()
			$median = Get-ArxMedian $values
			$spread = Get-ArxMedian ([double[]]@($values | ForEach-Object { [math]::Abs($_ - $median) }))
			$metrics[$metric] = [pscustomobject]@{ median = $median; spread = $spread; samples = $values.Count }
		}
		$baseline[$phase] = $metrics
	}
	$baseline
}

# One row per phase of the run and per metric: the run's value against the baseline. limit = median + max(K x spread,
# median x FloorPct %, the metric's -AbsFloor); status regressed (over the limit - only slower), ok, no-baseline (unknown
# phase or metric, or fewer than -MinRuns samples), no-data (the run has no valid number). info: shown, never flagged -
# every row of -Info, and the rows of a phase with fewer than -MinSeconds whole seconds (note 'too short')
function Compare-ArxPhasePerf($RunPerf, $Baseline, [string[]]$Metrics = $ArxPerfMetrics, [double]$K = $ArxPerfK,
	[double]$FloorPct = $ArxPerfFloorPct, [int]$MinRuns = $ArxPerfMinRuns, [hashtable]$AbsFloor = $ArxPerfAbsFloor,
	[int]$MinSeconds = $ArxPerfMinSeconds, [switch]$Info) {
	$Metrics = @($Metrics | ForEach-Object { $_ -split ',' } | ForEach-Object Trim | Where-Object { $_ })
	foreach ($phase in (Get-ArxPerfPhaseNames $RunPerf)) {
		$rec = Get-ArxPerfPhase $RunPerf $phase
		$valid = [bool](Get-ArxPerfField $rec 'valid')
		$seconds = Get-ArxPerfField $rec 'seconds'
		$short = $null -ne $seconds -and [int]$seconds -lt $MinSeconds
		$shownOnly = $Info -or $short
		$known = $null
		if ($Baseline -and $Baseline.Contains($phase)) { $known = $Baseline[$phase] }
		foreach ($metric in $Metrics) {
			$value = if ($valid) { Get-ArxPerfField $rec $metric }
			$base = if ($known -and $known.Contains($metric)) { $known[$metric] }
			$row = [ordered]@{ phase = $phase; metric = $metric; status = ''; baseline = $null; spread = $null; samples = 0
				value = $(if ($null -ne $value) { [double]$value }); delta = $null; deltaPct = $null; limit = $null
				note = $(if ($valid -and $short) { 'too short' } else { '' }) }
			if ($base) { $row.samples = [int]$base.samples }
			if ($null -eq $value) { $row.status = 'no-data' }
			elseif (-not $base -or $base.samples -lt $MinRuns) { $row.status = 'no-baseline' }
			else {
				$median = [double]$base.median
				$floor = if ($AbsFloor -and $AbsFloor.ContainsKey($metric)) { [double]$AbsFloor[$metric] } else { 0.0 }
				$row.baseline = $median
				$row.spread = [double]$base.spread
				$row.delta = $row.value - $median
				$row.deltaPct = if ($median -ne 0) { 100.0 * $row.delta / $median } else { $null }
				$row.limit = $median + [math]::Max([math]::Max($K * $row.spread, $median * $FloorPct / 100.0), $floor)
				$row.status = if ($shownOnly) { 'info' } elseif ($row.value -gt $row.limit) { 'regressed' } else { 'ok' }
				if ($shownOnly) { $row.limit = $null }
			}
			[pscustomobject]$row
		}
	}
}

function Format-ArxPerfNumber($Value) { if ($null -eq $Value) { '-' } else { [string]::Format($ArxPerfCulture, '{0:0.00}', [double]$Value) } }
function Format-ArxPerfDelta($Row) {
	if ($null -eq $Row.delta) { return '-' }
	$sign = if ($Row.delta -ge 0) { '+' } else { '' }
	$pct = if ($null -ne $Row.deltaPct) { [string]::Format($ArxPerfCulture, ' ({0}{1:0.0}%)', $(if ($Row.deltaPct -ge 0) { '+' } else { '' }), $Row.deltaPct) } else { '' }
	"$sign$(Format-ArxPerfNumber $Row.delta)$pct"
}

# The job summary's Performance section (Markdown lines): the regressed rows first, then the rest in the run's order
function Format-ArxPerfTable($Rows) {
	$rows = @($Rows | Where-Object { $_ })
	$lines = [Collections.Generic.List[string]]::new()
	$lines.Add('### Performance (non-blocking, #552)')
	if ($rows.Count -eq 0) {
		$lines.Add('No performance data: no game phase left an arx-perf.log with numbers.')
		return $lines.ToArray()
	}
	$regressed = @($rows | Where-Object status -eq 'regressed')
	$phases = @($rows | ForEach-Object phase | Select-Object -Unique)
	$slow = @($regressed | ForEach-Object phase | Select-Object -Unique)
	$noBase = @($rows | Where-Object status -eq 'no-baseline').Count
	$lines.Add("$($phases.Count) phase(s): $(if ($slow) { "**$($slow.Count) slower than the baseline** ($($slow -join ', '))" } else { 'none slower than the baseline' }); $noBase row(s) without a baseline yet. Baseline: the median ± spread of the last green main and gate runs with the same lane count; a metric is flagged above its limit (median + the larger of K x spread and a floor), the ones marked 'shown only' never are. Never fails the run.")
	$lines.Add('')
	$lines.Add("<details$(if ($slow) { ' open' })><summary>The numbers ($($rows.Count) rows, ms and counts)</summary>")
	$lines.Add('')
	$lines.Add('| Phase | Metric | Baseline | This run | Delta | Limit | Status |')
	$lines.Add('|---|---|---:|---:|---:|---:|---|')
	foreach ($r in (@($regressed) + @($rows | Where-Object status -ne 'regressed'))) {
		$baseCell = switch ($r.status) {
			'no-baseline' { "no baseline yet$(if ($r.samples) { " ($($r.samples) run(s))" })" }
			default { if ($null -ne $r.baseline) { "$(Format-ArxPerfNumber $r.baseline) ± $(Format-ArxPerfNumber $r.spread)" } else { '-' } }
		}
		$status = switch ($r.status) { 'regressed' { '**regressed**' } 'no-data' { 'no data' } 'no-baseline' { '-' } 'info' { if ($r.note) { "shown only ($($r.note))" } else { 'shown only' } } default { 'ok' } }
		$lines.Add("| $($r.phase) | $($r.metric) | $baseCell | $(Format-ArxPerfNumber $r.value) | $(Format-ArxPerfDelta $r) | $(Format-ArxPerfNumber $r.limit) | $status |")
	}
	$lines.Add('')
	$lines.Add('</details>')
	$lines.ToArray()
}

# A workflow command's message and property values, escaped (the runner decodes %25, %0D, %0A; %3A and %2C in properties)
function ConvertTo-ArxWorkflowData([string]$Text) { $Text.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A') }
function ConvertTo-ArxWorkflowProperty([string]$Text) { (ConvertTo-ArxWorkflowData $Text).Replace(':', '%3A').Replace(',', '%2C') }

# One ::warning line per regressed phase, naming each regressed metric with the baseline and this run's value
function Format-ArxPerfAnnotations($Rows) {
	$regressed = @($Rows | Where-Object { $_ -and $_.status -eq 'regressed' })
	foreach ($phase in @($regressed | ForEach-Object phase | Select-Object -Unique)) {
		$parts = foreach ($r in @($regressed | Where-Object phase -eq $phase)) {
			"$($r.metric) $(Format-ArxPerfNumber $r.baseline) -> $(Format-ArxPerfNumber $r.value) ($(Format-ArxPerfDelta $r); limit $(Format-ArxPerfNumber $r.limit))"
		}
		$title = ConvertTo-ArxWorkflowProperty "Performance: $phase slower"
		$text = ConvertTo-ArxWorkflowData "$phase is slower than its baseline - $($parts -join '; '). Not blocking (#552); the job summary has the table."
		"::warning title=$title::$text"
	}
}
