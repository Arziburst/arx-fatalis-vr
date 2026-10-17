# The performance log summary (#327): read one run's arx-perf.log (src/vr/PerfLog.h writes it next to
# arx.log) and boil it down to the worst seconds, every hitch with its cause and the average frame budget
# split. Dot-source it:
#   . (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')
# Used by perf-summary.ps1; tested by test-perf-summary.ps1 (fixture logs, no game).
#
# The lines (docs/vr/TESTING.md, "The performance log"):
#   # ...                                                       header, end
#   S t=<s> frames=.. min=.. mean=.. p95=.. max=.. hz=.. period=.. div=.. vr=0|1 missed=.. hitches=.. | <phase>=<ms> ... | uploads=.. self=..
#   H t=<s> frame=.. ms=.. slots=.. period=.. missed=.. uploads=.. cause=<phase> | <phase>=<ms> ...
#   M t=<s> <text>

# "a=1 b=2.5 c=x" as an ordered hashtable; numbers as [double]
function ConvertFrom-ArxPerfPairs([string]$Text) {
	$pairs = [ordered]@{}
	foreach ($token in ($Text.Trim() -split '\s+')) {
		if ($token -notmatch '^([^=]+)=(.*)$') { continue }
		$value = $Matches[2]
		$number = 0.0
		if ([double]::TryParse($value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
			$pairs[$Matches[1]] = $number
		} else {
			$pairs[$Matches[1]] = $value
		}
	}
	$pairs
}

# The lines of a log as objects: Header (the '#' lines), Seconds, Hitches, Marks, Ended (the '# end' line was written)
function Read-ArxPerfLog([string[]]$Lines) {
	$log = [pscustomobject]@{
		Header  = [Collections.Generic.List[string]]::new()
		Seconds = [Collections.Generic.List[object]]::new()
		Hitches = [Collections.Generic.List[object]]::new()
		Marks   = [Collections.Generic.List[object]]::new()
		Ended   = $false
		Unknown = 0
	}
	foreach ($line in $Lines) {
		if (-not $line) { continue }
		if ($line.StartsWith('#')) {
			if ($line.StartsWith('# end')) { $log.Ended = $true }
			$log.Header.Add($line)
			continue
		}
		$parts = $line -split ' \| '
		if ($line.StartsWith('S ') -and $parts.Count -eq 3) {
			$s = ConvertFrom-ArxPerfPairs $parts[0].Substring(2)
			$s.Phases = ConvertFrom-ArxPerfPairs $parts[1]
			foreach ($kv in (ConvertFrom-ArxPerfPairs $parts[2]).GetEnumerator()) { $s[$kv.Key] = $kv.Value }
			$log.Seconds.Add([pscustomobject]$s)
		} elseif ($line.StartsWith('H ') -and $parts.Count -eq 2) {
			$h = ConvertFrom-ArxPerfPairs $parts[0].Substring(2)
			$h.Phases = ConvertFrom-ArxPerfPairs $parts[1]
			$log.Hitches.Add([pscustomobject]$h)
		} elseif ($line -match '^M t=([0-9.]+) ?(.*)$') {
			$log.Marks.Add([pscustomobject]@{ t = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture); Text = $Matches[2] })
		} else {
			$log.Unknown++
		}
	}
	$log
}

# The phases of a hashtable, largest first, as "name ms" strings (at most $Count, none under $Floor ms)
function Format-ArxPerfTopPhases($Phases, [int]$Count = 3, [double]$Floor = 0.05, [string]$Format = '{0} {1:0.0}') {
	$top = @($Phases.GetEnumerator() | Where-Object { $_.Value -ge $Floor } | Sort-Object Value -Descending | Select-Object -First $Count)
	($top | ForEach-Object { [string]::Format([Globalization.CultureInfo]::InvariantCulture, $Format, $_.Key, $_.Value) }) -join ', '
}

