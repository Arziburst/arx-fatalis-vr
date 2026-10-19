<#
.SYNOPSIS
	Replay a recorded VR session (#550): launch the simulated headset from the clip's save and stream the clip
	into it; print the drift and the input events.

.DESCRIPTION
	A clip is a recorded VR session in the .vrsim dialect (tests/vr/ReplayClipTest.h has the format):
	  <user dir>\arx-replay.log            the session being recorded (arx-replay.log.gz once it ended)
	  <user dir>\arx-replay.<n>.log[.gz]   older sessions (rotated like arx-perf.log)
	  <user dir>\replay-marks\*.vrsim      the last seconds before a `replay mark <name>` (the channel)
	  <user dir>\crashes|hangs\<utc>\arx-replay.vrsim   the last 60 s before a crash or hang report

	The replay (smoke-test.ps1 -VrSim, this working copy's isolated user directory, D-014's game lock):
	  - the save the clip names in "# save:" (made at the record start) becomes the only save of the isolated user
	    directory, and the game starts with --loadslot 0 --seed <the clip's seed>;
	  - the simulator script sets the clip's rate (or -Rate) and plays the clip from the timeline's frame 0 - the first
	    playable frame, the moment the record started - with `play <file> from <From> to <To>`; the clip is copied
	    (decompressed) under the test channel directory first, so the game's own rotation never touches it;
	  - at each -Shots time (ms on the clip's clock) a shot is taken (capture\replay-<ms>-left.bmp, -right.bmp, -quad.bmp);
	  - when vr.sim.play.done it prints "Drift: current <m> m, max <m> m" (vr.sim.play.drift: metres between the recorded
	    and the replayed player) and "Input events (<n>):" with one indented line per "[vr] input: <event> (<binding>)"
	    and "<hand> grip down|up" of the run's arx.log, in order.
	A mark's or a report's clip starts in the middle of its session: the replay is approximate - the player is placed
	where the clip's first frame has him (the channel's setpos) and the inputs go on from there.

	Exit code 0 when the clip played to its end (done, no error), 1 otherwise, 2 when the game lock stayed busy.

.PARAMETER Clip
	The clip: .log, .log.gz or .vrsim.

.PARAMETER From
	The window's start on the clip's clock, ms (default: the clip's first frame).

.PARAMETER To
	The window's end, ms (default: the clip's last frame).

.PARAMETER Save
	The save to start from: a save folder (a path holding gsave.sav) or a name of the test save library. Without it the
	clip's save folder is looked up next to the clip (<clip folder>\save\), in the clip's user directory (a mark's
	clip: ..\save\, a report's: ..\..\save\), then in the player's own saves.

.PARAMETER Rate
	The display rate of the replay, Hz (default: the clip's).

.PARAMETER Shots
	Clip times in ms to take a shot at (each inside the window).

.PARAMETER Plan
	Print what would be launched (the clip, the save, the seed, the rate, the window, the arguments, the simulator
	script) and exit 0 without launching. Nothing it prints holds a user path or the account name.

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.EXAMPLE
	.\scripts\dev\replay.ps1 "$env:LOCALAPPDATA\ArxFatalisVR\userdirs\<key>\arx-replay.log.gz"
.EXAMPLE
	.\scripts\dev\replay.ps1 <clip> -From 20000 -To 50000 -Shots 25000,40000
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory, Position = 0)]
	[string]$Clip,
	[long]$From = -1,
	[long]$To = -1,
	[string]$Save,
	[double]$Rate = 0,
	[long[]]$Shots = @(),
	[switch]$Plan,
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$inv = [Globalization.CultureInfo]::InvariantCulture
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'

function Fail([string]$Message) {
	Write-Host "replay: $Message" -ForegroundColor Red
	exit 1
}

# --- The clip ----------------------------------------------------------------------------------------------------------

function Read-ClipText([string]$Path) {
	if ($Path -like '*.gz') {
		$file = [IO.File]::OpenRead($Path)
		try {
			$gz = [IO.Compression.GZipStream]::new($file, [IO.Compression.CompressionMode]::Decompress)
			$reader = [IO.StreamReader]::new($gz)
			try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
		} finally { $file.Dispose() }
	}
	$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
	try { ([IO.StreamReader]::new($stream)).ReadToEnd() } finally { $stream.Dispose() }
}

if (-not (Test-Path -LiteralPath $Clip -PathType Leaf)) { Fail "no clip at the given path" }
$clipName = Split-Path -Leaf $Clip
$clipDir = Split-Path -Parent (Resolve-Path -LiteralPath $Clip)
$text = Read-ClipText $Clip
$lines = @($text -split "`r?`n")

$header = @{}
$frames = 0
$firstMs = -1
$lastMs = -1
$firstPlayer = $null
foreach ($line in $lines) {
	if ($line -match '^@(\d+)ms\s+(.*)$') {
		$t = [long]$Matches[1]
		if ($Matches[2] -match '^\s*anchor\b') { continue }
		$frames++
		if ($firstMs -lt 0) { $firstMs = $t }
		$lastMs = $t
		if (-not $firstPlayer) {
			foreach ($command in ($Matches[2] -split ';')) {
				$w = @($command.Trim() -split '\s+')
				if ($w[0] -eq 'player' -and $w.Count -eq 6) { $firstPlayer = $w; break }
			}
		}
	} elseif ($frames -eq 0 -and $line -match '^# (build|save|seed|rate|mark): (.*)$') {
		$header[$Matches[1]] = $Matches[2].Trim()
	}
}
if ($frames -eq 0) { Fail "$clipName is no clip: it has no frame line (@<ms>ms ...)" }

$seed = if ("$($header['seed'])" -match '^\d+$') { [uint64]$header['seed'] } else { 0 }
$clipRate = if ("$($header['rate'])" -match '^\d+(\.\d+)?$') { [double]::Parse($header['rate'], $inv) } else { 90.0 }
if ($Rate -le 0) { $Rate = $clipRate }
$saveFolder = if ("$($header['save'])" -match '^(\S+)') { $Matches[1] } else { '' }
if ($saveFolder -eq 'none') { $saveFolder = '' }
$midSession = $firstMs -gt 0 -or [bool]$header['mark']

# --- The window and the shots ------------------------------------------------------------------------------------------

$fromMs = if ($From -ge 0) { $From } else { 0 }
$toMs = if ($To -ge 0) { $To } else { $lastMs }
if ($toMs -lt $fromMs) { Fail "the window ends ($toMs ms) before it starts ($fromMs ms)" }
if ($fromMs -gt $lastMs) { Fail "the window starts at $fromMs ms, after the clip's last frame ($lastMs ms)" }
if ($toMs -lt $firstMs) { Fail "the window ends at $toMs ms, before the clip's first frame ($firstMs ms)" }
foreach ($shot in $Shots) {
	if ($shot -lt $fromMs -or $shot -gt $toMs) { Fail "the shot at $shot ms is outside the window $fromMs-$toMs ms" }
}

# --- The save ----------------------------------------------------------------------------------------------------------

function Test-SaveFolder([string]$Dir) { $Dir -and (Test-Path -LiteralPath (Join-Path $Dir 'gsave.sav') -PathType Leaf) }

# The input events of a run's arx.log, in order: "<event> (<binding>)" and "<hand> grip down|up" (the game may still hold the file)
function Get-InputEvents([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return @() }
	$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
	try { $lines = @(([IO.StreamReader]::new($stream)).ReadToEnd() -split "`r?`n") } finally { $stream.Dispose() }
	@($lines | ForEach-Object {
		if ($_ -match '\[vr\] input: ([a-z_]+) \(([^()]*)\)\s*$') { "$($Matches[1]) ($($Matches[2]))" }
		elseif ($_ -match '\[vr\] input: (left|right) grip (down|up) at ') { "$($Matches[1]) grip $($Matches[2])" }
	})
}

$saveDir = $null
$saveWhere = ''
if ($Save) {
	if (Test-SaveFolder $Save) {
		$saveDir = (Resolve-Path -LiteralPath $Save).Path
		$saveWhere = '-Save, a save folder'
	} elseif (Test-SaveFolder (Join-Path (Get-ArxSaveLibrary) $Save)) {
		$saveDir = Join-Path (Get-ArxSaveLibrary) $Save
		$saveWhere = '-Save, the test save library'
	} else {
		Fail "-Save: no gsave.sav in the folder '$Save', and no such save in the test save library"
	}
} else {
	if (-not $saveFolder) { Fail "the clip names no save (# save: none) - give one with -Save" }
	$candidates = @(
		@{ Dir = (Join-Path $clipDir "save\$saveFolder"); Where = 'next to the clip' }
		@{ Dir = (Join-Path $clipDir "..\save\$saveFolder"); Where = "the clip's user directory" }
		@{ Dir = (Join-Path $clipDir "..\..\save\$saveFolder"); Where = "the clip's user directory" }
		@{ Dir = (Join-Path (Get-SharedUserDir) "save\$saveFolder"); Where = "the player's saves" }
	)
	foreach ($c in $candidates) {
		if (Test-SaveFolder $c.Dir) { $saveDir = (Resolve-Path -LiteralPath $c.Dir).Path; $saveWhere = $c.Where; break }
	}
	if (-not $saveDir) { Fail "the clip's save $saveFolder was not found (next to the clip, in its user directory, among the player's saves) - give it with -Save <folder>" }
}
$saveLeaf = Split-Path -Leaf $saveDir

# --- The plan ----------------------------------------------------------------------------------------------------------

$arxArgs = @('--loadslot', '0', '--seed', "$seed")
$playFile = "replay\$([IO.Path]::GetFileNameWithoutExtension(($clipName -replace '\.gz$', ''))).vrsim"
$simScript = @(
	'# replay.ps1 (#550): the clip streamed into the simulated headset from the timeline''s frame 0'
	"rate $($Rate.ToString($inv)) cap"
	'timeline gameplay'
	"play $playFile from $fromMs to $toMs"
)

$seconds = ($lastMs - $firstMs) / 1000.0
Write-Host ("Clip: {0}, {1} frames, {2} s{3}" -f $clipName, $frames, $seconds.ToString('0.0', $inv), $(if ($midSession) { ' (a mid-session clip: the replay is approximate)' } else { '' }))
Write-Host "Save: $saveLeaf ($saveWhere)"
Write-Host "Seed: $seed"
Write-Host "Rate: $($Rate.ToString($inv))"
Write-Host "Window: $fromMs-$toMs ms"
if ($Shots.Count) { Write-Host "Shots: $($Shots -join ', ')" }
Write-Host "Arguments: $($arxArgs -join ' ')"
Write-Host 'Sim script:'
foreach ($line in $simScript) { Write-Host "    $line" }
if ($Plan) { exit 0 }

# --- The launch --------------------------------------------------------------------------------------------------------

$testDir = Get-ArxTestDir
$userDir = Get-ArxUserDir
$work = Join-Path (Get-ArxAppDataDir) "replay\$(Get-WorkingCopyKey $ArxSourceDir)"
$library = Join-Path $work 'library'
Remove-Item -Recurse -Force $library -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $library 'save0000') | Out-Null
Copy-Item -Recurse -Force (Join-Path $saveDir '*') (Join-Path $library 'save0000')
$playDir = Join-Path $testDir 'replay'
New-Item -ItemType Directory -Force $playDir | Out-Null
[IO.File]::WriteAllText((Join-Path $testDir $playFile), $text, [Text.UTF8Encoding]::new($false))
$scriptFile = Join-Path $work 'replay.vrsim'
[IO.File]::WriteAllLines($scriptFile, $simScript, [Text.UTF8Encoding]::new($false))

