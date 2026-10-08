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
	  - no new crash report appeared in %USERPROFILE%\Saved Games\Arx Libertatis\crashes
	  - arx.log has no [E] line and status.json reports errors == 0
	  - every -Commands line was acknowledged without an error
	Prints the state timeline, the [E]/[W] log lines, the tail of the log and a PASS / FAIL verdict
	per run. Exit code 0 when every run passed, 1 otherwise.

	The game runs in a window (--windowed) and without the crash dialog (ARX_UNATTENDED=1), so a
	crash leaves a report in crashes\ and the test fails instead of waiting for a click.

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder. Default: found automatically in the Steam libraries.

.PARAMETER TestDir
	The test channel directory. Default: $env:ARXVR_TEST_DIR or %LOCALAPPDATA%\ArxFatalisVR\test.

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
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$BuildDir,
	[string]$DataDir,
	[string]$TestDir,
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

if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) $Config }
$exe = Join-Path $BuildDir 'arx.exe'
if (-not (Test-Path $exe)) { throw "$exe not found. Build first: .\scripts\build-windows.ps1 -Config $Config" }
if (-not $DataDir) { $DataDir = Find-ArxFatalisSteamInstall }
if (-not $DataDir) { throw 'Arx Fatalis (Steam app 1700) was not found in any Steam library. Install it, or pass -DataDir.' }
if (-not $TestDir) { $TestDir = Get-ArxTestDir }
if (-not $ArxArgs -and -not $Menu) { $ArxArgs = @('--loadlevel', '10') }
if (-not $ArxArgs) { $ArxArgs = @() }

$loadsSomething = ($ArxArgs -join ' ') -match '--load(level|slot|save)'
$expectedStates = if ($loadsSomething) { @('GAMEPLAY', 'CINEMATIC') } else { @('MENU') }

$log = Get-ArxLogPath
$crashDir = Get-ArxCrashDir

function Write-Check([bool]$Ok, [string]$Text) {
	if ($Ok) { Write-Host "  [ok]   $Text" -ForegroundColor Green } else { Write-Host "  [FAIL] $Text" -ForegroundColor Red }
}

function Invoke-SmokeRun([int]$Run) {
	$reasons = [Collections.Generic.List[string]]::new()
	$result = [ordered]@{ Run = $Run; Result = 'FAIL'; Exit = $null; State = '-'; Frames = 0; Fps = 0; VR = '-'; Errors = '-'; Warnings = '-'; Crash = '-'; Reasons = '' }
	$process = $null
	try {
		$running = @(Get-Process arx -ErrorAction SilentlyContinue)
		if ($running) { throw "another arx.exe is already running (pid $($running.Id -join ', ')) - close it first" }
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

$results = @()
for ($run = 1; $run -le $Repeat; $run++) {
	$results += Invoke-SmokeRun $run
	if ($run -lt $Repeat) { Start-Sleep -Seconds 2 }
}

$passed = @($results | Where-Object Result -eq 'PASS').Count
if ($Repeat -gt 1) { $results | Format-Table -AutoSize | Out-String | Write-Host }
$summary = "smoke test: passed $passed/$Repeat (build $BuildDir)"
if ($passed -eq $Repeat) { Write-Host $summary -ForegroundColor Green; exit 0 }
Write-Host $summary -ForegroundColor Red
exit 1
