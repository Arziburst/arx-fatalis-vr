<#
.SYNOPSIS
	Smoke test: start the game from a script, wait until it is playable, quit it, check the evidence.

.DESCRIPTION
	Starts arx.exe with the test channel (--test-dir, see docs/vr/TESTING.md), waits until
	status.json reports the expected state (CINEMATIC or GAMEPLAY after --loadlevel / --loadslot /
	--loadsave, MENU for a plain launch), runs the extra channel -Commands, keeps the game running
	for -Seconds, quits it through the channel (or with WM_CLOSE: -CloseWindow) and checks:
	  - the process exit code is 0
	  - arx.log was written after the launch and contains "Clean shutdown"
	  - no new crash report appeared in the user directory's crashes\ folder
	  - arx.log has no [E] line and status.json reports errors == 0
	  - every -Commands line was acknowledged without an error
	Prints the state timeline, the [E]/[W] log lines, the tail of the log and a PASS / FAIL verdict
	per run. Exit code 0 when every run passed, 1 otherwise, 2 when a VR run was skipped because
	another game holds the VR lock.

	The game runs in a window (--windowed) and without the crash dialog (ARX_UNATTENDED=1), so a
	crash leaves a report in crashes\ and the test fails instead of waiting for a click.

	Isolation (docs/vr/WORKFLOW.md, "Parallel work"): the game gets this working copy's own user and
	config directory (--user-dir / --config-dir = %LOCALAPPDATA%\ArxFatalisVR\userdirs\<folder>-<hash>)
	and its own test channel directory, so parallel workers never share arx.log, cfg.ini, saves or
	status.json with each other or with the player's Saved Games\Arx Libertatis (-Shared opts out).
	-Saves copies save slots into the isolated directory for -- --loadslot runs. A -Vr run takes the
	machine-wide VR lock (scripts/vr-lock.ps1) for the whole test; when another game holds it for
	longer than -VrLockTimeoutSec the test is SKIPPED (exit 2), not failed. Flat runs need no lock.

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder. Default: found automatically in the Steam libraries.

.PARAMETER TestDir
	The test channel directory. Default: $env:ARXVR_TEST_DIR or %LOCALAPPDATA%\ArxFatalisVR\test\<folder>-<hash>.

.PARAMETER UserDir
	The user and config directory for the game. Default: this working copy's isolated one.

.PARAMETER Shared
	Use the player's %USERPROFILE%\Saved Games\Arx Libertatis (their saves, log and config) instead of
	the isolated directory. Refuses to run while any other arx.exe runs (they would share the log).

.PARAMETER Saves
	Save slots to copy into the isolated user directory before the runs, e.g. save0000. Source:
	-SaveLibrary, $env:ARXVR_SAVE_LIBRARY, or the player's save folder (see build-paths.ps1).

.PARAMETER SaveLibrary
	Folder holding the save slots for -Saves (the test save library, #70).

.PARAMETER VrLockTimeoutSec
	With -Vr: how long to wait for the VR lock before skipping the test (default 60).

.PARAMETER Vr
	Start in VR (no --no-vr). SteamVR must be running; the result records the OpenXR session state.

.PARAMETER Menu
	A plain launch that expects the main menu (no --loadlevel 10 default); combine with -Commands 'loadslot 0'.

.PARAMETER Seconds
	How long the game stays in the expected state before it is quit (default 10).

.PARAMETER Repeat
	Run the whole thing N times and print a table (default 1).

.PARAMETER StartTimeoutSec
	How long to wait for the expected state after the launch (default 90).

.PARAMETER Commands
	Channel commands to send once the game is in the expected state, e.g. 'shot start', 'mark hello',
	'player.setplayerpos 0 0 0' (see docs/vr/TESTING.md). Each must be acknowledged without an error.

.PARAMETER CloseWindow
	Quit with WM_CLOSE (what a player does) instead of the channel's quit command.

.EXAMPLE
	.\scripts\dev\smoke-test.ps1
	# flat, --loadlevel 10 (the default when no arguments for arx.exe are given)
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Vr -- --loadslot 0
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Commands 'shot start','mark after-shot' -- --loadlevel 11
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Saves save0000 -- --loadslot 0
	# the save comes from the library (or the player's save folder) into the isolated user directory
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[string]$TestDir,
	[string]$UserDir,
	[switch]$Shared,
	[string[]]$Saves,
	[string]$SaveLibrary,
	[int]$VrLockTimeoutSec = 60,
	[switch]$Vr,
	[switch]$Menu,
	[int]$Seconds = 10,
	[int]$Repeat = 1,
	[int]$StartTimeoutSec = 90,
	[string[]]$Commands = @(),
	[switch]$CloseWindow,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $ArxSourceDir $Config }
