<#
.SYNOPSIS
	Self-test of scripts/dev/replay.ps1 (#550) on fixture clips: the replay's plan, no game.

.DESCRIPTION
	A clip is a recorded VR session in the .vrsim dialect (tests/vr/ReplayClipTest.h has the format):

	  # arx-replay ...                    the first line
	  # build: <text>
	  # save: <save folder> <save name>   the save made at the record start
	  # seed: <decimal>                   the RNG seed (--seed)
	  # rate: <hz>
	  # mark: <name> <ms>                 only in a mark's clip
	  ipd <m>                             set-up lines
	  floor <m>
	  @<ms>ms <command>; <command>; ...   one line per frame, absolute display time since the record start

	Clips are <user dir>\arx-replay.log (.gz once the session ended; arx-replay.<n>.log[.gz] the older ones),
	<user dir>\replay-marks\*.vrsim (`replay mark`) and <user dir>\crashes|hangs\<utc>\arx-replay.vrsim (the reports).

	replay.ps1 <Clip> [-From <ms>] [-To <ms>] [-Save <save>] [-Rate <hz>] [-Shots <ms>[,<ms>...]] [-Plan]
	  - launches the simulated headset (smoke-test.ps1 -VrSim, isolated user directory) from the clip's save, with
	    --loadslot 0 --seed <the clip's seed>, and a simulator script that plays the clip from the timeline's frame 0:
	      rate <hz> ...                              the clip's rate, or -Rate
	      play <file> from <From> to <To>            the clip copied (decompressed) under the test directory first, the
	                                                 path relative to it (the game's rotation never moves what it plays)
	    waits until vr.sim.play.done, takes a shot at each -Shots time (ms on the clip's clock) and prints:
	      Drift: current <m> m, max <m> m            vr.sim.play.drift
	      Input events (<n>):                        then one indented line per "[vr] input: <event> (<binding>)" of the run
	    exit 0 when the clip played to its end (done, no error), 1 otherwise, 2 when the game lock stayed busy
	  - the save: -Save <folder path> (that save folder) or <name> (the test save library); without -Save the header's
	    save folder, looked up in <clip folder>\save\, then <clip folder>\..\save\ (a mark's clip), then
	    <clip folder>\..\..\save\ (a report's clip), then the player's save folder; none found: exit 1 naming the folder
	  - -Plan: no launch, exit 0, prints the plan, each item at the start of a line:
	      Clip: <file name>, <n> frames, <s> s        only the file's name, never its folder
	      Save: <save folder> (<where it was found>)
	      Seed: <n>
	      Rate: <hz>
	      Window: <from>-<to> ms                      default: 0 to the last frame's time
	      Shots: <ms>, <ms>                           only with -Shots
	      Arguments: <arx.exe arguments>              --loadslot 0 --seed <n> ...
	      Sim script:                                 then the simulator script, indented
	  - refused (exit 1, no launch): a file that is no clip (no frame line), a window outside the clip or ending before it
	    starts, a shot outside the window
	  - nothing it prints holds a user path or the account name (the clip's comments may: they are not printed)
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

$script = Join-Path $PSScriptRoot 'replay.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-replay-test-$PID"
$pwsh = (Get-Process -Id $PID).Path
$c = [Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $script)) {
	Write-Host '  FAIL scripts/dev/replay.ps1 does not exist' -ForegroundColor Red
	Write-Host 'test-replay: FAILED' -ForegroundColor Red
	exit 1
}

# --- A fixture clip: 40 s at 90 Hz of a moving session ------------------------------------------------------------------

function Get-FixtureMs([long]$K) { [long][math]::Floor(($K * 11111 + 500) / 1000) }

function Format-FixturePose([double[]]$V) {
	[string]::Format($c, '{0:0.0000} {1:0.0000} {2:0.0000} {3:0.00} {4:0.00} {5:0.00}', $V[0], $V[1], $V[2], $V[3], $V[4], $V[5])
}

# The commands of frame k that move every frame
function Get-FixtureMoving([int]$K) {
	$t = $K * 0.011111
	$head = Format-FixturePose @((0.05 * [math]::Sin($t)), (-0.02 + 0.01 * [math]::Cos(1.3 * $t)), (-0.03 * [math]::Sin(0.7 * $t)),
		(40 * [math]::Sin(0.5 * $t)), (15 * [math]::Sin(0.9 * $t)), (5 * [math]::Sin(1.7 * $t)))
	$left = Format-FixturePose @((-0.2 - 0.05 * [math]::Sin($t)), (-0.45 + 0.03 * [math]::Cos($t)), (-0.35 + 0.04 * [math]::Sin(2 * $t)),
		(-20 * [math]::Sin($t)), (-30 + 10 * [math]::Cos($t)), (15 * [math]::Sin(0.5 * $t)))
	$right = Format-FixturePose @((0.2 + 0.05 * [math]::Sin($t + 1)), (-0.45 + 0.03 * [math]::Cos($t)), (-0.35 + 0.04 * [math]::Sin(2 * $t)),
		(20 * [math]::Sin($t)), (-30 + 10 * [math]::Cos($t)), (15 * [math]::Sin(0.5 * $t)))
	$curl = 0.5 + 0.4 * [math]::Sin($t)
	$fingers = [string]::Format($c, '{0:0.00} {1:0.00} 0.30 0.60 0.70', $curl, (1 - $curl))
	$player = [string]::Format($c, '{0:0.0} 120.0 {1:0.0} {2:0.00} 10', (1000 + 1.5 * $K), (2000 - 0.75 * $K), ((270 + 0.37 * $K) % 360))
	@("head $head", "hand left $left", "hand right $right", "hand left fingers $fingers", "hand right fingers $fingers", "player $player")
}

$script:FixtureFrames = 3600
$script:FixtureLastMs = Get-FixtureMs ($script:FixtureFrames - 1)

function Get-FixtureClipLines([string]$SaveFolder = 'save9137', [string]$Mark) {
	# The account name and a home path in places a careless tool would copy: the save's name and a stray comment
	$lines = @(
		'# arx-replay (#550): a fixture clip of test-replay.ps1'
		'# build: Arx Fatalis VR test'
		"# save: $SaveFolder fixtureuser's quick save"
		'# seed: 4242'
		'# rate: 90'
	)
	if ($Mark) { $lines += "# mark: $Mark" }
	$lines += @(
		('# note: recorded in ' + 'C:\Users\' + 'fixtureuser' + '\Saved Games\Arx Libertatis')
		'ipd 0.0617'
		'floor 1.8325'
	)
	$full = @(
		'hand left aim 0.0000 -0.0100 -0.0500 0.00 -40.00 0.00', 'hand right aim 0.0000 -0.0100 -0.0500 0.00 -40.00 0.00'
		'hand left on', 'hand right on', 'hand left fingers on', 'hand right fingers on'
		'profile /interaction_profiles/valve/index_controller', 'session focused'
		'action trigger left 0.00', 'action trigger right 0.00', 'action button_a left 0', 'action button_a right 0'
		'action thumbstick left 0.00 0.00', 'action thumbstick right 0.00 0.00'
	)
	for ($k = 0; $k -lt $script:FixtureFrames; $k++) {
		$commands = @(Get-FixtureMoving $k)
		if ($k -eq 0) { $commands += $full }
		if ($k -eq 45) { $commands += 'action button_a right 1' }
		if ($k -eq 63) { $commands += 'hand right off' }
		$lines += "@$(Get-FixtureMs $k)ms " + ($commands -join '; ')
	}
	$lines
}

function Write-Fixture([string]$Path, [string[]]$Lines) {
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllLines($Path, $Lines, [Text.UTF8Encoding]::new($false))
}

function Write-GzipFixture([string]$Path, [string[]]$Lines) {
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Lines -join "`n") + "`n")
	$file = [IO.File]::Create($Path)
	try {
		$gz = [IO.Compression.GZipStream]::new($file, [IO.Compression.CompressionMode]::Compress)
		$gz.Write($bytes, 0, $bytes.Length)
		$gz.Dispose()
	} finally { $file.Dispose() }
}

function New-FakeSave([string]$Dir) {
	New-Item -ItemType Directory -Force $Dir | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $Dir 'gsave.sav'), [byte[]](1, 2, 3, 4))
}

function ConvertTo-Quoted([string]$Text) { "'" + $Text.Replace("'", "''") + "'" }

# replay.ps1 with arguments as PowerShell text (so -Shots takes a list), in its own process
function Invoke-Replay([string]$Clip, [string]$Rest = '') {
	$command = "& $(ConvertTo-Quoted $script) $(ConvertTo-Quoted $Clip) $Rest -Plan; exit `$LASTEXITCODE"
	$output = & $pwsh -NoProfile -Command $command 2>&1
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = (@($output | ForEach-Object { "$_" }) -join "`n") }
}

function Test-Clean($Run, [string]$What) {
	Check ($Run.Text -notmatch '(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b') "$What`: no user path in the output"
	Check ($Run.Text -notmatch '(?i)(?<![\w-])fixtureuser(?![\w-])') "$What`: no account name in the output"
	Check (-not $Run.Text.Contains($root)) "$What`: no full path of the clip, only its name"
}

# The section of the plan after "Sim script:", its indented lines
function Get-SimScript([string]$Text) {
	$lines = $Text -split "`n"
	$start = [array]::FindIndex($lines, [Predicate[string]] { param($l) $l -match '^Sim script:' })
	if ($start -lt 0) { return @() }
	@($lines[($start + 1)..($lines.Count - 1)] | ForEach-Object { $_ } | Where-Object { $_ -match '^\s+\S' } | ForEach-Object { $_.Trim() })
}

$previousUser = $env:USERNAME
$env:USERNAME = 'fixtureuser'
try {
	$user = Join-Path $root 'user'
	$lines = Get-FixtureClipLines
	$clip = Join-Path $user 'arx-replay.log'
	Write-Fixture $clip $lines
	Write-GzipFixture (Join-Path $user 'arx-replay.1.log.gz') $lines
	New-FakeSave (Join-Path $user 'save\save9137')

	Write-Host '==> the plan of a session''s clip'
	$run = Invoke-Replay $clip
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match "(?m)^Clip: arx-replay\.log\b.*\b$($script:FixtureFrames) frames") "Clip: the file's name and its $($script:FixtureFrames) frames"
	Check ($run.Text -match '(?m)^Save: save9137\b') 'Save: the header''s save folder, found next to the clip'
	Check ($run.Text -match '(?m)^Seed: 4242\s*$') 'Seed: 4242'
	Check ($run.Text -match '(?m)^Rate: 90(\.0+)?\b') 'Rate: 90'
	Check ($run.Text -match "(?m)^Window: 0-$($script:FixtureLastMs) ms") "Window: 0-$($script:FixtureLastMs) ms (the whole clip)"
	Check ($run.Text -match '(?m)^Arguments: .*--seed 4242\b') 'Arguments: --seed 4242'
	Check ($run.Text -match '(?m)^Arguments: .*--loadslot 0\b') 'Arguments: --loadslot 0'
	Check ($run.Text -notmatch '(?m)^Shots:') 'no Shots line without -Shots'
	$sim = Get-SimScript $run.Text
	Check (@($sim | Where-Object { $_ -match '^rate 90(\.0+)?\b' }).Count -eq 1) "the sim script sets the clip's rate ($($sim -join ' | '))"
	Check (@($sim | Where-Object { $_ -match "^play\s+\S.*\bfrom 0 to $($script:FixtureLastMs)\s*$" }).Count -eq 1) 'the sim script plays the clip, the whole window'
	Check (@($sim | Where-Object { $_ -match '^(ipd|floor)\b' }).Count -eq 0) 'the sim script does not set the IPD or the floor itself (the clip does)'
	Test-Clean $run 'a session''s clip'

	Write-Host '==> the same clip, compressed (an older session: arx-replay.1.log.gz)'
	$gz = Invoke-Replay (Join-Path $user 'arx-replay.1.log.gz')
	Check ($gz.Exit -eq 0) "exit 0 ($($gz.Exit))"
	Check ($gz.Text -match "(?m)^Clip: arx-replay\.1\.log\.gz\b.*\b$($script:FixtureFrames) frames") 'Clip: the .gz file, the same frames'
	Check ($gz.Text -match '(?m)^Save: save9137\b' -and $gz.Text -match '(?m)^Seed: 4242\s*$') 'the same save and seed'
	Check ($gz.Text -match "(?m)^Window: 0-$($script:FixtureLastMs) ms") 'the same window'
	Test-Clean $gz 'a .gz clip'

	Write-Host '==> a window, a rate and shots'
	$run = Invoke-Replay $clip '-From 1000 -To 3000 -Rate 72 -Shots 1500,2500'
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Window: 1000-3000 ms') 'Window: 1000-3000 ms'
	Check ($run.Text -match '(?m)^Rate: 72(\.0+)?\b') 'Rate: 72 (-Rate)'
	Check ($run.Text -match '(?m)^Shots: .*\b1500\b.*\b2500\b') 'Shots: 1500 and 2500'
	$sim = Get-SimScript $run.Text
	Check (@($sim | Where-Object { $_ -match '^rate 72(\.0+)?\b' }).Count -eq 1) "the sim script runs at 72 Hz ($($sim -join ' | '))"
	Check (@($sim | Where-Object { $_ -match '^play\s+\S.*\bfrom 1000 to 3000\s*$' }).Count -eq 1) 'the sim script plays 1000-3000 ms'

	Write-Host '==> refused: outside the clip'
	$run = Invoke-Replay $clip '-From 50000'
	Check ($run.Exit -eq 1) "a window after the clip's end: exit 1 ($($run.Exit))"
	$run = Invoke-Replay $clip '-From 3000 -To 1000'
	Check ($run.Exit -eq 1) "a window ending before it starts: exit 1 ($($run.Exit))"
	$run = Invoke-Replay $clip '-From 1000 -To 3000 -Shots 5000'
	Check ($run.Exit -eq 1) "a shot outside the window: exit 1 ($($run.Exit))"
	$notClip = Join-Path $root 'notclip\arx.log'
	Write-Fixture $notClip @('[I] Arx Fatalis VR test', '[I] Clean shutdown')
	$run = Invoke-Replay $notClip
	Check ($run.Exit -eq 1) "a file that is no clip: exit 1 ($($run.Exit))"

	Write-Host '==> the save'
	$lonely = Join-Path $root 'lonely\arx-replay.log'
	Write-Fixture $lonely $lines
	$run = Invoke-Replay $lonely
	Check ($run.Exit -eq 1) "the clip's save is nowhere: exit 1 ($($run.Exit))"
	Check ($run.Text -match 'save9137') 'the message names the save folder it looked for'
	Test-Clean $run 'a missing save'
	$other = Join-Path $root 'elsewhere\save0042'
	New-FakeSave $other
	$run = Invoke-Replay $lonely "-Save $(ConvertTo-Quoted $other)"
	Check ($run.Exit -eq 0) "-Save <a save folder>: exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Save: save0042\b') 'Save: the folder given'
	Test-Clean $run '-Save <folder>'

	Write-Host '==> a crash report''s clip and a mark''s clip'
	$report = Join-Path $user 'crashes\2026-10-20-10-00-00-00\arx-replay.vrsim'
	Write-Fixture $report $lines
	$run = Invoke-Replay $report
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Clip: arx-replay\.vrsim\b') 'Clip: arx-replay.vrsim'
	Check ($run.Text -match '(?m)^Save: save9137\b') 'Save: found in the report''s user directory'
	Test-Clean $run 'a report''s clip'
	$markLines = Get-FixtureClipLines -Mark 'chest-did-not-open 39989'
	$mark = Join-Path $user 'replay-marks\2026-10-20-10-05-00-chest-did-not-open.vrsim'
	Write-Fixture $mark $markLines
	$run = Invoke-Replay $mark
	Check ($run.Exit -eq 0) "a mark's clip: exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Save: save9137\b') 'Save: found in the mark''s user directory'
	Test-Clean $run 'a mark''s clip'
} finally {
	$env:USERNAME = $previousUser
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-replay: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-replay: all checks passed' -ForegroundColor Green
exit 0
