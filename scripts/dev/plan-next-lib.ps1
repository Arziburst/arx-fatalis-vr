#Requires -Version 7
# The rules of plan-next.ps1 (#187, D-021): which tasks of the MVP plan may start now. Pure functions - they take the text of
# docs/vr/MVP.md, the board items, the open pull requests and the running agents as data and read nothing themselves, so
# scripts/dev/test-plan-next.ps1 can run them on fixtures. Dot-source from plan-next.ps1:
#   . (Join-Path $PSScriptRoot 'plan-next-lib.ps1')
#
# Where each fact comes from:
#   dependencies, waves, queue order   docs/vr/MVP.md ("Dependencies" and "Waves" tables, "Task queue" list)
#   status, labels (area:*, lane:vr,   the board (a status) and the issues (labels); the table's Area / Lane columns are
#   needs-design, model:fable)         the fallback when an issue has no label yet
#   running work                       board "In Progress", live background sessions named "ARXVR-<n> ...", open task PRs

# --- MVP.md ----------------------------------------------------------------------------------------------------------------

# The rows of one markdown table under "## Heading" or "### Heading": each row is an array of trimmed cell texts; header
# and separator rows are included (the callers skip the header by its first cell).
function Get-MvpTableRows([string]$Text, [string]$Heading) {
	$m = [regex]::Match($Text, '(?ms)^#{2,3}\s+' + [regex]::Escape($Heading) + '\s*$(.*?)(?=^#{2,3}\s|\z)')
	if (-not $m.Success) { throw "docs/vr/MVP.md has no '$Heading' section" }
	foreach ($line in ($m.Groups[1].Value -split "\r?\n")) {
		$t = $line.Trim()
		if (-not $t.StartsWith('|')) { continue }
		$cells = @($t.Trim('|') -split '\|' | ForEach-Object { $_.Trim() })
		if ($cells.Count -lt 2 -or -not @($cells | Where-Object { $_ -notmatch '^:?-{3,}:?$' })) { continue } # the |---|---| row
		, $cells
	}
}

# The issue numbers of the "Task queue" list, in order (the same list board.ps1 Get-MvpQueue reads from the file).
function Get-MvpQueueNumbers([string]$Text) {
	$section = [regex]::Match($Text, '(?ms)^## Task queue\s*$(.*?)(?=^## [^#]|\z)').Groups[1].Value
	@([regex]::Matches($section, '(?m)^\s*\d+\.\s+#(\d+)') | ForEach-Object { [int]$_.Groups[1].Value })
}

# One cell of the "Depends on" column: the issue numbers it names.
#   "—" or "— (anything)"        no dependency (numbers in the remark are not dependencies)
#   "everything else"            every other task of the plan (#77, the video)
#   "#74, #166 (no longer #36)"  a remark "no longer #n" is dropped; other remarks keep their numbers ("(accepted via #112)")
#   text without numbers         none ("late, once the tooling settles")
function ConvertFrom-MvpDependencyCell([string]$Cell) {
	$c = $Cell.Trim()
	$r = [pscustomobject]@{ Numbers = @(); Everything = $false }
	if ($c -match '^[—–-](\s|$)') { return $r }
	if ($c -match '^everything else') { $r.Everything = $true; return $r }
	$c = [regex]::Replace($c, '\([^)]*\bno longer\b[^)]*\)', '')
	$r.Numbers = @([regex]::Matches($c, '#(\d+)') | ForEach-Object { [int]$_.Groups[1].Value } | Select-Object -Unique)
	$r
}