$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }
if (-not $DataDir) { $DataDir = Find-ArxFatalisSteamInstall }
if (-not $DataDir) { throw 'Arx Fatalis (Steam app 1700) was not found in any Steam library. Install it, or pass -DataDir.' }
if (-not $TestDir) { $TestDir = Get-ArxTestDir }
if ($UserDir -and $Shared) { throw '-UserDir and -Shared exclude each other' }
if (-not $UserDir) { $UserDir = Get-ArxUserDir -Shared:$Shared }
if ($Shared -and $Saves) { throw '-Saves copies saves into the isolated user directory; it cannot be combined with -Shared' }
if (-not $ArxArgs -and -not $Menu) { $ArxArgs = @('--loadlevel', '10') }
if (-not $ArxArgs) { $ArxArgs = @() }

$loadsSomething = ($ArxArgs -join ' ') -match '--load(level|slot|save)'
$expectedStates = if ($loadsSomething) { @('GAMEPLAY', 'CINEMATIC') } else { @('MENU') }

$log = Get-ArxLogPath $UserDir
$crashDir = Get-ArxCrashDir $UserDir
if (-not $Shared) {
	New-Item -ItemType Directory -Force $UserDir | Out-Null
	if ($Saves) { Copy-ArxSaves -Names $Saves -UserDir $UserDir -Library $SaveLibrary }
}
Write-Host "user dir: $UserDir ($(if ($Shared) { 'shared' } else { 'isolated' }));  test channel: $TestDir"

function Write-Check([bool]$Ok, [string]$Text) {
	if ($Ok) { Write-Host "  [ok]   $Text" -ForegroundColor Green } else { Write-Host "  [FAIL] $Text" -ForegroundColor Red }
}

