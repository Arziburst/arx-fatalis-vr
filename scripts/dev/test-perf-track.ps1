<#
.SYNOPSIS
	Self-test of the CI performance tracking (#552): per-phase stats from fixture arx-perf.log files, the rolling
	baseline on the runner, the regression flag, the job summary table and the warning annotations. No game.

.DESCRIPTION
	Every CI game phase leaves its arx-perf.log in its evidence folder (ci-smoke.ps1 Save-Evidence copies arx*.log to
	<OutDir>\<phase>\). After the phases ran, those logs become per-phase numbers, a run record (perf.json) and - on a
	green main or gate run - one more entry of the baseline history on the runner; a pull request's run is compared
	with that baseline and a phase that got clearly slower is flagged with the numbers, never failing the run.

	Fixtures: scripts\dev\fixtures\perf\
	  normal.arx-perf.log      11 flat seconds, 60 frames each, no hitch, two R lines (priv 410.5, 412.3)
	  long-frame.arx-perf.log  11 VR seconds, one 520 ms frame in second 4 (45 frames there, missed 46, cause load)
	  causes.arx-perf.log      9 hitches: textures 3 (143 ms in all), render 3 (105 ms), load 2, channel 1; R peak 455.5
	  truncated.arx-perf.log   the normal run cut mid-write: 10 whole seconds, a half S line, no end line
	  empty.arx-perf.log       0 bytes
	  broken.arx-perf.log      4 lines, none of them a performance log line
	  perf-baseline.json       a baseline history: 15 runs, lanes 4 and 3, two old outliers before the last 10 of lanes 4

	THE INTERFACE (the worker implements it exactly; this file is the definition)

	scripts\dev\perf-track-lib.ps1 - dot-sources perf-summary-lib.ps1 and parses with its Read-ArxPerfLog (no second
	parser). Every function below is in it, takes these parameter names, and never throws on a missing, empty or
	broken input (it returns the "nothing" result named below instead).

	  Get-ArxPhasePerfStats -Path <arx-perf.log> -Phase <name>
	      One phase's record (a pscustomobject; the json field names are these property names):
	        phase                the -Phase name
	        valid                $true when the log has at least one whole S line; $false otherwise (missing, empty, broken)
	        reason               why it is not valid ('' or $null when valid)
	        seconds              the number of S lines
	        frames               the sum of the S lines' frames (0 when not valid)
	        meanMs               frames-weighted mean of the S lines' mean
	        medianMs             frames-weighted median of the S lines' mean: sort the seconds by mean, the first whose
	                             running frame count reaches at least half of all frames
	        p95Ms                frames-weighted 95th percentile of the S lines' p95, nearest rank: sort the seconds by
	                             p95, the first whose running frame count reaches at least 0.95 x all frames
	        maxMs                the largest of every S line's max and every H line's ms
	        hitches              the number of H lines
	        missed               the sum of the S lines' missed
	        causes               cause -> number of H lines (an ordered dictionary; an object in json); empty: no hitch
	        busiestCause         the cause with the most H lines; a tie goes to the larger total H ms; $null: no hitch
	        busiestCauseHitches  its number of H lines (0: no hitch)
	        peakPrivMiB          the largest R line priv (MiB); $null without R lines
	        ended                the '# end' line was written
	        unknownLines         lines that are none of #, S, H, M, R (Read-ArxPerfLog's Unknown)
	      When not valid: frames 0, seconds 0, meanMs / medianMs / p95Ms / maxMs $null.

	  Get-ArxRunPerf -EvidenceDir <a ci-smoke -OutDir>
	      { phases }: an ordered dictionary phase name -> Get-ArxPhasePerfStats of <EvidenceDir>\<phase>\arx-perf.log,
	      for every subfolder of EvidenceDir (sorted by name; a folder without arx-perf.log is a not-valid record).
	      Only arx-perf.log - never arx-perf.1.log .. .9.log (earlier games of the same user directory). A missing
	      EvidenceDir: no phases.

	  Write-ArxRunPerf -EvidenceDir <dir> -RunPerf <run> -Kind <kind> -Lanes <n> -Green <bool>
	      Writes <EvidenceDir>\perf.json (no machine path in it) and returns its path:
	        { "schema": 1, "time": "<yyyy-MM-dd HH:mm:ss>", "kind": "<kind>", "lanes": <n>, "green": <bool>,
	          "phases": { "<phase>": <record>, ... } }
	  Read-ArxRunPerf -Path <perf.json>
	      The same shape as Get-ArxRunPerf (phases an ordered dictionary of records) plus kind, lanes, green; $null when
	      the file is missing or broken.

	  Get-ArxCiRunKind -EventName <GITHUB_EVENT_NAME> -RefName <GITHUB_REF_NAME> -Suite <full|pr>
	      'main'  a push to main (RefName 'main' or 'refs/heads/main')
	      'gate'  a manual run (workflow_dispatch) with suite full - the merge gate
	      'pr'    a pull_request run (whatever its suite, the label full-ci included), a manual run with suite pr
	      'local' no event (a run by hand, ci-local.ps1)
	      'other' anything else

	  Get-ArxPerfBaselinePath -DurationsFile <ci-durations.json>
	      perf-baseline.json in the same folder - beside ci-history.json, on the runner
	      %LOCALAPPDATA%\ArxFatalisVR\ci-evidence\perf-baseline.json (a file in the evidence root: Remove-ArxOldCiEvidence
	      keeps it)

	  The baseline history file (perf-baseline.json; fixtures\perf\perf-baseline.json is one):
	      { "schema": 1, "runs": [ { "time": "..", "kind": "main"|"gate", "lanes": <n>,
	                                 "phases": { "<phase>": { "meanMs", "medianMs", "p95Ms", "maxMs", "hitches",
	                                                          "missed", "peakPrivMiB" } } }, ... ] }
	      oldest first; a run holds only its valid phases.

	  Update-ArxPerfBaseline -Path <perf-baseline.json> -RunPerf <run> -Kind <kind> -Lanes <n> -Green <bool> [-Keep <n>]
	      Appends the run only when Green and Kind is 'main' or 'gate'; anything else (a pull request, a red run, a run
	      by hand) leaves the file untouched - byte for byte, and a missing file stays missing. Keeps the last -Keep runs
	      of the file (oldest dropped). Returns { updated = <bool>; why = <text> }. A broken file is replaced.

	  Get-ArxPerfBaseline -Path <perf-baseline.json> -Lanes <n> [-Runs <n>]
	      The rolling baseline: from the last -Runs runs WITH THIS LANE COUNT (filter by lanes first, then take the
	      last -Runs), per phase and per metric present in them: a dictionary phase -> dictionary metric ->
	      { median; spread; samples }. median: the plain median of the samples (an even count averages the two middle
	      ones); spread: the median absolute deviation from it (unscaled MAD); samples: how many runs had the phase.
	      Missing or broken file, or no run with this lane count: an empty dictionary.

	  Compare-ArxPhasePerf -RunPerf <run> -Baseline <Get-ArxPerfBaseline> [-Metrics <names>] [-K <k>] [-FloorPct <p>]
	                       [-MinRuns <n>] [-Info]
	      One row per phase of the run (only the run's phases) and per metric of -Metrics:
	        { phase; metric; status; baseline (the median, $null without one); spread; samples; value; delta (value -
	          median); deltaPct (100 x delta / median); limit }
	      limit = median + max(K x spread, median x FloorPct / 100); status:
	        'regressed'    value > limit (only slower is flagged; a faster phase is 'ok')
	        'ok'           value <= limit
	        'no-baseline'  the phase or metric is not in the baseline, or has fewer than -MinRuns samples
	        'no-data'      the run's record of the phase is not valid
	        'info'         with the switch -Info, a row that has a baseline: shown in the table (baseline, value, delta)
	                       for information, NEVER 'regressed' and never an annotation ('no-baseline' / 'no-data' as above)
	      The flagged rows are a call with -Metrics $ArxPerfMetrics, the information rows a second call with
	      -Metrics $ArxPerfInfoMetrics -Info; perf-track.ps1 puts both into the table.
	      -Baseline may be $null or empty (every row 'no-baseline'). -RunPerf may come from Get-ArxRunPerf or
	      Read-ArxRunPerf.

	  The thresholds - set by the worker FROM THE MEASURED NOISE (the PR's noise table), as lib variables that are the
	  parameters' defaults:
	      $ArxPerfK          k of the limit
	      $ArxPerfFloorPct   the floor in percent of the median
	      $ArxPerfMetrics      the metrics flagged: meanMs and medianMs, NOT p95Ms (the coordinator's decision from the
	                           measured noise, 7 green 4-lane runs x 75 phases: p95Ms is bimodal - the 95 % rank lands in or
	                           out of the level-load second - 34-37 false flags in 525 even with a 20-25 % floor; meanMs 3)
	      $ArxPerfInfoMetrics  the metrics shown for information only: p95Ms, maxMs, hitches, missed
	      $ArxPerfMinRuns      fewer baseline samples than this: 'no-baseline'
	      $ArxPerfRuns         the -Runs of the rolling baseline (10 in the contract's example)
	  With these defaults +30 % meanMs or medianMs on a phase of a tight baseline is flagged, a run within a few percent
	  is not, and a p95-only jump (meanMs unchanged) shows in the table but is not flagged (checked below).

	  Format-ArxPerfTable -Rows <Compare rows>
	      Markdown lines for the job summary: a heading line (#..# with "Performance"), then a table whose header row
	      starts with the columns | Phase | Metric | Baseline | This run | Delta | (more columns may follow), the
	      separator row right after it, one row per Compare row. The metric cell is the json name (p95Ms). Numbers with
	      two decimals in the invariant culture (11.20, never 11,20); the delta with its percentage, e.g. +3.36 (+30.0%);
	      a regressed row has the word "regressed" in it, no other row has; a 'no-baseline' row has "no baseline yet" in
	      its Baseline cell. No rows: a line that says there is no performance data, not an error.

	  Format-ArxPerfAnnotations -Rows <Compare rows>
	      One GitHub workflow command line per REGRESSED PHASE (not per metric): "::warning title=<..>::<text>" naming the
	      phase and, for each regressed metric of it, the metric, the baseline and this run's value (two decimals). No
	      regressed phase: nothing.

	scripts\dev\perf-track.ps1 - what CI runs after the game checks (from ci-smoke.ps1 or a workflow step):
	  -EvidenceDir <dir> -BaselineFile <path> -Lanes <n> -Kind <kind> [-Green] [-SummaryFile <path>] [-K] [-FloorPct]
	  [-MinRuns] [-Runs] [-Metrics <a,b>]  (a comma-separated -Metrics is split: pwsh -File hands a list over as one string)
	  Writes perf.json into EvidenceDir, compares with Get-ArxPerfBaseline of -Lanes (the -Metrics rows flagged, the
	  $ArxPerfInfoMetrics rows not in -Metrics added with -Info), appends Format-ArxPerfTable to
	  -SummaryFile (default $env:GITHUB_STEP_SUMMARY), prints the annotations, then Update-ArxPerfBaseline. ALWAYS exit 0:
	  a regression, a missing baseline, a missing or broken log or evidence folder never fails the run (the flag is not
	  blocking - making it blocking is the owner's call).

	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}
function Near($A, [double]$B, [double]$Tolerance = 0.005) {
	if ($null -eq $A -or "$A" -eq '') { return $false }
	try { [math]::Abs([double]$A - $B) -le $Tolerance } catch { $false }
}
# A section that throws (a function missing, a wrong signature) is one FAIL, and the next section still runs
function Section([string]$Name, [scriptblock]$Body) {
	Write-Host "==> $Name"
	try { . $Body } catch { Check $false "$Name threw: $($_.Exception.Message)" }
}
function PhaseOf($Run, [string]$Name) {
	if ($null -eq $Run -or $null -eq $Run.phases) { return $null }
	if ($Run.phases -is [Collections.IDictionary]) { $Run.phases[$Name] } else { $Run.phases.$Name }
}
function PhaseNames($Run) {
	if ($null -eq $Run -or $null -eq $Run.phases) { return @() }
	if ($Run.phases -is [Collections.IDictionary]) { @($Run.phases.Keys) } else { @($Run.phases.PSObject.Properties.Name) }
}
function RowOf($Rows, [string]$Phase, [string]$Metric) { @($Rows | Where-Object { $_.phase -eq $Phase -and $_.metric -eq $Metric }) | Select-Object -First 1 }
function BaseOf($Baseline, [string]$Phase, [string]$Metric) {
	if ($null -eq $Baseline -or @($Baseline.Keys) -notcontains $Phase) { return $null }
	$m = $Baseline[$Phase]
	if ($null -eq $m -or @($m.Keys) -notcontains $Metric) { return $null }
	$m[$Metric]
}
function FileHash([string]$Path) { if (Test-Path -LiteralPath $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { 'missing' } }

$c = [Globalization.CultureInfo]::InvariantCulture
$fixtures = Join-Path $PSScriptRoot 'fixtures\perf'
function Fixture([string]$Name) { Join-Path $fixtures $Name }
$lib = Join-Path $PSScriptRoot 'perf-track-lib.ps1'
$trackScript = Join-Path $PSScriptRoot 'perf-track.ps1'
$pwsh = (Get-Process -Id $PID).Path

Check (Test-Path -LiteralPath $lib) 'scripts/dev/perf-track-lib.ps1 exists'
Check (Test-Path -LiteralPath $trackScript) 'scripts/dev/perf-track.ps1 exists'
if (-not (Test-Path -LiteralPath $lib)) {
	Write-Host "test-perf-track: $($failures.Count) check(s) FAILED - nothing to test without the lib (the interface is in this file's header)" -ForegroundColor Red
	exit 1
}
. $lib

# A flat log of $Seconds equal seconds, 60 frames each (the end-to-end run's regressed phase)
function FlatLog([double]$Mean, [double]$P95, [double]$Max, [int]$Seconds = 11) {
	$lines = @('# arx-perf.log (#327): fixture of scripts/dev/test-perf-track.ps1')
	for ($t = 0; $t -lt $Seconds; $t++) {
		$lines += [string]::Format($c, 'S t={0} frames=60 min={1:0.00} mean={2:0.00} p95={3:0.00} max={4:0.00} hz=60.0 period=16.67 div=1 vr=0 missed=0 hitches=0 | other=0.10 input=0.05 channel=0.02 load=0.00 save=0.00 update=1.00 script=0.02 physics=0.10 particles=0.01 render=4.00 textures=0.00 present=0.50 limit={5:0.00} vrwait=0.00 vrend=0.00 | uploads=0 self=0.4 selfmax=3',
			$t, ($Mean - 0.4), $Mean, $P95, $Max, ($Mean - 5.8))
	}
	$lines + "# end: $(60 * $Seconds) frames"
}

# A run record as Compare-ArxPhasePerf gets it: the phases with every metric at the fixture baseline's typical value,
# overridden by $Values (phase -> hashtable of fields)
function RunOf([hashtable]$Values) {
	$phases = [ordered]@{}
	foreach ($name in ($Values.Keys | Sort-Object)) {
		$base = if ($name -eq 'smoke-test') {
			@{ meanMs = 16.65; medianMs = 16.65; p95Ms = 17.0; maxMs = 17.7; hitches = 0; missed = 0; peakPrivMiB = 412.3 }
		} else {
			@{ meanMs = 9.0; medianMs = 8.9; p95Ms = 11.2; maxMs = 14.0; hitches = 0; missed = 0; peakPrivMiB = 410.0 }
		}
		$rec = [ordered]@{ phase = $name; valid = $true; reason = ''; seconds = 10; frames = 600; busiestCause = $null; busiestCauseHitches = 0; causes = [ordered]@{}; ended = $true; unknownLines = 0 }
		foreach ($k in $base.Keys) { $rec[$k] = $base[$k] }
		foreach ($k in $Values[$name].Keys) { $rec[$k] = $Values[$name][$k] }
		$phases[$name] = [pscustomobject]$rec
	}
	[pscustomobject]@{ phases = $phases }
}

$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-perf-track-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
try {

# --- 1. Per-phase stats from one arx-perf.log -----------------------------------------------------------------------
Section 'per-phase stats: a normal log' {
	$s = Get-ArxPhasePerfStats -Path (Fixture 'normal.arx-perf.log') -Phase 'smoke-test'
	Check ($s.phase -eq 'smoke-test' -and $s.valid -eq $true) "the record names its phase and is valid ($($s.phase), $($s.valid))"
	Check ($s.seconds -eq 11 -and $s.frames -eq 660) "11 seconds, 660 frames ($($s.seconds), $($s.frames))"
	Check (Near $s.meanMs 16.65) "meanMs is the frames-weighted mean of the seconds' means: 16.65 ($($s.meanMs))"
	Check (Near $s.medianMs 16.65) "medianMs: 16.65 ($($s.medianMs))"
	Check (Near $s.p95Ms 17.00) "p95Ms: with 11 equal seconds the 95 % rank is the last one, 17.00 ($($s.p95Ms))"
	Check (Near $s.maxMs 17.70) "maxMs: the largest S max, 17.70 ($($s.maxMs))"
	Check ($s.hitches -eq 0 -and $s.missed -eq 0) "no hitches, no missed slots ($($s.hitches), $($s.missed))"
	Check ([string]::IsNullOrEmpty($s.busiestCause) -and $s.busiestCauseHitches -eq 0) "no hitch: no busiest cause ('$($s.busiestCause)', $($s.busiestCauseHitches))"
	Check (Near $s.peakPrivMiB 412.3 0.001) "peakPrivMiB: the largest R priv, 412.3 ($($s.peakPrivMiB))"
	Check ($s.ended -eq $true -and $s.unknownLines -eq 0) "ended, no unknown lines ($($s.ended), $($s.unknownLines))"
}

Section 'per-phase stats: one long frame' {
	$s = Get-ArxPhasePerfStats -Path (Fixture 'long-frame.arx-perf.log') -Phase 'xrs-long'
	Check ($s.valid -eq $true -and $s.seconds -eq 11 -and $s.frames -eq 945) "11 seconds, 945 frames ($($s.seconds), $($s.frames))"
	Check (Near $s.meanMs 11.639 0.002) "meanMs is weighted by frames: (90 x 111.10 + 45 x 22.22) / 945 = 11.639 ($($s.meanMs))"
	Check (Near $s.medianMs 11.11) "medianMs: the long frame's second does not move the median, 11.11 ($($s.medianMs))"
	Check (Near $s.p95Ms 11.36) "p95Ms: frames-weighted nearest rank - 900 of 945 frames reach 95 % at 11.36, not the long second's 11.40 ($($s.p95Ms))"
	Check (Near $s.maxMs 520.0) "maxMs: the long frame, 520.0 ($($s.maxMs))"
	Check ($s.hitches -eq 1 -and $s.missed -eq 46) "1 hitch, 46 missed slots ($($s.hitches), $($s.missed))"
	Check ($s.busiestCause -eq 'load' -and $s.busiestCauseHitches -eq 1) "the busiest cause: load, 1 ($($s.busiestCause), $($s.busiestCauseHitches))"
	Check ($null -eq $s.peakPrivMiB) "no R line: peakPrivMiB is null ($($s.peakPrivMiB))"
}

Section 'per-phase stats: hitches of several causes' {
	$s = Get-ArxPhasePerfStats -Path (Fixture 'causes.arx-perf.log') -Phase 'xrs-causes'
	Check ($s.valid -eq $true -and $s.seconds -eq 6 -and $s.frames -eq 468) "6 seconds, 468 frames ($($s.seconds), $($s.frames))"
	Check ($s.hitches -eq 9) "hitches: the number of H lines, 9 ($($s.hitches))"
	Check ($s.missed -eq 76) "missed: the sum of the S lines' missed, 76 ($($s.missed))"
	Check (Near $s.maxMs 300.0) "maxMs: 300.0 ($($s.maxMs))"
	Check ($s.causes.textures -eq 3 -and $s.causes.render -eq 3 -and $s.causes.load -eq 2 -and $s.causes.channel -eq 1) "causes: textures 3, render 3, load 2, channel 1 ($($s.causes.textures), $($s.causes.render), $($s.causes.load), $($s.causes.channel))"
	Check ($s.busiestCause -eq 'textures' -and $s.busiestCauseHitches -eq 3) "the busiest cause: most H lines, the tie with render (3) to the larger total ms (143 > 105) - textures, 3 ($($s.busiestCause), $($s.busiestCauseHitches))"
	Check (Near $s.peakPrivMiB 455.5 0.001) "peakPrivMiB: the R peak, not the last R line, 455.5 ($($s.peakPrivMiB))"
}

Section 'per-phase stats: a log cut mid-write' {
	$s = Get-ArxPhasePerfStats -Path (Fixture 'truncated.arx-perf.log') -Phase 'xrs-cut'
	Check ($s.valid -eq $true -and $s.seconds -eq 10 -and $s.frames -eq 600) "the whole seconds count, the half line does not ($($s.seconds), $($s.frames))"
	Check ($s.ended -eq $false -and $s.unknownLines -eq 1) "no end line, 1 unknown line ($($s.ended), $($s.unknownLines))"
	Check (Near $s.maxMs 17.70) "maxMs of the whole seconds, 17.70 ($($s.maxMs))"
}

Section 'per-phase stats: empty, broken, missing' {
	$e = Get-ArxPhasePerfStats -Path (Fixture 'empty.arx-perf.log') -Phase 'xrs-empty'
	Check ($e.phase -eq 'xrs-empty' -and $e.valid -eq $false -and $e.frames -eq 0 -and $null -eq $e.p95Ms -and $null -eq $e.meanMs) "an empty log: not valid, 0 frames, no p95 / mean ($($e.valid), $($e.frames), '$($e.p95Ms)')"
	Check (-not [string]::IsNullOrWhiteSpace($e.reason)) "an empty log says why ($($e.reason))"
	$b = Get-ArxPhasePerfStats -Path (Fixture 'broken.arx-perf.log') -Phase 'xrs-broken'
	Check ($b.valid -eq $false -and $b.frames -eq 0 -and $null -eq $b.maxMs -and $b.unknownLines -eq 4) "a broken log: not valid, 0 frames, 4 unknown lines ($($b.valid), $($b.frames), $($b.unknownLines))"
	Check (-not [string]::IsNullOrWhiteSpace($b.reason)) "a broken log says why ($($b.reason))"
	$m = Get-ArxPhasePerfStats -Path (Join-Path $root 'no-such-folder\arx-perf.log') -Phase 'xrs-missing'
	Check ($m.valid -eq $false -and $m.frames -eq 0 -and -not [string]::IsNullOrWhiteSpace($m.reason)) "a missing log: not valid, with a reason ($($m.reason))"
}

# --- 2. The run: every phase of an evidence folder, perf.json --------------------------------------------------------
Section 'the run record from an evidence folder' {
	$ev = Join-Path $root 'evidence-run'
	foreach ($d in 'smoke-test', 'xrs-long', 'xrs-nolog') { New-Item -ItemType Directory -Force (Join-Path $ev $d) | Out-Null }
	Copy-Item (Fixture 'normal.arx-perf.log') (Join-Path $ev 'smoke-test\arx-perf.log')
	# an earlier game of the same user directory: never this phase's numbers
	Copy-Item (Fixture 'causes.arx-perf.log') (Join-Path $ev 'smoke-test\arx-perf.1.log')
	Set-Content -LiteralPath (Join-Path $ev 'smoke-test\arx.log') -Value 'a game log'
	Copy-Item (Fixture 'long-frame.arx-perf.log') (Join-Path $ev 'xrs-long\arx-perf.log')
	Set-Content -LiteralPath (Join-Path $ev 'xrs-nolog\arx.log') -Value 'a game log'
	Set-Content -LiteralPath (Join-Path $ev 'smoke-test.log') -Value 'the phase transcript'

	$run = Get-ArxRunPerf -EvidenceDir $ev
	$names = @(PhaseNames $run)
	Check (($names -join ',') -eq 'smoke-test,xrs-long,xrs-nolog') "a record per phase folder, by name ($($names -join ','))"
	$st = PhaseOf $run 'smoke-test'
	Check ($st.valid -eq $true -and $st.hitches -eq 0 -and (Near $st.p95Ms 17.0)) "a phase's numbers come from arx-perf.log, never arx-perf.1.log (hitches $($st.hitches), p95 $($st.p95Ms))"
	Check ((Near (PhaseOf $run 'xrs-long').maxMs 520.0)) 'the second phase''s own log'
	Check ((PhaseOf $run 'xrs-nolog').valid -eq $false) 'a phase folder without arx-perf.log: a not-valid record'

	$written = Write-ArxRunPerf -EvidenceDir $ev -RunPerf $run -Kind 'pr' -Lanes 4 -Green:$true
	$file = Join-Path $ev 'perf.json'
	Check ((Test-Path -LiteralPath $file) -and "$written" -eq $file) "perf.json is written into the evidence folder and its path returned ($written)"
	$json = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
	Check ($json.schema -eq 1 -and $json.kind -eq 'pr' -and $json.lanes -eq 4 -and $json.green -eq $true -and $json.time) "perf.json: schema 1, kind, lanes, green, time ($($json.schema), $($json.kind), $($json.lanes), $($json.green))"
	$fields = 'phase', 'valid', 'reason', 'seconds', 'frames', 'meanMs', 'medianMs', 'p95Ms', 'maxMs', 'hitches', 'missed', 'causes', 'busiestCause', 'busiestCauseHitches', 'peakPrivMiB', 'ended', 'unknownLines'
	$have = @($json.phases.'xrs-long'.PSObject.Properties.Name)
	$lacking = @($fields | Where-Object { $_ -notin $have })
	Check ($lacking.Count -eq 0) "a phase record in perf.json has every field ($(if ($lacking) { 'missing: ' + ($lacking -join ', ') } else { 'all' }))"
	Check ((Near $json.phases.'smoke-test'.p95Ms 17.0) -and (Near $json.phases.'xrs-long'.maxMs 520.0) -and $json.phases.'xrs-long'.busiestCause -eq 'load' -and $json.phases.'xrs-nolog'.valid -eq $false) 'perf.json holds the numbers'
	$raw = Get-Content -LiteralPath $file -Raw
	Check ($raw -notmatch [regex]::Escape($root)) 'perf.json carries no machine path'

	$back = Read-ArxRunPerf -Path $file
	Check ($back.lanes -eq 4 -and $back.kind -eq 'pr' -and $back.phases -is [Collections.IDictionary]) "Read-ArxRunPerf: kind, lanes, the phases as a dictionary ($($back.kind), $($back.lanes))"
	Check ((Near (PhaseOf $back 'xrs-long').p95Ms 11.36) -and (PhaseOf $back 'xrs-nolog').valid -eq $false) 'Read-ArxRunPerf gives the records back'
	Check ($null -eq (Read-ArxRunPerf -Path (Join-Path $root 'missing.json'))) 'Read-ArxRunPerf of a missing file: $null'
	$none = Get-ArxRunPerf -EvidenceDir (Join-Path $root 'no-evidence')
	Check (@(PhaseNames $none).Count -eq 0) 'a missing evidence folder: no phases, no error'
}

# --- 3. The run's kind ------------------------------------------------------------------------------------------------
Section 'the kind of a CI run' {
	$cases = @(
		@('push', 'main', 'full', 'main'), @('push', 'refs/heads/main', 'full', 'main'),
		@('workflow_dispatch', 'ARXVR-552-ci-perf-tracking', 'full', 'gate'),
		@('workflow_dispatch', 'ARXVR-552-ci-perf-tracking', 'pr', 'pr'),
		@('pull_request', '565/merge', 'pr', 'pr'), @('pull_request', '565/merge', 'full', 'pr'),
		@('', '', 'full', 'local')
	)
	foreach ($k in $cases) {
		$got = Get-ArxCiRunKind -EventName $k[0] -RefName $k[1] -Suite $k[2]
		Check ($got -eq $k[3]) "event '$($k[0])', ref '$($k[1])', suite $($k[2]): $($k[3]) ($got)"
	}
}

# --- 4. The rolling baseline ----------------------------------------------------------------------------------------
Section 'the baseline file beside ci-history.json' {
	$p = Get-ArxPerfBaselinePath -DurationsFile 'C:\runner\ci-evidence\ci-durations.json'
	Check ($p -eq 'C:\runner\ci-evidence\perf-baseline.json') "perf-baseline.json beside the durations and the history ($p)"
}

Section 'the rolling baseline: median and spread of the last runs with the same lane count' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$p95 = BaseOf $b 'xrs-look-around' 'p95Ms'
	Check ($p95.samples -eq 10) "10 samples - the last 10 runs of lanes 4, the lanes-3 runs between them do not use up the window ($($p95.samples))"
	Check (Near $p95.median 11.2 0.0001) "median 11.2: the two old outliers (20.0, 21.0) are out of the window ($($p95.median))"
	Check (Near $p95.spread 0.1 0.0001) "spread: the median absolute deviation, 0.1 ($($p95.spread))"
	$mean = BaseOf $b 'xrs-look-around' 'meanMs'
	Check ((Near $mean.median 9.0 0.0001) -and (Near $mean.spread 0.0 0.0001)) "meanMs: 9.0, spread 0 ($($mean.median), $($mean.spread))"
	$smoke = BaseOf $b 'smoke-test' 'p95Ms'
	Check ($smoke.samples -eq 2 -and (Near $smoke.median 17.0 0.0001)) "a phase in 2 of the runs: 2 samples ($($smoke.samples))"
	$b3 = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 3 -Runs 10
	$p3 = BaseOf $b3 'xrs-look-around' 'p95Ms'
	Check ($p3.samples -eq 3 -and (Near $p3.median 9.0 0.0001) -and @($b3.Keys) -notcontains 'smoke-test') "lanes 3 is its own baseline: 3 samples, 9.0 ($($p3.samples), $($p3.median))"
	$short = BaseOf (Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 3) 'xrs-look-around' 'p95Ms'
	Check ($short.samples -eq 3 -and (Near $short.median 11.3 0.0001)) "-Runs 3: the last 3 runs only, median 11.3 ($($short.samples), $($short.median))"
	Check (@((Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 5 -Runs 10).Keys).Count -eq 0) 'no run with this lane count: an empty baseline'
	Check (@((Get-ArxPerfBaseline -Path (Join-Path $root 'missing-baseline.json') -Lanes 4 -Runs 10).Keys).Count -eq 0) 'a missing baseline file: an empty baseline, no error'
	$bad = Join-Path $root 'broken-baseline.json'
	Set-Content -LiteralPath $bad -Value '{ "schema": 1, "runs": [ { not json'
	Check (@((Get-ArxPerfBaseline -Path $bad -Lanes 4 -Runs 10).Keys).Count -eq 0) 'a broken baseline file: an empty baseline, no error'
}

Section 'the baseline update rules' {
	$bf = Join-Path $root 'update\perf-baseline.json'
	New-Item -ItemType Directory -Force (Split-Path -Parent $bf) | Out-Null
	Copy-Item (Fixture 'perf-baseline.json') $bf
	$run = RunOf @{ 'xrs-look-around' = @{ p95Ms = 30.0 }; 'xrs-broken' = @{ valid = $false; frames = 0; p95Ms = $null; meanMs = $null } }
	$before = FileHash $bf
	foreach ($case in @(@('pr', $true), @('pr', $false), @('local', $true), @('other', $true), @('main', $false), @('gate', $false))) {
		$r = Update-ArxPerfBaseline -Path $bf -RunPerf $run -Kind $case[0] -Lanes 4 -Green:$case[1]
		Check ($r.updated -eq $false -and (FileHash $bf) -eq $before) "kind $($case[0]), green $($case[1]): the baseline is untouched (updated=$($r.updated); $($r.why))"
	}
	$fresh = Join-Path $root 'update\none\perf-baseline.json'
	$null = Update-ArxPerfBaseline -Path $fresh -RunPerf $run -Kind 'pr' -Lanes 4 -Green:$true
	Check (-not (Test-Path -LiteralPath $fresh)) 'a pull request''s run never creates the baseline file either'

	$r = Update-ArxPerfBaseline -Path $bf -RunPerf $run -Kind 'main' -Lanes 4 -Green:$true -Keep 30
	$hist = Get-Content -LiteralPath $bf -Raw | ConvertFrom-Json
	$last = @($hist.runs)[-1]
	Check ($r.updated -eq $true -and @($hist.runs).Count -eq 16) "a green main run is appended (updated=$($r.updated), $(@($hist.runs).Count) runs)"
	Check ($last.kind -eq 'main' -and $last.lanes -eq 4 -and (Near $last.phases.'xrs-look-around'.p95Ms 30.0)) "the new run: kind main, lanes 4, its numbers ($($last.kind), $($last.lanes), $($last.phases.'xrs-look-around'.p95Ms))"
	Check ($null -eq $last.phases.'xrs-broken') 'a phase without valid numbers is not stored'
	$b4 = Get-ArxPerfBaseline -Path $bf -Lanes 4 -Runs 10
	$p = BaseOf $b4 'xrs-look-around' 'p95Ms'
	Check ($p.samples -eq 10 -and (Near $p.median 11.25 0.0001)) "rolling: the newest counts, the oldest of the 10 drops - median 11.25 ($($p.samples), $($p.median))"

	$gateRun = RunOf @{ 'xrs-look-around' = @{ p95Ms = 50.0 } }
	$r = Update-ArxPerfBaseline -Path $bf -RunPerf $gateRun -Kind 'gate' -Lanes 3 -Green:$true -Keep 30
	$p4 = BaseOf (Get-ArxPerfBaseline -Path $bf -Lanes 4 -Runs 10) 'xrs-look-around' 'p95Ms'
	$p3 = BaseOf (Get-ArxPerfBaseline -Path $bf -Lanes 3 -Runs 10) 'xrs-look-around' 'p95Ms'
	Check ($r.updated -eq $true -and $p3.samples -eq 4) "a green gate run updates its lane count's baseline (updated=$($r.updated), lanes 3: $($p3.samples) samples)"
	Check ($p4.samples -eq 10 -and (Near $p4.median 11.25 0.0001)) "... and leaves another lane count's alone ($($p4.samples), $($p4.median))"

	$r = Update-ArxPerfBaseline -Path $bf -RunPerf $run -Kind 'main' -Lanes 4 -Green:$true -Keep 12
	$hist = Get-Content -LiteralPath $bf -Raw | ConvertFrom-Json
	Check (@($hist.runs).Count -eq 12 -and @($hist.runs)[-1].kind -eq 'main' -and @($hist.runs)[0].time -ne '2026-10-01 10:00:00') "-Keep 12: the oldest runs go, the newest stays ($(@($hist.runs).Count) runs)"

	$r = Update-ArxPerfBaseline -Path $fresh -RunPerf $run -Kind 'gate' -Lanes 4 -Green:$true
	$hist = if (Test-Path -LiteralPath $fresh) { Get-Content -LiteralPath $fresh -Raw | ConvertFrom-Json }
	Check ($r.updated -eq $true -and $hist.schema -eq 1 -and @($hist.runs).Count -eq 1) 'the first green gate run creates the file (schema 1, one run)'

	$bad = Join-Path $root 'update\broken.json'
	Set-Content -LiteralPath $bad -Value 'garbage'
	$r = Update-ArxPerfBaseline -Path $bad -RunPerf $run -Kind 'main' -Lanes 4 -Green:$true
	$hist = Get-Content -LiteralPath $bad -Raw | ConvertFrom-Json
	Check ($r.updated -eq $true -and @($hist.runs).Count -eq 1) 'a broken baseline file is replaced by a good one'
}

# --- 5. The flag --------------------------------------------------------------------------------------------------------
Section 'the flag: noise is not flagged, a clear regression is' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$opts = @{ Metrics = @('p95Ms', 'meanMs'); K = 3; FloorPct = 5; MinRuns = 3 }

	$noise = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 11.4 } }) -Baseline $b @opts)
	Check ($noise.Count -eq 2) "one row per phase and metric ($($noise.Count))"
	Check (@($noise | Where-Object status -ne 'ok').Count -eq 0) "p95 11.4, the top of the baseline's own runs: not flagged ($(($noise | ForEach-Object { "$($_.metric) $($_.status)" }) -join ', '))"

	$reg = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 14.56 } }) -Baseline $b @opts)
	$row = RowOf $reg 'xrs-look-around' 'p95Ms'
	Check ($row.status -eq 'regressed') "p95 +30 % (11.2 -> 14.56): regressed ($($row.status))"
	Check ((Near $row.baseline 11.2) -and (Near $row.value 14.56) -and (Near $row.delta 3.36) -and (Near $row.deltaPct 30.0 0.05) -and $row.samples -eq 10) "the row's numbers: baseline 11.2, value 14.56, delta 3.36 (+30 %), 10 samples ($($row.baseline), $($row.value), $($row.delta), $($row.deltaPct), $($row.samples))"
	Check ((Near $row.limit 11.76) -and (Near $row.spread 0.1)) "limit = median + max(K x spread, median x FloorPct %) = 11.2 + max(0.3, 0.56) = 11.76 ($($row.limit))"
	Check ((RowOf $reg 'xrs-look-around' 'meanMs').status -eq 'ok') 'the metric that did not change stays ok'

	$mid = RunOf @{ 'xrs-look-around' = @{ p95Ms = 11.9 } }
	Check ((RowOf @(Compare-ArxPhasePerf -RunPerf $mid -Baseline $b @opts) 'xrs-look-around' 'p95Ms').status -eq 'regressed') '11.9 over the limit 11.76 (K 3, floor 5 %): regressed'
	$wideK = @{} + $opts; $wideK.K = 10
	Check ((RowOf @(Compare-ArxPhasePerf -RunPerf $mid -Baseline $b @wideK) 'xrs-look-around' 'p95Ms').status -eq 'ok') 'K 10: the limit 11.2 + 10 x 0.1 = 12.2 - 11.9 is ok (K is used)'
	$wideFloor = @{} + $opts; $wideFloor.FloorPct = 10
	Check ((RowOf @(Compare-ArxPhasePerf -RunPerf $mid -Baseline $b @wideFloor) 'xrs-look-around' 'p95Ms').status -eq 'ok') 'floor 10 %: the limit 12.32 - 11.9 is ok (the floor is used)'

	$fast = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 8.0; meanMs = 6.0 } }) -Baseline $b @opts)
	Check (@($fast | Where-Object status -ne 'ok').Count -eq 0) 'a faster phase is never flagged'
}

