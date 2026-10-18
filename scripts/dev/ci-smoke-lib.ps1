# The testable parts of ci-smoke.ps1 (#522): the order of the phases, the selection of a pull request from the impact
# map, the lane scheduler, the lane count from the game lock, the durations file, the evidence kept on the runner and
# the run digest of a phase. Dot-source it after
# test-lib.ps1 (the lock functions of launch-lib.ps1 are used at call time):
#   . (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')
# scripts/dev/test-ci-smoke.ps1 tests every function here without a game.

# --- The evidence kept on the runner --------------------------------------------------------------------------------

# A green CI run uploads nothing (#522): its evidence stays on the runner in <root>\<run id>-<attempt>, the last 10 runs
function Get-ArxCiEvidenceRoot { Join-Path (Get-ArxAppDataDir) 'ci-evidence' }

# Keep the $Keep newest run folders of $Root (by creation time), delete the rest; returns the names deleted. Files in
# the root (ci-durations.json) are not runs and stay.
function Remove-ArxOldCiEvidence([string]$Root, [int]$Keep = 10) {
	$dirs = @(Get-ChildItem -LiteralPath $Root -Directory -ErrorAction SilentlyContinue | Sort-Object CreationTimeUtc, Name -Descending)
	foreach ($d in ($dirs | Select-Object -Skip $Keep)) {
		Remove-Item -LiteralPath $d.FullName -Recurse -Force -ErrorAction SilentlyContinue
		$d.Name
	}
}

# The root of the per-sequence user directories of one working copy (#296): ci-userdirs\<folder>-<hash>, like the
# copy's build, test channel and user directory. Without the key every copy on the PC shares one root, and a run of one
# copy deletes the directory a sequence of another copy is running in. Within one run the lanes never share a directory:
# one per sequence name, and a read-back reuses its own sequence's after it.
function Get-ArxCiUserDirRoot([string]$SourceDir) { Join-Path (Get-ArxAppDataDir) "ci-userdirs\$(Get-WorkingCopyKey $SourceDir)" }

# --- The durations of the last run ----------------------------------------------------------------------------------

# ci-durations.json next to the evidence: { "<phase>": <seconds>, ... } as the last run measured them. Missing or
# unreadable: an empty table (every phase is "unknown" and sorts as the longest).
function Read-ArxPhaseDurations([string]$Path) {
	$table = @{}
	if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $table }
	try {
		$json = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
		foreach ($p in $json.PSObject.Properties) { $table[$p.Name] = [double]$p.Value }
	} catch {
		Write-Warning "ignoring $Path ($($_.Exception.Message)): every phase counts as unknown"
	}
	$table
}