function Invoke-SmokeRun([int]$Run) {
	$reasons = [Collections.Generic.List[string]]::new()
	$result = [ordered]@{ Run = $Run; Result = 'FAIL'; Exit = $null; State = '-'; Frames = 0; Fps = 0; VR = '-'; Errors = '-'; Warnings = '-'; Crash = '-'; DrawCheck = '-'; Reasons = '' }
	$process = $null
	try {
		# Isolated runs only clash with a game from the same build (same exe to relink, same channel);
		# a shared run clashes with every game, because they would all write the same arx.log.
		$running = @(Get-Process arx -ErrorAction SilentlyContinue | Where-Object { $Shared -or $_.Path -eq $exe })
		if ($running) {
			$what = if ($Shared) { 'another arx.exe' } else { 'arx.exe from this build' }
			throw "$what is already running (pid $($running.Id -join ', ')) - close it first"
		}
		if ($Vr -and -not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
			throw '-Vr given but SteamVR (vrserver.exe) is not running - the game would start flat'
		}

		New-Item -ItemType Directory -Force $TestDir | Out-Null
		foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt') {
			Remove-Item (Join-Path $TestDir $f) -Force -ErrorAction SilentlyContinue
		}
		if (Test-Path $log) { Remove-Item $log -Force }
		$crashesBefore = @(Get-ChildItem $crashDir -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)

		# Start-Process does not quote arguments: paths with spaces need explicit quotes
		$argList = @('--data-dir', "`"$DataDir`"", '--windowed', '--test-dir', "`"$TestDir`"")
		if (-not $Shared) { $argList += '--user-dir', "`"$UserDir`"", '--config-dir', "`"$UserDir`"" }
		if (-not $Vr) { $argList += '--no-vr' }
		$argList += $ArxArgs
		Write-Host "=== run $Run/$Repeat`: arx.exe $($argList -join ' ')" -ForegroundColor Cyan
		$env:ARX_UNATTENDED = '1'
		$launchTime = Get-Date
		$process = Start-Process -FilePath $exe -ArgumentList $argList -PassThru `
			-RedirectStandardOutput (Join-Path $TestDir 'stdout.txt') -RedirectStandardError (Join-Path $TestDir 'stderr.txt')

		# Wait for the expected state, printing the timeline
		$deadline = (Get-Date).AddSeconds($StartTimeoutSec)
		$lastState = ''
		$status = $null
		$reached = $false
		while ((Get-Date) -lt $deadline) {
			if ($process.HasExited) { break }
			$status = Read-ArxStatus $TestDir
			if ($status -and $status.state -ne $lastState) {
				$lastState = $status.state
				Write-Host ("  +{0,6:0.0}s  state {1} (frame {2})" -f ((Get-Date) - $launchTime).TotalSeconds, $status.state, $status.frame)
			}
			if ($status -and $expectedStates -contains $status.state) { $reached = $true; break }
			Start-Sleep -Milliseconds 250
		}
		if ($process.HasExited) {
			$reasons.Add("the game exited on its own with code $($process.ExitCode) before reaching $($expectedStates -join '/')")
		} elseif (-not $reached) {
			$reasons.Add("did not reach $($expectedStates -join '/') within $StartTimeoutSec s (last state: $(if ($status) { $status.state } else { 'no status.json' }))")
		}
		$result.State = if ($status) { $status.state } else { '-' }

		if ($reached) {
			if ($Vr) {
				# Give the runtime a moment to move the session past idle
				try { $status = Wait-ArxStatus { param($s) $s.vr.sessionState -ne 'idle' } -TimeoutSec 10 -Dir $TestDir -What 'a session state' } catch { }
				$status = Read-ArxStatus $TestDir
				$result.VR = "$($status.vr.enabled)/$($status.vr.sessionState)"
				Write-Host "  VR: enabled=$($status.vr.enabled) session=$($status.vr.sessionState)"
				if (-not $status.vr.enabled) { $reasons.Add('VR requested but the game runs flat (see the log for the reason)') }
			}

			foreach ($command in $Commands) {
				$ack = Send-ArxCommand -Lines @($command) -Dir $TestDir
				Write-Host "  > $command`n    $ack"
				if ($ack -notmatch 'errors=0 ') { $reasons.Add("command '$command' failed: $ack") }
			}

			Start-Sleep -Seconds $Seconds
			$status = Read-ArxStatus $TestDir
			if ($process.HasExited) { $reasons.Add("the game exited on its own with code $($process.ExitCode) while it should have been running") }

			# A 'drawcheck' sent through -Commands draws the scene twice per frame with the same camera
			# (docs/vr/TESTING.md): it must finish, and the two pictures must be identical in every frame
			if ($status -and $status.drawCheck -and -not $process.HasExited) {
				try {
					$status = Wait-ArxStatus { param($s) $s.drawCheck -and -not $s.drawCheck.running } -TimeoutSec 60 -Dir $TestDir -What 'the draw check to finish'
				} catch { $reasons.Add("$_") }
				$dc = $status.drawCheck
				$result.DrawCheck = "$($dc.framesDiffering)/$($dc.frames)"
				Write-Check ([int]$dc.framesDiffering -eq 0 -and -not $dc.running) "drawcheck: $($dc.framesDiffering) of $($dc.frames) frames differ between two draws of the scene (max $($dc.maxPixelsDiffering) of $($dc.pixels) pixels, max delta $($dc.maxDelta))"
				if ([int]$dc.framesDiffering -ne 0) { $reasons.Add("drawcheck: $($dc.framesDiffering) of $($dc.frames) frames differ (most differing pair: $($dc.capture)_a.bmp / _b.bmp)") }
			}
		}

		# Quit
		if (-not $process.HasExited) {
			if ($CloseWindow) {
				Write-Host '  closing the window (WM_CLOSE)'
				$null = $process.CloseMainWindow()
			} else {
				try { $null = Send-ArxCommand -Lines @('quit') -Dir $TestDir -TimeoutSec 10 } catch { $reasons.Add("quit was not acknowledged: $_"); $null = $process.CloseMainWindow() }
			}
			if (-not $process.WaitForExit(60000)) {
				$reasons.Add('the game did not exit within 60 s, killed')
				$process.Kill()
			}
		}
		$process.WaitForExit()
		$result.Exit = $process.ExitCode

		# Evidence: frames and fps from the last status while the game ran, counters from the final one
		if ($status) { $result.Frames = $status.frame; $result.Fps = [math]::Round([double]$status.fps, 1) }
		$final = Read-ArxStatus $TestDir
		if ($final) {
			$result.Errors = $final.errors; $result.Warnings = $final.warnings
			if ($Vr) { $result.VR = "$($final.vr.enabled)/$($final.vr.sessionState)" }
		}
		$logFresh = (Test-Path $log) -and ((Get-Item $log).LastWriteTime -ge $launchTime)
		$clean = $logFresh -and [bool](Select-String -Path $log -Pattern 'Clean shutdown' -Quiet)
		$errorLines = if ($logFresh) { @(Select-String -Path $log -Pattern '^\[[EC]\]' | ForEach-Object Line) } else { @() }
		$warningLines = if ($logFresh) { @(Select-String -Path $log -Pattern '^\[W\]' | ForEach-Object Line) } else { @() }
		$crashesAfter = @(Get-ChildItem $crashDir -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
		$newCrashes = @($crashesAfter | Where-Object { $_ -notin $crashesBefore })
		$result.Crash = if ($newCrashes) { $newCrashes -join ';' } else { '-' }

		Write-Check ($process.ExitCode -eq 0) "exit code $($process.ExitCode)"
		Write-Check $logFresh "arx.log written after the launch"
		Write-Check $clean "'Clean shutdown' in arx.log"
		Write-Check ($newCrashes.Count -eq 0) "no new crash report ($($crashesAfter.Count) in $crashDir)"
		Write-Check ($errorLines.Count -eq 0) "no [E] line in arx.log ($($errorLines.Count) found, $($warningLines.Count) warnings)"
		Write-Check ($final -and [int]$final.errors -eq 0) "status.json errors == 0 (errors=$(if ($final) { $final.errors } else { '?' }), cleanShutdown=$(if ($final) { $final.cleanShutdown } else { '?' }))"
		if ($process.ExitCode -ne 0) { $reasons.Add("exit code $($process.ExitCode)") }
		if (-not $logFresh) { $reasons.Add('arx.log was not written by this run (the game died before shutdown, or never started)') }
		elseif (-not $clean) { $reasons.Add("no 'Clean shutdown' in arx.log") }
		if ($newCrashes) { $reasons.Add("new crash report: $($newCrashes -join ', ')") }
		if ($errorLines) { $reasons.Add("$($errorLines.Count) [E] line(s) in arx.log") }
		if ($final -and [int]$final.errors -ne 0) { $reasons.Add("status.json errors=$($final.errors)") }

		if ($errorLines -or $warningLines) {
			Write-Host '  --- [E]/[W] lines:'
			$errorLines + $warningLines | ForEach-Object { Write-Host "  $_" }
		}
		if ($logFresh) {
			Write-Host '  --- arx.log tail:'
			Get-Content $log -Tail 6 | ForEach-Object { Write-Host "  $_" }
		}
		foreach ($c in $newCrashes) {
			$report = Join-Path (Join-Path $crashDir $c) 'crash.txt'
			if (Test-Path $report) {
				Write-Host "  --- $report`:"
				Get-Content $report -TotalCount 14 | ForEach-Object { Write-Host "  $_" }
			}
		}
		$stderr = Get-Content (Join-Path $TestDir 'stderr.txt') -Raw -ErrorAction SilentlyContinue
		if ($stderr) { Write-Host "  --- stderr:`n$stderr" }
	} catch {
		$reasons.Add("$_")
		if ($process -and -not $process.HasExited) { try { $process.Kill() } catch { } }
	}

	$result.Reasons = $reasons -join '; '
	$result.Result = if ($reasons.Count -eq 0) { 'PASS' } else { 'FAIL' }
	$color = if ($result.Result -eq 'PASS') { 'Green' } else { 'Red' }
	Write-Host "  => $($result.Result) $($result.Reasons)" -ForegroundColor $color
	[pscustomobject]$result
}

