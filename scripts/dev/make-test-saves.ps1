<#
.SYNOPSIS
	Produce the test save library (#70): the saves a worker loads to reach an MVP scenario point at once.

.DESCRIPTION
	One game run (flat, or on the simulated headset with -VrSim), driven through the test channel (docs/vr/TESTING.md): the scenario file
	(scripts/dev/test-saves/scenarios.txt) starts a new game from the main menu, puts the player at each
	scenario point with channel commands (setpos, lookat, script-console commands) and saves there with
	the channel command `save <name>`. Then the same game loads every save again and checks it (the
	level, the player where it was saved, the controls free, a screenshot). The saves are copied into
	the library, one folder per scenario:

	  <library>\<name>\gsave.sav     the save, loadable with `--loadslot 0` once it is the only save
	  <library>\manifest.json        name, area, description, build commit and date of every save

	Load one with `smoke-test.ps1 -Save <name>` or `run-windows.ps1 -Isolated -Save <name>`: the isolated
	user directory then holds exactly that save, so slot 0 is it (slots are numbered newest first).

	The library is NOT in the repository: saves are derived from the original game's data, and a save
	format change of the engine would make a committed save stale. Regenerating it takes one launch
	(about two minutes, most of it the scripted awakening of a new game); a save the script cannot
	produce (it needs real play, or a spot that cannot be reached by coordinates) is recorded by the
	owner in flat mode and copied into the library by hand.

	Scenario file lines: `#` comments; `@describe <text>` describes the next `save`; `@state
	<STATE> [timeout s]` waits for GAMEPLAY / MENU; `@level <n> [timeout s]` waits for gameplay in
	level n; `@wait <seconds>`; `@until <status path> <value> [timeout s]` waits until status.json
	has that value; `@print <status paths>` shows values from status.json; `@expect <status path>
	<value>` fails the run unless status.json has that value (e.g. `@expect area 1`); `@near <x> <z>
	<radius>` waits until the player stands there; `@entity <id> <text>...` fails unless the line of
	`entities <id>` holds every text (e.g. `show=6`, an equipped item); everything else is one channel
	command and must be acknowledged with errors=0.

	`-Verify` is the check that needs no generating: a FRESH game started with `--loadslot 0` (the way
	a worker or the owner starts a save) loads every save of the library one after the other (`loadslot
	<n>` through the channel) and checks each one. Run it after an engine change that may touch the
	save format. One launch for the whole library.

	Launch control (D-014, scripts/launch-lib.ps1): one launch (flat or --vr-sim), never SteamVR, the game lock,
	the working copy's own fresh build, one line in the ledger.

.PARAMETER Script
	The scenario file. Default: scripts\dev\test-saves\scenarios.txt.

.PARAMETER Library
	The library folder. Default: $env:ARXVR_SAVE_LIBRARY or %LOCALAPPDATA%\ArxFatalisVR\savelib.

.PARAMETER Verify
	Do not generate: load every save of the library in a fresh game and check it.

.PARAMETER VrSim
	Make (or -Verify) the saves on the simulated headset (#112, #210): arx.exe --vr-sim <script> instead of a flat
	game, so the saves are made in VR the way a player makes them. A bare name is looked up in scripts\dev\vrsim
	(e.g. idle, the headset at rest). Still one launch, never the runtime or SteamVR.

.PARAMETER Only
	Copy only these saves of the run into the library (every save is still made and loaded again): renew some
	saves without replacing the others, e.g. -Only mvp-lever,mvp-door,mvp-chest.

.PARAMETER DataDir
	Arx Fatalis install folder, passed as --data-dir (default: the game finds the Steam install itself; CI's
	service account has none, ci-smoke.ps1 passes $env:ARXVR_DATA_DIR).

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER Rebuild
	Build this working copy first when its build is stale (or missing).

.PARAMETER LockTimeoutSec
	How long to wait for the game lock (default 300).

.PARAMETER StepTimeoutSec
	How long one `@state` / `@level` wait may take (default 90).

.EXAMPLE
	.\scripts\dev\make-test-saves.ps1
.EXAMPLE
	.\scripts\dev\make-test-saves.ps1 -Verify
.EXAMPLE
	.\scripts\dev\make-test-saves.ps1 -VrSim idle
.EXAMPLE
	.\scripts\dev\smoke-test.ps1 -Save mvp-goblin-fight
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[string]$Script,
	[string]$Library,
	[string]$DataDir,
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[switch]$Rebuild,
	[switch]$Verify,
	[string]$VrSim,
	[string[]]$Only,
	[int]$LockTimeoutSec = 300,
	[int]$StepTimeoutSec = 90
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

if (-not $Script) { $Script = Join-Path $PSScriptRoot 'test-saves\scenarios.txt' }
if (-not $Library) { $Library = Get-ArxSaveLibrary }
if (-not $Verify -and -not (Test-Path $Script)) { throw "scenario file not found: $Script" }
if ($Verify -and -not (Test-Path (Join-Path $Library 'manifest.json'))) { throw "no manifest.json in $Library - run scripts\dev\make-test-saves.ps1 first" }
# The simulated headset (#112): a script path, or a name in scripts\dev\vrsim
$simScript = if ($VrSim) { Resolve-ArxSimScript $VrSim } else { $null }

$BuildDir = Get-DefaultBuildDir $ArxSourceDir $Config
$copy = Get-ArxWorkingCopyInfo $ArxSourceDir
$stamp = Assert-ArxFreshBuild -SourceDir $ArxSourceDir -BuildDir $BuildDir -Config $Config -Head $copy.Head -Rebuild:$Rebuild
$TestDir = Get-ArxTestDir
# Its own user directory, emptied for every run: the saves in it are exactly the ones this run made
$UserDir = Join-Path (Get-ArxAppDataDir) "userdirs\$(Get-WorkingCopyKey $ArxSourceDir)-savegen"
$log = Get-ArxLogPath $UserDir
$crashDir = Get-ArxCrashDir $UserDir

# --- Parse the scenario file -------------------------------------------------------------------------------
# The parser, the verification steps and the slot order are in scenario-lib.ps1: host-tested by
# scripts/dev/test-save-library.ps1 (run-script-tests.ps1, CI)
. (Join-Path $PSScriptRoot 'test-saves\scenario-lib.ps1')
$steps = [Collections.Generic.List[object]]::new()
$saveNames = @()
if (-not $Verify) {
	$scenario = Read-ArxScenario (Get-Content $Script) $Script
	foreach ($s in $scenario.Steps) { $steps.Add($s) }
	$saveNames = @($scenario.SaveNames)
	$unknown = @($Only | Where-Object { $_ -notin $saveNames })
	if ($unknown) { throw "-Only: no save $($unknown -join ', ') in $Script (it makes $($saveNames -join ', '))" }
}
if ($Only -and $Verify) { throw '-Only chooses which saves of a run go into the library; -Verify loads them all' }

# -Verify: slots are numbered newest first by the save file's time, which a copy keeps - the order is
# computed here. Slot 0 is loaded by the command line, the others through the channel; every save differs
# in position from the one before, so "the player is there" cannot be true before the load happened.
if ($Verify) {
	$entries = @(Get-Content -Raw (Join-Path $Library 'manifest.json') | ConvertFrom-Json)
	$ordered = @(Get-ArxVerifyOrder $entries $Library)
	if ($ordered.Count -eq 0) { throw "no save of manifest.json is in $Library" }
	$slots = @(0..($ordered.Count - 1) | ForEach-Object { if ($_ -eq 0) { -1 } else { $_ } })
	foreach ($s in (New-ArxVerifySteps $ordered $slots)) { $steps.Add($s) }
}

# --- The run -------------------------------------------------------------------------------------------------
if ($Verify) { Write-Host "make-test-saves -Verify: $($ordered.Count) save(s) of $Library, a fresh game with --loadslot 0" -ForegroundColor Cyan }
else { Write-Host "make-test-saves: $($saveNames.Count) save(s) from $Script -> $Library" -ForegroundColor Cyan }
Write-Host "game: $(if ($simScript) { "the simulated headset, $simScript" } else { 'flat' })"
Write-Host "build: $(ConvertTo-ShortSha $stamp) ($($copy.Branch));  user dir: $UserDir;  test channel: $TestDir"
Write-ArxLaunchBudgetNote -Copy $ArxSourceDir -Branch $copy.Branch -Launches 1

$lock = Enter-ArxGameLock -What 'make-test-saves.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -TimeoutSec $LockTimeoutSec
if (-not $lock) {
	Write-Host "make-test-saves: SKIPPED - another game kept the game lock for $LockTimeoutSec s (one game at a time, D-014)" -ForegroundColor Yellow
	foreach ($l in Get-ArxLockHolders) { Write-Host "  held: $l" -ForegroundColor Yellow }
	exit 2
}

$failures = [Collections.Generic.List[string]]::new()
$manifest = [Collections.Generic.List[object]]::new()
$process = $null
$launchTime = Get-Date
$script:pendingDescription = ''

function Get-StatusValue([string]$Path) {
	$actual = Read-ArxStatus $TestDir
	foreach ($part in $Path -split '\.') { $actual = if ($null -ne $actual) { $actual.$part } }
	$actual
}

function Invoke-Steps($stepList) {
	foreach ($step in $stepList) {
		if ($process.HasExited) { throw "the game exited with code $($process.ExitCode) before line $($step.Line)" }
		$where = if ($step.Line) { "line $($step.Line)" } else { 'verification' }
		switch ($step.Kind) {
			'describe' { $script:pendingDescription = $step.Text }
			'wait' { Start-Sleep -Seconds ([double]$step.Text) }
			'state' {
				$want, $timeout = $step.Text -split '\s+'
				$t = if ($timeout) { [int]$timeout } else { $StepTimeoutSec }
				$null = Wait-ArxStatus { param($s) $s.state -eq $want } -TimeoutSec $t -Dir $TestDir -What "state $want ($where)"
				Write-Host ('  +{0,5:0.0}s  state {1}' -f ((Get-Date) - $launchTime).TotalSeconds, $want)
			}
			'level' {
				$n, $timeout = $step.Text -split '\s+'
				$t = if ($timeout) { [int]$timeout } else { $StepTimeoutSec }
				$null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' -and [string]$s.area -eq $n } -TimeoutSec $t -Dir $TestDir -What "gameplay in level $n ($where)"
				Write-Host ('  +{0,5:0.0}s  gameplay in level {1}' -f ((Get-Date) - $launchTime).TotalSeconds, $n)
			}
			'expect' {
				$path, $value = $step.Text -split '\s+', 2
				$actual = Get-StatusValue $path
				if ("$actual" -ne $value) { $failures.Add("$where`: expected $path = $value, status.json has '$actual'") } else { Write-Host "  [ok]   $path = $value" -ForegroundColor Green }
			}
			'until' {
				$path, $value, $timeout = $step.Text -split '\s+'
				$t = if ($timeout) { [int]$timeout } else { $StepTimeoutSec }
				$null = Wait-ArxStatus {
					param($s)
					$actual = $s
					foreach ($part in $path -split '\.') { $actual = if ($null -ne $actual) { $actual.$part } }
					"$actual" -eq $value
				} -TimeoutSec $t -Dir $TestDir -What "$path = $value ($where)"
				Write-Host ('  +{0,5:0.0}s  {1} = {2}' -f ((Get-Date) - $launchTime).TotalSeconds, $path, $value)
			}
			'near' {
				# near <x> <z> <radius>: the player stands within radius of the ground position
				$nx, $nz, $radius = $step.Text -split '\s+' | ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) }
				$s = Wait-ArxStatus { param($s) $s.player -and ([math]::Sqrt([math]::Pow($s.player.x - $nx, 2) + [math]::Pow($s.player.z - $nz, 2)) -le $radius) } `
					-TimeoutSec $StepTimeoutSec -Dir $TestDir -What "the player within $radius of $nx $nz ($where)"
				Write-Host ('  [ok]   player at {0:0} {1:0} (within {2:0} of {3:0} {4:0})' -f $s.player.x, $s.player.z, $radius, $nx, $nz) -ForegroundColor Green
			}
			'entity' {
				# entity <id> <text>...: the line of `entities <id>` holds every text, e.g. show=6 for an equipped item
				$id, $wanted = $step.Text -split '\s+'
				$null = Send-ArxCommand -Lines @("entities $id") -Dir $TestDir -TimeoutSec 30
				$line = Get-Content (Join-Path $TestDir 'entities.txt') -ErrorAction SilentlyContinue | Where-Object { $_ -match "^$([regex]::Escape($id)) " } | Select-Object -First 1
				$missing = @($wanted | Where-Object { -not $line -or -not $line.Contains($_) })
				if ($missing) { $failures.Add("$where`: entity $id is '$line', expected $($missing -join ' ')") } else { Write-Host "  [ok]   $line" -ForegroundColor Green }
			}
			'print' {
				$values = foreach ($path in $step.Text -split '\s+') { "$path=$(Get-StatusValue $path)" }
				Write-Host ('  +{0,5:0.0}s  {1}' -f ((Get-Date) - $launchTime).TotalSeconds, ($values -join '  '))
			}
			'command' {
				$ack = Send-ArxCommand -Lines @($step.Text) -Dir $TestDir -TimeoutSec 30
				$ok = $ack -match 'errors=0 '
				Write-Host "  > $($step.Text)$(if (-not $ok) { "`n    $ack" })" -ForegroundColor $(if ($ok) { 'Gray' } else { 'Red' })
				if (-not $ok) { $failures.Add("$where`: '$($step.Text)' was refused: $ack") }
				if ($ok -and $step.Text -match '^save\s+(\S.*)$') {
					$status = Read-ArxStatus $TestDir
					$manifest.Add([pscustomobject][ordered]@{
						name = $Matches[1].Trim(); description = $script:pendingDescription; area = [int]$status.area
						player = $status.player; commit = ($stamp -split ' ')[0]; created = (Get-Date).ToString('s')
						made = if ($simScript) { "vr-sim $(Split-Path -LeafBase $simScript)" } else { 'flat' }
					})
					$script:pendingDescription = ''
				}
			}
			default { throw "$Script`:$($step.Line): unknown directive @$($step.Kind)" }
		}
	}
}

