<#
.SYNOPSIS
	The coordinator's "new cycle" report: open PRs, board state, leftovers to clean up, and what is next.

.DESCRIPTION
	Read-only. Prints sections the coordinator acts on (see .claude/commands/new-cycle.md):
	  PULL REQUESTS   open PRs with CI result, needs-vr-test, mergeability
	  MAIN            latest CI result on main
	  BOARD           tasks In Progress / Testing
	  INCONSISTENT    board vs. issue state mismatches (to fix)
	  CLEANUP         remote branches whose PR is merged or closed, or that have no PR
	  WORKTREES       the worker worktrees: branch, uncommitted changes, PR state, removable or not
	  WORKING COPY    branch and uncommitted changes in the owner's working copy
	  LAUNCHES        game launches per working copy in the last -LaunchHours hours, from the launch
	                  ledger (%LOCALAPPDATA%\ArxFatalisVR\launches.log, D-014) - a worker past the budget
	                  of 10 launches per task is marked; the locks held right now
	  NEXT            the next task from the MVP queue (same logic as next-task.ps1)

.PARAMETER WorkingCopy
	The owner's working copy to inspect. Default: C:\Users\arzib\dev\arx-fatalis-vr.

.PARAMETER LaunchHours
	How many hours of the launch ledger to summarize (default 12).
#>
[CmdletBinding()]
param(
	[string]$WorkingCopy = 'C:\Users\arzib\dev\arx-fatalis-vr',
	[double]$LaunchHours = 12
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'board.ps1')
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

function Section([string]$Name) { Write-Host ''; Write-Host "== $Name" -ForegroundColor Cyan }

# --- Pull requests ---------------------------------------------------------------------------------
Section 'PULL REQUESTS'
# All GitHub reads here are REST (github-lib.ps1, #186); only the board is GraphQL, from the cache.
# A part that cannot be read prints why instead of ending the report.
$ghFailures = @()
function Read-Part([string]$Name, [scriptblock]$Read) {
	try { & $Read } catch { $script:ghFailures += "$Name : $($_.Exception.Message)"; $null }
}

$prs = Read-Part 'open pull requests' { @(Get-GhPulls 'open' 2 -WithMergeable) }
if ($null -ne $prs -and -not $prs) { 'none open' }
foreach ($pr in $prs) {
	$checks = Read-Part "checks of #$($pr.number)" { Get-GhCommitChecks $pr.headRefOid }
	$ci = switch ($checks.State) {
		'none'    { 'no CI (docs-only or not started)' }
		'running' { 'CI running/queued' }
		'failed'  { 'CI FAILED' }
		'green'   { 'CI green' }
		default   { 'CI unknown' }
	}
	$vr = if ($pr.labels.name -contains 'needs-vr-test') { ' needs-vr-test' } else { '' }
	$draft = if ($pr.isDraft) { ' draft' } else { '' }
	"#$($pr.number) [$ci$vr$draft, $($pr.mergeable)] $($pr.title)  ($($pr.headRefName))"
}

# --- main ------------------------------------------------------------------------------------------
Section 'MAIN'
$run = Read-Part 'latest run on main' { Get-GhLatestRun 'main' }
if ($run) { "latest CI on main: $($run.Status) $($run.Conclusion) $($run.HeadSha.Substring(0,9)) $($run.Title)" }
elseif ($ghFailures -match '^latest run') { 'unknown (could not be read, see GITHUB below)' }
else { 'no CI runs on main' }

# --- Board -----------------------------------------------------------------------------------------
$items = @(Read-Part 'the board' { Get-BoardItems })
$issues = @(Read-Part 'the issue list' { Get-GhIssues 'all' })
$issueByNumber = @{}; foreach ($i in $issues) { $issueByNumber[[int]$i.number] = $i }
$itemByNumber = @{}; foreach ($i in $items) { $itemByNumber[$i.Number] = $i }
$haveBoard = $items.Count -gt 0
$haveIssues = $issues.Count -gt 0

