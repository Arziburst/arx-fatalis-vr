<#
.SYNOPSIS
	Start an independent reviewer session for a pull request (decision D-021).

.DESCRIPTION
	Runs `claude --bg` in the repository's main working copy (trusted, D-011) with the prompt
	"/review-pr <pr>": the reviewer reads the PR, the task it closes (body and comments), the decisions
	and catalog rows it refers to, posts one PASS / GAPS comment on the PR and messages the coordinator.
	It changes nothing. Named "ARXVR-<issue> review (PR #<pr>)". Run it from the owner's copy.

.PARAMETER Pr
	The pull request number.

.PARAMETER Model
	Default: sonnet (D-015: reading and checking, no engine work).

.PARAMETER Effort
	Default: high.

.EXAMPLE
	.\scripts\dev\start-reviewer.ps1 184
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(Mandatory, Position = 0)] [int]$Pr,
	[string]$Model = 'sonnet',
	[ValidateSet('low', 'medium', 'high', 'xhigh', 'max')] [string]$Effort = 'high',
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$repo = 'repos/Arziburst/arx-fatalis-vr'

# REST, not GraphQL: the GraphQL quota is shared by every session
$pull = gh api "$repo/pulls/$Pr" | ConvertFrom-Json
if ($LASTEXITCODE -or -not $pull.title) { throw "could not read PR #$Pr" }
$issue = if ($pull.body -match '(?i)(closes|fixes|resolves)\s+#(\d+)') { $Matches[2] } elseif ($pull.title -match '^ARXVR-(\d+)') { $Matches[1] } else { 'PR' }

$session = "ARXVR-$issue review (PR #$Pr)"
$repoDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launchDir = (git -C $repoDir worktree list --porcelain | Select-Object -First 1) -replace '^worktree ', '' -replace '/', '\'

$running = @(claude agents --json 2>$null | ConvertFrom-Json) | Where-Object { $_.kind -eq 'background' -and $_.name -eq $session }
if ($running) { Write-Host "reviewer already runs: $($running[0].id) '$session'" -ForegroundColor Yellow; return }

$claudeArgs = @('--bg', '-n', $session, '--permission-mode', 'bypassPermissions', '--model', $Model, '--effort', $Effort, "/review-pr $Pr")
if ($DryRun) { Write-Host "would run in ${launchDir}: claude $($claudeArgs -join ' ')"; return }

Push-Location $launchDir
try { $out = & claude @claudeArgs 2>&1 } finally { Pop-Location }
if ($LASTEXITCODE) { throw "claude --bg failed (exit $LASTEXITCODE): $out" }
Write-Host ($out -join "`n")
Write-Host "reviewer: $session ($Model, effort $Effort)" -ForegroundColor Cyan