try {
	if (Test-Path $UserDir) { Remove-Item -Recurse -Force $UserDir }
	New-Item -ItemType Directory -Force $UserDir, $TestDir | Out-Null
	if ($Verify) { Copy-ArxSaves -Names @($ordered.name) -UserDir $UserDir -Library $Library }
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'entities.txt', 'stdout.txt', 'stderr.txt') {
		Remove-Item (Join-Path $TestDir $f) -Force -ErrorAction SilentlyContinue
	}

	$argList = @('--windowed', '--test-dir', "`"$TestDir`"", '--user-dir', "`"$UserDir`"", '--config-dir', "`"$UserDir`"", '--no-vr', '--mute')
	if ($DataDir) { $argList += '--data-dir', "`"$DataDir`"" }
	$launchArgs = @('--no-vr')
	# --vr-sim wins over --no-vr in the game; --no-vr stays, so a broken script means flat, never the runtime
	if ($simScript) { $argList += '--vr-sim', "`"$simScript`""; $launchArgs += '--vr-sim', (Split-Path -Leaf $simScript) }
	if ($Verify) { $argList += '--loadslot', '0'; $launchArgs += '--loadslot', '0' }
	Write-Host "=== arx.exe $($argList -join ' ')" -ForegroundColor Cyan
	$env:ARX_UNATTENDED = '1'
	$launchTime = Get-Date
	$previousHint = Set-ArxBackgroundWindow
	try {
		$process = Start-Process -FilePath (Join-Path $BuildDir 'arx.exe') -ArgumentList $argList -PassThru `
			-RedirectStandardOutput (Join-Path $TestDir 'stdout.txt') -RedirectStandardError (Join-Path $TestDir 'stderr.txt')
	} finally { Restore-ArxBackgroundWindow $previousHint }
	Set-ArxGameLockGame $lock $process.Id

	$firstState = if ($Verify) { 'GAMEPLAY' } else { 'MENU' }
	$null = Wait-ArxStatus { param($s) $s.state -eq $firstState } -TimeoutSec $StepTimeoutSec -Dir $TestDir -What "state $firstState"
	Write-Host ('  +{0,5:0.0}s  state {1}' -f ((Get-Date) - $launchTime).TotalSeconds, $firstState)

	Invoke-Steps $steps

	if (-not $Verify -and $failures.Count -eq 0) {
		# The same game loads every save again, oldest first: slot (n - 1) is the oldest, and every save
		# differs in position from the one loaded before (the newest, slot 0, is where the game stands now)
		Write-Host '  --- loading every save again in this game' -ForegroundColor Cyan
		$n = $manifest.Count
		$slots = @(($n - 1)..0)
		$inOrder = @($slots | ForEach-Object { $manifest[$n - 1 - $_] })
		Invoke-Steps (New-ArxVerifySteps $inOrder $slots)
	}
} catch {
	$failures.Add("$_")
} finally {
	if ($process -and -not $process.HasExited) {
		try { $null = Send-ArxCommand -Lines @('quit') -Dir $TestDir -TimeoutSec 10 } catch { $null = $process.CloseMainWindow() }
		if (-not $process.WaitForExit(60000)) { $failures.Add('the game did not exit within 60 s, killed'); $process.Kill() }
	}
	if ($process) {
		$process.WaitForExit()
		Add-ArxLaunchRecord -Script 'make-test-saves.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -Build $stamp -Vr $false -Sim ([bool]$simScript) -GamePid $process.Id `
			-ExitCode $process.ExitCode -DurationSec ((Get-Date) - $launchTime).TotalSeconds -ArxArgs $launchArgs -Time $launchTime
	}
	Exit-ArxGameLock $lock
}

