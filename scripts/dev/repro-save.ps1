<#
.SYNOPSIS
	Load a save a player reported, in one command (#417): the zip of "Export a save for a bug report", a save folder
	or a gsave.sav.

.DESCRIPTION
	Copies the save into this working copy's own repro user directory (%LOCALAPPDATA%\ArxFatalisVR\userdirs\
	<copy>-repro, emptied first: the save is its only save, so --loadslot 0 is it), starts the game into it with the
	test channel, waits until the save is loaded and prints where it stands - the area, the player's position and
	yaw - and the tail of arx.log. Then the game stays for the developer to play; -Quit ends it at once (a scripted
	check). The reporter's arx.log, arx-perf.log and cfg.ini stay in the zip: open it to read them.

	VR by default (a save made in VR loads only in VR, D-NEW): the real headset, which needs SteamVR running - this
	script never starts it (D-014) and stops with a hint instead. -VrSim <script> runs on the simulated headset.

	Launch control (D-014, scripts/launch-lib.ps1): one launch, the game lock, the working copy's own fresh build, one
	line in the ledger.

.PARAMETER Path
	The zip, the save folder or the gsave.sav.

.PARAMETER VrSim
	The simulated headset's script (a path, or a name in scripts\dev\vrsim such as idle) instead of the real headset.

.PARAMETER Quit
	Quit the game as soon as the position is printed (the check of the export entry in save-kind-check.ps1).

.PARAMETER LogLines
	How many lines of arx.log to print (default 25).

.PARAMETER DataDir
	Arx Fatalis install folder, passed as --data-dir (default: the game finds the Steam install itself).

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER Rebuild
	Build this working copy first when its build is stale (or missing).

.PARAMETER LockTimeoutSec
	How long to wait for the game lock (default 300).

.PARAMETER LoadTimeoutSec
	How long the load may take (default 120).

.EXAMPLE
	.\scripts\dev\repro-save.ps1 "$env:USERPROFILE\Downloads\arxvr-report-20261013-153012.zip"
.EXAMPLE
	.\scripts\dev\repro-save.ps1 .\arxvr-report-20261013-153012.zip -VrSim idle
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(Mandatory, Position = 0)]
	[string]$Path,
	[string]$VrSim,
	[switch]$Quit,
	[int]$LogLines = 25,
	[string]$DataDir,
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[switch]$Rebuild,
	[int]$LockTimeoutSec = 300,
	[int]$LoadTimeoutSec = 120
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'repro-save-lib.ps1')

$save = Read-ArxReportedSave $Path
$simScript = if ($VrSim) { Resolve-ArxSimScript $VrSim } else { $null }
if (-not $simScript -and -not (Test-SteamVrRunning)) {
	# A flat run lists no VR save (D-NEW): it would start at the main menu with nothing to load
	throw 'SteamVR is not running and this script never starts it (D-014): start SteamVR, or run on the simulated headset with -VrSim idle'
}

$BuildDir = Get-DefaultBuildDir $ArxSourceDir $Config
$copy = Get-ArxWorkingCopyInfo $ArxSourceDir
$stamp = Assert-ArxFreshBuild -SourceDir $ArxSourceDir -BuildDir $BuildDir -Config $Config -Head $copy.Head -Rebuild:$Rebuild
$TestDir = Get-ArxTestDir
$UserDir = Join-Path (Get-ArxAppDataDir) "userdirs\$(Get-WorkingCopyKey $ArxSourceDir)-repro"
$log = Get-ArxLogPath $UserDir

Write-Host "repro-save: $($save.Name) from $Path" -ForegroundColor Cyan
Write-Host "game: $(if ($simScript) { "the simulated headset, $simScript" } else { 'VR, the real headset (SteamVR is running)' })"
Write-Host "build: $(ConvertTo-ShortSha $stamp) ($($copy.Branch));  user dir: $UserDir;  test channel: $TestDir"
Write-ArxLaunchBudgetNote -Copy $ArxSourceDir -Branch $copy.Branch -Launches 1

