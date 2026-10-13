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

# The test save library (#70, scripts/dev/make-test-saves.ps1): one folder per scenario, each with
# gsave.sav. $env:ARXVR_SAVE_LIBRARY if set, else %LOCALAPPDATA%\ArxFatalisVR\savelib. Not in the
# repository: saves derive from the original game's data and go stale with the engine's save format.
function Get-ArxSaveLibrary {
	if ($env:ARXVR_SAVE_LIBRARY) { return $env:ARXVR_SAVE_LIBRARY }
	Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\savelib'
}

# Copy save slots (folders like mvp-new-game, each with gsave.sav) from a save library into the save
# folder of an isolated user directory. Library: -Library, else Get-ArxSaveLibrary. A name that is not
# in the library but is in the player's own save folder (save0000, ...) is taken from there.
function Copy-ArxSaves([string[]]$Names, [string]$UserDir, [string]$Library) {
	$explicitLibrary = [bool]$Library
	if (-not $Library) { $Library = Get-ArxSaveLibrary }
	$target = Join-Path $UserDir 'save'
	New-Item -ItemType Directory -Force $target | Out-Null
	foreach ($name in $Names) {
		$source = Join-Path $Library $name
		if (-not (Test-Path (Join-Path $source 'gsave.sav'))) {
			$own = Join-Path (Get-SharedUserDir) "save\$name"
			if (-not $explicitLibrary -and (Test-Path (Join-Path $own 'gsave.sav'))) {
				$source = $own
			} else {
				$have = @(Get-ChildItem $Library -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
				throw "save '$name' not found in $Library (no $name\gsave.sav; available: $(if ($have) { $have -join ', ' } else { 'none - run scripts\dev\make-test-saves.ps1' }))"
			}
		}
		$dest = Join-Path $target $name
		if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
		Copy-Item -Recurse $source $dest
		Write-Host "    save: $name <- $source"
	}
}

# Make one library save the ONLY save of an isolated user directory, so `--loadslot 0` loads it
# (slots are numbered newest first, so with several saves the slot of a given one is not stable).
function Use-ArxSave([string]$Name, [string]$UserDir, [string]$Library) {
	$saves = Join-Path $UserDir 'save'
	if (Test-Path $saves) { Remove-Item -Recurse -Force $saves }
	Copy-ArxSaves -Names @($Name) -UserDir $UserDir -Library $Library
}

# Why `--loadslot <n>` among the game's arguments would load nothing, or $null when there is no --loadslot or
# slot n exists. The game numbers the folders of <user dir>\save that hold a gsave.sav, newest first; a slot
# it does not have used to start the game with no level at all - a black world with only the HUD, which
# looked like a rendering bug in the headset (TRAPS.md, #24).
function Get-ArxLoadSlotProblem([string[]]$ArxArgs, [string]$UserDir) {
	$args_ = @($ArxArgs)
	$i = [array]::IndexOf([string[]]$args_, '--loadslot')
	if ($i -lt 0) { return $null }
	if ($i + 1 -ge $args_.Count -or $args_[$i + 1] -notmatch '^\d+$') { return '--loadslot needs a slot number' }
	$slot = [int]$args_[$i + 1]
	$saveDir = Join-Path $UserDir 'save'
	$count = @(Get-ChildItem $saveDir -Directory -ErrorAction SilentlyContinue |
	           Where-Object { Test-Path (Join-Path $_.FullName 'gsave.sav') }).Count
	if ($slot -lt $count) { return $null }
	"--loadslot ${slot}: $saveDir has $count save(s) (slot 0 = the newest), so there is nothing to load - the game " +
	"would start at the main menu. Load a test save instead: -Isolated -Save mvp-new-game (the test save library, " +
	"docs/vr/TESTING.md), or record a save in flat mode first"
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
# The game finds its data itself (#79, src/io/fs/SteamLibrary.cpp); the scripts use this only for
# checks of the machine (setup-coordinator.ps1).
function Find-ArxFatalisSteamInstall { Find-SteamAppInstall -AppId 1700 }

# A fingerprint of the working copy's uncommitted state, written into the build stamp (built-from.txt) and
# compared by the fresh-build checks (#306, #294): a hash of `git diff HEAD` (staged and unstaged edits of
# tracked files) plus the name and content hash of every untracked, not ignored file. The same tree gives the
# same fingerprint; a clean tree a fixed one. Edits outside the repository are not seen. $null when
# $SourceDir is not a git working copy.
function Get-ArxTreeFingerprint([string]$SourceDir) {
	if (-not "$(git -C $SourceDir rev-parse HEAD 2>$null)".Trim()) { return $null }
	$diff = (git -C $SourceDir -c core.quotepath=off diff HEAD --no-color 2>$null) -join "`n"
	$untracked = @(git -C $SourceDir -c core.quotepath=off ls-files --others --exclude-standard 2>$null)
	$hashes = if ($untracked) { @($untracked | git -C $SourceDir hash-object --stdin-paths 2>$null) } else { @() }
	$files = for ($i = 0; $i -lt $untracked.Count; $i++) { "$($untracked[$i]) $($hashes[$i])" }
	$sha = [Security.Cryptography.SHA256]::Create()
	$bytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes("$diff`n--untracked--`n$($files -join "`n")"))
	-join ($bytes[0..7] | ForEach-Object { $_.ToString('x2') })
}

# The fingerprint a stamp ("<sha>[ dirty] fp=<16 hex>") carries, or '' for a stamp without one (made before #306)
function Get-ArxStampFingerprint([string]$Stamp) {
	if ($Stamp -match '\bfp=([0-9a-f]+)') { $Matches[1] } else { '' }
}
