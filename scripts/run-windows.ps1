<#
.SYNOPSIS
	Run the locally built Arx Fatalis VR with the game data from the Steam install of Arx Fatalis.

.DESCRIPTION
	Finds the Arx Fatalis install (Steam app 1700) in the player's Steam libraries and starts arx.exe
	with it as the data directory. Any extra arguments are passed through to arx.exe.

	Test runs start in a window (arx.exe --windowed) so the game does not take over the desktop;
	pass -Fullscreen to use the fullscreen setting from cfg.ini instead. A launch from here is a
	development launch (arx.exe --dev-launch, #394): straight to the main menu and every .cin cinematic
	is skipped; add --intro to see the publisher logos and the intro cinematic, --cinematics to see all
	cinematics (new-game intro, in-game cutscenes). -Player launches as a player does - arx.exe without
	--dev-launch: the logos, the main menu and every cinematic (the desktop shortcut does that).

	Launch control (docs/vr/WORKFLOW.md "Parallel work", decision D-014; scripts/launch-lib.ps1):
	  - no SteamVR start: without --no-vr the game may start in VR only when SteamVR (vrserver.exe) is
	    already running; otherwise the launch is made flat (--no-vr) with a note. -StartSteamVR, the
	    owner's switch, starts SteamVR first (the headset turns on) and is never used by a worker.
	  - one game at a time on this PC: the launch holds a game lock slot while the game runs (a VR
	    launch also the VR lock), waits up to -LockTimeoutSec for the holder and names it.
	  - only this working copy's own fresh build: an arx.exe from another copy, or whose built-from.txt
	    does not name the copy's HEAD or predates later uncommitted edits (tree fingerprint, #306), is refused; -Rebuild builds first.
	  - the launch is written to the ledger %LOCALAPPDATA%\ArxFatalisVR\launches.log.
	  - -Background shows the window without taking the focus (self-tests).

	Parallel work: -Isolated gives this working copy its own user and config directory
	(%LOCALAPPDATA%\ArxFatalisVR\userdirs\<folder>-<hash>: arx.log, cfg.ini, saves, crash reports), so a
	worker's runs never touch the player's files in Saved Games\Arx Libertatis; -Saves copies save
	slots into it.

	The game finds the Arx Fatalis data in the Steam libraries itself (#79, arx.log line "Steam data:");
	-DataDir passes another install folder as --data-dir.

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.
	Must be a build directory of this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder, passed as --data-dir. Default: none - the game finds the install in the
	Steam libraries itself (#79).

.PARAMETER Sound
	Keep the game's sound in a background launch. A launch by hand has sound anyway; -Background and
	-TestChannel (a worker's self-test) launches are silent (arx.exe --mute, #345) so they do not play over
	the owner. test-pr.ps1 and launch-shortcut.ps1 pass it - the owner's launches have sound.

.PARAMETER Mute
	Silence the launch (arx.exe --mute) even by hand; cfg.ini keeps its volumes.

.PARAMETER DryRun
	Print the arx.exe command line and exit: nothing is started, no game lock is taken.

.PARAMETER Fullscreen
	Do not force windowed mode; use the fullscreen setting from cfg.ini.

.PARAMETER Player
	A player's launch (#394): no --dev-launch, so the publisher logos, the main menu and every .cin cinematic are
	shown as in the original game. launch-shortcut.ps1 (the desktop shortcut) passes it.

.PARAMETER Isolated
	Use this working copy's own user and config directory (--user-dir / --config-dir) instead of the
	player's Saved Games\Arx Libertatis. -UserDir names another directory.

.PARAMETER Playground
	Start the test playground (#321, scripts\dev\playground): generates the level with arx-level-generator when it
	is missing or older than the generator, then starts a new game in it (--data-dir <level folder> --loadlevel 30).

.PARAMETER UserDir
	The user and config directory to use (implies -Isolated).

.PARAMETER Saves
	Save slots to copy into the isolated user directory before the launch, e.g. save0000 (needs
	-Isolated). Source: -SaveLibrary, $env:ARXVR_SAVE_LIBRARY, or the player's save folder.

.PARAMETER Save
	A save of the test save library (#70, scripts\dev\make-test-saves.ps1), e.g. mvp-goblin-fight (needs
	-Isolated): the ONLY save of the isolated user directory, and `--loadslot 0` is the default argument.

.PARAMETER SaveLibrary
	Folder holding the saves for -Saves / -Save (the test save library, #70). Default:
	$env:ARXVR_SAVE_LIBRARY or %LOCALAPPDATA%\ArxFatalisVR\savelib.

.PARAMETER TestChannel
	Start with the test channel (--test-dir) of this working copy, so scripts/dev/game-status.ps1 and
	game-cmd.ps1 can talk to the game (docs/vr/TESTING.md).

.PARAMETER StartSteamVR
	The owner's switch: start SteamVR when it is not running, then launch in VR. Workers never pass it.

.PARAMETER Rebuild
	Build this working copy first when its build is stale (or missing).

.PARAMETER Background
	Show the game window without activating it, so the launch does not take the focus (self-tests).

.PARAMETER Dialogs
	Keep the game's dialogs in a launch that would otherwise go without them. A launch with the test channel
	(-TestChannel or --test-dir) or an isolated user directory (-Isolated) is a scripted one: the game gets
	ARXVR_NO_DIALOGS=1 and writes every dialog to arx.log and stderr as a '[dialog]' line instead of waiting
	for a click (#501). test-pr.ps1 - the owner's launch - passes -Dialogs.

.PARAMETER LockTimeoutSec
	How long to wait for the game lock when another game runs (default 300). 0: do not wait.

.EXAMPLE
	.\scripts\run-windows.ps1
.EXAMPLE
	.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
.EXAMPLE
	.\scripts\run-windows.ps1 -Fullscreen -Config Debug -- --no-vr
.EXAMPLE
	.\scripts\run-windows.ps1 -Player
	# as a player: the publisher logos, the main menu, the intro cinematic after New game
.EXAMPLE
	.\scripts\run-windows.ps1 -Isolated -Saves save0000 -TestChannel -Background -- --no-vr --loadslot 0
#>
# PositionalBinding=$false: after "--" everything is positional, and without this "--skiplogo" would bind to -Config.
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[switch]$Fullscreen,
	[switch]$Player,
	[switch]$Isolated,
	[switch]$Playground,
	[string]$UserDir,
	[string[]]$Saves,
	[string]$Save,
	[string]$SaveLibrary,
	[switch]$TestChannel,
	[switch]$StartSteamVR,
	[switch]$Sound,
	[switch]$Mute,
	[switch]$DryRun,
	[switch]$Rebuild,
	[switch]$Background,
	[switch]$Dialogs,
	[Alias('VrLockTimeoutSec')]
	[int]$LockTimeoutSec = 300,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'launch-lib.ps1')
$SourceDir = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $SourceDir $Config }
if (-not $ArxArgs) { $ArxArgs = @() }
if ($Playground) {
	# The test playground (#321): the generated level's folder as a data dir, a new game in its area
	. (Join-Path $PSScriptRoot 'dev\playground\playground-lib.ps1')
	$ArxArgs = @(Get-ArxPlaygroundArgs (Initialize-ArxPlaygroundLevel)) + $ArxArgs
}

# Only this working copy's own fresh build (D-014)
$copy = Get-ArxWorkingCopyInfo $SourceDir
$stamp = Assert-ArxFreshBuild -SourceDir $SourceDir -BuildDir $BuildDir -Config $Config -Head $copy.Head -Rebuild:$Rebuild
$exe = Join-Path $BuildDir 'arx.exe'

# The game finds the data in the Steam libraries itself (#79); --data-dir only for -DataDir
$dataArgs = if ($DataDir) { @('--data-dir', $DataDir) } else { @() }

$modeArgs = @()
if (-not $Fullscreen) { $modeArgs += '--windowed' }
# A development launch skips the logos and the cinematics; -Player launches as a player does (#394)
$modeArgs += Get-ArxLaunchKindArgs -Player:$Player
# By hand the game has sound; a background launch (-Background, -TestChannel) is silent unless -Sound (#345)
if ($Mute -or (($Background -or $TestChannel) -and -not $Sound)) { $modeArgs += '--mute' }

# Isolated user and config directory
$dirArgs = @()
if ($UserDir) { $Isolated = $true } elseif ($Isolated) { $UserDir = Get-DefaultUserDir $SourceDir }
if ($Isolated) {
	if (-not $DryRun) { New-Item -ItemType Directory -Force $UserDir | Out-Null }
	$dirArgs += '--user-dir', $UserDir, '--config-dir', $UserDir
} elseif ($Saves -or $Save) {
	throw '-Saves / -Save copy saves into an isolated user directory: add -Isolated (the player''s own saves are never touched)'
}
if ($TestChannel) { $dirArgs += '--test-dir', (Get-DefaultTestDir $SourceDir) }

Write-Host "==> Running $exe" -ForegroundColor Cyan
Write-Host "    build: $(ConvertTo-ShortSha $stamp) ($($copy.Branch))"
Write-Host "    data: $(if ($DataDir) { "$DataDir (--data-dir)" } else { 'found by the game in the Steam libraries (arx.log: Steam data)' })"
if ($Isolated) { Write-Host "    user dir: $UserDir (isolated)" } else { Write-Host "    user dir: $(Get-SharedUserDir) (shared)" }
if ($TestChannel) { Write-Host "    test channel: $(Get-DefaultTestDir $SourceDir)" }
# A scripted launch (test channel, isolated user dir) never blocks on a message box (#501); by hand the dialogs stay
$noDialogs = Test-ArxScriptedLaunch -ArxArgs $ArxArgs -TestChannel:$TestChannel -Isolated:$Isolated -Dialogs:$Dialogs
Write-Host "    dialogs: $(if ($noDialogs) { 'off (ARXVR_NO_DIALOGS=1: written to arx.log and stderr)' } else { 'shown' })"
if ($DryRun) {
	# Nothing is written or started: the saves are not copied and SteamVR is not started (D-014)
	if ($Saves) { Write-Host "    dry run: would copy the saves $($Saves -join ', ')" }
	if ($Save) {
		Write-Host "    dry run: would use the save $Save"
		if (-not $ArxArgs) { $ArxArgs = @('--loadslot', '0') }
	}
} else {
	if ($Saves) { Copy-ArxSaves -Names $Saves -UserDir $UserDir -Library $SaveLibrary }
	if ($Save) {
		Use-ArxSave -Name $Save -UserDir $UserDir -Library $SaveLibrary
		if (-not $ArxArgs) { $ArxArgs = @('--loadslot', '0') }
	}
	# A --loadslot of a save that is not there loads nothing: refuse before the game (and the headset) starts
	$slotProblem = Get-ArxLoadSlotProblem -ArxArgs $ArxArgs -UserDir $(if ($Isolated) { $UserDir } else { Get-SharedUserDir })
	if ($slotProblem) { throw $slotProblem }
}

# No SteamVR start without the owner: flat unless SteamVR already runs (or -StartSteamVR)
if ($DryRun -and $StartSteamVR -and $ArxArgs -notcontains '--no-vr' -and -not (Test-SteamVrRunning)) {
	Write-Host '    dry run: would start SteamVR (-StartSteamVR); the launch would be VR' -ForegroundColor Yellow
	$vr = [pscustomobject]@{ Vr = $true; Args = @($ArxArgs); Reason = 'dry run: SteamVR would be started on -StartSteamVR' }
} else {
	$vr = Resolve-ArxVrMode -ArxArgs $ArxArgs -StartSteamVR:$StartSteamVR
}
$ArxArgs = $vr.Args
Write-Host "    mode: $(if ($vr.Vr) { 'VR' } else { 'flat' }) ($($vr.Reason))"

# Start-Process does not quote arguments: paths with spaces need explicit quotes (docs/vr/TRAPS.md)
$quote = { param($a) if ("$a" -match '\s') { '"' + $a + '"' } else { "$a" } }
$argList = @($dataArgs + $modeArgs + $dirArgs + $ArxArgs | ForEach-Object { & $quote $_ })
if ($DryRun) {
	Write-Host "    dry run, not started: arx.exe $($argList -join ' ')" -ForegroundColor Cyan
	exit 0
}
Write-ArxLaunchBudgetNote -Copy $SourceDir -Branch $copy.Branch

# One game at a time: hold a game lock slot (and the VR lock for a VR launch) while the game runs
$lock = Enter-ArxGameLock -What 'run-windows.ps1' -Copy $SourceDir -Branch $copy.Branch -Vr:$vr.Vr -TimeoutSec $LockTimeoutSec
if (-not $lock) {
	throw "another game is running on this PC (one at a time, D-014):`n  $((Get-ArxLockHolders) -join "`n  ")`nWait for it to end, then launch again."
}
Write-Host "    game lock: slot $($lock.Slot)$(if ($lock.Vr) { ' + VR lock' })"

$code = $null
$process = $null
$started = Get-Date
$previousHint = $null
if ($Background) { $previousHint = Set-ArxBackgroundWindow }
# Scripted: ARXVR_NO_DIALOGS=1; by hand: neither variable, even if an earlier script in this shell left one (#501)
$previousDialogEnv = Set-ArxDialogEnv -NoDialogs:$noDialogs
try {
	$process = Start-Process -FilePath $exe -ArgumentList $argList -PassThru -NoNewWindow
	Set-ArxGameLockGame $lock $process.Id
	$process.WaitForExit()
	$code = $process.ExitCode
} finally {
	if ($Background) { Restore-ArxBackgroundWindow $previousHint }
	Restore-ArxDialogEnv $previousDialogEnv
	Exit-ArxGameLock $lock
	Add-ArxLaunchRecord -Script 'run-windows.ps1' -Copy $SourceDir -Branch $copy.Branch -Build $stamp -Vr $vr.Vr `
		-GamePid $(if ($process) { $process.Id } else { 0 }) -ExitCode $code -DurationSec ((Get-Date) - $started).TotalSeconds -ArxArgs $ArxArgs -Time $started
}

# The run digest and automatic bug filing (#131): after the game closed, never in the owner's way - the digest is
# local and quick, the GitHub part goes to a detached background process, and a failure here only warns.
if ($process) {
	try {
		$digestUserDir = if ($Isolated) { $UserDir } else { Get-SharedUserDir }
		& (Join-Path $PSScriptRoot 'dev\log-digest.ps1') -UserDir $digestUserDir -Since $started -Source 'run-windows.ps1' `
			-ExitCode $code -DurationSec ((Get-Date) - $started).TotalSeconds -Copy $SourceDir -Detach
	} catch {
		Write-Warning "run digest failed (the game's exit code is unaffected): $($_.Exception.Message)"
	}
}
exit $code