# The plan of MVP.md as data: Tasks (Number, Area, Lane, DependsOn, Wave, Note) sorted by number, and Queue (the order).
#   Dependencies table: "| #n, #m text | area | lane | depends on |" - the leading issue numbers are the row's tasks.
#   Waves table: "| **w** - title | VR lane cells | plain lane cells |" - every issue number in the two lane cells is in wave w
#   (the first wave that names it); "#a -> #b" inside a cell means b runs after a, which counts as a dependency of b.
function Read-MvpPlan([string]$Text) {
	$tasks = @{}
	$get = {
		param([int]$n)
		if (-not $tasks.ContainsKey($n)) {
			$tasks[$n] = [pscustomobject]@{ Number = $n; Area = $null; Lane = $null; DependsOn = [Collections.Generic.List[int]]::new(); Wave = $null; Note = ''; Everything = $false }
		}
		$tasks[$n]
	}
	foreach ($cells in @(Get-MvpTableRows $Text 'Dependencies')) {
		if ($cells.Count -lt 4 -or $cells[0] -eq 'Task') { continue }
		$lead = [regex]::Match($cells[0], '^((?:#\d+\s*,?\s*)+)')
		if (-not $lead.Success) { continue }
		$deps = ConvertFrom-MvpDependencyCell $cells[3]
		foreach ($m in [regex]::Matches($lead.Groups[1].Value, '#(\d+)')) {
			$n = [int]$m.Groups[1].Value
			$t = & $get $n
			$t.Area = $cells[1]; $t.Lane = $cells[2]; $t.Note = $cells[3]; $t.Everything = $deps.Everything
			foreach ($d in $deps.Numbers) { if ($d -ne $n -and -not $t.DependsOn.Contains($d)) { $t.DependsOn.Add($d) } }
		}
	}
	foreach ($cells in @(Get-MvpTableRows $Text 'Waves')) {
		$w = [regex]::Match($cells[0], '^\*\*(\d+)\*\*')
		if (-not $w.Success -or $cells.Count -lt 2) { continue }
		foreach ($cell in $cells[1..($cells.Count - 1)]) {
			$c = [regex]::Replace($cell, '\*?\([^)]*\)\*?', '') # "*(after the owner's decisions on #118 / #120)*" is a remark, not a member
			$prev = $null; $after = $false
			foreach ($tok in [regex]::Matches($c, '#(\d+)|→')) {
				if ($tok.Value -eq '→') { $after = $true; continue }
				$n = [int]$tok.Groups[1].Value
				$t = & $get $n
				if ($null -eq $t.Wave) { $t.Wave = [int]$w.Groups[1].Value }
				if ($after -and $null -ne $prev -and $prev -ne $n -and -not $t.DependsOn.Contains($prev)) { $t.DependsOn.Add($prev) }
				$after = $false; $prev = $n
			}
		}
	}
	foreach ($t in @($tasks.Values | Where-Object Everything)) {
		foreach ($o in $tasks.Keys) { if ($o -ne $t.Number -and -not $t.DependsOn.Contains([int]$o)) { $t.DependsOn.Add([int]$o) } }
	}
	[pscustomobject]@{
		Tasks = @($tasks.Values | Sort-Object Number | ForEach-Object {
			[pscustomobject]@{ Number = $_.Number; Area = $_.Area; Lane = $_.Lane; DependsOn = @($_.DependsOn); Wave = $_.Wave; Note = $_.Note }
		})
		Queue = @(Get-MvpQueueNumbers $Text)
	}
}

# --- The rules -------------------------------------------------------------------------------------------------------------

# Area names ("render", "build", ...) of a task: the issue's area:* labels, else the table's Area column.
function Get-TaskAreas($PlanTask, $Item) {
	$fromLabels = @($Item.Labels | Where-Object { $_ -like 'area:*' } | ForEach-Object { $_.Substring(5) })
	if ($fromLabels.Count) { return $fromLabels }
	if ($PlanTask -and $PlanTask.Area) { return @($PlanTask.Area) }
	@()
}

# VR lane: the issue has lane:vr, or the table says VR (a label that is not yet set does not hide the lane).
function Test-TaskVrLane($PlanTask, $Item) {
	[bool](($Item.Labels -contains 'lane:vr') -or ($PlanTask -and $PlanTask.Lane -eq 'VR'))
}

