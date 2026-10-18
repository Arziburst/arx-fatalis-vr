# The .xrs sequence runner (research rows F-05 and F-14, #113): parse a sequence, run it against a game that
# is already running with the test channel (--test-dir), judge it. Dot-source it:
#   . (Join-Path $PSScriptRoot 'xrs-lib.ps1')
# Shared by xrs-run.ps1, smoke-test.ps1 (-Sequence) and the self-test test-xrs.ps1.
#
# Adapted from tools/xrsim-run.ps1 of Dishonored VR (https://github.com/VR-Stereo-Hub/Dishonored-VR, MIT
# License, see LICENSE.xrsim). Changed: the sequence is parsed and checked before anything runs, the
# commands go through this game's test channel (command.txt / ack.txt / status.json, docs/vr/TESTING.md)
# instead of a simulator's own files, @assert / @until read status.json by a dotted path, @log / @nolog
# read arx.log, and @until, @diff and @pending are new.
#
# A .xrs file is test-channel commands, one per line, with a few directives (docs/vr/TESTING.md):
#   # ...                          comment
#   <command>[; <command>]         any channel command (shot, mark, vr-input tap right.a, vr-sim head rot 25 0 0,
#                                  loadlevel 10, a script-console line ...); ';' sends them as ONE batch, which the
#                                  game applies in the same frame. A refused command (errors>0 in the ack) fails the step.
#   @wait <ms>                     sleep
#   @frames <n>                    wait until the game rendered n more frames (the channel's waitframes: its ack comes then)
#   @until <path> <op> <v> [<n>s]  wait up to n seconds (default 30) for status.json to satisfy the condition
#   @assert <path> <op> <v>        status.json, read fresh now. path: dotted, "quoted" where a key holds a dot
#                                  (events.byName."spell.cast"); op: eq ne gt ge lt le
#   @remember <name> <path>        keep a status.json value; a later @assert / @until value can be {name} or {name+5} / {name-5}
#                                  / {name*0.9} (the yaw before a turn, the position before a walk; *: relative)
#   @mark                          forget the arx.log written so far: @log and @nolog only look at what the
#                                  game wrote after the last mark (the start of the sequence is the first mark)
#   @log <regex> [<n>s]            wait up to n seconds (default 5) for a log line since the mark to match; a
#                                  match moves the mark past it, so consecutive @log lines assert an ORDER
#   @nolog <regex>                 fail if a log line since the mark matches. Does not move the mark.
#   @shot <name>                   capture: on the simulated headset both eyes and the flat frame
#                                  (capture\<name>-left.bmp, -right.bmp, -quad.bmp), else the frame (capture\<name>.bmp)
#   @diff <a> <b> [max] <op> <v> [eye]  img-diff.ps1 of two shots: the mean absolute per-channel difference (0-255),
#                                  or with max the largest change of one channel (a small bright effect),
#                                  eye = left (default) | right | quad | left-layer | right-layer (the eye without the quad,
#                                  after `vr-sim layers on`, #72) on the simulated headset. A still scene
#                                  shot twice is below ~0.5; a head turn is several levels.
#   @key <name> [n] [ms]           press a key n times (default 1), ms apart (default 300), in the game window,
#                                  without taking the focus (a posted keyboard message): Escape, Enter, Space, A-Z, 0-9, F1-F12 ...
#                                  Real input: a test run's window drops it (#487) - press keys with desktop-key
#   @pending <#issue> <step>       a step that the game cannot pass yet (the feature is issue #N): its failure is
#                                  reported as pending, not as a failure; once it passes the run FAILS until the
#                                  @pending is removed - an expectation cannot go stale.
#   @sim <name> / @args <...>      launch information (xrs-run.ps1): the simulated-headset script and the arx.exe
#                                  arguments of the run the sequence wants; ignored while running
#   @save <name>                   launch information: start from this save of the test save library (#70,
#                                  smoke-test.ps1 -Save) - with @args --loadslot 0; ignored while running
#   @playground                    launch information: start in the test playground (#321, smoke-test.ps1
#                                  -Playground: the generated level in area 30); ignored while running
#   @player                        launch information: a player's launch (#394, smoke-test.ps1 -Player): no
#                                  --dev-launch, so the publisher logos, the main menu and every cinematic are
#                                  shown; without @args the sequence starts while the first logo is up (state LOGO)
#
# A sequence stops at its first failed step (-ContinueOnError: runs on and lists them all).

