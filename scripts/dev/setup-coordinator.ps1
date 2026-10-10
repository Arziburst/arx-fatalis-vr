<#
.SYNOPSIS
	Prepare this machine for the coordinator / worker loop (coordinator/BINDINGS.md, D-011, D-013).

.DESCRIPTION
	Sets the owner's personal Claude Code settings (~/.claude/settings.json) that the loop needs, keeping
	everything else in that file:
	  - statusLine -> scripts/dev/statusline.ps1 of the main working copy (model, context, 5-hour and
	    weekly limits; writes ~/.claude/statusline/last.json, which the coordinator reads);
	  - crossSessionInbound = accept (the coordinator's messages reach the workers without the owner);
	  - autoContinueAtUsageLimit = true (sessions wait for the 5-hour reset and continue).
	Then checks what only the owner can do and prints the missing steps:
	  - the bypass-permissions disclaimer accepted once (workers run without prompts, D-011);
	  - the main working copy trusted (background sessions start only in a trusted folder);
	  - gh logged in, Claude Code present, the worktree root, the Arx Fatalis install.

.PARAMETER Check
	Only report; change nothing.

.EXAMPLE
	.\scripts\dev\setup-coordinator.ps1
.EXAMPLE
	.\scripts\dev\setup-coordinator.ps1 -Check
#>
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot '..\build-paths.ps1')

$repoDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$mainCopy = (git -C $repoDir worktree list --porcelain | Select-Object -First 1) -replace '^worktree ', '' -replace '/', '\'
$statusScript = (Join-Path $mainCopy 'scripts\dev\statusline.ps1') -replace '\\', '/'
$settingsPath = Join-Path $env:USERPROFILE '.claude\settings.json'
$missing = @()

function Write-State([bool]$ok, [string]$text) {
	if ($ok) { Write-Host "  ok    $text" -ForegroundColor Green } else { Write-Host "  todo  $text" -ForegroundColor Yellow }
}

Write-Host "main working copy: $mainCopy" -ForegroundColor Cyan

# --- Personal settings --------------------------------------------------------------------------------
$settings = if (Test-Path $settingsPath) { Get-Content $settingsPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{} }
$want = [ordered]@{
	statusLine               = [pscustomobject]@{ type = 'command'; command = "pwsh -NoProfile -ExecutionPolicy Bypass -File `"$statusScript`"" }
	crossSessionInbound      = 'accept'
	autoContinueAtUsageLimit = $true
	autoCompactWindow        = 900000   # compact at 900k of the 1M context: the coordinator lives long (#217)
}
$changed = $false
foreach ($key in $want.Keys) {
	$current = $settings.PSObject.Properties[$key]
	$same = $current -and (($current.Value | ConvertTo-Json -Compress -Depth 5) -eq ($want[$key] | ConvertTo-Json -Compress -Depth 5))
	Write-State $same "settings.json $key"
	if (-not $same) {
		$changed = $true
		if (-not $Check) { $settings | Add-Member -NotePropertyName $key -NotePropertyValue $want[$key] -Force }
	}
}
if ($changed -and -not $Check) {
	New-Item -ItemType Directory -Force (Split-Path $settingsPath) | Out-Null
	if (Test-Path $settingsPath) { Copy-Item $settingsPath "$settingsPath.bak" -Force }
	$settings | ConvertTo-Json -Depth 20 | Set-Content $settingsPath -Encoding utf8
	Write-Host "  wrote $settingsPath (previous copy: settings.json.bak)"
}

# --- What only the owner can do ----------------------------------------------------------------------
$bypass = [bool]$settings.skipDangerousModePermissionPrompt
Write-State $bypass 'bypass-permissions disclaimer accepted'
if (-not $bypass) { $missing += "Run 'claude --dangerously-skip-permissions' in a terminal, pick 'Yes, I accept' with the arrow keys (the default is 'No, exit'), then /exit." }

$trusted = $false
$claudeJson = Join-Path $env:USERPROFILE '.claude.json'
if (Test-Path $claudeJson) {
	$projects = (Get-Content $claudeJson -Raw | ConvertFrom-Json).projects
	foreach ($p in $projects.PSObject.Properties) {
		if (($p.Name -replace '/', '\').TrimEnd('\') -ieq $mainCopy.TrimEnd('\') -and $p.Value.hasTrustDialogAccepted) { $trusted = $true }
	}
}
Write-State $trusted "main working copy trusted by Claude Code"
if (-not $trusted) { $missing += "Run 'claude' once in $mainCopy and answer 'Yes, proceed' to the trust question, then /exit." }

$gh = $false
try { gh auth status 2>&1 | Out-Null; $gh = ($LASTEXITCODE -eq 0) } catch {}
Write-State $gh 'gh logged in'
if (-not $gh) { $missing += "Run 'gh auth login' (GitHub account with access to Arziburst/arx-fatalis-vr)." }

$claude = [bool](Get-Command claude -ErrorAction SilentlyContinue)
Write-State $claude "Claude Code installed$(if ($claude) { ': ' + (claude --version 2>$null | Select-Object -First 1) })"
if (-not $claude) { $missing += 'Install Claude Code.' }

$wtRoot = if ($env:ARXVR_WORKTREE_ROOT) { $env:ARXVR_WORKTREE_ROOT } else { Join-Path $env:USERPROFILE 'arx-fatalis-vr-wt' }
Write-State $true "worker worktrees go to $wtRoot (created on demand; ARXVR_WORKTREE_ROOT changes it)"

$data = Find-ArxFatalisSteamInstall
Write-State ([bool]$data) "Arx Fatalis (Steam app 1700)$(if ($data) { ': ' + $data })"
if (-not $data) { $missing += 'Install Arx Fatalis from Steam (app 1700).' }

if ($missing) {
	Write-Host "`nSteps for the owner:" -ForegroundColor Yellow
	$i = 1; foreach ($m in $missing) { Write-Host "  $i. $m"; $i++ }
	exit 1
}
Write-Host "`nThis machine is ready for the coordinator loop." -ForegroundColor Green
