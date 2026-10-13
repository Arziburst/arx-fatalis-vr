<#
.SYNOPSIS
	Builds the tester zip: Arx Fatalis VR that runs on another PC without any development toolchain (#88).

.DESCRIPTION
	Takes the Release build of this working copy (builds it when it is missing or stale) and writes
	  ArxFatalisVR-<version>-<sha>.zip           arx.exe, every DLL it needs, the engine's data (data/core, copied next to
	                                             arx.exe; no data.dirs, which points into a working copy), the launchers,
	                                             HOW-TO-USE.txt, the bug-report template, collect-support.ps1, BUILD.txt
	  ArxFatalisVR-<version>-<sha>-symbols.zip   the .pdb files, kept for every tester build so crash reports can be read (D4, #132)
	into -OutDir (default: dist\ in the working copy, git-ignored). The package never contains Arx Fatalis game data.

	Checks (a failure fails the script, exit 1):
	  1. every DLL that arx.exe and the DLLs in the package import is in the package or is a Windows system DLL
	     (dumpbin /dependents); the MSVC runtime is copied app-local from the Visual Studio redistributable folder
	  2. the zip holds no data.dirs and no game data (*.pak)
	  3. unless -SkipVerify: the zip is extracted into a clean folder outside the working copy and the EXTRACTED arx.exe
	     is started flat with a PATH that holds only the Windows folders (no vcpkg, no compiler): it must reach the
	     playable state through the test channel, read its data from the extracted folder, and quit with "Clean shutdown".
	     This is one game launch (counted in the ledger; it waits for the game lock like every launch, D-014).

	The version is the first line of the VERSION file ("Arx Fatalis VR 0.1-dev") reduced to its number, so the zip name,
	the log banner and a git tag agree. A package built from a dirty tree says so in its name and BUILD.txt.

.PARAMETER Config
	Which build to package: Release (default), RelWithDebInfo.

.PARAMETER OutDir
	Where the zips go. Default: dist\ in the working copy.

.PARAMETER NoBuild
	Fail instead of building when the build is missing or stale.

.PARAMETER SkipVerify
	Do not start the extracted game (numeric checks only).

.PARAMETER DataDir
	Arx Fatalis install folder for the verification run (default: $env:ARXVR_DATA_DIR, else the game finds it in the
	Steam libraries itself, as for a tester).

.PARAMETER LockTimeoutSec
	How long the verification run waits for the game lock (default 300).

.EXAMPLE
	.\scripts\package-tester.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet('Release', 'RelWithDebInfo')]
	[string]$Config = 'Release',
	[string]$OutDir,
	[switch]$NoBuild,
	[switch]$SkipVerify,
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName System.IO.Compression.FileSystem

$SourceDir = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'dev\test-lib.ps1')
if (-not $OutDir) { $OutDir = Join-Path $SourceDir 'dist' }

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Check([bool]$Ok, [string]$Text) {
	if ($Ok) { Write-Host "  [ok]   $Text" -ForegroundColor Green } else { Write-Host "  [FAIL] $Text" -ForegroundColor Red }
}

# --- 1. A fresh build of this working copy ------------------------------------------------------------------------

$buildDir = Get-DefaultBuildDir $SourceDir $Config
$head = (git -C $SourceDir rev-parse HEAD).Trim()
$dirty = @(git -C $SourceDir status --porcelain).Count -gt 0
$stampFile = Join-Path $buildDir 'built-from.txt'
$stamp = if (Test-Path $stampFile) { (Get-Content $stampFile -Raw).Trim() } else { '' }
$stale = -not (Test-Path (Join-Path $buildDir 'arx.exe')) -or -not $stamp -or ($stamp -split ' ')[0] -ne $head -or ($dirty -and $stamp -notmatch 'dirty')
if ($stale) {
	if ($NoBuild) { throw "the build in $buildDir is missing or not from HEAD ($stamp) - build first or drop -NoBuild" }
	Write-Step "Building $Config (the build is missing or stale)"
	& (Join-Path $PSScriptRoot 'build-windows.ps1') -Config $Config
	if (-not $?) { throw 'Build failed.' }
	$stamp = (Get-Content $stampFile -Raw).Trim()
}

