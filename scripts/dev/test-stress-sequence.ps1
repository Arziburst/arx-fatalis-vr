<#
.SYNOPSIS
	Self-test of the sequence stress check (#566): its sequence copy, its reading of a failed run and of the logs, its
	summary, and its arguments - fixture text, no game, no load.

.DESCRIPTION
	Checks scripts/dev/stress-sequence-lib.ps1 and stress-sequence.ps1 -DryRun:
	  - the copy marks every `motion reset` batch with its record (on the same line or the lines after it), adds a
	    `stall` after the matching lines, replaces the first exactly matching line, and maps each line to the original
	  - an edit that matches nothing throws
	  - a smoke-test transcript's failure: the original line, the step, the assert's path, value, operator, expectation;
	    an until; a transcript without a sequence failure
	  - arx.log's records, a record's window in a fixture arx-perf.log (its hitches and missed slots, the mark found)
	  - the per-assert table: grouped by line, most frequent first, every value kept
	  - stress-sequence.ps1 -DryRun: the plan printed and exit 0, launching nothing; a bad -Load, a -StallMs without
	    -StallAfter and a -StallAfter that matches nothing are refused before anything starts
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'stress-sequence-lib.ps1')

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

# --- The sequence copy ------------------------------------------------------------------------------------------
$sequence = @(
	'@sim idle'                                                                                            # 1
	'vr-sim rate 90 clock'                                                                                 # 2
	'motion reset; vr-sim @+0ms action thumbstick left 0 1; vr-sim @+1500ms record walk90   # the walk'   # 3
	'@until motion.records.walk90.frames ge 0 10s'                                                         # 4
	'@assert motion.records.walk90.frames ge 121'                                                          # 5
	'motion reset'                                                                                         # 6
	'# a comment that says record nothing'                                                                 # 7
	'vr-sim record jump90'                                                                                 # 8
	'motion reset'                                                                                         # 9
	'vr-sim rate 90 clock'                                                                                 # 10
)
$c = New-StressSequenceCopy -Lines $sequence -Marks -StallAfter 'record walk90$' -StallMs 40 -Edits @('vr-sim rate 90 clock=>vr-sim rate 45 clock')
Check ($c.Lines[2] -eq 'motion reset; mark stress-reset walk90; vr-sim @+0ms action thumbstick left 0 1; vr-sim @+1500ms record walk90   # the walk') "the reset batch carries its record's mark ($($c.Lines[2]))"
Check ($c.Lines[3] -eq 'stall 40' -and $c.Origin[3] -eq 0) 'a stall after the line matching -StallAfter (its trailing comment ignored), mapped to no original line'
Check ($c.Lines[6] -eq 'motion reset; mark stress-reset jump90') "a reset alone on its line takes the next record, past a comment ($($c.Lines[6]))"
Check ($c.Lines[9] -eq 'motion reset') 'a reset with no record before the end is not marked'
Check ($c.Lines[1] -eq 'vr-sim rate 45 clock' -and $c.Lines[10] -eq 'vr-sim rate 90 clock') 'an edit replaces the first exact line only'
Check (($c.Records -join ',') -eq 'walk90,jump90') "the marked records ($($c.Records -join ','))"
Check ($c.Lines.Count -eq 11 -and $c.Origin[4] -eq 4 -and $c.Origin[10] -eq 10) "each line of the copy maps to the original's line ($($c.Origin -join ','))"
$plain = New-StressSequenceCopy -Lines $sequence
Check (($plain.Lines -join "`n") -eq ($sequence -join "`n") -and -not $plain.Records.Count) 'without marks, stalls or edits the copy is the original'
$threw = $false
try { $null = New-StressSequenceCopy -Lines $sequence -Edits @('vr-sim rate 91 clock=>x') } catch { $threw = $_.Exception.Message -match 'matches no line' }
Check $threw 'an edit that matches no line throws'

# --- A failed run's transcript ----------------------------------------------------------------------------------
$transcript = @"
  [ok]   sequence started
  => FAIL sequence refresh-rate: line 6 '@assert motion.records.walk90.frames ge 121': ASSERT FAILED: motion.records.walk90.frames (90) ge 121; exit code 0
