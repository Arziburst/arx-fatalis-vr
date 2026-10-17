<#
.SYNOPSIS
	Self-test of the performance log summary (#327): a fixture arx-perf.log, no game.

.DESCRIPTION
	Checks scripts/dev/perf-summary-lib.ps1 and perf-summary.ps1 on a log written in the game's format
	(src/vr/PerfStats.cpp formatSecond / formatHitch):
	  - every S, H and M line is parsed with its numbers and phases; header and end lines are recognised
	  - the run totals and the frame budget are weighted by frames, the budget sorted largest first
	  - the worst seconds come by p95 (ties by max), every hitch is listed with its cause
	  - a log without an end line is called out; a log with no seconds says so instead of failing
	  - the display rates (#23): consecutive seconds of one rate grouped, judged by their frames against rate / div
	    (MISMATCH over 5 % off), the second of a switch named and not judged, -ExpectHz matched or MISMATCH
	  - perf-summary.ps1 reads a file the game still holds open, prints the summary and exits 0; a missing log is exit 1
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
function Near([double]$A, [double]$B, [double]$Tolerance = 0.001) { [math]::Abs($A - $B) -le $Tolerance }

$phases = 'other input channel load save update script physics particles render textures present limit vrwait vrend' -split ' '
function PhaseText([hashtable]$Values, [string]$Format) {
	($phases | ForEach-Object { "$_=" + ([double]$(if ($null -ne $Values[$_]) { $Values[$_] } else { 0 })).ToString($Format, [Globalization.CultureInfo]::InvariantCulture) }) -join ' '
}
function SecondLine($T, $Frames, $Min, $Mean, $P95, $Max, $Missed, $Hitches, [hashtable]$P, $Vr = 1, $Uploads = 0, $Self = 0.4, $SelfMax = 3) {
	$c = [Globalization.CultureInfo]::InvariantCulture
	[string]::Format($c, 'S t={0} frames={1} min={2:0.00} mean={3:0.00} p95={4:0.00} max={5:0.00} hz=90.0 period=11.11 div=1 vr={6} missed={7} hitches={8} | {9} | uploads={10} self={11:0.0} selfmax={12:0}',
		$T, $Frames, $Min, $Mean, $P95, $Max, $Vr, $Missed, $Hitches, (PhaseText $P '0.00'), $Uploads, $Self, $SelfMax)
}

# --- A fixture run: 4 seconds in VR, a channel stall in second 2, a level load in second 3 ----------------------
$lines = @(
	'# arx-perf.log (#327): Arx Fatalis VR 0.1-dev + abc1234, started 2026-10-13 12:00:00'
	'# S = one second: ...'
	(SecondLine 0 90 10.90 11.11 11.30 11.60 0 0 @{ vrwait = 6.0; render = 4.0; update = 1.0; other = 0.11 })
	(SecondLine 1 90 10.90 11.11 11.40 12.00 0 0 @{ vrwait = 6.0; render = 4.0; update = 1.0; other = 0.11 })
	'M t=2.100 before the stall'
	(SecondLine 2 72 10.90 13.89 11.50 211.10 18 1 @{ vrwait = 4.8; render = 4.0; update = 1.0; channel = 3.0; other = 1.09 })
	'H t=2.200 frame=252 ms=211.1 slots=19.0 period=11.11 missed=18 uploads=0 cause=channel | ' + (PhaseText @{ channel = 200.2; render = 4.0; update = 1.0; vrwait = 5.9 } '0.0')
	(SecondLine 3 60 10.90 16.67 30.00 260.00 25 2 @{ vrwait = 4.0; render = 5.0; update = 1.0; load = 6.0; textures = 0.5; other = 0.17 } -Uploads 40 -SelfMax 41)
	'H t=3.500 frame=400 ms=260.0 slots=23.4 period=11.11 missed=22 uploads=37 cause=load | ' + (PhaseText @{ load = 231.0; textures = 9.0; render = 5.0; vrwait = 3.0 } '0.0')
	'H t=3.800 frame=430 ms=30.0 slots=2.7 period=11.11 missed=2 uploads=3 cause=textures | ' + (PhaseText @{ textures = 20.0; render = 6.0; vrwait = 4.0 } '0.0')
	'# end: 312 frames'
)

Write-Host '==> parsing'
$log = Read-ArxPerfLog $lines
Check ($log.Seconds.Count -eq 4) "4 seconds parsed ($($log.Seconds.Count))"
Check ($log.Hitches.Count -eq 3) "3 hitches parsed ($($log.Hitches.Count))"
Check ($log.Marks.Count -eq 1 -and $log.Marks[0].Text -eq 'before the stall' -and (Near $log.Marks[0].t 2.1)) 'the mark with its time and text'
Check ($log.Ended) 'the end line is recognised'
Check ($log.Unknown -eq 0) "no unknown lines ($($log.Unknown))"
$s3 = $log.Seconds[3]
Check ($s3.t -eq 3 -and $s3.frames -eq 60 -and (Near $s3.p95 30.0) -and (Near $s3.max 260.0) -and $s3.missed -eq 25 -and $s3.hitches -eq 2) 'a second''s stats'
Check ((Near $s3.Phases.load 6.0) -and (Near $s3.Phases.textures 0.5) -and $s3.uploads -eq 40 -and (Near $s3.self 0.4)) 'a second''s phases, uploads and self cost'
$h = $log.Hitches[1]
Check ($h.cause -eq 'load' -and (Near $h.ms 260.0) -and $h.missed -eq 22 -and $h.uploads -eq 37 -and (Near $h.Phases.load 231.0)) 'a hitch with its cause and phases'

Write-Host '==> summary'
$summary = Get-ArxPerfSummary $log -Top 2
$r = $summary.Run
Check ($r.Seconds -eq 4 -and $r.Frames -eq 312) "run: 4 seconds, 312 frames ($($r.Seconds), $($r.Frames))"
$expectedMean = (90 * 11.11 + 90 * 11.11 + 72 * 13.89 + 60 * 16.67) / 312
Check (Near $r.MeanMs $expectedMean) "the mean frame is weighted by frames ($($r.MeanMs) vs $expectedMean)"
Check ($r.Missed -eq 43 -and $r.Hitches -eq 3 -and $r.VrSeconds -eq 4 -and (Near $r.Hz 90.009 0.01)) 'missed slots, hitches, VR seconds, display rate'
$keys = @($summary.Budget.Keys)
Check ($keys[0] -eq 'vrwait' -and $keys[1] -eq 'render') "the budget is sorted largest first ($($keys[0..2] -join ', '))"
$expectedLoad = 60 * 6.0 / 312
Check (Near $summary.Budget.load $expectedLoad) "a phase's budget is weighted by frames ($($summary.Budget.load) vs $expectedLoad)"
Check ($summary.Worst.Count -eq 2 -and $summary.Worst[0].t -eq 3 -and $summary.Worst[1].t -eq 2) 'the worst seconds by p95, then max (second 2 beats 1 on p95 11.5)'
Check ($summary.Hitches.Count -eq 3) 'every hitch is in the summary'
Check (Near $r.SelfMaxUs 41) "the log's most expensive frame over the run ($($r.SelfMaxUs))"
$causes = @($summary.Causes.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" })
Check (($causes -join ',') -eq 'channel=1,load=1,textures=1') "hitches counted by cause ($($causes -join ','))"

Write-Host '==> text'
$text = (Format-ArxPerfSummary $summary -Path 'arx-perf.log') -join "`n"
Check ($text -match 'Run: 4 s with frames, 312 frames') 'the run line'
Check ($text -match 'us per frame \(max 41 us\)') 'the run line has the log''s most expensive frame'
Check ($text -match 'by cause: channel 1, load 1, textures 1') 'the hitch causes are counted'
Check ($text -notmatch 'NO end line') 'a clean run is not called unclean'
Check ($text -match 'vrwait \d+\.\d\d \(\d+%\)') 'the budget line has ms and shares'
Check ($text -match 't=   3 s  p95   30\.00  max   260\.0') 'the worst second line'
Check ($text -match 't=    2\.200 s    211\.1 ms \(19\.0 slots\)  cause channel') 'the stall hitch with its cause'
Check ($text -match 'cause load .*\| load 231\.0, textures 9\.0, render 5\.0, vrwait 3\.0') 'the load hitch with its largest phases'
Check ($text -match 'before the stall') 'the marks'

Write-Host '==> display rates (#23)'
$d = @($summary.Displays)
Check ($d.Count -eq 1 -and (Near $d[0].Hz 90.0) -and $d[0].Seconds -eq 4 -and $d[0].Frames -eq 312) "one display rate, 90 Hz over the 4 seconds ($($d.Count))"
Check ((Near $d[0].Fps (312 * 1000.0 / (90 * 11.11 + 90 * 11.11 + 72 * 13.89 + 60 * 16.67)) 0.01) -and $d[0].Verdict -eq 'MISMATCH' -and $d[0].Below -eq 2) "78.0 frames/s of 90: MISMATCH, 2 seconds under 95 % ($($d[0].Fps), $($d[0].Verdict), $($d[0].Below))"
Check ($text -match 'Display rates' -and $text -match '90\.0 Hz \(11\.11 ms\) div 1 vr .*78\.0 frames/s of 90\.0 expected: MISMATCH, 2 of 4 s under 95 %') 'the display line'
Check ($text -match 'display 90\.0 Hz \(11\.11 ms\)') 'one rate: the run line names it'

# A simulated run switched 72 -> 144 Hz (the refresh-rate check): the second of the switch mixes both, the 144 Hz part
# lost frames
function RateLine($T, $Frames, $Mean, $Hz, $Period) {
	[string]::Format([Globalization.CultureInfo]::InvariantCulture, 'S t={0} frames={1} min={2:0.00} mean={2:0.00} p95={2:0.00} max={2:0.00} hz={3:0.0} period={4:0.00} div=1 vr=1 missed=0 hitches=0 | {5} | uploads=0 self=0.4 selfmax=3',
		$T, $Frames, $Mean, $Hz, $Period, (PhaseText @{ vrwait = $Mean } '0.00'))
}
$rates = @(
	(RateLine 0 72 13.89 72.0 13.89), (RateLine 1 72 13.89 72.0 13.89), (RateLine 2 72 13.89 72.0 13.89),
	(RateLine 3 60 16.67 100.0 10.00),
	(RateLine 4 144 6.94 144.1 6.94), (RateLine 5 144 6.94 144.1 6.94), (RateLine 6 100 10.0 144.1 6.94),
	'# end: 736 frames'
)
$switched = Get-ArxPerfSummary (Read-ArxPerfLog $rates) -ExpectHz 144, 120
$g = @($switched.Displays)
Check ($g.Count -eq 3 -and (Near $g[0].Hz 72.0) -and (Near $g[2].Hz 144.1)) "three groups: 72, the switch, 144 ($(($g | ForEach-Object Hz) -join ', '))"
Check ($g[0].Verdict -eq 'matched' -and $g[1].Verdict -eq 'switch' -and $g[2].Verdict -eq 'MISMATCH' -and $g[2].Below -eq 1) "72 matched, the switch not judged, 144 MISMATCH with 1 second under ($(($g | ForEach-Object Verdict) -join ', '))"
$switchedText = (Format-ArxPerfSummary $switched) -join "`n"
Check ($switchedText -match 'display several rates \(below\)') 'several rates: the run line does not average them'
Check ($switchedText -match 'the rate changed within this second') 'the switch second is named'
Check ($switchedText -match 'Expected display rate 144\.0 Hz: matched - the runtime ran at it for 3 s') 'an expected rate the runtime gave'
Check ($switchedText -match 'Expected display rate 120\.0 Hz: MISMATCH - the runtime ran at 72\.0 Hz, 144\.1 Hz') 'an expected rate the runtime never gave'
$flat = Get-ArxPerfSummary (Read-ArxPerfLog @((SecondLine 0 60 16.6 16.67 16.8 17.0 0 0 @{ limit = 10.0 } -Vr 0), '# end')) -ExpectHz 90
Check ((((Format-ArxPerfSummary $flat) -join "`n") -match 'Expected display rate 90\.0 Hz: MISMATCH - the runtime ran at no VR rate at all')) 'a flat run never matches an expected headset rate'

$unclean = Get-ArxPerfSummary (Read-ArxPerfLog ($lines | Where-Object { $_ -notlike '# end*' }))
Check (((Format-ArxPerfSummary $unclean) -join "`n") -match 'NO end line') 'a log without its end line is called out'
$empty = (Format-ArxPerfSummary (Get-ArxPerfSummary (Read-ArxPerfLog @('# arx-perf.log', '# end: 0 frames')))) -join "`n"
Check ($empty -match 'No seconds in the log') 'a log without seconds says so'

Write-Host '==> perf-summary.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-perf-summary-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
try {
	$file = Join-Path $root 'arx-perf.log'
	[IO.File]::WriteAllLines($file, $lines, [Text.UTF8Encoding]::new($false))
	$pwsh = (Get-Process -Id $PID).Path
	# The game holds its log open for writing while it runs
	$held = [IO.FileStream]::new($file, 'Open', 'Write', 'ReadWrite')
	try {
		$output = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'perf-summary.ps1') -Path $file -Top 3 2>&1
		$exit = $LASTEXITCODE
	} finally {
		$held.Dispose()
	}
	$joined = $output -join "`n"
	Check ($exit -eq 0) "exit 0 on a log the game holds open ($exit)"
	Check ($joined -match 'Performance log: arx-perf\.log' -and $joined -notmatch [regex]::Escape($root)) 'only the file name, not the full path, is printed'
	Check ($joined -match 'Hitches \(3\)') 'the hitches are printed'
	& $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'perf-summary.ps1') -Path (Join-Path $root 'missing.log') *> $null
	Check ($LASTEXITCODE -eq 1) "a missing log is exit 1 ($LASTEXITCODE)"
} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-perf-summary: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-perf-summary: all checks passed' -ForegroundColor Green
exit 0