# The run: the window plus the load and a margin; a hold at an anchor is bounded by it too
$runSeconds = [int][math]::Ceiling(($toMs - [math]::Max($fromMs, $firstMs)) / 1000.0) + 30
$params = @{ Seconds = $runSeconds; LockTimeoutSec = 600; ArxArgs = $arxArgs; VrSim = $scriptFile; Save = 'save0000'; SaveLibrary = $library }
if ($Rebuild) { $params.Rebuild = $true }
Clear-ArxLaunchMarker $testDir
$launched = Get-Date
$job = Start-ThreadJob -ScriptBlock {
	param($Smoke, $Params)
	& $Smoke @Params *>&1 | ForEach-Object { "$_" }
	"SMOKE-EXIT=$LASTEXITCODE"
} -ArgumentList $smoke, $params

function Complete-Run([int]$TimeoutSec = 600) {
	$null = Wait-Job $job -Timeout $TimeoutSec
	if ($job.State -eq 'Running') { Stop-Job $job }
	$output = @(Receive-Job $job -ErrorAction SilentlyContinue | ForEach-Object { "$_" })
	Remove-Job $job -Force -ErrorAction SilentlyContinue
	$exitLine = @($output | Where-Object { $_ -match '^SMOKE-EXIT=' }) | Select-Object -Last 1
	$script:smokeExit = if ($exitLine -match '^SMOKE-EXIT=(-?\d+)') { [int]$Matches[1] } else { $null }
	Write-Host '--- smoke-test.ps1, last lines:'
	$output | Where-Object { $_ -notmatch '^SMOKE-EXIT=' } | Select-Object -Last 8 | ForEach-Object { Write-Host "    $_" }
}