"@
$f = Get-StressFailure $transcript $c.Origin
Check ($f.Line -eq 5 -and $f.CopyLine -eq 6) "the copy's line 6 is the original's line 5 ($($f.Line))"
Check ($f.Kind -eq 'assert' -and $f.Path -eq 'motion.records.walk90.frames' -and $f.Actual -eq '90' -and $f.Op -eq 'ge' -and $f.Expected -eq '121') "the assert's path, value, operator and expectation ($($f | ConvertTo-Json -Compress))"
Check ($f.Message -eq 'ASSERT FAILED: motion.records.walk90.frames (90) ge 121') "the message ends where the next reason starts ($($f.Message))"
$u = Get-StressFailure "  => FAIL sequence x: line 4 '@until motion.records.walk90.frames ge 0 10s': UNTIL FAILED: motion.records.walk90.frames (<absent>) never became ge 0 within 10 s"
Check ($u.Kind -eq 'until' -and $u.Actual -eq '<absent>' -and $u.Line -eq 4) 'an until failure, the line as is without a copy map'
Check ($null -eq (Get-StressFailure "  => PASS`n")) 'a passing transcript has no failure'

# --- The logs ---------------------------------------------------------------------------------------------------
$logText = @(
	'[I] CommandChannel:4605   [channel] record walk90: {"timelineMs":4000,"frames":90,"seconds":1.51,"walked":128.1}'
	'[I] VRSystem:2183         [vr] rate: 43 frames in 1.00 s = 43.0/s on 90.0 slots/s (hmd 11.11 ms), 43 shown | UNDER-SUBMITTING'
	'[I] CommandChannel:4605   [channel] record span90: {"timelineMs":9000,"frames":4,"seconds":0.13}'
) -join "`n"
$records = Get-StressRecords $logText
Check ($records.Count -eq 2 -and $records[0].Name -eq 'walk90' -and $records[0].Values.frames -eq 90 -and $records[1].Values.seconds -eq 0.13) 'the records of arx.log with their values'
$perfLines = @(
	'# arx-perf.log (#327): fixture'
	'S t=11 frames=90 min=10.9 mean=11.11 p95=11.5 max=12.0 hz=90.0 period=11.11 div=1 vr=1 missed=0 hitches=0 | update=0.3 vrwait=9.0 | uploads=0 self=0.4 selfmax=3'
	'M t=11.900 stress-reset walk90'
	'H t=11.950 frame=1000 ms=36.5 slots=3.3 period=11.11 missed=4 uploads=0 cause=update | update=34.8 vrwait=1.0 render=0.5'
	'S t=12 frames=60 min=10.9 mean=16.0 p95=36.5 max=420.0 hz=90.0 period=11.11 div=1 vr=1 missed=40 hitches=2 | update=7.0 vrwait=8.0 | uploads=0 self=0.4 selfmax=3'
	'H t=12.700 frame=1060 ms=420.0 slots=37.8 period=11.11 missed=36 uploads=0 cause=channel | channel=400.0 vrwait=10.0'
	'H t=13.600 frame=1100 ms=30.0 slots=2.7 period=11.11 missed=2 uploads=0 cause=render | render=25.0'
	'S t=13 frames=89 min=10.9 mean=11.2 p95=11.5 max=30.0 hz=90.0 period=11.11 div=1 vr=1 missed=2 hitches=1 | update=0.3 vrwait=9.0 | uploads=0 self=0.4 selfmax=3'
)
$perf = Read-ArxPerfLog $perfLines
$w = Get-StressWindow $perf 'walk90' 1.51
Check ($null -ne $w -and [Math]::Abs($w.Start - 11.9) -lt 1e-9 -and [Math]::Abs($w.End - 13.41) -lt 1e-9) 'the window starts at the mark and lasts the record''s seconds'
Check ($w.Hitches.Count -eq 2 -and $w.Missed -eq 40 -and $w.Longest -eq 420) "the hitches inside the window only ($($w.Hitches.Count), missed $($w.Missed), longest $($w.Longest))"
Check ($w.Seconds.Count -eq 3) "the seconds the window touches ($($w.Seconds.Count))"
$text = (Format-StressWindow $w) -join "`n"
Check ($text -match '\+0\.050 s\s+36\.5 ms' -and $text -match 'cause update\s+\| update 34\.8' -and $text -match '\+0\.800 s\s+420\.0 ms') "the window's hitches by time after the reset, with cause and phases`n$text"
Check ($null -eq (Get-StressWindow $perf 'span90' 0.13)) 'a record without a mark has no window'
$capped = (Format-StressWindow $w -MaxHitches 1) -join "`n"
Check ($capped -match '420\.0 ms' -and $capped -notmatch '36\.5 ms' -and $capped -match '\.\.\. and 1 shorter hitch') "many hitches: the longest ones, the rest counted`n$capped"

