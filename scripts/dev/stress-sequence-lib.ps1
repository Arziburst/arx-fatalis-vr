# The pieces of stress-sequence.ps1 (#566) that need no game: the sequence copy it runs (marks at every `motion
# reset`, optional stalls and edits, each line with the original line it is), the failure of a run read from its
# transcript, the records of arx.log, the frames of arx-perf.log inside a record's window, and the per-assert summary.
# Dot-source it:
#   . (Join-Path $PSScriptRoot 'stress-sequence-lib.ps1')
# Tested by test-stress-sequence.ps1 (fixture text, no game).

. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

$script:StressInv = [Globalization.CultureInfo]::InvariantCulture

# A line of a sequence without its trailing comment (" # ...", as xrs-lib.ps1 reads it), trimmed
function Get-StressCode([string]$Raw) { ($Raw -replace '\s+#(\s.*)?$', '').Trim() }

# The copy of a sequence a stress run executes. $Lines: the original's lines.
#   -Marks          every line that starts with `motion reset` gets `; mark stress-reset <record>` in its batch: the
#                   channel's mark lands in arx-perf.log (an M line) in the frame of the reset, so a record's window
#                   can be found in the performance log. <record> is the first `record <name>` on that line or on the
#                   lines after it, before the next `motion reset`. The mark is in the same batch: no round trip more.
#   -StallAfter     a regex: after every line whose code matches it a line `stall <StallMs>` (one forced long frame
#                   on the main thread, cause=channel, a little after the matched step)
#   -Edits          'old=>new': the first line whose code is exactly old becomes new (a negative control); an edit that
#                   matches no line throws
# Returns Lines, Origin (the original's line of each copy line, 0 for an added one) and Records (the marked ones).
function New-StressSequenceCopy([string[]]$Lines, [switch]$Marks, [string]$StallAfter = '', [int]$StallMs = 0, [string[]]$Edits = @()) {
	$edit = [ordered]@{}
	foreach ($e in $Edits) {
		if ($e -notmatch '^(.+?)=>(.+)$') { throw "an edit is 'old=>new', got '$e'" }
		$edit[$Matches[1].Trim()] = $Matches[2].Trim()
	}
	$used = @{}
	$out = [Collections.Generic.List[string]]::new()
	$origin = [Collections.Generic.List[int]]::new()
	$records = [Collections.Generic.List[string]]::new()
	for ($i = 0; $i -lt $Lines.Count; $i++) {
		$raw = $Lines[$i]
		$code = Get-StressCode $raw
		if ($edit.Contains($code) -and -not $used.ContainsKey($code)) {
			$used[$code] = $true
			$raw = $edit[$code]
			$code = Get-StressCode $raw
		}
		if ($Marks -and $code -match '^motion reset\b') {
			$name = $null
			for ($j = $i; $j -lt $Lines.Count -and -not $name; $j++) {
				$c = Get-StressCode $Lines[$j]
				if ($j -gt $i -and $c -match '^motion reset\b') { break }
				if ($c -notmatch '^[#@]' -and $c -match '\brecord\s+(\w+)') { $name = $Matches[1] }
			}
			if ($name) {
				$raw = $raw -replace '^(\s*)motion reset\b', "`$1motion reset; mark stress-reset $name"
				$records.Add($name)
			}
		}
		$out.Add($raw)
		$origin.Add($i + 1)
		if ($StallAfter -and $StallMs -gt 0 -and $code -and -not $code.StartsWith('#') -and $code -match $StallAfter) {
			$out.Add("stall $StallMs")
			$origin.Add(0)
		}
	}
	foreach ($old in $edit.Keys) {
		if (-not $used.ContainsKey($old)) { throw "the edit '$old=>$($edit[$old])' matches no line of the sequence" }
	}
	[pscustomobject]@{ Lines = $out.ToArray(); Origin = $origin.ToArray(); Records = $records.ToArray() }
}

