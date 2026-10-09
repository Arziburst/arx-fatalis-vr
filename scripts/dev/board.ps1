# Shared helpers for the project board. Dot-source from the other scripts in this folder.
#
# The board is a GitHub Projects v2 board: the only GraphQL the dev scripts need (#186). Everything else is REST
# (github-lib.ps1). To spare the shared GraphQL quota:
#   - the whole board is read with one batched query per 100 items and cached in
#     %LOCALAPPDATA%\ArxFatalisVR\board-cache.json (fresh for 5 minutes; a stale copy is used, with a warning, when
#     GitHub cannot be reached or the limit is spent - never an empty answer);
#   - a status move is one mutation; when the limit is spent it is queued in board-queue.json, shows in
#     Get-BoardItems as already done (Pending = $true) and is applied by the next run that can reach GitHub.

$Repo        = 'Arziburst/arx-fatalis-vr'
$BoardOwner  = 'Arziburst'
$BoardNumber = 3
$BoardCacheSeconds = 300

. (Join-Path $PSScriptRoot 'github-lib.ps1')

# --- The cache -----------------------------------------------------------------------------------------------------------

function Get-BoardCachePath { Join-Path (Get-ArxGhStateDir) 'board-cache.json' }
function Get-BoardQueuePath { Join-Path (Get-ArxGhStateDir) 'board-queue.json' }

function Read-BoardCache {
	$path = Get-BoardCachePath
	if (-not (Test-Path $path)) { return $null }
	try {
		$c = Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
		if (-not $c.projectId -or -not $c.items) { return $null }
		$c
	} catch { $null }
}

function Write-BoardCache($Cache) { Use-ArxGhLock { Write-ArxGhFile (Get-BoardCachePath) $Cache } }

$script:BoardQuery = @'
query($login: String!, $number: Int!, $after: String) {
  user(login: $login) {
    projectV2(number: $number) {
      id
      field(name: "Status") { ... on ProjectV2SingleSelectField { id options { id name } } }
      items(first: 100, after: $after) {
        pageInfo { hasNextPage endCursor }
        nodes {
          id
          status: fieldValueByName(name: "Status") { ... on ProjectV2ItemFieldSingleSelectValue { name } }
          content {
            ... on Issue { number title labels(first: 30) { nodes { name } } milestone { title } }
            ... on PullRequest { number title labels(first: 30) { nodes { name } } milestone { title } }
          }
        }
      }
    }
  }
}
'@

# The whole board from GitHub, as the cache object.
function Read-BoardFromGitHub {
	$items = [Collections.Generic.List[object]]::new()
	$after = $null
	$project = $null
	do {
		$vars = @{ login = $BoardOwner; number = $BoardNumber }
		if ($after) { $vars.after = $after }
		$project = (Invoke-ArxGhGraphql $script:BoardQuery $vars).user.projectV2
		if (-not $project) { throw "project $BoardOwner/$BoardNumber not found" }
		foreach ($n in $project.items.nodes) {
			if (-not $n.content.number) { continue } # draft items and redacted content have no issue number
			$items.Add([pscustomobject]@{
				Id        = $n.id
				Number    = [int]$n.content.number
				Title     = $n.content.title
				Status    = $n.status.name
				Milestone = $n.content.milestone.title
				Labels    = @($n.content.labels.nodes | ForEach-Object { $_.name })
			})
		}
		$after = if ($project.items.pageInfo.hasNextPage) { $project.items.pageInfo.endCursor } else { $null }
	} while ($after)
	$options = [ordered]@{}
	foreach ($o in $project.field.options) { $options[$o.name] = $o.id }
	[pscustomobject]@{
		fetchedEpoch  = Get-ArxEpoch
		fetchedAt     = (Get-Date).ToString('s')
		projectId     = $project.id
		statusFieldId = $project.field.id
		options       = $options
		items         = $items.ToArray()
	}
}