$script:smokeExit = $null
if (-not (Wait-ArxGameLaunched $job $testDir)) {
	Complete-Run
	if ($script:smokeExit -eq 2) { Write-Host 'replay: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; exit 2 }
	Fail "the game was not launched (smoke-test exit $($script:smokeExit))"
}

$done = $false
$final = $null
try {
	$null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' } -TimeoutSec 120 -Dir $testDir -What 'GAMEPLAY'
	if ($midSession -and $firstPlayer) {
		# The player where the clip's first frame has him: its eye, 170 units above the feet the teleport takes (TRAPS.md)
		$x = [double]::Parse($firstPlayer[1], $inv); $y = [double]::Parse($firstPlayer[2], $inv) + 170; $z = [double]::Parse($firstPlayer[3], $inv)
		$ack = Send-ArxCommand -Lines @(('setpos {0} {1} {2} {3}' -f $x.ToString('0.0', $inv), $y.ToString('0.0', $inv), $z.ToString('0.0', $inv), $firstPlayer[4])) -Dir $testDir
		Write-Host "replay: a mid-session clip - the player placed at its first frame ($ack)"
	}
	$pendingShots = [Collections.Generic.List[long]]::new()
	foreach ($shot in ($Shots | Sort-Object)) { $pendingShots.Add($shot) }
	$deadline = (Get-Date).AddSeconds($runSeconds)
	while ((Get-Date) -lt $deadline) {
		$status = Read-ArxStatus $testDir
		if (-not $status) { Start-Sleep -Milliseconds 200; continue }
		if ($status.state -eq 'EXITED') { break }
		$play = if ($status.vr -and $status.vr.sim -and $status.vr.sim.PSObject.Properties['play']) { $status.vr.sim.play } else { $null }
		if ($play) {
			$final = $play
			while ($pendingShots.Count -and [double]$play.positionMs -ge $pendingShots[0]) {
				$name = "replay-$($pendingShots[0])"
				$pendingShots.RemoveAt(0)
				try { $null = Send-ArxCommand -Lines @("shot $name") -Dir $testDir } catch { Write-Host "replay: the shot $name failed: $_" }
			}
			if ($play.done -eq $true) { $done = $true; break }
		}
		Start-Sleep -Milliseconds 200
	}
} catch {
	Write-Host "replay: $_" -ForegroundColor Red
}

# The run ends when smoke-test's seconds are up (a quit of our own would count as an early exit there)
Complete-Run

$events = @(Get-InputEvents (Get-ArxLogPath $userDir))
Write-Host ''
if ($final) {
	Write-Host ("Drift: current {0} m, max {1} m ({2} frames compared)" -f ([double]$final.drift.current).ToString('0.000', $inv), ([double]$final.drift.max).ToString('0.000', $inv), $final.drift.frames)
	Write-Host "Played: $($final.framesServed) frames, $($final.eventsReplayed) events, position $($final.positionMs) ms, done $($final.done), anchors waiting $($final.anchorsWaiting)$(if ($final.error) { ", error: $($final.error)" })"
} else {
	Write-Host 'Drift: none (the play never started)'
}
Write-Host "Input events ($($events.Count)):"
foreach ($event in $events) { Write-Host "    $event" }

if ($done -and $final -and -not $final.error -and $script:smokeExit -eq 0) { exit 0 }
if ($script:smokeExit -eq 2) { exit 2 }
exit 1
