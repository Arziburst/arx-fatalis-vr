# Launch control: the rules every script that starts arx.exe follows (docs/vr/WORKFLOW.md, "Parallel
# work"; decision D-014 in docs/vr/DECISIONS.md). Shared by run-windows.ps1 and scripts/dev/test-lib.ps1
# (smoke-test.ps1). Dot-source it.
#
#   1. No SteamVR start without the owner. A launch without --no-vr would let the OpenXR loader start
#      SteamVR, which turns on the owner's headset. Resolve-ArxVrMode makes such a launch flat (adds
#      --no-vr) when vrserver.exe is not running and says so; only -StartSteamVR (the owner's switch)
#      starts SteamVR, through Start-SteamVr.
#   2. One game at a time on this machine. Every launch, flat or VR, holds a game lock slot
#      (%ProgramData%\ArxFatalisVR\locks\game-<n>.lock, writable by every account, so the CI runner
#      service and the owner see each other; #205) while the game runs; a VR launch holds vr.lock as
#      well. A launch waits in line with a timeout and names the holders (working copy, branch, pid).
#      Like the VR lock of #123, a lock is a file opened without write sharing: Windows releases the
#      handle when the holder dies, so a crash or a kill leaves no stale lock. A game that runs outside
#      the lock (a raw arx.exe) counts as a holder too. settings.json ("maxGames") allows more slots.
#      CI owns the slots (#527): while the CI runner runs a job (the marker ci-runner.marker it writes,
#      Set-ArxRunnerMarker), every other scripted launch waits without taking a slot, and only the job's
#      own launches ($env:ARXVR_CI_RUNNER) take them; a game that already holds a slot finishes. The
#      owner's own launch (-Owner: run-windows.ps1 by hand, test-pr.ps1) ignores the marker and is never
#      stopped. A waiting launch names the holders at once and every -ReportSec, and the timeout line names
#      them too; an orphan game (a lock file no process holds any more, naming an arx.exe whose launcher
#      is gone) is stopped (Stop-ArxOrphanGames). A game a script starts dies with the script
#      (Register-ArxGameProcess: a job object with KILL_ON_JOB_CLOSE, #526), and ci-local / ci-smoke put
#      their whole process chain into one (Register-ArxLauncherChain).
#   3. Only the working copy's own fresh build: Assert-ArxFreshBuild refuses a build directory of
#      another copy and a built-from.txt whose commit is not the copy's HEAD or whose tree fingerprint
#      (git diff HEAD plus untracked files, #306) differs from the copy's now.
#   4. The launch ledger %LOCALAPPDATA%\ArxFatalisVR\launches.log: one line per launch (time, copy,
#      branch, build, mode, pid, exit code, duration), read by scripts/dev/cycle-report.ps1.
#   5. The launch budget: 10 launches per task (WORKFLOW.md); Write-ArxLaunchBudgetNote warns past it.
#   6. Self-test windows do not take the focus: Set-ArxBackgroundWindow sets the SDL hint that shows
#      the window without activating it (SW_SHOWNA). Since #487 the game does it itself for every
#      --test-dir run (never activated, in a corner, no real input); Test-ArxForegroundKept checks it.
#   7. Scripted launches show no dialogs (#501): Set-ArxDialogEnv -NoDialogs puts ARXVR_NO_DIALOGS=1 into the
#      game's environment, an owner's launch gets neither variable; a dialog becomes a '[dialog]' line in arx.log
#      and stderr (Get-ArxDialogLines reads it back).

. (Join-Path $PSScriptRoot 'build-paths.ps1')

function Get-ArxAppDataDir { Join-Path $env:LOCALAPPDATA 'ArxFatalisVR' }

function ConvertTo-ShortSha([string]$Sha) { if ($Sha -and $Sha.Length -gt 9) { $Sha.Substring(0, 9) } else { $Sha } }

# Machine-wide launch settings: settings.json in the lock folder (%ProgramData%\ArxFatalisVR\locks, so the
# CI runner service reads the same file), else the old %LOCALAPPDATA%\ArxFatalisVR\settings.json, e.g.
# { "maxGames": 2 } (how many games may run at once; default 1 - the owner's rule). Missing file or key:
# the defaults. $env:ARXVR_MAX_GAMES overrides maxGames for the launches of one process tree: the CI workflow
# sets it to 2, so its two lanes (ci-smoke.ps1, #522) may hold two slots while everyone else keeps one.
function Get-ArxLaunchSettings {
	$settings = @{ maxGames = 1 }
	$file = @(Get-ArxLockDirs | ForEach-Object { Join-Path $_ 'settings.json' } | Where-Object { Test-Path $_ })[0]
	if ($file) {
		try {
			$json = Get-Content $file -Raw | ConvertFrom-Json
			if ($json.maxGames) { $settings.maxGames = [Math]::Max(1, [int]$json.maxGames) }
		} catch {
			Write-Warning "ignoring $file ($($_.Exception.Message))"
		}
	}
	if ($env:ARXVR_MAX_GAMES) {
		$n = 0
		if ([int]::TryParse($env:ARXVR_MAX_GAMES, [ref]$n)) { $settings.maxGames = [Math]::Max(1, $n) }
		else { Write-Warning "ignoring ARXVR_MAX_GAMES='$env:ARXVR_MAX_GAMES' (not a number)" }
	}
	[pscustomobject]$settings
}

# HEAD, branch and dirtiness of a working copy, for the lock holder line, the build check and the ledger
function Get-ArxWorkingCopyInfo([string]$SourceDir) {
	$head = "$(git -C $SourceDir rev-parse HEAD 2>$null)".Trim()
	$branch = "$(git -C $SourceDir branch --show-current 2>$null)".Trim()
	if ($head -and -not $branch) { $branch = 'detached' }
	$dirty = if ($head) { [bool]@(git -C $SourceDir status --porcelain 2>$null) } else { $false }
	[pscustomobject]@{ Path = $SourceDir; Head = $head; Branch = $branch; Dirty = $dirty }
}