Section 'the flag: no baseline yet is not a failure' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$opts = @{ Metrics = @('p95Ms', 'meanMs'); K = 3; FloorPct = 5; MinRuns = 3 }
	$rows = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-new' = @{ p95Ms = 99.0 }; 'smoke-test' = @{ p95Ms = 40.0 } }) -Baseline $b @opts)
	Check ((RowOf $rows 'xrs-new' 'p95Ms').status -eq 'no-baseline' -and $null -eq (RowOf $rows 'xrs-new' 'p95Ms').baseline) "a phase the baseline does not know: no-baseline ($((RowOf $rows 'xrs-new' 'p95Ms').status))"
	Check ((RowOf $rows 'smoke-test' 'p95Ms').status -eq 'no-baseline') "2 samples under -MinRuns 3: no-baseline, however slow ($((RowOf $rows 'smoke-test' 'p95Ms').status))"
	$two = @{} + $opts; $two.MinRuns = 2
	Check ((RowOf @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'smoke-test' = @{ p95Ms = 40.0 } }) -Baseline $b @two) 'smoke-test' 'p95Ms').status -eq 'regressed') '-MinRuns 2: the same 2 samples are a baseline'
	Check (@($rows | Where-Object phase -eq 'xrs-look-around').Count -eq 0) 'a phase of the baseline that this run did not run (a pull request''s selection): no row'
	$empty = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 99.0 } }) -Baseline ([ordered]@{}) @opts)
	Check ($empty.Count -eq 2 -and @($empty | Where-Object status -ne 'no-baseline').Count -eq 0) 'an empty baseline: every row no-baseline'
	$null1 = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 99.0 } }) -Baseline $null @opts)
	Check ($null1.Count -eq 2 -and @($null1 | Where-Object status -ne 'no-baseline').Count -eq 0) 'no baseline at all ($null): every row no-baseline'
	$invalid = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ valid = $false; frames = 0; p95Ms = $null; meanMs = $null } }) -Baseline $b @opts)
	Check ($invalid.Count -ge 1 -and @($invalid | Where-Object status -ne 'no-data').Count -eq 0) "a phase without valid numbers in this run: no-data, never regressed ($(($invalid | ForEach-Object status) -join ', '))"
}

