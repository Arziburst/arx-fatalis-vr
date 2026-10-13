<#
.SYNOPSIS
	Exit hook check (#146): end the game the way #146 ended and check that arx.log names how, why and who.

.DESCRIPTION
	One launch on the simulated headset (smoke-test.ps1 -Sequence, no runtime, no SteamVR, every launch rule of
	D-014; level 10). The sequence lets the simulated runtime end the session (vr-sim session exiting, the game goes
	on flat), then ends the process from inside with vr-sim fault exit-process <code> (ExitProcess, as a library
	does - the #146 shape) or, with -Fault exit, std::exit. The smoke run itself FAILS - the process vanished
	without "Clean shutdown", which is the point - and this script then checks the evidence:

	  - the process ended with the requested exit code (the smoke test's own reading)
	  - arx.log has the exit hook's [E] ExitTrace lines: the way out (ExitProcess / exit()),
	    the last cause noted before it (the runtime ending the session, "exiting"),
	    and the calling stack with arx.exe in it

	Automatic bug filing is off for the run (ARXVR_AUTOREPORT=0): the [E] lines are deliberate.
	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).

.PARAMETER Fault
	exit-process (default): ExitProcess(<code>), seen by the TLS callback; exit: std::exit(<code>), seen by the
	atexit handler first.

.PARAMETER Code
	The exit code to end with (default -1, the code of #146).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\exit-trace-check.ps1
.EXAMPLE
	.\scripts\dev\exit-trace-check.ps1 -Fault exit -Code 3
#>
[CmdletBinding()]
param(
	[ValidateSet('exit-process', 'exit')]
	[string]$Fault = 'exit-process',
	[int]$Code = -1,
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
New-Item -ItemType Directory -Force $testDir | Out-Null
$sequence = Join-Path $testDir 'exit-trace-check.xrs'
Set-Content -LiteralPath $sequence -Encoding utf8 -Value @"
# Written by exit-trace-check.ps1: the runtime ends the session, then the process ends from inside (#146)
@sim idle
@args --loadlevel 10
@until state eq GAMEPLAY 60s
@until vr.sessionState eq focused 30s
vr-sim session exiting
@log OpenXR session ended by the runtime \(exiting\) 10s
vr-sim fault $Fault $Code
@log ExitTrace:0  The process is ending outside the game's own shutdown 10s
"@

# smoke-test.ps1 takes the launch from its parameters (the @sim / @args lines are for xrs-run.ps1)
$params = @{ Sequence = $sequence; VrSim = 'idle' }
if ($Rebuild) { $params.Rebuild = $true }
$previousReport = $env:ARXVR_AUTOREPORT
$env:ARXVR_AUTOREPORT = '0'
Write-Host "==> exit hook check: vr-sim fault $Fault $Code after the runtime ended the session (the smoke run must FAIL)"
try {
	# *>&1: the smoke test reports with Write-Host (the information stream)
	$output = & (Join-Path $PSScriptRoot 'smoke-test.ps1') @params *>&1 | ForEach-Object { Write-Host $_; "$_" }
	$smokeExit = $LASTEXITCODE
} finally {
	$env:ARXVR_AUTOREPORT = $previousReport
}
if ($smokeExit -eq 2) { Write-Host 'exit hook check: SKIPPED (the smoke test was skipped)' -ForegroundColor Yellow; exit 2 }

$log = Get-ArxLogPath
$lines = @(if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log | Where-Object { $_ -match '^\[E\] ExitTrace:0  ' } })
$via = if ($Fault -eq 'exit') { 'exit\(\) was called' } else { 'ExitProcess' }
$checks = [ordered]@{
	"the process ended with exit code $Code" = [bool]($output -match "exited on its own with code $Code\b")
	"arx.log: the way out ($Fault)" = [bool]($lines -match "outside the game's own shutdown \(#146\): $via")
	'arx.log: the cause noted before it' = [bool]($lines -match 'Last cause noted before it: OpenXR session ended by the runtime \(exiting\), \d+ ms earlier')
	'arx.log: the stack with arx.exe in it' = [bool]($lines -match 'Called from \(innermost first\): .*arx\.exe\+0x[0-9a-f]+')
}

Write-Host '  --- the exit hook in arx.log:'
$lines | ForEach-Object { Write-Host "  $_" }
$failed = 0
foreach ($name in $checks.Keys) {
	if ($checks[$name]) { Write-Host "  [ok]   $name" -ForegroundColor Green } else { Write-Host "  [FAIL] $name" -ForegroundColor Red; $failed++ }
}
if ($failed -eq 0) { Write-Host 'exit hook check: PASS' -ForegroundColor Green; exit 0 }
Write-Host "exit hook check: FAIL ($failed of $($checks.Count) checks)" -ForegroundColor Red
exit 1