# The failure of one run from its smoke-test transcript ("=> FAIL sequence <name>: line <n> '<step>': <message>"),
# with the original's line (via $Origin of the copy) and, for an assert or an until, the path, the value read, the
# operator and the expected value. $null when the transcript names no sequence failure.
function Get-StressFailure([string]$Transcript, [int[]]$Origin = @()) {
	$m = [regex]::Match($Transcript, "=> FAIL sequence [^:]+: line (\d+) '([^']*)': ([^\r\n]*)")
	if (-not $m.Success) { return $null }
	$copyLine = [int]$m.Groups[1].Value
	$line = if ($Origin.Count) { if ($copyLine -ge 1 -and $copyLine -le $Origin.Count) { $Origin[$copyLine - 1] } else { 0 } } else { $copyLine }
	# The reasons of smoke-test.ps1 are joined by '; ': the sequence's message ends where the next reason starts
	$message = ($m.Groups[3].Value -split '; (?=sequence |the game |exit |crash|no )')[0].Trim()
	$f = [ordered]@{ Line = $line; CopyLine = $copyLine; Step = $m.Groups[2].Value; Message = $message
		Kind = 'other'; Path = ''; Actual = ''; Op = ''; Expected = '' }
	if ($message -match '^ASSERT FAILED: (\S+) \((.*)\) (eq|ne|gt|ge|lt|le) (\S+)$') {
		$f.Kind = 'assert'; $f.Path = $Matches[1]; $f.Actual = $Matches[2]; $f.Op = $Matches[3]; $f.Expected = $Matches[4]
	} elseif ($message -match '^UNTIL FAILED: (\S+) \((.*)\) never became (eq|ne|gt|ge|lt|le) (\S+)') {
		$f.Kind = 'until'; $f.Path = $Matches[1]; $f.Actual = $Matches[2]; $f.Op = $Matches[3]; $f.Expected = $Matches[4]
	} elseif ($message -match '^LOG FAILED') {
		$f.Kind = 'log'
	}
	[pscustomobject]$f
}

# The records a run took, in order, from arx.log's "[channel] record <name>: {json}" lines: Name and Values (the
# record's JSON object). A record taken twice keeps its last values.
function Get-StressRecords([string]$LogText) {
	$records = [ordered]@{}
	foreach ($m in [regex]::Matches($LogText, '\[channel\] record (\w+): (\{[^\r\n]*\})')) {
		try { $values = $m.Groups[2].Value | ConvertFrom-Json } catch { continue }
		$records[$m.Groups[1].Value] = [pscustomobject]@{ Name = $m.Groups[1].Value; Values = $values }
	}
	@($records.Values)
}

# A record's window in the performance log: from the `stress-reset <record>` mark to the mark plus the record's
# seconds (the frame clock since the reset). Start, End, the seconds lines it touches, its hitches (an H line's t is
# its frame's start), the missed slots of those hitches and the longest frame. $null when the log has no such mark.
function Get-StressWindow($Perf, [string]$Record, [double]$Seconds) {
	$mark = @($Perf.Marks | Where-Object { $_.Text -eq "stress-reset $Record" } | Select-Object -Last 1)
	if (-not $mark) { return $null }
	$start = [double]$mark[0].t
	$end = $start + $Seconds
	$hitches = @($Perf.Hitches | Where-Object { [double]$_.t -ge $start - 0.0005 -and [double]$_.t -le $end + 0.0005 })
	$secondLines = @($Perf.Seconds | Where-Object { [double]$_.t -ge [Math]::Floor($start) -and [double]$_.t -le [Math]::Floor($end) })
	$missed = 0.0
	$longest = 0.0
	foreach ($h in $hitches) { $missed += [double]$h.missed; $longest = [Math]::Max($longest, [double]$h.ms) }
	[pscustomobject]@{ Record = $Record; Start = $start; End = $end; Hitches = $hitches; Seconds = $secondLines; Missed = $missed; Longest = $longest }
}

# One line per hitch of a window: its time after the reset, length, slots, missed slots, cause, top phases
function Format-StressWindow($Window, [int]$MaxHitches = 12) {
	$lines = @()
	$lines += [string]::Format($script:StressInv, '{0}: window t={1:0.000}-{2:0.000} s, {3} hitch(es), {4:0} missed slot(s){5}',
		$Window.Record, $Window.Start, $Window.End, $Window.Hitches.Count, $Window.Missed,
		$(if ($Window.Hitches.Count) { [string]::Format($script:StressInv, ', longest frame {0:0.0} ms', $Window.Longest) } else { '' }))
	# The longest ones when there are many (a starved PC has a hitch every frame), in time order
	$shown = @($Window.Hitches | Sort-Object { [double]$_.ms } -Descending | Select-Object -First $MaxHitches | Sort-Object { [double]$_.t })
	foreach ($h in $shown) {
		$lines += [string]::Format($script:StressInv, '  +{0:0.000} s  {1,6:0.0} ms  {2,4:0.0} slots  missed {3,2:0}  cause {4,-8} | {5}',
			[double]$h.t - $Window.Start, [double]$h.ms, [double]$h.slots, [double]$h.missed, $h.cause, (Format-ArxPerfTopPhases $h.Phases 4))
	}
	if ($Window.Hitches.Count -gt $shown.Count) { $lines += "  ... and $($Window.Hitches.Count - $shown.Count) shorter hitch(es)" }
	foreach ($s in $Window.Seconds) {
		$lines += [string]::Format($script:StressInv, '  second t={0}: {1} frames at {2:0.0} Hz, missed {3}, max {4:0.0} ms',
			$s.t, $s.frames, [double]$s.hz, $s.missed, [double]$s.max)
	}
	$lines
}

