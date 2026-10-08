# Shared by build-windows.ps1, run-windows.ps1 and register-steamvr.ps1. Dot-source it.
#
# Every working copy (the main clone, each git worktree) gets its own build directory: CMake refuses
# to reuse a build directory that was configured from a different source tree.

function Get-DefaultBuildDir([string]$SourceDir, [string]$Config) {
	$full = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\').ToLowerInvariant()
	$sha = [Security.Cryptography.SHA1]::Create()
	$hash = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($full))[0..3] | ForEach-Object { $_.ToString('x2') })
	$name = "$(Split-Path -Leaf $full)-$hash"
	Join-Path $env:LOCALAPPDATA "ArxFatalisVR\build\$name\$Config"
}

# Steam's install folder as recorded in the registry (forward slashes, as Steam writes it), or $null.
function Get-SteamPath {
	(Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
}

# The Arx Fatalis (Steam app 1700) install folder in any Steam library, or $null.
# Until the engine finds the data itself (task #79), the scripts do the lookup.
function Find-ArxFatalisSteamInstall {
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
		$manifest = Join-Path $library 'steamapps\appmanifest_1700.acf'
		if (-not (Test-Path $manifest)) { continue }
		if ((Get-Content -Raw $manifest) -match '"installdir"\s+"([^"]+)"') {
			$dir = Join-Path $library "steamapps\common\$($Matches[1])"
			if (Test-Path $dir) { return $dir }
		}
	}
	return $null
}
