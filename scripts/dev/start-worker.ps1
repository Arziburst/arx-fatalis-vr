<#
.SYNOPSIS
	Start a worker session for a task: its own worktree and a background Claude Code session running /next-task.

.DESCRIPTION
	What the coordinator runs to hand out a task in parallel mode (docs/vr/WORKFLOW.md, "Parallel work";
	decision D-011). It
	  1. creates the task's worktree with new-worktree.ps1 (from origin/main, outside OneDrive), or reuses it;
	  2. starts `claude --bg` in that worktree, named "ARXVR-<n> <issue title>", with the prompt
	     "/next-task <n>" and the permission mode the owner chose for workers (bypassPermissions);
	  3. prints the session id and how to look at it.
	A background session that already runs in that worktree is not started twice.

	The owner follows the workers with `claude agents` and opens one with `claude attach <id>` - to answer
	a question, or to reply /accept-task or /reject-task after a VR test.

.PARAMETER Issue
	The task's issue number.

.PARAMETER Type
	The change type of the branch (feat, fix, build, ...). Default: feat.

.PARAMETER Name
	The short name of the branch. Default: from the issue title (new-worktree.ps1).

.PARAMETER Model
	The model of the worker session. Default: the owner's default model.

.PARAMETER PermissionMode
	Permission mode of the worker session. Default: bypassPermissions (D-011).

.PARAMETER Prompt
	The first prompt. Default: "/next-task <Issue>".

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
	[ValidateSet('acceptEdits', 'auto', 'bypassPermissions', 'manual', 'dontAsk')] [string]$PermissionMode = 'bypassPermissions',
	[string]$Prompt,
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$repo = 'Arziburst/arx-fatalis-vr'

$title = gh issue view $Issue -R $repo --json title -q .title
if ($LASTEXITCODE -or -not $title) { throw "could not read the title of #$Issue" }

$wtArgs = @{ Issue = $Issue; Type = $Type; PassThru = $true }
if ($Name) { $wtArgs.Name = $Name }
$path = & (Join-Path $PSScriptRoot 'new-worktree.ps1') @wtArgs | Select-Object -Last 1
if (-not $path -or -not (Test-Path $path)) { throw "new-worktree.ps1 returned no worktree for #$Issue" }

# One worker per worktree: a live background session there means the task is already being worked on
$running = @(claude agents --json 2>$null | ConvertFrom-Json) |
	Where-Object { $_.kind -eq 'background' -and $_.cwd -and ($_.cwd.TrimEnd('\') -ieq $path.TrimEnd('\')) }
if ($running) {
	Write-Host "a background session already runs in ${path}: $($running[0].id) '$($running[0].name)' ($($running[0].status))" -ForegroundColor Yellow
	Write-Host "open it: claude attach $($running[0].id)"
	return
}

# Same form as scripts/dev/hooks/session-title.ps1 sets for /next-task
$session = "ARXVR-$Issue $title"
if ($session.Length -gt 80) { $session = $session.Substring(0, 77) + '...' }
if (-not $Prompt) { $Prompt = "/next-task $Issue" }

$claudeArgs = @('--bg', '-n', $session, '--permission-mode', $PermissionMode)
if ($Model) { $claudeArgs += '--model', $Model }
$claudeArgs += $Prompt

if ($DryRun) {
	Write-Host "would run in ${path}:"
	$shown = $claudeArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
	Write-Host "  claude $($shown -join ' ')"
	return
}

Push-Location $path
try { $out = & claude @claudeArgs 2>&1 } finally { Pop-Location }
if ($LASTEXITCODE) {
	if ("$out" -match 'disclaimer') {
		# Claude Code wants a person to accept the bypass-mode disclaimer once; nobody can do it for them
		throw "the owner has to accept the bypass-permissions disclaimer once: run 'claude --dangerously-skip-permissions' in a terminal, accept, exit - then start the worker again"
	}
	throw "claude --bg failed (exit $LASTEXITCODE): $out"
}

Write-Host ($out -join "`n")
Write-Host "worker:   $session" -ForegroundColor Cyan
Write-Host "worktree: $path"
Write-Host "follow:   claude agents   |   claude attach <id>   |   claude logs <id>"