Section 'the flag: a run read back from perf.json compares the same' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$dir = Join-Path $root 'roundtrip'
	New-Item -ItemType Directory -Force $dir | Out-Null
	$file = Write-ArxRunPerf -EvidenceDir $dir -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 14.56 } }) -Kind 'pr' -Lanes 4 -Green:$true
	$back = Read-ArxRunPerf -Path (Join-Path $dir 'perf.json')
	$rows = @(Compare-ArxPhasePerf -RunPerf $back -Baseline $b -Metrics 'p95Ms' -K 3 -FloorPct 5 -MinRuns 3)
	Check ((RowOf $rows 'xrs-look-around' 'p95Ms').status -eq 'regressed') 'the regression is found in the json round trip too'
}

Section 'the flag: the defaults (set from the measured noise)' {
	foreach ($v in 'ArxPerfK', 'ArxPerfFloorPct', 'ArxPerfMetrics', 'ArxPerfInfoMetrics', 'ArxPerfMinRuns', 'ArxPerfRuns') {
		Check ($null -ne (Get-Variable -Name $v -ValueOnly -ErrorAction SilentlyContinue)) "`$$v is set by the lib"
	}
	Check (@($ArxPerfMetrics) -contains 'meanMs' -and @($ArxPerfMetrics) -contains 'medianMs' -and @($ArxPerfMetrics) -notcontains 'p95Ms') "the default flagged metrics are meanMs and medianMs, not p95Ms ($(@($ArxPerfMetrics) -join ', '))"
	$info = @($ArxPerfInfoMetrics)
	Check ($info -contains 'p95Ms' -and $info -contains 'maxMs' -and $info -contains 'hitches' -and $info -contains 'missed') "p95Ms, maxMs, hitches and missed are shown for information ($($info -join ', '))"
	Check ($ArxPerfK -gt 0 -and $ArxPerfFloorPct -gt 0) "K and the floor are positive ($ArxPerfK, $ArxPerfFloorPct %)"
	Check ($ArxPerfMinRuns -ge 1 -and $ArxPerfMinRuns -le 10 -and $ArxPerfRuns -ge $ArxPerfMinRuns) "MinRuns 1..10 and at most Runs ($ArxPerfMinRuns, $ArxPerfRuns)"
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$noise = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 11.4; meanMs = 9.1; medianMs = 9.0 } }) -Baseline $b)
	Check (@($noise | Where-Object status -eq 'regressed').Count -eq 0) "with the defaults, a run within 1-2 % of the baseline (mean 9.1, median 9.0, p95 11.4) is not flagged ($(($noise | ForEach-Object { "$($_.metric) $($_.status)" }) -join ', '))"
	$reg = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ meanMs = 11.7 } }) -Baseline $b)
	Check ((RowOf $reg 'xrs-look-around' 'meanMs').status -eq 'regressed') "with the defaults, +30 % meanMs (9.0 -> 11.7) on a phase is flagged ($((RowOf $reg 'xrs-look-around' 'meanMs').status))"
	$regMedian = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ medianMs = 11.57 } }) -Baseline $b)
	Check ((RowOf $regMedian 'xrs-look-around' 'medianMs').status -eq 'regressed') "with the defaults, +30 % medianMs (8.9 -> 11.57) on a phase is flagged ($((RowOf $regMedian 'xrs-look-around' 'medianMs').status))"

	# A p95-only jump (+30 %, meanMs and medianMs unchanged): in the table for information, not flagged
	$jump = RunOf @{ 'xrs-look-around' = @{ p95Ms = 14.56 } }
	$p95Only = @(@(Compare-ArxPhasePerf -RunPerf $jump -Baseline $b) + @(Compare-ArxPhasePerf -RunPerf $jump -Baseline $b -Metrics $ArxPerfInfoMetrics -Info))
	$row = RowOf $p95Only 'xrs-look-around' 'p95Ms'
	Check ($null -ne $row -and $row.status -eq 'info' -and (Near $row.value 14.56) -and (Near $row.baseline 11.2)) "with the defaults, a p95-only jump is an info row with its numbers (status '$($row.status)', $($row.baseline) -> $($row.value))"
	Check (@($p95Only | Where-Object status -eq 'regressed').Count -eq 0) "... and nothing is regressed ($(($p95Only | ForEach-Object { "$($_.metric) $($_.status)" }) -join ', '))"
	Check (@(Format-ArxPerfAnnotations -Rows $p95Only).Count -eq 0) '... no annotation'
	$table = @(Format-ArxPerfTable -Rows $p95Only)
	$p95Line = @($table | Where-Object { $_ -match '\|\s*xrs-look-around\s*\|\s*p95Ms\s*\|' })
	Check ($p95Line.Count -eq 1 -and $p95Line[0] -match '11\.20' -and $p95Line[0] -match '14\.56' -and $p95Line[0] -notmatch 'regressed') "... the table shows the p95Ms row with 11.20 and 14.56, not regressed ($($p95Line -join ''))"
}