# The display rates of a run (#23): consecutive seconds with the same display (the runtime's predicted display period in
# VR, the monitor or fps_limit flat, to 0.1 Hz; the divisor; VR or not) as one group each, with the frames the game made
# against the rate it should have made: Expected = Hz / div. A group is MISMATCH when its frames per second (its frames
# over the time they took) are more than $Tolerance off the expected rate - the game did not keep up, or nothing paced
# it; a single second between two other rates is the second the rate changed in (its line mixes both): 'switch', not
# judged. Below counts the seconds of a group whose own rate (1000 / mean) was under (1 - $Tolerance) of the expected.
function Get-ArxPerfDisplays($Seconds, [double]$Tolerance = 0.05) {
	$groups = [Collections.Generic.List[object]]::new()
	$current = $null
	foreach ($s in $Seconds) {
		$hz = if ($s.period -gt 0) { [math]::Round(1000.0 / $s.period, 1) } else { 0.0 }
		$div = [math]::Max([int]$s.div, 1)
		$key = "$hz/$div/$($s.vr)"
		if (-not $current -or $current.Key -ne $key) {
			$current = [pscustomobject]@{ Key = $key; Hz = $hz; PeriodMs = [double]$s.period; Div = $div; Vr = ($s.vr -eq 1)
				FirstT = $s.t; LastT = $s.t; Seconds = 0; Frames = 0.0; TimeMs = 0.0; Below = 0; Expected = 0.0; Fps = 0.0; Verdict = '' }
			$groups.Add($current)
		}
		$current.LastT = $s.t
		$current.Seconds++
		$current.Frames += $s.frames
		$current.TimeMs += $s.frames * $s.mean
		$expected = $hz / $div
		if ($expected -gt 0 -and $s.mean -gt 0 -and 1000.0 / $s.mean -lt $expected * (1 - $Tolerance)) { $current.Below++ }
	}
	for ($i = 0; $i -lt $groups.Count; $i++) {
		$g = $groups[$i]
		$g.Expected = $g.Hz / $g.Div
		$g.Fps = if ($g.TimeMs -gt 0) { $g.Frames * 1000.0 / $g.TimeMs } else { 0.0 }
		if ($g.Hz -le 0) { $g.Verdict = 'rate unknown' }
		elseif ($g.Seconds -eq 1 -and $i -gt 0 -and $i -lt $groups.Count - 1) { $g.Verdict = 'switch' }
		elseif ([math]::Abs($g.Fps - $g.Expected) -le $g.Expected * $Tolerance) { $g.Verdict = 'matched' }
		else { $g.Verdict = 'MISMATCH' }
	}
	, $groups
}

