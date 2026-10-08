<#
.SYNOPSIS
	Run the locally built Arx Fatalis VR with the game data from the Steam install of Arx Fatalis.

.DESCRIPTION
	Finds the Arx Fatalis install (Steam app 1700) in the player's Steam libraries and starts arx.exe
	with it as the data directory. Any extra arguments are passed through to arx.exe.

	Test runs start in a window (arx.exe --windowed) so the game does not take over the desktop;
	pass -Fullscreen to use the fullscreen setting from cfg.ini instead. A plain launch goes straight
	to the main menu; add --intro to see the publisher logos and the intro cinematic.

	Until the engine finds the data itself (task #79), this script does the lookup (Find-ArxFatalisSteamInstall in build-paths.ps1).

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder. Default: found automatically in the Steam libraries.

.PARAMETER Fullscreen
	Do not force windowed mode; use the fullscreen setting from cfg.ini.

.EXAMPLE
	.\scripts\run-windows.ps1
.EXAMPLE
	.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
.EXAMPLE
	.\scripts\run-windows.ps1 -Fullscreen -Config Debug -- --no-vr
#>
# PositionalBinding=$false: after "--" everything is positional, and without this "--skiplogo" would bind to -Config.
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[switch]$Fullscreen,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-paths.ps1')
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir (Split-Path -Parent $PSScriptRoot) $Config }

$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }

if (-not $DataDir) { $DataDir = Find-ArxFatalisSteamInstall }
if (-not $DataDir) { throw 'Arx Fatalis (Steam app 1700) was not found in any Steam library. Install it, or pass -DataDir.' }

$modeArgs = @()
if (-not $Fullscreen) { $modeArgs += '--windowed' }

Write-Host "==> Running $exe" -ForegroundColor Cyan
Write-Host "    data: $DataDir"
& $exe --data-dir $DataDir @modeArgs @ArxArgs
