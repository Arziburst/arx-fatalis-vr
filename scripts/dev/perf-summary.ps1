<#
.SYNOPSIS
	Summarise one run's performance log (arx-perf.log, #327): the worst seconds, every hitch with its cause, the
	average frame budget split.

.DESCRIPTION
	Every run of the game writes arx-perf.log next to arx.log (src/vr/PerfLog.h): one line per second and one per
	hitch, a frame over 1.5 expected frame times (the display period in VR, times vr_frame_divisor). This prints:
	  - the run: seconds, frames, mean frame time, the display rate, the display slots OpenXR saw missed, hitches,
	    the log's own cost per frame
	  - the frame budget: the mean ms per frame of each phase over the run (input, channel, load, save, update,
	    script, physics, particles, render, textures, present, limit, vrwait, vrend, other) and its share
	  - the worst seconds by p95, with their three largest phases
	  - every hitch: when, how long, its cause (the phase that took the most time), missed slots, texture uploads
	    and its largest phases
	  - the marks (channel `mark` commands)
	The lines are explained in docs/vr/TESTING.md, "The performance log". Workers attach this output to their
	reports; on the owner's machine it reads the headset run that just ended.

.PARAMETER Path
	The log to read. Default: arx-perf.log in -UserDir.

.PARAMETER UserDir
	The user directory holding arx.log and arx-perf.log. Default: this working copy's isolated one when it has an
	arx-perf.log, otherwise the player's Saved Games\Arx Libertatis (the owner's headset runs).

.PARAMETER Previous
	Read an earlier run: 1 = arx-perf.1.log (the run before the last one), up to 9.

.PARAMETER Top
	How many worst seconds to list (default 5).

.EXAMPLE
	.\scripts\dev\perf-summary.ps1                      # the last run
	.\scripts\dev\perf-summary.ps1 -Previous 1          # the run before it
	.\scripts\dev\perf-summary.ps1 -Path .\evidence\arx-perf.log -Top 10
#>
[CmdletBinding()]
param(
	[string]$Path,
	[string]$UserDir,
	[ValidateRange(0, 9)][int]$Previous = 0,
	[int]$Top = 5
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

$SourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$name = if ($Previous) { "arx-perf.$Previous.log" } else { 'arx-perf.log' }
if (-not $Path) {
	if (-not $UserDir) {
		$isolated = Get-DefaultUserDir $SourceDir
		$UserDir = if (Test-Path (Join-Path $isolated $name)) { $isolated } else { Get-SharedUserDir }
	}
	$Path = Join-Path $UserDir $name
}
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
	Write-Host "perf-summary: no performance log at $Path (a build before #327, or --no-perf-log)" -ForegroundColor Red
	exit 1
}

# The game may still be writing it: read without locking it out
$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
try {
	$reader = [IO.StreamReader]::new($stream)
	$lines = $reader.ReadToEnd() -split "`r?`n"
} finally {
	$stream.Dispose()
}

$summary = Get-ArxPerfSummary (Read-ArxPerfLog $lines) -Top $Top
# Only the file's name: the output goes into reports, and the full path carries the account name
Format-ArxPerfSummary $summary -Path (Split-Path -Leaf $Path) | ForEach-Object { Write-Output $_ }
exit 0