# --- 6. The summary table and the annotations ------------------------------------------------------------------------
Section 'the summary table and the annotations' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$run = RunOf @{
		'xrs-look-around' = @{ p95Ms = 14.56; meanMs = 12.0 }   # two metrics regressed: one annotation
		'smoke-test'      = @{ p95Ms = 22.1 }                   # +30 % against its 2 samples (MinRuns 2)
		'xrs-new'         = @{ p95Ms = 11.0 }                   # no baseline yet
	}
	$rows = @(Compare-ArxPhasePerf -RunPerf $run -Baseline $b -Metrics 'p95Ms', 'meanMs' -K 3 -FloorPct 5 -MinRuns 2)
	$lines = @(Format-ArxPerfTable -Rows $rows)
	$text = $lines -join "`n"
	Check (@($lines | Where-Object { $_ -match '^#{1,6} .*Performance' }).Count -ge 1) 'a heading with "Performance"'
	$h = -1
	for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match '^\|\s*Phase\s*\|\s*Metric\s*\|\s*Baseline\s*\|\s*This run\s*\|\s*Delta\s*\|') { $h = $i; break } }
	Check ($h -ge 0) 'the header row: | Phase | Metric | Baseline | This run | Delta |'
	Check ($h -ge 0 -and $h + 1 -lt $lines.Count -and $lines[$h + 1] -match '^\|(\s*:?-{3,}:?\s*\|)+\s*$') 'the separator row right after it'
	$data = @(if ($h -ge 0) { $lines[($h + 2)..($lines.Count - 1)] | Where-Object { $_ -match '^\|' } })
	Check ($data.Count -eq 6) "one row per phase and metric: 6 ($($data.Count))"
	$lp = @($data | Where-Object { $_ -match '\|\s*xrs-look-around\s*\|\s*p95Ms\s*\|' })
	Check ($lp.Count -eq 1 -and $lp[0] -match '11\.20' -and $lp[0] -match '14\.56' -and $lp[0] -match '\+3\.36' -and $lp[0] -match '\+30\.0\s*%' -and $lp[0] -match 'regressed') "the regressed row: 11.20, 14.56, +3.36 (+30.0%), regressed ($($lp -join ''))"
	$sm = @($data | Where-Object { $_ -match '\|\s*smoke-test\s*\|\s*meanMs\s*\|' })
	Check ($sm.Count -eq 1 -and $sm[0] -notmatch 'regressed' -and $sm[0] -match '16\.65') "an ok row is not marked regressed ($($sm -join ''))"
	$new = @($data | Where-Object { $_ -match '\|\s*xrs-new\s*\|' })
	Check ($new.Count -eq 2 -and @($new | Where-Object { $_ -match '\|\s*p95Ms\s*\|\s*no baseline yet\s*\|' }).Count -eq 1 -and @($new | Where-Object { $_ -match 'regressed' }).Count -eq 0) "a phase without a baseline: 'no baseline yet' in the Baseline cell ($($new -join ' / '))"
	Check ($text -notmatch '\d,\d\d\b') 'numbers in the invariant culture (no 11,20)'

	$ann = @(Format-ArxPerfAnnotations -Rows $rows)
	Check ($ann.Count -eq 2) "one annotation per regressed phase - xrs-look-around (2 metrics) and smoke-test: 2 ($($ann.Count))"
	Check (@($ann | Where-Object { $_ -notmatch '^::warning( [^:]*)?::' -or $_ -match "`n" }).Count -eq 0) 'each is one ::warning workflow command line'
	$la = @($ann | Where-Object { $_ -match 'xrs-look-around' })
	Check ($la.Count -eq 1 -and $la[0] -match 'p95Ms' -and $la[0] -match '11\.20' -and $la[0] -match '14\.56' -and $la[0] -match 'meanMs' -and $la[0] -match '9\.00' -and $la[0] -match '12\.00') "the phase's annotation names each regressed metric with baseline and value ($($la -join ''))"
	$st = @($ann | Where-Object { $_ -match 'smoke-test' })
	Check ($st.Count -eq 1 -and $st[0] -match '17\.00' -and $st[0] -match '22\.10') "smoke-test's annotation ($($st -join ''))"
	$okRows = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ p95Ms = 11.3 } }) -Baseline $b -Metrics 'p95Ms' -K 3 -FloorPct 5 -MinRuns 3)
	Check (@(Format-ArxPerfAnnotations -Rows $okRows).Count -eq 0) 'nothing regressed: no annotation'
	$noRows = @(Format-ArxPerfTable -Rows @())
	Check ($noRows.Count -ge 1 -and @($noRows | Where-Object { $_ -match '^\|' }).Count -eq 0) "no rows: a line that says so, no table ($($noRows -join ' / '))"
}