# --- The summary ------------------------------------------------------------------------------------------------
$f2 = Get-StressFailure "  => FAIL sequence refresh-rate: line 6 '@assert motion.records.walk90.frames ge 121': ASSERT FAILED: motion.records.walk90.frames (97) ge 121" $c.Origin
$f3 = Get-StressFailure "  => FAIL sequence refresh-rate: line 30 '@assert motion.records.span90.frames ge 9': ASSERT FAILED: motion.records.span90.frames (4) ge 9"
$results = @(
	[pscustomobject]@{ Run = 1; Verdict = 'FAIL'; Failure = $f }
	[pscustomobject]@{ Run = 2; Verdict = 'PASS'; Failure = $null }
	[pscustomobject]@{ Run = 3; Verdict = 'FAIL'; Failure = $f3 }
	[pscustomobject]@{ Run = 4; Verdict = 'FAIL'; Failure = $f2 }
	[pscustomobject]@{ Run = 5; Verdict = 'SKIPPED'; Failure = $null }
	[pscustomobject]@{ Run = 6; Verdict = 'FAIL'; Failure = $null }
)
$table = Get-StressAssertTable $results
Check ($table.Count -eq 3 -and $table[0].Line -eq 5 -and $table[0].Count -eq 2 -and ($table[0].Runs -join ',') -eq '1,4') 'grouped by the original line, the most frequent first'
Check (($table[0].Values -join ' | ') -eq '90 ge 121 | 97 ge 121') "every value read is kept ($($table[0].Values -join ' | '))"
$lines = Format-StressAssertTable $table 6
Check ($lines[0] -eq '2 of 6  line 5  @assert motion.records.walk90.frames ge 121' -and $lines[1] -eq '    runs 1, 4: 90 ge 121 | 97 ge 121') "the table's lines`n$($lines -join "`n")"
Check (($lines -join "`n") -match 'no sequence step failed') 'a failed run without a sequence failure is listed too'
Check (@(Format-StressAssertTable @() 3)[0] -eq 'no assert failed in 3 run(s)') 'no failures says so'
$allPassed = @([pscustomobject]@{ Run = 1; Verdict = 'PASS'; Failure = $null })
Check (@(Format-StressAssertTable (Get-StressAssertTable $allPassed) 1)[0] -eq 'no assert failed in 1 run(s)') 'no failures says so through the table of passing runs (as the script calls it)'