$lock = Enter-ArxGameLock -What 'repro-save.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -Vr:(-not $simScript) -TimeoutSec $LockTimeoutSec
if (-not $lock) {
	Write-Host "repro-save: another game kept the game lock for $LockTimeoutSec s (one game at a time, D-014)" -ForegroundColor Yellow
	foreach ($l in Get-ArxLockHolders) { Write-Host "  held: $l" -ForegroundColor Yellow }
	exit 2
}

$failure = $null
$process = $null
$launchTime = Get-Date
try {
	$folder = Install-ArxReportedSave $save $UserDir
	Write-Host "    save: $folder"
	New-Item -ItemType Directory -Force $TestDir | Out-Null
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt') {
		Remove-Item (Join-Path $TestDir $f) -Force -ErrorAction SilentlyContinue
	}
	$argList = Get-ArxReproArgs -UserDir $UserDir -TestDir $TestDir -SimScript $simScript -DataDir $DataDir
	Write-Host "=== arx.exe $($argList -join ' ')" -ForegroundColor Cyan
	if ($Quit) { $env:ARX_UNATTENDED = '1' }
	$launchTime = Get-Date
	$previousHint = if ($Quit) { Set-ArxBackgroundWindow } else { $null }
	try {
		$process = Start-Process -FilePath (Join-Path $BuildDir 'arx.exe') -ArgumentList $argList -PassThru `
			-RedirectStandardOutput (Join-Path $TestDir 'stdout.txt') -RedirectStandardError (Join-Path $TestDir 'stderr.txt')
	} finally { if ($Quit) { Restore-ArxBackgroundWindow $previousHint } }
	Set-ArxGameLockGame $lock $process.Id

	$status = Wait-ArxStatus { param($s) $s.state -in 'GAMEPLAY', 'MENU' -and $s.uptimeMs -gt 0 -and ($s.state -eq 'GAMEPLAY' -or $s.events.byName.'load.refused' -gt 0) } `
		-TimeoutSec $LoadTimeoutSec -Dir $TestDir -What 'the save loaded (or refused)'
	if ($status.state -ne 'GAMEPLAY') {
		$failure = "the game did not load the save: it is at the main menu ($($status.saves.kind) run, $($status.saves.hidden) save(s) of the other kind hidden - arx.log says why)"
	} else {
		# A frame or two after the load the player stands where the save put it
		Start-Sleep -Seconds 2
		$status = Read-ArxStatus $TestDir
		Write-Host "repro-save: loaded $($save.Name) - $(Format-ArxReproPosition $status)" -ForegroundColor Green
	}
	Write-Host "--- the last $LogLines lines of $log"
	Get-Content -LiteralPath $log -Tail $LogLines -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "  $_" }

	if ($Quit -or $failure) {
		$null = Send-ArxCommand -Lines @('quit') -Dir $TestDir -TimeoutSec 10
		if (-not $process.WaitForExit(60000)) { $process.Kill(); $failure = "$failure; the game did not exit within 60 s, killed".TrimStart('; ') }
	} else {
		Write-Host 'repro-save: the game is yours - this script waits until it exits' -ForegroundColor Cyan
	}
} catch {
	$failure = "$_"
} finally {
	if ($process) {
		if (-not $process.HasExited -and $failure) { $null = $process.CloseMainWindow() }
		$process.WaitForExit()
		Add-ArxLaunchRecord -Script 'repro-save.ps1' -Copy $ArxSourceDir -Branch $copy.Branch -Build $stamp -Vr (-not $simScript) -Sim ([bool]$simScript) `
			-GamePid $process.Id -ExitCode $process.ExitCode -DurationSec ((Get-Date) - $launchTime).TotalSeconds -ArxArgs @('--loadslot', '0') -Time $launchTime
	}
	Exit-ArxGameLock $lock
}

if ($failure) {
	Write-Host "repro-save: FAIL - $failure" -ForegroundColor Red
	exit 1
}
exit 0
