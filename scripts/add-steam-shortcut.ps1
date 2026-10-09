<#
.SYNOPSIS
	Add the locally built Arx Fatalis VR to Steam as a non-Steam game in the VR library (or remove it).

.DESCRIPTION
	Until Arx Fatalis VR is a Steam app of its own, the way to start it from inside VR - from the
	SteamVR dashboard library, like any Steam VR game - is a non-Steam shortcut with "Include in VR
	Library" set (decision D-004, task #80). This script writes that shortcut into
	<Steam>\userdata\<account>\config\shortcuts.vdf:

	- AppName "Arx Fatalis VR", Exe = this working copy's arx.exe, StartDir = its folder,
	  icon data\vr\ARXVR.ico, OpenVR = 1 (the "Include in VR Library" check box);
	- the library images under the shortcut's app id in config\grid: the 460x215 tile
	  (data\vr\steamvr-tile.png) as <id>.png and the key art as <id>_hero.png.

	A shortcut with the same name is updated in place; every other shortcut stays byte for byte as
	it was. shortcuts.vdf is backed up first (shortcuts.vdf.arxvr-<time>.bak next to it).

	Steam keeps the shortcuts in memory and writes the file back when it exits, so an edit made while
	Steam runs is lost: the script refuses while steam.exe runs (exit Steam first; -Force writes
	anyway). Start Steam afterwards; the game is in the library, and with SteamVR running in the
	SteamVR dashboard library.

	A launch from Steam runs arx.exe directly: not through run-windows.ps1, so no game lock, no
	launch ledger, no fresh-build check - rebuild the working copy before playing.

.PARAMETER Config
	Which build the shortcut starts: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER LaunchOptions
	Arguments for arx.exe, e.g. '--loadslot 0'. Default: none (the main menu; VR is detected by itself).

.PARAMETER AccountId
	Steam account (the folder name under <Steam>\userdata). Default: the logged-in account, else the
	most recent one in config\loginusers.vdf.

.PARAMETER ShortcutsFile
	Work on this shortcuts.vdf instead of the account's (tests). Grid images go to a "grid" folder
	next to it, and the Steam check is skipped.

.PARAMETER Remove
	Remove the shortcut and its grid images.

.PARAMETER Force
	Write even though Steam is running (Steam will probably overwrite the change when it exits).

.EXAMPLE
	.\scripts\add-steam-shortcut.ps1
.EXAMPLE
	.\scripts\add-steam-shortcut.ps1 -LaunchOptions '--loadslot 0'
.EXAMPLE
	.\scripts\add-steam-shortcut.ps1 -Remove
#>
[CmdletBinding()]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$LaunchOptions = '',
	[string]$AccountId,
	[string]$ShortcutsFile,
	[switch]$Remove,
	[switch]$Force
)

$ErrorActionPreference = 'Stop'
$SourceDir = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-paths.ps1')
. (Join-Path $PSScriptRoot 'steam-shortcuts-lib.ps1')
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $SourceDir $Config }

$AppName = 'Arx Fatalis VR'
$Icon = Join-Path $SourceDir 'data\vr\ARXVR.ico'
$Tile = Join-Path $SourceDir 'data\vr\steamvr-tile.png'
$Hero = Join-Path $SourceDir 'data\vr\key-art.png'

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }

# The Steam account whose shortcuts we edit: the logged-in one (registry, while Steam runs or after
# it ran), else the most recent login in loginusers.vdf (SteamID64 -> account id).
function Find-SteamAccountId([string]$Steam) {
	$active = (Get-ItemProperty 'HKCU:\Software\Valve\Steam\ActiveProcess' -Name ActiveUser -ErrorAction SilentlyContinue).ActiveUser
	if ($active) { return "$active" }
	$loginUsers = Join-Path $Steam 'config\loginusers.vdf'
	if (Test-Path $loginUsers) {
		$best = $null
		foreach ($m in [regex]::Matches((Get-Content -Raw $loginUsers), '"(7656\d{13})"\s*\{([^}]*)\}')) {
			$recent = $m.Groups[2].Value -match '"MostRecent"\s+"1"'
			$stamp = if ($m.Groups[2].Value -match '"Timestamp"\s+"(\d+)"') { [long]$Matches[1] } else { 0 }
			$rank = [long]$recent * [long]1e12 + $stamp
			if (-not $best -or $rank -gt $best.Rank) { $best = @{ Rank = $rank; Id = [uint64]$m.Groups[1].Value } }
		}
		if ($best) { return "$($best.Id - [uint64]76561197960265728)" }
	}
	$null
}

# --- Where ------------------------------------------------------------------------------------------