# The board cache object (items, projectId, statusFieldId, options), fresh enough or re-read. When GitHub cannot be
# read and a cache exists, that (stale) cache is returned with a warning; without one the error is thrown - callers must
# never read a failure as "the board is empty" (TRAPS.md).
function Get-BoardData([switch]$Fresh, [int]$MaxAgeSeconds = $BoardCacheSeconds) {
	$cache = Read-BoardCache
	$age = if ($cache) { (Get-ArxEpoch) - [long]$cache.fetchedEpoch } else { [long]::MaxValue }
	if ($cache -and -not $Fresh -and $age -le $MaxAgeSeconds) { return $cache }
	try {
		$data = Read-BoardFromGitHub
		Write-BoardCache $data
		return (Read-BoardCache)
	} catch {
		if (-not $cache) { throw }
		$why = if (Test-ArxGhRateLimitError $_) { $_.Exception.Message } else { "GitHub could not be read: $($_.Exception.Message)" }
		if (-not $script:BoardStaleWarned) {
			$script:BoardStaleWarned = $true # once per run
			Write-Warning "BOARD IS STALE: using the cache from $([math]::Round($age / 60, 1)) min ago ($why)"
		}
		return $cache
	}
}

# --- The queue of status moves that could not be sent ---------------------------------------------------------------------

function Read-BoardQueue {
	$path = Get-BoardQueuePath
	$empty = [pscustomobject]@{ nextAttemptEpoch = 0; moves = @() }
	if (-not (Test-Path $path)) { return $empty }
	try {
		$q = Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json
		[pscustomobject]@{ nextAttemptEpoch = [long]$q.nextAttemptEpoch; moves = @($q.moves | Where-Object { $_ }) }
	} catch { $empty }
}

function Write-BoardQueue($Queue) {
	Use-ArxGhLock {
		if (@($Queue.moves).Count -eq 0) { Remove-Item -Force (Get-BoardQueuePath) -ErrorAction SilentlyContinue }
		else { Write-ArxGhFile (Get-BoardQueuePath) $Queue }
	}
}

function Add-BoardQueueMove([int]$Issue, [string]$Status, [long]$NextAttemptEpoch) {
	Use-ArxGhLock {
		$q = Read-BoardQueue
		$moves = @($q.moves | Where-Object { [int]$_.issue -ne $Issue }) # the last move of an issue wins
		$moves += [pscustomobject]@{ issue = $Issue; status = $Status; queuedAt = (Get-Date).ToString('s') }
		Write-BoardQueue ([pscustomobject]@{ nextAttemptEpoch = $NextAttemptEpoch; moves = $moves })
	}
}

function Remove-BoardQueueMove([int]$Issue) {
	Use-ArxGhLock {
		$q = Read-BoardQueue
		if (@($q.moves | Where-Object { [int]$_.issue -eq $Issue }).Count) {
			Write-BoardQueue ([pscustomobject]@{ nextAttemptEpoch = $q.nextAttemptEpoch; moves = @($q.moves | Where-Object { [int]$_.issue -ne $Issue }) })
		}
	}
}

# One line per queued move, for the reports.
function Get-BoardQueueSummary {
	foreach ($m in (Read-BoardQueue).moves) { "#$($m.issue) -> $($m.status) (queued $($m.queuedAt), not yet on GitHub)" }
}

# --- Items ---------------------------------------------------------------------------------------------------------------

# The board items; queued moves are applied on top (Pending = $true). Tries to send the queue first when it is due.
function Get-BoardItems([switch]$Fresh) {
	if (@((Read-BoardQueue).moves).Count) { try { Invoke-BoardQueueFlush | Out-Null } catch { Write-Warning "board queue not sent: $($_.Exception.Message)" } }
	$data = Get-BoardData -Fresh:$Fresh
	$queued = @{}; foreach ($m in (Read-BoardQueue).moves) { $queued[[int]$m.issue] = $m.status }
	foreach ($i in $data.items) {
		$pending = $queued.ContainsKey([int]$i.Number)
		[pscustomobject]@{
			Id        = $i.Id
			Number    = [int]$i.Number
			Title     = $i.Title
			Status    = if ($pending) { $queued[[int]$i.Number] } else { $i.Status }
			Milestone = $i.Milestone
			Labels    = @($i.Labels)
			Pending   = $pending
		}
	}
}

function Get-MvpQueue {
	# Issue numbers from the "Task queue" section of docs/vr/MVP.md, in order.
	$mvp = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot '..\..\docs\vr\MVP.md')
	$section = [regex]::Match($mvp, '(?ms)^## Task queue\s*$(.*?)(?=^## [^#]|\z)').Groups[1].Value
	[regex]::Matches($section, '(?m)^\s*\d+\.\s+#(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
}

