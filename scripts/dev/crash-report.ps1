<#
.SYNOPSIS
	The one command for a crash, hang or kill (#545): for the newest report, print the kind, the exit code,
	the killer thread, the faulting module and the stack, the last 100 flight-recorder events and the
	resource trend - with no user path or name in the output.

.DESCRIPTION
	The dump helper writes a report folder for every end of the game that is not a clean quit:

	  <user dir>\crashes\<utc>\   the game ended: a crash, abort, terminate, heap corruption, an exit from outside, a kill
	  <user dir>\hangs\<utc>\     the hang watchdog's report of a stall (the game ran on)

	Each folder holds report.txt (key=value lines, then "stack:" and the stack), arx-recorder.txt (the flight
	recorder, tests/platform/FlightRecorderTest.h), arx-perf.log (the run's performance log) and, when the game
	was still there to dump, the minidump named by dump=.

	This prints one report. With a cdb.exe it symbolises the dump's stack; without one (or without a dump) it
	prints the stack report.txt already holds (the game symbolised arx.exe's frames with DbgHelp at the crash).
	Nothing it prints holds a user path, the account name or the machine name (Protect-ArxText,
	scripts/dev/log-digest-lib.ps1), even when a report file does. Covered by scripts/dev/test-crash-report.ps1;
	scripts/dev/flight-recorder-check.ps1 runs it on the real reports.

.PARAMETER UserDir
	The user directory holding crashes\ and hangs\. Default: this working copy's isolated user directory when it
	has reports, otherwise the player's Saved Games\Arx Libertatis (the owner's headset runs).

.PARAMETER Path
	A report folder to read instead of the newest one.

.PARAMETER Cdb
	The debugger to symbolise the dump with; "none" never runs one. Default: a cdb.exe that is installed (the
	Windows SDK debuggers or WinDbg), else none.

.EXAMPLE
	.\scripts\dev\crash-report.ps1
.EXAMPLE
	.\scripts\dev\crash-report.ps1 -Path <folder> -Cdb none
#>
[CmdletBinding()]
param(
	[string]$UserDir,
	[string]$Path,
	[string]$Cdb
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

# Every line out goes through the scrubber: a report file may hold a path the public repo must not (#387)
function Emit([string]$Line = '') {
	Write-Output (Protect-ArxText "$Line")
}

# Read a file the game may still hold open (a hang: the game runs on), without locking it out
function Read-ReportLines([string]$File) {
	if (-not (Test-Path -LiteralPath $File)) { return @() }
	try {
		$stream = [IO.FileStream]::new($File, 'Open', 'Read', 'ReadWrite, Delete')
		try { @(([IO.StreamReader]::new($stream)).ReadToEnd() -split "`r?`n") } finally { $stream.Dispose() }
	} catch {
		@()
	}
}

# A report folder's name is its UTC time yyyy-MM-dd-HH-mm-ss-NN, so the newest sorts last by name
function Get-ReportFolders([string]$Dir) {
	@(Get-ChildItem -LiteralPath $Dir -Directory -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -match '^\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'report.txt')) })
}

function Resolve-NewestReport([string]$Root) {
	$folders = @(Get-ReportFolders (Join-Path $Root 'crashes')) + @(Get-ReportFolders (Join-Path $Root 'hangs'))
	if (-not $folders.Count) { return $null }
	($folders | Sort-Object Name | Select-Object -Last 1).FullName
}

# A cdb.exe that is installed, or $null
function Find-Cdb {
	$found = Get-Command cdb.exe -ErrorAction SilentlyContinue
	if ($found) { return $found.Source }
	foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
		if (-not $base) { continue }
		$candidate = Join-Path $base 'Windows Kits\10\Debuggers\x64\cdb.exe'
		if (Test-Path -LiteralPath $candidate) { return $candidate }
	}
	$null
}

# --- Resolve the report ------------------------------------------------------------------------------

if ($Path) {
	$folder = $Path
	if (-not (Test-Path -LiteralPath (Join-Path $folder 'report.txt'))) {
		Emit 'No crash or hang report at the given path.'
		exit 1
	}
} else {
	if (-not $UserDir) {
		$isolated = Get-DefaultUserDir (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
		$UserDir = if (Resolve-NewestReport $isolated) { $isolated } else { Get-SharedUserDir }
	}
	$folder = Resolve-NewestReport $UserDir
	if (-not $folder) {
		Emit 'No crash or hang report found.'
		exit 1
	}
}

# --- report.txt: key=value fields, then the stack ----------------------------------------------------

$reportLines = Read-ReportLines (Join-Path $folder 'report.txt')
$fields = [ordered]@{}
$stack = @()
$inStack = $false
foreach ($line in $reportLines) {
	if ($inStack) { if ($line.Trim()) { $stack += $line }; continue }
	if ($line -eq 'stack:') { $inStack = $true; continue }
	if ($line -match '^([a-z]+)=(.*)$') { $fields[$Matches[1]] = $Matches[2] }
}

Emit "Report: $(Split-Path -Leaf $folder)"
Emit "Kind: $($fields['kind'])"
Emit "Exit code: $($fields['exit'])"
Emit "Killer thread: $($fields['killer'])"
if ($fields.Contains('exception') -and $fields['exception']) { Emit "Exception: $($fields['exception'])" }
Emit "Faulting module: $($fields['module'])"

# --- The stack: cdb if there is one and a dump, else report.txt's own --------------------------------

if (-not $PSBoundParameters.ContainsKey('Cdb') -or $Cdb -eq '') { $Cdb = Find-Cdb }
$dumpName = "$($fields['dump'])"
$dumpFile = if ($dumpName -and $dumpName -ne 'none') { Join-Path $folder $dumpName } else { $null }
$haveDump = [bool]$dumpFile -and (Test-Path -LiteralPath $dumpFile -PathType Leaf)
$cdbExe = if ($Cdb -and $Cdb -ne 'none') { $Cdb } else { $null }

if ($cdbExe -and $haveDump) {
	Emit 'Stack (cdb):'
	# -z <dump> -c "<commands>; q": the faulting thread's stack with source lines, then quit. The symbols: the
	# arx.pdb archived under the exe's hash (report.txt exehash=, scripts/launch-lib.ps1 Save-ArxSymbolArchive)
	$cdbArgs = @('-z', $dumpFile, '-lines', '-n')
	$exeHash = "$($fields['exehash'])".Trim().ToLowerInvariant()
	if ($exeHash -match '^[0-9a-f]{64}$') {
		$symbolDir = Join-Path $env:LOCALAPPDATA "ArxFatalisVR\symbols\$exeHash"
		if (Test-Path -LiteralPath (Join-Path $symbolDir 'arx.pdb')) { $cdbArgs += @('-y', $symbolDir) }
	}
	$cdbArgs += @('-c', '~#k; q')
	$cdbOutput = @()
	try {
		$cdbOutput = @(& $cdbExe @cdbArgs 2>&1 | ForEach-Object { "$_" })
	} catch {
		$cdbOutput = @("(cdb failed: $_)")
	}
	foreach ($line in $cdbOutput) {
		if ("$line".Trim()) { Emit "  $line" }
	}
} else {
	Emit 'Stack (report):'
	foreach ($line in $stack) { Emit $line }
}

# --- The last flight-recorder events -----------------------------------------------------------------

$eventRx = '^\d+\.\d{3} \d+ [a-z]+ -?\d+( .*)?$'
$events = @(Read-ReportLines (Join-Path $folder 'arx-recorder.txt') | Where-Object { $_ -match $eventRx })
if ($events.Count) {
	$last = @($events | Select-Object -Last 100)
	Emit "Last $($last.Count) events (arx-recorder.txt, newest last):"
	foreach ($event in $last) { Emit "  $event" }
}

# --- The resource trend ------------------------------------------------------------------------------

$perfLines = Read-ReportLines (Join-Path $folder 'arx-perf.log')
if ($perfLines.Count) {
	$trend = Get-ArxResourceTrend (Read-ArxPerfLog $perfLines).Resources
	if (@($trend.Keys).Count) {
		Emit 'Resource trend (arx-perf.log; first / last / max / slope per minute / largest step):'
		foreach ($row in Format-ArxResourceRows $trend) { Emit $row }
	}
}

exit 0