# D-015: area:build / area:docs only -> Sonnet, any other area (or none) -> Opus; Fable only with the model:fable label that
# the coordinator sets. Effort defaults as in start-worker.ps1: sonnet medium, opus high, fable xhigh.
function Get-WorkerModelAdvice([string[]]$Labels, [string[]]$Areas) {
	$model = if ($Labels -contains 'model:fable') { 'fable' }
		elseif ($Areas.Count -gt 0 -and -not @($Areas | Where-Object { $_ -notin 'build', 'docs' })) { 'sonnet' }
		else { 'opus' }
	$effort = switch ($model) { 'sonnet' { 'medium' } 'fable' { 'xhigh' } default { 'high' } }
	$type = if ($Labels -contains 'bug') { 'fix' }
		elseif ($Areas.Count -gt 0 -and -not @($Areas | Where-Object { $_ -ne 'docs' })) { 'docs' }
		elseif ($Areas.Count -gt 0 -and -not @($Areas | Where-Object { $_ -ne 'build' })) { 'build' }
		else { 'feat' }
	[pscustomobject]@{ Model = $model; Effort = $effort; Type = $type }
}

function Get-StartWorkerLine([int]$Number, $Advice) {
	".\scripts\dev\start-worker.ps1 $Number -Type $($Advice.Type) -Model $($Advice.Model) -Effort $($Advice.Effort)"
}

function Join-Limited([string[]]$Parts, [int]$Max = 5) {
	if ($Parts.Count -le $Max) { return ($Parts -join ', ') }
	(($Parts | Select-Object -First $Max) -join ', ') + " and $($Parts.Count - $Max) more"
}

