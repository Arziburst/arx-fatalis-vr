<#
.SYNOPSIS
	Self-test of scripts/dev/clip-to-xrs.ps1 (#550) on fixture clips: the cut, the strip, the files it writes. No game.

.DESCRIPTION
	The clip format: scripts/dev/test-replay.ps1 and tests/vr/ReplayClipTest.h.

	clip-to-xrs.ps1 <Clip> -From <ms> -To <ms> -Name <name> [-OutDir <dir>]
	  - <Clip>: a .log, .log.gz or .vrsim clip; the window at most 30 s (To - From <= 30000), inside the clip
	  - writes <name>.vrsim and <name>.xrs into -OutDir (default: scripts\dev\vrsim and scripts\dev\xrs), exit 0;
	    refused (exit 1, nothing written): a window over 30 s, ending before it starts or outside the clip, no clip
	  - <name>.vrsim, a simulator script (src/vr/SimRig.h) the CI harness runs as it is:
	      comment lines first: the source clip's file name (never its folder), the window, the build, the seed, the save
	      "rate <the clip's rate> cap" and "timeline gameplay" (the CI pacing, docs: coordinator Tests and CI rule 6)
	      @0ms ...            the whole state at From: the last value at or before From of everything the clip sets
	                          (poses, curls, aims, actions, hand on/off, fingers on/off, profile, session, player)
	      @<t - From>ms ...   the clip's frame lines of (From, To], each with its own commands, re-based
	    and stripped: no ipd and no floor line (the player's eye distance and height), no comment of the clip but the
	    ones above, and no user path or account name anywhere (the save's name scrubbed as log-digest-lib's
	    Protect-ArxText does)
	  - <name>.xrs, a skeleton sequence that xrs-lib's Read-ArxSequence parses: "@sim <name>", "@args ... --seed <seed>",
	    a comment naming the clip's save folder (a test save library scenario must take its place), and a wait for
	    the timeline ("@until vr.sim.pending eq 0 ..."); the worker adds the asserts
Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

$script = Join-Path $PSScriptRoot 'clip-to-xrs.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-clip-to-xrs-test-$PID"
$pwsh = (Get-Process -Id $PID).Path
$c = [Globalization.CultureInfo]::InvariantCulture

if (-not (Test-Path -LiteralPath $script)) {
	Write-Host '  FAIL scripts/dev/clip-to-xrs.ps1 does not exist' -ForegroundColor Red
	Write-Host 'test-clip-to-xrs: FAILED' -ForegroundColor Red
	exit 1
}

# --- A fixture clip: 40 s at 90 Hz (the same session as test-replay.ps1's) -------------------------------------------

function Get-FixtureMs([long]$K) { [long][math]::Floor(($K * 11111 + 500) / 1000) }

function Format-FixturePose([double[]]$V) {
	[string]::Format($c, '{0:0.0000} {1:0.0000} {2:0.0000} {3:0.00} {4:0.00} {5:0.00}', $V[0], $V[1], $V[2], $V[3], $V[4], $V[5])
}

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
$script:Aim = 'hand left aim 0.0000 -0.0100 -0.0500 0.00 -40.00 0.00'

# The commands of frame k, in the clip's order
function Get-FixtureCommands([int]$K) {
	$commands = @(Get-FixtureMoving $K)
	if ($K -eq 0) {
		$commands += @(
			$script:Aim, 'hand right aim 0.0000 -0.0100 -0.0500 0.00 -40.00 0.00'
			'hand left on', 'hand right on', 'hand left fingers on', 'hand right fingers on'
			'profile /interaction_profiles/valve/index_controller', 'session focused'
			'action trigger left 0.00', 'action trigger right 0.00', 'action button_a left 0', 'action button_a right 0'
			'action thumbstick left 0.00 0.00', 'action thumbstick right 0.00 0.00'
		)
	}
	if ($K -eq 45) { $commands += 'action button_a right 1' }
	if ($K -eq 63) { $commands += 'hand right off' }
	if ($K -eq 2000) { $commands += 'action thumbstick left 0.10 0.95' }
	$commands
}

function Get-FixtureClipLines {
	$lines = @(
		'# arx-replay (#550): a fixture clip of test-clip-to-xrs.ps1'
		'# build: Arx Fatalis VR test'
		"# save: save9137 fixtureuser's quick save"
		'# seed: 4242'
		'# rate: 90'
		('# note: recorded in ' + 'C:\Users\' + 'fixtureuser' + '\Saved Games\Arx Libertatis')
		'ipd 0.0617'
		'floor 1.8325'
	)
	for ($k = 0; $k -lt $script:FixtureFrames; $k++) { $lines += "@$(Get-FixtureMs $k)ms " + ((Get-FixtureCommands $k) -join '; ') }
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

function Invoke-Convert([string[]]$Arguments) {
	$output = & $pwsh -NoProfile -File $script @Arguments 2>&1
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = (@($output | ForEach-Object { "$_" }) -join "`n") }
}

# The timed commands of a script: time (ms) -> the commands at that time, split at ';', in order
function Get-TimedCommands([string[]]$Lines) {
	$timed = [ordered]@{}
	foreach ($line in $Lines) {
		if ($line -notmatch '^@(\d+)ms\s+(.+)$') { continue }
		$t = [long]$Matches[1]
		if (-not $timed.Contains($t)) { $timed[$t] = [Collections.Generic.List[string]]::new() }
		foreach ($command in ($Matches[2] -split ';')) { if ($command.Trim()) { $timed[$t].Add(($command.Trim() -replace '\s+', ' ')) } }
	}
	$timed
}

# A user path, the account name: as text and as UTF-16 at both alignments
function Find-Private([string]$Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	$views = @([Text.Encoding]::Latin1.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes))
	if ($bytes.Length -gt 1) { $views += [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
	foreach ($view in $views) {
		if ($view -match '(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b') { 'a user path' }
		if ($view -match '(?i)(?<![\w-])fixtureuser(?![\w-])') { 'the account name' }
	}
}

$previousUser = $env:USERNAME
$env:USERNAME = 'fixtureuser'
try {
	$clip = Join-Path $root 'user\arx-replay.log'
	$lines = Get-FixtureClipLines
	Write-Fixture $clip $lines
	$out = Join-Path $root 'out'

	# The window: frame 100 to frame 300 (1111 - 3333 ms)
	$fromMs = Get-FixtureMs 100
	$toMs = Get-FixtureMs 300

	Write-Host "==> a cut of $fromMs-$toMs ms"
	$run = Invoke-Convert @($clip, '-From', $fromMs, '-To', $toMs, '-Name', 'replay-fixture', '-OutDir', $out)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))$(if ($run.Exit) { ': ' + $run.Text })"
	$vrsim = Join-Path $out 'replay-fixture.vrsim'
	$xrs = Join-Path $out 'replay-fixture.xrs'
	Check (Test-Path -LiteralPath $vrsim) 'replay-fixture.vrsim written'
	Check (Test-Path -LiteralPath $xrs) 'replay-fixture.xrs written'

	if (Test-Path -LiteralPath $vrsim) {
		$text = [IO.File]::ReadAllText($vrsim)
		$scriptLines = @($text -split "`r?`n")
		Check (@($scriptLines | Where-Object { $_ -match '^\s*(ipd|floor)\b' }).Count -eq 0) 'no ipd and no floor line (stripped)'
		Check (-not $text.Contains('0.0617') -and -not $text.Contains('1.8325')) 'the eye distance and the height are nowhere in it'
		Check (@($scriptLines | Where-Object { $_ -match '^rate 90 cap\s*$' }).Count -eq 1) 'rate 90 cap'
		Check (@($scriptLines | Where-Object { $_ -match '^timeline gameplay\s*$' }).Count -eq 1) 'timeline gameplay'
		Check ($text -match '(?m)^#.*\barx-replay\.log\b') 'a comment names the source clip''s file'
		Check (@($scriptLines | Where-Object { $_ -match '^@\+' -or ($_ -match '^@' -and $_ -notmatch '^@\d+ms\s') }).Count -eq 0) 'every timed line is @<t>ms, absolute'

		$timed = Get-TimedCommands $scriptLines
		$times = @($timed.Keys)
		$window = $toMs - $fromMs
		Check ($times.Count -gt 0 -and $times[0] -eq 0) 'the script starts at @0ms'
		Check (@($times | Where-Object { $_ -lt 0 -or $_ -gt $window }).Count -eq 0) "every line within 0-$window ms"
		Check (@($times | Where-Object { $_ -gt 0 }).Count -eq 200) "200 frames after the start, frames 101-300 ($(@($times | Where-Object { $_ -gt 0 }).Count))"

		# The state at From: frame 100's poses, and what was set before it and still holds
		$start = if ($timed.Contains([long]0)) { @($timed[[long]0]) } else { @() }
		foreach ($expected in @((Get-FixtureMoving 100) + @($script:Aim, 'hand right off', 'action button_a right 1',
				'profile /interaction_profiles/valve/index_controller', 'session focused', 'action thumbstick left 0.00 0.00'))) {
			Check ($start -contains $expected) "@0ms has '$expected'"
		}
		Check ($start -notcontains 'hand right on') '@0ms: the right hand is off (the last value, not the first)'
		Check ($start -notcontains 'action button_a right 0') '@0ms: A is held (the last value, not the first)'

		# A frame inside: its own commands, re-based
		$k = 250
		$t = (Get-FixtureMs $k) - $fromMs
		$got = if ($timed.Contains([long]$t)) { @($timed[[long]$t]) } else { @() }
		$want = @(Get-FixtureMoving $k)
		Check ((@($want | Where-Object { $got -notcontains $_ }).Count -eq 0) -and $got.Count -eq $want.Count) "frame $k at @$($t)ms, its commands"

		$found = @(Find-Private $vrsim)
		Check ($found.Count -eq 0) "no user path or account name in the .vrsim$(if ($found) { ' - found ' + ($found -join ', ') })"
	}

	if (Test-Path -LiteralPath $xrs) {
		$text = [IO.File]::ReadAllText($xrs)
		Check ($text -match '(?m)^@sim replay-fixture\s*$') '@sim replay-fixture'
		Check ($text -match '(?m)^@args\s.*--seed 4242\b') '@args with --seed 4242'
		Check ($text -match '(?m)^#.*\bsave9137\b') 'a comment names the clip''s save folder'
		Check ($text -match '(?m)^@until vr\.sim\.pending eq 0\b') 'waits for the timeline: @until vr.sim.pending eq 0'
		$found = @(Find-Private $xrs)
		Check ($found.Count -eq 0) "no user path or account name in the .xrs$(if ($found) { ' - found ' + ($found -join ', ') })"
		$parsed = $null
		try {
			. (Join-Path $PSScriptRoot 'xrs-lib.ps1')
			$parsed = Read-ArxSequence $xrs
		} catch { Write-Host "    $_" }
		Check ($null -ne $parsed) 'the .xrs parses (xrs-lib Read-ArxSequence)'
	}
	Check ($run.Text -notmatch '(?i)(?<![\w-])fixtureuser(?![\w-])' -and -not $run.Text.Contains($root)) 'its output names no account and no full path'

	Write-Host '==> the same cut from the compressed clip'
	$gz = Join-Path $root 'user\arx-replay.1.log.gz'
	Write-GzipFixture $gz $lines
	$outGz = Join-Path $root 'out-gz'
	$run = Invoke-Convert @($gz, '-From', $fromMs, '-To', $toMs, '-Name', 'replay-fixture', '-OutDir', $outGz)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	$gzVrsim = Join-Path $outGz 'replay-fixture.vrsim'
	if ((Test-Path -LiteralPath $gzVrsim) -and (Test-Path -LiteralPath $vrsim)) {
		$a = Get-TimedCommands @([IO.File]::ReadAllLines($vrsim))
		$b = Get-TimedCommands @([IO.File]::ReadAllLines($gzVrsim))
		Check (($a.Keys -join ',') -eq ($b.Keys -join ',') -and (@($a.Values | ForEach-Object { $_ -join ';' }) -join '|') -eq (@($b.Values | ForEach-Object { $_ -join ';' }) -join '|')) 'the same timed lines as from the plain clip'
	} else {
		Check $false 'the .gz cut was written'
	}

	Write-Host '==> refused'
	foreach ($case in @(
			@{ What = 'a window over 30 s'; Args = @('-From', '0', '-To', '30001') }
			@{ What = 'a window ending before it starts'; Args = @('-From', '3000', '-To', '1000') }
			@{ What = 'a window after the clip''s end'; Args = @('-From', '45000', '-To', '50000') }
		)) {
		$dir = Join-Path $root ('refused-' + ($case.What -replace '\W', ''))
		$run = Invoke-Convert (@($clip) + $case.Args + @('-Name', 'refused', '-OutDir', $dir))
		Check ($run.Exit -eq 1) "$($case.What): exit 1 ($($run.Exit))"
		Check (-not (Test-Path -LiteralPath (Join-Path $dir 'refused.vrsim')) -and -not (Test-Path -LiteralPath (Join-Path $dir 'refused.xrs'))) "$($case.What): nothing written"
	}
	$run = Invoke-Convert @($clip, '-From', '0', '-To', '30000', '-Name', 'longest', '-OutDir', (Join-Path $root 'longest'))
	Check ($run.Exit -eq 0) "exactly 30 s is allowed: exit 0 ($($run.Exit))"
	$notClip = Join-Path $root 'notclip\arx.log'
	Write-Fixture $notClip @('[I] Arx Fatalis VR test', '[I] Clean shutdown')
	$run = Invoke-Convert @($notClip, '-From', '0', '-To', '1000', '-Name', 'notclip', '-OutDir', (Join-Path $root 'notclip-out'))
	Check ($run.Exit -eq 1) "a file that is no clip: exit 1 ($($run.Exit))"
} finally {
	$env:USERNAME = $previousUser
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-clip-to-xrs: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-clip-to-xrs: all checks passed' -ForegroundColor Green
exit 0
