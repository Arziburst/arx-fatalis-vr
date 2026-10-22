<#
.SYNOPSIS
	Self-test of Use-ArxSave (scripts/build-paths.ps1) and the replay start-save slots (#569, D-060).

.DESCRIPTION
	No game, no network: fixture user directories and a fixture save library in a scratch folder under the temp
	folder; %LOCALAPPDATA% and $env:ARXVR_SAVE_LIBRARY point into the scratch folder while it runs, and the library is
	always passed with -Library, so the real user directories and the player's saves are never read or written.

	The replay recorder saves at every VR session's record start into a replay slot save9000..save9004 (D-060,
	src/core/SaveSlots.h isReplaySaveSlot: exactly "save" + 9000..9004). A scripted launch in an isolated user
	directory (run-windows.ps1 -Save, smoke-test.ps1 -Save, replay.ps1) calls Use-ArxSave; on 2026-10-21 that deleted
	the whole save folder and with it save9001, the start save of the owner's recorded headset session. Checks that
	  - the library save is installed as before: the named folder with the library's files only (a folder of the same
	    name in the user directory is replaced, not merged), its file times kept
	  - every replay slot save9000..save9004 that was there is still there, with the same files: names, sizes,
	    contents and file times
	  - every other entry of the save folder is gone as before: the player's slots (save0000, a stray save0005), the
	    neighbours of the replay range (save8999, save9005) and loose files
	  - a user directory without a save folder (or without anything) gets the library save as before
	  - a user directory whose save folder holds only replay slots keeps them all and gets the library save
	  - a second Use-ArxSave keeps the replay slots again and replaces the first library save
	  - a Use-ArxSave that fails (no such save in the library) still leaves the replay slots
	Exit code 0 when everything passed, 1 otherwise. Run by run-script-tests.ps1, so CI runs it.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-use-arxsave-test-$PID"
if (Test-Path $root) { Remove-Item -Recurse -Force $root }
New-Item -ItemType Directory -Force $root | Out-Null
. (Join-Path $PSScriptRoot '..\build-paths.ps1')

$script:failed = 0
function Check([bool]$Ok, [string]$Text, $Got = $null) {
	if ($Ok) { Write-Host "  ok    $Text" }
	else { $script:failed++; Write-Host "  FAIL  $Text$(if ($null -ne $Got) { " - got: $Got" })" -ForegroundColor Red }
}
# The message of the error a script block throws, or $null when it does not throw
function Throws([scriptblock]$Block) { try { & $Block | Out-Null; $null } catch { "$($_.Exception.Message)" } }