if ($ShortcutsFile) {
	$ShortcutsFile = [IO.Path]::GetFullPath($ShortcutsFile)
	$GridDir = Join-Path (Split-Path -Parent $ShortcutsFile) 'grid'
} else {
	$steam = Get-SteamPath
	if (-not $steam) { throw 'Steam was not found in the registry (HKCU\Software\Valve\Steam). Is Steam installed?' }
	if (-not $AccountId) { $AccountId = Find-SteamAccountId $steam }
	if (-not $AccountId) { throw "No Steam account found (no logged-in user, no $steam\config\loginusers.vdf). Pass -AccountId <folder under $steam\userdata>." }
	$configDir = Join-Path $steam "userdata\$AccountId\config"
	if (-not (Test-Path $configDir)) { throw "$configDir not found. Has account $AccountId logged in to Steam on this PC? Pass -AccountId." }
	$ShortcutsFile = Join-Path $configDir 'shortcuts.vdf'
	$GridDir = Join-Path $configDir 'grid'
	if (Get-Process -Name steam -ErrorAction SilentlyContinue) {
		if (-not $Force) {
			throw 'Steam is running: it keeps the shortcuts in memory and overwrites shortcuts.vdf when it exits. Exit Steam (Steam > Exit), run this script again, then start Steam. (-Force writes anyway.)'
		}
		Write-Warning 'Steam is running (-Force): it will probably overwrite this change when it exits.'
	}
}

Write-Step "Shortcuts: $ShortcutsFile"
[byte[]]$original = if (Test-Path $ShortcutsFile) { [IO.File]::ReadAllBytes($ShortcutsFile) } else { , [byte[]]::new(0) }
$nodes = ConvertFrom-BinaryVdf $original
$shortcuts = Get-ShortcutsMap $nodes

# --- Change -----------------------------------------------------------------------------------------

$staleIds = @(Find-Shortcut $shortcuts $AppName | ForEach-Object {
	$id = Get-VdfInt32Value $_ 'appid'
	if ($null -ne $id) { [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$id), 0) }
})

if ($Remove) {
	$removed = Remove-Shortcut $shortcuts $AppName
	if (-not $removed.Count -and -not $staleIds.Count) {
		Write-Host "    no '$AppName' shortcut - nothing to remove"
	}
	$appId = $null
} else {
	$exe = [IO.Path]::GetFullPath((Join-Path $BuildDir 'arx.exe'))
	if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }
	foreach ($file in $Icon, $Tile, $Hero) { if (-not (Test-Path $file)) { throw "$file not found." } }
	$appId = Set-Shortcut $shortcuts $AppName $exe ([IO.Path]::GetFullPath($Icon)) $LaunchOptions $true
	Write-Host "    exe:     $exe"
	Write-Host "    options: $(if ($LaunchOptions) { $LaunchOptions } else { '(none)' })"
	Write-Host "    app id:  $appId (steam://rungameid/$(Get-SteamShortcutGameId $appId))"
	Write-Host '    VR library: yes'
}

[byte[]]$updated = ConvertTo-BinaryVdf $nodes
if ([Linq.Enumerable]::SequenceEqual($original, $updated)) {
	Write-Step 'shortcuts.vdf already up to date'
} else {
	if ($original.Length) {
		$backup = "$ShortcutsFile.arxvr-$(Get-Date -Format 'yyyyMMdd-HHmmss').bak"
		Copy-Item $ShortcutsFile $backup
		Write-Host "    backup:  $backup"
	}
	New-Item -ItemType Directory -Force (Split-Path -Parent $ShortcutsFile) | Out-Null
	[IO.File]::WriteAllBytes($ShortcutsFile, $updated)
	Write-Step 'shortcuts.vdf written'
}

# --- Grid images ------------------------------------------------------------------------------------

# Images of an earlier shortcut (another arx.exe path gives another app id) go away.
foreach ($id in ($staleIds | Where-Object { $_ -ne $appId } | Select-Object -Unique)) {
	foreach ($name in "$id.png", "${id}_hero.png") {
		$path = Join-Path $GridDir $name
		if (Test-Path $path) { Remove-Item $path; Write-Host "    removed $path" }
	}
}
if ($appId) {
	New-Item -ItemType Directory -Force $GridDir | Out-Null
	Copy-Item $Tile (Join-Path $GridDir "$appId.png") -Force
	Copy-Item $Hero (Join-Path $GridDir "${appId}_hero.png") -Force
	Write-Host "    images:  $GridDir\$appId.png, ${appId}_hero.png"
}

if ($Remove) {
	Write-Step "Removed '$AppName'. Start Steam."
} else {
	Write-Step "Done. Start Steam: '$AppName' is in the library; with SteamVR running it is in the SteamVR dashboard library too."
}
