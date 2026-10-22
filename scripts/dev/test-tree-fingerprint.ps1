<#
.SYNOPSIS
	Self-test of the build stamp's tree fingerprint (#306, #294): an edit after a build makes the build stale.

.DESCRIPTION
	Runs in a scratch git repository with a stub scripts\build-windows.ps1 that writes the stamp the way the real
	one does (Get-ArxTreeFingerprint) and counts its runs; the real builds, locks and launches are not touched.
	Checks, through Assert-ArxFreshBuild (the fresh-build check of every launch script):
	  - same HEAD, clean build, then an edit: stale; with -Rebuild it rebuilds
	  - dirty build, then a further edit: stale
	  - no change since the build (clean and dirty): fresh, no rebuild
	  - an untracked file appearing, a staged edit, a deleted file: stale
	  - a stamp without a fingerprint (an old build): stale
	  - the AddressSanitizer build directory (<Config>-asan, #618): the stale message names -Asan, -Rebuild rebuilds
	    that directory (not <Config>); -Rebuild of a directory it would not build is refused
	  - the fingerprint of the real working copy takes well under a second
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-fp-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
$env:LOCALAPPDATA = Join-Path $root 'local'
$env:ARXVR_LOCK_DIR = Join-Path $root 'locks'
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

try {
	$repo = Join-Path $root 'repo'
	New-Item -ItemType Directory -Force (Join-Path $repo 'scripts') | Out-Null
	git -C $repo init -q
	git -C $repo config user.email 'test@example.com'
	git -C $repo config user.name 'test'
	git -C $repo config core.autocrlf false
	Set-Content (Join-Path $repo 'a.cpp') 'int a;' -NoNewline
	Set-Content (Join-Path $repo 'b.cpp') 'int b;' -NoNewline
	$libPath = (Resolve-Path (Join-Path $PSScriptRoot '..\build-paths.ps1')).Path
	$counter = Join-Path $root 'builds.txt'
	# the stub build: the stamp lines of build-windows.ps1, plus a run counter
	Set-Content (Join-Path $repo 'scripts\build-windows.ps1') @"
param([string]`$Config = 'Release', [switch]`$Asan)
. '$libPath'
`$src = Split-Path -Parent `$PSScriptRoot
`$dir = "`$(Get-DefaultBuildDir `$src `$Config)`$(if (`$Asan) { '-asan' })"
New-Item -ItemType Directory -Force `$dir | Out-Null
Set-Content (Join-Path `$dir 'arx.exe') 'x'
`$fp = Get-ArxTreeFingerprint `$src
`$dirty = @(git -C `$src status --porcelain)
Set-Content (Join-Path `$dir 'built-from.txt') "`$((git -C `$src rev-parse HEAD).Trim())`$(if (`$dirty) { ' dirty' }) fp=`$fp" -NoNewline
Add-Content '$counter' 'x'
"@
	git -C $repo add -A
	git -C $repo commit -q -m init
	$head = (git -C $repo rev-parse HEAD).Trim()
	$build = Get-DefaultBuildDir $repo 'Release'
	$builds = { if (Test-Path $counter) { @(Get-Content $counter).Count } else { 0 } }
	$assert = { param([switch]$Rebuild) Assert-ArxFreshBuild -SourceDir $repo -BuildDir $build -Config Release -Head $head -Rebuild:$Rebuild 6>$null }
	$isStale = { try { & $assert | Out-Null; $false } catch { "$_" -match 'stale build' } }

	Write-Host '--- clean build, then edits'
	& (Join-Path $repo 'scripts\build-windows.ps1') | Out-Null
	Check 'clean build, no change: fresh' (-not (& $isStale))
	$n = & $builds
	& $assert -Rebuild | Out-Null
	Check 'clean build, no change, -Rebuild: no rebuild' ((& $builds) -eq $n)
	Add-Content (Join-Path $repo 'a.cpp') '// edit'
	Check 'clean build, then an edit: stale' (& $isStale)
	& $assert -Rebuild | Out-Null
	Check 'the edit, -Rebuild: rebuilt' ((& $builds) -eq $n + 1)
	Check 'after the rebuild: fresh' (-not (& $isStale))

	Write-Host '--- dirty build, then a further edit'
	Check 'dirty build, no further change: fresh' (-not (& $isStale))
	Add-Content (Join-Path $repo 'a.cpp') '// edit 2'
	Check 'dirty build, then a further edit: stale' (& $isStale)
	& $assert -Rebuild | Out-Null
	Check 'fresh after the rebuild' (-not (& $isStale))

	Write-Host '--- other kinds of change'
	Set-Content (Join-Path $repo 'new.cpp') 'int n;'
	Check 'an untracked file appears: stale' (& $isStale)
	& $assert -Rebuild | Out-Null
	Add-Content (Join-Path $repo 'new.cpp') '// more'
	Check 'an untracked file is edited: stale' (& $isStale)
	& $assert -Rebuild | Out-Null
	git -C $repo add -A
	# a file moving from untracked to staged changes the fingerprint: a spare rebuild, never a stale build called fresh
	Check 'staging an untracked file: stale (conservative)' (& $isStale)
	& $assert -Rebuild | Out-Null
	Check 'fresh after the rebuild' (-not (& $isStale))
	Remove-Item (Join-Path $repo 'b.cpp')
	Check 'a tracked file deleted: stale' (& $isStale)
	& $assert -Rebuild | Out-Null
	Set-Content (Join-Path $build 'built-from.txt') $head -NoNewline
	Check 'an old stamp without a fingerprint: stale' (& $isStale)

	Write-Host '--- the AddressSanitizer build directory (#618)'
	& $assert -Rebuild | Out-Null
	$releaseStamp = Get-Content (Join-Path $build 'built-from.txt') -Raw
	$asanBuild = "$build-asan"
	$assertAsan = { param([switch]$Rebuild) Assert-ArxFreshBuild -SourceDir $repo -BuildDir $asanBuild -Config Release -Head $head -Rebuild:$Rebuild 6>$null }
	$asanError = { try { & $assertAsan | Out-Null; '' } catch { "$_" } }
	& (Join-Path $repo 'scripts\build-windows.ps1') -Asan | Out-Null
	Check 'asan build, no change: fresh' (-not (& $asanError))
	Add-Content (Join-Path $repo 'a.cpp') '// edit 3'
	$message = & $asanError
	Check 'asan build, then an edit: stale, the message names -Asan' ($message -match 'stale build' -and $message -match 'build-windows\.ps1 -Config Release -Asan') $message
	$n = & $builds
	& $assertAsan -Rebuild | Out-Null
	Check 'the edit, -Rebuild of the asan directory: rebuilt once' ((& $builds) -eq $n + 1)
	Check 'the asan directory is fresh after the rebuild' (-not (& $asanError))
	Check 'the Release directory was not the one rebuilt' ((Get-Content (Join-Path $build 'built-from.txt') -Raw) -eq $releaseStamp)
	$other = "$build-other"
	New-Item -ItemType Directory -Force $other | Out-Null
	Set-Content (Join-Path $other 'arx.exe') 'x'
	Set-Content (Join-Path $other 'built-from.txt') $head -NoNewline
	$n = & $builds
	$message = try { Assert-ArxFreshBuild -SourceDir $repo -BuildDir $other -Config Release -Head $head -Rebuild 6>$null | Out-Null; '' } catch { "$_" }
	Check 'a directory -Rebuild would not build: refused, nothing built' ($message -match '-Rebuild builds only' -and (& $builds) -eq $n) $message

	Write-Host '--- the cost on the real working copy'
	$real = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
	$null = Get-ArxTreeFingerprint $real
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$fp = Get-ArxTreeFingerprint $real
	$sw.Stop()
	Check "the real working copy: fingerprint $fp in $($sw.ElapsedMilliseconds) ms (< 1000)" ($fp -and $sw.ElapsedMilliseconds -lt 1000)
	Check 'the same tree gives the same fingerprint' ($fp -eq (Get-ArxTreeFingerprint $real))
} finally {
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "$script:failures check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'all checks passed' -ForegroundColor Green
exit 0
