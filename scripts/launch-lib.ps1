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
#   3. Only the working copy's own fresh build: Assert-ArxFreshBuild refuses a build directory of
#      another copy and a built-from.txt whose commit is not the copy's HEAD ("dirty" is allowed).
#   4. The launch ledger %LOCALAPPDATA%\ArxFatalisVR\launches.log: one line per launch (time, copy,
#      branch, build, mode, pid, exit code, duration), read by scripts/dev/cycle-report.ps1.
#   5. The launch budget: 10 launches per task (WORKFLOW.md); Write-ArxLaunchBudgetNote warns past it.
#   6. Self-test windows do not take the focus: Set-ArxBackgroundWindow sets the SDL hint that shows
#      the window without activating it (SW_SHOWNA).

. (Join-Path $PSScriptRoot 'build-paths.ps1')

function Get-ArxAppDataDir { Join-Path $env:LOCALAPPDATA 'ArxFatalisVR' }

function ConvertTo-ShortSha([string]$Sha) { if ($Sha -and $Sha.Length -gt 9) { $Sha.Substring(0, 9) } else { $Sha } }

# Machine-wide launch settings: settings.json in the lock folder (%ProgramData%\ArxFatalisVR\locks, so the
# CI runner service reads the same file), else the old %LOCALAPPDATA%\ArxFatalisVR\settings.json, e.g.
# { "maxGames": 2 } (how many games may run at once; default 1 - the owner's rule). Missing file or key:
# the defaults.
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

# vrstartup.exe of the SteamVR install (Steam app 250820), or $null
function Find-SteamVrStartup {
	$dir = Find-SteamAppInstall -AppId 250820
	if (-not $dir) { return $null }
	$exe = Join-Path $dir 'bin\win64\vrstartup.exe'
	if (Test-Path $exe) { $exe } else { $null }
}

# Start SteamVR and wait until vrserver.exe and vrcompositor.exe run, plus -SettleSec for the OpenXR
# runtime to be ready. The owner's path only (-StartSteamVR): a worker never calls this (D-014).
function Start-SteamVr([int]$TimeoutSec = 90, [int]$SettleSec = 15) {
	if (Test-SteamVrRunning) { return }
	$exe = Find-SteamVrStartup
	if (-not $exe) { throw 'SteamVR (Steam app 250820) was not found in any Steam library - start it by hand' }
	Write-Host "==> Starting SteamVR on -StartSteamVR: $exe" -ForegroundColor Yellow
	Start-Process -FilePath $exe | Out-Null
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while ((Get-Date) -lt $deadline) {
		if ((Test-SteamVrRunning) -and (Get-Process vrcompositor -ErrorAction SilentlyContinue)) {
			Write-Host "    vrserver and vrcompositor are up; settling $SettleSec s"
			Start-Sleep -Seconds $SettleSec
			return
		}
		Start-Sleep -Milliseconds 500
	}
	throw "SteamVR did not come up within $TimeoutSec s (vrserver.exe / vrcompositor.exe not running)"
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

# --- 3. The working copy's own fresh build ---------------------------------------------------------------

# Refuse (or with -Rebuild, rebuild) unless $BuildDir is a build directory of $SourceDir holding an
# arx.exe whose built-from.txt names the copy's HEAD ("<sha>" or "<sha> dirty"). Returns the stamp.
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

# Take a game lock slot (and with -Vr the VR lock), waiting up to $TimeoutSec seconds while every slot
# is held or a game runs outside the lock. Prints the holders while waiting. Returns the lock object
# (keep it, pass it to Set-ArxGameLockGame and Exit-ArxGameLock) or $null when the wait timed out.
function Enter-ArxGameLock([string]$What, [string]$Copy, [string]$Branch, [switch]$Vr, [int]$TimeoutSec = 300) {
	foreach ($dir in Get-ArxLockDirs) { Initialize-ArxLockDir $dir }
	$maxGames = (Get-ArxLaunchSettings).maxGames
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	$lastReport = [DateTime]::MinValue
	do {
		$taken = @()
		$slot = 0
		$busy = $null
		try {
			if ($Vr) {
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
		if (((Get-Date) - $lastReport).TotalSeconds -ge 30) {
			Write-Host "game lock busy ($busy)$(if ($TimeoutSec -gt 0) { " - waiting up to $TimeoutSec s, one game at a time on this PC (D-014)" })" -ForegroundColor Yellow
			foreach ($line in Get-ArxLockHolders) { Write-Host "  held: $line" -ForegroundColor Yellow }
			$lastReport = Get-Date
		}
		if ($TimeoutSec -gt 0) { Start-Sleep -Milliseconds 500 }
	} while ((Get-Date) -lt $deadline)
	return $null
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
function Add-ArxLaunchRecord([string]$Script, [string]$Copy, [string]$Branch, [string]$Build, [bool]$Vr, [int]$GamePid, $ExitCode, [double]$DurationSec, [string[]]$ArxArgs, [DateTime]$Time = (Get-Date)) {
	$fields = [ordered]@{
		time = $Time.ToString('yyyy-MM-ddTHH:mm:ss'); script = $Script; copy = $Copy; branch = $Branch
		build = ($Build -replace ' dirty$', '+dirty') -replace '^([0-9a-f]{9})[0-9a-f]+', '$1'
		mode = $(if ($Vr) { 'vr' } else { 'flat' }); pid = $GamePid; exit = $(if ($null -eq $ExitCode) { '-' } else { $ExitCode })
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