# The summary of a read log: Run totals, Budget (phase -> mean ms per frame over the run, largest first),
# Worst (the $Top seconds with the highest p95, ties by max), Hitches (all of them, in order), Displays (the display
# rates, Get-ArxPerfDisplays) and, with -ExpectHz, the rates the run was meant to show that the runtime never gave
function Get-ArxPerfSummary($Log, [int]$Top = 5, [double[]]$ExpectHz = @()) {
	$seconds = @($Log.Seconds)
	$frames = 0.0; $time = 0.0; $missed = 0.0; $hitches = 0.0; $vrSeconds = 0; $self = 0.0; $selfMax = 0.0
	$periods = [Collections.Generic.List[double]]::new()
	$budget = [ordered]@{}
	foreach ($s in $seconds) {
		$frames += $s.frames
		$time += $s.frames * $s.mean
		$missed += $s.missed
		$hitches += $s.hitches
		$self += $s.frames * $s.self
		if ($s.selfmax -gt $selfMax) { $selfMax = $s.selfmax }
		if ($s.vr -eq 1) { $vrSeconds++ }
		if ($s.period -gt 0) { $periods.Add($s.period) }
		foreach ($kv in $s.Phases.GetEnumerator()) {
			if (-not $budget.Contains($kv.Key)) { $budget[$kv.Key] = 0.0 }
			$budget[$kv.Key] += $s.frames * $kv.Value
		}
	}
	$meanMs = if ($frames) { $time / $frames } else { 0.0 }
	$split = [ordered]@{}
	foreach ($kv in ($budget.GetEnumerator() | Sort-Object Value -Descending)) {
		$split[$kv.Key] = if ($frames) { $kv.Value / $frames } else { 0.0 }
	}
	$period = if ($periods.Count) { ($periods | Measure-Object -Average).Average } else { 0.0 }
	$displays = Get-ArxPerfDisplays $seconds
	# The rates asked for (-ExpectHz: the headset set to 120 Hz in SteamVR) against the rates the runtime gave
	$expectations = foreach ($want in $ExpectHz) {
		$seen = @($displays | Where-Object { $_.Vr -and $_.Verdict -ne 'switch' -and [math]::Abs($_.Hz - $want) -le [math]::Max(0.5, $want * 0.01) })
		$ran = @($displays | Where-Object { $_.Vr -and $_.Verdict -ne 'switch' } | ForEach-Object Hz | Sort-Object -Unique)
		[pscustomobject]@{ Hz = $want; Seen = [bool]$seen.Count; Seconds = ($seen | Measure-Object Seconds -Sum).Sum; Ran = $ran }
	}
	[pscustomobject]@{
		Run = [pscustomobject]@{
			Seconds   = $seconds.Count
			Frames    = [int]$frames
			MeanMs    = $meanMs
			Fps       = if ($meanMs -gt 0) { 1000.0 / $meanMs } else { 0.0 }
			PeriodMs  = $period
			Hz        = if ($period -gt 0) { 1000.0 / $period } else { 0.0 }
			VrSeconds = $vrSeconds
			Missed    = [int]$missed
			Hitches   = [int]$hitches
			SelfUs    = if ($frames) { $self / $frames } else { 0.0 }
			SelfMaxUs = $selfMax
			Ended     = $Log.Ended
		}
		Budget  = $split
		# cause -> hitches, most first
		Causes  = $(
			$causes = [ordered]@{}
			foreach ($g in ($Log.Hitches | Group-Object cause | Sort-Object @{ Expression = 'Count'; Descending = $true }, Name)) { $causes[$g.Name] = $g.Count }
			$causes
		)
		Worst   = @($seconds | Sort-Object @{ Expression = 'p95'; Descending = $true }, @{ Expression = 'max'; Descending = $true } | Select-Object -First $Top)
		Hitches = @($Log.Hitches)
		Marks   = @($Log.Marks)
		Displays = $displays
		Expect  = @($expectations)
	}
}

