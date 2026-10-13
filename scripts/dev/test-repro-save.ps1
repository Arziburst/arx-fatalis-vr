<#
.SYNOPSIS
	Self-test of scripts/dev/repro-save.ps1 (#417): reading a reported save and preparing the repro user directory.

.DESCRIPTION
	No game, no network: fixtures in a scratch folder. Checks that
	  - the save is found in the zip of "Export a save for a bug report" (<slot>/gsave.sav next to arx.log,
	    arx-perf.log and cfg.ini), in a save folder and as a gsave.sav, with its slot name, bytes and file time
	  - a zip without a save, or with two, a missing path and a file of another kind are refused naming why
	  - the repro user directory holds exactly that save afterwards (an earlier repro's files are gone, the
	    reporter's cfg.ini is not copied) with the save's file time
	  - the arx.exe arguments load slot 0 with the test channel, on the simulated headset only when one is named
	  - the printed position is the same in every culture
	  - repro-save.ps1 itself parses, and refuses a missing file before anything starts
	Exit code 0 when everything passed, 1 otherwise. Run by run-script-tests.ps1, so CI runs it.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-repro-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
. (Join-Path $PSScriptRoot 'repro-save-lib.ps1')

$script:failed = 0
function Check([bool]$Ok, [string]$Text, $Got = $null) {
	if ($Ok) { Write-Host "  ok    $Text" }
	else { $script:failed++; Write-Host "  FAIL  $Text$(if ($null -ne $Got) { " - got: $Got" })" -ForegroundColor Red }
}
function Throws([scriptblock]$Block) { try { & $Block | Out-Null; $null } catch { "$($_.Exception.Message)" } }

# A zip like the game's export: the save in its slot folder, the three text files at the top
function New-Report([string]$Zip, [string[]]$Slots) {
	$dir = Join-Path $root ([IO.Path]::GetFileNameWithoutExtension($Zip))
	New-Item -ItemType Directory -Force $dir | Out-Null
	foreach ($slot in $Slots) {
		New-Item -ItemType Directory -Force (Join-Path $dir $slot) | Out-Null
		[IO.File]::WriteAllBytes((Join-Path $dir "$slot\gsave.sav"), [byte[]](0, 1, 2, 255, 13, 10) + [Text.Encoding]::ASCII.GetBytes($slot))
		(Get-Item (Join-Path $dir "$slot\gsave.sav")).LastWriteTime = [datetime]'2026-10-12 14:30:00'
	}
	foreach ($f in 'arx.log', 'arx-perf.log', 'cfg.ini') { Set-Content (Join-Path $dir $f) "the reporter's $f" }
	[IO.Compression.ZipFile]::CreateFromDirectory($dir, $Zip)
	$Zip
}

try {
	Write-Host 'Reading a reported save'
	$zip = New-Report (Join-Path $root 'arxvr-report-20261012-143000.zip') @('save0003')
	$save = Read-ArxReportedSave $zip
	Check ($save.Name -eq 'save0003') 'the zip: the slot folder is the name' $save.Name
	Check ([Text.Encoding]::ASCII.GetString($save.Bytes, 6, 8) -eq 'save0003' -and $save.Bytes[3] -eq 255) 'the bytes of the save, binary intact'
	Check ($save.Time -eq [datetime]'2026-10-12 14:30:00') 'its file time' $save.Time
	$folder = Join-Path $root 'arxvr-report-20261012-143000\save0003'
	Check ((Read-ArxReportedSave $folder).Name -eq 'save0003') 'a save folder'
	Check ((Read-ArxReportedSave (Join-Path $folder 'gsave.sav')).Name -eq 'save0003') 'a gsave.sav'

	$two = New-Report (Join-Path $root 'two.zip') @('save0000', 'save0001')
	Check ((Throws { Read-ArxReportedSave $two }) -like '*holds 2 saves*expected exactly one*') 'a zip with two saves is refused'
	$none = New-Report (Join-Path $root 'none.zip') @()
	Check ((Throws { Read-ArxReportedSave $none }) -like '*holds 0 saves*') 'a zip without a save is refused'
	Check ((Throws { Read-ArxReportedSave (Join-Path $root 'missing.zip') }) -like 'not found:*') 'a missing path is refused'
	Check ((Throws { Read-ArxReportedSave (Join-Path $root 'arxvr-report-20261012-143000\cfg.ini') }) -like '*neither a zip*') 'another kind of file is refused'
	Check ((Throws { Read-ArxReportedSave $root }) -like '*no gsave.sav*') 'a folder without gsave.sav is refused'

	Write-Host 'The repro user directory'
	$userDir = Join-Path $root 'userdir'
	New-Item -ItemType Directory -Force (Join-Path $userDir 'save\save0000') | Out-Null
	Set-Content (Join-Path $userDir 'save\save0000\gsave.sav') 'an earlier repro'
	Set-Content (Join-Path $userDir 'arx.log') 'an earlier log'
	$installed = Install-ArxReportedSave $save $userDir
	$saves = @(Get-ChildItem (Join-Path $userDir 'save') -Directory | ForEach-Object Name)
	Check (($saves -join ',') -eq 'save0003') 'exactly the reported save: --loadslot 0 is it' ($saves -join ',')
	Check (-not (Test-Path (Join-Path $userDir 'arx.log'))) 'the earlier repro is gone'
	Check (-not (Test-Path (Join-Path $userDir 'cfg.ini'))) "the reporter's cfg.ini is not copied"
	Check ((Get-Item (Join-Path $installed 'gsave.sav')).LastWriteTime -eq [datetime]'2026-10-12 14:30:00') 'the save keeps its file time'
	Check ([IO.File]::ReadAllBytes((Join-Path $installed 'gsave.sav')).Length -eq $save.Bytes.Length) 'and its bytes'

	Write-Host 'The launch'
	$vrArgs = Get-ArxReproArgs -UserDir 'C:\u' -TestDir 'C:\t'
	Check (($vrArgs -join ' ') -match '--loadslot 0') 'slot 0' ($vrArgs -join ' ')
	Check (($vrArgs -join ' ') -match '--user-dir "C:\\u" --config-dir "C:\\u" --test-dir "C:\\t"') 'its own user, config and test directories' ($vrArgs -join ' ')
	Check ($vrArgs -notcontains '--vr-sim' -and $vrArgs -notcontains '--no-vr') 'VR by default: neither --vr-sim nor --no-vr'
	$simArgs = Get-ArxReproArgs -UserDir 'C:\u' -TestDir 'C:\t' -SimScript 'C:\s\idle.vrsim' -DataDir 'D:\Arx'
	Check (($simArgs -join ' ') -match '--vr-sim "C:\\s\\idle.vrsim"' -and ($simArgs -join ' ') -match '--data-dir "D:\\Arx"') '-VrSim and -DataDir' ($simArgs -join ' ')

	$status = [pscustomobject]@{ area = 1; player = [pscustomobject]@{ x = 10700.04; y = 3105; z = 11400.5; yaw = 72.25 } }
	$saved = [Threading.Thread]::CurrentThread.CurrentCulture
	try {
		[Threading.Thread]::CurrentThread.CurrentCulture = 'de-DE'
		$text = Format-ArxReproPosition $status
	} finally { [Threading.Thread]::CurrentThread.CurrentCulture = $saved }
	Check ($text -eq 'area 1, player at x 10700.0 y 3105.0 z 11400.5 (yaw 72.3)' -or $text -eq 'area 1, player at x 10700.0 y 3105.0 z 11400.5 (yaw 72.2)') 'the position, the same in every culture' $text

	Write-Host 'The script'
	$tokens = $null; $errors = $null
	$null = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'repro-save.ps1'), [ref]$tokens, [ref]$errors)
	Check ($errors.Count -eq 0) 'repro-save.ps1 parses' ($errors | ForEach-Object Message)
	$out = & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $PSScriptRoot 'repro-save.ps1') (Join-Path $root 'missing.zip') -VrSim idle 2>&1 | Out-String
	Check ($LASTEXITCODE -ne 0 -and $out -match 'not found') 'a missing file fails before anything starts' $out.Trim()
} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failed) { Write-Host "$script:failed check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'all checks passed' -ForegroundColor Green
