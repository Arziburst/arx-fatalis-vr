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
	  WORKING COPY    branch and uncommitted changes in the owner's working copy
	  NEXT            the next task from the MVP queue (same logic as next-task.ps1)

.PARAMETER WorkingCopy
	The owner's working copy to inspect. Default: C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr.
#>
[CmdletBinding()]
param([string]$WorkingCopy = 'C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr')

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'board.ps1')

function Section([string]$Name) { Write-Host ''; Write-Host "== $Name" -ForegroundColor Cyan }

# --- Pull requests ---------------------------------------------------------------------------------
Section 'PULL REQUESTS'
$prs = gh pr list -R $Repo --state open --limit 50 --json number,title,headRefName,labels,mergeable,isDraft,statusCheckRollup,updatedAt |
	ConvertFrom-Json
if (-not $prs) { 'none open' }
foreach ($pr in $prs) {
	$checks = @($pr.statusCheckRollup)
	$ci = if (-not $checks) { 'no CI (docs-only or not started)' }
		elseif ($checks | Where-Object { $_.status -ne 'COMPLETED' }) { 'CI running/queued' }
		elseif ($checks | Where-Object { $_.conclusion -notin 'SUCCESS', 'SKIPPED', 'NEUTRAL' }) { 'CI FAILED' }
		else { 'CI green' }
	$vr = if ($pr.labels.name -contains 'needs-vr-test') { ' needs-vr-test' } else { '' }
	$draft = if ($pr.isDraft) { ' draft' } else { '' }
	"#$($pr.number) [$ci$vr$draft, $($pr.mergeable)] $($pr.title)  ($($pr.headRefName))"
}

# --- main ------------------------------------------------------------------------------------------
Section 'MAIN'
$run = gh run list -R $Repo --branch main --limit 1 --json status,conclusion,headSha,displayTitle | ConvertFrom-Json
if ($run) { "latest CI on main: $($run[0].status) $($run[0].conclusion) $($run[0].headSha.Substring(0,9)) $($run[0].displayTitle)" }
else { 'no CI runs on main' }

# --- Board -----------------------------------------------------------------------------------------
$items = Get-BoardItems
$issues = gh issue list -R $Repo --state all --limit 500 --json number,state,title,labels,milestone | ConvertFrom-Json
$issueByNumber = @{}; foreach ($i in $issues) { $issueByNumber[[int]$i.number] = $i }
$itemByNumber = @{}; foreach ($i in $items) { $itemByNumber[$i.Number] = $i }

Section 'BOARD'
foreach ($status in 'Testing', 'In Progress') {
	$list = $items | Where-Object { $_.Status -eq $status -and ($_.Labels -contains 'task' -or $_.Labels -contains 'bug') }
	foreach ($t in $list) { "$($status.ToUpper().PadRight(12)) #$($t.Number) $($t.Title)" }
}

Section 'INCONSISTENT'
$problems = @()
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
if ($problems) { $problems } else { 'none' }

# --- Cleanup ---------------------------------------------------------------------------------------
Section 'CLEANUP'
$branches = gh api "repos/$Repo/branches?per_page=100" --paginate -q '.[].name' | Where-Object { $_ -notin 'main', 'master' }
$allPrs = gh pr list -R $Repo --state all --limit 200 --json number,state,headRefName | ConvertFrom-Json
$found = $false
foreach ($b in $branches) {
	$pr = $allPrs | Where-Object headRefName -eq $b | Select-Object -First 1
	if (-not $pr) { "branch '$b' has no PR"; $found = $true }
	elseif ($pr.state -ne 'OPEN') { "branch '$b' belongs to $($pr.state.ToLower()) PR #$($pr.number) -> delete"; $found = $true }
}
if (-not $found) { 'no leftover remote branches' }

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

# --- Next ------------------------------------------------------------------------------------------
Section 'NEXT'
& (Join-Path $PSScriptRoot 'next-task.ps1')