# One game in VR at a time: a VR test holds the machine-wide lock for all its runs, or is skipped
$lock = $null
if ($Vr) {
	$lock = Enter-VrLock -What "smoke-test.ps1 -Vr in $ArxSourceDir" -TimeoutSec $VrLockTimeoutSec
	if (-not $lock) {
		Write-Host "smoke test: SKIPPED - the VR lock stayed busy for $VrLockTimeoutSec s ($(Get-VrLockHolder)); the VR self-test is best effort, say so in the PR" -ForegroundColor Yellow
		exit 2
	}
	Write-Host "VR lock: taken ($(Get-VrLockPath))"
}

$results = @()
try {
	for ($run = 1; $run -le $Repeat; $run++) {
		$results += Invoke-SmokeRun $run
		if ($run -lt $Repeat) { Start-Sleep -Seconds 2 }
	}
} finally {
	Exit-VrLock $lock
}

$passed = @($results | Where-Object Result -eq 'PASS').Count
if ($Repeat -gt 1) { $results | Format-Table -AutoSize | Out-String | Write-Host }
$summary = "smoke test: passed $passed/$Repeat (build $BuildDir; user dir $UserDir)"
if ($passed -eq $Repeat) { Write-Host $summary -ForegroundColor Green; exit 0 }
Write-Host $summary -ForegroundColor Red
exit 1
