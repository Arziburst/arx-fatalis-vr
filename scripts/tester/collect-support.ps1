<#
.SYNOPSIS
	Collects what a bug report needs into one zip on the Desktop. Nothing is uploaded: you send the zip yourself.

.DESCRIPTION
	Copies the game's logs (arx.log and the previous runs), the settings (cfg.ini), crash reports, the
	SteamVR logs, and writes system.txt: Windows version, graphics card and driver, the active OpenXR runtime,
	whether SteamVR is running, the build id of the package. No saves, no game data, no personal files.

.PARAMETER OutDir
	Where the zip goes (default: the Desktop).

.PARAMETER UserDir
	The game's user directory (default: Saved Games\Arx Libertatis).
#>
[CmdletBinding()]
param(
	[string]$OutDir = [Environment]::GetFolderPath('Desktop'),
	[string]$UserDir = (Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Saved Games\Arx Libertatis')
)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$stage = Join-Path ([IO.Path]::GetTempPath()) "arxvr-support-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
New-Item -ItemType Directory -Force $stage | Out-Null

function Copy-IfExists([string]$From, [string]$ToName) {
	if (Test-Path $From) { Copy-Item -Recurse -Force $From (Join-Path $stage $ToName); Write-Host "  + $ToName" }
}

# The game's user directory: arx.log, arx.1.log ... (previous runs), cfg.ini, crashes
New-Item -ItemType Directory -Force (Join-Path $stage 'game') | Out-Null
if (Test-Path $UserDir) {
	Get-ChildItem $UserDir -Filter 'arx*.log' -ErrorAction SilentlyContinue | ForEach-Object { Copy-IfExists $_.FullName "game\$($_.Name)" }
	Copy-IfExists (Join-Path $UserDir 'cfg.ini') 'game\cfg.ini'
	Copy-IfExists (Join-Path $UserDir 'crashes') 'game\crashes'
} else {
	Write-Host "  (no game user directory at $UserDir - has the game been started?)"
}

# SteamVR logs
$steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
if ($steam) {
	New-Item -ItemType Directory -Force (Join-Path $stage 'steamvr') | Out-Null
	foreach ($n in 'vrserver.txt', 'vrclient_arx.txt', 'vrcompositor.txt', 'vrmonitor.txt', 'vrstartup.txt') {
		Copy-IfExists (Join-Path $steam "logs\$n") "steamvr\$n"
	}
}

# System facts
$sys = [Collections.Generic.List[string]]::new()
$sys.Add("collected: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$buildFile = Join-Path $root 'BUILD.txt'
$sys.Add("package: $(if (Test-Path $buildFile) { (Get-Content $buildFile -TotalCount 1) } else { 'unknown (no BUILD.txt)' })")
# The hash of the exe that crashed picks the matching arx.pdb (symbols are kept per exe hash, #132)
$arxExe = Join-Path $root 'arx.exe'
$sys.Add("arx.exe sha256: $(if (Test-Path $arxExe) { (Get-FileHash -Algorithm SHA256 $arxExe).Hash.ToLowerInvariant() } else { 'arx.exe not next to this script' })")
if (Test-Path $buildFile) { Copy-Item -Force $buildFile (Join-Path $stage 'BUILD.txt') }
$os = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
if ($os) { $sys.Add("windows: $($os.Caption) $($os.Version) build $($os.BuildNumber)") }
foreach ($gpu in @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)) {
	$sys.Add("gpu: $($gpu.Name), driver $($gpu.DriverVersion) ($($gpu.DriverDate))")
}
$cs = Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue
if ($cs) { $sys.Add("ram: $([math]::Round($cs.TotalPhysicalMemory / 1GB, 1)) GB") }
$runtime = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue).ActiveRuntime
$sys.Add("OpenXR ActiveRuntime: $(if ($runtime) { $runtime } else { 'none registered' })")
if ($runtime -and (Test-Path $runtime)) { $sys.Add((Get-Content $runtime -Raw)) }
$sys.Add("SteamVR running (vrserver.exe): $([bool](Get-Process vrserver -ErrorAction SilentlyContinue))")
$sys.Add("Meta Quest Link running (OVRServer_x64.exe): $([bool](Get-Process OVRServer_x64 -ErrorAction SilentlyContinue))")
$arxInstalled = 'Steam not found'
if ($steam) {
	$libraries = @($steam)
	$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
	if (Test-Path $vdf) {
		foreach ($m in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s+"([^"]+)"')) { $libraries += $m.Groups[1].Value -replace '\\\\', '\' }
	}
	$arxInstalled = [bool]($libraries | Where-Object { Test-Path (Join-Path $_ 'steamapps\appmanifest_1700.acf') })
}
$sys.Add("Arx Fatalis (Steam app 1700) in a Steam library: $arxInstalled")
Set-Content (Join-Path $stage 'system.txt') $sys -Encoding utf8
Write-Host '  + system.txt'

$zip = Join-Path $OutDir "arxvr-support-$(Get-Date -Format 'yyyyMMdd-HHmmss').zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
Remove-Item -Recurse -Force $stage
Write-Host ''
Write-Host "Support bundle: $zip" -ForegroundColor Green
Write-Host 'Attach it to your report (see BUG-REPORT-TEMPLATE.md). It holds logs and the facts in system.txt - look inside if you wish; nothing was sent anywhere.'