# --- A free PC --------------------------------------------------------------------------------------------------
$own = 'C:\build\copy-a\Release'
function Proc([string]$Name, [int]$Id, [string]$Cmd = '', [string]$Exe = '') { [pscustomobject]@{ Name = $Name; ProcessId = $Id; CommandLine = $Cmd; ExecutablePath = $Exe } }
$quiet = @((Proc 'pwsh.exe' 10 'pwsh -NoProfile -Command x'), (Proc 'arx.exe' 11 '' "$own\arx.exe"), (Proc 'pwsh.exe' 12 'pwsh -EncodedCommand abc'))
Check ((Get-StressBusyReason $quiet $null $own @(12)) -eq '') "this copy's own game and load and an unrelated pwsh leave the PC free"
$busy = Get-StressBusyReason ($quiet + @((Proc 'arx.exe' 20 '' 'C:\build\copy-b\Release\arx.exe'), (Proc 'pwsh.exe' 21 'pwsh -NoProfile -File scripts\dev\ci-local.ps1'), (Proc 'pwsh.exe' 22 'pwsh -File E:\w\scripts\build-windows.ps1 -Config Release'))) $null $own
Check ($busy -match 'another game \(arx\.exe pid 20\)' -and $busy -match 'ci-local\.ps1 \(pid 21\)' -and $busy -match 'build-windows\.ps1 \(pid 22\)') "another copy's game, ci-local and build make it busy ($busy)"
Check ((Get-StressBusyReason @((Proc 'arx.exe' 30 '' 'C:\build\copy-a\Release2\arx.exe')) $null $own) -match 'pid 30') 'a build directory that only starts like this copy''s is another game'
Check ((Get-StressBusyReason @() ([pscustomobject]@{ Pid = 5; What = 'gate' }) $own) -match 'a CI job runs \(gate, pid 5\)') 'the CI runner marker makes it busy'
Check ((Get-StressBusyReason @((Proc 'pwsh.exe' 40 'pwsh -File scripts\dev\ci-smoke.ps1')) $null $own @(40)) -eq '') 'an own pid never counts'
Check ((Get-StressBusyReason @((Proc 'cl.exe' 50), (Proc 'ninja.exe' 51)) $null $own @(51)) -eq 'a compile (cl.exe pid 50)') 'a compile is busy unless it is this script''s own load'

# --- The script's arguments (-DryRun launches nothing) -----------------------------------------------------------
$pwsh = (Get-Process -Id $PID).Path
$script = Join-Path $PSScriptRoot 'stress-sequence.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "test-stress-sequence-$PID"
try {
	$seqFile = Join-Path $root 'fixture.xrs'
	New-Item -ItemType Directory -Force $root | Out-Null
	[IO.File]::WriteAllLines($seqFile, $sequence)
	$out = (& $pwsh -NoProfile -File $script -Sequence $seqFile -Runs 3 -Load none -StallAfter 'record walk90$' -StallMs 40 -OutDir (Join-Path $root 'out') -DryRun *>&1) -join "`n"
	Check ($LASTEXITCODE -eq 0 -and $out -match 'dry run - no load, nothing launched' -and $out -match '3 run\(s\) one at a time, load: no load' -and $out -match 'a 40 ms stall after 1 line' -and $out -match 'marked records: walk90 jump90') "-DryRun prints the plan and exits 0`n$out"
	$copied = Join-Path $root 'out\sequence\fixture.xrs'
	Check ((Test-Path -LiteralPath $copied) -and ((Get-Content -LiteralPath $copied) -contains 'stall 40')) 'the copy is written to -OutDir'
	$out = (& $pwsh -NoProfile -File $script -Sequence $seqFile -Load cpu -LoadCores 2 -OutDir (Join-Path $root 'out2') -DryRun *>&1) -join "`n"
	Check ($LASTEXITCODE -eq 0 -and $out -match 'load: 2 busy-loop process' -and $out -match 'as shipped') "-Load cpu -LoadCores 2 in the plan`n$out"
	& $pwsh -NoProfile -File $script -Sequence $seqFile -Load disk -DryRun *> $null
	Check ($LASTEXITCODE -ne 0) "an unknown -Load is refused ($LASTEXITCODE)"
	$out = (& $pwsh -NoProfile -File $script -Sequence $seqFile -StallMs 40 -OutDir (Join-Path $root 'out3') -DryRun *>&1) -join "`n"
	Check ($LASTEXITCODE -ne 0 -and $out -match 'come together') "-StallMs without -StallAfter is refused ($LASTEXITCODE)"
	$out = (& $pwsh -NoProfile -File $script -Sequence $seqFile -StallAfter 'record nothing$' -StallMs 40 -OutDir (Join-Path $root 'out4') -DryRun *>&1) -join "`n"
	Check ($LASTEXITCODE -ne 0 -and $out -match 'matches no line') "a -StallAfter that matches nothing is refused ($LASTEXITCODE)"
} finally {
	Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-stress-sequence: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-stress-sequence: all checks passed' -ForegroundColor Green
exit 0