# Which tasks may start now.
#   $Plan          Read-MvpPlan
#   $Items         board items: Number, Title, Status, Labels (Get-BoardItems)
#   $PullRequests  open pull requests as ConvertTo-GhPull shapes: number, headRefName
#   $Agents        `claude agents --json`; $null = could not be read (the answer says so)
# Rules, in the order a task is judged (the first group must pass for a task to be considered at all):
#   (a live worker on a task in Testing only waits for the owner's verdict: it holds no slot and no area)
#   status Todo, no needs-design label (D-017), no open PR and no live worker already on it, an area label to check against,
#   every dependency Done on the board, its wave not later than the earliest open wave (+ $WaveLookahead);
#   then capacity: no running or already chosen task in the same area, the VR lane (at most $MaxVrInProgress in progress,
#   at most $MaxVrTesting waiting in Testing), and, only when $MaxWorkers > 0, a free worker slot (0 = no worker cap, D-027). Ready tasks go in wave, queue order.
function Get-NextPlan {
	param(
		$Plan, $Items, $PullRequests = @(), $Agents = $null,
		[int]$MaxWorkers = 0, [int]$MaxVrInProgress = 2, [int]$MaxVrTesting = 4, [int]$WaveLookahead = 0
	)
	$item = @{}; foreach ($i in @($Items)) { $item[[int]$i.Number] = $i }
	$planTask = @{}; foreach ($t in $Plan.Tasks) { $planTask[[int]$t.Number] = $t }
	$queueIndex = @{}; $k = 0; foreach ($n in $Plan.Queue) { if (-not $queueIndex.ContainsKey([int]$n)) { $queueIndex[[int]$n] = $k++ } }
	$warnings = [Collections.Generic.List[string]]::new()
	$isTask = { param($i) $i.Labels -contains 'task' -or $i.Labels -contains 'bug' }

	# live workers: background sessions "ARXVR-<n> <title>"; the coordinator, reviewers ("... review (PR #n)") and finished ones do not count
	$live = @{}
	if ($null -eq $Agents) { $warnings.Add('the running sessions could not be read (claude agents) - live workers are unknown, only the board and the PRs show what runs') }
	foreach ($a in @($Agents)) {
		if ($a.kind -ne 'background') { continue }
		$m = [regex]::Match([string]$a.name, '^ARXVR-(\d+)\s+(.*)$')
		if (-not $m.Success -or $m.Groups[2].Value -match '^review \(PR' -or $a.state -eq 'done') { continue }
		$live[[int]$m.Groups[1].Value] = $a
	}
	$prOf = @{}
	foreach ($p in @($PullRequests)) {
		$m = [regex]::Match([string]$p.headRefName, '^ARXVR-(\d+)-')
		if ($m.Success) { $prOf[[int]$m.Groups[1].Value] = $p }
	}

	# what runs now: In Progress on the board, a live worker, an open PR that is not waiting in Testing for the owner
	$running = @{}   # a Hashtable, not [ordered]: an int key of an ordered dictionary would be read as a position
	$addRunning = {
		param([int]$n, [string]$source)
		if (-not $running.Contains($n)) {
			$it = $item[$n]; $pt = $planTask[$n]
			$running[$n] = [pscustomobject]@{
				Number = $n; Title = $it.Title; Status = $it.Status; Areas = @(Get-TaskAreas $pt $it); Vr = (Test-TaskVrLane $pt $it)
				Sources = [Collections.Generic.List[string]]::new(); Worker = $live[$n]
			}
		}
		$running[$n].Sources.Add($source)
	}
	foreach ($i in @($Items | Where-Object { $_.Status -eq 'In Progress' -and (& $isTask $_) } | Sort-Object Number)) { & $addRunning $i.Number 'board In Progress' }
	foreach ($n in @($live.Keys | Sort-Object)) { if ($item[$n].Status -notin 'Done', 'Testing') { & $addRunning $n "live worker $($live[$n].id)" } }
	foreach ($n in @($prOf.Keys | Sort-Object)) { if ($item[$n].Status -notin 'Testing', 'Done') { & $addRunning $n "open PR #$($prOf[$n].number)" } }
	$running.Values | Sort-Object Number | Where-Object { $null -ne $Agents -and -not $_.Worker -and $_.Status -eq 'In Progress' } | ForEach-Object {
		$warnings.Add("#$($_.Number) is In Progress on the board but no live worker session is on it (a dead worker or the owner's own session?) - it still holds its slot and area")
	}

	$testing = @($Items | Where-Object { $_.Status -eq 'Testing' -and (& $isTask $_) } | Sort-Object Number | ForEach-Object {
		[pscustomobject]@{ Number = $_.Number; Title = $_.Title; Vr = (Test-TaskVrLane $planTask[[int]$_.Number] $_); Worker = if ($live.ContainsKey([int]$_.Number)) { "$($live[[int]$_.Number].id) $($live[[int]$_.Number].state)" } else { $null } }
	})

	# the earliest wave that still has unfinished work
	$openByWave = @{}
	foreach ($t in $Plan.Tasks) {
		if ($null -ne $t.Wave -and $item[[int]$t.Number].Status -in 'Todo', 'In Progress', 'Testing') {
			if (-not $openByWave.ContainsKey($t.Wave)) { $openByWave[$t.Wave] = [Collections.Generic.List[int]]::new() }
			$openByWave[$t.Wave].Add($t.Number)
		}
	}
	$currentWave = if ($openByWave.Count) { [int]($openByWave.Keys | Measure-Object -Minimum).Minimum } else { $null } # an [int]: Measure-Object answers a double, which no int hashtable key matches

	$areaOwner = @{}   # area -> "#n" of the running or already chosen task
	$chosen = @{}      # number -> $true
	foreach ($r in @($running.Values | Sort-Object Number)) { foreach ($a in $r.Areas) { if (-not $areaOwner.ContainsKey($a)) { $areaOwner[$a] = $r.Number } } }
	$vrRunning = @($running.Values | Where-Object Vr).Count
	$vrTesting = @($testing | Where-Object Vr).Count
	$slotsUsed = $running.Count

	$candidates = foreach ($i in @($Items | Where-Object { $_.Status -eq 'Todo' -and (& $isTask $_) })) {
		$pt = $planTask[[int]$i.Number]
		[pscustomobject]@{
			Item = $i; Plan = $pt
			SortWave = if ($pt -and $null -ne $pt.Wave) { [int]$pt.Wave } else { 999 }
			SortQueue = if ($queueIndex.ContainsKey([int]$i.Number)) { $queueIndex[[int]$i.Number] } else { 9999 }
		}
	}
	$ready = [Collections.Generic.List[object]]::new()
	$waiting = [Collections.Generic.List[object]]::new()
	foreach ($c in @($candidates | Sort-Object SortWave, SortQueue, { $_.Item.Number })) {
		$n = [int]$c.Item.Number; $pt = $c.Plan
		$areas = @(Get-TaskAreas $pt $c.Item)
		$vr = Test-TaskVrLane $pt $c.Item
		$reasons = [Collections.Generic.List[string]]::new()
		$notes = [Collections.Generic.List[string]]::new()
		if ($c.Item.Labels -contains 'needs-design') { $reasons.Add("waits for the owner's design talk (needs-design, D-017)") }
		if ($prOf.ContainsKey($n)) { $reasons.Add("a pull request is already open (#$($prOf[$n].number))") }
		if ($live.ContainsKey($n)) { $reasons.Add("a worker is already running on it ($($live[$n].id))") }
		if (-not $pt) { $notes.Add('not in the MVP plan (no Dependencies row): dependencies and wave are not checked') }
		if (-not $areas.Count) { $reasons.Add("no area:* label - area conflicts cannot be checked (add the label)") }
		$open = @(if ($pt) { foreach ($d in $pt.DependsOn) { $s = $item[[int]$d].Status; if ($s -ne 'Done') { "#$d ($(if ($s) { $s } else { 'not on the board' }))" } } })
		if ($open.Count) { $reasons.Add("waits for $(Join-Limited $open 6)") }
		if ($pt -and $null -ne $pt.Wave -and $null -ne $currentWave -and $pt.Wave -gt $currentWave + $WaveLookahead) {
			$reasons.Add("wave $($pt.Wave) is not open yet (wave $currentWave still has $(Join-Limited @($openByWave[$currentWave] | ForEach-Object { "#$_" })))")
		}
		if (-not $reasons.Count) {
			foreach ($a in $areas) {
				if ($areaOwner.ContainsKey($a)) {
					$who = $areaOwner[$a]
					$reasons.Add($(if ($chosen.ContainsKey([int]$who)) { "area $a taken by #$who (ready as well, earlier in the order)" } else { "area $a busy (#$who)" }))
				}
			}
			if ($vr -and $vrRunning -ge $MaxVrInProgress) { $reasons.Add("VR lane full: $vrRunning of $MaxVrInProgress in progress") }
			if ($vr -and $vrTesting -ge $MaxVrTesting) { $reasons.Add("VR lane paused: $vrTesting PRs wait in Testing for the owner (limit $MaxVrTesting)") }
			if ($MaxWorkers -gt 0 -and $slotsUsed -ge $MaxWorkers) { $reasons.Add("no free worker slot ($slotsUsed of $MaxWorkers in use)") }
		}
		if ($reasons.Count) {
			$waiting.Add([pscustomobject]@{ Number = $n; Title = $c.Item.Title; Reasons = @($reasons); Notes = @($notes) })
			continue
		}
		$advice = Get-WorkerModelAdvice $c.Item.Labels $areas
		$ready.Add([pscustomobject]@{
			Number = $n; Title = $c.Item.Title; Areas = $areas; Vr = $vr; Wave = if ($pt) { $pt.Wave } else { $null }
			Model = $advice.Model; Effort = $advice.Effort; Type = $advice.Type; StartLine = (Get-StartWorkerLine $n $advice); Notes = @($notes)
		})
		$chosen[$n] = $true
		foreach ($a in $areas) { $areaOwner[$a] = $n }
		if ($vr) { $vrRunning++ }
		$slotsUsed++
	}
	[pscustomobject]@{
		Ready = @($ready); Waiting = @($waiting); Running = @($running.Values | Sort-Object Number | ForEach-Object {
			[pscustomobject]@{ Number = $_.Number; Title = $_.Title; Areas = $_.Areas; Vr = $_.Vr; Sources = @($_.Sources); Worker = if ($_.Worker) { "$($_.Worker.id) $($_.Worker.state)" } else { $null } }
		})
		Testing = $testing
		Limits = [pscustomobject]@{
			Workers = $running.Count; MaxWorkers = $MaxWorkers; FreeSlots = $(if ($MaxWorkers -gt 0) { [math]::Max(0, $MaxWorkers - $running.Count) } else { $null })
			VrInProgress = @($running.Values | Where-Object Vr).Count; MaxVrInProgress = $MaxVrInProgress
			VrTesting = $vrTesting; MaxVrTesting = $MaxVrTesting; CurrentWave = $currentWave; WaveLookahead = $WaveLookahead
		}
		Warnings = @($warnings)
	}
}
