<#
.SYNOPSIS
	CI performance tracking (#552): the per-phase numbers of a ci-smoke run against the rolling baseline - a Performance
	table in the job summary, a warning per slower phase, the baseline fed by green main and gate runs. Never fails.

.DESCRIPTION
	ci-smoke.ps1 runs this at its end, after the game phases. From every phase folder of -EvidenceDir it reads
	arx-perf.log (perf-track-lib.ps1: frames, mean / median / p95 / max frame ms, hitches, missed slots, the busiest
	hitch cause, peak private memory), writes them into -EvidenceDir\perf.json, compares each phase with the baseline of
	the last green main and gate runs with the same lane count (-BaselineFile, Get-ArxPerfBaseline) and appends the table
	to the job summary; a phase over the limit gets a ::warning line. Then a green main or gate run is appended to the
	baseline; a pull request's run, a red run and a run by hand never change it.

	Exit code: always 0. A slower phase, a missing baseline, a missing or broken log or evidence folder never fail the
	run - the flag is not blocking (making it blocking is the owner's call). docs/vr/TESTING.md, "Performance tracking
	in CI"; tested by test-perf-track.ps1.

.PARAMETER EvidenceDir
	The ci-smoke -OutDir: one folder per phase with its arx-perf.log.

.PARAMETER BaselineFile
	The baseline history (on the runner: perf-baseline.json in the ci-evidence root, beside ci-history.json).

.PARAMETER Lanes
	The run's nominal lane count (CI's ARXVR_MAX_GAMES): a baseline compares like with like.

.PARAMETER Kind
	main, gate, pr, local or other (Get-ArxCiRunKind). Only main and gate feed the baseline.

.PARAMETER Green
	Every game phase passed: only then does a main or gate run feed the baseline.

.PARAMETER SummaryFile
	Where the table is appended (default $env:GITHUB_STEP_SUMMARY; none: only printed).

.PARAMETER Metrics
	The metrics flagged (default $ArxPerfMetrics: meanMs, medianMs); a comma-separated string is split (pwsh -File hands
	a list over as one string). -K, -FloorPct, -MinRuns and -Runs default to the lib's values, set from the measured noise.

.PARAMETER InfoMetrics
	The metrics shown in the table against their baseline but never flagged (default $ArxPerfInfoMetrics: p95Ms, maxMs,
	hitches, missed - too noisy on the short CI phases to flag); an empty string shows none.

.EXAMPLE
	.\scripts\dev\perf-track.ps1 -EvidenceDir $env:TEMP\ci-evidence -BaselineFile $env:TEMP\perf-baseline.json -Lanes 4 -Kind local
#>
[CmdletBinding()]
param(
	[string]$EvidenceDir,
	[string]$BaselineFile,
	[int]$Lanes,
	[string]$Kind = 'local',
	[switch]$Green,
	[string]$SummaryFile = $env:GITHUB_STEP_SUMMARY,
	[double]$K = -1,
	[double]$FloorPct = -1,
	[int]$MinRuns = -1,
	[int]$Runs = -1,
	[string[]]$Metrics,
	[string[]]$InfoMetrics
)

try {
	. (Join-Path $PSScriptRoot 'perf-track-lib.ps1')
	$opts = @{}
	if ($K -ge 0) { $opts.K = $K }
	if ($FloorPct -ge 0) { $opts.FloorPct = $FloorPct }
	if ($MinRuns -ge 0) { $opts.MinRuns = $MinRuns }
	$Metrics = @($Metrics | ForEach-Object { $_ -split ',' } | ForEach-Object Trim | Where-Object { $_ })
	if ($Metrics) { $opts.Metrics = $Metrics }
	$InfoMetrics = if ($PSBoundParameters.ContainsKey('InfoMetrics')) { @($InfoMetrics | ForEach-Object { $_ -split ',' } | ForEach-Object Trim | Where-Object { $_ }) } else { $ArxPerfInfoMetrics }
	# A metric flagged is not shown a second time
	$InfoMetrics = @($InfoMetrics | Where-Object { $_ -notin $(if ($Metrics) { $Metrics } else { $ArxPerfMetrics }) })
	if ($Runs -lt 1) { $Runs = $ArxPerfRuns }

	$run = Get-ArxRunPerf -EvidenceDir $EvidenceDir
	$names = @(Get-ArxPerfPhaseNames $run)
	$valid = @($names | Where-Object { (Get-ArxPerfPhase $run $_).valid })
	if ($EvidenceDir -and (Test-Path -LiteralPath $EvidenceDir -PathType Container)) {
		$null = Write-ArxRunPerf -EvidenceDir $EvidenceDir -RunPerf $run -Kind $Kind -Lanes $Lanes -Green:$Green
	}
	$baseline = Get-ArxPerfBaseline -Path $BaselineFile -Lanes $Lanes -Runs $Runs
	# The flagged metrics, then the ones only shown, together per phase in the run's order
	$flagged = @(Compare-ArxPhasePerf -RunPerf $run -Baseline $baseline @opts)
	$infoOpts = @{} + $opts
	$infoOpts.Metrics = $InfoMetrics
	$shown = @(if ($InfoMetrics) { Compare-ArxPhasePerf -RunPerf $run -Baseline $baseline @infoOpts -Info })
	$rows = @(foreach ($name in $names) { $flagged + $shown | Where-Object phase -eq $name })
	$regressed = @($rows | Where-Object status -eq 'regressed' | ForEach-Object phase | Select-Object -Unique)
	Write-Host "perf-track: $($names.Count) phase(s), $($valid.Count) with numbers; baseline: $($baseline.Count) phase(s) of the last $Runs green main/gate runs on $Lanes lane(s); $(if ($regressed) { "SLOWER: $($regressed -join ', ')" } else { 'none slower' }) (not blocking)"

	$table = @(Format-ArxPerfTable -Rows $rows)
	if ($SummaryFile) {
		try { Add-Content -LiteralPath $SummaryFile -Value $table -Encoding utf8 } catch { Write-Host "perf-track: could not write the summary ($($_.Exception.Message))" }
	} else {
		foreach ($r in @($rows | Where-Object status -eq 'regressed')) { Write-Host "  regressed: $($r.phase) $($r.metric) $(Format-ArxPerfNumber $r.baseline) -> $(Format-ArxPerfNumber $r.value) (limit $(Format-ArxPerfNumber $r.limit))" }
	}
	foreach ($line in @(Format-ArxPerfAnnotations -Rows $rows)) { Write-Host $line }

	$update = Update-ArxPerfBaseline -Path $BaselineFile -RunPerf $run -Kind $Kind -Lanes $Lanes -Green:$Green
	Write-Host "perf-track: baseline $(if ($update.updated) { 'updated' } else { 'unchanged' }) - $($update.why)"
} catch {
	# Never fails the run: the numbers are a report, not a gate
	Write-Host "perf-track: no performance report ($($_.Exception.Message))"
}
exit 0
