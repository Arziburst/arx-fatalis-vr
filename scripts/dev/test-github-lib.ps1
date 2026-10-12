#Requires -Version 7
<#
.SYNOPSIS
	Self-test of the GitHub access of the dev scripts (#186): REST first, the board cache, rate-limit handling, the move queue.

.DESCRIPTION
	Runs github-lib.ps1 and board.ps1 against an in-memory GitHub (no network, no sleeping) in a scratch state folder
	($env:ARXVR_GH_STATE_DIR) and checks the rules of the task:
	  - issues, PRs, checks and runs are read with REST only; GraphQL is used only for the Projects v2 board
	  - the board is one GraphQL query per 100 items, then served from the cache without any call while it is fresh
	  - a spent GraphQL limit: a near reset is waited for once, a far one throws the rate-limit error with the reset time,
	    and later calls (even from another process) fail fast without asking GitHub
	  - a stale cache is returned with a warning when GitHub cannot be read; without a cache the error is thrown
	    (never an empty board)
	  - a status move is one mutation; under the limit it is queued, shows as Pending, the last move of an issue wins and
	    the queue is sent after the reset
	  - transient 5xx errors are retried for reads, never for a non-idempotent write
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-gh-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
$env:ARXVR_GH_STATE_DIR = $root
Remove-Item Env:\ARXVR_FORCE_GRAPHQL_ERROR -ErrorAction SilentlyContinue
. (Join-Path $PSScriptRoot 'board.ps1')

# --- The fake GitHub -----------------------------------------------------------------------------------------------
$fake = @{
	Calls = [Collections.Generic.List[string]]::new(); Sleeps = [Collections.Generic.List[double]]::new()
	GraphqlDown = $false; ResetIn = 3000; Remaining = 0   # a spent primary limit that resets in $ResetIn seconds
	Transient = 0                                         # the next N REST reads answer HTTP 502
	Items = @(); Mutations = [Collections.Generic.List[string]]::new()
}
function New-FakeItem([int]$Number, [string]$Status, [string[]]$Labels = @('task')) {
	@{ id = "PVTI_$Number"; status = @{ name = $Status }; content = @{ number = $Number; title = "Task $Number"; labels = @{ nodes = @($Labels | ForEach-Object { @{ name = $_ } }) }; milestone = @{ title = 'M1 MVP' } } }
}
function Reset-Fake { $fake.Calls.Clear(); $fake.Sleeps.Clear(); $fake.Mutations.Clear() }
function Reply($obj) { @{ Exit = 0; Text = ($obj | ConvertTo-Json -Depth 12 -Compress) } }

$script:ArxGhSleep = { param($s) $fake.Sleeps.Add($s) }
$script:ArxGhInvoker = {
	param($GhArgs, $InputText)
	$fake.Calls.Add(($GhArgs -join ' '))
	if ($GhArgs[1] -eq 'rate_limit') {
		return (Reply @{ resources = @{ graphql = @{ limit = 5000; remaining = $fake.Remaining; reset = ((Get-ArxEpoch) + $fake.ResetIn) }; core = @{ limit = 5000; remaining = 5000; reset = ((Get-ArxEpoch) + 3000) } } })
	}
	if ($GhArgs[1] -eq 'graphql') {
		if ($fake.GraphqlDown) { return @{ Exit = 1; Text = 'GraphQL: API rate limit already exceeded for user ID 1. (HTTP 403)' } }
		$q = ($InputText | ConvertFrom-Json)
		if ($q.query -match 'updateProjectV2ItemFieldValue') { $fake.Mutations.Add("set $($q.variables.i) -> $($q.variables.o)"); return (Reply @{ data = @{ updateProjectV2ItemFieldValue = @{ projectV2Item = @{ id = $q.variables.i } } } }) }
		if ($q.query -match 'addProjectV2ItemById') { $fake.Mutations.Add("add $($q.variables.c)"); return (Reply @{ data = @{ addProjectV2ItemById = @{ item = @{ id = 'PVTI_new' } } } }) }
		$after = [int]($q.variables.after ?? 0)
		$page = @($fake.Items | Select-Object -Skip $after -First 100)
		$hasNext = ($after + 100) -lt $fake.Items.Count
		return (Reply @{ data = @{ user = @{ projectV2 = @{
			id = 'PVT_1'
			field = @{ id = 'PVTSSF_1'; options = @(@{ id = 'o1'; name = 'Backlog' }, @{ id = 'o2'; name = 'Todo' }, @{ id = 'o3'; name = 'In Progress' }, @{ id = 'o4'; name = 'Testing' }, @{ id = 'o5'; name = 'Done' }) }
			items = @{ pageInfo = @{ hasNextPage = $hasNext; endCursor = "$($after + 100)" }; nodes = $page }
		} } } })
	}
	# REST
	if ($fake.Transient -gt 0 -and $GhArgs -contains 'GET') { $fake.Transient--; return @{ Exit = 1; Text = 'gh: Bad Gateway (HTTP 502)' } }
	$path = $GhArgs | Where-Object { $_ -like 'repos/*' } | Select-Object -First 1
	switch -Regex ($path) {
		'/issues/(\d+)$' { return (Reply @{ number = [int]$Matches[1]; title = "Issue $($Matches[1])"; state = 'open'; html_url = 'https://x/issues/1'; node_id = "I_$($Matches[1])"; labels = @(@{ name = 'task' }, @{ name = 'area:build' }); milestone = @{ title = 'M1 MVP' }; body = 'b' }) }
		'/pulls/(\d+)$' { return (Reply @{ number = [int]$Matches[1]; title = 'PR title'; state = 'open'; merged_at = $null; mergeable = $true; html_url = 'https://x/pull/1'; body = 'body'; head = @{ ref = 'ARXVR-1-x'; sha = ('a' * 40) }; labels = @(@{ name = 'needs-vr-test' }) }) }
		'/commits/\w+/check-runs' { return (Reply @{ check_runs = @(@{ name = 'Build (Windows)'; status = 'completed'; conclusion = 'success' }) }) }
		'/commits/\w+/status' { return (Reply @{ total_count = 0; statuses = @() }) }
		'/actions/runs' { return (Reply @{ workflow_runs = @(@{ status = 'completed'; conclusion = 'success'; head_sha = ('b' * 40); display_title = 'run'; id = 7 }) }) }
		'/issues\?' { return (Reply @(@{ number = 1; state = 'open'; title = 'i'; labels = @(); milestone = $null }, @{ number = 2; state = 'open'; title = 'a pull request'; pull_request = @{}; labels = @() })) }
		'/issues/\d+/comments$' { return @{ Exit = 1; Text = 'gh: Server Error (HTTP 500)' } }
	}
	@{ Exit = 1; Text = "fake GitHub: unexpected $($GhArgs -join ' ')" }
}

$failures = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green } else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failures++ }
}
function GraphqlCalls { @($fake.Calls | Where-Object { $_ -match 'graphql' }).Count }
function Fresh-State { Remove-Item "$root\*.json" -Force -ErrorAction SilentlyContinue; $script:ArxGhBlockedMemory = @{}; $script:BoardStaleWarned = $false; Reset-Fake }
function Age-Cache([int]$Seconds) {
	$p = Get-BoardCachePath; $c = Get-Content -Raw $p | ConvertFrom-Json; $c.fetchedEpoch = $c.fetchedEpoch - $Seconds
	$c | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 $p
}
$WarningPreference = 'SilentlyContinue'

