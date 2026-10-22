<#
.SYNOPSIS
	Lists the marks of the moment of a headset run (#551, marks v2 #574), or copies one mark folder out.

.DESCRIPTION
	During a headset test the owner presses both Index trackpads to mark a moment; the game writes
	<user dir>\marks\<yyyymmdd>T<hhmmss>Z-<n>\ (the UTC time of the mark and its number in the session).
	A SHORT press (let go within about 0.5 s) records what he sees: left.png and right.png (both eyes of the press
	frame), recorder.txt (the flight recorder's tail) and meta.txt (time, area, position, yaw, build, game state, VR
	session state, kind=press, the press frame's pose). A HOLD (up to 1 minute) records what he says and does during
	it as well: note.wav (the voice note while held), snap-+NN.Ns.jpg (the left eye at half size every 2 s after the
	press and at the release, NN.N the seconds after the press), clip.vrsim (the replay from 10 s before the press to
	the release, replayable with scripts/dev/replay.ps1; its "# hold: <pressMs> <releaseMs>" line), kind=hold with
	the press and release times in meta.txt, and transcript.txt once the note is transcribed (scripts/dev/transcribe.ps1:
	local, Russian and English, "[m:ss.s] text" lines on the press-relative timeline).

	Without -Copy it lists every mark, oldest first:
	  Marks: <count>
	  Mark: <folder name>
	    Time: <meta.txt time>          "unknown" without meta.txt (also Area and Position)
	    Area: <area>
	    Position: <x> <y> <z>
	    Kind: press | hold <s.s> s | unknown     a hold's length from its press to its release; unknown: a #551 mark
	    Files: <the folder's files, sorted>
	    Note: <seconds> s              the voice note's length from its WAV header; "none" without one, "broken"
	                                   when the header is not a WAV header
	    Clip: none | <len> s (<before> s before the press), <n> frames
	    Snapshots: <n>                 then the pictures by their time after the press:
	      +0.0 s  left.png, right.png (press)
	      +2.0 s  snap-+02.0s.jpg ...  the last one of a hold marked (release)
	    Transcript: none | not transcribed (run marks.ps1 -Transcribe) | its lines, each with the picture nearest it:
	      [0:03.1] <text>  <- snap-+04.0s.jpg
	When the transcription tools are cached (scripts/dev/transcribe.ps1 fetched them once), new notes are
	transcribed before the listing; -Transcribe also fetches the tools on first use (about 900 MB).
	With -Copy <name> -To <dir> it copies that mark's folder into <dir>\<name> ("last": the newest mark).

	Nothing it prints holds a user path or the account name (the marks go to a coordinator; the repository is
	public); transcripts stay on this machine. Exit code 0 when it listed or copied, 1 on an error (no such mark, a
	copy that failed, a -Transcribe that failed).

.PARAMETER UserDir
	The game's user directory. Default: this working copy's isolated user directory when it has marks, else the
	player's Saved Games\Arx Libertatis (the owner's headset runs).

.PARAMETER ToolsDir
	The transcription tools' cache (transcribe.ps1's). Default: %LOCALAPPDATA%\ArxFatalisVR\tools.

.PARAMETER Transcribe
	Transcribe every note without a transcript first, fetching the tools if they are not cached yet.

.PARAMETER Copy
	A mark's folder name to copy out, or "last" for the newest.

.PARAMETER To
	Where -Copy puts the mark's folder (default: the current directory).

.EXAMPLE
	.\scripts\dev\marks.ps1
	.\scripts\dev\marks.ps1 -Transcribe
	.\scripts\dev\marks.ps1 -Copy last -To $env:TEMP\marks
