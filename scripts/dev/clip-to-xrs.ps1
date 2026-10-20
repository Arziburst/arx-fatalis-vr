<#
.SYNOPSIS
	Cut up to 30 s of a recorded VR session (#550) into a simulator script and a skeleton CI sequence.

.DESCRIPTION
	A clip (arx-replay.log, .log.gz, a mark's or a report's .vrsim; the format: tests/vr/ReplayClipTest.h) holds a whole
	session. A CI sequence may hold at most one short clip (coordinator Tests and CI rule 11), so this writes:

	  <OutDir>\<Name>.vrsim   a simulator script the CI harness runs as it is: comment lines naming the source clip's
	                          file (never its folder), the window, the build, the seed and the save folder; "rate <the
	                          clip's rate> cap" and "timeline gameplay"; "@0ms ..." with the whole state at From - the last
	                          value at or before From of everything the clip sets (poses, curls, aims, actions, hand
	                          on/off, fingers on/off, profile, session, player); then the clip's frame lines of (From, To],
	                          each with its own commands, re-based to the window's start. Stripped: no ipd and no floor
	                          line (the player's eye distance and height), none of the clip's own comments, no anchor
	                          lines (the streaming player's), and no user path or account name anywhere.
	  <OutDir>\<Name>.xrs     a skeleton sequence (xrs-lib's Read-ArxSequence parses it): "@sim <Name>", "@save" with a
	                          placeholder scenario and a comment naming the clip's save folder (a test save library
	                          scenario must take its place), "@args --loadslot 0 --seed <seed>", a wait for the timeline
	                          ("@until vr.sim.pending eq 0") - the worker adds the asserts.

	Refused (exit 1, nothing written): a window over 30 s, ending before it starts or outside the clip, a file that is
	no clip. Exit 0 when both files were written.

.PARAMETER Clip
	The clip: .log, .log.gz or .vrsim.

.PARAMETER From
	The window's start on the clip's clock, ms.

.PARAMETER To
	The window's end, ms (To - From <= 30000).

.PARAMETER Name
	The name of the sequence and the script (letters, digits, - and _).

.PARAMETER OutDir
	Where both files go (default: scripts\dev\vrsim for the script and scripts\dev\xrs for the sequence).

.EXAMPLE
	.\scripts\dev\clip-to-xrs.ps1 "$env:LOCALAPPDATA\ArxFatalisVR\userdirs\<key>\arx-replay.log.gz" -From 12000 -To 22000 -Name replay-chest
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory, Position = 0)]
	[string]$Clip,
	[Parameter(Mandatory)]
	[long]$From,
	[Parameter(Mandatory)]
	[long]$To,
	[Parameter(Mandatory)]
	[string]$Name,
	[string]$OutDir
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

$inv = [Globalization.CultureInfo]::InvariantCulture

function Fail([string]$Message) {
	Write-Host "clip-to-xrs: $Message" -ForegroundColor Red
	exit 1
}

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

# What a command sets: the key its last value is kept under for the state at From ($null: not state)
function Get-CommandKey([string[]]$W) {
	$word = $W[0].ToLowerInvariant()
	switch ($word) {
		'head' { if ($W.Count -eq 7) { return 'head' } elseif ($W.Count -eq 5) { return "head $($W[1].ToLowerInvariant())" } else { return $null } }
		'hand' {
			if ($W.Count -lt 3) { return $null }
			$side = $W[1].ToLowerInvariant()
			$what = $W[2].ToLowerInvariant()
			if ($what -eq 'aim') { return "hand $side aim" }
			if ($what -in 'on', 'off') { return "hand $side tracked" }
			if ($what -eq 'fingers') { if ($W.Count -eq 4 -and $W[3] -in 'on', 'off') { return "hand $side joints" } else { return "hand $side fingers" } }
			if ($what -in 'pos', 'rot') { return "hand $side $what" }
			if ($W.Count -eq 8) { return "hand $side" }
			return $null
		}
		'profile' { return 'profile' }
		'session' { return 'session' }
		'action' { if ($W.Count -ge 4) { return "action $($W[1]) $($W[2].ToLowerInvariant())" } else { return $null } }
		'player' { return 'player' }
		default { return $null }
	}
}

if ($Name -notmatch '^[A-Za-z0-9_-]{1,64}$') { Fail 'the name is letters, digits, - and _' }
if (-not (Test-Path -LiteralPath $Clip -PathType Leaf)) { Fail 'no clip at the given path' }
if ($To -lt $From) { Fail "the window ends ($To ms) before it starts ($From ms)" }
if ($To - $From -gt 30000) { Fail "the window is $($To - $From) ms: a CI sequence holds at most 30 s of a clip (rule 11)" }

$clipName = Split-Path -Leaf $Clip
$lines = @((Read-ClipText $Clip) -split "`r?`n")
$header = @{}
$frames = [Collections.Generic.List[object]]::new()
$firstMs = -1
$lastMs = -1
foreach ($line in $lines) {
	if ($line -match '^@(\d+)ms\s+(.*)$') {
		$t = [long]$Matches[1]
		$commands = @(($Matches[2] -split ';') | ForEach-Object { ($_.Trim() -replace '\s+', ' ') } | Where-Object { $_ })
		if ($commands.Count -and $commands[0] -match '^anchor\b') { continue }
		if ($firstMs -lt 0) { $firstMs = $t }
		$lastMs = $t
		$frames.Add([pscustomobject]@{ T = $t; Commands = $commands })
	} elseif ($frames.Count -eq 0 -and $line -match '^# (build|save|seed|rate|mark): (.*)$') {
		$header[$Matches[1]] = $Matches[2].Trim()
	}
}
if ($frames.Count -eq 0) { Fail "$clipName is no clip: it has no frame line (@<ms>ms ...)" }
if ($From -gt $lastMs) { Fail "the window starts at $From ms, after the clip's last frame ($lastMs ms)" }
if ($To -lt $firstMs) { Fail "the window ends at $To ms, before the clip's first frame ($firstMs ms)" }

$seed = if ("$($header['seed'])" -match '^\d+$') { $header['seed'] } else { '0' }
$rate = if ("$($header['rate'])" -match '^\d+(\.\d+)?$') { $header['rate'] } else { '90' }
$saveFolder = if ("$($header['save'])" -match '^(\S+)') { $Matches[1] } else { 'none' }
$build = Protect-ArxText "$($header['build'])"

# The state at From: the last value at or before it of everything the clip sets, in the order first seen
$state = [ordered]@{}
foreach ($frame in $frames) {
	if ($frame.T -gt $From) { break }
	foreach ($command in $frame.Commands) {
		$key = Get-CommandKey @($command -split ' ')
		if ($key) { $state[$key] = $command }
	}
}

$script = [Collections.Generic.List[string]]::new()
$script.Add("# $Name`: $($To - $From) ms of the clip $clipName ($From-$To ms on its clock), cut by scripts/dev/clip-to-xrs.ps1 (#550)")
$script.Add("# build: $build; seed: $seed; save: $saveFolder (the test save library scenario of the sequence takes its place)")
$script.Add("rate $rate cap")
$script.Add('timeline gameplay')
if ($state.Count) { $script.Add("@0ms " + (@($state.Values) -join '; ')) }
foreach ($frame in $frames) {
	if ($frame.T -le $From -or $frame.T -gt $To) { continue }
	if (-not $frame.Commands.Count) { continue }
	$script.Add("@$($frame.T - $From)ms " + ($frame.Commands -join '; '))
}

$sequence = @(
	"# $Name`: a sequence cut from the clip $clipName ($From-$To ms) by scripts/dev/clip-to-xrs.ps1 (#550): the recorded"
	'# head, hands, fingers and inputs replayed on the simulated headset. The asserts are the worker''s.'
	"# The clip's save: $saveFolder (made at its record start) - a test save library scenario must take its place in @save."
	"@sim $Name"
	'@save mvp-new-game'
	"@args --loadslot 0 --seed $seed"
	''
	'@until state eq GAMEPLAY 60s'
	"@until vr.sim.pending eq 0 $([int][math]::Ceiling(($To - $From) / 1000.0) + 30)s"
	'@assert errors eq 0'
)

$scriptDir = if ($OutDir) { $OutDir } else { Join-Path $PSScriptRoot 'vrsim' }
$sequenceDir = if ($OutDir) { $OutDir } else { Join-Path $PSScriptRoot 'xrs' }
New-Item -ItemType Directory -Force $scriptDir | Out-Null
New-Item -ItemType Directory -Force $sequenceDir | Out-Null
$scriptFile = Join-Path $scriptDir "$Name.vrsim"
$sequenceFile = Join-Path $sequenceDir "$Name.xrs"
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($scriptFile, (($script | ForEach-Object { Protect-ArxText $_ }) -join "`n") + "`n", $utf8)
[IO.File]::WriteAllText($sequenceFile, (($sequence | ForEach-Object { Protect-ArxText $_ }) -join "`n") + "`n", $utf8)

$timed = @($script | Where-Object { $_ -match '^@\d+ms ' }).Count
Write-Host "clip-to-xrs: $Name.vrsim ($timed timed lines, $($To - $From) ms of $clipName) and $Name.xrs written"
exit 0