# --- 1. SteamVR ---------------------------------------------------------------------------------------

function Test-SteamVrRunning { [bool](Get-Process vrserver -ErrorAction SilentlyContinue) }

# A tool of the SteamVR install (Steam app 250820), e.g. 'vrstartup.exe', or $null
function Find-SteamVrBinary([string]$Name) {
	$dir = Find-SteamAppInstall -AppId 250820
	if (-not $dir) { return $null }
	$exe = Join-Path $dir "bin\win64\$Name"
	if (Test-Path $exe) { $exe } else { $null }
}

# vrstartup.exe of the SteamVR install, or $null
function Find-SteamVrStartup { Find-SteamVrBinary 'vrstartup.exe' }

# Wait until vrserver.exe and vrcompositor.exe both run: $true, or $false when $Sec ran out
function Wait-SteamVrUp([int]$Sec) {
	$deadline = (Get-Date).AddSeconds($Sec)
	while ((Get-Date) -lt $deadline) {
		if ((Test-SteamVrRunning) -and (Get-Process vrcompositor -ErrorAction SilentlyContinue)) { return $true }
		Start-Sleep -Milliseconds 500
	}
	return $false
}

# Start SteamVR and wait until vrserver.exe and vrcompositor.exe run, plus -SettleSec for the OpenXR
# runtime to be ready. The owner's path only (-StartSteamVR): a worker never calls this (D-014).
# SteamVR is started explicitly, never left to the game's OpenXR call (#171: that did not bring it up and
# the game started flat). First through Steam (steam://rungameid/250820 - it starts Steam too, the way the
# owner starts SteamVR by hand); when SteamVR is not up after half the timeout, through SteamVR's own
# vrstartup.exe.
function Start-SteamVr([int]$TimeoutSec = 120, [int]$SettleSec = 15) {
	if (Test-SteamVrRunning) { return }
	$half = [Math]::Max(10, [int]($TimeoutSec / 2))
	Write-Host '==> Starting SteamVR on -StartSteamVR: steam://rungameid/250820' -ForegroundColor Yellow
	$up = $false
	try {
		Start-Process -FilePath 'steam://rungameid/250820' | Out-Null
		$up = Wait-SteamVrUp $half
	} catch {
		Write-Host "    could not open the Steam URL: $($_.Exception.Message)" -ForegroundColor Yellow
	}
	if (-not $up) {
		$exe = Find-SteamVrStartup
		if ($exe) {
			Write-Host "    SteamVR is not up after $half s - starting it with its own launcher: $exe" -ForegroundColor Yellow
			Start-Process -FilePath $exe | Out-Null
		} else {
			Write-Host '    no SteamVR launcher to fall back on (Steam app 250820 is not in any Steam library); waiting on' -ForegroundColor Yellow
		}
		$up = Wait-SteamVrUp ($TimeoutSec - $half)
	}
	if (-not $up) {
		$state = "vrserver.exe $(if (Test-SteamVrRunning) { 'runs' } else { 'does not run' }), vrcompositor.exe $(if (Get-Process vrcompositor -ErrorAction SilentlyContinue) { 'runs' } else { 'does not run' })"
		throw "SteamVR did not come up within $TimeoutSec s ($state). Start it by hand (Steam > Library > SteamVR; check that the headset is connected and the base stations are on), then run again."
	}
	Write-Host "    vrserver and vrcompositor are up; settling $SettleSec s"
	Start-Sleep -Seconds $SettleSec
}

# The SteamVR processes that make up a running SteamVR (vrwebhelper is left out: it lingers after the exit)
$script:SteamVrCoreProcesses = @('vrmonitor', 'vrserver', 'vrcompositor', 'vrdashboard', 'vrservice', 'vrstartup')
function Get-SteamVrCoreProcesses { @(Get-Process -Name $script:SteamVrCoreProcesses -ErrorAction SilentlyContinue) }

# Quit SteamVR the way its own "Exit SteamVR" does, so the headset powers down (#171). SteamVR's status window
# (vrmonitor.exe) takes URL commands - vrmonitor.exe contains the handler OnVRMonitorURL with the command `quit`
# (OnQuit), and the vrmonitor:// protocol is registered for it - so `vrmonitor.exe vrmonitor://quit` hands the
# running instance the same quit request as the menu item: vrmonitor tells the VR apps to quit, shuts the
# drivers down (the Index display and the controllers go to standby by the drivers' own shutdown), and
# vrserver exits last. Nothing is killed: stopping vrserver / vrmonitor from outside (CloseMainWindow plus
# Stop-Process, the first attempt) took the software away while the Index stayed on. When SteamVR does not
# exit in time the script says which processes remain and leaves them to the owner. The base stations are not
# touched: SteamVR's own "base station power management" setting decides what they do.
# The owner's path only (test-pr.ps1 -StopSteamVR, stop-steamvr.ps1): a worker never calls this (D-014).
function Stop-SteamVr([int]$TimeoutSec = 60, [int]$SettleSec = 3) {
	$running = Get-SteamVrCoreProcesses
	if (-not $running) { Write-Host '==> SteamVR is not running - nothing to stop' -ForegroundColor Cyan; return }
	Write-Host "==> Stopping SteamVR (vrmonitor://quit, SteamVR's own Exit): $((($running | ForEach-Object { $_.Name }) | Sort-Object -Unique) -join ', ')" -ForegroundColor Yellow
	$exe = Find-SteamVrBinary 'vrmonitor.exe'
	try {
		if ($exe) { Start-Process -FilePath $exe -ArgumentList 'vrmonitor://quit' | Out-Null }
		else { Start-Process -FilePath 'vrmonitor://quit' | Out-Null }
	} catch {
		throw "could not send the quit request to SteamVR ($($_.Exception.Message)) - exit it from its own menu (SteamVR status window > menu > Exit SteamVR)"
	}
	$started = Get-Date
	$deadline = $started.AddSeconds($TimeoutSec)
	$lastReport = $started
	while ((Get-Date) -lt $deadline) {
		$running = Get-SteamVrCoreProcesses
		if (-not $running) {
			Write-Host "    SteamVR exited after $([int]((Get-Date) - $started).TotalSeconds) s; settling $SettleSec s for the headset to power down"
			Start-Sleep -Seconds $SettleSec
			return
		}
		if (((Get-Date) - $lastReport).TotalSeconds -ge 10) {
			Write-Host "    still running: $((($running | ForEach-Object { $_.Name }) | Sort-Object -Unique) -join ', ')"
			$lastReport = Get-Date
		}
		Start-Sleep -Milliseconds 500
	}
	throw "SteamVR did not exit within $TimeoutSec s; still running: $((($running | ForEach-Object { $_.Name }) | Sort-Object -Unique) -join ', '). Nothing was killed (a killed SteamVR leaves the headset on): exit it from its own menu (SteamVR status window > menu > Exit SteamVR)."
}