#>
[CmdletBinding()]
param(
	[string]$UserDir,
	[string]$ToolsDir,
	[switch]$Transcribe,
	[string]$Copy,
	[string]$To = '.'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')
. (Join-Path $PSScriptRoot 'transcribe-lib.ps1')

$invariant = [Globalization.CultureInfo]::InvariantCulture
$utf8 = [Text.UTF8Encoding]::new($false)

# What may be printed: scrubbed of user paths and the account and machine names
function Write-Clean([string]$Text) {
	Write-Output (Protect-ArxText $Text)
}

if (-not $UserDir) {
	$isolated = Get-DefaultUserDir (Join-Path $PSScriptRoot '..\..')
	$UserDir = if (Test-Path -LiteralPath (Join-Path $isolated 'marks')) { $isolated } else { Get-SharedUserDir }
}
if (-not $ToolsDir) { $ToolsDir = Get-TranscribeToolsDir }

$marksDir = Join-Path $UserDir 'marks'
function Get-Marks {
	if (-not (Test-Path -LiteralPath $marksDir -PathType Container)) { return @() }
	@(Get-ChildItem -LiteralPath $marksDir -Directory |
		Where-Object { $_.Name -match '^\d{8}T\d{6}Z-\d+$' } |
		Sort-Object Name)
}
$marks = @(Get-Marks)

# The fields of a meta.txt: key=value lines after the comment
function Read-MarkMeta([string]$Folder) {
	$meta = @{}
	$file = Join-Path $Folder 'meta.txt'
	if (-not (Test-Path -LiteralPath $file)) { return $meta }
	foreach ($line in [IO.File]::ReadAllLines($file)) {
		if ($line -match '^([A-Za-z]+)=(.*)$') { $meta[$Matches[1]] = $Matches[2] }
	}
	$meta
}

function Format-Seconds([double]$Seconds) {
	$Seconds.ToString('0.0', $invariant)
}

# note.wav's length in seconds, one decimal: "none" without it, "broken" when its header is not a WAV header
function Get-NoteLength([string]$Folder) {
	$file = Join-Path $Folder 'note.wav'
	if (-not (Test-Path -LiteralPath $file)) { return 'none' }
	$bytes = [IO.File]::ReadAllBytes($file)
	if ($bytes.Length -lt 44) { return 'broken' }
	$ascii = [Text.Encoding]::ASCII
	if ($ascii.GetString($bytes, 0, 4) -ne 'RIFF' -or $ascii.GetString($bytes, 8, 4) -ne 'WAVE') { return 'broken' }
	# The chunks after "WAVE": the byte rate in "fmt ", the size of "data"
	$byteRate = 0
	$dataBytes = -1
	$at = 12
	while ($at + 8 -le $bytes.Length) {
		$id = $ascii.GetString($bytes, $at, 4)
		$size = [BitConverter]::ToUInt32($bytes, $at + 4)
		if ($id -eq 'fmt ' -and $at + 20 -le $bytes.Length) { $byteRate = [BitConverter]::ToUInt32($bytes, $at + 16) }
		if ($id -eq 'data') { $dataBytes = $size; break }
		$at += 8 + $size + ($size % 2)
	}
	if ($byteRate -le 0 -or $dataBytes -lt 0) { return 'broken' }
	(Format-Seconds ([double]$dataBytes / $byteRate)) + ' s'
}

# Kind: press | hold <s.s> s | unknown (a #551 mark: no kind=)
function Get-MarkKind($Meta) {
	if (-not $Meta.ContainsKey('kind')) { return 'unknown' }
	if ($Meta['kind'] -ne 'hold') { return $Meta['kind'] }
	$press = 0L; $release = 0L
	if ([long]::TryParse([string]$Meta['pressMs'], [ref]$press) -and [long]::TryParse([string]$Meta['releaseMs'], [ref]$release) -and
		$press -ge 0 -and $release -ge $press) {
		return "hold $(Format-Seconds (($release - $press) / 1000.0)) s"
	}
	'hold'
}

# Clip: none | <len> s[ (<before> s before the press)], <n> frames - from the clip's frame lines and "# hold:" line
function Get-ClipSummary([string]$Folder) {
	$file = Join-Path $Folder 'clip.vrsim'
	if (-not (Test-Path -LiteralPath $file)) { return 'none' }
	$first = -1L; $last = -1L; $frames = 0; $press = -1L
	foreach ($line in [IO.File]::ReadLines($file)) {
		if ($line -match '^@(\d+)ms\b') {
			$ms = [long]$Matches[1]
			if ($first -lt 0) { $first = $ms }
			$last = $ms
			$frames++
		} elseif ($first -lt 0 -and $line -match '^#\s*hold:\s*(\d+)\s+(\d+)\s*$') {
			$press = [long]$Matches[1]
		}
	}
	$length = if ($frames -gt 0) { Format-Seconds (($last - $first) / 1000.0) } else { '0.0' }
	$before = if ($press -ge 0 -and $frames -gt 0) { " ($(Format-Seconds (($press - $first) / 1000.0)) s before the press)" } else { '' }
	"$length s$before, $frames frames"
}

# The pictures of a mark on the press-relative timeline, in tenths of a second: the press's eyes, then the snapshots
function Get-MarkPictures([string]$Folder, [string[]]$Files) {
	$pictures = @()
	$eyes = @($Files | Where-Object { $_ -eq 'left.png' -or $_ -eq 'right.png' } | Sort-Object)
	if ($eyes.Count -gt 0) { $pictures += [pscustomobject]@{ Tenths = 0; Name = $eyes[0]; Label = "$($eyes -join ', ') (press)" } }
	$snaps = @($Files | Where-Object { $_ -match '^snap-\+(\d+)\.(\d)s\.jpg$' } | ForEach-Object {
		$null = $_ -match '^snap-\+(\d+)\.(\d)s\.jpg$'
		[pscustomobject]@{ Tenths = [int]$Matches[1] * 10 + [int]$Matches[2]; Name = $_; Label = $_ }
	} | Sort-Object Tenths)
	for ($i = 0; $i -lt $snaps.Count; $i++) {
		if ($i -eq $snaps.Count - 1) { $snaps[$i].Label = "$($snaps[$i].Name) (release)" }
	}
	[pscustomobject]@{ All = @($pictures + $snaps); Snapshots = $snaps.Count }
}

# The picture nearest a time (tenths of a second after the press); a tie: the earlier one
function Get-NearestPicture($Pictures, [int]$Tenths) {
	$best = $null
	foreach ($picture in $Pictures) {
		if (-not $best -or [math]::Abs($picture.Tenths - $Tenths) -lt [math]::Abs($best.Tenths - $Tenths)) { $best = $picture }
	}
	$best
}

if ($Copy) {
	$mark = if ($Copy -eq 'last') { $marks | Select-Object -Last 1 } else { $marks | Where-Object { $_.Name -eq $Copy } | Select-Object -First 1 }
	if (-not $mark) {
		Write-Clean "No mark $Copy in the user directory's marks folder"
		exit 1
	}
	try {
		New-Item -ItemType Directory -Force $To | Out-Null
		$target = Join-Path $To $mark.Name
		New-Item -ItemType Directory -Force $target | Out-Null
		foreach ($file in Get-ChildItem -LiteralPath $mark.FullName -File) {
			Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $target $file.Name) -Force
		}
	} catch {
		Write-Clean "Could not copy $($mark.Name): $($_.Exception.Message)"
		exit 1
	}
	Write-Clean "Copied: $($mark.Name)"
	exit 0
}