# --- Evidence of the run ---------------------------------------------------------------------------------------
if ($process) {
	$logFresh = (Test-Path $log) -and ((Get-Item $log).LastWriteTime -ge $launchTime)
	$clean = $logFresh -and [bool](Select-String -Path $log -Pattern 'Clean shutdown' -Quiet)
	$errorLines = if ($logFresh) { @(Select-String -Path $log -Pattern '^\[[EC]\]' | ForEach-Object Line) } else { @() }
	$newCrashes = @(Get-ChildItem $crashDir -Directory -ErrorAction SilentlyContinue)
	if ($process.ExitCode -ne 0) { $failures.Add("exit code $($process.ExitCode)") }
	if (-not $clean) { $failures.Add("no 'Clean shutdown' in $log") }
	if ($errorLines) { $failures.Add("$($errorLines.Count) [E] line(s) in arx.log, first: $($errorLines[0])") }
	if ($newCrashes) { $failures.Add("crash report: $($newCrashes[0].FullName)") }
	try { & (Join-Path $PSScriptRoot 'log-digest.ps1') -UserDir $UserDir -Since $launchTime -Source 'make-test-saves.ps1' -ExitCode $process.ExitCode -DurationSec ((Get-Date) - $launchTime).TotalSeconds -Copy $ArxSourceDir }
	catch { Write-Warning "run digest failed: $($_.Exception.Message)" }
}