. (Join-Path $PSScriptRoot 'test-lib.ps1')

$script:XrsOps = 'eq|ne|gt|ge|lt|le'

# A sequence by path, or by name from scripts\dev\xrs (".xrs" optional)
function Resolve-ArxSequence([string]$Name) {
	$dir = Join-Path $ArxSourceDir 'scripts\dev\xrs'
	foreach ($candidate in @($Name, (Join-Path $dir $Name), (Join-Path $dir "$Name.xrs"))) {
		if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
	}
	$known = @(Get-ChildItem $dir -Filter '*.xrs' -ErrorAction SilentlyContinue | ForEach-Object BaseName)
	throw "no sequence '$Name' (a path, or one of: $($known -join ', '))"
}

# Parse one line into a step, or throw with the reason
function ConvertTo-XrsStep([string]$Line, [int]$Number) {
	$step = [ordered]@{ N = $Number; Line = $Line; Kind = 'command' }
	$m = $null
	if ($Line -notmatch '^@') {
		$step.Commands = @($Line -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
		return [pscustomobject]$step
	}
	if ($Line -match '^@wait\s+(\d+)\s*$') { $step.Kind = 'wait'; $step.Ms = [int]$Matches[1] }
	elseif ($Line -match '^@frames\s+(\d+)\s*$') { $step.Kind = 'frames'; $step.Count = [int]$Matches[1] }
	elseif ($Line -match "^@(assert|until)\s+(\S+)\s+($script:XrsOps)\s+(\S+)(?:\s+(\d+)s)?\s*$") {
		$step.Kind = $Matches[1]; $step.Path = $Matches[2]; $step.Op = $Matches[3]; $step.Value = $Matches[4]
		if ($step.Kind -eq 'until') {
			$step.Seconds = if ($Matches[5]) { [int]$Matches[5] } else { 30 }
		} elseif ($Matches[5]) {
			throw "@assert has no timeout (use @until)"
		}
	}
	elseif ($Line -match '^@remember\s+(\w+)\s+(\S+)\s*$') { $step.Kind = 'remember'; $step.Name = $Matches[1]; $step.Path = $Matches[2] }
	elseif ($Line -match '^@mark\s*$') { $step.Kind = 'mark' }
	elseif ($Line -match '^@log\s+(.+?)(?:\s+(\d+)s)?\s*$') {
		$step.Kind = 'log'; $step.Regex = $Matches[1]; $step.Seconds = if ($Matches[2]) { [int]$Matches[2] } else { 5 }
		try { $null = [regex]::new($step.Regex) } catch { throw "bad regular expression /$($step.Regex)/: $($_.Exception.Message)" }
	}
	elseif ($Line -match '^@nolog\s+(.+?)\s*$') {
		$step.Kind = 'nolog'; $step.Regex = $Matches[1]
		try { $null = [regex]::new($step.Regex) } catch { throw "bad regular expression /$($step.Regex)/: $($_.Exception.Message)" }
	}
	elseif ($Line -match '^@shot\s+([\w.-]+)\s*$') { $step.Kind = 'shot'; $step.Name = $Matches[1] }
	elseif ($Line -match "^@diff\s+([\w.-]+)\s+([\w.-]+)\s+(?:(mean|max)\s+)?($script:XrsOps)\s+(\S+)(?:\s+(left-layer|right-layer|left|right|quad))?\s*$") {
		$step.Kind = 'diff'; $step.A = $Matches[1]; $step.B = $Matches[2]; $step.Op = $Matches[4]; $step.Value = $Matches[5]
		$step.Metric = if ($Matches[3]) { $Matches[3] } else { 'mean' }
		$step.Eye = if ($Matches[6]) { $Matches[6] } else { 'left' }
		$d = 0.0
		if (-not [double]::TryParse($step.Value, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$d)) { throw "@diff compares with a number, got '$($step.Value)'" }
	}
	elseif ($Line -match '^@key\s+(\w+)(?:\s+(\d+))?(?:\s+(\d+))?\s*$') {
		$step.Kind = 'key'; $step.Key = $Matches[1]
		$step.Count = if ($Matches[2]) { [int]$Matches[2] } else { 1 }
		$step.DelayMs = if ($Matches[3]) { [int]$Matches[3] } else { 300 }
		if (-not (Get-XrsKeyCode $step.Key)) { throw "@key: unknown key '$($step.Key)'" }
	}
	elseif ($Line -match '^@pending\s+#(\d+)\s+(.+)$') {
		$step.Kind = 'pending'; $step.Issue = [int]$Matches[1]
		if ($Matches[2] -match '^@pending') { throw '@pending cannot be nested' }
		$step.Inner = ConvertTo-XrsStep $Matches[2] $Number
		if ($step.Inner.Kind -in 'mark', 'wait', 'sim', 'args', 'save', 'playground', 'player') { throw "@pending needs a step that can fail, not @$($step.Inner.Kind)" }
	}
	elseif ($Line -match '^@sim\s+(\S+)\s*$') { $step.Kind = 'sim'; $step.Name = $Matches[1] }
	elseif ($Line -match '^@args\s+(.+?)\s*$') { $step.Kind = 'args'; $step.Args = @($Matches[1] -split '\s+') }
	elseif ($Line -match '^@save\s+(\S+)\s*$') { $step.Kind = 'save'; $step.Name = $Matches[1] }
	elseif ($Line -match '^@playground\s*$') { $step.Kind = 'playground' }
	elseif ($Line -match '^@player\s*$') { $step.Kind = 'player' }
	else { throw "unknown or malformed directive" }
	[pscustomobject]$step
}

# Read a sequence: the steps in order, the launch information. Every syntax error is collected, with its line,
# before anything runs - a typo must not cost a game launch.
function Read-ArxSequence([string]$Name) {
	$path = Resolve-ArxSequence $Name
	$steps = @()
	$errors = @()
	$n = 0
	foreach ($raw in [IO.File]::ReadAllLines($path)) {
		$n++
		# A trailing comment is " # " (a '#' with white space on both sides; "@pending #24" and "/#x/" stay)
		$line = ($raw -replace '\s+#(\s.*)?$', '').Trim()
		if (-not $line -or $line.StartsWith('#')) { continue }
		try { $steps += ConvertTo-XrsStep $line $n } catch { $errors += "$(Split-Path -Leaf $path):$n  $line  -> $($_.Exception.Message)" }
	}
	if ($errors) { throw "sequence $path has $($errors.Count) error(s):`n  $($errors -join "`n  ")" }
	if (-not $steps) { throw "sequence $path has no steps" }
	$sim = @($steps | Where-Object Kind -eq 'sim' | Select-Object -First 1)
	$argsStep = @($steps | Where-Object Kind -eq 'args' | Select-Object -First 1)
	$save = @($steps | Where-Object Kind -eq 'save' | Select-Object -First 1)
	[pscustomobject]@{
		Path  = $path
		Name  = [IO.Path]::GetFileNameWithoutExtension($path)
		Steps = @($steps | Where-Object { $_.Kind -notin 'sim', 'args', 'save', 'playground', 'player' })
		Sim   = if ($sim) { $sim[0].Name } else { $null }
		# @( ) keeps a one-word @args an array: an if-expression unrolls it into a string, which splats char by char
		Args  = @(if ($argsStep) { $argsStep[0].Args })
		Save  = if ($save) { $save[0].Name } else { $null }
		Playground = [bool]($steps | Where-Object Kind -eq 'playground')
		Player = [bool]($steps | Where-Object Kind -eq 'player')
	}
}

# A value of status.json by a dotted path: events.byName."spell.cast", vr.sim.rig.headRot.yaw. $null when absent.
function Get-XrsValue($Object, [string]$Path) {
	$value = $Object
	foreach ($m in [regex]::Matches($Path, '"([^"]+)"|[^.]+')) {
		if ($null -eq $value) { return $null }
		$name = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Value }
		$property = $value.PSObject.Properties[$name]
		if (-not $property) { return $null }
		$value = $property.Value
	}
	return $value
}

function Format-XrsValue($Value) {
	if ($null -eq $Value) { return '<absent>' }
	if ($Value -is [bool]) { return "$Value".ToLower() }
	if ($Value -is [double] -or $Value -is [single]) { return ([double]$Value).ToString('0.####', [Globalization.CultureInfo]::InvariantCulture) }
	if ($Value -is [string] -or $Value -is [ValueType]) { return "$Value" }
	return ($Value | ConvertTo-Json -Compress -Depth 3)
}

# Does `$Actual <op> $Expected` hold? Numbers compare as numbers, anything else as text (case-insensitive).
function Test-XrsCondition($Actual, [string]$Op, [string]$Expected) {
	if ($null -eq $Actual) { return $false }
	$left = Format-XrsValue $Actual
	$a = 0.0; $e = 0.0
	$inv = [Globalization.CultureInfo]::InvariantCulture
	$numeric = [double]::TryParse($left, [Globalization.NumberStyles]::Float, $inv, [ref]$a) -and
		[double]::TryParse($Expected, [Globalization.NumberStyles]::Float, $inv, [ref]$e)
	switch ($Op) {
		'eq' { return $(if ($numeric) { $a -eq $e } else { $left -ieq $Expected }) }
		'ne' { return $(if ($numeric) { $a -ne $e } else { $left -ine $Expected }) }
		default {
			if (-not $numeric) { return $false }
			switch ($Op) { 'gt' { return $a -gt $e } 'ge' { return $a -ge $e } 'lt' { return $a -lt $e } 'le' { return $a -le $e } }
		}
	}
}

# {name} and {name+5} / {name-5} / {name*0.9} in a value: what @remember kept, with a number added or
# multiplied by one (a relative threshold: "at least 10 % less" is {name*0.9})
function Expand-XrsValue($Context, [string]$Value) {
	[regex]::Replace($Value, '\{(\w+)(?:([-+*])([\d.]+))?\}', {
		param($m)
		if (-not $Context.Vars.ContainsKey($m.Groups[1].Value)) { throw "no remembered value '$($m.Groups[1].Value)' - an @remember must come first" }
		$v = $Context.Vars[$m.Groups[1].Value]
		if (-not $m.Groups[2].Success) { return Format-XrsValue $v }
		$d = [double]::Parse($m.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
		$result = switch ($m.Groups[2].Value) { '*' { [double]$v * $d } '-' { [double]$v - $d } default { [double]$v + $d } }
		return $result.ToString('0.####', [Globalization.CultureInfo]::InvariantCulture)
	})
}

# --- arx.log since a byte mark ---------------------------------------------------------------------------------

function Read-XrsLogSince([string]$LogPath, [long]$From) {
	if (-not $LogPath -or -not (Test-Path -LiteralPath $LogPath)) { return '' }
	$fs = [IO.File]::Open($LogPath, 'Open', 'Read', 'ReadWrite')
	try {
		if ($From -gt $fs.Length) { $From = 0 }      # the log rotated under us
		$null = $fs.Seek($From, 'Begin')
		# Latin-1 on purpose: one byte is one character, so an offset into the text IS an offset into the file.
		# Decoded as UTF-8, a stray high byte becomes a 3-byte replacement character, the mark overshoots the
		# file and the next read matches lines from before the mark.
		return ([IO.StreamReader]::new($fs, [Text.Encoding]::GetEncoding(28591))).ReadToEnd()
	} finally { $fs.Close() }
}

function Get-XrsLogLength([string]$LogPath) {
	if ($LogPath -and (Test-Path -LiteralPath $LogPath)) { return (Get-Item -LiteralPath $LogPath).Length }
	return 0
}

# --- @key: a keyboard message to the game window, without the focus --------------------------------------------

$script:XrsKeys = @{ Escape = 0x1B; Enter = 0x0D; Return = 0x0D; Space = 0x20; Tab = 0x09; Backspace = 0x08
	Left = 0x25; Up = 0x26; Right = 0x27; Down = 0x28; Shift = 0x10; Control = 0x11; Ctrl = 0x11; Alt = 0x12
	Insert = 0x2D; Delete = 0x2E; Home = 0x24; End = 0x23; PageUp = 0x21; PageDown = 0x22 }

function Get-XrsKeyCode([string]$Name) {
	if ($script:XrsKeys.ContainsKey($Name)) { return [int]$script:XrsKeys[$Name] }
	if ($Name -match '^[A-Za-z]$') { return [int][char]$Name.ToUpper() }
	if ($Name -match '^\d$') { return [int][char]$Name }
	if ($Name -match '^F(\d+)$' -and [int]$Matches[1] -ge 1 -and [int]$Matches[1] -le 12) { return 0x6F + [int]$Matches[1] }
	return 0
}

function Send-XrsKey([int]$ProcessId, [string]$Key, [int]$Count, [int]$DelayMs) {
	if (-not ('ArxXrsNative' -as [type])) {
		Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ArxXrsNative {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
'@
	}
	$window = (Get-Process -Id $ProcessId -ErrorAction Stop).MainWindowHandle
	if ($window -eq [IntPtr]::Zero) { throw "@key: process $ProcessId has no window" }
	$vk = Get-XrsKeyCode $Key
	$scan = [ArxXrsNative]::MapVirtualKey([uint32]$vk, 0)
	$extended = $Key -in 'Left', 'Up', 'Right', 'Down', 'Insert', 'Delete', 'Home', 'End', 'PageUp', 'PageDown'
	$down = [int64](1 -bor ($scan -shl 16) -bor $(if ($extended) { 1 -shl 24 } else { 0 }))
	$up = [int64]($down -bor (1 -shl 30) -bor (1 -shl 31))
	for ($i = 0; $i -lt $Count; $i++) {
		$null = [ArxXrsNative]::PostMessage($window, 0x0100, [IntPtr]$vk, [IntPtr]$down)
		Start-Sleep -Milliseconds 60
		$null = [ArxXrsNative]::PostMessage($window, 0x0101, [IntPtr]$vk, [IntPtr]$up)
		if ($i -lt $Count - 1) { Start-Sleep -Milliseconds $DelayMs }
	}
}

# --- shots and the image comparison ----------------------------------------------------------------------------

# The file of a shot: <test dir>\capture\<name>-<eye>.bmp on the simulated headset, <name>.bmp otherwise
function Get-XrsShotFile([string]$TestDir, [string]$Name, [string]$Eye) {
	$dir = Join-Path $TestDir 'capture'
	foreach ($file in @((Join-Path $dir "$Name-$Eye.bmp"), (Join-Path $dir "$Name.bmp"))) {
		if (Test-Path -LiteralPath $file) { return $file }
	}
	throw "no shot '$Name' ($Eye) in $dir - a @shot $Name must come first"
}

# status.json, retried while it is being replaced (Read-ArxStatus returns nothing for a moment then)
function Read-XrsStatus([string]$Dir) {
	for ($i = 0; $i -lt 40; $i++) {
		$s = Read-ArxStatus $Dir
		if ($s) { return $s }
		Start-Sleep -Milliseconds 50
	}
	throw "no status.json in $Dir"
}

# --- running ---------------------------------------------------------------------------------------------------

function New-XrsContext([string]$TestDir, [string]$LogPath, [scriptblock]$Out) {
	[pscustomobject]@{
		TestDir = $TestDir
		LogPath = $LogPath
		LogMark = [long](Get-XrsLogLength $LogPath)
		Vars    = @{}
		Out     = if ($Out) { $Out } else { { param($Text, $Color) Write-Host $Text -ForegroundColor $Color } }
	}
}

function Write-XrsLine($Context, [string]$Text, [string]$Color = 'Gray') { & $Context.Out $Text $Color }

# Run one step; throws the reason when it fails
function Invoke-XrsStep($Context, $Step) {
	$dir = $Context.TestDir
	switch ($Step.Kind) {
		'wait' { Start-Sleep -Milliseconds $Step.Ms }
		'frames' {
			# The game holds the ack until it rendered the frames (#522): no status.json polling, no slack
			$ack = Send-ArxCommand -Lines @("waitframes $($Step.Count)") -Dir $dir -TimeoutSec 60
			Write-XrsLine $Context "      $ack" 'DarkGray'
			if ($ack -notmatch 'errors=0 ') { throw "the game refused the frame wait: $ack" }
		}
		'command' {
			$ack = Send-ArxCommand -Lines $Step.Commands -Dir $dir
			Write-XrsLine $Context "      $ack" 'DarkGray'
			if ($ack -notmatch 'errors=0 ') { throw "the game refused the command: $ack" }
		}
		'remember' {
			$value = Get-XrsValue (Read-XrsStatus $dir) $Step.Path
			if ($null -eq $value) { throw "REMEMBER FAILED: status.json has no '$($Step.Path)'" }
			$Context.Vars[$Step.Name] = $value
			Write-XrsLine $Context "      {$($Step.Name)} = $(Format-XrsValue $value)" 'DarkGray'
		}
		'assert' {
			$expected = Expand-XrsValue $Context $Step.Value
			$actual = Get-XrsValue (Read-XrsStatus $dir) $Step.Path
			if (-not (Test-XrsCondition $actual $Step.Op $expected)) { throw "ASSERT FAILED: $($Step.Path) ($(Format-XrsValue $actual)) $($Step.Op) $expected" }
			Write-XrsLine $Context "      $($Step.Path) = $(Format-XrsValue $actual)" 'DarkGray'
		}
		'until' {
			$status = $null
			$expected = Expand-XrsValue $Context $Step.Value
			try {
				$status = Wait-ArxStatus { param($s) Test-XrsCondition (Get-XrsValue $s $Step.Path) $Step.Op $expected } -TimeoutSec $Step.Seconds -Dir $dir -What "$($Step.Path) $($Step.Op) $expected"
			} catch {
				$actual = Get-XrsValue (Read-XrsStatus $dir) $Step.Path
				throw "UNTIL FAILED: $($Step.Path) ($(Format-XrsValue $actual)) never became $($Step.Op) $expected within $($Step.Seconds) s"
			}
			Write-XrsLine $Context "      $($Step.Path) = $(Format-XrsValue (Get-XrsValue $status $Step.Path))" 'DarkGray'
		}
		'mark' { $Context.LogMark = [long](Get-XrsLogLength $Context.LogPath) }
		'log' {
			$deadline = (Get-Date).AddSeconds($Step.Seconds)
			do {
				$text = Read-XrsLogSince $Context.LogPath $Context.LogMark
				$hit = [regex]::Match($text, $Step.Regex)
				if ($hit.Success) { break }
				Start-Sleep -Milliseconds 250
			} while ((Get-Date) -lt $deadline)
			if (-not $hit.Success) { throw "LOG FAILED: no log line matched /$($Step.Regex)/ within $($Step.Seconds) s of the mark" }
			$eol = $text.IndexOf("`n", $hit.Index); if ($eol -lt 0) { $eol = $text.Length }
			$bol = $text.LastIndexOf("`n", [Math]::Max(0, $hit.Index - 1)) + 1
			Write-XrsLine $Context "      matched: $($text.Substring($bol, $eol - $bol).Trim())" 'DarkGray'
			$Context.LogMark += $eol
		}
		'nolog' {
			$hit = [regex]::Match((Read-XrsLogSince $Context.LogPath $Context.LogMark), $Step.Regex)
			if ($hit.Success) { throw "NOLOG FAILED: the log matched /$($Step.Regex)/ since the mark: $($hit.Value)" }
		}
		'shot' {
			$status = Read-ArxStatus $dir
			if ($status.vr -and $status.vr.sim) {
				$ack = Send-ArxCommand -Lines @("vr-sim shot $($Step.Name)") -Dir $dir
				if ($ack -notmatch 'errors=0 ') { throw "the game refused the shot: $ack" }
				$null = Wait-ArxStatus { param($s) $s.vr.sim.shots.PSObject.Properties[$Step.Name] } -TimeoutSec 30 -Dir $dir -What "shot $($Step.Name)"
			} else {
				$seq = [int]$status.captureSeq
				$ack = Send-ArxCommand -Lines @("shot $($Step.Name)") -Dir $dir
				if ($ack -notmatch 'errors=0 ') { throw "the game refused the shot: $ack" }
				$null = Wait-ArxStatus { param($s) [int]$s.captureSeq -gt $seq } -TimeoutSec 30 -Dir $dir -What "shot $($Step.Name)"
			}
		}
		'diff' {
			$a = Get-XrsShotFile $dir $Step.A $Step.Eye
			$b = Get-XrsShotFile $dir $Step.B $Step.Eye
			$d = & (Join-Path $PSScriptRoot 'img-diff.ps1') -A $a -B $b -PassThru
			Write-XrsLine $Context "      $($Step.A) vs $($Step.B) ($($Step.Eye)): mean $($d.Mean), max channel $($d.Max), $($d.PctChanged)% of the channels changed" 'DarkGray'
			$measured = if ($Step.Metric -eq 'max') { $d.Max } else { $d.Mean }
			if (-not (Test-XrsCondition $measured $Step.Op $Step.Value)) { throw "DIFF FAILED: $($Step.A) vs $($Step.B) ($($Step.Eye)) $($Step.Metric) difference $measured $($Step.Op) $($Step.Value)" }
		}
		'key' {
			$status = Read-ArxStatus $dir
			Send-XrsKey ([int]$status.pid) $Step.Key $Step.Count $Step.DelayMs
		}
		'pending' {
			$failure = $null
			try { Invoke-XrsStep $Context $Step.Inner } catch { $failure = "$($_.Exception.Message)" }
			if ($failure) {
				Write-XrsLine $Context "      pending (#$($Step.Issue)): $failure" 'Yellow'
				$Step | Add-Member -NotePropertyName WasPending -NotePropertyValue $true -Force
			} else {
				throw "PENDING PASSED: #$($Step.Issue) now works - remove the @pending from line $($Step.N)"
			}
		}
		default { throw "step kind '$($Step.Kind)' cannot run" }
	}
}