# Decide whether a launch may run in VR. Returns Vr (bool), Args (the arx.exe arguments to use) and
# Reason. Without --no-vr and without SteamVR running the launch becomes flat (--no-vr is added) -
# unless -StartSteamVR, the owner's switch, which starts SteamVR first.
function Resolve-ArxVrMode([string[]]$ArxArgs, [switch]$StartSteamVR) {
	if (-not $ArxArgs) { $ArxArgs = @() }
	if ($ArxArgs -contains '--no-vr') {
		return [pscustomobject]@{ Vr = $false; Args = $ArxArgs; Reason = '--no-vr given' }
	}
	if (Test-SteamVrRunning) {
		return [pscustomobject]@{ Vr = $true; Args = $ArxArgs; Reason = 'SteamVR is running' }
	}
	if ($StartSteamVR) {
		Start-SteamVr
		return [pscustomobject]@{ Vr = $true; Args = $ArxArgs; Reason = 'SteamVR started on -StartSteamVR' }
	}
	Write-Host '    SteamVR is not running and this script never starts it (D-014): launching flat (--no-vr added).' -ForegroundColor Yellow
	Write-Host '    To play in VR start SteamVR first; the owner may pass -StartSteamVR instead.' -ForegroundColor Yellow
	return [pscustomobject]@{ Vr = $false; Args = @($ArxArgs) + '--no-vr'; Reason = 'SteamVR not running, --no-vr added' }
}

# The kind of launch (#394). A plain arx.exe is the player's launch: the publisher logos, the main menu and every
# .cin cinematic (the new-game intro, the cutscenes) - the desktop shortcut and the Steam shortcut launch that way.
# Development and test launches add --dev-launch: no logos and no cutscenes, straight to the menu or the game
# (--intro / --cinematics bring them back). The scripts that start the game take their arguments from here.
$script:ArxDevLaunchArg = '--dev-launch'

function Get-ArxLaunchKindArgs([switch]$Player) {
	if ($Player) { return @() }
	return @($script:ArxDevLaunchArg)
}

# --- 3. The working copy's own fresh build ---------------------------------------------------------------