if ($Verify) {
	if ($failures.Count -eq 0) {
		Write-Host "make-test-saves -Verify: PASS - $($ordered.Count) save(s) loaded, slot 0 by --loadslot 0: $(($ordered.name) -join ', ')" -ForegroundColor Green
		exit 0
	}
	foreach ($f in $failures) { Write-Host "  [FAIL] $f" -ForegroundColor Red }
	Write-Host 'make-test-saves -Verify: FAIL' -ForegroundColor Red
	exit 1
}

# --- Into the library ------------------------------------------------------------------------------------------
$folders = @(Get-ChildItem (Join-Path $UserDir 'save') -Directory -ErrorAction SilentlyContinue | Sort-Object Name)
if ($manifest.Count -ne $folders.Count) {
	$failures.Add("$($manifest.Count) save command(s) acknowledged but $($folders.Count) save folder(s) in $UserDir\save")
}
if ($failures.Count -eq 0) {
	New-Item -ItemType Directory -Force $Library | Out-Null
	# The game numbers new save folders from the first free number, so the i-th save is the i-th folder
	for ($i = 0; $i -lt $manifest.Count; $i++) {
		if ($Only -and $manifest[$i].name -notin $Only) { continue }
		$target = Join-Path $Library $manifest[$i].name
		if (Test-Path $target) { Remove-Item -Recurse -Force $target }
		Copy-Item -Recurse $folders[$i].FullName $target
		$size = (Get-ChildItem $target -Recurse -File | Measure-Object Length -Sum).Sum
		Write-Host ("  save: {0,-22} area {1,-3} {2,6:0.0} MB  {3}" -f $manifest[$i].name, $manifest[$i].area, ($size / 1MB), $manifest[$i].description)
	}
	# Saves of earlier runs stay; the manifest lists what exists, the newest definition wins
	$manifestFile = Join-Path $Library 'manifest.json'
	$all = [ordered]@{}
	if (Test-Path $manifestFile) { foreach ($e in (Get-Content -Raw $manifestFile | ConvertFrom-Json)) { $all[$e.name] = $e } }
	$copied = @($manifest | Where-Object { -not $Only -or $_.name -in $Only })
	foreach ($e in $copied) { $all[$e.name] = $e }
	ConvertTo-Json -InputObject @($all.Values) -Depth 5 | Set-Content -Encoding utf8 $manifestFile
	Write-Host "make-test-saves: PASS - $($copied.Count) of $($manifest.Count) save(s) in $Library, each loaded again in the same game" -ForegroundColor Green
	exit 0
}
foreach ($f in $failures) { Write-Host "  [FAIL] $f" -ForegroundColor Red }
Write-Host 'make-test-saves: FAIL - nothing was copied into the library' -ForegroundColor Red
exit 1