# New notes transcribed first: by itself only when the tools are cached (never a download unasked)
$exitCode = 0
$untranscribed = @($marks | Where-Object {
	(Test-Path -LiteralPath (Join-Path $_.FullName 'note.wav')) -and -not (Test-Path -LiteralPath (Join-Path $_.FullName 'transcript.txt'))
})
if ($untranscribed.Count -gt 0 -and ($Transcribe -or (Test-TranscribeToolsCached $ToolsDir))) {
	$output = & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $PSScriptRoot 'transcribe.ps1') -All -UserDir $UserDir -ToolsDir $ToolsDir 2>&1
	if ($LASTEXITCODE -ne 0 -and $Transcribe) { $exitCode = 1 }
	foreach ($line in $output) { Write-Clean "$line" }
}

Write-Clean "Marks: $($marks.Count)"
foreach ($mark in $marks) {
	$meta = Read-MarkMeta $mark.FullName
	$files = @(Get-ChildItem -LiteralPath $mark.FullName -File | Sort-Object Name | ForEach-Object Name)
	$field = { param($key) if ($meta.ContainsKey($key) -and $meta[$key] -ne '') { $meta[$key] } else { 'unknown' } }
	Write-Clean "Mark: $($mark.Name)"
	Write-Clean "  Time: $(& $field 'time')"
	Write-Clean "  Area: $(& $field 'area')"
	Write-Clean "  Position: $(& $field 'position')"
	Write-Clean "  Kind: $(Get-MarkKind $meta)"
	Write-Clean "  Files: $($files -join ', ')"
	Write-Clean "  Note: $(Get-NoteLength $mark.FullName)"
	Write-Clean "  Clip: $(Get-ClipSummary $mark.FullName)"
	$pictures = Get-MarkPictures $mark.FullName $files
	Write-Clean "  Snapshots: $($pictures.Snapshots)"
	foreach ($picture in $pictures.All) {
		Write-Clean "    +$(Format-Seconds ($picture.Tenths / 10.0)) s  $($picture.Label)"
	}
	$transcript = Join-Path $mark.FullName 'transcript.txt'
	if (Test-Path -LiteralPath $transcript) {
		Write-Clean '  Transcript:'
		foreach ($line in [IO.File]::ReadAllLines($transcript, $utf8)) {
			if (-not $line.Trim()) { continue }
			$nearest = if ($line -match '^\[(\d+):(\d{2})\.(\d)\]') {
				Get-NearestPicture $pictures.All (([int]$Matches[1] * 60 + [int]$Matches[2]) * 10 + [int]$Matches[3])
			} else { $null }
			Write-Clean ("    $line" + $(if ($nearest) { "  <- $($nearest.Name)" } else { '' }))
		}
	} elseif ($files -contains 'note.wav') {
		Write-Clean '  Transcript: not transcribed (run marks.ps1 -Transcribe)'
	} else {
		Write-Clean '  Transcript: none'
	}
}
exit $exitCode
