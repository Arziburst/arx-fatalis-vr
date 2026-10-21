<#
.SYNOPSIS
	Lists the marks of the moment of a headset run (#551), or copies one mark folder out.

.DESCRIPTION
	During a headset test the owner holds both thumbstick clicks for 3 s to mark a moment; the game writes
	<user dir>\marks\<yyyymmdd>T<hhmmss>Z-<n>\ (the UTC time of the mark and its number in the session):
	left.png and right.png (the eye images), recorder.txt (the flight recorder's tail), clip.vrsim (the replay ring's
	last 30 s, replayable with scripts/dev/replay.ps1), meta.txt (time, area, position, yaw, build, game state, VR
	session state) and, when one was recorded, note.wav (the voice note, up to 10 s).

	Without -Copy it lists every mark, oldest first:
	  Marks: <count>
	  Mark: <folder name>
	    Time: <meta.txt time>          "unknown" without meta.txt (also Area and Position)
	    Area: <area>
	    Position: <x> <y> <z>
	    Files: <the folder's files, sorted>
	    Note: <seconds> s              the voice note's length from its WAV header; "none" without one, "broken"
	                                   when the header is not a WAV header
	With -Copy <name> -To <dir> it copies that mark's folder into <dir>\<name> ("last": the newest mark).

	Nothing it prints holds a user path or the account name (the marks go to a coordinator; the repository is
	public). Exit code 0 when it listed or copied, 1 on an error (no such mark, a copy that failed).

.PARAMETER UserDir
	The game's user directory. Default: this working copy's isolated user directory when it has marks, else the
	player's Saved Games\Arx Libertatis (the owner's headset runs).

.PARAMETER Copy
	A mark's folder name to copy out, or "last" for the newest.

.PARAMETER To
	Where -Copy puts the mark's folder (default: the current directory).

.EXAMPLE
	.\scripts\dev\marks.ps1
	.\scripts\dev\marks.ps1 -Copy last -To $env:TEMP\marks
#>
[CmdletBinding()]
param(
	[string]$UserDir,
	[string]$Copy,
	[string]$To = '.'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

$invariant = [Globalization.CultureInfo]::InvariantCulture

# What may be printed: scrubbed of user paths and the account and machine names
function Write-Clean([string]$Text) {
	Write-Output (Protect-ArxText $Text)
}

if (-not $UserDir) {
	$isolated = Get-DefaultUserDir (Join-Path $PSScriptRoot '..\..')
	$UserDir = if (Test-Path -LiteralPath (Join-Path $isolated 'marks')) { $isolated } else { Get-SharedUserDir }
}

$marksDir = Join-Path $UserDir 'marks'
$marks = @()
if (Test-Path -LiteralPath $marksDir -PathType Container) {
	$marks = @(Get-ChildItem -LiteralPath $marksDir -Directory |
		Where-Object { $_.Name -match '^\d{8}T\d{6}Z-\d+$' } |
		Sort-Object Name)
}

# The fields of a meta.txt: key=value lines after the comment
function Read-MarkMeta([string]$Folder) {
	$meta = @{}
	$file = Join-Path $Folder 'meta.txt'
	if (-not (Test-Path -LiteralPath $file)) { return $meta }
	foreach ($line in [IO.File]::ReadAllLines($file)) {
		if ($line -match '^([a-z]+)=(.*)$') { $meta[$Matches[1]] = $Matches[2] }
	}
	$meta
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
	([double]$dataBytes / $byteRate).ToString('0.0', $invariant) + ' s'
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

Write-Clean "Marks: $($marks.Count)"
foreach ($mark in $marks) {
	$meta = Read-MarkMeta $mark.FullName
	$files = @(Get-ChildItem -LiteralPath $mark.FullName -File | Sort-Object Name | ForEach-Object Name)
	$field = { param($key) if ($meta.ContainsKey($key) -and $meta[$key] -ne '') { $meta[$key] } else { 'unknown' } }
	Write-Clean "Mark: $($mark.Name)"
	Write-Clean "  Time: $(& $field 'time')"
	Write-Clean "  Area: $(& $field 'area')"
	Write-Clean "  Position: $(& $field 'position')"
	Write-Clean "  Files: $($files -join ', ')"
	Write-Clean "  Note: $(Get-NoteLength $mark.FullName)"
}
exit 0
