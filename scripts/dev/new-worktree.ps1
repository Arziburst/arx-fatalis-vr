<#
.SYNOPSIS
	Create (or remove) the git worktree of a task: a separate working copy outside OneDrive, from origin/main.

.DESCRIPTION
	Parallel work (docs/vr/WORKFLOW.md): every worker session gets its own working copy, a git
	worktree at C:\Users\<user>\arx-fatalis-vr-wt\ARXVR-<issue>-<type>-<name> on the branch of the same
	name. The branch is cut from origin/main - never from master, the untouched upstream mirror that
	git sees as the default branch (docs/vr/TRAPS.md) - and the script checks that afterwards.
	If the branch already exists on origin (a task being resumed), the worktree tracks it instead.
	Running the script again for the same branch prints the existing worktree and changes nothing.

	-Remove deletes the worktree of a finished task together with its build directory, isolated user
	directory and test channel directory, and its local branch when that is merged into origin/main.
	A worktree with uncommitted changes is refused (list them, commit or drop them; -Force overrides).

	The script can run from any working copy of the repository (the owner's copy or a worktree).

.PARAMETER Issue
	The task's issue number. Without -Name the name is made from the issue title (gh).

.PARAMETER Type
	The change type of the branch (feat, fix, build, ...; see WORKFLOW.md). Default: feat.

.PARAMETER Name
	The short name of the branch, lowercase words joined with '-'. Default: from the issue title.

.PARAMETER Branch
	The full branch name instead of -Issue/-Type/-Name (what test-pr.ps1 passes).

.PARAMETER Root
	Where the worktrees live. Default: $env:ARXVR_WORKTREE_ROOT or %USERPROFILE%\arx-fatalis-vr-wt.

.PARAMETER Remove
	Remove the worktree (and its derived directories, and its merged local branch) instead of creating it.

.PARAMETER Force
	With -Remove: remove even with uncommitted changes.

.PARAMETER PassThru
	Return the worktree path as the script's output (for other scripts).

.EXAMPLE
	.\scripts\dev\new-worktree.ps1 -Issue 18 -Type feat -Name per-eye-swapchains
.EXAMPLE
	.\scripts\dev\new-worktree.ps1 -Issue 18 -Remove
.EXAMPLE
	.\scripts\dev\new-worktree.ps1 -Branch ARXVR-18-feat-per-eye-swapchains -PassThru
#>
[CmdletBinding(DefaultParameterSetName = 'Task')]
param(
	[Parameter(ParameterSetName = 'Task', Mandatory, Position = 0)] [int]$Issue,
	[Parameter(ParameterSetName = 'Task')]
	[ValidateSet('feat', 'fix', 'perf', 'refactor', 'build', 'ci', 'docs', 'test', 'chore')] [string]$Type = 'feat',
	[Parameter(ParameterSetName = 'Task')] [string]$Name,
	[Parameter(ParameterSetName = 'Branch', Mandatory)] [string]$Branch,
	[string]$Root,
	[switch]$Remove,
	[switch]$Force,
	[switch]$PassThru
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'board.ps1')
. (Join-Path $PSScriptRoot '..\build-paths.ps1')

if (-not $Root) { $Root = if ($env:ARXVR_WORKTREE_ROOT) { $env:ARXVR_WORKTREE_ROOT } else { Join-Path $env:USERPROFILE 'arx-fatalis-vr-wt' } }
$RepoDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Invoke-Git([string[]]$Arguments) {
	$out = & git -C $RepoDir @Arguments 2>&1
	if ($LASTEXITCODE) { throw "git $($Arguments -join ' ') failed: $out" }
	$out
}

# All worktrees of this repository: Path, Head, Branch (short name or $null when detached)
function Get-Worktrees {
	$list = @()
	$current = $null
	foreach ($line in (Invoke-Git 'worktree', 'list', '--porcelain')) {
		if ($line -match '^worktree (.+)$') { $current = [pscustomobject]@{ Path = $Matches[1] -replace '/', '\'; Head = ''; Branch = $null }; $list += $current }
		elseif ($line -match '^HEAD (\w+)$') { $current.Head = $Matches[1] }
		elseif ($line -match '^branch refs/heads/(.+)$') { $current.Branch = $Matches[1] }
	}
	$list
}

function ConvertTo-Slug([string]$Text) {
	$slug = ($Text.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
	if ($slug.Length -gt 40) { $slug = $slug.Substring(0, 40).TrimEnd('-') }
	$slug
}

# --- Resolve the branch name --------------------------------------------------------------------------

if ($PSCmdlet.ParameterSetName -eq 'Task') {
	if ($Remove -and -not $Name) {
		# Any worktree of this issue, whatever its type and name
		$match = @(Get-Worktrees | Where-Object { $_.Branch -like "ARXVR-$Issue-*" })
		if ($match.Count -gt 1) { throw "several worktrees belong to #${Issue}: $($match.Branch -join ', ') - pass -Branch" }
		if ($match) { $Branch = $match[0].Branch }
		else { Write-Host "no worktree for #$Issue"; return }
	} else {
		if (-not $Name) {
			$title = gh issue view $Issue -R $Repo --json title -q .title
			if ($LASTEXITCODE -or -not $title) { throw "could not read the title of #$Issue - pass -Name" }
			$Name = ConvertTo-Slug $title
		}
		$Branch = "ARXVR-$Issue-$Type-$(ConvertTo-Slug $Name)"
	}
}
if ($Branch -notmatch '^ARXVR-\d+-[a-z]+-[a-z0-9-]+$') { throw "branch '$Branch' does not follow ARXVR-<issue>-<type>-<short-name> (WORKFLOW.md)" }

$existing = Get-Worktrees | Where-Object Branch -eq $Branch | Select-Object -First 1

# --- Remove -------------------------------------------------------------------------------------------

if ($Remove) {
	if (-not $existing) { Write-Host "no worktree on branch $Branch"; return }
	$path = $existing.Path
	if (Test-Path $path) {
		$dirty = @(git -C $path status --porcelain 2>$null)
		if ($dirty -and -not $Force) {
			throw "worktree $path has uncommitted changes - commit or discard them first, or pass -Force:`n  $($dirty -join "`n  ")"
		}
		# Worktrees made by a Claude Code session are locked (git worktree lock); removal needs them unlocked
		& git -C $RepoDir worktree unlock $path 2>&1 | Out-Null
		Invoke-Git 'worktree', 'remove', $(if ($Force) { '--force' } else { '--' }), $path | Out-Null
	} else {
		Invoke-Git 'worktree', 'prune' | Out-Null
	}
	Write-Host "removed worktree $path"
	foreach ($dir in (Join-Path $env:LOCALAPPDATA "ArxFatalisVR\build\$(Get-WorkingCopyKey $path)"), (Get-DefaultUserDir $path), (Join-Path $env:LOCALAPPDATA "ArxFatalisVR\test\$(Get-WorkingCopyKey $path)")) {
		if (Test-Path $dir) { Remove-Item -Recurse -Force $dir; Write-Host "removed $dir" }
	}
	Invoke-Git 'fetch', '-q', '--prune', 'origin' | Out-Null
	$merged = @(Invoke-Git 'branch', '--merged', 'origin/main', '--format', '%(refname:short)') -contains $Branch
	if ($merged) { Invoke-Git 'branch', '-d', $Branch | Out-Null; Write-Host "deleted local branch $Branch (merged into origin/main)" }
	elseif (@(Invoke-Git 'branch', '--list', $Branch)) { Write-Host "kept local branch $Branch - not merged into origin/main (git branch -D $Branch to drop it)" }
	return
}

# --- Create -------------------------------------------------------------------------------------------

Invoke-Git 'fetch', '-q', '--prune', 'origin' | Out-Null
$mainSha = (Invoke-Git 'rev-parse', 'origin/main').Trim()

if ($existing) {
	Write-Host "worktree exists: $($existing.Path) (branch $Branch @ $($existing.Head.Substring(0, 9)))"
	if ($PassThru) { return $existing.Path }
	return
}

$path = Join-Path $Root $Branch
if (Test-Path $path) {
	$other = Get-Worktrees | Where-Object { $_.Path -ieq $path }
	if ($other) { throw "$path is a worktree on branch '$($other.Branch)', not '$Branch'" }
	throw "$path exists but is not a registered worktree - remove the folder (or 'git worktree prune') first"
}
New-Item -ItemType Directory -Force $Root | Out-Null

$remote = @(Invoke-Git 'branch', '-r', '--list', "origin/$Branch")
$local = @(Invoke-Git 'branch', '--list', $Branch)
if ($remote) {
	if ($local) { Invoke-Git 'worktree', 'add', $path, $Branch | Out-Null }
	else { Invoke-Git 'worktree', 'add', '--track', '-b', $Branch, $path, "origin/$Branch" | Out-Null }
	$from = "origin/$Branch (resumed)"
} elseif ($local) {
	Invoke-Git 'worktree', 'add', $path, $Branch | Out-Null
	$from = 'the existing local branch'
} else {
	Invoke-Git 'worktree', 'add', '--no-track', '-b', $Branch, $path, 'origin/main' | Out-Null
	$from = "origin/main @ $($mainSha.Substring(0, 9))"
}

# The trap of TRAPS.md: a branch cut from master has none of our work
$base = (git -C $path merge-base HEAD origin/main 2>$null)
if (-not $base) { throw "${path}: HEAD shares no history with origin/main - the branch was cut from the wrong base; remove it with -Remove -Force" }
$head = (git -C $path rev-parse HEAD).Trim()

Write-Host "worktree: $path" -ForegroundColor Cyan
Write-Host "branch:   $Branch from $from (HEAD $($head.Substring(0, 9)), merge base with origin/main $($base.Trim().Substring(0, 9)))"
Write-Host "next:     cd $path; claude    then /next-task $(if ($Issue) { $Issue } else { ($Branch -split '-')[1] })"
if ($PassThru) { return $path }