# --- Moves ---------------------------------------------------------------------------------------------------------------

# Sends one status move to GitHub (adds the issue to the board if it is not there). Throws on failure, the rate-limit
# error included. Updates the cache so the next read already shows it.
function Send-BoardMove([int]$Issue, [string]$Status) {
	$data = Get-BoardData
	$optionId = $data.options.$Status
	$item = $data.items | Where-Object { [int]$_.Number -eq $Issue } | Select-Object -First 1
	if (-not $optionId -or -not $item) {
		$data = Get-BoardData -Fresh # a column or an item added since the cache was written
		$optionId = $data.options.$Status
		$item = $data.items | Where-Object { [int]$_.Number -eq $Issue } | Select-Object -First 1
	}
	if (-not $optionId) { throw "the board has no column '$Status' (have: $(($data.options.PSObject.Properties.Name) -join ', '))" }
	if (-not $item) {
		Write-Host "Issue #$Issue is not on the board yet, adding it"
		$node = (Get-GhIssue $Issue).NodeId
		$added = Invoke-ArxGhGraphql 'mutation($p: ID!, $c: ID!) { addProjectV2ItemById(input: {projectId: $p, contentId: $c}) { item { id } } }' @{ p = $data.projectId; c = $node }
		$item = [pscustomobject]@{ Id = $added.addProjectV2ItemById.item.id; Number = $Issue; Title = ''; Status = $null; Milestone = $null; Labels = @() }
		$data.items = @($data.items) + $item
	}
	Invoke-ArxGhGraphql 'mutation($p: ID!, $i: ID!, $f: ID!, $o: String!) { updateProjectV2ItemFieldValue(input: {projectId: $p, itemId: $i, fieldId: $f, value: {singleSelectOptionId: $o}}) { projectV2Item { id } } }' `
		@{ p = $data.projectId; i = $item.Id; f = $data.statusFieldId; o = $optionId } | Out-Null
	$item.Status = $Status
	Write-BoardCache $data
}

# Sends the queued moves, oldest first. Stops quietly at the first rate-limit error and tries again after the reset;
# any other failure keeps the move queued with its error. -Force ignores the "not before" time. Returns the applied moves.
function Invoke-BoardQueueFlush([switch]$Force) {
	$q = Read-BoardQueue
	if (-not @($q.moves).Count) { return @() }
	if (-not $Force -and (Get-ArxEpoch) -lt $q.nextAttemptEpoch) { return @() }
	$applied = @(); $next = 0; $failed = $false
	foreach ($m in $q.moves) {
		if ($next) { break }
		try {
			Send-BoardMove ([int]$m.issue) $m.status
			$applied += $m
			Write-Host "queued move sent: #$($m.issue) -> $($m.status)"
		} catch {
			if (Test-ArxGhRateLimitError $_) { $next = Get-ArxGhRateLimitResetEpoch $_ }
			else { $failed = $true; Write-Warning "queued move #$($m.issue) -> $($m.status) failed (stays queued): $($_.Exception.Message)" }
		}
	}
	if (-not $next -and $failed) { $next = (Get-ArxEpoch) + 300 }
	# re-read: another process may have queued or sent moves while this one was sending
	$sent = @($applied | ForEach-Object { [int]$_.issue })
	$left = @((Read-BoardQueue).moves | Where-Object { [int]$_.issue -notin $sent })
	Write-BoardQueue ([pscustomobject]@{ nextAttemptEpoch = $next; moves = $left })
	$applied
}

# The move itself, for set-status.ps1: sent now, or queued when the GraphQL limit is spent. Returns 'applied' or 'queued'.
function Set-BoardStatus([int]$Issue, [string]$Status) {
	try {
		Send-BoardMove $Issue $Status
		Remove-BoardQueueMove $Issue # an older queued move of this issue must not overwrite this one later
		try { Invoke-BoardQueueFlush | Out-Null } catch {}
		'applied'
	} catch {
		if (-not (Test-ArxGhRateLimitError $_)) { throw }
		Add-BoardQueueMove $Issue $Status (Get-ArxGhRateLimitResetEpoch $_)
		Write-Warning "$($_.Exception.Message) - the move is queued and is sent by the next board script run after the reset (set-status.ps1 -Flush forces it)."
		'queued'
	}
}
