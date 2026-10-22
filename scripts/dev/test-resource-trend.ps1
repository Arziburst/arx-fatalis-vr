<#
.SYNOPSIS
	Self-test of the resource trend of the performance log summary (#545): a fixture arx-perf.log with R lines, no game.

.DESCRIPTION
	Every 5 s the game writes one R line into arx-perf.log (src/platform/ProcessResources.h, formatResources; covered
	by tests/platform/ProcessResourcesTest.cpp):

	  R t=<s> priv=<MiB> ws=<MiB> commit=<MiB> handles=<n> gdi=<n> user=<n> threads=<n> tex=<n> buf=<n> xrsc=<n>

	priv = the process's private bytes, ws = its working set, commit = the system's commit charge (MiB, one decimal);
	handles, GDI and USER objects and threads of the process; GL textures and buffers alive; OpenXR swapchains alive.

	The interface this checks, in scripts/dev/perf-summary-lib.ps1:
	  - Read-ArxPerfLog: R lines go to .Resources (one object per line: t and the ten fields as numbers) and are not
	    Unknown; the S / H / M parsing is unchanged
	  - Get-ArxPerfSummary: .Resources is an ordered dictionary, field name -> trend, in the R line's order (priv ws commit
	    handles gdi user threads tex buf xrsc), empty when the log has no R line. A trend has
	      Samples  the number of R lines
	      First    the first value, Last the last, Max the largest
	      Slope    the least-squares slope over all samples, per minute (MiB per minute for the memory fields)
	      Step     the largest rise between two consecutive samples (0 when the field never rose)
	      StepT    the t of the sample after that rise (the first such rise on a tie)
	  - Format-ArxPerfSummary: a section whose first line starts with "Resources", then one row per field, the field's
	    name first, with "first <v>", "last <v>", "max <v>", "slope <+/-v>/min" and "step +<v> at t=<s>" in that order,
	    e.g. "  priv  first 412.0  last 476.0  max 476.0  slope +94.0/min  step +64.0 at t=30" (spacing free); no section
	    when the log has no R line
	  - perf-summary.ps1 prints it
	scripts/dev/crash-report.ps1 prints the same rows for a report's arx-perf.log (test-crash-report.ps1).
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}
function Near($A, [double]$B, [double]$Tolerance = 0.01) { $null -ne $A -and [math]::Abs([double]$A - $B) -le $Tolerance }

$c = [Globalization.CultureInfo]::InvariantCulture
$phases = 'other=0.10 input=0.00 channel=0.00 load=0.00 save=0.00 update=1.00 script=0.00 physics=0.00 particles=0.00 render=4.00 textures=0.00 present=0.50 limit=11.00 vrwait=0.00 vrend=0.00'
function SecondLine([int]$T) { "S t=$T frames=60 min=16.50 mean=16.67 p95=16.80 max=17.00 hz=60.0 period=16.67 div=1 vr=0 missed=0 hitches=0 | $phases | uploads=0 self=0.4 selfmax=3" }

# Twelve samples, 5 s apart: a 64 MiB step of priv at t=30 (the channel's `leak 64`), ws up and down, handles growing
# by 2 every 5 s (24 a minute), textures dropping at t=40, the rest flat
$times = 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60
$priv = 412, 412, 412, 412, 412, 476, 476, 476, 476, 476, 476, 476
$ws = 600, 700, 650, 650, 650, 650, 650, 650, 650, 650, 650, 650
$handles = 500, 502, 504, 506, 508, 510, 512, 514, 516, 518, 520, 522
$tex = 900, 900, 900, 900, 900, 900, 900, 800, 800, 800, 800, 800
$lines = [Collections.Generic.List[string]]::new()
$lines.Add('# arx-perf.log (#327): Arx Fatalis VR 0.1-dev + abc1234, started 2026-10-19 12:00:00')
for ($i = 0; $i -lt $times.Count; $i++) {
	$lines.Add((SecondLine $times[$i]))
	if ($times[$i] -eq 25) { $lines.Add('M t=26.000 before the leak') }
	$lines.Add([string]::Format($c, 'R t={0} priv={1:0.0} ws={2:0.0} commit=16000.0 handles={3} gdi=45 user=31 threads=40 tex={4} buf=210 xrsc=0',
		$times[$i], $priv[$i], $ws[$i], $handles[$i], $tex[$i]))
}
$lines.Add('# end: 720 frames')

# The least-squares slope of priv, per minute
$tMean = ($times | Measure-Object -Average).Average
$pMean = ($priv | Measure-Object -Average).Average
$num = 0.0; $den = 0.0
for ($i = 0; $i -lt $times.Count; $i++) { $num += ($times[$i] - $tMean) * ($priv[$i] - $pMean); $den += ($times[$i] - $tMean) * ($times[$i] - $tMean) }
$privSlope = 60.0 * $num / $den

