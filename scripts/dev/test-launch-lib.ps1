<#
.SYNOPSIS
	Self-test of the machine-wide game lock (#205): where the lock files live, who may use them, and that a lock
	of one process is seen by another.

.DESCRIPTION
	The game lock (scripts/launch-lib.ps1, D-014) must hold between Windows accounts - the CI runner service is
	NETWORK SERVICE, the owner and the workers are another account. Two accounts cannot be used here, so this
	checks the parts that decide it, in scratch folders ($env:ARXVR_LOCK_DIR and a fake %LOCALAPPDATA%; the real
	locks and launches of the PC are not touched):
	  - the lock folder gets an ACL with an inheritable Modify rule for Authenticated Users and BUILTIN\Users, a
	    file created in it inherits it, a folder without the rules is repaired, and a folder that already has
	    them is left alone - so a lock file left behind by one account can be opened and deleted by another
	  - the machine-wide folder and the old per-account folder are both taken, listed and checked
	  - a lock held by another process makes a launch wait (timeout) and names the holder; a held lock of the
	    OLD location (an older launch-lib.ps1) counts too; a killed holder frees the lock at once; a lock file
	    left behind without a holder is free
	  - a game outside the lock is found through WMI even though its .Path may be hidden (another account), and
	    stops being a holder once a lock names its pid
	  - settings.json is read from the lock folder first
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-lock-test-$PID"
$lockDir = Join-Path $root 'machine\locks'
$legacyBase = Join-Path $root 'local'
New-Item -ItemType Directory -Force $legacyBase | Out-Null
$env:ARXVR_LOCK_DIR = $lockDir
$env:LOCALAPPDATA = $legacyBase
$legacyDir = Join-Path $legacyBase 'ArxFatalisVR'
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

$pwsh = (Get-Process -Id $PID).Path
$children = @()
# the holder processes get the same stub as the test itself (see "Taking a lock"): a real game running on this
# PC must not stop them taking the lock (#224)
function Start-Holder([string]$Code) {
	$libPath = (Resolve-Path (Join-Path $PSScriptRoot '..\launch-lib.ps1')).Path
	$p = Start-Process $pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', ". '$libPath'; function Get-ArxGamesOutsideLock { @() }; $Code")
	$script:children += $p
	$p
}
function Wait-Until([scriptblock]$Condition, [int]$Sec = 20) {
	$deadline = (Get-Date).AddSeconds($Sec)
	while ((Get-Date) -lt $deadline) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 200 }
	$false
}

# --- The kind of launch (#394) ---------------------------------------------------------------------------------
# A plain arx.exe is the player's launch (logos, main menu, every cinematic); the dev launchers add --dev-launch, the
# player launchers (the desktop shortcut, the Steam shortcut, the tester zip) never do
Write-Host '==> development and player launches'
$devArgs = @(Get-ArxLaunchKindArgs)
Check 'a launch is a development launch by default: --dev-launch' ($devArgs.Count -eq 1 -and $devArgs[0] -eq '--dev-launch') "$devArgs"
Check '-Player adds nothing: arx.exe as a player starts it' (@(Get-ArxLaunchKindArgs -Player).Count -eq 0)
$scripts = Split-Path -Parent $PSScriptRoot
$text = { param($Path) [IO.File]::ReadAllText((Join-Path $scripts $Path)) }
Check 'run-windows.ps1 takes its launch kind from launch-lib, -Player opts out' ((& $text 'run-windows.ps1') -match 'Get-ArxLaunchKindArgs -Player:\$Player')
Check 'smoke-test.ps1 takes its launch kind from launch-lib, -Player opts out' ((& $text 'dev\smoke-test.ps1') -match 'Get-ArxLaunchKindArgs -Player:\$Player')
Check 'xrs-run.ps1 and ci-smoke.ps1 pass a sequence''s @player on' (((& $text 'dev\xrs-run.ps1') -match '\$parsed\.Player') -and ((& $text 'dev\ci-smoke.ps1') -match '\$seq\.Player'))
Check 'test-pr.ps1 has -Player and passes it to run-windows.ps1' ((& $text 'dev\test-pr.ps1') -match '\$runArgs\.Player = \$true')
Check 'the desktop shortcut (launch-shortcut.ps1) launches as a player' ((& $text 'launch-shortcut.ps1') -match "run-windows\.ps1'\) [^\r\n]*-Player")
Check 'the verify run of package-tester.ps1 and make-test-saves.ps1 are development launches' (((& $text 'package-tester.ps1') -match "'--dev-launch'") -and ((& $text 'dev\make-test-saves.ps1') -match "'--dev-launch'"))
$testerLaunchers = @(Get-ChildItem (Join-Path $scripts 'tester') -Filter '*.cmd' | Where-Object { (Get-Content -Raw $_.FullName) -match 'arx\.exe' })
Check 'the tester zip''s launchers start arx.exe as a player (no --dev-launch)' ($testerLaunchers.Count -ge 2 -and -not ($testerLaunchers | Where-Object { (Get-Content -Raw $_.FullName) -match '--dev-launch' })) "$($testerLaunchers.Name)"

