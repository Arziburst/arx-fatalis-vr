<#
.SYNOPSIS
	Configure and build Arx Fatalis VR on Windows with MSVC.

.DESCRIPTION
	One command from a fresh clone to a built arx.exe:
	  - finds Visual Studio (Build Tools) with the C++ toolset via vswhere and loads its environment
	  - uses vcpkg in manifest mode (vcpkg.json); clones and bootstraps vcpkg on first run if needed
	  - configures with CMake + Ninja (falls back to NMake) and builds

	The build directory lives outside the source tree by default, so that a source tree inside a
	synced folder (e.g. OneDrive) does not sync thousands of build files.

.PARAMETER Config
	Build type: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory. Default: %LOCALAPPDATA%\ArxFatalisVR\build\<folder>-<hash>\<Config>, one per working
	copy (main clone or worktree), because CMake cannot share a build directory between source trees.

.PARAMETER VcpkgRoot
	vcpkg checkout. Default: $env:VCPKG_ROOT as set before the script starts (not the one Visual Studio
	sets), else %LOCALAPPDATA%\ArxFatalisVR\vcpkg (cloned and bootstrapped if missing).

.PARAMETER Clean
	Delete the build directory before configuring.

.EXAMPLE
	.\scripts\build-windows.ps1
.EXAMPLE
	.\scripts\build-windows.ps1 -Config Debug -Clean
#>
[CmdletBinding()]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$VcpkgRoot,
	[switch]$Clean
)

$ErrorActionPreference = 'Stop'
# Read before loading the MSVC environment: VsDevCmd points VCPKG_ROOT at the vcpkg bundled with
# Visual Studio, which would silently replace ours and rebuild every dependency from source.
$UserVcpkgRoot = $env:VCPKG_ROOT
$SourceDir = Split-Path -Parent $PSScriptRoot
$AppDataDir = Join-Path $env:LOCALAPPDATA 'ArxFatalisVR'
. (Join-Path $PSScriptRoot 'build-paths.ps1')
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $SourceDir $Config }

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }

# --- Visual Studio environment -------------------------------------------------------------------

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found. Install Visual Studio 2022 or Build Tools 2022 with the "Desktop development with C++" workload.' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No Visual Studio installation with the C++ x64 toolset found.' }

if (-not $env:VSCMD_VER) {
	Write-Step "Loading MSVC environment from $vsPath"
	$devCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
	cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
		if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
	}
}

# --- Tools -----------------------------------------------------------------------------------------

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe' }
if (-not (Test-Path $cmake)) { throw 'CMake not found. Install it with: winget install Kitware.CMake' }

$generator = 'NMake Makefiles'
if (Get-Command ninja -ErrorAction SilentlyContinue) { $generator = 'Ninja' }

# --- vcpkg -----------------------------------------------------------------------------------------

if (-not $VcpkgRoot) { $VcpkgRoot = if ($UserVcpkgRoot) { $UserVcpkgRoot } else { Join-Path $AppDataDir 'vcpkg' } }
$env:VCPKG_ROOT = $VcpkgRoot
if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
	if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
		Write-Step "Cloning vcpkg into $VcpkgRoot"
		git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
		if ($LASTEXITCODE) { throw 'git clone of vcpkg failed.' }
	}
	Write-Step 'Bootstrapping vcpkg'
	& (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
	if ($LASTEXITCODE) { throw 'vcpkg bootstrap failed.' }
}

# --- Configure and build ---------------------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
	Write-Step "Removing $BuildDir"
	Remove-Item -Recurse -Force $BuildDir
}

# A running game from this build keeps arx.exe locked: the link would fail with LNK1104 (docs/vr/TRAPS.md)
$running = @(Get-Process arx -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($BuildDir, [StringComparison]::OrdinalIgnoreCase) })
if ($running) { throw "arx.exe from $BuildDir is still running (pid $($running.Id -join ', ')) - close it before building." }

Write-Step "Configuring ($Config, $generator) in $BuildDir"
& $cmake -S $SourceDir -B $BuildDir -G $generator `
	"-DCMAKE_BUILD_TYPE=$Config" `
	"-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
	'-DVCPKG_TARGET_TRIPLET=x64-windows' `
	'-DUSE_STATIC_LIBS=OFF' `
	'-DBUILD_TOOLS=OFF' `
	'-DBUILD_CRASHREPORTER=OFF' `
	'-DBUILD_TESTS=OFF' `
	'-DICON_TYPE=none'
if ($LASTEXITCODE) { throw 'CMake configure failed.' }

Write-Step 'Building'
& $cmake --build $BuildDir
if ($LASTEXITCODE) { throw 'Build failed.' }

$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "Build finished but $exe was not found." }

# The build stamp: which commit this build came from ("<sha>" or "<sha> dirty"), read by
# scripts/dev/test-pr.ps1 to decide whether a worktree needs rebuilding.
$head = (git -C $SourceDir rev-parse HEAD 2>$null)
if ($head) {
	$dirty = @(git -C $SourceDir status --porcelain 2>$null)
	$stamp = "$($head.Trim())$(if ($dirty) { ' dirty' })"
	[IO.File]::WriteAllText((Join-Path $BuildDir 'built-from.txt'), $stamp, [Text.UTF8Encoding]::new($false))
	Write-Step "Done: $exe (built from $stamp)"
} else {
	Write-Step "Done: $exe"
}
