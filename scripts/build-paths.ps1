# Shared by build-windows.ps1, run-windows.ps1, register-steamvr.ps1 and scripts/dev/test-lib.ps1. Dot-source it.
#
# Every working copy (the main clone, each git worktree) has its own key "<folder>-<hash>" and from
# it its own build directory (CMake refuses to reuse a build directory that was configured from a
# different source tree), its own isolated user directory and its own test channel directory, so
# parallel workers never share files (docs/vr/WORKFLOW.md, "Parallel work").

function Get-WorkingCopyKey([string]$SourceDir) {
	$full = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\').ToLowerInvariant()
	$sha = [Security.Cryptography.SHA1]::Create()
	$hash = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($full))[0..3] | ForEach-Object { $_.ToString('x2') })
	"$(Split-Path -Leaf $full)-$hash"
}

function Get-DefaultBuildDir([string]$SourceDir, [string]$Config) {
	Join-Path $env:LOCALAPPDATA "ArxFatalisVR\build\$(Get-WorkingCopyKey $SourceDir)\$Config"
}

# The isolated user directory of a working copy: its own arx.log, cfg.ini, saves and crash reports
# (run-windows.ps1 -Isolated, the smoke test). Passed to arx.exe as --user-dir and --config-dir.
function Get-DefaultUserDir([string]$SourceDir) {
	Join-Path $env:LOCALAPPDATA "ArxFatalisVR\userdirs\$(Get-WorkingCopyKey $SourceDir)"
}

# The test channel directory (--test-dir, docs/vr/TESTING.md) of a working copy: $env:ARXVR_TEST_DIR
# if set, else %LOCALAPPDATA%\ArxFatalisVR\test\<key>, so two copies can use the channel at once.
function Get-DefaultTestDir([string]$SourceDir) {
	if ($env:ARXVR_TEST_DIR) { return $env:ARXVR_TEST_DIR }
	Join-Path $env:LOCALAPPDATA "ArxFatalisVR\test\$(Get-WorkingCopyKey $SourceDir)"
}

# The player's own user directory, shared by every working copy (the owner's saves live here):
# %USERPROFILE%\Saved Games\Arx Libertatis
function Get-SharedUserDir {
	Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Saved Games\Arx Libertatis'
}

# Copy save slots (folders like save0000, each with gsave.sav) from a save library into the save
# folder of an isolated user directory. Library: -Library, else $env:ARXVR_SAVE_LIBRARY, else the
# shared user directory's save folder (the owner's saves) until the test save library exists (#70).
function Copy-ArxSaves([string[]]$Names, [string]$UserDir, [string]$Library) {
	if (-not $Library) {
		$Library = if ($env:ARXVR_SAVE_LIBRARY) { $env:ARXVR_SAVE_LIBRARY } else { Join-Path (Get-SharedUserDir) 'save' }
	}
	$target = Join-Path $UserDir 'save'
	New-Item -ItemType Directory -Force $target | Out-Null
	foreach ($name in $Names) {
		$source = Join-Path $Library $name
		if (-not (Test-Path (Join-Path $source 'gsave.sav'))) {
			$have = @(Get-ChildItem $Library -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
			throw "save '$name' not found in $Library (no $name\gsave.sav; available: $(if ($have) { $have -join ', ' } else { 'none' }))"
		}
		$dest = Join-Path $target $name
		if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
		Copy-Item -Recurse $source $dest
		Write-Host "    save: $name <- $source"
	}
}

# Steam's install folder as recorded in the registry (forward slashes, as Steam writes it), or $null.
function Get-SteamPath {
	(Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
}

# The install folder of a Steam app (by app id) in any Steam library, or $null.
function Find-SteamAppInstall([int]$AppId) {
	$steam = Get-SteamPath
	if (-not $steam) { return $null }
	$libraries = @($steam)
	$vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
	if (Test-Path $vdf) {
		foreach ($match in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s+"([^"]+)"')) {
			$libraries += $match.Groups[1].Value -replace '\\\\', '\'
		}
	}
	foreach ($library in ($libraries | Select-Object -Unique)) {
		$manifest = Join-Path $library "steamapps\appmanifest_$AppId.acf"
		if (-not (Test-Path $manifest)) { continue }
		if ((Get-Content -Raw $manifest) -match '"installdir"\s+"([^"]+)"') {
			$dir = Join-Path $library "steamapps\common\$($Matches[1])"
			if (Test-Path $dir) { return $dir }
		}
	}
	return $null
}

# The Arx Fatalis (Steam app 1700) install folder in any Steam library, or $null.
# Until the engine finds the data itself (task #79), the scripts do the lookup.
function Find-ArxFatalisSteamInstall { Find-SteamAppInstall -AppId 1700 }