try {
	Write-Host 'REST reads'
	Fresh-State
	$issue = Get-GhIssue 186
	Check ($issue.Title -eq 'Issue 186' -and $issue.Labels -contains 'area:build' -and $issue.State -eq 'OPEN' -and -not $issue.IsPullRequest) 'issue: title, labels, state'
	$pull = Get-GhPull 191
	Check ($pull.headRefName -eq 'ARXVR-1-x' -and $pull.state -eq 'OPEN' -and $pull.labels.name -contains 'needs-vr-test' -and $pull.mergeable -eq 'MERGEABLE') 'pull request: branch, state, labels, mergeable'
	Check ((Get-GhCommitChecks ('a' * 40)).State -eq 'green') 'commit checks: green'
	Check ((Get-GhLatestRun 'main').Conclusion -eq 'success') 'latest run on main'
	Check (@(Get-GhIssues 'all').Count -eq 1) 'issue list drops pull requests'
	Check ((GraphqlCalls) -eq 0) 'none of them used GraphQL'

	Write-Host 'Board: one query per 100 items, then the cache'
	Fresh-State
	$fake.Items = @(1..230 | ForEach-Object { New-FakeItem $_ 'Todo' })
	$items = @(Get-BoardItems)
	Check ($items.Count -eq 230 -and (GraphqlCalls) -eq 3) "230 items read with $(GraphqlCalls) GraphQL calls (3 pages)"
	Check ($items[0].Labels -contains 'task' -and $items[0].Milestone -eq 'M1 MVP' -and $items[0].Status -eq 'Todo') 'item fields: labels, milestone, status'
	Reset-Fake
	Get-BoardItems | Out-Null
	Check ((GraphqlCalls) -eq 0 -and $fake.Calls.Count -eq 0) 'a fresh cache needs no call at all'

	Write-Host 'Rate limit: near reset is waited for, far reset throws, later calls fail fast'
	Fresh-State; $fake.Items = @(New-FakeItem 5 'Todo')
	Get-BoardItems | Out-Null; Age-Cache 1000; Reset-Fake
	$fake.GraphqlDown = $true; $fake.ResetIn = 20; $fake.Remaining = 0
	$r = Get-BoardItems
	Check ($fake.Sleeps.Count -eq 1 -and $fake.Sleeps[0] -ge 20 -and $fake.Sleeps[0] -le 25) "reset in 20 s: waited once ($($fake.Sleeps -join ',') s)"
	Check ((GraphqlCalls) -eq 2 -and @($r).Count -eq 1) 'then one retry, then the stale cache is served'
	Fresh-State; $fake.Items = @(New-FakeItem 5 'Todo'); $fake.GraphqlDown = $false
	Get-BoardItems | Out-Null; Age-Cache 1000
	$fake.GraphqlDown = $true; $fake.ResetIn = 3000; Reset-Fake
	$err = $null
	try { Invoke-ArxGhGraphql 'query { x }' | Out-Null } catch { $err = $_ }
	Check ($err -and (Test-ArxGhRateLimitError $err) -and $err.Exception.Message -match 'resets at \d\d:\d\d:\d\d') 'reset in 50 min: the rate-limit error names the reset time'
	Check ($fake.Sleeps.Count -eq 0) 'a far reset is not waited for'
	Reset-Fake
	$err = $null
	try { Invoke-ArxGhGraphql 'query { x }' | Out-Null } catch { $err = $_ }
	Check ($err -and (Test-ArxGhRateLimitError $err) -and $fake.Calls.Count -eq 0) 'the next call fails fast without asking GitHub'
	$script:ArxGhBlockedMemory = @{} # as another process
	$err = $null
	try { Invoke-ArxGhGraphql 'query { x }' | Out-Null } catch { $err = $_ }
	Check ($err -and $fake.Calls.Count -eq 0) '... also from another process (gh-blocked.json)'
	Clear-ArxGhBlock
	$fake.GraphqlDown = $false
	Check ($null -ne (Get-BoardItems)) 'after Clear-ArxGhBlock GitHub is asked again'

	Write-Host 'Stale cache and no cache'
	Fresh-State; $fake.Items = @((New-FakeItem 5 'Todo'), (New-FakeItem 6 'Done')); $fake.GraphqlDown = $false
	Get-BoardItems | Out-Null; Age-Cache 1000
	$fake.GraphqlDown = $true; $fake.ResetIn = 3000
	Check (@(Get-BoardItems).Count -eq 2) 'limit spent: the stale cache is served (2 items), not an empty board'
	Fresh-State; $fake.GraphqlDown = $true
	$err = $null
	try { Get-BoardItems | Out-Null } catch { $err = $_ }
	Check ($err -and (Test-ArxGhRateLimitError $err)) 'limit spent and no cache: the error is thrown, not an empty board'

	Write-Host 'Status moves'
	Fresh-State; $fake.GraphqlDown = $false; $fake.Items = @((New-FakeItem 5 'Todo'), (New-FakeItem 6 'Todo'))
	Get-BoardItems | Out-Null; Reset-Fake
	Check ((Set-BoardStatus 5 'In Progress') -eq 'applied' -and $fake.Mutations.Count -eq 1 -and $fake.Mutations[0] -eq 'set PVTI_5 -> o3') 'a move is one mutation with the cached ids'
	Check (@(Get-BoardItems | Where-Object Number -eq 5)[0].Status -eq 'In Progress') 'the cache shows the move at once'
	Reset-Fake
	Check ((Set-BoardStatus 9 'Todo') -eq 'applied' -and $fake.Mutations -contains 'add I_9') 'an issue not on the board is added first'
	Reset-Fake; $fake.GraphqlDown = $true; $fake.ResetIn = 3000
	$res = Set-BoardStatus 6 'In Progress'
	Check ($res -eq 'queued') 'limit spent: the move is queued, the call does not fail'
	$res2 = Set-BoardStatus 6 'Testing'
	$q = Read-BoardQueue
	Check (@($q.moves).Count -eq 1 -and $q.moves[0].status -eq 'Testing') 'the last move of an issue wins in the queue'
	$six = @(Get-BoardItems | Where-Object Number -eq 6)[0]
	Check ($six.Status -eq 'Testing' -and $six.Pending) 'the queued move shows as Pending in the board items'
	Check (@(Get-BoardQueueSummary).Count -eq 1) 'the queue is listed for the reports'
	Reset-Fake
	Check (@(Invoke-BoardQueueFlush).Count -eq 0 -and $fake.Calls.Count -eq 0) 'before the reset the queue is not sent'
	$fake.GraphqlDown = $false; Clear-ArxGhBlock
	Check (@(Invoke-BoardQueueFlush -Force).Count -eq 1 -and $fake.Mutations -contains 'set PVTI_6 -> o4') 'after the reset the queued move is sent'
	Check (@((Read-BoardQueue).moves).Count -eq 0) 'the queue is empty again'
	$fake.GraphqlDown = $true; Set-BoardStatus 6 'Done' | Out-Null
	$fake.GraphqlDown = $false; Clear-ArxGhBlock; Reset-Fake
	Set-BoardStatus 6 'Backlog' | Out-Null
	Check (@((Read-BoardQueue).moves).Count -eq 0) 'a direct move removes an older queued move of the same issue'

	Write-Host 'Transient errors'
	Fresh-State; $fake.Transient = 2
	Check ((Get-GhIssue 3).Title -eq 'Issue 3' -and $fake.Sleeps.Count -eq 2 -and $fake.Sleeps[1] -gt $fake.Sleeps[0]) 'a read is retried after 502 with growing backoff'
	Fresh-State; $fake.Transient = 0
	$err = $null
	try { Invoke-ArxGhRest 'repos/Arziburst/arx-fatalis-vr/issues/3/comments' 'POST' @{ body = 'x' } | Out-Null } catch { $err = $_ }
	Check ($err -and $fake.Sleeps.Count -eq 0 -and @($fake.Calls | Where-Object { $_ -match 'POST' }).Count -eq 1) 'a non-idempotent write is not retried'
} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "$failures check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'all checks passed' -ForegroundColor Green
exit 0
