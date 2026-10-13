<#
.SYNOPSIS
	VR saves and flat saves never see each other (#417, D-NEW), and a save exported for a bug report loads again:
	three game runs on one user directory, then the export's zip checked and loaded with repro-save.ps1.

.DESCRIPTION
	The sequences are scripts\dev\xrs\save-kinds\*.xrs, each run by smoke-test.ps1 with its @sim / @args / @save:
	  1. the simulated headset from the test save mvp-items (a VR save of the library): it lists 1 save, saves
	     vr-made (the VR mark), opens the saves page, the saves folder (logged, not opened) and exports mvp-items
	     for a bug report (into <test dir>\reports, not the Desktop: a test run)
	  2. flat (--no-vr) on the same user directory with --loadslot 0: both VR saves hidden, the load refused (the log
	     line), the main menu; a new game's save flat-made is a flat save and listed
	  3. the simulated headset again with --loadslot 2: the flat save hidden although it is the newest, the load of
	     slot 2 refused (the log line); slot 0 is vr-made
	Between the runs this script checks what the sequences cannot see: the startup's refusal lines in arx.log, and the
	zip - exactly mvp-items/gsave.sav (byte for byte the library's save), arx.log, arx-perf.log and cfg.ini, with no
	personal data in the three text files (Find-ArxPrivateData of the bug filing, #387). Then
	  4. repro-save.ps1 <the zip> -VrSim idle -Quit: the save loads where the library says mvp-items stands.
	Four launches (D-014: the game lock, the working copy's own fresh build, never SteamVR); CI runs it in its
	sequences phase.

	Exit code 0 when everything passed, 1 when something failed, 2 when a run was skipped (the game lock stayed busy).

.PARAMETER Config
	Which build to run: Release (default), RelWithDebInfo or Debug.

.PARAMETER DataDir
	Arx Fatalis install folder, passed as --data-dir (default: the game finds the Steam install itself).

.PARAMETER LockTimeoutSec
	How long each run waits for the game lock (default 300).

.PARAMETER SaveLibrary
	The test save library holding mvp-items (default: Get-ArxSaveLibrary).

.EXAMPLE
	.\scripts\dev\save-kind-check.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$DataDir,
	[int]$LockTimeoutSec = 300,
	[string]$SaveLibrary
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not $SaveLibrary) { $SaveLibrary = Get-ArxSaveLibrary }
$UserDir = Join-Path (Get-ArxAppDataDir) "userdirs\$(Get-WorkingCopyKey $ArxSourceDir)-savekinds"
$TestDir = Get-ArxTestDir
$reports = Join-Path $TestDir 'reports'
$log = Get-ArxLogPath $UserDir
$dataArgs = if ($DataDir) { @{ DataDir = $DataDir } } else { @{} }

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Ok, [string]$Text, $Got = $null) {
	if ($Ok) { Write-Host "  [ok]   $Text" -ForegroundColor Green }
	else { $failures.Add("$Text$(if ($null -ne $Got) { " - got: $Got" })"); Write-Host "  [FAIL] $Text$(if ($null -ne $Got) { " - got: $Got" })" -ForegroundColor Red }
}

# One part: its sequence through smoke-test.ps1, with the launch information the sequence declares
function Invoke-Part([string]$Name) {
	$seq = Read-ArxSequence (Join-Path $PSScriptRoot "xrs\save-kinds\$Name.xrs")
	$p = @{ Config = $Config; Sequence = $seq.Path; UserDir = $UserDir; LockTimeoutSec = $LockTimeoutSec; SaveLibrary = $SaveLibrary } + $dataArgs
	if ($seq.Sim) { $p.VrSim = $seq.Sim }
	if ($seq.Save) { $p.Save = $seq.Save }
	if ($seq.Args) { $p.ArxArgs = @($seq.Args) }
	# A refused --loadslot comes up at the main menu
	if (-not $seq.Save) { $p.Menu = $true }
	Write-Host "==> save-kind-check part $Name" -ForegroundColor Cyan
	& (Join-Path $PSScriptRoot 'smoke-test.ps1') @p
	$code = $LASTEXITCODE
	if ($code -eq 2) { Write-Host "save-kind-check: SKIPPED - the game lock stayed busy (part $Name)" -ForegroundColor Yellow; exit 2 }
	Check ($code -eq 0) "part $Name passed (smoke-test.ps1 exit $code)"
	$code -eq 0
}

function Test-LogLine([string]$Pattern, [string]$Text) {
	$line = Select-String -LiteralPath $log -Pattern $Pattern -ErrorAction SilentlyContinue | Select-Object -First 1
	Check ([bool]$line) $Text $(if (-not $line) { "no line matching '$Pattern' in $log" })
	if ($line) { Write-Host "         $($line.Line.Trim())" }
}

if (Test-Path -LiteralPath $UserDir) { Remove-Item -LiteralPath $UserDir -Recurse -Force }
if (Test-Path -LiteralPath $reports) { Remove-Item -LiteralPath $reports -Recurse -Force }

# --- 1. VR saves, the saves page --------------------------------------------------------------------------------
if (Invoke-Part '1-vr-saves') {
	$zips = @(Get-ChildItem -LiteralPath $reports -Filter 'arxvr-report-*.zip' -ErrorAction SilentlyContinue)
	Check ($zips.Count -eq 1) "one zip in $reports" $zips.Count
	if ($zips.Count -eq 1) {
		$zipPath = $zips[0].FullName
		$zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
		try {
			$names = @($zip.Entries | ForEach-Object FullName)
			Check (($names -join ' ') -eq 'mvp-items/gsave.sav arx.log arx-perf.log cfg.ini') 'the zip holds the save, arx.log, arx-perf.log and cfg.ini' ($names -join ' ')
			foreach ($entry in $zip.Entries) {
				$stream = $entry.Open(); $buffer = [IO.MemoryStream]::new(); $stream.CopyTo($buffer); $stream.Dispose()
				if ($entry.Name -eq 'gsave.sav') {
					$library = [IO.File]::ReadAllBytes((Join-Path $SaveLibrary 'mvp-items\gsave.sav'))
					Check ([Linq.Enumerable]::SequenceEqual([byte[]]$buffer.ToArray(), [byte[]]$library)) 'the save is the library''s mvp-items, byte for byte'
				} else {
					$text = [Text.Encoding]::UTF8.GetString($buffer.ToArray())
					$leaks = @(Find-ArxPrivateData $text)
					Check ($text.Length -gt 0 -and $leaks.Count -eq 0) "$($entry.Name): $($text.Length) characters, no personal data" ($leaks -join ', ')
				}
			}
		} finally { $zip.Dispose() }
	}
}

# --- 2. Flat on the same user directory ------------------------------------------------------------------------
if (Invoke-Part '2-flat-run') {
	Test-LogLine '--loadslot 0 refused: 0 save\(s\) of this flat run; 2 vr save\(s\) in the save folder are hidden' 'the flat run refused --loadslot 0 of a VR save'
	Test-LogLine 'Hiding save save0000 "vr-made": a VR save, and this run is flat' 'and hid vr-made'
}

# --- 3. VR again --------------------------------------------------------------------------------------------------
if (Invoke-Part '3-vr-run') {
	Test-LogLine '--loadslot 2 refused: 2 save\(s\) of this vr run; 1 flat save\(s\) in the save folder are hidden' 'the VR run refused --loadslot 2 (only two VR saves)'
	Test-LogLine 'Hiding save save0001 "flat-made": a flat save \(no VR mark\), and this run is in VR' 'and hid the newer flat save'
}

# --- 4. The exported zip loads where the save stands ------------------------------------------------------------
if ($zipPath) {
	Write-Host "==> save-kind-check part 4-repro: repro-save.ps1 $zipPath -VrSim idle -Quit" -ForegroundColor Cyan
	$out = & (Join-Path $PSScriptRoot 'repro-save.ps1') $zipPath -VrSim idle -Quit -Config $Config -LockTimeoutSec $LockTimeoutSec @dataArgs 6>&1 | ForEach-Object { "$_" }
	$code = $LASTEXITCODE
	$out | ForEach-Object { Write-Host "  $_" }
	if ($code -eq 2) { Write-Host 'save-kind-check: SKIPPED - the game lock stayed busy (part 4)' -ForegroundColor Yellow; exit 2 }
	Check ($code -eq 0) "repro-save.ps1 loaded the zip (exit $code)"
	$want = Get-Content -Raw (Join-Path $SaveLibrary 'manifest.json') | ConvertFrom-Json | Where-Object name -eq 'mvp-items'
	$line = $out | Where-Object { $_ -match 'loaded mvp-items - area (\d+), player at x (\S+) y (\S+) z (\S+)' } | Select-Object -First 1
	if ($line -and $want) {
		$null = $line -match 'loaded mvp-items - area (\d+), player at x (\S+) y (\S+) z (\S+)'
		$c = [Globalization.CultureInfo]::InvariantCulture
		$got = @([double]::Parse($Matches[2], $c), [double]::Parse($Matches[3], $c), [double]::Parse($Matches[4], $c))
		$off = [math]::Sqrt([math]::Pow($got[0] - $want.player.x, 2) + [math]::Pow($got[1] - $want.player.y, 2) + [math]::Pow($got[2] - $want.player.z, 2))
		Check ([int]$Matches[1] -eq [int]$want.area) "the same area as the library's mvp-items ($($want.area))" $Matches[1]
		Check ($off -le 5) ('the same position: {0:0.0} units from where the library made it' -f $off)
	} else {
		Check $false 'repro-save.ps1 printed where the save stands, and the library knows mvp-items' $line
	}
}

Write-Host ''
if ($failures.Count) {
	foreach ($f in $failures) { Write-Host "  [FAIL] $f" -ForegroundColor Red }
	Write-Host 'save-kind-check: FAIL' -ForegroundColor Red
	exit 1
}
Write-Host 'save-kind-check: PASS - VR and flat saves never see each other; the exported save loads where it was made' -ForegroundColor Green
exit 0