# --- 7. perf-track.ps1: what CI runs; never fails the run ----------------------------------------------------------
Section 'perf-track.ps1 on a pull request: table, annotation, exit 0, baseline untouched' {
	$ev = Join-Path $root 'track\evidence'
	foreach ($d in 'xrs-look-around', 'smoke-test', 'xrs-broken') { New-Item -ItemType Directory -Force (Join-Path $ev $d) | Out-Null }
	[IO.File]::WriteAllLines((Join-Path $ev 'xrs-look-around\arx-perf.log'), [string[]](FlatLog 12.0 14.56 15.0), [Text.UTF8Encoding]::new($false))
	Copy-Item (Fixture 'normal.arx-perf.log') (Join-Path $ev 'smoke-test\arx-perf.log')
	Copy-Item (Fixture 'broken.arx-perf.log') (Join-Path $ev 'xrs-broken\arx-perf.log')
	$bf = Join-Path $root 'track\perf-baseline.json'
	Copy-Item (Fixture 'perf-baseline.json') $bf
	$sum = Join-Path $root 'track\summary.md'
	Set-Content -LiteralPath $sum -Value '**Game checks:** an earlier line of the job summary'
	$before = FileHash $bf
	$common = @('-EvidenceDir', $ev, '-BaselineFile', $bf, '-Lanes', '4', '-SummaryFile', $sum, '-K', '3', '-FloorPct', '5', '-MinRuns', '3', '-Runs', '10', '-Metrics', 'p95Ms,meanMs')

	$out = @(& $pwsh -NoProfile -File $trackScript @common -Kind 'pr' 2>&1 | ForEach-Object { "$_" })
	$exit = $LASTEXITCODE
	Check ($exit -eq 0) "exit 0 with a regressed phase - the flag never fails the run ($exit)"
	$warn = @($out | Where-Object { $_ -match '^::warning' })
	Check ($warn.Count -eq 1 -and $warn[0] -match 'xrs-look-around' -and $warn[0] -match '14\.56') "one ::warning, for xrs-look-around ($($warn -join ' / '))"
	$s = if (Test-Path -LiteralPath $sum) { Get-Content -LiteralPath $sum -Raw } else { '' }
	Check ($s -match 'an earlier line of the job summary') 'the summary is appended to, not overwritten'
	Check ($s -match '\|\s*Phase\s*\|\s*Metric\s*\|\s*Baseline\s*\|\s*This run\s*\|\s*Delta\s*\|' -and $s -match '\|\s*xrs-look-around\s*\|\s*p95Ms\s*\|[^\r\n]*regressed') 'the job summary has the Performance table with the regressed row'
	Check ($s -match 'no baseline yet') 'smoke-test (2 samples, -MinRuns 3) says no baseline yet'
	Check ((Test-Path -LiteralPath (Join-Path $ev 'perf.json')) -and @((Get-Content -LiteralPath (Join-Path $ev 'perf.json') -Raw | ConvertFrom-Json).phases.PSObject.Properties.Name).Count -eq 3) 'perf.json is in the evidence, with the 3 phases'
	Check ((FileHash $bf) -eq $before) 'a pull request''s run leaves the baseline byte for byte'

	$out = @(& $pwsh -NoProfile -File $trackScript @common -Kind 'main' 2>&1 | ForEach-Object { "$_" })
	Check ($LASTEXITCODE -eq 0 -and (FileHash $bf) -eq $before) "a red main run (no -Green): exit 0, the baseline untouched ($LASTEXITCODE)"

	$out = @(& $pwsh -NoProfile -File $trackScript @common -Kind 'main' -Green 2>&1 | ForEach-Object { "$_" })
	$hist = Get-Content -LiteralPath $bf -Raw | ConvertFrom-Json
	$last = @($hist.runs)[-1]
	Check ($LASTEXITCODE -eq 0 -and @($hist.runs).Count -eq 16) "a green main run: exit 0, appended to the baseline ($(@($hist.runs).Count) runs)"
	Check ($last.kind -eq 'main' -and $last.lanes -eq 4 -and (Near $last.phases.'xrs-look-around'.p95Ms 14.56) -and $null -ne $last.phases.'smoke-test' -and $null -eq $last.phases.'xrs-broken') 'the stored run: kind main, lanes 4, its valid phases only'
}