Section 'BOARD'
if (-not $haveBoard) { 'the board could not be read (no cache either) - nothing below about the board is true' }
foreach ($status in 'Testing', 'In Progress') {
	$list = $items | Where-Object { $_.Status -eq $status -and ($_.Labels -contains 'task' -or $_.Labels -contains 'bug') }
	foreach ($t in $list) { "$($status.ToUpper().PadRight(12)) #$($t.Number) $($t.Title)$(if ($t.Pending) { '  [queued move, not on GitHub yet]' })" }
}
foreach ($line in (Get-BoardQueueSummary)) { "PENDING      $line" }

Section 'INCONSISTENT'
$problems = @()
if (-not ($haveBoard -and $haveIssues)) { $items = @(); $issues = @() } # nothing is concluded from a part that could not be read
foreach ($t in $items) {
	$issue = $issueByNumber[$t.Number]
	if (-not $issue) { continue }
	if ($issue.state -eq 'CLOSED' -and $t.Status -ne 'Done') { $problems += "#$($t.Number) is closed but on the board as '$($t.Status)' -> move to Done (or reopen)" }
	if ($issue.state -eq 'OPEN' -and $t.Status -eq 'Done') { $problems += "#$($t.Number) is Done on the board but still open -> close it (or move back)" }
	if (-not $t.Status) { $problems += "#$($t.Number) has no board status" }
}
foreach ($i in $issues) {
	$isWork = ($i.labels.name -contains 'task') -or ($i.labels.name -contains 'bug')
	if ($i.state -eq 'OPEN' -and $isWork -and -not $itemByNumber.ContainsKey([int]$i.number)) {
		$problems += "#$($i.number) is an open task/bug but not on the board -> add it"
	}
	if ($i.state -eq 'OPEN' -and $isWork -and -not $i.milestone) {
		$problems += "#$($i.number) has no milestone -> M1 MVP or later"
	}
}
if ($problems) { $problems } elseif ($haveBoard -and $haveIssues) { 'none' } else { 'not checked: the board or the issue list could not be read (see GITHUB below)' }

# --- Cleanup ---------------------------------------------------------------------------------------
Section 'CLEANUP'
$branchList = Read-Part 'remote branches' { Invoke-ArxGhRestPages "repos/$Repo/branches" '' 3 }
$branches = @($branchList | ForEach-Object { $_.name }) | Where-Object { $_ -notin 'main', 'master' }
$allPrs = @(Read-Part 'all pull requests' { Get-GhPulls 'all' 3 })
$found = $false
if (-not $branchList -or -not $allPrs) { 'not checked: branches or pull requests could not be read (see GITHUB below)'; $found = $true; $branches = @() }
foreach ($b in $branches) {
	$pr = $allPrs | Where-Object headRefName -eq $b | Select-Object -First 1
	if (-not $pr) { "branch '$b' has no PR"; $found = $true }
	elseif ($pr.state -ne 'OPEN') { "branch '$b' belongs to $($pr.state.ToLower()) PR #$($pr.number) -> delete"; $found = $true }
}
if (-not $found) { 'no leftover remote branches' }

