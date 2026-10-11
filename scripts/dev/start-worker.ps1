<#
.SYNOPSIS
	Start a worker for a task through the coordinator core (coordinator/core/scripts/start-worker.ps1).

.DESCRIPTION
	Arx binding of the core's worker start (coordinator/BINDINGS.md). Run it from the owner's copy
	(E:\dev\arx-fatalis-vr\scripts\dev\); plan-next.ps1 prints the line to run. It:
	  1. reads the issue; its body must hold the task's contract in a "## Contract" section with a
	     ```yaml block (coordinator/core/CONTRACT.md) - the coordinator writes it before delegating;
	  2. checks the contract with coordinator/core/scripts/check-contract.ps1 (an ERROR stops here);
	  3. writes the brief: the worker role (coordinator/core/roles/worker.md), the contract, the project
	     rules for workers (coordinator/PROJECT.md, "Workers"), the effort and the report phrase;
	  4. starts the core script: worktree .claude\worktrees\<branch>, session "#<n> worker: <title>",
	     launched from the trusted owner's copy (-LaunchFrom), safety from coordinator\worker-settings.json.

.PARAMETER Issue
	The task's issue number.
.PARAMETER Type
	Branch / commit type (feat, fix, ...). Default feat.
.PARAMETER Name
	The short branch name. Default: from the issue title.
.PARAMETER Model
	The worker's model (CORE section 7; plan-next.ps1 suggests it). Required by the core.
.PARAMETER Effort
	The effort, written into the brief (the CLI has no confirmed effort flag for --bg).
.PARAMETER PermissionMode
	auto (default) or acceptEdits. bypassPermissions is refused by the core script.
.PARAMETER DryRun
	Print what would happen; start nothing.
#>
param(
	[Parameter(Mandatory, Position = 0)] [int]$Issue,
	[ValidateSet('feat', 'fix', 'perf', 'refactor', 'build', 'ci', 'docs', 'test', 'chore')] [string]$Type = 'feat',
	[string]$Name,
	[string]$Model,
	[ValidateSet('low', 'medium', 'high', 'xhigh', 'max')] [string]$Effort,
	[ValidateSet('acceptEdits', 'auto')] [string]$PermissionMode = 'auto',
	[switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$repoRoot = (git -C $PSScriptRoot rev-parse --show-toplevel)
$coord = Join-Path $repoRoot 'coordinator'
$core = Join-Path $coord 'core'
foreach ($p in @($core, (Join-Path $coord 'worker-settings.json'), (Join-Path $coord 'PROJECT.md'))) {
	if (-not (Test-Path -LiteralPath $p)) { throw "missing $p - run this from the owner's copy, where coordinator/ lives" }
}
if (-not $Model) { throw 'pass -Model (plan-next.ps1 prints the line with the model)' }

$issueJson = gh api "repos/Arziburst/arx-fatalis-vr/issues/$Issue" | ConvertFrom-Json
$title = [string]$issueJson.title
$body = [string]$issueJson.body

# 1. The contract: the ```yaml block under "## Contract" in the issue body.
$m = [regex]::Match($body, '(?ms)^##\s+Contract\s*$.*?```ya?ml\s*\r?\n(.*?)\r?\n```')
if (-not $m.Success) { throw "issue #$Issue has no '## Contract' section with a yaml block - write the contract first (CONTRACT.md)" }
$contract = $m.Groups[1].Value.Trim()
if ($contract -notmatch '(?m)^---') { $contract = "---`n$contract`n---" }
$tmp = Join-Path ([IO.Path]::GetTempPath()) "arxvr-contract-$Issue.md"
[IO.File]::WriteAllText($tmp, $contract + "`n")

# 2. Check it.
& (Get-Process -Id $PID).Path -NoProfile -ExecutionPolicy Bypass -File (Join-Path $core 'scripts\check-contract.ps1') $tmp
if ($LASTEXITCODE -ne 0) { throw "check-contract failed for #$Issue - fix the contract in the issue" }

# 3. The brief.
$role = [IO.File]::ReadAllText((Join-Path $core 'roles\worker.md'))
$role = [regex]::Replace($role, '(?s)^---.*?---\s*', '')      # drop the frontmatter
$project = [IO.File]::ReadAllText((Join-Path $coord 'PROJECT.md'))
$rules = [regex]::Match($project, '(?ms)^## Workers.*?(?=^## )').Value.Trim()
if (-not $Name) {
	$Name = ($title.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
	if ($Name.Length -gt 40) { $Name = $Name.Substring(0, 40).Trim('-') }
}
$branch = "ARXVR-$Issue-$Type-$Name"
$effortLine = if ($Effort) { "Work at effort: $Effort." } else { '' }
$brief = @"
$role

## Your contract (issue #${Issue}: $title)
$contract

## Project rules for workers (Arx Fatalis VR)
Read CLAUDE.md and docs/vr/WORKFLOW.md first - they are the project's rules (naming, launches, testing,
PRs, decisions). Branch: $branch. Commits: ARXVR-$Issue <type>: <description>, body "Refs #$Issue".
$rules

$effortLine
When done (or blocked), send the report to the coordinator: SendMessage to "Coordinator", first line
"#$Issue ready for review" (or "#$Issue blocked"), then the YAML report. Do not merge: the coordinator merges.
"@
$briefFile = Join-Path ([IO.Path]::GetTempPath()) "arxvr-brief-$Issue.md"
[IO.File]::WriteAllText($briefFile, $brief)

# 4. Start through the core.
$coreArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $core 'scripts\start-worker.ps1'),
	'-Repo', $repoRoot, '-LaunchFrom', $repoRoot, '-Name', "#$Issue worker: $title", '-Branch', $branch,
	'-Model', $Model, '-BriefFile', $briefFile, '-Safety', (Join-Path $coord 'worker-settings.json'), '-Mode', $PermissionMode)
if ($DryRun) { $coreArgs += '-DryRun' }
& (Get-Process -Id $PID).Path @coreArgs
exit $LASTEXITCODE