# A save slot folder with the given files (relative path -> content), every file dated $Time
function New-Slot([string]$Dir, [hashtable]$Files, [datetime]$Time) {
	foreach ($rel in $Files.Keys) {
		$path = Join-Path $Dir $rel
		New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
		Set-Content -LiteralPath $path -Value $Files[$rel] -NoNewline
		(Get-Item -LiteralPath $path).LastWriteTime = $Time
	}
}
# Everything a slot folder holds, as one comparable string: relative path, size, UTC file time, SHA-256 per file
function Get-SlotPrint([string]$Dir) {
	if (-not (Test-Path -LiteralPath $Dir)) { return '<missing>' }
	$base = (Get-Item -LiteralPath $Dir).FullName.TrimEnd('\')
	$lines = foreach ($f in @(Get-ChildItem -LiteralPath $Dir -Recurse -File -Force | Sort-Object FullName)) {
		$rel = $f.FullName.Substring($base.Length + 1)
		"$rel|$($f.Length)|$($f.LastWriteTimeUtc.ToString('o'))|$((Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash)"
	}
	if (-not $lines) { return '<empty>' }
	$lines -join "`n"
}
# The names of the entries (folders and files) of a user directory's save folder, sorted, comma-joined
function Get-SaveEntries([string]$UserDir) {
	$saves = Join-Path $UserDir 'save'
	if (-not (Test-Path -LiteralPath $saves)) { return '<no save folder>' }
	(@(Get-ChildItem -LiteralPath $saves -Force | Select-Object -ExpandProperty Name | Sort-Object)) -join ','
}

$savedLocal = $env:LOCALAPPDATA
$savedLibrary = $env:ARXVR_SAVE_LIBRARY
$guard = Join-Path $root 'localappdata'
New-Item -ItemType Directory -Force $guard | Out-Null
try {
	# Nothing here may reach the real %LOCALAPPDATA% (user dirs, save library) even through a default
	$env:LOCALAPPDATA = $guard
	$env:ARXVR_SAVE_LIBRARY = Join-Path $guard 'no-such-library'

	# The library: save0000 (the name replay.ps1 installs a clip's save under) and mvp-new-game
	$lib = Join-Path $root 'lib'
	$libTime = [datetime]'2026-10-05 12:00:00'
	New-Slot (Join-Path $lib 'save0000') @{ 'gsave.sav' = 'library save0000'; 'save.bmp' = 'library picture' } $libTime
	New-Slot (Join-Path $lib 'mvp-new-game') @{ 'gsave.sav' = 'library mvp-new-game' } $libTime
	$libSave0000 = Get-SlotPrint (Join-Path $lib 'save0000')
	$libNewGame = Get-SlotPrint (Join-Path $lib 'mvp-new-game')

	# The replay slots' fixture content: more than gsave.sav, a subfolder, distinct times
	$replayFiles = @{ 'gsave.sav' = 'recorded start save'; 'save.bmp' = 'start picture'; 'extra\level.dat' = 'level data' }

	Write-Host 'A user directory with player slots, replay slots and strays'
	$user = Join-Path $root 'user-mixed'
	$saves = Join-Path $user 'save'
	New-Slot (Join-Path $saves 'save0000') @{ 'gsave.sav' = 'player save0000'; 'old-only.txt' = 'only in the old save0000' } ([datetime]'2026-10-10 09:00:00')
	New-Slot (Join-Path $saves 'save9001') $replayFiles ([datetime]'2026-10-21 18:30:00')
	New-Slot (Join-Path $saves 'save9004') @{ 'gsave.sav' = 'another recorded start save' } ([datetime]'2026-10-20 20:15:00')
	New-Slot (Join-Path $saves 'save0005') @{ 'gsave.sav' = 'stray save0005' } ([datetime]'2026-10-11 09:00:00')
	New-Slot (Join-Path $saves 'save8999') @{ 'gsave.sav' = 'below the replay range' } ([datetime]'2026-10-11 09:00:00')
	New-Slot (Join-Path $saves 'save9005') @{ 'gsave.sav' = 'above the replay range' } ([datetime]'2026-10-11 09:00:00')
	New-Item -ItemType Directory -Force (Join-Path $saves 'empty-folder') | Out-Null
	Set-Content -LiteralPath (Join-Path $saves 'loose.txt') -Value 'a loose file'
	$before9001 = Get-SlotPrint (Join-Path $saves 'save9001')
	$before9004 = Get-SlotPrint (Join-Path $saves 'save9004')

	$null = Use-ArxSave -Name 'save0000' -UserDir $user -Library $lib 6>&1
	Check ((Get-SlotPrint (Join-Path $saves 'save0000')) -eq $libSave0000) 'save0000 is the library save0000: its files only (not merged with the old one), file times kept' (Get-SlotPrint (Join-Path $saves 'save0000'))
	Check (-not (Test-Path (Join-Path $saves 'save0000\old-only.txt'))) 'a file only the old save0000 had is gone'
	Check ((Get-SaveEntries $user) -eq 'save0000,save9001,save9004') 'the save folder holds the library save and the two replay slots, nothing else' (Get-SaveEntries $user)
	Check ((Get-SlotPrint (Join-Path $saves 'save9001')) -eq $before9001) 'save9001 is untouched: the same files, sizes, contents and times' (Get-SlotPrint (Join-Path $saves 'save9001'))
	Check ((Get-SlotPrint (Join-Path $saves 'save9004')) -eq $before9004) 'save9004 is untouched' (Get-SlotPrint (Join-Path $saves 'save9004'))
	foreach ($gone in 'save0005', 'save8999', 'save9005', 'empty-folder', 'loose.txt') {
		Check (-not (Test-Path -LiteralPath (Join-Path $saves $gone))) "$gone is gone (not a replay slot)"
	}

	Write-Host 'A second Use-ArxSave on the same user directory'
	$null = Use-ArxSave -Name 'mvp-new-game' -UserDir $user -Library $lib 6>&1
	Check ((Get-SaveEntries $user) -eq 'mvp-new-game,save9001,save9004') 'the first library save is replaced; the replay slots stay' (Get-SaveEntries $user)
	Check ((Get-SlotPrint (Join-Path $saves 'mvp-new-game')) -eq $libNewGame) 'mvp-new-game is the library one' (Get-SlotPrint (Join-Path $saves 'mvp-new-game'))
	Check ((Get-SlotPrint (Join-Path $saves 'save9001')) -eq $before9001) 'save9001 is still untouched' (Get-SlotPrint (Join-Path $saves 'save9001'))
	Check ((Get-SlotPrint (Join-Path $saves 'save9004')) -eq $before9004) 'save9004 is still untouched' (Get-SlotPrint (Join-Path $saves 'save9004'))

	Write-Host 'A Use-ArxSave that fails'
	$failUser = Join-Path $root 'user-fail'
	$fs = Join-Path $failUser 'save'
	New-Slot (Join-Path $fs 'save0000') @{ 'gsave.sav' = 'player save0000' } ([datetime]'2026-10-10 09:00:00')
	New-Slot (Join-Path $fs 'save9001') $replayFiles ([datetime]'2026-10-21 18:30:00')
	$beforeFail = Get-SlotPrint (Join-Path $fs 'save9001')
	$message = Throws { Use-ArxSave -Name 'nonexistent' -UserDir $failUser -Library $lib 6>&1 }
	Check ($message -like "*save 'nonexistent' not found in $lib*") 'a save missing from the library fails as before, naming the library' $message
	Check ((Get-SlotPrint (Join-Path $fs 'save9001')) -eq $beforeFail) 'save9001 survives the failed call' (Get-SlotPrint (Join-Path $fs 'save9001'))

	Write-Host 'A user directory without a save folder'
	$bare = Join-Path $root 'user-bare'
	New-Item -ItemType Directory -Force $bare | Out-Null
	Set-Content -LiteralPath (Join-Path $bare 'cfg.ini') -Value '[video]'
	$null = Use-ArxSave -Name 'save0000' -UserDir $bare -Library $lib 6>&1
	Check ((Get-SaveEntries $bare) -eq 'save0000') 'the save folder is made and holds only the library save' (Get-SaveEntries $bare)
	Check ((Get-SlotPrint (Join-Path $bare 'save\save0000')) -eq $libSave0000) 'it is the library save0000'
	Check ((Get-Content -LiteralPath (Join-Path $bare 'cfg.ini')) -eq '[video]') 'the rest of the user directory is not touched'
	$none = Join-Path $root 'user-none'
	$null = Use-ArxSave -Name 'mvp-new-game' -UserDir $none -Library $lib 6>&1
	Check ((Get-SaveEntries $none) -eq 'mvp-new-game') 'a user directory that does not exist yet is made, with only the library save' (Get-SaveEntries $none)

	Write-Host 'A user directory with only replay slots'
	$replayOnly = Join-Path $root 'user-replay-only'
	$rs = Join-Path $replayOnly 'save'
	New-Slot (Join-Path $rs 'save9000') $replayFiles ([datetime]'2026-10-18 10:00:00')
	New-Slot (Join-Path $rs 'save9002') @{ 'gsave.sav' = 'replay 9002' } ([datetime]'2026-10-19 11:00:00')
	New-Slot (Join-Path $rs 'save9003') @{ 'gsave.sav' = 'replay 9003'; 'save.bmp' = 'pic 9003' } ([datetime]'2026-10-20 12:00:00')
	$beforeRo = @{}
	foreach ($n in 'save9000', 'save9002', 'save9003') { $beforeRo[$n] = Get-SlotPrint (Join-Path $rs $n) }
	$null = Use-ArxSave -Name 'save0000' -UserDir $replayOnly -Library $lib 6>&1
	Check ((Get-SaveEntries $replayOnly) -eq 'save0000,save9000,save9002,save9003') 'every replay slot stays and the library save is added' (Get-SaveEntries $replayOnly)
	Check ((Get-SlotPrint (Join-Path $rs 'save0000')) -eq $libSave0000) 'the added save is the library save0000'
	foreach ($n in 'save9000', 'save9002', 'save9003') {
		Check ((Get-SlotPrint (Join-Path $rs $n)) -eq $beforeRo[$n]) "$n is untouched" (Get-SlotPrint (Join-Path $rs $n))
	}

	# Get-ArxLoadSlotProblem: the game does not number the replay slots for --loadslot (D-060)
	Write-Host 'Get-ArxLoadSlotProblem and the replay slots'
	Check ($null -eq (Get-ArxLoadSlotProblem -ArxArgs @('--loadslot', '0') -UserDir $replayOnly)) '--loadslot 0 with the library save and replay slots: slot 0 exists'
	Check ($null -ne (Get-ArxLoadSlotProblem -ArxArgs @('--loadslot', '1') -UserDir $replayOnly)) '--loadslot 1 is a problem: the replay slots are not counted'
	$onlyReplay = Join-Path $root 'user-only-replay-slots'
	New-Slot (Join-Path $onlyReplay 'save\save9001') $replayFiles ([datetime]'2026-10-21 18:30:00')
	Check ($null -ne (Get-ArxLoadSlotProblem -ArxArgs @('--loadslot', '0') -UserDir $onlyReplay)) '--loadslot 0 with only replay slots is a problem: there is nothing to load'

	Check (@(Get-ChildItem -LiteralPath $guard -Force).Count -eq 0) 'nothing was written under %LOCALAPPDATA% (pointed into the scratch folder)' ((Get-ChildItem -LiteralPath $guard -Force -Recurse | Select-Object -ExpandProperty FullName) -join ', ')
} finally {
	$env:LOCALAPPDATA = $savedLocal
	$env:ARXVR_SAVE_LIBRARY = $savedLibrary
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failed) { Write-Host "$script:failed check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'all checks passed' -ForegroundColor Green