try {
	# --- The folder and its ACL ---------------------------------------------------------------------------------
	Write-Host '==> the lock folder and its ACL'
	Check 'lock folder is the override' ((Get-ArxLockDir) -eq $lockDir)
	Check 'two lock folders: machine-wide first, old per-account second' ((@(Get-ArxLockDirs) -join '|') -eq "$lockDir|$legacyDir")
	$env:ARXVR_LOCK_DIR = $legacyDir
	Check 'one folder when both locations are the same' (@(Get-ArxLockDirs).Count -eq 1)
	$env:ARXVR_LOCK_DIR = $null
	Check 'default folder is under ProgramData' ((Get-ArxLockDir) -eq (Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'ArxFatalisVR\locks'))
	$env:ARXVR_LOCK_DIR = $lockDir

	Initialize-ArxLockDir $lockDir
	Check 'created folder has the Authenticated Users and Users rules' (@(Get-ArxLockDirMissingSids $lockDir).Count -eq 0) "missing: $(Get-ArxLockDirMissingSids $lockDir)"
	$probe = Join-Path $lockDir 'probe.lock'
	[IO.File]::WriteAllText($probe, 'x')
	$fileSids = @((Get-Acl -LiteralPath $probe).Access | Where-Object { ($_.FileSystemRights -band [Security.AccessControl.FileSystemRights]::Modify) -eq [Security.AccessControl.FileSystemRights]::Modify -and $_.AccessControlType -eq 'Allow' } |
		ForEach-Object { try { $_.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value } catch { } })
	Check 'a lock file created in it inherits Modify for Authenticated Users' ('S-1-5-11' -in $fileSids) "Modify SIDs: $fileSids"
	Check 'a lock file created in it inherits Modify for Users' ('S-1-5-32-545' -in $fileSids)
	Remove-Item $probe -Force

	$strict = Join-Path $root 'strict'
	New-Item -ItemType Directory $strict | Out-Null
	$acl = Get-Acl -LiteralPath $strict
	$acl.SetAccessRuleProtection($true, $false)   # no inheritance, no rules but the owner's
	$acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.WindowsIdentity]::GetCurrent().User, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow'))
	Set-Acl -LiteralPath $strict -AclObject $acl
	Check 'a folder without the rules is detected' (@(Get-ArxLockDirMissingSids $strict).Count -eq 2)
	Initialize-ArxLockDir $strict
	Check 'Initialize-ArxLockDir repairs it' (@(Get-ArxLockDirMissingSids $strict).Count -eq 0)
	$before = (Get-Acl -LiteralPath $strict).Sddl
	Initialize-ArxLockDir $strict
	Check 'a folder that has the rules is left alone' ((Get-Acl -LiteralPath $strict).Sddl -eq $before)

	# --- Taking a lock ------------------------------------------------------------------------------------------
	# the lock tests do not look at the real games running on this PC
	$realOutsideLock = ${function:Get-ArxGamesOutsideLock}
	function Get-ArxGamesOutsideLock { @() }

	Write-Host '==> the lock'
	$lock = Enter-ArxGameLock -What 'test' -Copy 'c' -Branch 'b' -TimeoutSec 0
	Check 'a free lock is taken' ($null -ne $lock -and $lock.Slot -eq 1)
	Check 'the lock file is in the machine-wide folder' (Test-Path (Join-Path $lockDir 'game-1.lock'))
	Check 'and in the old folder (older launchers see it)' (Test-Path (Join-Path $legacyDir 'game-1.lock'))
	Check 'the holders list names it once' (@(Get-ArxLockHolders).Count -eq 1 -and (@(Get-ArxLockHolders))[0] -match "pid=$PID .*what=test copy=c branch=b") "got: $(Get-ArxLockHolders)"
	Check 'a second launch of this process does not get a slot' ($null -eq (Enter-ArxGameLock -What 'second' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null))
	Exit-ArxGameLock $lock
	Check 'released: no lock file left in either folder' (-not (Get-ChildItem $lockDir, $legacyDir -Filter '*.lock' -ErrorAction SilentlyContinue))
	Check 'released: no holders' (@(Get-ArxLockHolders).Count -eq 0)

	Write-Host '==> a lock held by another process'
	$holder = Start-Holder "`$l = Enter-ArxGameLock -What 'holder' -Copy 'other' -Branch 'other-branch' -TimeoutSec 0; Start-Sleep 120"
	Check 'the holder process took the lock' (Wait-Until { @(Get-ArxLockHolders).Count -gt 0 })
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$second = Enter-ArxGameLock -What 'waiter' -Copy 'c' -Branch 'b' -TimeoutSec 2 6>$null
	Check 'a launch waits for the timeout and gets nothing' ($null -eq $second -and $sw.Elapsed.TotalSeconds -ge 1.8) "elapsed $($sw.Elapsed.TotalSeconds)"
	$held = @(Get-ArxLockHolders)
	Check 'the holder is named (pid, what, copy, branch)' ($held.Count -eq 1 -and $held[0] -match "pid=$($holder.Id) .*what=holder copy=other branch=other-branch") "got: $held"
	Stop-Process -Id $holder.Id -Force
	$afterKill = $null
	Check 'a killed holder frees the lock at once (its files stay behind but are not busy)' (Wait-Until { $script:afterKill = Enter-ArxGameLock -What 'after-kill' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null; [bool]$script:afterKill })
	Exit-ArxGameLock $script:afterKill

	Write-Host '==> a lock held in the old folder only (an older launch-lib.ps1)'
	$old = Start-Holder "`$f = Open-ArxLockFile (Join-Path (Get-ArxAppDataDir) 'game-1.lock'); Start-Sleep 120"
	Check 'the old-style holder took its lock' (Wait-Until { @(Get-ArxLockHolders).Count -gt 0 })
	Check 'a launch with the new library waits for it' ($null -eq (Enter-ArxGameLock -What 'new' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null))
	Stop-Process -Id $old.Id -Force
	Check 'and runs once the old holder is gone' (Wait-Until { $script:x = Enter-ArxGameLock -What 'new' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null; [bool]$script:x })
	Exit-ArxGameLock $script:x

	Write-Host '==> a lock file left behind'
	[IO.File]::WriteAllText((Join-Path $lockDir 'game-1.lock'), 'pid=999999 game=- since=2020-01-01 00:00:00 what=dead copy=x branch=x')
	Check 'a stale file is not a holder' (@(Get-ArxLockHolders).Count -eq 0)
	$fresh = Enter-ArxGameLock -What 'fresh' -Copy 'c' -Branch 'b' -TimeoutSec 0
	Check 'and does not block a launch' ($null -ne $fresh)
	Exit-ArxGameLock $fresh

	Write-Host '==> settings.json'
	New-Item -ItemType Directory -Force $legacyDir | Out-Null
	Check 'default is one game' ((Get-ArxLaunchSettings).maxGames -eq 1)
	Set-Content (Join-Path $legacyDir 'settings.json') '{ "maxGames": 2 }'
	Check 'the old settings.json is still read' ((Get-ArxLaunchSettings).maxGames -eq 2)
	Set-Content (Join-Path $lockDir 'settings.json') '{ "maxGames": 3 }'
	Check 'the lock folder settings.json wins' ((Get-ArxLaunchSettings).maxGames -eq 3)
	Remove-Item (Join-Path $lockDir 'settings.json'), (Join-Path $legacyDir 'settings.json')

	# --- Games outside the lock ----------------------------------------------------------------------------------
	Write-Host '==> a game outside the lock'
	${function:Get-ArxGamesOutsideLock} = $realOutsideLock
	$fakeGame = Join-Path $root 'fake\arx.exe'
	New-Item -ItemType Directory -Force (Split-Path $fakeGame) | Out-Null
	Copy-Item (Join-Path $env:SystemRoot 'System32\PING.EXE') $fakeGame
	$game = Start-Process $fakeGame -PassThru -WindowStyle Hidden -ArgumentList '-n', '60', '127.0.0.1'
	$children += $game
	Check 'an arx.exe no lock names is found' (Wait-Until { @(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id).Count -eq 1 })
	Check 'with its path from WMI' ((@(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id))[0].Path -ieq $fakeGame)
	$withGame = Enter-ArxGameLock -What 'g' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null
	Check 'and keeps a launch waiting' ($null -eq $withGame)
	# a lock that names the pid makes the game known: write it by hand as Set-ArxGameLockGame would
	$taken = Open-ArxLockFiles 'game-1.lock'
	foreach ($f in $taken) { Write-ArxLockFile $f "pid=$PID game=$($game.Id) since=now what=t copy=c branch=b" }
	Check 'a lock naming its pid makes the game a known one' (@(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id).Count -eq 0)
	Close-ArxLockFiles $taken
	Check 'without the lock it is outside again' (@(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id).Count -eq 1)
} finally {
	foreach ($c in $children) { try { Stop-Process -Id $c.Id -Force -ErrorAction SilentlyContinue } catch { } }
	Start-Sleep -Milliseconds 300
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

# A test run's game never takes the foreground (#487): the verdict on what Get-ArxForegroundWindow reports
$before = [pscustomobject]@{ Handle = 100; ProcessId = 7 }
Check 'foreground: the game''s window has it - fail' (-not (Test-ArxForegroundKept $before ([pscustomobject]@{ Handle = 200; ProcessId = 42 }) 42).Ok)
Check 'foreground: the window that had it keeps it - ok' (Test-ArxForegroundKept $before ([pscustomobject]@{ Handle = 100; ProcessId = 7 }) 42).Ok
Check 'foreground: another window took it, not the game - ok' (Test-ArxForegroundKept $before ([pscustomobject]@{ Handle = 300; ProcessId = 9 }) 42).Ok
Check 'foreground: none in a session without a desktop - ok' (Test-ArxForegroundKept $null ([pscustomobject]@{ Handle = 0; ProcessId = 0 }) 42).Ok
$now = Get-ArxForegroundWindow
Check 'foreground: Get-ArxForegroundWindow answers' ($null -ne $now -and $now.Handle -ge 0)

# --- Scripted launches show no dialogs (#501) ---------------------------------------------------------------------
Write-Host '==> scripted launches show no dialogs (#501)'
$dialogRoot = Join-Path ([IO.Path]::GetTempPath()) "arxvr-dialog-test-$PID"
New-Item -ItemType Directory -Force $dialogRoot | Out-Null
try {
	# The environment a started process gets: ARXVR_NO_DIALOGS=1 while set, nothing after the restore
	$shellBefore = $env:ARXVR_NO_DIALOGS
	Remove-Item Env:ARXVR_NO_DIALOGS -ErrorAction SilentlyContinue
	$probe = { param($Name)
		$file = Join-Path $dialogRoot "$Name.txt"
		$p = Start-Process $pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', "Set-Content -LiteralPath '$file' -Value ""[`$env:ARXVR_NO_DIALOGS]""")
		$p.WaitForExit()
		(Get-Content -LiteralPath $file -Raw).Trim()
	}
	$previous = Set-ArxNoDialogs
	$seen = & $probe 'scripted'
	Restore-ArxNoDialogs $previous
	Check 'a process started after Set-ArxNoDialogs has ARXVR_NO_DIALOGS=1' ($seen -eq '[1]') $seen
	Check 'Restore-ArxNoDialogs removes it from the shell again' ($null -eq $env:ARXVR_NO_DIALOGS)
	$seen = & $probe 'owner'
	Check 'a process started after the restore (the owner''s next launch) has none' ($seen -eq '[]') $seen
	$env:ARXVR_NO_DIALOGS = 'kept'
	Restore-ArxNoDialogs (Set-ArxNoDialogs)
	Check 'a value set before is put back' ($env:ARXVR_NO_DIALOGS -eq 'kept')
	if ($null -eq $shellBefore) { Remove-Item Env:ARXVR_NO_DIALOGS -ErrorAction SilentlyContinue } else { $env:ARXVR_NO_DIALOGS = $shellBefore }

	# Which run-windows.ps1 launches are scripted ones
	Check 'run-windows -TestChannel: no dialogs' (Test-ArxScriptedLaunch -TestChannel)
	Check 'run-windows -Isolated: no dialogs' (Test-ArxScriptedLaunch -Isolated)
	Check 'run-windows -- --test-dir <dir>: no dialogs' (Test-ArxScriptedLaunch -ArxArgs @('--no-vr', '--test-dir', 'x'))
	Check 'run-windows by hand (no switch): dialogs shown' (-not (Test-ArxScriptedLaunch -ArxArgs @('--skiplogo', '--loadslot', '0')))
	Check 'test-pr.ps1 -Isolated (run-windows -Isolated -Dialogs): dialogs shown' (-not (Test-ArxScriptedLaunch -Isolated -Dialogs))
	$runText = & $text 'run-windows.ps1'
	Check 'run-windows.ps1 decides with Test-ArxScriptedLaunch from its own switches' ($runText -match 'Test-ArxScriptedLaunch -ArxArgs \$ArxArgs -TestChannel:\$TestChannel -Isolated:\$Isolated -Dialogs:\$Dialogs')
	Check 'run-windows.ps1 sets it around the start and restores it' ($runText -match 'Set-ArxNoDialogs' -and $runText -match 'Restore-ArxNoDialogs' -and $runText.IndexOf('Set-ArxNoDialogs') -lt $runText.IndexOf('Start-Process -FilePath $exe'))
	$smokeText = & $text 'dev\smoke-test.ps1'
	Check 'smoke-test.ps1 (and xrs-run.ps1 through it) start the game with it' ($smokeText.IndexOf('Set-ArxNoDialogs') -ge 0 -and $smokeText.IndexOf('Set-ArxNoDialogs') -lt $smokeText.IndexOf('Start-Process -FilePath $exe') -and ((& $text 'dev\xrs-run.ps1') -match "'smoke-test\.ps1'\) @smokeArgs"))
	Check 'ci-smoke.ps1 (CI) sets it for the whole job' ((& $text 'dev\ci-smoke.ps1') -match "(?m)^\`$env:ARXVR_NO_DIALOGS = '1'")
	Check 'make-test-saves.ps1 sets it; package-tester.ps1''s verify run sets ARX_UNATTENDED, which counts too' (((& $text 'dev\make-test-saves.ps1') -match "\`$env:ARXVR_NO_DIALOGS = '1'") -and ((& $text 'package-tester.ps1') -match "\`$env:ARX_UNATTENDED = '1'"))
	Check 'test-pr.ps1 (the owner) keeps the dialogs: passes -Dialogs' ((& $text 'dev\test-pr.ps1') -match '\$runArgs\.Dialogs = \$true')
	Check 'the desktop shortcut launches without -Isolated / -TestChannel: dialogs shown' ((& $text 'launch-shortcut.ps1') -notmatch '-Isolated|-TestChannel|ARXVR_NO_DIALOGS')
	$playerLaunchers = @(Get-ChildItem (Join-Path $scripts 'tester') -File) + @(Get-Item (Join-Path $scripts 'add-steam-shortcut.ps1'), (Join-Path $scripts 'steam-shortcuts-lib.ps1'))
	Check 'the Steam shortcut and the tester zip''s launchers never set it' (-not ($playerLaunchers | Where-Object { (Get-Content -Raw $_.FullName) -match 'ARXVR_NO_DIALOGS|ARX_UNATTENDED' })) "$($playerLaunchers.Name)"

	# The failure reason of a run that ended on a dialog: the line from arx.log and from stderr, once
	$dialogLog = Join-Path $dialogRoot 'arx.log'
	$dialogErr = Join-Path $dialogRoot 'stderr.txt'
	$line = '[dialog] error: Critical Error - Arx Fatalis VR: unknown option: --no-such-option'
	[IO.File]::WriteAllLines($dialogLog, @('[I] Startup:164           Starting Arx Fatalis VR', "[E] Dialog:533            $line"))
	[IO.File]::WriteAllLines($dialogErr, @("[E] Dialog:533            $line", 'Usage: arx [options]'))
	$lines = @(Get-ArxDialogLines @($dialogLog, $dialogErr, $null, (Join-Path $dialogRoot 'missing.txt')))
	Check 'Get-ArxDialogLines: the line from arx.log and stderr, once, without the log prefix' ($lines.Count -eq 1 -and $lines[0] -eq $line) "$lines"
	Check 'the reason names the dialog line' ((Get-ArxDialogReason $lines) -eq "the game ended on a dialog: $line")
	Check 'two dialogs: the first and a count' ((Get-ArxDialogReason @($line, '[dialog] info: x: y')) -eq "the game ended on a dialog: $line (and 1 more)")
	Check 'no dialog: no reason' ($null -eq (Get-ArxDialogReason @()) -and @(Get-ArxDialogLines @((Join-Path $dialogRoot 'missing.txt'))).Count -eq 0)
	Check 'smoke-test.ps1 puts the dialog line first among the failure reasons' ($smokeText -match '\$reasons\.Insert\(0, \(Get-ArxDialogReason \$dialogLines\)\)')
} finally {
	Remove-Item $dialogRoot -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-launch-lib: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-launch-lib: all checks passed' -ForegroundColor Green
exit 0
