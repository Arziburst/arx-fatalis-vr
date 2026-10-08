<#
.SYNOPSIS
	Run the locally built Arx Fatalis VR with the game data from the Steam install of Arx Fatalis.

.DESCRIPTION
	Finds the Arx Fatalis install (Steam app 1700) in the player's Steam libraries and starts arx.exe
	with it as the data directory. Any extra arguments are passed through to arx.exe.

	Test runs start in a window (arx.exe --windowed) so the game does not take over the desktop;
	pass -Fullscreen to use the fullscreen setting from cfg.ini instead. A plain launch goes straight
	to the main menu and every .cin cinematic is skipped; add --intro to see the publisher logos and
	the intro cinematic, --cinematics to see all cinematics (new-game intro, in-game cutscenes).

	Parallel work (docs/vr/WORKFLOW.md): -Isolated gives this working copy its own user and config
	directory (%LOCALAPPDATA%\ArxFatalisVR\userdirs\<folder>-<hash>: arx.log, cfg.ini, saves, crash
	reports), so a worker's runs never touch the player's files in Saved Games\Arx Libertatis; -Saves
	copies save slots into it. A launch that may start in VR (no --no-vr) takes the machine-wide VR
	lock (scripts/vr-lock.ps1) for as long as the game runs: only one game in VR at a time. The lock
	is waited for up to -VrLockTimeoutSec; a flat launch (--no-vr) needs no lock.

	Until the engine finds the data itself (task #79), this script does the lookup (Find-ArxFatalisSteamInstall in build-paths.ps1).

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder. Default: found automatically in the Steam libraries.

.PARAMETER Fullscreen
	Do not force windowed mode; use the fullscreen setting from cfg.ini.

.PARAMETER Isolated
	Use this working copy's own user and config directory (--user-dir / --config-dir) instead of the
	player's Saved Games\Arx Libertatis. -UserDir names another directory.

.PARAMETER UserDir
	The user and config directory to use (implies -Isolated).

.PARAMETER Saves
	Save slots to copy into the isolated user directory before the launch, e.g. save0000 (needs
	-Isolated). Source: -SaveLibrary, $env:ARXVR_SAVE_LIBRARY, or the player's save folder.

.PARAMETER SaveLibrary
	Folder holding the save slots for -Saves (the test save library, #70).

.PARAMETER TestChannel
	Start with the test channel (--test-dir) of this working copy, so scripts/dev/game-status.ps1 and
	game-cmd.ps1 can talk to the game (docs/vr/TESTING.md).

.PARAMETER VrLockTimeoutSec
	How long to wait for the VR lock when another game runs in VR (default 120). 0: do not wait.

.EXAMPLE
	.\scripts\run-windows.ps1
.EXAMPLE
	.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
.EXAMPLE
	.\scripts\run-windows.ps1 -Fullscreen -Config Debug -- --no-vr
.EXAMPLE
	.\scripts\run-windows.ps1 -Isolated -Saves save0000 -TestChannel -- --no-vr --loadslot 0
#>
# PositionalBinding=$false: after "--" everything is positional, and without this "--skiplogo" would bind to -Config.
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[switch]$Fullscreen,
	[switch]$Isolated,
	[string]$UserDir,
	[string[]]$Saves,
	[string]$SaveLibrary,
	[switch]$TestChannel,
	[int]$VrLockTimeoutSec = 120,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build-paths.ps1')
. (Join-Path $PSScriptRoot 'vr-lock.ps1')
$SourceDir = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $SourceDir $Config }
if (-not $ArxArgs) { $ArxArgs = @() }

$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }

if (-not $DataDir) { $DataDir = Find-ArxFatalisSteamInstall }
if (-not $DataDir) { throw 'Arx Fatalis (Steam app 1700) was not found in any Steam library. Install it, or pass -DataDir.' }

$modeArgs = @()
if (-not $Fullscreen) { $modeArgs += '--windowed' }

# Isolated user and config directory
$dirArgs = @()
if ($UserDir) { $Isolated = $true } elseif ($Isolated) { $UserDir = Get-DefaultUserDir $SourceDir }
if ($Isolated) {
	New-Item -ItemType Directory -Force $UserDir | Out-Null
	$dirArgs += '--user-dir', $UserDir, '--config-dir', $UserDir
} elseif ($Saves) {
	throw '-Saves copies saves into an isolated user directory: add -Isolated (the player''s own saves are never touched)'
}
if ($TestChannel) { $dirArgs += '--test-dir', (Get-DefaultTestDir $SourceDir) }

Write-Host "==> Running $exe" -ForegroundColor Cyan
Write-Host "    data: $DataDir"
if ($Isolated) { Write-Host "    user dir: $UserDir (isolated)" } else { Write-Host "    user dir: $(Get-SharedUserDir) (shared)" }
if ($TestChannel) { Write-Host "    test channel: $(Get-DefaultTestDir $SourceDir)" }
if ($Saves) { Copy-ArxSaves -Names $Saves -UserDir $UserDir -Library $SaveLibrary }

# One game in VR at a time: a launch without --no-vr may start in VR, so it holds the lock while it runs
$lock = $null
if ($ArxArgs -notcontains '--no-vr') {
	$lock = Enter-VrLock -What "run-windows.ps1 in $SourceDir" -TimeoutSec $VrLockTimeoutSec
	if (-not $lock) {
		throw "another game is running in VR ($(Get-VrLockHolder)). Wait for it to end, or add --no-vr for a flat launch."
	}
	Write-Host "    VR lock: taken ($(Get-VrLockPath))"
}

try {
	& $exe --data-dir $DataDir @modeArgs @dirArgs @ArxArgs
	$code = $LASTEXITCODE
} finally {
	Exit-VrLock $lock
}
exit $code