Write-Host '==> parsing'
$log = Read-ArxPerfLog $lines
Check ($log.Unknown -eq 0) "R lines are not unknown lines ($($log.Unknown))"
Check ($log.Seconds.Count -eq 12 -and $log.Marks.Count -eq 1 -and $log.Ended) 'the S and M lines and the end line are parsed as before'
$parsedR = if ($log.PSObject.Properties['Resources']) { @($log.Resources | Where-Object { $null -ne $_ }) } else { @() }
Check ($parsedR.Count -eq 12) "12 R lines parsed into .Resources ($($parsedR.Count))"
if ($parsedR.Count -eq 12) {
	$r5 = $log.Resources[5]
	Check ($r5.t -eq 30 -and (Near $r5.priv 476) -and (Near $r5.ws 650) -and (Near $r5.commit 16000) -and $r5.handles -eq 510) 'an R line''s t and memory fields'
	Check ($r5.gdi -eq 45 -and $r5.user -eq 31 -and $r5.threads -eq 40 -and $r5.tex -eq 900 -and $r5.buf -eq 210 -and $r5.xrsc -eq 0) 'an R line''s counts'
}

Write-Host '==> the trend'
$summary = Get-ArxPerfSummary $log
$res = if ($summary.PSObject.Properties['Resources']) { $summary.Resources } else { $null }
Check ($null -ne $res) 'the summary has .Resources'
if ($res) {
	$keys = @($res.Keys)
	Check (($keys -join ' ') -eq 'priv ws commit handles gdi user threads tex buf xrsc') "the fields in the R line's order ($($keys -join ' '))"
	$p = $res['priv']
	Check ($p -and $p.Samples -eq 12 -and (Near $p.First 412) -and (Near $p.Last 476) -and (Near $p.Max 476)) 'priv: samples, first, last, max'
	Check ($p -and (Near $p.Step 64) -and $p.StepT -eq 30) "priv: the step of the leak, +64 at t=30 ($($p.Step) at $($p.StepT))"
	Check ($p -and (Near $p.Slope $privSlope)) "priv: the least-squares slope per minute ($($p.Slope) vs $privSlope)"
	$w = $res['ws']
	Check ($w -and (Near $w.Max 700) -and (Near $w.Last 650) -and (Near $w.Step 100) -and $w.StepT -eq 10) 'ws: the max is not the last value; the largest rise'
	$h = $res['handles']
	Check ($h -and (Near $h.Slope 24) -and (Near $h.Step 2) -and $h.StepT -eq 10) "handles: 24 a minute, the first of equal rises ($($h.Slope), +$($h.Step) at $($h.StepT))"
	$t = $res['tex']
	Check ($t -and (Near $t.Step 0) -and (Near $t.Max 900) -and (Near $t.Last 800) -and $t.Slope -lt 0) 'tex: a drop is no step, the slope is negative'
	$x = $res['xrsc']
	Check ($x -and (Near $x.Slope 0) -and (Near $x.Step 0) -and (Near $x.Max 0)) 'xrsc: flat at zero'
}

Write-Host '==> text'
$text = (Format-ArxPerfSummary $summary -Path 'arx-perf.log') -join "`n"
Check ($text -match '(?m)^Resources') 'a section starting with "Resources"'
Check ($text -match '(?m)^\s+priv\s.*first 412(\.0)?\s.*last 476(\.0)?\s.*max 476(\.0)?\s.*slope \+\d+(\.\d+)?/min.*step \+64(\.0)? at t=30\b') 'the priv row: first, last, max, slope, the step and when'
Check ($text -match '(?m)^\s+handles\s.*slope \+24(\.0+)?/min') 'the handles row: +24 a minute'
Check ($text -match '(?m)^\s+tex\s.*slope -\d') 'the tex row: a negative slope'
foreach ($field in 'ws', 'commit', 'gdi', 'user', 'threads', 'buf', 'xrsc') {
	Check ($text -match "(?m)^\s+$field\s.*first .*last .*max .*slope .*step ") "a row for $field"
}
Check ($text -match 'Run: 12 s with frames') 'the run line is unchanged'

Write-Host '==> a log without R lines'
$old = Get-ArxPerfSummary (Read-ArxPerfLog @('# arx-perf.log', (SecondLine 0), (SecondLine 1), '# end: 120 frames'))
Check ($old.PSObject.Properties['Resources'] -and @($old.Resources.Keys).Count -eq 0) 'no trend'
$oldText = (Format-ArxPerfSummary $old) -join "`n"
Check ($oldText -notmatch '(?m)^\s+priv\s') 'no priv row'

Write-Host '==> perf-summary.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-resource-trend-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
try {
	$file = Join-Path $root 'arx-perf.log'
	[IO.File]::WriteAllLines($file, $lines, [Text.UTF8Encoding]::new($false))
	$pwsh = (Get-Process -Id $PID).Path
	$output = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'perf-summary.ps1') -Path $file 2>&1
	$exit = $LASTEXITCODE
	$joined = $output -join "`n"
	Check ($exit -eq 0) "exit 0 ($exit)"
	Check ($joined -match '(?m)^\s+priv\s.*step \+64(\.0)? at t=30\b') 'the priv row with its step is printed'
} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-resource-trend: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-resource-trend: all checks passed' -ForegroundColor Green
exit 0
