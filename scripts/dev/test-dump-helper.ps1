<#
.SYNOPSIS
	Self-test (#545): the dump helper (arx.exe --dump-helper) is not a game for the game lock.

.DESCRIPTION
	Every launch of the game starts a second arx.exe, the dump helper (arx.exe --dump-helper ...), which lives as long as
	its game and a little after it (it writes the report of a game that died). No lock names its pid. The game lock
	(scripts/launch-lib.ps1, D-014) must not take it for "a game outside the lock" - like the crash processor
	(arx.exe --crashinfo=...) - or every second launch on the PC would wait for a game that is not there.

	Checked with stand-ins in a scratch folder (a copy of cmd.exe named arx.exe, which keeps running whatever its command
	line says): the real locks and games of the PC are not touched.
	  - a stand-in game (no --dump-helper) is found outside the lock: the check itself sees stand-ins
	  - a stand-in dump helper (its command line has --dump-helper) is not
	  - with only the dump helper running, a launch takes the lock at once
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-dump-helper-test-$PID"
$env:ARXVR_LOCK_DIR = Join-Path $root 'locks'
$env:LOCALAPPDATA = Join-Path $root 'local'
Remove-Item Env:ARXVR_MAX_GAMES -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $env:LOCALAPPDATA | Out-Null
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}
function Wait-Until([scriptblock]$Condition, [int]$Sec = 20) {
	$deadline = (Get-Date).AddSeconds($Sec)
	while ((Get-Date) -lt $deadline) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 200 }
	$false
}

$fakeDir = Join-Path $root 'fake'
New-Item -ItemType Directory -Force $fakeDir | Out-Null
$fakeArx = Join-Path $fakeDir 'arx.exe'
Copy-Item (Join-Path $env:SystemRoot 'System32\cmd.exe') $fakeArx

# Only the stand-ins of this test count: a real game of a worker on this PC is none of its business
$realOutsideLock = ${function:Get-ArxGamesOutsideLock}
function Get-ArxGamesOutsideLock { @(& $realOutsideLock | Where-Object { $_.Path -and $_.Path.StartsWith($fakeDir, [StringComparison]::OrdinalIgnoreCase) }) }

# The stand-ins wait in a ping started by the copied cmd.exe; what follows "rem" is only there for the command line
function Start-StandIn([string]$Tail) {
	Start-Process $fakeArx -PassThru -WindowStyle Hidden -ArgumentList "/d /c ping -n 120 127.0.0.1 >nul & rem $Tail"
}
function Stop-StandIn($Process) {
	if (-not $Process) { return }
	foreach ($child in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($Process.Id)" -ErrorAction SilentlyContinue)) {
		Stop-Process -Id $child.ProcessId -Force -ErrorAction SilentlyContinue
	}
	Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
}

$game = $null
$helper = $null
try {
	Write-Host '==> a game and a dump helper outside the lock'
	$game = Start-StandIn '--loadlevel 10'
	$helper = Start-StandIn "--dump-helper --pid $($game.Id)"
	Check 'a stand-in game is a game outside the lock' (Wait-Until { @(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id).Count -eq 1 })
	$helperSeen = (Wait-Until { "$((Get-CimInstance Win32_Process -Filter "ProcessId=$($helper.Id)" -ErrorAction SilentlyContinue).CommandLine)" -match '--dump-helper' })
	Check 'the stand-in dump helper runs with --dump-helper in its command line' $helperSeen
	Check 'the dump helper is not a game outside the lock' (@(Get-ArxGamesOutsideLock | Where-Object Id -eq $helper.Id).Count -eq 0)

	Write-Host '==> only the dump helper left'
	Stop-StandIn $game
	$null = Wait-Until { @(Get-ArxGamesOutsideLock | Where-Object Id -eq $game.Id).Count -eq 0 }
	$lock = Enter-ArxGameLock -What 'test-dump-helper' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null
	Check 'a launch takes the lock while a dump helper runs' ($null -ne $lock)
	Exit-ArxGameLock $lock
} finally {
	Stop-StandIn $game
	Stop-StandIn $helper
	Start-Sleep -Milliseconds 300
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-dump-helper: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-dump-helper: all checks passed' -ForegroundColor Green
exit 0
