<#
.SYNOPSIS
	Start a worker session for a task: its own worktree and a background Claude Code session running /next-task.

.DESCRIPTION
	What the coordinator runs to hand out a task in parallel mode (docs/vr/WORKFLOW.md, "Parallel work";
	decision D-011). Run it from the owner's copy (C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr\scripts\dev\),
	so the worktree belongs to the owner's repository. It
	  1. creates the task's worktree with new-worktree.ps1 (from origin/main, outside OneDrive), or reuses it;
	  2. starts `claude --bg` named "ARXVR-<n> <issue title>" with the prompt "/next-task <n> worktree=<path>"
	     and the permission mode the owner chose for workers (bypassPermissions);
	  3. prints the session id and how to look at it.
	The session is launched in the repository's main working copy (the owner's copy), not in the worktree:
	Claude Code starts a background session only in a folder the owner trusted, and that trust is per
	folder - it does not extend to new worktrees. /next-task enters the worktree (EnterWorktree) as its
	very first step when it gets worktree=<path>.
	A worker that already runs for that worktree or under that name is not started twice.

	The owner follows the workers with `claude agents` and opens one with `claude attach <id>` - to answer
	a question, or to reply /accept-task or /reject-task after a VR test.

.PARAMETER Issue
	The task's issue number.

.PARAMETER Type
	The change type of the branch (feat, fix, build, ...). Default: feat.

.PARAMETER Name
	The short name of the branch. Default: from the issue title (new-worktree.ps1).

.PARAMETER Model
	The model of the worker session (fable, opus, sonnet or a model id). Default (decision D-015): only
	area:build / area:docs -> sonnet; any other area -> opus. Fable only when the coordinator passes
	-Model fable for a really large and hard task.

.PARAMETER Effort
	Effort level of the worker session (low, medium, high, xhigh, max). Default by model: sonnet -> medium,
	opus -> high, fable -> xhigh. The coordinator raises it for hard tasks and lowers it for routine ones.

.PARAMETER PermissionMode
	Permission mode of the worker session. Default: bypassPermissions (D-011).

.PARAMETER Prompt
	The first prompt. Default: "/next-task <Issue> worktree=<path of the worktree>".

.PARAMETER DryRun
	Create the worktree and print the claude command line without starting the session.

.EXAMPLE
	.\scripts\dev\start-worker.ps1 18
.EXAMPLE
	.\scripts\dev\start-worker.ps1 93 -Type fix -Name exit-crash-dangling-owner
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(Mandatory, Position = 0)] [int]$Issue,
	[ValidateSet('feat', 'fix', 'perf', 'refactor', 'build', 'ci', 'docs', 'test', 'chore')] [string]$Type = 'feat',
	[string]$Name,
	[string]$Model,
	[ValidateSet('low', 'medium', 'high', 'xhigh', 'max')] [string]$Effort,
	[ValidateSet('acceptEdits', 'auto', 'bypassPermissions', 'manual', 'dontAsk')] [string]$PermissionMode = 'bypassPermissions',
	[string]$Prompt,
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$repo = 'Arziburst/arx-fatalis-vr'

. (Join-Path $PSScriptRoot 'github-lib.ps1') # REST instead of `gh issue view` (#186); the lib reads $repo (variable names ignore case)
$info = Get-GhIssue $Issue
if (-not $info.Title) { throw "could not read #$Issue" }
$title = $info.Title

# D-015: build and docs -> Sonnet, other engine work -> Opus; Fable only when the coordinator asks for it
if (-not $Model) {
	$areas = @($info.Labels | Where-Object { $_ -like 'area:*' })
	$Model = if ($areas -and -not ($areas | Where-Object { $_ -notin 'area:build', 'area:docs' })) { 'sonnet' } else { 'opus' }
	Write-Host "model:    $Model (D-015, labels: $(if ($areas) { $areas -join ', ' } else { 'no area' }))"
}
if (-not $Effort) { $Effort = switch -Wildcard ($Model) { '*sonnet*' { 'medium' } '*fable*' { 'xhigh' } default { 'high' } } }
Write-Host "effort:   $Effort"

$wtArgs = @{ Issue = $Issue; Type = $Type; PassThru = $true }
if ($Name) { $wtArgs.Name = $Name }
$path = & (Join-Path $PSScriptRoot 'new-worktree.ps1') @wtArgs | Select-Object -Last 1
if (-not $path -or -not (Test-Path $path)) { throw "new-worktree.ps1 returned no worktree for #$Issue" }

# The main working copy of this repository (first entry of the worktree list): trusted, so --bg may start there
$repoDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launchDir = (git -C $repoDir worktree list --porcelain | Select-Object -First 1) -replace '^worktree ', '' -replace '/', '\'
if (-not (Test-Path $launchDir)) { throw "could not find the main working copy of $repoDir" }

# Same form as scripts/dev/hooks/session-title.ps1 sets for /next-task
$session = "ARXVR-$Issue $title"
if ($session.Length -gt 80) { $session = $session.Substring(0, 77) + '...' }

# One worker per task: a live background session in the worktree or under the same name is already on it
$running = @(claude agents --json 2>$null | ConvertFrom-Json) | Where-Object {
	$_.kind -eq 'background' -and (($_.cwd -and $_.cwd.TrimEnd('\') -ieq $path.TrimEnd('\')) -or $_.name -eq $session)
}
if ($running) {
	Write-Host "a worker for #$Issue already runs: $($running[0].id) '$($running[0].name)' ($($running[0].status))" -ForegroundColor Yellow
	Write-Host "open it: claude attach $($running[0].id)"
	return
}

if (-not $Prompt) { $Prompt = "/next-task $Issue worktree=$path" }

$claudeArgs = @('--bg', '-n', $session, '--permission-mode', $PermissionMode, '--model', $Model, '--effort', $Effort, $Prompt)

if ($DryRun) {
	Write-Host "would run in ${launchDir}:"
	$shown = $claudeArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
	Write-Host "  claude $($shown -join ' ')"
	return
}

Push-Location $launchDir
try { $out = & claude @claudeArgs 2>&1 } finally { Pop-Location }
if ($LASTEXITCODE) {
	if ("$out" -match 'disclaimer') {
		# Claude Code wants a person to accept the bypass-mode disclaimer once; nobody can do it for them
		throw "the owner has to accept the bypass-permissions disclaimer once: run 'claude --dangerously-skip-permissions' in a terminal, accept, exit - then start the worker again"
	}
	throw "claude --bg failed (exit $LASTEXITCODE): $out"
}

Write-Host ($out -join "`n")
Write-Host "worker:   $session ($Model, effort $Effort)" -ForegroundColor Cyan
Write-Host "worktree: $path"
Write-Host "follow:   claude agents   |   claude attach <id>   |   claude logs <id>"