# --- 2. Name and version ------------------------------------------------------------------------------------------

$versionLine = (Get-Content (Join-Path $SourceDir 'VERSION') -TotalCount 1)
if ($versionLine -notmatch '(\d[\w.\-+]*)\s*$') { throw "cannot read the version from the first line of VERSION: '$versionLine'" }
$version = $Matches[1]
$sha = $head.Substring(0, 9)
$name = "ArxFatalisVR-$version-$sha$(if ($dirty) { '-dirty' })"
$branch = (git -C $SourceDir branch --show-current)
$tag = (git -C $SourceDir describe --tags --exact-match 2>$null)

$stageRoot = Join-Path $OutDir 'stage'
$stage = Join-Path $stageRoot $name
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force $stage | Out-Null
New-Item -ItemType Directory -Force $OutDir | Out-Null

# --- 3. Stage the package ------------------------------------------------------------------------------------------

Write-Step "Staging $name"
Copy-Item (Join-Path $buildDir 'arx.exe') $stage
foreach ($dll in Get-ChildItem $buildDir -Filter '*.dll' -File) { Copy-Item $dll.FullName $stage }
# xr_hello.exe is a development tool; the tester package carries only the game

# The engine's own data (data/core: localisation, misc) next to arx.exe, where RUNTIME_DATADIR "." looks for it.
# data.dirs, which CMake writes into the build directory, is left out on purpose: it names a path in the working copy.
Copy-Item -Recurse -Force (Join-Path $SourceDir 'data\core\*') $stage

$testerFiles = Join-Path $PSScriptRoot 'tester'
Copy-Item (Join-Path $testerFiles '*') $stage
foreach ($f in 'COPYING', 'AUTHORS') { if (Test-Path (Join-Path $SourceDir $f)) { Copy-Item (Join-Path $SourceDir $f) $stage } }