# The runs' failures by assert: one group per original line, with how many runs failed there and every value read.
# $Results: objects with Run, Verdict (PASS / FAIL / SKIPPED) and Failure (Get-StressFailure's object or $null).
function Get-StressAssertTable($Results) {
	$groups = [ordered]@{}
	foreach ($r in @($Results | Where-Object Verdict -eq 'FAIL')) {
		$f = $r.Failure
		$key = if ($f) { "$($f.Line)" } else { 'none' }
		if (-not $groups.Contains($key)) {
			$groups[$key] = [pscustomobject]@{ Line = $(if ($f) { $f.Line } else { -1 }); Step = $(if ($f) { $f.Step } else { '(no sequence step failed: see the transcript)' })
				Count = 0; Values = [Collections.Generic.List[string]]::new(); Runs = [Collections.Generic.List[int]]::new() }
		}
		$g = $groups[$key]
		$g.Count++
		$g.Runs.Add([int]$r.Run)
		if ($f -and $f.Kind -in 'assert', 'until') { $g.Values.Add("$($f.Actual) $($f.Op) $($f.Expected)") }
		elseif ($f) { $g.Values.Add($f.Message) }
	}
	@($groups.Values | Sort-Object @{ Expression = 'Count'; Descending = $true }, Line)
}

function Format-StressAssertTable($Table, [int]$Runs) {
	if (-not @($Table | Where-Object { $_ }).Count) { return @("no assert failed in $Runs run(s)") }
	$lines = @()
	foreach ($g in $Table) {
		$where = if ($g.Line -gt 0) { "line $($g.Line)" } elseif ($g.Line -eq 0) { 'added step' } else { 'no step' }
		$lines += "$($g.Count) of $Runs  $where  $($g.Step)"
		$lines += "    runs $($g.Runs -join ', '): $($g.Values -join ' | ')"
	}
	$lines
}

# Why the PC is not free for a stress run, or '' when it is: a CI job (the runner marker, launch-lib.ps1's
# Get-ArxRunnerMarker), another game (an arx.exe that is not from $OwnBuildDir), another copy's ci-local / ci-smoke or
# build (a pwsh running ci-local.ps1, ci-smoke.ps1 or build-windows.ps1). $Processes: Win32_Process-like objects
# (Name, ProcessId, CommandLine, ExecutablePath); $OwnPids are never counted (this script, its load and the load's children).
function Get-StressBusyReason($Processes, $Marker, [string]$OwnBuildDir = '', [int[]]$OwnPids = @()) {
	if ($Marker) { return "a CI job runs ($($Marker.What), pid $($Marker.Pid))" }
	$reasons = [Collections.Generic.List[string]]::new()
	foreach ($p in @($Processes)) {
		if ([int]$p.ProcessId -in $OwnPids) { continue }
		if ($p.Name -eq 'arx.exe') {
			if ($OwnBuildDir -and $p.ExecutablePath -and $p.ExecutablePath.StartsWith($OwnBuildDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }
			$reasons.Add("another game (arx.exe pid $($p.ProcessId))")
		} elseif ($p.Name -eq 'pwsh.exe' -and "$($p.CommandLine)" -match '\b(ci-local|ci-smoke|build-windows)\.ps1') {
			$reasons.Add("$($Matches[1]).ps1 (pid $($p.ProcessId))")
		} elseif ($p.Name -in 'cl.exe', 'link.exe', 'ninja.exe', 'MSBuild.exe') {
			# A compile however it was started (a worker's cmake --build too); this script's own build load is in $OwnPids
			$reasons.Add("a compile ($($p.Name) pid $($p.ProcessId))")
		}
	}
	@($reasons | Select-Object -Unique) -join ', '
}