# Refuse (or with -Rebuild, rebuild) unless $BuildDir is a build directory of $SourceDir holding an
# arx.exe whose built-from.txt names the copy's HEAD and the fingerprint of its uncommitted state
# ("<sha>[ dirty] fp=<hash>"). Returns the stamp.
function Assert-ArxFreshBuild([string]$SourceDir, [string]$BuildDir, [string]$Config, [string]$Head, [switch]$Rebuild) {
	$root = Join-Path (Get-ArxAppDataDir) "build\$(Get-WorkingCopyKey $SourceDir)"
	$full = [IO.Path]::GetFullPath($BuildDir).TrimEnd('\')
	if (-not $full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
		throw "$BuildDir is not a build of this working copy. Launches run only the copy's own build, $root\<Config> (D-014)."
	}
	$exe = Join-Path $BuildDir 'arx.exe'
	$stampFile = Join-Path $BuildDir 'built-from.txt'
	$readStamp = { if (Test-Path $stampFile) { (Get-Content $stampFile -Raw).Trim() } else { '' } }
	$stamp = & $readStamp
	$problem = if (-not (Test-Path $exe)) { "no arx.exe in $BuildDir" }
		elseif (-not $stamp) { 'the build has no built-from.txt (made before the stamp existed)' }
		elseif ($Head -and ($stamp -split ' ')[0] -ne $Head) { "built from $(ConvertTo-ShortSha (($stamp -split ' ')[0])), HEAD is $(ConvertTo-ShortSha $Head)" }
		elseif ($Head -and -not (Get-ArxStampFingerprint $stamp)) { 'the build stamp has no tree fingerprint (made before #306)' }
		elseif ($Head -and (Get-ArxStampFingerprint $stamp) -ne (Get-ArxTreeFingerprint $SourceDir)) { 'the working tree changed since the build (uncommitted edits, #306)' }
	if (-not $problem) { return $stamp }
	if (-not $Rebuild) {
		throw "stale build: $problem. Build first (.\scripts\build-windows.ps1 -Config $Config) or pass -Rebuild - a launch runs only a fresh build of its working copy (D-014)."
	}
	Write-Host "==> Build is stale ($problem) - rebuilding on -Rebuild" -ForegroundColor Yellow
	& (Join-Path $SourceDir 'scripts\build-windows.ps1') -Config $Config
	$stamp = & $readStamp
	if (-not (Test-Path $exe) -or -not $stamp) { throw 'the rebuild left no arx.exe or no built-from.txt' }
	return $stamp
}

# --- 2. The game lock ------------------------------------------------------------------------------------

# The lock files live in a machine-wide folder, not in %LOCALAPPDATA%: the CI runner service is another
# Windows account (NT AUTHORITY\NETWORK SERVICE) with another %LOCALAPPDATA%, and its game must queue
# behind the owner's and the workers' games and the other way round (#205).
#   %ProgramData%\ArxFatalisVR\locks   ($env:ARXVR_LOCK_DIR overrides it, for tests and odd machines)
# The folder gets an ACL that lets every user create, open and delete the lock files (Modify, inherited by
# the files), so a file left behind by one account is not stuck for another. The old location
# (%LOCALAPPDATA%\ArxFatalisVR) is still taken and checked next to the new one, so a working copy that
# runs an older launch-lib.ps1 keeps seeing every lock of this account; drop it once no such copy is left.
function Get-ArxLockDir {
	if ($env:ARXVR_LOCK_DIR) { return $env:ARXVR_LOCK_DIR }
	Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'ArxFatalisVR\locks'
}

# The lock folders in use: the machine-wide one, then the old per-account one
function Get-ArxLockDirs {
	$dirs = @(Get-ArxLockDir)
	$legacy = Get-ArxAppDataDir
	if ($legacy -ine $dirs[0]) { $dirs += $legacy }
	$dirs
}

function Get-VrLockPath { Join-Path (Get-ArxLockDir) 'vr.lock' }
function Get-GameLockPath([int]$Slot) { Join-Path (Get-ArxLockDir) "game-$Slot.lock" }

# The SIDs that must be able to create, open and delete lock files: Authenticated Users (which the
# NETWORK SERVICE token includes) and BUILTIN\Users
$script:ArxLockDirSids = @('S-1-5-11', 'S-1-5-32-545')

# Which of $script:ArxLockDirSids have no inheritable Modify rule on $Dir (empty when the folder is fine)
function Get-ArxLockDirMissingSids([string]$Dir) {
	$modify = [Security.AccessControl.FileSystemRights]::Modify
	$both = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit
	$have = @()
	foreach ($r in (Get-Acl -LiteralPath $Dir).Access) {
		if ($r.AccessControlType -ne 'Allow' -or ($r.FileSystemRights -band $modify) -ne $modify -or ($r.InheritanceFlags -band $both) -ne $both) { continue }
		if ($r.PropagationFlags -band [Security.AccessControl.PropagationFlags]::InheritOnly) { continue }
		try { $have += $r.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value } catch { }
	}
	@($script:ArxLockDirSids | Where-Object { $_ -notin $have })
}

# Create the machine-wide lock folder with the ACL above (or add the missing rules to an existing one).
# Whoever creates the folder owns it and may change the ACL; another account that finds the folder
# without the rules cannot repair it - then it only warns, and taking a lock fails with the real error.
function Initialize-ArxLockDir([string]$Dir) {
	[IO.Directory]::CreateDirectory($Dir) | Out-Null
	$missing = @(Get-ArxLockDirMissingSids $Dir)
	if (-not $missing) { return }
	try {
		# only the DACL section: Set-Acl would also write the owner, group and audit rules, which needs privileges
		$info = [IO.DirectoryInfo]::new($Dir)
		$acl = [IO.FileSystemAclExtensions]::GetAccessControl($info, [Security.AccessControl.AccessControlSections]::Access)
		foreach ($sid in $missing) {
			$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new(
				[Security.Principal.SecurityIdentifier]::new($sid), 'Modify', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
		}
		[IO.FileSystemAclExtensions]::SetAccessControl($info, $acl)
	} catch {
		Write-Warning "cannot open the lock folder $Dir to all users ($($_.Exception.Message)); a game started by another Windows account may not see the locks"
	}
}

# What a lock file says ("pid=... game=... since=... what=... copy=... branch=..."), or a note
function Read-ArxLockHolder([string]$Path) {
	try {
		$stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
		try {
			$text = [IO.StreamReader]::new($stream).ReadToEnd().Trim()
			if ($text -match 'pid=(\d+)' -and -not (Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue)) { $text += ' (that process is gone)' }
			if ($text) { $text } else { 'unknown (empty lock file)' }
		} finally { $stream.Dispose() }
	} catch {
		"unknown (cannot read $Path`: $($_.Exception.Message))"
	}
}

# Everything the lock files say right now: one line per held lock (free files are skipped; a lock taken in
# both folders is listed once)
function Get-ArxLockHolders {
	$lines = @()
	foreach ($dir in Get-ArxLockDirs) {
		foreach ($file in Get-ChildItem $dir -Filter '*.lock' -File -ErrorAction SilentlyContinue) {
			$held = $true
			try {
				# a file we can open without sharing is free (left behind by a holder that died)
				$probe = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
				$probe.Dispose()
				$held = $false
			} catch [IO.IOException] { }
			if ($held) { $lines += "$($file.Name): $(Read-ArxLockHolder $file.FullName)" }
		}
	}
	$lines | Select-Object -Unique
}

# arx.exe processes that no lock names (started by hand, or by a script before it recorded the pid);
# the crash processor (arx.exe --crashinfo=...) is not a game. The process list comes from WMI, not from
# Get-Process: the .Path of a process of another account (the CI runner service) is empty there (#205).
function Get-ArxGamesOutsideLock {
	$known = @()
	foreach ($dir in Get-ArxLockDirs) {
		foreach ($file in Get-ChildItem $dir -Filter '*.lock' -File -ErrorAction SilentlyContinue) {
			if ((Read-ArxLockHolder $file.FullName) -match 'game=(\d+)') { $known += [int]$Matches[1] }
		}
	}
	@(Get-CimInstance Win32_Process -Filter "Name='arx.exe'" -ErrorAction SilentlyContinue | Where-Object {
		[int]$_.ProcessId -notin $known -and "$($_.CommandLine)" -notmatch '--crashinfo' -and
		(-not $_.ExecutablePath -or $_.ExecutablePath.EndsWith('\arx.exe', [StringComparison]::OrdinalIgnoreCase))
	} | ForEach-Object { [pscustomobject]@{ Id = [int]$_.ProcessId; Path = "$($_.ExecutablePath)" } })
}

function Open-ArxLockFile([string]$Path) {
	$stream = [IO.File]::Open($Path, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
	[pscustomobject]@{ Path = $Path; Stream = $stream; Writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false)) }
}

# Take the lock file $Name in every lock folder (machine-wide and old). Throws an IOException, with nothing
# left open, when one of them is held.
function Open-ArxLockFiles([string]$Name) {
	$opened = @()
	try {
		foreach ($dir in Get-ArxLockDirs) { $opened += Open-ArxLockFile (Join-Path $dir $Name) }
	} catch {
		Close-ArxLockFiles $opened
		throw
	}
	$opened
}

function Write-ArxLockFile($File, [string]$Text) {
	$File.Stream.SetLength(0)
	$File.Writer.Write($Text)
	$File.Writer.Flush()
}

function Close-ArxLockFiles($Files) {
	foreach ($f in @($Files)) {
		try { $f.Writer.Dispose() } catch { }
		try { $f.Stream.Dispose() } catch { }
		Remove-Item $f.Path -Force -ErrorAction SilentlyContinue
	}
}

# --- The CI runner's priority (#527) ---
# While the CI runner runs a job it writes ci-runner.marker into the lock folder: 'pid=<n> since=<time> what=<job>',
# the pid of a process that lives as long as the job (the workflow writes it before the build and clears it in a
# step that always runs). While the marker's process is alive, every scripted launch that is not the job's own
# ($env:ARXVR_CI_RUNNER, set by the workflow for the job) and not the owner's (-Owner) waits without taking a slot,
# so the runner's phases get the PC as soon as the running games finish. A marker whose process is gone, or older
# than a job can be, is stale and counts as none - a job that died never blocks the PC.
$script:ArxRunnerMarkerMaxAgeHours = 2

function Get-ArxRunnerMarkerPath { Join-Path (Get-ArxLockDir) 'ci-runner.marker' }

function Set-ArxRunnerMarker([int]$RunnerPid, [string]$What) {
	Initialize-ArxLockDir (Get-ArxLockDir)
	$text = "pid=$RunnerPid since=$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) what=$What"
	[IO.File]::WriteAllText((Get-ArxRunnerMarkerPath), $text, [Text.UTF8Encoding]::new($false))
	Write-Host "game lock: the CI runner marker is up ($text)"
}

function Clear-ArxRunnerMarker {
	$path = Get-ArxRunnerMarkerPath
	if (Test-Path -LiteralPath $path) {
		Remove-Item -LiteralPath $path -Force
		Write-Host 'game lock: the CI runner marker is cleared'
	}
}

# The running CI job the marker names, { Pid; Since; What }, or $null: no marker, or a stale one (its process is
# gone, started after the marker was written - a reused pid - or the marker is older than a job can be)
function Get-ArxRunnerMarker {
	$path = Get-ArxRunnerMarkerPath
	if (-not (Test-Path -LiteralPath $path)) { return $null }
	try { $text = [IO.File]::ReadAllText($path).Trim() } catch { return $null }
	if ($text -notmatch '^pid=(\d+) since=(\S+ \S+) what=(.*)$') { return $null }
	$runnerPid = [int]$Matches[1]
	$since = [DateTime]::MinValue
	if (-not [DateTime]::TryParseExact($Matches[2], 'yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture, 'None', [ref]$since)) { return $null }
	if ((Get-Date) - $since -gt [TimeSpan]::FromHours($script:ArxRunnerMarkerMaxAgeHours)) { return $null }
	$process = Get-Process -Id $runnerPid -ErrorAction SilentlyContinue
	if (-not $process) { return $null }
	try { if ($process.StartTime -gt $since.AddMinutes(1)) { return $null } } catch { }
	[pscustomobject]@{ Pid = $runnerPid; Since = $Matches[2]; What = $Matches[3] }
}

# --- Orphan games (#527) ---
# A game whose launcher died without stopping it (a killed ci-local, #526): its lock file is no longer held (the
# handle went with the launcher), still says game=<pid>, and that arx.exe runs on. Such a game is stopped and the
# file removed; one line per orphan says so. Never stopped: a process that is not arx.exe (the pid was reused), the
# game of a holder that is alive, and the owner's own game (a lock taken with -Owner, '(owner)' in its what=).
function Stop-ArxOrphanGames {
	$lines = @()
	foreach ($dir in Get-ArxLockDirs) {
		foreach ($file in Get-ChildItem $dir -Filter 'game-*.lock' -File -ErrorAction SilentlyContinue) {
			try {
				$probe = [IO.File]::Open($file.FullName, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
				$probe.Dispose()
			} catch { continue } # held by a live process, or unreadable: not ours to judge
			$text = Read-ArxLockHolder $file.FullName
			if ($text -notmatch 'pid=(\d+) game=(\d+)') { continue }
			$launcher = [int]$Matches[1]
			$game = [int]$Matches[2]
			if ($text -match 'what=[^\r\n]*\(owner\)') { continue }
			if (Get-Process -Id $launcher -ErrorAction SilentlyContinue) { continue }
			$process = Get-Process -Id $game -ErrorAction SilentlyContinue
			if ($process -and $process.ProcessName -eq 'arx') {
				try {
					Stop-Process -Id $game -Force -ErrorAction Stop
					Wait-Process -Id $game -Timeout 5 -ErrorAction SilentlyContinue
					$lines += "game lock: stopped the orphan arx.exe pid $game - its launcher pid $launcher is gone ($($file.Name): $text)"
				} catch {
					$lines += "game lock: could not stop the orphan arx.exe pid $game - its launcher pid $launcher is gone ($($file.Name): $text): $($_.Exception.Message)"
					continue
				}
			}
			Remove-Item -LiteralPath $file.FullName -Force -ErrorAction SilentlyContinue
		}
	}
	$lines
}

# Take a game lock slot (and with -Vr the VR lock), waiting up to $TimeoutSec seconds while every slot
# is held, a game runs outside the lock, or the CI runner runs a job (unless this is the job's own launch,
# $env:ARXVR_CI_RUNNER, or the owner's, -Owner). Orphan games are stopped first. Names the holders at once and
# every -ReportSec while waiting, and on the timeout. Returns the lock object (keep it, pass it to
# Set-ArxGameLockGame and Exit-ArxGameLock) or $null when the wait timed out.
function Enter-ArxGameLock([string]$What, [string]$Copy, [string]$Branch, [switch]$Vr, [int]$TimeoutSec = 300, [int]$ReportSec = 30, [switch]$Owner) {
	foreach ($dir in Get-ArxLockDirs) { Initialize-ArxLockDir $dir }
	$maxGames = (Get-ArxLaunchSettings).maxGames
	if ($Owner) { $What = "$What (owner)" }
	$yields = -not $Owner -and -not $env:ARXVR_CI_RUNNER
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	$lastReport = [DateTime]::MinValue
	$busy = $null
	do {
		$taken = @()
		$slot = 0
		$busy = $null
		try {
			foreach ($line in Stop-ArxOrphanGames) { Write-Host $line -ForegroundColor Yellow }
			if ($yields) {
				$runner = Get-ArxRunnerMarker
				if ($runner) { $busy = "the CI runner runs a job - $($runner.What), pid $($runner.Pid) since $($runner.Since) - and CI has the game slots first (#527)" }
			}
			if (-not $busy -and $Vr) {
				try { $taken += Open-ArxLockFiles 'vr.lock' } catch [IO.IOException] { $busy = 'the VR lock is held' }
			}
			if (-not $busy) {
				for ($s = 1; $s -le $maxGames -and -not $slot; $s++) {
					try { $taken += Open-ArxLockFiles "game-$s.lock"; $slot = $s } catch [IO.IOException] { }
				}
				if (-not $slot) { $busy = "all $maxGames game slot(s) are held" }
			}
			if (-not $busy) {
				$outside = Get-ArxGamesOutsideLock
				if ($outside) { $busy = "a game runs outside the lock: $(($outside | ForEach-Object { "pid $($_.Id) $($_.Path)" }) -join '; ')" }
			}
			if (-not $busy) {
				$lock = [pscustomobject]@{ Files = $taken; Slot = $slot; Vr = [bool]$Vr; Line = "pid=$PID game=- since=$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) what=$What copy=$Copy branch=$Branch" }
				foreach ($f in $taken) { Write-ArxLockFile $f $lock.Line }
				return $lock
			}
		} catch {
			$busy = "cannot take the lock: $($_.Exception.Message)"
		}
		Close-ArxLockFiles $taken
		if (((Get-Date) - $lastReport).TotalSeconds -ge $ReportSec) {
			Write-Host "game lock busy ($busy)$(if ($TimeoutSec -gt 0) { " - waiting up to $TimeoutSec s, one game at a time on this PC (D-014)" })" -ForegroundColor Yellow
			foreach ($line in Get-ArxLockHolders) { Write-Host "  held: $line" -ForegroundColor Yellow }
			$lastReport = Get-Date
		}
		if ($TimeoutSec -gt 0) { Start-Sleep -Milliseconds 500 }
	} while ((Get-Date) -lt $deadline)
	$holders = @(Get-ArxLockHolders)
	Write-Host "game lock: timed out after $TimeoutSec s ($busy) - held: $(if ($holders) { $holders -join '; ' } else { 'no lock file (a game outside the lock, or the CI runner)' })" -ForegroundColor Yellow
	return $null
}

# --- Games die with their launcher (#526) ---
# A Windows job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: every process in it is killed when the last handle
# to the job closes, which happens when the process holding the handle ends - killed or not. The handle lives in
# this process ($script:ArxLauncherJob), one job per process, made on first use.
function Initialize-ArxJobNative {
	if ('ArxJobNative' -as [type]) { return }
	Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ArxJobNative {
  [StructLayout(LayoutKind.Sequential)]
  public struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
    public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
    public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
    public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct IO_COUNTERS {
    public ulong ReadOperationCount; public ulong WriteOperationCount; public ulong OtherOperationCount;
    public ulong ReadTransferCount; public ulong WriteTransferCount; public ulong OtherTransferCount;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
    public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; public IO_COUNTERS IoInfo;
    public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit; public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
  }
  public const int JobObjectExtendedLimitInformation = 9;
  public const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] public static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
  [DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, int size);
  [DllImport("kernel32.dll", SetLastError = true)] public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll", SetLastError = true)] public static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
}
'@
}

# The job object of this process (made once): $null, with a warning, when Windows refuses it
function Get-ArxLauncherJob {
	if ($script:ArxLauncherJob) { return $script:ArxLauncherJob }
	try {
		Initialize-ArxJobNative
		$job = [ArxJobNative]::CreateJobObjectW([IntPtr]::Zero, $null)
		if ($job -eq [IntPtr]::Zero) { throw "CreateJobObject failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
		$info = [ArxJobNative+JOBOBJECT_EXTENDED_LIMIT_INFORMATION]::new()
		# A nested struct comes back as a copy: set it whole
		$basic = $info.BasicLimitInformation
		$basic.LimitFlags = [ArxJobNative]::JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
		$info.BasicLimitInformation = $basic
		$size = [Runtime.InteropServices.Marshal]::SizeOf($info)
		if (-not [ArxJobNative]::SetInformationJobObject($job, [ArxJobNative]::JobObjectExtendedLimitInformation, [ref]$info, $size)) {
			throw "SetInformationJobObject failed ($([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
		}
		$script:ArxLauncherJob = $job
		$job
	} catch {
		Write-Warning "no job object for the games of this process ($($_.Exception.Message)): a game would outlive a killed launcher (#526)"
		$null
	}
}

# Put $Process (a Process object, e.g. from Start-Process -PassThru) into this process's job: it is killed when this
# process ends. Returns $true when it is in the job, $false (with a warning) when Windows refused.
function Register-ArxGameProcess($Process) {
	if (-not $Process) { return $false }
	$job = Get-ArxLauncherJob
	if (-not $job) { return $false }
	$inJob = $false
	if ([ArxJobNative]::IsProcessInJob($Process.Handle, $job, [ref]$inJob) -and $inJob) { return $true }
	if ([ArxJobNative]::AssignProcessToJobObject($job, $Process.Handle)) { return $true }
	Write-Warning "could not put pid $($Process.Id) into this process's job object (error $([Runtime.InteropServices.Marshal]::GetLastWin32Error())): it would outlive a killed launcher (#526)"
	$false
}

# Put this process itself into its job: every process it starts from now on (a child pwsh, a background job, the
# games they start) is in the job too and dies with it (#526: a killed ci-local must take its ci-smoke, the phase
# processes and their games with it). Returns $true when this process is in the job.
function Register-ArxLauncherChain {
	Register-ArxGameProcess (Get-Process -Id $PID)
}

# Record the game's pid in the lock, so other launchers know this arx.exe runs under the lock
function Set-ArxGameLockGame($Lock, [int]$GamePid) {
	if (-not $Lock) { return }
	$Lock.Line = $Lock.Line -replace 'game=\S+', "game=$GamePid"
	foreach ($f in $Lock.Files) { Write-ArxLockFile $f $Lock.Line }
}

# Release a lock taken with Enter-ArxGameLock (also happens by itself when the process ends)
function Exit-ArxGameLock($Lock) {
	if (-not $Lock) { return }
	Close-ArxLockFiles $Lock.Files
}

# --- 4. The launch ledger and 5. the budget --------------------------------------------------------------

function Get-ArxLaunchLogPath { Join-Path (Get-ArxAppDataDir) 'launches.log' }

# Append one launch to the ledger: key=value pairs, values with spaces quoted
function Add-ArxLaunchRecord([string]$Script, [string]$Copy, [string]$Branch, [string]$Build, [bool]$Vr, [int]$GamePid, $ExitCode, [double]$DurationSec, [string[]]$ArxArgs, [DateTime]$Time = (Get-Date), [bool]$Sim = $false) {
	$fields = [ordered]@{
		time = $Time.ToString('yyyy-MM-ddTHH:mm:ss'); script = $Script; copy = $Copy; branch = $Branch
		build = (($Build -replace ' fp=[0-9a-f]+$', '') -replace ' dirty$', '+dirty') -replace '^([0-9a-f]{9})[0-9a-f]+', '$1'
		# sim: the simulated headset (--vr-sim, #112) - VR code, no runtime, no headset
		mode = $(if ($Vr) { 'vr' } elseif ($Sim) { 'sim' } else { 'flat' }); pid = $GamePid; exit = $(if ($null -eq $ExitCode) { '-' } else { $ExitCode })
		duration = $DurationSec.ToString('0.0', [Globalization.CultureInfo]::InvariantCulture); args = ($ArxArgs -join ' ')
	}
	$line = ($fields.GetEnumerator() | ForEach-Object {
		$v = "$($_.Value)"
		if ($v -eq '') { $v = '-' }
		if ($v -match '[\s"]') { $v = '"' + ($v -replace '"', "'") + '"' }
		"$($_.Key)=$v"
	}) -join ' '
	$path = Get-ArxLaunchLogPath
	New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
	for ($i = 0; $i -lt 50; $i++) {
		try { [IO.File]::AppendAllText($path, $line + "`r`n", [Text.UTF8Encoding]::new($false)); return } catch [IO.IOException] { Start-Sleep -Milliseconds 50 }
	}
	Write-Warning "could not append to the launch ledger $path"
}

# The ledger's lines of the last $SinceHours hours (all when 0) as objects with the keys above
function Get-ArxLaunchRecords([double]$SinceHours = 0) {
	$path = Get-ArxLaunchLogPath
	if (-not (Test-Path $path)) { return @() }
	$since = if ($SinceHours -gt 0) { (Get-Date).AddHours(-$SinceHours) } else { [DateTime]::MinValue }
	$records = @()
	foreach ($line in [IO.File]::ReadAllLines($path)) {
		$record = [ordered]@{}
		foreach ($m in [regex]::Matches($line, '(\w+)=("([^"]*)"|\S+)')) {
			$record[$m.Groups[1].Value] = if ($m.Groups[3].Success) { $m.Groups[3].Value } else { $m.Groups[2].Value }
		}
		if (-not $record.Contains('time')) { continue }
		$time = [DateTime]::MinValue
		if (-not [DateTime]::TryParse($record.time, [ref]$time) -or $time -lt $since) { continue }
		$record.Time = $time
		$records += [pscustomobject]$record
	}
	$records
}

# The launch budget (WORKFLOW.md): a note when this branch of this copy has launched $Budget times or more
# in the last 24 hours. $Launches: how many launches are about to be made (default 1).
function Write-ArxLaunchBudgetNote([string]$Copy, [string]$Branch, [int]$Launches = 1, [int]$Budget = 10) {
	$done = @(Get-ArxLaunchRecords -SinceHours 24 | Where-Object { $_.copy -ieq $Copy -and $_.branch -eq $Branch }).Count
	if ($done + $Launches -le $Budget) { return }
	Write-Host "    launch budget: launch $($done + 1)$(if ($Launches -gt 1) { "-$($done + $Launches)" }) of '$Branch' in 24 h - the budget is $Budget per task (WORKFLOW.md); more only after asking the coordinator" -ForegroundColor Yellow
}

# --- 6. Self-test windows stay in the background --------------------------------------------------------------

# SDL reads its hints from the environment: with this one set, the game window is shown without being
# activated (SW_SHOWNA on Windows), so a self-test does not take the focus from the owner's work. The
# window still renders (a *minimized* window would not: ArxGame::run skips frames while minimized).
# Returns the previous value, to restore after the launch (Restore-ArxBackgroundWindow).
function Set-ArxBackgroundWindow {
	$previous = $env:SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN
	$env:SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN = '1'
	$previous
}

function Restore-ArxBackgroundWindow($Previous) {
	if ($null -eq $Previous) { Remove-Item Env:SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN -ErrorAction SilentlyContinue } else { $env:SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN = $Previous }
}

# The foreground window now and its process (#487); Handle 0 in a session without a desktop (the CI runner)
function Get-ArxForegroundWindow {
	if (-not ('ArxForegroundNative' -as [type])) {
		Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ArxForegroundNative {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
}
'@
	}
	$window = [ArxForegroundNative]::GetForegroundWindow()
	[uint32]$owner = 0
	if ($window -ne [IntPtr]::Zero) { $null = [ArxForegroundNative]::GetWindowThreadProcessId($window, [ref]$owner) }
	[pscustomobject]@{ Handle = [int64]$window; ProcessId = [int]$owner }
}

# A test run's game never takes the foreground (#487): the verdict on the foreground window $Now (from
# Get-ArxForegroundWindow) against the one $Before the launch. Ok is false only when the game's own window has it -
# another window the person at the PC switched to is theirs to take.
function Test-ArxForegroundKept($Before, $Now, [int]$GameProcessId) {
	if (-not $Now -or $Now.Handle -eq 0) { return [pscustomobject]@{ Ok = $true; Text = 'no foreground window (a session without a desktop)' } }
	if ($Now.ProcessId -eq $GameProcessId) { return [pscustomobject]@{ Ok = $false; Text = "the game's window took the foreground (#487)" } }
	if ($Before -and $Now.Handle -eq $Before.Handle) { return [pscustomobject]@{ Ok = $true; Text = "kept by the window that had it before the launch (pid $($Now.ProcessId))" } }
	[pscustomobject]@{ Ok = $true; Text = "another window has it (pid $($Now.ProcessId)), not the game" }
}

# --- 7. Scripted launches show no dialogs ------------------------------------------------------------------------------

# A game started by a script, a worker or CI never blocks on a message box (#501): with ARXVR_NO_DIALOGS=1 in its
# environment the game writes every dialog to arx.log and stderr as one '[dialog] <type>: <title>: <message>' line,
# answers it with its default (a question: no) and after an error exits with a failure code. The crash processor
# inherits it: no crash dialog, no Explorer window. The owner's own launches (test-pr.ps1, run-windows.ps1 by hand,
# the desktop and the Steam shortcut) keep the dialogs. The game also counts ARX_UNATTENDED - any non-empty value -
# which the test channel sets for the crash processor, so both are handled together.
#
# Set-ArxDialogEnv -NoDialogs sets both to 1 for the game about to be started; without -NoDialogs (an owner's launch)
# it removes both, so a variable left in the shell by an earlier script cannot take the owner's dialogs away. It
# returns the previous values: Restore-ArxDialogEnv puts them back right after the start, in a finally - the scripts
# run in the caller's session, which must come out of a launch unchanged (the started game keeps its own copy).
$script:ArxDialogVariables = @('ARXVR_NO_DIALOGS', 'ARX_UNATTENDED')

function Set-ArxDialogEnv([switch]$NoDialogs) {
	$previous = @{}
	foreach ($name in $script:ArxDialogVariables) {
		$previous[$name] = [Environment]::GetEnvironmentVariable($name)
		[Environment]::SetEnvironmentVariable($name, $(if ($NoDialogs) { '1' } else { $null }))
	}
	$previous
}

function Restore-ArxDialogEnv($Previous) {
	if (-not $Previous) { return }
	foreach ($name in $script:ArxDialogVariables) { [Environment]::SetEnvironmentVariable($name, $Previous[$name]) }
}

# Whether a run-windows.ps1 launch is a scripted one, without dialogs: with the test channel (-TestChannel or
# --test-dir) or an isolated user directory (-Isolated), a worker's runs - unless -Dialogs, which the owner's test
# station (test-pr.ps1) passes.
function Test-ArxScriptedLaunch([string[]]$ArxArgs, [switch]$TestChannel, [switch]$Isolated, [switch]$Dialogs) {
	if ($Dialogs) { return $false }
	[bool]($TestChannel -or $Isolated -or (@($ArxArgs) -contains '--test-dir'))
}

# The '[dialog]' lines a run without dialogs wrote (#501), from its arx.log and its stderr: the text from '[dialog]'
# on (the log prefix "[E] Dialog:123  " cut), each once. Missing files are skipped.
function Get-ArxDialogLines([string[]]$Paths) {
	$lines = foreach ($path in @($Paths)) {
		if (-not $path -or -not (Test-Path -LiteralPath $path)) { continue }
		foreach ($line in [IO.File]::ReadAllLines($path)) {
			$at = $line.IndexOf('[dialog] ')
			if ($at -ge 0) { $line.Substring($at).TrimEnd() }
		}
	}
	@($lines | Select-Object -Unique)
}

# The failure reason of a run that ended on a dialog: the first '[dialog]' line, and how many more there were
function Get-ArxDialogReason([string[]]$Lines) {
	$Lines = @($Lines | Where-Object { $_ })
	if (-not $Lines) { return $null }
	"the game ended on a dialog: $($Lines[0])$(if ($Lines.Count -gt 1) { " (and $($Lines.Count - 1) more)" })"
}