# --- 4. DLL closure: everything arx.exe needs is in the package or in Windows ---------------------------------------

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath } else { $null }
$dumpbin = $null
if ($vsPath) {
	$dumpbin = Get-ChildItem (Join-Path $vsPath 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $dumpbin) { throw 'dumpbin.exe not found (Visual Studio C++ tools are needed to build the package, not to run it).' }
$crtDir = $null
if ($vsPath) {
	$crtDir = Get-ChildItem (Join-Path $vsPath 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
		Sort-Object Name -Descending | ForEach-Object { Get-ChildItem (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
		Select-Object -First 1 -ExpandProperty FullName
}

$system32 = Join-Path $env:SystemRoot 'System32'
function Get-Imports([string]$Binary) {
	$out = & $dumpbin /nologo /dependents $Binary
	$inList = $false
	foreach ($line in $out) {
		if ($line -match 'Image has the following dependencies') { $inList = $true; continue }
		if ($line -match '^\s*Summary') { break }
		if ($inList -and $line -match '^\s+(\S+\.dll)\s*$') { $Matches[1] }
	}
}
# api-ms-win-* and ext-ms-* are API sets (resolved by the loader), the rest must exist in System32
function Test-IsWindowsDll([string]$Dll) {
	$Dll -match '^(api|ext)-ms-' -or (Test-Path (Join-Path $system32 $Dll))
}

Write-Step 'Checking the DLL closure (dumpbin /dependents)'
$problems = [Collections.Generic.List[string]]::new()
$copiedCrt = [Collections.Generic.List[string]]::new()
$queue = [Collections.Generic.Queue[string]]::new()
foreach ($b in (Get-ChildItem $stage -File | Where-Object { $_.Extension -in '.exe', '.dll' })) { $queue.Enqueue($b.FullName) }
$seen = @{}
while ($queue.Count) {
	$binary = $queue.Dequeue()
	if ($seen[$binary]) { continue }
	$seen[$binary] = $true
	foreach ($dll in (Get-Imports $binary)) {
		$inPackage = Join-Path $stage $dll
		if (Test-Path $inPackage) { continue }
		if ($crtDir -and (Test-Path (Join-Path $crtDir $dll))) {
			# app-local MSVC runtime (allowed by Microsoft's redistribution terms)
			Copy-Item (Join-Path $crtDir $dll) $stage
			$copiedCrt.Add($dll)
			$queue.Enqueue($inPackage)
		} elseif (-not (Test-IsWindowsDll $dll)) {
			$problems.Add("$(Split-Path -Leaf $binary) imports $dll, which is neither in the package nor a Windows DLL")
		}
	}
}
Write-Check ($problems.Count -eq 0) "every import resolves inside the package or Windows ($($seen.Count) binaries checked; MSVC runtime copied: $(if ($copiedCrt) { ($copiedCrt | Select-Object -Unique) -join ', ' } else { 'none needed' }))"
$problems | ForEach-Object { Write-Host "         $_" -ForegroundColor Red }

# The OpenXR loader must be there: without it arx.exe cannot find a headset (the exe loads it by import or at run time)
$loader = Get-ChildItem $stage -Filter 'openxr_loader*.dll' -File
Write-Check ([bool]$loader) "OpenXR loader in the package ($(if ($loader) { $loader.Name } else { 'missing' }))"
if (-not $loader) { $problems.Add('openxr_loader.dll is missing') }

# --- 5. BUILD.txt, the zips ------------------------------------------------------------------------------------------

# The exe's hash ties a crash report or a support bundle to the symbols kept for this build (D4, #132)
$exeHash = (Get-FileHash -Algorithm SHA256 (Join-Path $stage 'arx.exe')).Hash.ToLowerInvariant()
$buildTxt = @(
	"$name",
	"version: $version",
	"commit: $head$(if ($dirty) { ' (uncommitted changes in the tree)' })",
	"branch: $branch$(if ($tag) { "  tag: $tag" })",
	"config: $Config",
	"arx.exe sha256: $exeHash",
	"packaged: $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
)
[IO.File]::WriteAllText((Join-Path $stage 'BUILD.txt'), (($buildTxt -join "`r`n") + "`r`n"), [Text.UTF8Encoding]::new($false))

$zip = Join-Path $OutDir "$name.zip"
$symbolsZip = Join-Path $OutDir "$name-symbols.zip"
foreach ($z in $zip, $symbolsZip) { if (Test-Path $z) { Remove-Item -Force $z } }

# The .pdb files are not in the tester zip (large); they go into their own zip, next to it
$pdbStage = Join-Path $stageRoot 'symbols'
New-Item -ItemType Directory -Force $pdbStage | Out-Null
foreach ($pdb in Get-ChildItem $buildDir -Filter '*.pdb' -File | Where-Object { $_.BaseName -in 'arx' }) { Copy-Item $pdb.FullName $pdbStage }
Copy-Item (Join-Path $stage 'BUILD.txt') $pdbStage

Write-Step "Writing $zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
if (@(Get-ChildItem $pdbStage -Filter '*.pdb').Count) {
	[IO.Compression.ZipFile]::CreateFromDirectory($pdbStage, $symbolsZip, [IO.Compression.CompressionLevel]::Optimal, $false)
	# The symbol archive: every package's arx.pdb stays on this PC under the exe hash, whatever happens to dist\
	$archiveDir = Join-Path $env:LOCALAPPDATA "ArxFatalisVR\symbols\$exeHash"
	New-Item -ItemType Directory -Force $archiveDir | Out-Null
	Copy-Item -Force (Join-Path $pdbStage '*') $archiveDir
	Write-Host "  symbols: $symbolsZip; archived under $archiveDir"
} else {
	Write-Host "  (no arx.pdb in $buildDir - the $Config build has no debug symbols; build RelWithDebInfo for a symbols zip)" -ForegroundColor Yellow
}
Remove-Item -Recurse -Force $stageRoot

# --- 6. The zip itself ----------------------------------------------------------------------------------------------

$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { $entries = @($archive.Entries | ForEach-Object { $_.FullName }) } finally { $archive.Dispose() }
$badEntries = @($entries | Where-Object { $_ -match '(^|/)data\.dirs$' -or $_ -match '\.pak$' })
Write-Check ($badEntries.Count -eq 0) "no data.dirs and no game data (*.pak) in the zip$(if ($badEntries) { ': ' + ($badEntries -join ', ') })"
if ($badEntries) { $problems.Add("forbidden files in the zip: $($badEntries -join ', ')") }
$mustHave = 'arx.exe', 'BUILD.txt', 'HOW-TO-USE.txt', 'BUG-REPORT-TEMPLATE.md', 'collect-support.ps1', 'Collect support bundle.cmd',
	'Start Arx Fatalis VR.cmd', 'Start flat (no VR).cmd', 'misc/', 'localisation/'
$missing = @($mustHave | Where-Object { $want = $_; -not ($entries | Where-Object { $_ -eq "$name/$want" -or $_ -like "$name/$want*" }) })
Write-Check ($missing.Count -eq 0) "the zip holds the launchers, the docs and the engine data$(if ($missing) { ' - missing: ' + ($missing -join ', ') })"
if ($missing) { $problems.Add("missing from the zip: $($missing -join ', ')") }
$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "  zip: $zip ($sizeMb MB, $($entries.Count) entries)"

# --- 7. Run the extracted package ----------------------------------------------------------------------------------

$verified = '-'
if ($SkipVerify) {
	Write-Host '  verification run skipped (-SkipVerify)' -ForegroundColor Yellow
} elseif ($problems.Count) {
	Write-Host '  verification run not started: the package has problems (above)' -ForegroundColor Yellow
} else {
	Write-Step 'Verifying: extract the zip into a clean folder and start the extracted arx.exe (1 launch)'
	$verifyRoot = Join-Path ([IO.Path]::GetTempPath()) "arxvr-pkgcheck-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
	$verifyUser = Join-Path $verifyRoot 'user'
	$verifyTest = Join-Path $verifyRoot 'test'
	New-Item -ItemType Directory -Force $verifyUser, $verifyTest | Out-Null
	[IO.Compression.ZipFile]::ExtractToDirectory($zip, (Join-Path $verifyRoot 'pkg'))
	$exe = Join-Path $verifyRoot "pkg\$name\arx.exe"
	$lock = Enter-ArxGameLock -What 'package-tester.ps1 (verify)' -Copy $SourceDir -Branch $branch -TimeoutSec $LockTimeoutSec
	if (-not $lock) { throw "the game lock stayed busy for $LockTimeoutSec s - run again later (a verification that did not run is not a pass)" }
	$process = $null
	$reasons = [Collections.Generic.List[string]]::new()
	$launchTime = Get-Date
	$argList = @()
	if ($DataDir) { $argList += '--data-dir', "`"$DataDir`"" }
	$argList += '--windowed', '--dev-launch', '--no-vr', '--mute', '--test-dir', "`"$verifyTest`"", '--user-dir', "`"$verifyUser`"", '--config-dir', "`"$verifyUser`"", '--loadlevel', '10'
	$savedPath = $env:PATH
	try {
		# What a tester's PC has: Windows and nothing else - no vcpkg, no compiler, no OpenXR SDK
		$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
		$env:ARX_UNATTENDED = '1'
		$previousHint = Set-ArxBackgroundWindow
		try {
			$process = Start-Process -FilePath $exe -ArgumentList $argList -PassThru -WorkingDirectory (Split-Path -Parent $exe) `
				-RedirectStandardOutput (Join-Path $verifyTest 'stdout.txt') -RedirectStandardError (Join-Path $verifyTest 'stderr.txt')
		} finally { Restore-ArxBackgroundWindow $previousHint; $env:PATH = $savedPath }
		Set-ArxGameLockGame $lock $process.Id
		$deadline = (Get-Date).AddSeconds(90)
		$status = $null
		$reached = $false
		while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
			$status = Read-ArxStatus $verifyTest
			if ($status -and $status.state -eq 'GAMEPLAY') { $reached = $true; break }
			Start-Sleep -Milliseconds 250
		}
		Write-Check $reached "extracted game reached GAMEPLAY (last state: $(if ($status) { $status.state } else { 'no status.json' }))"
		if (-not $reached) { $reasons.Add($(if ($process.HasExited) { "the game exited with code $($process.ExitCode) before GAMEPLAY" } else { 'GAMEPLAY not reached within 90 s' })) }
		if ($reached) {
			Start-Sleep -Seconds 3
			try { $null = Send-ArxCommand -Lines @('quit') -Dir $verifyTest -TimeoutSec 10 } catch { $reasons.Add("quit was not acknowledged: $_"); $null = $process.CloseMainWindow() }
		}
		if (-not $process.WaitForExit(30000)) { $process.Kill(); $reasons.Add('the game did not quit within 30 s') }
		elseif ($reached) {
			Write-Check ($process.ExitCode -eq 0) "exit code $($process.ExitCode)"
			if ($process.ExitCode -ne 0) { $reasons.Add("exit code $($process.ExitCode)") }
		}
		$log = Join-Path $verifyUser 'arx.log'
		$clean = (Test-Path $log) -and [bool](Select-String -Path $log -Pattern 'Clean shutdown' -Quiet)
		Write-Check $clean "'Clean shutdown' in arx.log"
		if ($reached -and -not $clean) { $reasons.Add("no 'Clean shutdown' in arx.log") }
		if (Test-Path $log) {
			$errors = @(Select-String -Path $log -Pattern '^\[E\]' | Select-Object -First 5)
			Write-Check ($errors.Count -eq 0) "no [E] lines in arx.log$(if ($errors) { ': ' + ($errors[0].Line.Trim()) })"
			if ($errors) { $reasons.Add("[E] lines in arx.log, first: $($errors[0].Line.Trim())") }
			# The engine data must come from the package, not from a leftover path of a working copy
			$srcLines = @(Select-String -Path $log -SimpleMatch -Pattern $SourceDir | Select-Object -First 2)
			Write-Check ($srcLines.Count -eq 0) "arx.log never mentions the working copy ($SourceDir)$(if ($srcLines) { ': ' + $srcLines[0].Line.Trim() })"
			if ($srcLines) { $reasons.Add('arx.log mentions the working copy path - the package still depends on it') }
		}
		$crashes = @(Get-ChildItem (Join-Path $verifyUser 'crashes') -Directory -ErrorAction SilentlyContinue)
		Write-Check ($crashes.Count -eq 0) "no crash report$(if ($crashes) { ': ' + $crashes[0].Name })"
		if ($crashes) { $reasons.Add('crash report written') }
		$stderr = if (Test-Path (Join-Path $verifyTest 'stderr.txt')) { (Get-Content (Join-Path $verifyTest 'stderr.txt') -Raw) } else { '' }
		if ($stderr) { Write-Host "  --- stderr:`n$stderr" }
	} catch {
		$reasons.Add("$_")
		if ($process -and -not $process.HasExited) { try { $process.Kill() } catch { } }
	} finally {
		$env:PATH = $savedPath
		if ($process) {
			Add-ArxLaunchRecord -Script 'package-tester.ps1' -Copy $SourceDir -Branch $branch -Build $stamp -Vr $false -GamePid $process.Id `
				-ExitCode $(if ($process.HasExited) { $process.ExitCode } else { $null }) -DurationSec ((Get-Date) - $launchTime).TotalSeconds -ArxArgs $argList -Time $launchTime
		}
		Exit-ArxGameLock $lock
	}
	if ($reasons.Count) {
		$reasons | ForEach-Object { $problems.Add("verification: $_") }
		$verified = 'FAIL'
		Write-Host "  evidence kept in $verifyRoot" -ForegroundColor Yellow
	} else {
		$verified = 'ok'
		Remove-Item -Recurse -Force $verifyRoot -ErrorAction SilentlyContinue
	}
}

# --- 8. Verdict --------------------------------------------------------------------------------------------------------

Write-Host ''
if ($problems.Count) {
	Write-Host "PACKAGE FAIL ($($problems.Count) problem(s)):" -ForegroundColor Red
	$problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
	exit 1
}
Write-Host "PACKAGE OK: $zip ($sizeMb MB), extracted-game run: $verified" -ForegroundColor Green
if ($env:GITHUB_OUTPUT) { Add-Content $env:GITHUB_OUTPUT "zip=$zip" }
exit 0