# --- Worktrees -------------------------------------------------------------------------------------
Section 'WORKTREES'
$wtRoot = if ($env:ARXVR_WORKTREE_ROOT) { $env:ARXVR_WORKTREE_ROOT } else { Join-Path $env:USERPROFILE 'arx-fatalis-vr-wt' }
$worktrees = @()
$current = $null
foreach ($line in (git -C $WorkingCopy worktree list --porcelain 2>$null)) {
	if ($line -match '^worktree (.+)$') { $current = [pscustomobject]@{ Path = $Matches[1] -replace '/', '\'; Head = ''; Branch = $null }; $worktrees += $current }
	elseif ($line -match '^HEAD (\w+)$') { $current.Head = $Matches[1] }
	elseif ($line -match '^branch refs/heads/(.+)$') { $current.Branch = $Matches[1] }
}
$found = $false
foreach ($w in ($worktrees | Select-Object -Skip 1)) { # the first entry is the main working copy itself
	$found = $true
	$pr = if ($w.Branch) { $allPrs | Where-Object headRefName -eq $w.Branch | Select-Object -First 1 }
	$prText = if (-not $w.Branch) { 'detached HEAD' } elseif (-not $pr) { 'no PR' } else { "PR #$($pr.number) $($pr.state.ToLower())" }
	if (-not (Test-Path $w.Path)) { "$($w.Path) : folder MISSING -> git worktree prune"; continue }
	$dirty = @(git -C $w.Path status --porcelain 2>$null)
	$state = if ($dirty) { "UNCOMMITTED changes: $($dirty.Count) file(s)" } else { 'clean' }
	$verdict = if ($pr -and $pr.state -ne 'OPEN' -and -not $dirty) { " -> remove: scripts\dev\new-worktree.ps1 -Branch $($w.Branch) -Remove" }
		elseif ($pr -and $pr.state -ne 'OPEN') { ' -> PR finished but changes left behind: look, then -Remove -Force' }
		else { '' }
	"$($w.Path) : branch '$($w.Branch)' @ $($w.Head.Substring(0, 9)), $state, $prText$verdict"
}
if (Test-Path $wtRoot) {
	foreach ($dir in Get-ChildItem $wtRoot -Directory) {
		if (-not ($worktrees | Where-Object { $_.Path -ieq $dir.FullName })) { "$($dir.FullName) : not a registered worktree -> stale folder, delete it by hand"; $found = $true }
	}
}
if (-not $found) { "no worktrees (root $wtRoot)" }

# --- Owner's working copy --------------------------------------------------------------------------
Section 'WORKING COPY'
if (Test-Path (Join-Path $WorkingCopy '.git')) {
	git -C $WorkingCopy fetch -q --prune origin 2>$null
	$branch = git -C $WorkingCopy branch --show-current
	$dirty = git -C $WorkingCopy status --porcelain
	$behind = git -C $WorkingCopy rev-list --count "HEAD..origin/main" 2>$null
	"$WorkingCopy : branch '$branch', $(if ($dirty) { "UNCOMMITTED changes: $(@($dirty).Count) file(s)" } else { 'clean' }), $behind commit(s) behind origin/main"
	$merged = git -C $WorkingCopy branch --merged origin/main --format '%(refname:short)' | Where-Object { $_ -notin 'main', 'master' }
	if ($merged) { "local branches already merged into main: $($merged -join ', ')" }
} else { "$WorkingCopy not found" }

# --- Launches (the ledger of D-014) ----------------------------------------------------------------
Section 'LAUNCHES'
$records = @(Get-ArxLaunchRecords -SinceHours $LaunchHours)
if (-not $records) { "no game launches in the last $LaunchHours h ($(Get-ArxLaunchLogPath))" }
foreach ($group in ($records | Group-Object copy | Sort-Object Count -Descending)) {
	$vr = @($group.Group | Where-Object mode -eq 'vr').Count
	$failed = @($group.Group | Where-Object { $_.exit -ne '0' }).Count
	$last = ($group.Group | Sort-Object Time | Select-Object -Last 1)
	$branches = ($group.Group | Select-Object -ExpandProperty branch -Unique) -join ', '
	$budget = if ($group.Count -gt 10) { ' -> OVER THE BUDGET of 10 launches per task (WORKFLOW.md): ask the worker why' } else { '' }
	"$($group.Name) : $($group.Count) launch(es) ($vr VR, $failed with exit <> 0), last $($last.Time.ToString('HH:mm')) by $($last.script), branch $branches$budget"
}
$held = @(Get-ArxLockHolders)
if ($held) { foreach ($line in $held) { "lock held now: $line" } } else { 'no game runs under the lock right now' }

# --- Next ------------------------------------------------------------------------------------------
Section 'NEXT'
try { & (Join-Path $PSScriptRoot 'next-task.ps1') } catch { $ghFailures += "next task : $($_.Exception.Message)"; 'unknown - the board could not be read (see GITHUB below)' }

# --- GitHub ----------------------------------------------------------------------------------------
if ($ghFailures) {
	Section 'GITHUB'
	'parts of this report are missing because GitHub could not be read (rate limit / network); run it again after the reset:'
	$ghFailures
}
