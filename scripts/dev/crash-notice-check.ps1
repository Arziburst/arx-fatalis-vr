<#
.SYNOPSIS
	Crash notice check (#545, M6): a report left by the last run is announced once at the main menu, never again after OK.

.DESCRIPTION
	With the dump helper on, a crash shows no "Fatal Error" dialog. At the next launch the main menu (flat, and on the
	VR panel) says "The game ended with an error last time. A report was saved." with [Open folder] and [OK]: once per
	new report, never again after OK (stage 3 adds "Send the report?" to the same notice).

	The interface (tests/platform/CrashNoticeTest.h has the rule of which reports are new):
	  - status.json "crashNotice": { "state": "shown" | "none", "report": "<folder name in crashes\>" | "" }
	  - test channel: "crashnotice ok" presses OK, "crashnotice open" presses Open folder (not used here: it opens
	    Explorer)
	  - OK writes notice-shown.txt into every report folder the notice was about (<user dir>\crashes\<name>\); a folder
	    with that file is never announced again; hangs\ is never announced; a folder without report.txt is no report

	Two flat launches to the main menu (smoke-test.ps1 -Menu, D-014) in a fresh user directory of their own
	(%LOCALAPPDATA%\ArxFatalisVR\userdirs\<working copy>-crash-notice), with fixture reports planted before the first:
	  crashes\2026-10-01-10-00-00-00   report.txt              not dismissed
	  crashes\2026-10-01-11-00-00-00   report.txt              not dismissed: the newest such - the one the notice names
	  crashes\2026-10-01-12-00-00-00   report.txt + marker     dismissed before
	  crashes\2026-10-01-13-00-00-00   no report.txt           not a report
	  hangs\2026-10-01-14-00-00-00     report.txt (kind=hang)  the game ran on: no notice
	  1. the notice is shown and names 2026-10-01-11-00-00-00; "crashnotice ok" hides it; the run passes the smoke test
	  2. the markers: in 10 and 11 (and still 12), not in 13, not in hangs\; every report file kept
	  3. a second launch: the menu without the notice
	Exit code 0 on PASS, 1 on FAIL, 2 when a launch was skipped (the game lock stayed busy).

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.EXAMPLE
	.\scripts\dev\crash-notice-check.ps1
#>
[CmdletBinding()]
param(
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$userDir = Join-Path (Get-ArxAppDataDir) ('userdirs\' + (Get-WorkingCopyKey $ArxSourceDir) + '-crash-notice')
$testDir = Get-ArxTestDir
$crashes = Join-Path $userDir 'crashes'
$hangs = Join-Path $userDir 'hangs'
$marker = 'notice-shown.txt'

$script:failed = 0
$script:passed = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green; $script:passed++ }
	else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failed++ }
}

function New-FixtureReport([string]$Dir, [string]$Kind, [switch]$NoReport, [switch]$Shown) {
	New-Item -ItemType Directory -Force $Dir | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $Dir 'crash.dmp'), [byte[]](0x4D, 0x44, 0x4D, 0x50) + [byte[]]::new(60))
	if (-not $NoReport) {
		Set-Content -LiteralPath (Join-Path $Dir 'report.txt') -Encoding utf8 -Value @("kind=$Kind", 'exit=-1073741819 (0xc0000005)', 'killer=1000 main thread', 'module=arx.exe+0x10', 'dump=crash.dmp', 'stack:')
	}
	if ($Shown) { Set-Content -LiteralPath (Join-Path $Dir $marker) -Encoding ascii -Value 'shown' }
}

# One launch to the main menu with a sequence written here; returns the smoke test's exit code
function Invoke-NoticeRun([string]$Name, [string[]]$Sequence) {
	New-Item -ItemType Directory -Force $testDir | Out-Null
	$file = Join-Path $testDir "crash-notice-$Name.xrs"
	Set-Content -LiteralPath $file -Encoding utf8 -Value (@("# Written by crash-notice-check.ps1 (#545): $Name") + $Sequence)
	$params = @{ Menu = $true; UserDir = $userDir; Sequence = $file }
	if ($Rebuild -and -not $script:rebuilt) { $params.Rebuild = $true; $script:rebuilt = $true }
	Write-Host "==> $Name" -ForegroundColor Cyan
	& $smoke @params *>&1 | ForEach-Object { Write-Host "  $_" }
	$LASTEXITCODE
}

Remove-Item -LiteralPath $userDir -Recurse -Force -ErrorAction SilentlyContinue
New-FixtureReport (Join-Path $crashes '2026-10-01-10-00-00-00') 'crash'
New-FixtureReport (Join-Path $crashes '2026-10-01-11-00-00-00') 'abort'
New-FixtureReport (Join-Path $crashes '2026-10-01-12-00-00-00') 'crash' -Shown
New-FixtureReport (Join-Path $crashes '2026-10-01-13-00-00-00') 'crash' -NoReport
New-FixtureReport (Join-Path $hangs '2026-10-01-14-00-00-00') 'hang'

$previousReport = $env:ARXVR_AUTOREPORT
$env:ARXVR_AUTOREPORT = '0'
$script:rebuilt = $false
try {
	$exit = Invoke-NoticeRun 'shown' @(
		'@until state eq MENU 30s'
		'@assert menuMode eq mainmenu'
		'@until crashNotice.state eq shown 15s'
		'@assert crashNotice.report eq 2026-10-01-11-00-00-00'
		'crashnotice ok'
		'@until crashNotice.state eq none 5s'
		'@frames 10'
		'@assert crashNotice.state eq none'
	)
	if ($exit -eq 2) { Write-Host 'crash notice check: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; exit 2 }
	Check ($exit -eq 0) "launch 1: the notice names the newest new report, OK hides it, the run passes (smoke-test exit $exit)"

	Write-Host '==> the markers'
	foreach ($name in '2026-10-01-10-00-00-00', '2026-10-01-11-00-00-00', '2026-10-01-12-00-00-00') {
		Check (Test-Path -LiteralPath (Join-Path (Join-Path $crashes $name) $marker) -PathType Leaf) "crashes\$name\$marker after OK"
	}
	Check (-not (Test-Path -LiteralPath (Join-Path (Join-Path $crashes '2026-10-01-13-00-00-00') $marker))) 'no marker in a folder without report.txt'
	Check (-not (Test-Path -LiteralPath (Join-Path (Join-Path $hangs '2026-10-01-14-00-00-00') $marker))) 'no marker in hangs\'
	foreach ($name in '2026-10-01-10-00-00-00', '2026-10-01-11-00-00-00', '2026-10-01-12-00-00-00') {
		$dir = Join-Path $crashes $name
		Check ((Test-Path -LiteralPath (Join-Path $dir 'report.txt')) -and (Test-Path -LiteralPath (Join-Path $dir 'crash.dmp'))) "crashes\$name keeps its report and dump"
	}

	$exit = Invoke-NoticeRun 'gone' @(
		'@until state eq MENU 30s'
		'@assert menuMode eq mainmenu'
		'@frames 60'
		'@assert crashNotice.state eq none'
	)
	if ($exit -eq 2) { Write-Host 'crash notice check: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; exit 2 }
	Check ($exit -eq 0) "launch 2: no notice after OK (smoke-test exit $exit)"
} finally {
	$env:ARXVR_AUTOREPORT = $previousReport
}

Write-Host ''
if ($script:failed -eq 0) { Write-Host "crash notice check: PASS ($($script:passed) checks)" -ForegroundColor Green; exit 0 }
Write-Host "crash notice check: FAIL ($($script:failed) of $($script:failed + $script:passed) checks)" -ForegroundColor Red
exit 1