Section 'perf-track.ps1 without a baseline, without evidence, with the summary from the environment' {
	$ev = Join-Path $root 'track\evidence'   # from the section above
	$missing = Join-Path $root 'track2\perf-baseline.json'
	$sum = Join-Path $root 'track2\summary.md'
	New-Item -ItemType Directory -Force (Split-Path -Parent $sum) | Out-Null
	$out = @(& $pwsh -NoProfile -File $trackScript -EvidenceDir $ev -BaselineFile $missing -Lanes 4 -Kind 'pr' -SummaryFile $sum -K 3 -FloorPct 5 -MinRuns 3 -Runs 10 -Metrics 'p95Ms,meanMs' 2>&1 | ForEach-Object { "$_" })
	$s = if (Test-Path -LiteralPath $sum) { Get-Content -LiteralPath $sum -Raw } else { '' }
	Check ($LASTEXITCODE -eq 0 -and $s -match 'no baseline yet' -and @($out | Where-Object { $_ -match '^::warning' }).Count -eq 0) "no baseline file: exit 0, 'no baseline yet', no warning ($LASTEXITCODE)"
	Check (-not (Test-Path -LiteralPath $missing)) 'a pull request''s run does not create the baseline file'

	& $pwsh -NoProfile -File $trackScript -EvidenceDir (Join-Path $root 'no-such-evidence') -BaselineFile $missing -Lanes 4 -Kind 'pr' -SummaryFile $sum *> $null
	Check ($LASTEXITCODE -eq 0) "no evidence folder at all: exit 0 ($LASTEXITCODE)"

	$envSum = Join-Path $root 'track2\step-summary.md'
	# a copy: a broken script must never write into the committed fixture
	$copy = Join-Path $root 'track2\perf-baseline.json'
	Copy-Item (Fixture 'perf-baseline.json') $copy
	$old = $env:GITHUB_STEP_SUMMARY
	$env:GITHUB_STEP_SUMMARY = $envSum
	try {
		& $pwsh -NoProfile -File $trackScript -EvidenceDir $ev -BaselineFile $copy -Lanes 4 -Kind 'pr' -K 3 -FloorPct 5 -MinRuns 3 -Runs 10 -Metrics 'p95Ms,meanMs' *> $null
	} finally {
		$env:GITHUB_STEP_SUMMARY = $old
	}
	$s = if (Test-Path -LiteralPath $envSum) { Get-Content -LiteralPath $envSum -Raw } else { '' }
	Check ($s -match '\|\s*Phase\s*\|\s*Metric\s*\|\s*Baseline\s*\|\s*This run\s*\|\s*Delta\s*\|') 'without -SummaryFile the table goes to $env:GITHUB_STEP_SUMMARY'
}