# Write the durations of the phases that ran ($Results with a Seconds and a PASS / FAIL verdict) over the old table: a
# phase that did not run this time (NOT RUN, SKIPPED, a sequence whose save was missing) keeps its last measured time
function Write-ArxPhaseDurations([string]$Path, [hashtable]$Old, $Results) {
	$table = @{}
	if ($Old) { foreach ($k in $Old.Keys) { $table[$k] = $Old[$k] } }
	foreach ($r in @($Results)) {
		if ($r.Verdict -in 'PASS', 'FAIL' -and $null -ne $r.PSObject.Properties['Seconds'] -and $null -ne $r.Seconds) { $table[$r.Name] = [Math]::Round([double]$r.Seconds, 1) }
	}
	$sorted = [ordered]@{}
	foreach ($k in ($table.Keys | Sort-Object)) { $sorted[$k] = $table[$k] }
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	$tmp = "$Path.tmp"
	[IO.File]::WriteAllText($tmp, ($sorted | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
	Move-Item -LiteralPath $tmp -Destination $Path -Force
}

# --- The order of the sequences -------------------------------------------------------------------------------------

# The files a pull request changes: git diff --name-only --no-renames <base>...HEAD (a rename lists the old and the new
# path, so the map's lines for both apply). A shallow checkout (CI's) has no merge base: then the two-dot diff against
# the base, which for the merge commit CI checks out is the same list. No base (a push to main, a manual run): nothing
# counts as changed.
function Get-ArxChangedFiles([string]$SourceDir, [string]$Base) {
	$set = Get-ArxChangedFileSet $SourceDir $Base
	if ($Base -and -not $set.Ok) { Write-Warning "cannot diff against ${Base}: no sequence counts as changed" }
	$set.Files
}

# The same, telling an empty change from a diff that could not be made: { Ok = the diff ran; Files }. The selection of
# a pull request (#522) runs everything when it does not know what changed.
function Get-ArxChangedFileSet([string]$SourceDir, [string]$Base) {
	if (-not $Base) { return [pscustomobject]@{ Ok = $false; Files = @() } }
	foreach ($range in @("$Base...HEAD"), @($Base, 'HEAD')) {
		$files = @(git -C $SourceDir diff --name-only --no-renames @range 2>$null)
		if ($LASTEXITCODE -eq 0) { $global:LASTEXITCODE = 0; return [pscustomobject]@{ Ok = $true; Files = $files } }
	}
	$global:LASTEXITCODE = 0
	[pscustomobject]@{ Ok = $false; Files = @() }
}

# The run order of the sequence phases. $Units: one per sequence, in the serial (name) order, each
# { Phases = the phase names in run order (the sequence, then its read-back); Paths = their .xrs files, repo-relative }.
# A unit one of whose files is in $Changed goes first; then by the last run's duration ($Durations, phase name ->
# seconds; a unit's is the sum of its phases), longest first; a unit whose sequence has no recorded duration counts as
# the longest. Ties keep the serial order. Returns the phase names, each read-back right after its sequence.
function Get-ArxPhaseOrder($Units, [string[]]$Changed = @(), [hashtable]$Durations = @{}) {
	$changedSet = @{}
	foreach ($c in @($Changed)) { if ($c) { $changedSet[($c -replace '\\', '/').ToLowerInvariant()] = $true } }
	$i = 0
	$rows = foreach ($u in @($Units)) {
		$phases = @($u.Phases)
		$isChanged = [bool]@($u.Paths | Where-Object { $changedSet.ContainsKey(("$_" -replace '\\', '/').ToLowerInvariant()) }).Count
		$duration = [double]::PositiveInfinity
		if ($Durations.ContainsKey($phases[0])) {
			$duration = 0.0
			foreach ($p in $phases) { if ($Durations.ContainsKey($p)) { $duration += [double]$Durations[$p] } }
		}
		[pscustomobject]@{ Phases = $phases; Changed = $isChanged; Duration = $duration; Index = $i++ }
	}
	@($rows | Sort-Object @{ Expression = 'Changed'; Descending = $true }, @{ Expression = 'Duration'; Descending = $true }, @{ Expression = 'Index'; Descending = $false } |
		ForEach-Object { $_.Phases })
}

# --- The selection of a pull request (#522) -------------------------------------------------------------------------

# The checks that are phases of their own, not sequences, and may stand in the impact map like a sequence
$ArxImpactChecks = @('smoke-test', 'spell-drawcheck')

# The impact map, scripts\dev\xrs\impact.txt: which game checks a change of a path can break. A line is
#   core: <name> [<name> ...]                       run on every pull request (several core: lines add up)
#   <path glob> -> <name> [<name> ...]              a changed path that matches selects these
#   <path glob> -> *                                ... selects everything (the harness every check runs on)
#   <path glob> -> core                             ... is covered by the core alone (docs, the map itself)
# A name is a sequence (the .xrs file name without the extension; its read-back follows it) or one of
# $ArxImpactChecks. A glob is a repository path with / and the wildcards of -like (* also crosses folders), any case.
# '#' starts a comment. Returns { Path; Core; Rules = { Glob; Targets; Line; Pattern }; Errors } - a line that is
# none of these is an error, never skipped in silence.
function Read-ArxImpactMap([string]$Path) {
	$core = [Collections.Generic.List[string]]::new()
	$rules = [Collections.Generic.List[object]]::new()
	$errors = [Collections.Generic.List[string]]::new()
	$n = 0
	foreach ($raw in [IO.File]::ReadAllLines($Path)) {
		$n++
		$line = ($raw -replace '(^|\s)#.*$', '').Trim()
		if (-not $line) { continue }
		if ($line -match '^core:(.*)$') {
			foreach ($name in -split $Matches[1]) { $core.Add($name) }
			continue
		}
		if ($line -match '^(\S+)\s+->(.*)$') {
			$glob = $Matches[1]
			$targets = @(-split $Matches[2])
			if (-not $targets) { $errors.Add("line ${n}: '$line' names nothing (a path the core covers says '-> core')"); continue }
			if ($targets.Count -gt 1 -and ($targets -contains '*' -or $targets -contains 'core')) { $errors.Add("line ${n}: '$line': '*' and 'core' stand alone"); continue }
			$rules.Add([pscustomobject]@{ Glob = $glob; Targets = $targets; Line = $n; Pattern = [WildcardPattern]::new($glob, [Management.Automation.WildcardOptions]::IgnoreCase) })
			continue
		}
		$errors.Add("line ${n}: '$line' is neither 'core: <name> ...' nor '<path glob> -> <name> ...'")
	}
	[pscustomobject]@{ Path = $Path; Core = @($core); Rules = @($rules); Errors = @($errors) }
}

# The sequence a changed .xrs file selects by itself: scripts/dev/xrs/<name>.xrs and its read-back
# scripts/dev/xrs/readback/<name>-readback.xrs both give <name>; any other path gives $null
function Get-ArxChangedSequenceName([string]$Path) {
	$p = $Path -replace '\\', '/'
	if ($p -match '^scripts/dev/xrs/readback/(.+)-readback\.xrs$') { return $Matches[1] }
	if ($p -match '^scripts/dev/xrs/([^/]+)\.xrs$') { return $Matches[1] }
	$null
}

# The game checks of a change set: the core, every name of a rule whose glob matches a changed path, and the changed or
# added .xrs files themselves (a read-back selects its sequence). A changed path that no rule matches (and that is not
# an .xrs) or a rule '-> *' makes it Full: everything runs. Returns
#   { Full; Names (sorted, without duplicates); Reasons (name -> the first reason: 'core' or the path); FullBecause
#     (the lines that made it Full); Unmapped (the paths no rule matched) }
# Names lists the selection even when Full, for the log.
function Get-ArxImpactSelection($Map, [string[]]$Changed = @()) {
	$reasons = @{}
	foreach ($name in $Map.Core) { if (-not $reasons.ContainsKey($name)) { $reasons[$name] = 'core' } }
	$fullBecause = [Collections.Generic.List[string]]::new()
	$unmapped = [Collections.Generic.List[string]]::new()
	foreach ($raw in @($Changed)) {
		if (-not $raw) { continue }
		$path = $raw -replace '\\', '/'
		$own = Get-ArxChangedSequenceName $path
		if ($own -and -not $reasons.ContainsKey($own)) { $reasons[$own] = $path }
		$matched = [bool]$own
		foreach ($rule in $Map.Rules) {
			if (-not $rule.Pattern.IsMatch($path)) { continue }
			$matched = $true
			if ($rule.Targets[0] -eq '*') { $fullBecause.Add("$path (line $($rule.Line): $($rule.Glob) -> *)"); continue }
			if ($rule.Targets[0] -eq 'core') { continue }
			foreach ($name in $rule.Targets) { if (-not $reasons.ContainsKey($name)) { $reasons[$name] = $path } }
		}
		if (-not $matched) {
			$unmapped.Add($path)
			$fullBecause.Add("$path (no rule matches it)")
		}
	}
	[pscustomobject]@{
		Full = [bool]$fullBecause.Count; Names = @($reasons.Keys | Sort-Object); Reasons = $reasons
		FullBecause = @($fullBecause); Unmapped = @($unmapped)
	}
}

# The coverage check of the map: its syntax errors, a name that is neither a sequence of $Sequences nor one of
# $ArxImpactChecks, a glob listed twice, and a sequence or check that is neither in the core nor named by a rule (it
# would run only in the full suite - a rule '-> *' does not count). Returns the problems, one line each; none: @().
function Test-ArxImpactMap($Map, [string[]]$Sequences) {
	$problems = [Collections.Generic.List[string]]::new()
	foreach ($e in $Map.Errors) { $problems.Add($e) }
	$known = @{}
	foreach ($name in @($Sequences) + $ArxImpactChecks) { $known[$name] = $true }
	$reached = @{}
	foreach ($name in $Map.Core) {
		if (-not $known.ContainsKey($name)) { $problems.Add("core: '$name' is no sequence (scripts/dev/xrs/$name.xrs) and no check") }
		$reached[$name] = $true
	}
	$globs = @{}
	foreach ($rule in $Map.Rules) {
		if ($globs.ContainsKey($rule.Glob.ToLowerInvariant())) { $problems.Add("line $($rule.Line): the glob $($rule.Glob) is listed twice (also line $($globs[$rule.Glob.ToLowerInvariant()]))") }
		else { $globs[$rule.Glob.ToLowerInvariant()] = $rule.Line }
		if ($rule.Targets[0] -in '*', 'core') { continue }
		foreach ($name in $rule.Targets) {
			if (-not $known.ContainsKey($name)) { $problems.Add("line $($rule.Line): '$name' is no sequence (scripts/dev/xrs/$name.xrs) and no check") }
			$reached[$name] = $true
		}
	}
	foreach ($name in $known.Keys | Sort-Object) {
		if (-not $reached.ContainsKey($name)) { $problems.Add("'$name' is in no core: line and named by no rule: a pull request would run it only when everything runs") }
	}
	@($problems)
}

# The sequence names of a folder of .xrs files (the read-backs in readback\ follow their sequence and are not listed)
function Get-ArxSequenceNames([string]$XrsDir) {
	@(Get-ChildItem -LiteralPath $XrsDir -Filter '*.xrs' -File | ForEach-Object BaseName | Sort-Object)
}

# --- The lane count -------------------------------------------------------------------------------------------------

# The games of the PC that are not this job's: game slots held by a lock whose copy= is not $Copy, and arx.exe
# processes outside the lock that do not run from $BuildDir. One line each.
function Get-ArxForeignGames([string]$Copy, [string]$BuildDir) {
	$foreign = @()
	foreach ($line in @(Get-ArxLockHolders)) {
		if ($line -notmatch '^game-\d+\.lock: ') { continue }
		$ours = $line -match ' copy=(.+?) branch=' -and $Matches[1] -eq $Copy
		if (-not $ours) { $foreign += $line }
	}
	foreach ($game in @(Get-ArxGamesOutsideLock)) {
		$ours = $game.Path -and $BuildDir -and $game.Path.StartsWith($BuildDir, [StringComparison]::OrdinalIgnoreCase)
		if (-not $ours) { $foreign += "arx.exe pid $($game.Id) outside the lock $($game.Path)" }
	}
	$foreign
}

# How many lanes may run now: one while another game of the PC (the owner's, a worker's) holds a slot, else $Lanes
function Get-ArxLaneCount([int]$Lanes, [string]$Copy, [string]$BuildDir) {
	if (@(Get-ArxForeignGames $Copy $BuildDir).Count) { 1 } else { $Lanes }
}

# --- The scheduler --------------------------------------------------------------------------------------------------

# Run $Phases on up to $Lanes lanes at once, in the given order, every phase exactly once. A phase:
#   Name; DependsOn   names that must have finished first (a name not among $Phases counts as finished)
#   Exclusive         a key: two phases with the same key never run at the same time ($null: none)
#   Solo              runs alone (#23, a sequence that asserts pacing): nothing else is started while it is due until the
#                     running phases ended, and nothing while it runs
#   Gate              optional { param($Done, $Phase) } asked when the phase is due ($Done: name -> result of the finished):
#                     $null runs it, 'omit' drops it without a result, an object is its result without running it
# $Start { param($Phase, $Lane) } starts a phase and returns its job; $Finish { param($Phase, $Lane, $Job, $Seconds) } is called
# when the job left the Running state and returns the phase's result (an object with Name, Exit, Verdict).
# $LaneLimit { } is asked before a phase goes to a lane while another lane is busy (at most every $LimitCacheMs).
# Returns the results in the order the phases finished, each with Lane, Started and Seconds added.
function Invoke-ArxPhaseSchedule(
	$Phases, [int]$Lanes = 2, [scriptblock]$Start, [scriptblock]$Finish, [scriptblock]$LaneLimit,
	[int]$PollMs = 200, [int]$LimitCacheMs = 2000
) {
	$queue = [Collections.Generic.List[object]]::new()
	foreach ($p in @($Phases)) { $queue.Add($p) }
	$known = @{}
	foreach ($p in $queue) {
		if ($known.ContainsKey($p.Name)) { throw "phase '$($p.Name)' is listed twice" }
		$known[$p.Name] = $true
	}
	$busy = @{}          # lane -> { Phase; Job; Started }
	$finished = @{}      # name -> result ($null when omitted)
	$outcome = [Collections.Generic.List[object]]::new()
	$limitValue = $Lanes
	$limitAt = [DateTime]::MinValue
	while ($queue.Count -or $busy.Count) {
		foreach ($lane in @($busy.Keys)) {
			$slot = $busy[$lane]
			if ($slot.Job.State -in 'NotStarted', 'Running') { continue }
			$seconds = ((Get-Date) - $slot.Started).TotalSeconds
			$result = & $Finish $slot.Phase $lane $slot.Job $seconds
			if (-not $result) { $result = [pscustomobject]@{ Name = $slot.Phase.Name; Exit = 1; Verdict = 'FAIL' } }
			$result | Add-Member -NotePropertyName Lane -NotePropertyValue $lane -Force
			$result | Add-Member -NotePropertyName Started -NotePropertyValue $slot.Started -Force
			$result | Add-Member -NotePropertyName Seconds -NotePropertyValue $seconds -Force
			$finished[$slot.Phase.Name] = $result
			$outcome.Add($result)
			$busy.Remove($lane)
		}
		$progress = $true
		while ($progress -and $queue.Count) {
			$progress = $false
			$free = @(1..$Lanes | Where-Object { -not $busy.ContainsKey($_) })[0]
			if (-not $free) { break }
			$running = @($busy.Values | ForEach-Object { $_.Phase })
			$next = $null
			# A solo phase runs alone: while one runs nothing starts
			if (@($running | Where-Object { $_.Solo })) { break }
			foreach ($p in $queue) {
				$waits = @($p.DependsOn | Where-Object { $_ -and $known.ContainsKey($_) -and -not $finished.ContainsKey($_) })
				if ($waits) { continue }
				if ($p.Exclusive -and @($running | Where-Object { $_.Exclusive -eq $p.Exclusive })) { continue }
				# A solo phase that is due holds the queue until the lanes are empty - the others wait behind it
				if ($p.Solo -and $running.Count) { break }
				$next = $p
				break
			}
			if (-not $next) { break }
			if ($busy.Count) {
				if ($LaneLimit -and ((Get-Date) - $limitAt).TotalMilliseconds -ge $LimitCacheMs) {
					$limitValue = [Math]::Max(1, [Math]::Min($Lanes, [int](& $LaneLimit)))
					$limitAt = Get-Date
				}
				if ($busy.Count -ge $limitValue) { break }
			}
			$null = $queue.Remove($next)
			$progress = $true
			$gate = if ($next.Gate) { & $next.Gate $finished $next } else { $null }
			if ($gate -is [string] -and $gate -eq 'omit') { $finished[$next.Name] = $null; continue }
			if ($gate) {
				$finished[$next.Name] = $gate
				$outcome.Add($gate)
				continue
			}
			$busy[$free] = [pscustomobject]@{ Phase = $next; Job = (& $Start $next $free); Started = Get-Date }
		}
		if ($busy.Count) { Start-Sleep -Milliseconds $PollMs }
		elseif ($queue.Count -and -not $progress) {
			throw "cannot schedule the phases $(($queue | ForEach-Object Name) -join ', '): they wait for each other"
		}
	}
	$outcome
}

# --- The evidence of one phase --------------------------------------------------------------------------------------

# The pid of the last game a phase ran in the test channel $TestDir: status.json's pid, else the launch marker's
function Get-ArxPhaseGamePid([string]$TestDir) {
	try {
		$status = Get-Content -LiteralPath (Join-Path $TestDir 'status.json') -Raw -ErrorAction Stop | ConvertFrom-Json
		if ($status.pid) { return [int]$status.pid }
	} catch { }
	$marker = Join-Path $TestDir 'launched.txt'
	if ((Test-Path -LiteralPath $marker) -and ((Get-Content -LiteralPath $marker -Raw) -match 'pid=(\d+)')) { return [int]$Matches[1] }
	$null
}

# The run digest folder of the game $GamePid (log-digest-lib.ps1 names it <yyyyMMdd-HHmmss>-pid<pid>), the newest when a
# pid came back; $null when there is none. Two lanes write digests at once, so the newest folder is not the phase's.
function Find-ArxRunDigest([string]$RunsDir, $GamePid) {
	if (-not $GamePid) { return $null }
	Get-ChildItem -LiteralPath $RunsDir -Directory -Filter "*-pid$GamePid" -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
}

# --- The save library phase -----------------------------------------------------------------------------------------

# The test save library of this account (D-023, derived from the game's data, never committed): checked by a fresh game
# that loads every save (-Verify, one launch); generated again when it is missing, lacks a save a sequence names, or no
# longer loads (an engine save format change). Runs in a lane's job; its exit code is make-test-saves.ps1's.
function Invoke-ArxSaveLibraryPhase([string]$ScriptDir, [string]$Library, [string[]]$Needed, [int]$LockTimeoutSec, [string]$DataDir) {
	$verifyArgs = @{ Library = $Library; LockTimeoutSec = $LockTimeoutSec }
	if ($DataDir) { $verifyArgs.DataDir = $DataDir }
	$have = @()
	$manifest = Join-Path $Library 'manifest.json'
	if (Test-Path $manifest) { $have = @(Get-Content -Raw $manifest | ConvertFrom-Json | Where-Object { Test-Path (Join-Path $Library "$($_.name)\gsave.sav") } | ForEach-Object name) }
	$missing = @($Needed | Where-Object { $_ -notin $have })
	if ($missing) { Write-Host "ci-smoke: the save library $Library lacks $($missing -join ', ') - generating it" }
	else {
		& (Join-Path $ScriptDir 'make-test-saves.ps1') -Verify @verifyArgs
		if ($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq 2) { return }
		Write-Host 'ci-smoke: the saves of the library no longer load - generating it again'
	}
	& (Join-Path $ScriptDir 'make-test-saves.ps1') @verifyArgs
}