# Run a parsed sequence against the running game. Returns Passed, the failures and the pending steps.
function Invoke-ArxSequence($Sequence, [string]$TestDir, [string]$LogPath, [switch]$ContinueOnError, [scriptblock]$Out) {
	$ctx = New-XrsContext $TestDir $LogPath $Out
	$failures = @()
	$pending = @()
	$count = $Sequence.Steps.Count
	$i = 0
	Write-XrsLine $ctx "sequence $($Sequence.Name): $count steps" 'Cyan'
	foreach ($step in $Sequence.Steps) {
		$i++
		Write-XrsLine $ctx "  [$i/$count] $($step.Line)" 'White'
		$clock = [Diagnostics.Stopwatch]::StartNew()
		try {
			Invoke-XrsStep $ctx $step
			# The step's wall time (#522): what the harness's waits cost, read off a transcript
			Write-XrsLine $ctx "      $($clock.ElapsedMilliseconds) ms" 'DarkGray'
			if ($step.Kind -eq 'pending' -and $step.PSObject.Properties['WasPending']) { $pending += $step }
		} catch {
			$reason = "line $($step.N) '$($step.Line)': $($_.Exception.Message)"
			$failures += $reason
			Write-XrsLine $ctx "      FAIL $($_.Exception.Message)" 'Red'
			if (-not $ContinueOnError) { break }
		}
	}
	[pscustomobject]@{
		Name     = $Sequence.Name
		Passed   = ($failures.Count -eq 0)
		Failures = $failures
		Pending  = @($pending | ForEach-Object { "#$($_.Issue): $($_.Inner.Line)" })
		Steps    = $count
		Ran      = $i
	}
}

# A fresh user / config directory for one sequence run (#296): CI runs every shipped sequence in its own, so what one
# sequence leaves behind (a saved file, a remapped controls ini, a changed setting) can never reach the next, and the
# order of the sequences does not matter. Whatever an earlier run left under the same name is removed first.
function New-ArxSequenceUserDir([string]$Root, [string]$Name) {
	$dir = Join-Path $Root $Name
	if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
	New-Item -ItemType Directory -Force $dir | Out-Null
	$dir
}