Section 'perf-track.ps1 with its defaults: a p95-only jump is shown, not flagged' {
	$ev = Join-Path $root 'track3\evidence'
	New-Item -ItemType Directory -Force (Join-Path $ev 'xrs-look-around') | Out-Null
	# meanMs / medianMs 9.0 (the baseline's 9.0 / 8.9), p95 14.56 (+30 % on 11.2)
	[IO.File]::WriteAllLines((Join-Path $ev 'xrs-look-around\arx-perf.log'), [string[]](FlatLog 9.0 14.56 15.0), [Text.UTF8Encoding]::new($false))
	$bf = Join-Path $root 'track3\perf-baseline.json'
	Copy-Item (Fixture 'perf-baseline.json') $bf
	$sum = Join-Path $root 'track3\summary.md'
	$out = @(& $pwsh -NoProfile -File $trackScript -EvidenceDir $ev -BaselineFile $bf -Lanes 4 -Kind 'pr' -SummaryFile $sum 2>&1 | ForEach-Object { "$_" })
	$exit = $LASTEXITCODE
	$s = if (Test-Path -LiteralPath $sum) { Get-Content -LiteralPath $sum -Raw } else { '' }
	$p95Line = @($s -split "`r?`n" | Where-Object { $_ -match '\|\s*xrs-look-around\s*\|\s*p95Ms\s*\|' })
	Check ($exit -eq 0 -and $p95Line.Count -eq 1 -and $p95Line[0] -match '11\.20' -and $p95Line[0] -match '14\.56') "exit 0, the table shows p95Ms 11.20 -> 14.56 for information ($exit; $($p95Line -join ''))"
	Check ($s -notmatch 'regressed' -and @($out | Where-Object { $_ -match '^::warning' }).Count -eq 0) 'nothing is flagged: no regressed row, no ::warning'
	Check (@($s -split "`r?`n" | Where-Object { $_ -match '\|\s*xrs-look-around\s*\|\s*meanMs\s*\|' }).Count -eq 1) 'the flagged metric meanMs has its row too'
}

