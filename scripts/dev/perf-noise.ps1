<#
.SYNOPSIS
	How much a green CI run's per-phase performance numbers vary (#552): the noise the flag's thresholds come from.

.DESCRIPTION
	Reads the per-phase numbers (perf-track-lib.ps1) of several green runs with the same lane count - their perf.json, or
	the arx-perf.log files of their evidence folders - and prints per metric:
	  - the spread of a phase across the runs relative to its median (the median absolute deviation / median), as the
	    median, 90th percentile and largest over the phases, and the largest single deviation from the median
	  - leave-one-out false flags: every run of every phase compared with the baseline of the OTHER runs, for each K and
	    floor of the grid - how many green phase-runs a threshold would have flagged (want: none or nearly none)
	  - for each threshold, whether a +30 % on the phase's median would be caught
	A phase run of fewer whole seconds than -MinSeconds (default $ArxPerfMinSeconds) is left out: it is never flagged.
	The thresholds in perf-track-lib.ps1 ($ArxPerfK, $ArxPerfFloorPct, $ArxPerfAbsFloor) are set from this output; the
	table of #552's pull request is in docs/vr/TESTING.md, "Performance tracking in CI".

.PARAMETER EvidenceDir
	The run folders (ci-smoke -OutDir); on the runner %LOCALAPPDATA%\ArxFatalisVR\ci-evidence\<run id>-<attempt> of the
	service account. Only green runs with one lane count belong together.

.PARAMETER Metrics
	The metrics to measure (default: all a baseline stores).

.EXAMPLE
	.\scripts\dev\perf-noise.ps1 -EvidenceDir (Get-ChildItem C:\Windows\ServiceProfiles\NetworkService\AppData\Local\ArxFatalisVR\ci-evidence -Directory).FullName
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string[]]$EvidenceDir,
	[string[]]$Metrics,
	[double[]]$KGrid = @(3, 4, 5, 6),
	[double[]]$FloorGrid = @(5, 10, 15, 20, 25),
	[hashtable]$AbsFloor,
	[int]$MinSeconds = -1
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'perf-track-lib.ps1')
if (-not $Metrics) { $Metrics = $ArxPerfBaselineMetrics }
if (-not $AbsFloor) { $AbsFloor = $ArxPerfAbsFloor }
# A phase run shorter than this is never flagged ($ArxPerfMinSeconds), so it is no false flag either; 0 counts them all
if ($MinSeconds -lt 0) { $MinSeconds = $ArxPerfMinSeconds }
$c = [Globalization.CultureInfo]::InvariantCulture

$runs = foreach ($dir in $EvidenceDir) {
	$json = Read-ArxRunPerf -Path (Join-Path $dir 'perf.json')
	if (-not $json) { $json = Get-ArxRunPerf -EvidenceDir $dir }
	[pscustomobject]@{ Name = Split-Path -Leaf $dir; Perf = $json }
}
Write-Host "perf-noise: $(@($runs).Count) run(s): $(($runs | ForEach-Object Name) -join ', ')"

# phase -> metric -> values (one per run that has the phase valid)
$values = @{}
foreach ($r in $runs) {
	foreach ($phase in (Get-ArxPerfPhaseNames $r.Perf)) {
		$rec = Get-ArxPerfPhase $r.Perf $phase
		if (-not (Get-ArxPerfField $rec 'valid') -or [int](Get-ArxPerfField $rec 'seconds') -lt $MinSeconds) { continue }
		if (-not $values.ContainsKey($phase)) { $values[$phase] = @{} }
		foreach ($m in $Metrics) {
			$v = Get-ArxPerfField $rec $m
			if ($null -eq $v) { continue }
			if (-not $values[$phase].ContainsKey($m)) { $values[$phase][$m] = [Collections.Generic.List[double]]::new() }
			$values[$phase][$m].Add([double]$v)
		}
	}
}

function Pct([double[]]$Sorted, [double]$P) { if (-not $Sorted.Count) { return $null }; $Sorted[[math]::Min($Sorted.Count - 1, [math]::Ceiling($P * $Sorted.Count) - 1)] }

foreach ($m in $Metrics) {
	$rel = [Collections.Generic.List[double]]::new()
	$worst = 0.0; $worstAt = ''
	$medians = [Collections.Generic.List[double]]::new()
	$series = @()
	foreach ($phase in ($values.Keys | Sort-Object)) {
		if (-not $values[$phase].ContainsKey($m)) { continue }
		$vs = [double[]]$values[$phase][$m].ToArray()
		if ($vs.Count -lt 3) { continue }
		$series += [pscustomobject]@{ Phase = $phase; Values = $vs }
		$med = Get-ArxMedian $vs
		$mad = Get-ArxMedian ([double[]]@($vs | ForEach-Object { [math]::Abs($_ - $med) }))
		$medians.Add($med)
		if ($med -gt 0) {
			$rel.Add(100.0 * $mad / $med)
			foreach ($v in $vs) { $d = 100.0 * ($v - $med) / $med; if ($d -gt $worst) { $worst = $d; $worstAt = "$phase $([string]::Format($c, '{0:0.##} vs {1:0.##}', $v, $med))" } }
		}
	}
	$sortedRel = [double[]]@($rel | Sort-Object)
	$sortedMed = [double[]]@($medians | Sort-Object)
	Write-Host ''
	Write-Host ([string]::Format($c, '== {0}: {1} phases; median of the phase medians {2:0.##} (p90 {3:0.##}); spread/median % - median {4:0.#}, p90 {5:0.#}, max {6:0.#}; largest rise over a median +{7:0.#}% ({8})',
		$m, $series.Count, (Get-ArxMedian $sortedMed), (Pct $sortedMed 0.9), (Get-ArxMedian $sortedRel), (Pct $sortedRel 0.9), ($sortedRel | Select-Object -Last 1), $worst, $worstAt))
	$floor = if ($AbsFloor.ContainsKey($m)) { [double]$AbsFloor[$m] } else { 0.0 }
	# Leave one out: each run of each phase against the baseline of the other runs
	foreach ($k in $KGrid) {
		$cells = foreach ($f in $FloorGrid) {
			$flags = 0; $total = 0; $missed30 = 0; $phasesN = 0
			foreach ($s in $series) {
				$phasesN++
				for ($i = 0; $i -lt $s.Values.Count; $i++) {
					$others = [double[]]@(for ($j = 0; $j -lt $s.Values.Count; $j++) { if ($j -ne $i) { $s.Values[$j] } })
					$med = Get-ArxMedian $others
					$mad = Get-ArxMedian ([double[]]@($others | ForEach-Object { [math]::Abs($_ - $med) }))
					$limit = $med + [math]::Max([math]::Max($k * $mad, $med * $f / 100.0), $floor)
					$total++
					if ($s.Values[$i] -gt $limit) { $flags++ }
				}
				$all = Get-ArxMedian $s.Values
				$allMad = Get-ArxMedian ([double[]]@($s.Values | ForEach-Object { [math]::Abs($_ - $all) }))
				if ($all * 1.3 -le $all + [math]::Max([math]::Max($k * $allMad, $all * $f / 100.0), $floor)) { $missed30++ }
			}
			[string]::Format($c, 'floor {0,2}%: {1,3}/{2} false, +30% missed on {3,2}/{4}', $f, $flags, $total, $missed30, $phasesN)
		}
		Write-Host ([string]::Format($c, '  K {0}: ', $k) + ($cells -join ' | '))
	}
}
