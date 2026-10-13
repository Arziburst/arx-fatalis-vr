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
	  - the game event counts in status.json (events.byName, #130): vr.mode=1, game.state>=2, -ExpectEvents
	Prints the state timeline, the [E]/[W] log lines, the tail of the log and a PASS / FAIL verdict
	per run. Exit code 0 when every run passed, 1 otherwise, 2 when the test was SKIPPED: -Vr while
	SteamVR is not running, or the game lock stayed busy for -LockTimeoutSec.

	The game runs in a window (--windowed) that is shown without taking the focus, and without the
	crash dialog (ARX_UNATTENDED=1), so a crash leaves a report in crashes\ and the test fails instead
	of waiting for a click.

	Isolation (docs/vr/WORKFLOW.md, "Parallel work"): the game gets this working copy's own user and
	config directory (--user-dir / --config-dir = %LOCALAPPDATA%\ArxFatalisVR\userdirs\<folder>-<hash>)
	and its own test channel directory, so parallel workers never share arx.log, cfg.ini, saves or
	status.json with each other or with the player's Saved Games\Arx Libertatis (-Shared opts out).
	-Saves copies save slots into the isolated directory for -- --loadslot runs.

	Launch control (decision D-014, scripts/launch-lib.ps1): the test never starts SteamVR - a -Vr test
	is SKIPPED (exit 2) unless vrserver.exe already runs; it holds the machine-wide game lock for all
	its runs (one game at a time on this PC; a -Vr test also the VR lock), waiting up to
	-LockTimeoutSec for the holder named in the output; it runs only this working copy's own build made
	from its HEAD (-Rebuild builds first); every run is one line in %LOCALAPPDATA%\ArxFatalisVR\launches.log.

.PARAMETER Sound
	Keep the game's sound. Without it every run gets --mute (#345): CI, workers and sequences stay silent.

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER BuildDir
	Build directory containing arx.exe. Default: the one build-windows.ps1 uses for this working copy.

.PARAMETER DataDir
	Arx Fatalis install folder, passed as --data-dir. Default: none - the game finds the install in the
	Steam libraries itself (#79), and the test checks the "Steam data:" line of arx.log.

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

.PARAMETER Save
	A save of the test save library (#70, scripts\dev\make-test-saves.ps1), e.g. mvp-goblin-fight: it becomes
	the ONLY save of the isolated user directory (slots are numbered newest first, so a given save has no
	stable slot among several), and `--loadslot 0` is the default arx.exe argument.

.PARAMETER SaveLibrary
	Folder holding the saves for -Saves / -Save (the test save library, #70). Default: $env:ARXVR_SAVE_LIBRARY
	or %LOCALAPPDATA%\ArxFatalisVR\savelib.

.PARAMETER LockTimeoutSec
	How long to wait for the game lock (and with -Vr the VR lock) before skipping the test (default 300).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (or missing).

.PARAMETER Vr
	Start in VR (no --no-vr). SteamVR must already be running - the test never starts it (D-014) and is
	SKIPPED otherwise; the result records the OpenXR session state.

.PARAMETER VrSim
	Run in VR on the simulated headset (#112): arx.exe --vr-sim <script>, no runtime and no SteamVR, so it
	runs any time (still one game at a time). A bare name is looked up in scripts\dev\vrsim (e.g. basic,
	eye-check). The test waits for the session to be focused, lets the script's timeline finish before it
	quits (up to -Seconds plus 60 s) and prints every shot's verdicts (docs/vr/TESTING.md).

.PARAMETER Menu
	A plain launch that expects the main menu (no --loadlevel 10 default); combine with -Commands 'loadslot 0'.

.PARAMETER Playground
	Start the test playground (#321, scripts\dev\playground): generates the level when it is missing or older than
	the generator, starts a new game in it (--data-dir <level folder> --loadlevel 30) and fails unless the game reaches
	GAMEPLAY in the playground area.

.PARAMETER Seconds
	How long the game stays in the expected state before it is quit (default 10).

.PARAMETER Repeat
	Run the whole thing N times and print a table (default 1).

.PARAMETER StartTimeoutSec
	How long to wait for the expected state after the launch (default 90).

.PARAMETER Commands
	Channel commands to send once the game is in the expected state, e.g. 'shot start', 'mark hello',
	'player.setplayerpos 0 0 0' (see docs/vr/TESTING.md). Each must be acknowledged without an error.

.PARAMETER Sequence
	A scripted sequence (#113, scripts\dev\xrs\*.xrs: a name or a path) run on the game once it is in the expected
	state, after -Commands: channel commands with asserts on status.json and arx.log, shots and their comparison
	(docs/vr/TESTING.md, "Scripted sequences"). A failed step fails the run. The sequence is parsed before the game
	starts. With a sequence -Seconds defaults to 0: the sequence decides how long the game runs.

.PARAMETER ExpectEvents
	Game event counts the final status.json must report (events.byName, #130), each 'name=N' (exactly N)
	or 'name>=N' (at least N), e.g. 'spell.cast=1','ui.book=2','game.state>=3'. Every run also checks the
	event log itself: vr.mode=1 and game.state>=2 (a start and EXITED), whatever -ExpectEvents says.

.PARAMETER CloseWindow
	Quit with WM_CLOSE (what a player does) instead of the channel's quit command.

.PARAMETER EvidenceDir
	Keep each FAILED run's evidence in <EvidenceDir>\runNN-<time>\ before the next run replaces it (#146): arx.log,
	stdout.txt, stderr.txt, status.json, the run's new crash reports, reasons.txt and, with -Vr, the SteamVR logs
	(Steam\logs\vrserver*, vrcompositor*, vrmonitor*, vrclient_arx*, xrclient_arx*) written during the run.

.EXAMPLE
	.\scripts\dev\smoke-test.ps1
	# flat, --loadlevel 10 (the default when no arguments for arx.exe are given)
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Vr -Repeat 10 -Seconds 3 -EvidenceDir $env:LOCALAPPDATA\ArxFatalisVR\evidence\146
	# the #146 hunt, only while the owner has SteamVR running: every failed run keeps its logs
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Vr -- --loadslot 0
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -VrSim basic -Commands 'vr-sim head rot 30 0 0','vr-sim shot turned'
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Commands 'shot start','mark after-shot' -- --loadlevel 11
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Save mvp-goblin-fight
	# a scenario of the test save library: the only save of the isolated directory, loaded with --loadslot 0
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
	[string]$Save,
	[string]$SaveLibrary,
	[Alias('VrLockTimeoutSec')]
	[int]$LockTimeoutSec = 300,
	[switch]$Rebuild,
	[switch]$Vr,
	[switch]$Sound,
	[string]$VrSim,
	[switch]$Menu,
	[switch]$Playground,
	[int]$Seconds = 10,
	[int]$Repeat = 1,
	[int]$StartTimeoutSec = 90,
	[string[]]$Commands = @(),
	[string]$Sequence,
	[string[]]$ExpectEvents = @(),
	[switch]$CloseWindow,
	[string]$EvidenceDir,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')

# A sequence with a syntax error costs no launch
$sequenceToRun = if ($Sequence) { Read-ArxSequence $Sequence } else { $null }

# Game event counts to assert (#130): the event log's own baseline first, then -ExpectEvents
$eventChecks = @(
	foreach ($spec in @('vr.mode=1', 'game.state>=2') + $ExpectEvents) {
		if ($spec -notmatch '^\s*([\w.]+)\s*(>=|=)\s*(\d+)\s*$') { throw "-ExpectEvents: expected 'name=N' or 'name>=N', got '$spec'" }
		[pscustomobject]@{ Name = $Matches[1]; AtLeast = ($Matches[2] -eq '>='); Count = [int]$Matches[3] }
	}
)

if (-not $BuildDir) { $BuildDir = Get-DefaultBuildDir $ArxSourceDir $Config }
# Only this working copy's own fresh build (D-014)
$copy = Get-ArxWorkingCopyInfo $ArxSourceDir
$stamp = Assert-ArxFreshBuild -SourceDir $ArxSourceDir -BuildDir $BuildDir -Config $Config -Head $copy.Head -Rebuild:$Rebuild
$exe = Join-Path $BuildDir 'arx.exe'
if (-not $TestDir) { $TestDir = Get-ArxTestDir }
if ($UserDir -and $Shared) { throw '-UserDir and -Shared exclude each other' }
if (-not $UserDir) { $UserDir = Get-ArxUserDir -Shared:$Shared }
if ($Shared -and ($Saves -or $Save)) { throw '-Saves / -Save copy saves into the isolated user directory; they cannot be combined with -Shared' }
if ($Save -and $Saves) { throw '-Save and -Saves exclude each other' }
# The simulated headset (#112): a script path, or a name in scripts\dev\vrsim
$simScript = $null
if ($VrSim) {
	if ($Vr) { throw '-Vr and -VrSim exclude each other: the simulator replaces the runtime' }
	$simScript = Resolve-ArxSimScript $VrSim
}
$sim = [bool]$simScript
# -Save <name>: that library save is the only save of the isolated user directory, so slot 0 is it
if ($Save -and -not $ArxArgs) { $ArxArgs = @('--loadslot', '0') }
if ($Playground) {
	# The test playground (#321): the generated level's folder as a data dir, a new game in its area
	if ($Menu -or $Save -or $Saves) { throw '-Playground starts a new game in the playground: it excludes -Menu, -Save and -Saves' }
	. (Join-Path $PSScriptRoot 'playground\playground-lib.ps1')
	$ArxArgs = @(Get-ArxPlaygroundArgs (Initialize-ArxPlaygroundLevel)) + @($ArxArgs)
}
if (-not $ArxArgs -and -not $Menu) { $ArxArgs = @('--loadlevel', '10') }
if (-not $ArxArgs) { $ArxArgs = @() }

$loadsSomething = ($ArxArgs -join ' ') -match '--load(level|slot|save)'
$expectedStates = if ($loadsSomething) { @('GAMEPLAY', 'CINEMATIC') } else { @('MENU') }

$log = Get-ArxLogPath $UserDir
$crashDir = Get-ArxCrashDir $UserDir
if (-not $Shared) {
	New-Item -ItemType Directory -Force $UserDir | Out-Null
	if ($Saves) { Copy-ArxSaves -Names $Saves -UserDir $UserDir -Library $SaveLibrary }
	if ($Save) { Use-ArxSave -Name $Save -UserDir $UserDir -Library $SaveLibrary }
}
Write-Host "build: $(ConvertTo-ShortSha $stamp) ($($copy.Branch));  user dir: $UserDir ($(if ($Shared) { 'shared' } else { 'isolated' }));  test channel: $TestDir"

function Write-Check([bool]$Ok, [string]$Text) {
	if ($Ok) { Write-Host "  [ok]   $Text" -ForegroundColor Green } else { Write-Host "  [FAIL] $Text" -ForegroundColor Red }
}

# A failed run's evidence (-EvidenceDir, #146), copied before the next run replaces it: the game's log, stdout,
# stderr and status, its new crash reports and, with -Vr, the SteamVR logs written during the run
function Save-SmokeEvidence([int]$Run, [DateTime]$Since, [string[]]$Crashes, [string]$Why) {
	$dir = Join-Path $EvidenceDir ('run{0:00}-{1:yyyyMMdd-HHmmss}' -f $Run, $Since)
	New-Item -ItemType Directory -Force $dir | Out-Null
	foreach ($file in @($log, (Join-Path $TestDir 'stdout.txt'), (Join-Path $TestDir 'stderr.txt'), (Join-Path $TestDir 'status.json'))) {
		if (Test-Path -LiteralPath $file) { Copy-Item -LiteralPath $file -Destination $dir }
	}
	foreach ($c in $Crashes) { Copy-Item -LiteralPath (Join-Path $crashDir $c) -Destination $dir -Recurse }
	$steam = if ($Vr) { Get-SteamPath } else { $null }
	if ($steam) {
		Get-ChildItem (Join-Path $steam 'logs') -File -ErrorAction SilentlyContinue |
			Where-Object { $_.Name -match '^(vrserver|vrcompositor|vrmonitor|vrclient_arx|xrclient_arx)' -and $_.LastWriteTime -ge $Since } |
			Copy-Item -Destination $dir
	}
	Set-Content -LiteralPath (Join-Path $dir 'reasons.txt') -Value $Why -Encoding utf8
	Write-Host "  evidence kept in $dir"
}

function Invoke-SmokeRun([int]$Run) {
	$reasons = [Collections.Generic.List[string]]::new()
	$result = [ordered]@{ Run = $Run; Result = 'FAIL'; Exit = $null; State = '-'; Frames = 0; Fps = 0; VR = '-'; Errors = '-'; Warnings = '-'; Crash = '-'; DrawCheck = '-'; Reasons = '' }
	$process = $null
	$launchTime = Get-Date
	try {
		New-Item -ItemType Directory -Force $TestDir | Out-Null
		foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'launched.txt') {
			Remove-Item (Join-Path $TestDir $f) -Force -ErrorAction SilentlyContinue
		}
		# arx.log is not deleted here: the game rotates the previous run to arx.1.log itself (#129), and
		# deleting it would throw that history away. "Fresh" is judged by LastWriteTime >= launch time.
		$crashesBefore = @(Get-ChildItem $crashDir -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)

		# Start-Process does not quote arguments: paths with spaces need explicit quotes
		$argList = @()
		if ($DataDir) { $argList += '--data-dir', "`"$DataDir`"" }
		$argList += '--windowed', '--test-dir', "`"$TestDir`""
		if (-not $Shared) { $argList += '--user-dir', "`"$UserDir`"", '--config-dir', "`"$UserDir`"" }
		if (-not $Vr) { $argList += '--no-vr' }
		if (-not $Sound) { $argList += '--mute' }
		# --vr-sim wins over --no-vr in the game; --no-vr stays, so a broken script means flat, never the runtime
		if ($sim) { $argList += '--vr-sim', "`"$simScript`"" }
		$argList += $ArxArgs
		Write-Host "=== run $Run/$Repeat`: arx.exe $($argList -join ' ')" -ForegroundColor Cyan
		$env:ARX_UNATTENDED = '1'
		$launchTime = Get-Date
		# Shown without taking the focus (SDL hint from the environment, restored right after the start)
		$previousHint = Set-ArxBackgroundWindow
		try {
			$process = Start-Process -FilePath $exe -ArgumentList $argList -PassThru `
				-RedirectStandardOutput (Join-Path $TestDir 'stdout.txt') -RedirectStandardError (Join-Path $TestDir 'stderr.txt')
		} finally { Restore-ArxBackgroundWindow $previousHint }
		Set-ArxGameLockGame $lock $process.Id
		# The game is running: a check driving it from outside starts its timeouts only now, not during the lock wait (#258)
		Set-Content -LiteralPath (Get-ArxLaunchMarker $TestDir) -Value "pid=$($process.Id) run=$Run" -Encoding ascii

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
			# Evidence for the no-focus rule (D-014): the window of a self-test must not take the focus
			if ($status.window) { Write-Host "  window: focused=$($status.window.focused) minimized=$($status.window.minimized) $($status.window.width)x$($status.window.height)" }
			if ($Playground) {
				$inPlayground = [int]$status.area -eq $script:ArxPlaygroundArea
				Write-Check $inPlayground "playground: the game is in area $($status.area) (expected $script:ArxPlaygroundArea) in state $($status.state)"
				if (-not $inPlayground) { $reasons.Add("-Playground: the game is in area $($status.area), not the playground area $script:ArxPlaygroundArea") }
				elseif ($status.state -ne 'GAMEPLAY') { $reasons.Add("-Playground: the game is in state $($status.state), not GAMEPLAY") }
			}
			if ($Vr) {
				# Give the runtime a moment to move the session past idle; frames follow once it is ready (#18)
				try { $status = Wait-ArxStatus { param($s) $s.vr.sessionState -ne 'idle' } -TimeoutSec 10 -Dir $TestDir -What 'a session state' } catch { }
				try { $status = Wait-ArxStatus { param($s) [int64]$s.vr.frames -gt 0 } -TimeoutSec 10 -Dir $TestDir -What 'a submitted VR frame' } catch { }
				$status = Read-ArxStatus $TestDir
				$result.VR = "$($status.vr.enabled)/$($status.vr.sessionState)/$($status.vr.frames)"
				Write-Host "  VR: enabled=$($status.vr.enabled) session=$($status.vr.sessionState) frames=$($status.vr.frames) shown=$($status.vr.shownFrames)"
				if (-not $status.vr.enabled) { $reasons.Add('VR requested but the game runs flat (see the log for the reason)') }
			}
			if ($sim) {
				# The simulated headset is focused at once; frames follow within a frame or two
				try { $status = Wait-ArxStatus { param($s) $s.vr.sessionState -eq 'focused' -and [int64]$s.vr.frames -gt 0 } -TimeoutSec 15 -Dir $TestDir -What 'a focused simulated session' } catch { }
				$status = Read-ArxStatus $TestDir
				$result.VR = "sim/$($status.vr.sessionState)/$($status.vr.frames)"
				Write-Host "  VR (simulated): enabled=$($status.vr.enabled) session=$($status.vr.sessionState) frames=$($status.vr.frames) runtime=$($status.vr.runtime) timeline=$($status.vr.sim.timeline)"
				if (-not $status.vr.enabled -or -not $status.vr.sim) { $reasons.Add('--vr-sim given but the game runs flat (see the log: a script error names its line)') }
				elseif ($status.vr.sessionState -ne 'focused') { $reasons.Add("the simulated session is not focused (state $($status.vr.sessionState))") }
			}

			foreach ($command in $Commands) {
				$ack = Send-ArxCommand -Lines @($command) -Dir $TestDir
				Write-Host "  > $command`n    $ack"
				if ($ack -notmatch 'errors=0 ') { $reasons.Add("command '$command' failed: $ack") }
			}

			if ($sequenceToRun) {
				$sequenceResult = Invoke-ArxSequence $sequenceToRun -TestDir $TestDir -LogPath $log
				foreach ($failure in $sequenceResult.Failures) { $reasons.Add("sequence $($sequenceResult.Name): $failure") }
				Write-Check $sequenceResult.Passed "sequence $($sequenceResult.Name): $($sequenceResult.Ran) of $($sequenceResult.Steps) steps run, $($sequenceResult.Failures.Count) failed, $($sequenceResult.Pending.Count) pending"
				foreach ($p in $sequenceResult.Pending) { Write-Host "         pending $p" -ForegroundColor Yellow }
			}
			Start-Sleep -Seconds $(if ($sequenceToRun -and -not $PSBoundParameters.ContainsKey('Seconds')) { 0 } else { $Seconds })
			$status = Read-ArxStatus $TestDir
			if ($sim -and $status -and $status.vr.sim -and -not $process.HasExited) {
				# The script's timeline and its shots finish before the quit
				try {
					$status = Wait-ArxStatus { param($s) $s.vr.sim -and [int]$s.vr.sim.pending -eq 0 -and [int]$s.vr.sim.pendingShots -eq 0 } -TimeoutSec 60 -Dir $TestDir -What 'the simulator timeline to finish'
				} catch { $reasons.Add("$_ (timeline $($status.vr.sim.timeline), $($status.vr.sim.pending) lines pending)") }
			}
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
			if ($Vr) { $result.VR = "$($final.vr.enabled)/$($final.vr.sessionState)/$($final.vr.frames)" }
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
		if (-not $DataDir) {
			# The game found the data in the Steam libraries itself (#79)
			$steamLine = if ($logFresh) { Select-String -Path $log -Pattern 'Steam data: ' | Select-Object -First 1 -ExpandProperty Line } else { $null }
			$steamFound = [bool]$steamLine -and ($steamLine -notmatch 'Steam data: (Arx Fatalis .* not found|not searched)')
			$steamText = if ($steamLine) { $steamLine -replace '^.*Steam data: ', '' } else { 'no "Steam data:" line' }
			Write-Check $steamFound "data found in the Steam libraries: $steamText"
			if ($logFresh -and -not $steamFound) { $reasons.Add("Arx Fatalis data not found in the Steam libraries: $steamText") }
		}
		Write-Check ($newCrashes.Count -eq 0) "no new crash report ($($crashesAfter.Count) in $crashDir)"
		Write-Check ($errorLines.Count -eq 0) "no [E] line in arx.log ($($errorLines.Count) found, $($warningLines.Count) warnings)"
		Write-Check ($final -and [int]$final.errors -eq 0) "status.json errors == 0 (errors=$(if ($final) { $final.errors } else { '?' }), cleanShutdown=$(if ($final) { $final.cleanShutdown } else { '?' }))"
		# The game event log (#130): counts from status.json events.byName, never from reading the log
		$byName = if ($final -and $final.events) { $final.events.byName } else { $null }
		$failedEvents = @(foreach ($check in $eventChecks) {
			$have = if ($byName -and $byName.PSObject.Properties[$check.Name]) { [int]$byName.($check.Name) } else { 0 }
			$ok = if ($check.AtLeast) { $have -ge $check.Count } else { $have -eq $check.Count }
			if (-not $ok) { "$($check.Name)=$have (expected $(if ($check.AtLeast) { '>=' } else { '' })$($check.Count))" }
		})
		$eventsText = ($eventChecks | ForEach-Object { "$($_.Name)$(if ($_.AtLeast) { '>=' } else { '=' })$($_.Count)" }) -join ', '
		Write-Check ($byName -and $failedEvents.Count -eq 0) "game events as expected ($eventsText; total=$(if ($final -and $final.events) { $final.events.total } else { '?' }))"
		if (-not $byName) { $reasons.Add('status.json has no events (game event log, #130)') }
		elseif ($failedEvents) { $reasons.Add("game events: $($failedEvents -join ', ')") }
		if ($sim -and $final -and $final.vr.sim) {
			$c = $final.vr.sim.compositor
			$paired = [int64]$c.pairedFrames -eq [int64]$c.projectionFrames
			Write-Check ([int64]$c.projectionFrames -gt 0 -and $paired) "simulated compositor: $($c.projectionFrames) frames with both eyes, $($c.pairedFrames) paired, $($c.staleFrames) with a stale eye, $($c.quadFrames) with the flat frame quad"
			if ([int64]$c.projectionFrames -eq 0) { $reasons.Add('the simulated compositor got no frame with both eyes') }
			elseif (-not $paired) { $reasons.Add("$($c.staleFrames) frames showed eye images of different frames (a stale eye)") }
			foreach ($p in $final.vr.sim.shots.PSObject.Properties) {
				$shot = $p.Value
				Write-Host ("  shot {0} (frame {1}): left {2}, right {3}, flat frame {4}; L/R diff {5}, shift {6}{7}" -f $p.Name, $shot.frame,
					$shot.left.scene.verdict, $shot.right.scene.verdict, $(if ($shot.quad.shown) { $shot.quad.stats.verdict } else { 'not shown' }),
					$shot.lr.diff, $shot.lr.shift, $(if ($shot.swapEyes) { ' (swap-eyes fault)' } else { '' }))
			}
			Write-Host "  shots saved in $(Join-Path $TestDir 'capture')"
		}
		if (($Vr -or $sim) -and $final) {
			# The session runs and submits frames even with nobody in the headset (synchronized, never focused)
			Write-Check ([int64]$final.vr.frames -gt 0) "VR frames submitted (frames=$($final.vr.frames), shown=$($final.vr.shownFrames), session=$($final.vr.sessionState))"
			if ($final.vr.enabled -and [int64]$final.vr.frames -eq 0) { $reasons.Add('no VR frame was submitted (the session never ran, or xrWaitFrame / xrEndFrame failed - see the log)') }
			# The rate / cadence verdict of the frame loop (#20, docs/vr/TESTING.md): printed, not a gate until measured with the headset
			if ($final.vr.pacing) {
				$p = $final.vr.pacing
				Write-Host "  VR pacing: $($p.verdict) / $($p.cadence) cadence - $($p.framesPerSecond)/s on $($p.slotsPerSecond) slots/s, $($p.slotsPerFrame) slots/frame (sd $($p.cadenceSdMs) ms), wait+end $($p.budgetMeanMs) mean / $($p.budgetMaxMs) max ms, worst frame $($p.worstFrameMs) ms, worst run $($p.worstRunFrames) frames; $($p.reports) reports, gaps=$($final.vr.frameGaps) resubmitted=$($final.vr.resubmittedFrames) discarded=$($final.vr.discardedFrames)"
			} elseif ($final.vr.enabled) {
				Write-Host "  VR pacing: no report (the session ran shorter than one 3 s window and fewer than 2 frames)"
			}
		}
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

	# The launch ledger (D-014): one line per run
	if ($process) {
		Add-ArxLaunchRecord -Script 'smoke-test.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -Build $stamp -Vr $Vr -Sim $sim -GamePid $process.Id `
			-ExitCode $(if ($process.HasExited) { $process.ExitCode } else { $null }) -DurationSec ((Get-Date) - $launchTime).TotalSeconds -ArxArgs $ArxArgs -Time $launchTime
		# The run digest (#131): local and quick; the queued GitHub reports are sent once after the game lock is released
		try {
			& (Join-Path $PSScriptRoot 'log-digest.ps1') -UserDir $UserDir -Since $launchTime -Source 'smoke-test.ps1' `
				-ExitCode $(if ($process.HasExited) { $process.ExitCode } else { $null }) -DurationSec ((Get-Date) - $launchTime).TotalSeconds -Copy $ArxSourceDir
		} catch {
			Write-Warning "run digest failed (the verdict is unaffected): $($_.Exception.Message)"
		}
	}

	$result.Reasons = $reasons -join '; '
	$result.Result = if ($reasons.Count -eq 0) { 'PASS' } else { 'FAIL' }
	if ($EvidenceDir -and $reasons.Count -gt 0 -and $launchTime) {
		try { Save-SmokeEvidence -Run $Run -Since $launchTime -Crashes $newCrashes -Why $result.Reasons }
		catch { Write-Warning "keeping the evidence failed (the verdict is unaffected): $($_.Exception.Message)" }
	}
	$color = if ($result.Result -eq 'PASS') { 'Green' } else { 'Red' }
	Write-Host "  => $($result.Result) $($result.Reasons)" -ForegroundColor $color
	[pscustomobject]$result
}

# No SteamVR start without the owner (D-014): a VR test needs SteamVR already running, or is skipped
if ($Vr -and -not (Test-SteamVrRunning)) {
	Write-Host 'smoke test: SKIPPED - -Vr given but SteamVR (vrserver.exe) is not running, and a worker never starts it (D-014); say so in the PR and run the flat test instead' -ForegroundColor Yellow
	exit 2
}
Write-ArxLaunchBudgetNote -Copy $ArxSourceDir -Branch $copy.Branch -Launches $Repeat

# One game at a time on this PC: the test holds a game lock slot (and for -Vr the VR lock) for all its runs, or is skipped
$lock = Enter-ArxGameLock -What "smoke-test.ps1$(if ($Vr) { ' -Vr' })" -Copy $ArxSourceDir -Branch $copy.Branch -Vr:$Vr -TimeoutSec $LockTimeoutSec
if (-not $lock) {
	Write-Host "smoke test: SKIPPED - another game kept the game lock for $LockTimeoutSec s (one game at a time, D-014); run the test again when it is free:" -ForegroundColor Yellow
	foreach ($line in Get-ArxLockHolders) { Write-Host "  held: $line" -ForegroundColor Yellow }
	exit 2
}
Write-Host "game lock: slot $($lock.Slot)$(if ($lock.Vr) { ' + VR lock' }) ($(Get-ArxLockDir))"

$results = @()
try {
	for ($run = 1; $run -le $Repeat; $run++) {
		$results += Invoke-SmokeRun $run
		if ($run -lt $Repeat) { Start-Sleep -Seconds 2 }
	}
} finally {
	Exit-ArxGameLock $lock
}

# Send what the digests of these runs queued (bug filing of #131; REST only, soft failure, no wait for the owner)
try { & (Join-Path $PSScriptRoot 'log-digest.ps1') -Flush } catch { Write-Warning "sending the digest reports failed: $($_.Exception.Message)" }

$passed = @($results | Where-Object Result -eq 'PASS').Count
if ($Repeat -gt 1) { $results | Format-Table -AutoSize | Out-String | Write-Host }
$summary = "smoke test: passed $passed/$Repeat (build $BuildDir; user dir $UserDir)"
if ($passed -eq $Repeat) { Write-Host $summary -ForegroundColor Green; exit 0 }
Write-Host $summary -ForegroundColor Red
exit 1