# worker's checks (#552): the coordinator's decisions on the measured noise - a phase of fewer than $ArxPerfMinSeconds whole
# seconds is shown, never flagged; a count metric has an absolute floor ($ArxPerfAbsFloor), so a median of 0 does not flag
# every single hitch
Section 'worker: a phase too short to judge is shown, never flagged' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	Check ($ArxPerfMinSeconds -eq 3) "`$ArxPerfMinSeconds is 3 ($ArxPerfMinSeconds)"
	$short = RunOf @{ 'xrs-look-around' = @{ seconds = 2; meanMs = 12.0; medianMs = 12.0 } }
	$rows = @(Compare-ArxPhasePerf -RunPerf $short -Baseline $b -Metrics 'meanMs', 'medianMs' -K 4 -FloorPct 10 -MinRuns 5)
	Check ($rows.Count -eq 2 -and @($rows | Where-Object { $_.status -ne 'info' -or $_.note -ne 'too short' }).Count -eq 0) "2 seconds, meanMs +33 %: status info, note 'too short' ($(($rows | ForEach-Object { "$($_.status)/$($_.note)" }) -join ', '))"
	Check ((Near (RowOf $rows 'xrs-look-around' 'meanMs').baseline 9.0) -and (Near (RowOf $rows 'xrs-look-around' 'meanMs').value 12.0)) 'the short phase still shows its baseline and value'
	Check (@(Format-ArxPerfAnnotations -Rows $rows).Count -eq 0) 'no annotation for a short phase'
	$table = (Format-ArxPerfTable -Rows $rows) -join "`n"
	Check ($table -match 'too short' -and $table -notmatch 'regressed') 'the table says too short, nothing regressed'
	$three = @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ seconds = 3; meanMs = 12.0 } }) -Baseline $b -Metrics 'meanMs' -K 4 -FloorPct 10 -MinRuns 5)
	Check ($three[0].status -eq 'regressed') "3 seconds is long enough: regressed ($($three[0].status))"
	$zero = @(Compare-ArxPhasePerf -RunPerf $short -Baseline $b -Metrics 'meanMs' -K 4 -FloorPct 10 -MinRuns 5 -MinSeconds 0)
	Check ($zero[0].status -eq 'regressed') "-MinSeconds 0 judges every phase: regressed ($($zero[0].status))"
}

Section 'worker: a count metric has an absolute floor' {
	$b = Get-ArxPerfBaseline -Path (Fixture 'perf-baseline.json') -Lanes 4 -Runs 10
	$h = BaseOf $b 'xrs-look-around' 'hitches'
	Check ((Near $h.median 0.0) -and (Near $h.spread 0.0)) "the fixture's hitches: median 0, spread 0 ($($h.median), $($h.spread))"
	Check ($ArxPerfAbsFloor.hitches -ge 1 -and $ArxPerfAbsFloor.missed -ge 1) "absolute floors for hitches and missed ($($ArxPerfAbsFloor.hitches), $($ArxPerfAbsFloor.missed))"
	$opts = @{ Metrics = @('hitches'); K = 4; FloorPct = 10; MinRuns = 5; AbsFloor = @{ hitches = 3 } }
	$at = RowOf @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ hitches = 3 } }) -Baseline $b @opts) 'xrs-look-around' 'hitches'
	Check ($at.status -eq 'ok' -and (Near $at.limit 3.0)) "3 hitches on a median of 0 with the floor 3: ok, limit 3 ($($at.status), $($at.limit))"
	$over = RowOf @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ hitches = 4 } }) -Baseline $b @opts) 'xrs-look-around' 'hitches'
	Check ($over.status -eq 'regressed' -and $null -eq $over.deltaPct) "4 hitches: regressed, no percentage of a median 0 ($($over.status), '$($over.deltaPct)')"
	$noFloor = @{} + $opts; $noFloor.AbsFloor = @{}
	$one = RowOf @(Compare-ArxPhasePerf -RunPerf (RunOf @{ 'xrs-look-around' = @{ hitches = 1 } }) -Baseline $b @noFloor) 'xrs-look-around' 'hitches'
	Check ($one.status -eq 'regressed') "without the floor a single hitch on a median of 0 would be flagged ($($one.status))"
	$table = (Format-ArxPerfTable -Rows @($over)) -join "`n"
	Check ($table -match '\|\s*xrs-look-around\s*\|\s*hitches\s*\|[^\r\n]*\+4\.00\s*\|' -and $table -notmatch '\d,\d\d\b') "a delta without a percentage: +4.00 ($(@($table -split "`n" | Where-Object { $_ -match 'hitches' }) -join ''))"
}

} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-perf-track: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-perf-track: all checks passed' -ForegroundColor Green
exit 0
