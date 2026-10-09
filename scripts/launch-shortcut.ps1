<#
.SYNOPSIS
	What the "Arx Fatalis VR" desktop shortcut runs: rebuild when the build is stale, then start the game.

.DESCRIPTION
	Generated launcher (%LOCALAPPDATA%\ArxFatalisVR\launch.ps1, see create-desktop-shortcut.ps1) calls this
	script from the working copy, so changes here need no regeneration.
	The build is stale - and is rebuilt first - when there is no arx.exe, no built-from.txt, its commit
	differs from the copy's HEAD, or either the stamp or the copy has uncommitted changes ("<sha> dirty");
	the same rule as scripts\dev\test-pr.ps1. An up-to-date build starts at once. The game never starts
	after a failed build: the build output stays on screen and the window waits for Enter.

.PARAMETER WorkingCopy
	The working copy to run. Default: the one containing this script.

.PARAMETER Config
	Which build: Release (default), RelWithDebInfo or Debug.
#>
[CmdletBinding()]
param(
	[string]$WorkingCopy = (Split-Path -Parent $PSScriptRoot),
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')] [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-paths.ps1')

function Short([string]$Sha) { if ($Sha -and $Sha.Length -gt 9) { $Sha.Substring(0, 9) } else { $Sha } }

$host.UI.RawUI.WindowTitle = 'Arx Fatalis VR'
try {
	$branch = (git -C $WorkingCopy branch --show-current)
	$head = (git -C $WorkingCopy rev-parse HEAD).Trim()
	$dirty = @(git -C $WorkingCopy status --porcelain)
	Write-Host "Arx Fatalis VR - branch: $branch @ $(Short $head)" -ForegroundColor Cyan

	$buildDir = Get-DefaultBuildDir $WorkingCopy $Config
	$stampFile = Join-Path $buildDir 'built-from.txt'
	$stamp = if (Test-Path $stampFile) { (Get-Content $stampFile -Raw).Trim() } else { '' }
	$reason = if (-not (Test-Path (Join-Path $buildDir 'arx.exe'))) { 'no build yet' }
		elseif (-not $stamp) { 'the build has no built-from.txt' }
		elseif ($dirty) { 'the working copy has uncommitted changes' }
		elseif ($stamp -ne $head) { "built from '$(Short $stamp)', HEAD is $(Short $head)" }

	if ($reason) {
		Write-Host "building $(Short $head) ... ($reason)" -ForegroundColor Yellow
		& (Join-Path $WorkingCopy 'scripts\build-windows.ps1') -Config $Config
		if (-not $?) { throw 'Build failed - the game was not started.' }
	} else {
		Write-Host "up to date (built from $(Short $head))" -ForegroundColor Green
	}

	& (Join-Path $WorkingCopy 'scripts\run-windows.ps1') -Config $Config
	if ($LASTEXITCODE) { throw "arx.exe exited with code $LASTEXITCODE" }
} catch {
	Write-Host $_ -ForegroundColor Red
	Read-Host 'Press Enter to close'
}
