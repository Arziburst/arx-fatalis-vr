<#
.SYNOPSIS
	Register (or unregister) the locally built Arx Fatalis VR with SteamVR as an application.

.DESCRIPTION
	SteamVR shows a name and a tile image for an application - on the loading card while the game
	starts, and in the SteamVR library - only if it knows the application from a .vrmanifest file.
	For OpenXR applications SteamVR matches the running executable against "binary_path_windows"
	of the manifests listed in <Steam>\config\appconfig.json.

	This script fills the manifest template data\vr\arx-fatalis-vr.vrmanifest with the absolute path
	of this working copy's arx.exe and the 460x215 tile data\vr\steamvr-tile.png (no arguments: the
	game finds Arx Fatalis in the Steam libraries itself, #79; -DataDir adds --data-dir), writes it to
	%LOCALAPPDATA%\ArxFatalisVR\steamvr\arx-fatalis-vr.vrmanifest and adds that path to SteamVR's
	appconfig.json. The game itself does not depend on OpenVR.

	SteamVR reads appconfig.json and the manifests when it starts: restart SteamVR after running
	this script. Re-run it after switching working copies or build configurations, so the manifest
	points at the arx.exe you actually run.

.PARAMETER Config
	Which build to register: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder passed as --data-dir. Default: none - the game finds it in the Steam libraries.

.PARAMETER Unregister
	Remove the manifest from appconfig.json and delete it.

.EXAMPLE
	.\scripts\register-steamvr.ps1
.EXAMPLE
	.\scripts\register-steamvr.ps1 -Config Debug
.EXAMPLE
	.\scripts\register-steamvr.ps1 -Unregister
#>
[CmdletBinding()]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$SourceDir = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build-paths.ps1')
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $SourceDir $Config }

$ManifestDir = Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\steamvr'
$ManifestPath = Join-Path $ManifestDir 'arx-fatalis-vr.vrmanifest'
$Template = Join-Path $SourceDir 'data\vr\arx-fatalis-vr.vrmanifest'
$Tile = Join-Path $SourceDir 'data\vr\steamvr-tile.png'

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }

# JSON string escaping for values substituted into the manifest template (without the surrounding quotes).
function ConvertTo-JsonString([string]$Value) {
	$quoted = ConvertTo-Json $Value -Compress
	$quoted.Substring(1, $quoted.Length - 2)
}

# --- SteamVR app config ----------------------------------------------------------------------------

$steam = Get-SteamPath
if (-not $steam) { throw 'Steam was not found in the registry (HKCU\Software\Valve\Steam). Is Steam installed?' }
$appConfigPath = Join-Path $steam 'config\appconfig.json'

$appConfig = $null
if (Test-Path $appConfigPath) {
	$appConfig = Get-Content -Raw $appConfigPath | ConvertFrom-Json
}
if (-not $appConfig) { $appConfig = [pscustomobject]@{ manifest_paths = @() } }
if (-not ($appConfig.PSObject.Properties.Name -contains 'manifest_paths')) {
	$appConfig | Add-Member -NotePropertyName manifest_paths -NotePropertyValue @()
}
# Keep every other manifest; drop ours (compared case-insensitively, whatever the slashes).
$normalizedOurs = [IO.Path]::GetFullPath($ManifestPath).ToLowerInvariant()
$others = @($appConfig.manifest_paths | Where-Object {
	$_ -and ([IO.Path]::GetFullPath($_).ToLowerInvariant() -ne $normalizedOurs)
})

$steamVrRunning = [bool](Get-Process -Name vrserver, vrmonitor -ErrorAction SilentlyContinue)

if ($Unregister) {
	$appConfig.manifest_paths = $others
	Write-Step "Removing $ManifestPath from $appConfigPath"
	$appConfig | ConvertTo-Json -Depth 5 | Set-Content -Path $appConfigPath -Encoding utf8
	if (Test-Path $ManifestPath) { Remove-Item $ManifestPath }
	if ($steamVrRunning) { Write-Warning 'SteamVR is running: the change takes effect after SteamVR is restarted.' }
	Write-Step 'Done'
	return
}

# --- Manifest --------------------------------------------------------------------------------------

$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }
if (-not (Test-Path $Template)) { throw "Manifest template $Template not found." }
if (-not (Test-Path $Tile)) { throw "Tile image $Tile not found." }

# The game finds the data in the Steam libraries itself (#79): no arguments unless -DataDir is given
$arguments = if ($DataDir) { "--data-dir `"$DataDir`"" } else { '' }

$manifest = Get-Content -Raw $Template
$manifest = $manifest.Replace('@BINARY_PATH_WINDOWS@', (ConvertTo-JsonString ([IO.Path]::GetFullPath($exe))))
$manifest = $manifest.Replace('@IMAGE_PATH@', (ConvertTo-JsonString ([IO.Path]::GetFullPath($Tile))))
$manifest = $manifest.Replace('@ARGUMENTS@', (ConvertTo-JsonString $arguments))

New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
Write-Step "Writing $ManifestPath"
Write-Host "    binary: $exe"
Write-Host "    image:  $Tile"
Write-Host "    args:   $arguments"
Set-Content -Path $ManifestPath -Value $manifest -Encoding utf8 -NoNewline

# --- Register --------------------------------------------------------------------------------------

$appConfig.manifest_paths = @($others) + @($ManifestPath)
Write-Step "Registering the manifest in $appConfigPath"
$appConfig | ConvertTo-Json -Depth 5 | Set-Content -Path $appConfigPath -Encoding utf8

if ($steamVrRunning) {
	Write-Warning 'SteamVR is running: restart it so it picks up the manifest.'
} else {
	Write-Host 'Start SteamVR; it picks up the manifest on startup.'
}
Write-Step 'Done'
