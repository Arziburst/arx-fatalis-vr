<#
.SYNOPSIS
	Self-test of the selection of a pull request for deleted paths (#608): a deleted source path selects what a change
	of that path selects, not the full suite. No game, no network.

.DESCRIPTION
	ci-smoke.ps1 -Suite pr maps the changed paths through scripts\dev\xrs\impact.txt. A path that the change deletes
	often has its line removed from the map in the same change (#486 deleted src/vr/ArmFit.* and its line): it then
	matched no line and the whole suite ran (23 and 55 min). This test feeds fixture change sets with deleted paths to the
	selection, and checks the change set and the base tree's map read from a scratch git repository.

	The interface this test assumes. It was written before the implementation (contract #608, tests first); the worker
	builds it as written, in scripts\dev\ci-smoke-lib.ps1:

	Get-ArxChangedFileSet $SourceDir $Base
	  as today, { Ok; Files } - Files still lists every changed path, the deleted ones included, and a rename as its old
	  and its new path - plus
	    Deleted   the paths of Files the change deletes (repository paths with /, as git prints them); a rename's old
	              path is deleted, its new path is not. @() when Ok is $false.

	Read-ArxBaseImpactMap $SourceDir $Base [-MapPath <repository path>]
	  The impact map of the base tree, parsed like Read-ArxImpactMap (the same object: Path, Core, Rules, Errors).
	  The base tree is the merge base of $Base and HEAD, or $Base itself when there is none (a shallow checkout, as
	  Get-ArxChangedFileSet's two-dot fallback). -MapPath defaults to scripts/dev/xrs/impact.txt. Returns $null - and
	  never throws - when $Base is empty, the ref is unknown, or the base tree has no such file.

	Get-ArxImpactSelection $Map $Changed [-Deleted <paths>] [-BaseMap <map>]
	  as today, plus: a path of $Changed that is also in -Deleted and that no rule of $Map matches is matched against
	  the rules of -BaseMap (the map before the change) the same way - its names are selected with the path as the
	  reason (the reason may add words after the path), a base rule '-> *' makes it Full, '-> core' selects nothing
	  more. Only a deleted path that matches no rule of either map is Unmapped and makes it Full, as today. A path not
	  in -Deleted never looks at -BaseMap. Both default to none ($null -BaseMap: no fallback): the old selection.

	ci-smoke.ps1 -Suite pr passes the change set's Deleted as -Deleted and Read-ArxBaseImpactMap's map as -BaseMap to
	Get-ArxImpactSelection.

	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-ci-impact-deleted-test-$PID"
$localBase = Join-Path $root 'local'
New-Item -ItemType Directory -Force $localBase | Out-Null
$env:LOCALAPPDATA = $localBase
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')
. (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}
# A check whose code may throw (a missing parameter or function before the implementation): the throw is its failure
function CheckBlock([string]$Name, [scriptblock]$Block) {
	try {
		$r = & $Block
		Check $Name ([bool]$r.Ok) "$($r.Detail)"
	} catch { Check $Name $false "threw: $_" }
}
function Result([bool]$Ok, [string]$Detail) { [pscustomobject]@{ Ok = $Ok; Detail = $Detail } }
function Show($Sel) { "Full=$($Sel.Full) Names=$($Sel.Names -join ',') Unmapped=$($Sel.Unmapped -join ',') FullBecause=$($Sel.FullBecause -join '; ')" }

try {
	# --- The selection of fixture change sets ---------------------------------------------------------------------------
	Write-Host '==> the selection: deleted paths against the map and the base map'
	# The map after the change: the line of src/vr/ArmFit.* is gone (the change deletes those files), src/vr/ArmReach.*
	# is new (a rename of ArmFit.* gets it)
	$mapFile = Join-Path $root 'impact.txt'
	Set-Content $mapFile -Encoding utf8 -Value @(
		'core: smoke-test first'
		'scripts/dev/smoke-test.ps1 -> *'
		'src/vr/VRMelee.* -> melee-swing'
		'src/vr/ArmReach.* -> arm-reach'
		'docs/* -> core'
	)
	# The map before the change (the base tree's)
	$baseFile = Join-Path $root 'impact-base.txt'
	Set-Content $baseFile -Encoding utf8 -Value @(
		'core: smoke-test first'
		'scripts/dev/smoke-test.ps1 -> *'
		'src/vr/VRMelee.* -> melee-swing'
		'src/vr/ArmFit.* -> arm-fit arms-elbow'
		'src/vr/OldHarness.* -> *'
		'src/vr/OldDocs.* -> core'
		'docs/* -> core'
	)
	$map = Read-ArxImpactMap $mapFile
	$baseMap = Read-ArxImpactMap $baseFile
	Check 'the fixture maps parse without errors' (-not $map.Errors -and -not $baseMap.Errors) "$($map.Errors) $($baseMap.Errors)"

	CheckBlock 'a deleted path whose line is in the map: its sequences only, not everything' {
		$s = Get-ArxImpactSelection $map @('src/vr/VRMelee.cpp') -Deleted @('src/vr/VRMelee.cpp') -BaseMap $baseMap
		Result (-not $s.Full -and ($s.Names -join ',') -eq 'first,melee-swing,smoke-test') (Show $s)
	}
	CheckBlock 'a deleted path whose line the change removed too: the base map''s line selects its sequences, not everything (#486''s ArmFit)' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp', 'src/vr/ArmFit.h') -Deleted @('src/vr/ArmFit.cpp', 'src/vr/ArmFit.h') -BaseMap $baseMap
		Result (-not $s.Full -and ($s.Names -join ',') -eq 'arm-fit,arms-elbow,first,smoke-test' -and -not $s.Unmapped) (Show $s)
	}
	CheckBlock 'a sequence the base map selects names the deleted path as its reason' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp') -Deleted @('src/vr/ArmFit.cpp') -BaseMap $baseMap
		Result ("$($s.Reasons['arm-fit'])" -match '^src/vr/ArmFit\.cpp' -and $s.Reasons['first'] -eq 'core') "arm-fit: '$($s.Reasons['arm-fit'])'; $(Show $s)"
	}
	CheckBlock 'a deleted path in no line of either map: everything, as today, and it is named' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp', 'src/gone/Old.cpp') -Deleted @('src/vr/ArmFit.cpp', 'src/gone/Old.cpp') -BaseMap $baseMap
		Result ($s.Full -and ($s.Unmapped -join ',') -eq 'src/gone/Old.cpp' -and ($s.FullBecause -join '; ') -match 'src/gone/Old\.cpp.*no rule matches') (Show $s)
	}
	CheckBlock 'a deleted path whose removed line said -> *: everything, with the path in the reason' {
		$s = Get-ArxImpactSelection $map @('src/vr/OldHarness.cpp') -Deleted @('src/vr/OldHarness.cpp') -BaseMap $baseMap
		Result ($s.Full -and -not $s.Unmapped -and ($s.FullBecause -join '; ') -match 'src/vr/OldHarness\.cpp') (Show $s)
	}
	CheckBlock 'a deleted path whose removed line said -> core: the core alone' {
		$s = Get-ArxImpactSelection $map @('src/vr/OldDocs.md') -Deleted @('src/vr/OldDocs.md') -BaseMap $baseMap
		Result (-not $s.Full -and ($s.Names -join ',') -eq 'first,smoke-test') (Show $s)
	}
	CheckBlock 'a rename (the old path deleted, the new one added): the lines of both paths select, the old one from the base map' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp', 'src/vr/ArmReach.cpp') -Deleted @('src/vr/ArmFit.cpp') -BaseMap $baseMap
		Result (-not $s.Full -and ($s.Names -join ',') -eq 'arm-fit,arm-reach,arms-elbow,first,smoke-test') (Show $s)
	}
	CheckBlock 'a path that is not deleted never falls back to the base map: no line, everything' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp') -Deleted @() -BaseMap $baseMap
		Result ($s.Full -and ($s.Unmapped -join ',') -eq 'src/vr/ArmFit.cpp') (Show $s)
	}
	CheckBlock 'no base map (it could not be read): a deleted path in no line selects everything, no throw' {
		$s = Get-ArxImpactSelection $map @('src/vr/ArmFit.cpp') -Deleted @('src/vr/ArmFit.cpp') -BaseMap $null
		Result ($s.Full -and ($s.Unmapped -join ',') -eq 'src/vr/ArmFit.cpp') (Show $s)
	}

	# --- The change set and the base map of a git repository -----------------------------------------------------------
	Write-Host '==> the change set and the base map of a branch'
	$repo = Join-Path $root 'repo'
	New-Item -ItemType Directory -Force (Join-Path $repo 'scripts\dev\xrs'), (Join-Path $repo 'src\vr') | Out-Null
	$git = { git -C $repo -c user.name=t -c user.email=t@example.com -c core.autocrlf=false @args 2>&1 | Out-Null }
	& $git init -q -b main
	Set-Content (Join-Path $repo 'README.md') 'no map yet'
	& $git add -A
	& $git commit -q -m 'no map'
	& $git tag nomap
	Set-Content (Join-Path $repo 'src\vr\ArmFit.cpp') 'arm fit'
	Set-Content (Join-Path $repo 'src\vr\Keep.cpp') 'keep'
	Set-Content (Join-Path $repo 'src\vr\Old.cpp') "a file that is renamed`nwith enough content`nto be found as a rename"
	Set-Content (Join-Path $repo 'scripts\dev\xrs\alt-impact.txt') 'core: alt'
	Set-Content (Join-Path $repo 'scripts\dev\xrs\impact.txt') @('core: first', 'src/vr/ArmFit.* -> arm-fit', 'src/vr/Keep.* -> keep', 'src/vr/Old.* -> old', 'scripts/dev/xrs/impact.txt -> core')
	& $git add -A
	& $git commit -q -m base
	& $git checkout -q -b feature
	& $git rm -q src/vr/ArmFit.cpp
	& $git mv src/vr/Old.cpp src/vr/New.cpp
	Set-Content (Join-Path $repo 'src\vr\Keep.cpp') 'keep 2'
	Set-Content (Join-Path $repo 'scripts\dev\xrs\impact.txt') @('core: first', 'src/vr/Keep.* -> keep', 'src/vr/New.* -> new', 'scripts/dev/xrs/impact.txt -> core')
	& $git add -A
	& $git commit -q -m change

	$set = Get-ArxChangedFileSet $repo 'main'
	Check 'the change set lists every changed path, the deleted ones and both paths of a rename' ($set.Ok -and (($set.Files | Sort-Object) -join ',') -eq 'scripts/dev/xrs/impact.txt,src/vr/ArmFit.cpp,src/vr/Keep.cpp,src/vr/New.cpp,src/vr/Old.cpp') "$($set.Files -join ',')"
	CheckBlock 'the change set''s Deleted: the deleted path and a rename''s old path, nothing else' {
		Result ($set.PSObject.Properties['Deleted'] -and ((@($set.Deleted) | Sort-Object) -join ',') -eq 'src/vr/ArmFit.cpp,src/vr/Old.cpp') "Deleted: $(if ($set.PSObject.Properties['Deleted']) { $set.Deleted -join ',' } else { '(no property)' })"
	}
	CheckBlock 'a change set that could not be made has no deleted paths' {
		$bad = Get-ArxChangedFileSet $repo 'origin/nowhere'
		Result (-not $bad.Ok -and $bad.PSObject.Properties['Deleted'] -and @($bad.Deleted).Count -eq 0) "Ok=$($bad.Ok) Deleted=$(if ($bad.PSObject.Properties['Deleted']) { $bad.Deleted -join ',' } else { '(no property)' })"
	}
	CheckBlock 'the base map is the base tree''s impact.txt, not the working copy''s' {
		$b = Read-ArxBaseImpactMap $repo 'main'
		Result ($b -and -not $b.Errors -and ($b.Core -join ',') -eq 'first' -and @($b.Rules | Where-Object Glob -eq 'src/vr/ArmFit.*').Count -eq 1 -and @($b.Rules | Where-Object Glob -eq 'src/vr/New.*').Count -eq 0) "$(if ($b) { "$(@($b.Rules).Count) rules: $(@($b.Rules | ForEach-Object Glob) -join ', '); errors: $($b.Errors -join '; ')" } else { '$null' })"
	}
	CheckBlock 'the base map of another path (-MapPath)' {
		$b = Read-ArxBaseImpactMap $repo 'main' -MapPath 'scripts/dev/xrs/alt-impact.txt'
		Result ($b -and ($b.Core -join ',') -eq 'alt') "$(if ($b) { $b.Core -join ',' } else { '$null' })"
	}
	CheckBlock 'no base map, no throw: an unknown ref, no base, a base tree without the file' {
		$unknown = Read-ArxBaseImpactMap $repo 'origin/nowhere'
		$none = Read-ArxBaseImpactMap $repo ''
		$missing = Read-ArxBaseImpactMap $repo 'nomap'
		Result ($null -eq $unknown -and $null -eq $none -and $null -eq $missing) "unknown=$($null -ne $unknown) none=$($null -ne $none) missing=$($null -ne $missing)"
	}
	CheckBlock 'end to end: the branch that deletes ArmFit.cpp and its line, and renames Old.cpp, selects the lines of all its paths, not everything' {
		$s = Get-ArxImpactSelection (Read-ArxImpactMap (Join-Path $repo 'scripts\dev\xrs\impact.txt')) $set.Files -Deleted $set.Deleted -BaseMap (Read-ArxBaseImpactMap $repo 'main')
		Result (-not $s.Full -and ($s.Names -join ',') -eq 'arm-fit,first,keep,new,old') (Show $s)
	}

	# --- ci-smoke.ps1 uses them -----------------------------------------------------------------------------------------
	Write-Host '==> ci-smoke.ps1: the selection gets the deleted paths and the base map'
	$ci = Get-Content -Raw (Join-Path $PSScriptRoot 'ci-smoke.ps1')
	$calls = @([regex]::Matches($ci, 'Get-ArxImpactSelection [^\r\n]*') | ForEach-Object Value)
	Check 'ci-smoke.ps1 selects a pull request with -Deleted and -BaseMap' (@($calls | Where-Object { $_ -match '-Deleted' -and $_ -match '-BaseMap' }).Count -ge 1) ($calls -join ' | ')
	Check 'ci-smoke.ps1 reads the base tree''s map (Read-ArxBaseImpactMap)' ($ci -match 'Read-ArxBaseImpactMap')
} finally {
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-ci-impact-deleted: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-ci-impact-deleted: all checks passed' -ForegroundColor Green
exit 0