# The summary as text lines (plain, fit for a terminal and for a report's code block)
function Format-ArxPerfSummary($Summary, [string]$Path = '') {
	$c = [Globalization.CultureInfo]::InvariantCulture
	$r = $Summary.Run
	$out = [Collections.Generic.List[string]]::new()
	if ($Path) { $out.Add("Performance log: $Path") }
	if (-not $r.Seconds) {
		$out.Add('No seconds in the log: the run ended before its first second closed, or the log is not a performance log.')
		return $out
	}
	$out.Add([string]::Format($c, 'Run: {0} s with frames, {1} frames, mean {2:0.00} ms ({3:0.0} fps); display {4} ({5} of {0} s in VR); missed display slots {6}; hitches {7}; the log''s own cost {8:0.0} us per frame (max {9:0} us){10}',
		$r.Seconds, $r.Frames, $r.MeanMs, $r.Fps,
		$(if (@($Summary.Displays | Where-Object Verdict -ne 'switch' | ForEach-Object Hz | Sort-Object -Unique).Count -gt 1) { 'several rates (below)' }
		  elseif ($r.Hz -gt 0) { [string]::Format($c, '{0:0.0} Hz ({1:0.00} ms)', $r.Hz, $r.PeriodMs) } else { 'rate unknown' }),
		$r.VrSeconds, $r.Missed, $r.Hitches, $r.SelfUs, $r.SelfMaxUs, $(if ($r.Ended) { '' } else { ' - NO end line: the game did not shut down cleanly' })))

	# The display rates (#23): what paced the game - in VR the runtime's predicted display period - and whether the
	# frames came at that rate
	$out.Add('Display rates (in VR the runtime''s predicted display period; expected frames/s = rate / div):')
	foreach ($g in $Summary.Displays) {
		$where = if ($g.FirstT -eq $g.LastT) { "t={0}" -f $g.FirstT } else { "t={0}-{1}" -f $g.FirstT, $g.LastT }
		$judged = switch ($g.Verdict) {
			'switch' { 'the rate changed within this second (its line mixes both)' }
			'rate unknown' { 'rate unknown' }
			default { [string]::Format($c, '{0:0.0} frames/s of {1:0.0} expected: {2}, {3} of {4} s under 95 %', $g.Fps, $g.Expected, $g.Verdict, $g.Below, $g.Seconds) }
		}
		$out.Add([string]::Format($c, '  {0,6:0.0} Hz ({1:0.00} ms) div {2} {3}  {4,-10} {5,3} s, {6,6} frames: {7}',
			$g.Hz, $g.PeriodMs, $g.Div, $(if ($g.Vr) { 'vr  ' } else { 'flat' }), $where, $g.Seconds, [int]$g.Frames, $judged))
	}
	foreach ($e in $Summary.Expect) {
		if ($e.Seen) {
			$out.Add([string]::Format($c, 'Expected display rate {0:0.0} Hz: matched - the runtime ran at it for {1} s', $e.Hz, $e.Seconds))
		} else {
			$out.Add([string]::Format($c, 'Expected display rate {0:0.0} Hz: MISMATCH - the runtime ran at {1}', $e.Hz,
				$(if ($e.Ran.Count) { ($e.Ran | ForEach-Object { [string]::Format($c, '{0:0.0} Hz', $_) }) -join ', ' } else { 'no VR rate at all' })))
		}
	}

	$out.Add('Frame budget (mean ms per frame over the run, share of the mean frame):')
	$parts = foreach ($kv in $Summary.Budget.GetEnumerator()) {
		if ($kv.Value -lt 0.005) { continue }
		[string]::Format($c, '{0} {1:0.00} ({2:0}%)', $kv.Key, $kv.Value, $(if ($r.MeanMs -gt 0) { 100.0 * $kv.Value / $r.MeanMs } else { 0 }))
	}
	$out.Add('  ' + ($parts -join ', '))

	$out.Add("Worst seconds (by p95):")
	foreach ($s in $Summary.Worst) {
		$out.Add([string]::Format($c, '  t={0,4} s  p95 {1,7:0.00}  max {2,7:0.0}  mean {3,6:0.00} ms  frames {4,3}  missed {5,3}  hitches {6,2}  {7}  top: {8}',
			$s.t, $s.p95, $s.max, $s.mean, $s.frames, $s.missed, $s.hitches, $(if ($s.vr -eq 1) { 'vr  ' } else { 'flat' }),
			(Format-ArxPerfTopPhases $s.Phases 3 0.05 '{0} {1:0.00}')))
	}

	$out.Add("Hitches ($($Summary.Hitches.Count)), frames over 1.5 expected frame times:")
	if (-not $Summary.Hitches.Count) { $out.Add('  none') }
	else { $out.Add('  by cause: ' + (($Summary.Causes.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join ', ')) }
	foreach ($h in $Summary.Hitches) {
		$out.Add([string]::Format($c, '  t={0,9:0.000} s  {1,7:0.0} ms ({2:0.0} slots)  cause {3,-9}  missed {4}  uploads {5}  | {6}',
			$h.t, $h.ms, $h.slots, $h.cause, $h.missed, $h.uploads, (Format-ArxPerfTopPhases $h.Phases 4)))
	}

	if ($Summary.Marks.Count) {
		$out.Add('Marks:')
		foreach ($m in $Summary.Marks) { $out.Add([string]::Format($c, '  t={0,9:0.000} s  {1}', $m.t, $m.Text)) }
	}
	$out
}
