<#
.SYNOPSIS
	Self-test of the equal-tree skip of a push to main (#541): the decision whether the push run repeats the game checks
	the merge gate already passed on the same tree. No network, no GitHub, no game.

.DESCRIPTION
	Feeds scripts/dev/ci-skip-equal-tree.ps1 fixture run lists (the JSON of gh run list) and a fake tree lookup, and checks
	its decision. The last part uses the default lookup (git) in a scratch repository.

	The interface this test assumes. It was written before the implementation (contract #541, tests first); the worker
	builds it as written:

	scripts/dev/ci-skip-equal-tree.ps1 -HeadTree <tree sha> -RunsJson <path> [-TreeOf <scriptblock>]
	  -HeadTree    the tree of the pushed commit (in the workflow: git rev-parse 'HEAD^{tree}')
	  -RunsJson    a file with the output of
	                 gh run list --workflow build-windows.yml --event workflow_dispatch --status success
	                   --json databaseId,headSha,displayTitle,conclusion,event,createdAt
	               (an array of runs, any order). The file may be missing, empty, or hold an error text or a JSON object
	               instead of an array when gh failed.
	  -TreeOf      { param($Sha) ... } -> the tree sha of the commit $Sha, or $null / '' when unknown; may throw.
	               Default: git rev-parse --verify --quiet "$Sha^{tree}" in the current directory (unknown -> $null).
	               In CI the gate's head commit is not in a depth-1 checkout: the workflow fetches it or passes its own
	               lookup (e.g. the GitHub API's commit tree).
	  Output: exactly one object on the pipeline (log lines may go to Write-Host):
	    [pscustomobject]@{ Decision = 'skip' | 'run'; RunId = <databaseId of the matching run> | $null; Reason = <string> }
	    skip: RunId is the run's databaseId and Reason contains it (the workflow prints Reason as the log line).
	    run:  RunId is $null and Reason is not empty (why the game checks run).
	  Never throws; a run decision is not an error.

	Which runs count: every listed run with event 'workflow_dispatch', conclusion 'success', suite full and a headSha
	(the listing's size is the workflow's choice, e.g. gh run list --limit 30; the helper takes the whole file). The
	single runner often runs a merged PR's push run after the next PR's gate, so the head's tree need not be the latest
	gate's. Any such run whose tree (TreeOf headSha) equals -HeadTree -> skip, naming that run; when several match, the
	latest of them by createdAt. No match, no such run, or anything that cannot be read -> run (fail safe). A run whose
	tree cannot be read (the lookup throws or finds nothing) does not match; the others are still compared. Runs of
	another suite, of another event, not successful, without a headSha are ignored - they never match.

	The suite of a run (the access point). gh run list does not show a run's inputs, so the workflow names the suite in
	the title of a manual run, and the helper reads it from displayTitle:
	  build-windows.yml    run-name: ${{ github.event_name == 'workflow_dispatch' && format('{0} suite={1}', github.ref_name, inputs.suite) || '' }}
	                       (an empty run-name keeps the default title of push and pull request runs)
	  displayTitle         a run is suite full when its title contains the token 'suite=full' (whitespace or the end of
	                       the title after it); a title with 'suite=pr' or with no suite= token (a run from before
	                       run-name, today's 'Build (Windows)') is not full and is ignored.

	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-ci-skip-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
$helper = Join-Path $PSScriptRoot 'ci-skip-equal-tree.ps1'

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

# made-up commit and tree shas: 40 hex digits
function Sha([string]$Hex) { ($Hex * 40).Substring(0, 40) }
$head = Sha 'a1'       # the pushed commit's tree
$other = Sha 'b2'      # some other tree
$trees = @{}           # the fake lookup: commit sha -> tree sha
$fake = { param($Sha) $trees[$Sha] }

# A run of gh run list's output; the defaults are a successful suite=full gate run
function Run([long]$Id, [string]$Commit, [string]$Created, [string]$Title = 'ARXVR-1-feat-x suite=full',
	[string]$Conclusion = 'success', [string]$EventName = 'workflow_dispatch') {
	[ordered]@{ conclusion = $Conclusion; createdAt = $Created; databaseId = $Id; displayTitle = $Title; event = $EventName; headSha = $Commit }
}
function Json($Runs) { ConvertTo-Json -InputObject @($Runs) -Depth 5 }

# The helper's decision for a runs file. -Text is written as is; -NoFile leaves the file missing.
# Returns the single output object, or a stand-in with Problem set (a throw, no object, several objects).
$script:n = 0
function Decide([string]$Text, [string]$HeadTree = $head, $TreeOf = $fake, [switch]$NoFile, [switch]$DefaultLookup) {
	$script:n++
	$file = Join-Path $root "runs-$($script:n).json"
	if (-not $NoFile) { [IO.File]::WriteAllText($file, $Text) }
	$params = @{ HeadTree = $HeadTree; RunsJson = $file }
	if (-not $DefaultLookup) { $params.TreeOf = $TreeOf }
	try { $out = @(& $helper @params 6>$null) }
	catch { return [pscustomobject]@{ Decision = $null; RunId = $null; Reason = $null; Problem = "threw: $_" } }
	if ($out.Count -ne 1) { return [pscustomobject]@{ Decision = $null; RunId = $null; Reason = $null; Problem = "$($out.Count) output objects, expected 1" } }
	$r = $out[0]
	$r | Add-Member -NotePropertyName Problem -NotePropertyValue $null -Force -ErrorAction SilentlyContinue
	$r
}
function Show($r) { if ($r.Problem) { $r.Problem } else { "Decision=$($r.Decision) RunId=$($r.RunId) Reason=$($r.Reason)" } }
function Expect-Skip([string]$Name, $r, [long]$Id) {
	Check $Name ($r.Decision -eq 'skip' -and "$($r.RunId)" -eq "$Id" -and "$($r.Reason)" -match [regex]::Escape("$Id")) "expected skip naming run $Id; got $(Show $r)"
}
function Expect-Run([string]$Name, $r) {
	Check $Name ($r.Decision -eq 'run' -and $null -eq $r.RunId -and -not [string]::IsNullOrWhiteSpace("$($r.Reason)")) "expected run with a reason and no run id; got $(Show $r)"
}

try {
	Check 'the helper scripts/dev/ci-skip-equal-tree.ps1 exists' (Test-Path -LiteralPath $helper) $helper

	Write-Host '--- one gate run'
	$gate = Sha 'c3'
	$trees[$gate] = $head
	Expect-Skip 'equal tree: skip, naming the run' (Decide (Json (Run 101 $gate '2026-10-22T10:00:00Z'))) 101
	$trees[$gate] = $other
	Expect-Run 'a different tree: run' (Decide (Json (Run 102 $gate '2026-10-22T10:00:00Z')))
	$trees[$gate] = $head.ToUpperInvariant()
	Expect-Skip 'equal tree in another letter case: skip' (Decide (Json (Run 103 $gate '2026-10-22T10:00:00Z'))) 103

	Write-Host '--- no successful full gate run'
	$trees[$gate] = $head
	Expect-Run 'no runs ([]): run' (Decide '[]')
	Expect-Run 'only a failed gate run on the same tree: run' (Decide (Json (Run 111 $gate '2026-10-22T10:00:00Z' -Conclusion 'failure')))
	Expect-Run 'only a cancelled gate run on the same tree: run' (Decide (Json (Run 112 $gate '2026-10-22T10:00:00Z' -Conclusion 'cancelled')))
	Expect-Run 'only a push run on the same tree: run' (Decide (Json (Run 113 $gate '2026-10-22T10:00:00Z' -EventName 'push')))
	Expect-Run 'only a run without a headSha: run' (Decide (Json ([ordered]@{ conclusion = 'success'; createdAt = '2026-10-22T10:00:00Z'; databaseId = 114; displayTitle = 'x suite=full'; event = 'workflow_dispatch' })))

	Write-Host '--- unreadable runs or trees (fail safe)'
	Expect-Run 'the runs file is missing: run' (Decide '' -NoFile)
	Expect-Run 'the runs file is empty: run' (Decide '')
	Expect-Run 'the runs file holds gh''s error text: run' (Decide 'HTTP 502: Bad Gateway (https://api.github.com/repos/o/r/actions/workflows/build-windows.yml/runs)')
	Expect-Run 'the runs file holds a JSON error object: run' (Decide '{"message":"Bad credentials","documentation_url":"https://docs.github.com/rest","status":"401"}')
	Expect-Run 'the runs file holds null: run' (Decide 'null')
	Expect-Run 'the runs file is cut off: run' (Decide '[{"conclusion":"success","createdAt":"2026-10-22T10:00:00Z","databaseId":121,')
	$one = Json (Run 122 $gate '2026-10-22T10:00:00Z')
	Expect-Run 'the tree lookup throws: run' (Decide $one -TreeOf { param($Sha) throw "fatal: bad object $Sha" })
	Expect-Run 'the tree lookup finds nothing ($null): run' (Decide $one -TreeOf { param($Sha) $null })
	Expect-Run 'the tree lookup returns an empty string: run' (Decide $one -TreeOf { param($Sha) '' })
	Expect-Run 'an empty head tree never equals an unknown run tree: run' (Decide $one -HeadTree '' -TreeOf { param($Sha) '' })

	Write-Host '--- several runs: any successful suite=full run on the same tree skips'
	$old = Sha 'd4'; $new = Sha 'e5'
	$trees[$old] = $head; $trees[$new] = $other
	Expect-Skip 'an older full run on the same tree, a later full run on another: skip, naming the older' (Decide (Json @(
		(Run 202 $new '2026-10-22T11:00:00Z'), (Run 201 $old '2026-10-22T10:00:00Z')))) 201
	Expect-Skip 'the same, listed oldest first: skip, naming the older' (Decide (Json @(
		(Run 201 $old '2026-10-22T10:00:00Z'), (Run 202 $new '2026-10-22T11:00:00Z')))) 201
	Expect-Skip 'the matching run is the 30th of 30, after 29 full runs on other trees: skip, naming it' (Decide (Json @(
		@(foreach ($i in 1..29) { Run (240 + $i) $new ('2026-10-22T{0:00}:30:00Z' -f (23 - [math]::Floor($i / 2))) }) + @(Run 239 $old '2026-10-21T08:00:00Z')))) 239
	$trees[$old] = $other; $trees[$new] = $head
	Expect-Skip 'the latest full run on the same tree, an older one on another: skip, naming the latest' (Decide (Json @(
		(Run 302 $new '2026-10-22T11:00:00Z'), (Run 301 $old '2026-10-22T10:00:00Z')))) 302
	Expect-Skip 'the same, listed oldest first: skip, naming the latest' (Decide (Json @(
		(Run 301 $old '2026-10-22T10:00:00Z'), (Run 302 $new '2026-10-22T11:00:00Z')))) 302
	Expect-Skip 'a later failed full run on another tree does not count: skip' (Decide (Json @(
		(Run 303 $old '2026-10-22T12:00:00Z' -Conclusion 'failure'), (Run 302 $new '2026-10-22T11:00:00Z'), (Run 301 $old '2026-10-22T10:00:00Z')))) 302
	$trees[$old] = $other; $trees[$new] = $other
	Expect-Run 'several full runs, none on the same tree: run' (Decide (Json @(
		(Run 312 $new '2026-10-22T11:00:00Z'), (Run 311 $old '2026-10-22T10:00:00Z'))))
	$same1 = Sha '3a'; $same2 = Sha '4b'
	$trees[$same1] = $head; $trees[$same2] = $head
	Expect-Skip 'two full runs on the same tree: skip, naming the later' (Decide (Json @(
		(Run 321 $same1 '2026-10-22T10:00:00Z'), (Run 322 $same2 '2026-10-22T11:00:00Z'), (Run 323 $new '2026-10-22T12:00:00Z')))) 322
	Expect-Skip 'the same, the later listed last of three after another tree: skip, naming the later' (Decide (Json @(
		(Run 323 $new '2026-10-22T12:00:00Z'), (Run 321 $same1 '2026-10-22T10:00:00Z'), (Run 322 $same2 '2026-10-22T11:00:00Z')))) 322
	Expect-Run 'a later failed full run on the same tree, the successful full run on another: run' (Decide (Json @(
		(Run 332 $same1 '2026-10-22T11:00:00Z' -Conclusion 'failure'), (Run 331 $new '2026-10-22T10:00:00Z'))))
	Expect-Run 'a later cancelled full run on the same tree, the successful full run on another: run' (Decide (Json @(
		(Run 334 $same1 '2026-10-22T11:00:00Z' -Conclusion 'cancelled'), (Run 333 $new '2026-10-22T10:00:00Z'))))
	$broken = Sha '5c'
	$picky = { param($Sha) if ($Sha -eq $broken) { throw "fatal: bad object $Sha" }; $trees[$Sha] }
	Expect-Skip 'the lookup throws for the latest full run, an older one is on the same tree: skip, naming the older' (Decide (Json @(
		(Run 342 $broken '2026-10-22T11:00:00Z'), (Run 341 $same1 '2026-10-22T10:00:00Z'))) -TreeOf $picky) 341
	Expect-Run 'the lookup throws for the only full run, the others are not full: run' (Decide (Json @(
		(Run 344 $broken '2026-10-22T11:00:00Z'), (Run 343 $same1 '2026-10-22T10:00:00Z' -Title 'ARXVR-2-feat-y suite=pr'))) -TreeOf $picky)

	Write-Host '--- runs of another suite are ignored'
	$prRun = Sha 'f6'; $full = Sha '17'
	$trees[$prRun] = $head; $trees[$full] = $other
	Expect-Run 'a later suite=pr run on the same tree, the full run on another: run' (Decide (Json @(
		(Run 402 $prRun '2026-10-22T11:00:00Z' -Title 'ARXVR-2-feat-y suite=pr'), (Run 401 $full '2026-10-22T10:00:00Z'))))
	Expect-Run 'only a suite=pr run on the same tree: run' (Decide (Json (Run 403 $prRun '2026-10-22T11:00:00Z' -Title 'ARXVR-2-feat-y suite=pr')))
	Expect-Run 'only a run whose title names no suite (before run-name) on the same tree: run' (Decide (Json (Run 404 $prRun '2026-10-22T11:00:00Z' -Title 'Build (Windows)')))
	$trees[$prRun] = $other; $trees[$full] = $head
	Expect-Skip 'a later suite=pr run on another tree does not hide the full run: skip, naming the full run' (Decide (Json @(
		(Run 412 $prRun '2026-10-22T11:00:00Z' -Title 'ARXVR-2-feat-y suite=pr'), (Run 411 $full '2026-10-22T10:00:00Z')))) 411
	Expect-Skip 'a later run without a suite on another tree does not hide it either: skip' (Decide (Json @(
		(Run 413 $prRun '2026-10-22T11:00:00Z' -Title 'Build (Windows)'), (Run 411 $full '2026-10-22T10:00:00Z')))) 411
	Expect-Skip 'a later push run on another tree does not hide it either: skip' (Decide (Json @(
		(Run 414 $prRun '2026-10-22T11:00:00Z' -EventName 'push'), (Run 411 $full '2026-10-22T10:00:00Z')))) 411
	Expect-Skip 'a later suite=pr run on the same tree does not take the name: skip, naming the full run' (Decide (Json @(
		(Run 416 $full '2026-10-22T12:00:00Z' -Title 'ARXVR-2-feat-y suite=pr'), (Run 411 $full '2026-10-22T10:00:00Z')))) 411

	Write-Host '--- the default tree lookup (git, current directory)'
	$repo = Join-Path $root 'repo'
	New-Item -ItemType Directory -Force $repo | Out-Null
	git -C $repo init -q
	git -C $repo config user.email 'test@example.com'
	git -C $repo config user.name 'test'
	Set-Content (Join-Path $repo 'a.txt') 'one' -NoNewline
	git -C $repo add -A
	git -C $repo commit -q -m one
	$c1 = (git -C $repo rev-parse HEAD).Trim()
	$t1 = (git -C $repo rev-parse 'HEAD^{tree}').Trim()
	Set-Content (Join-Path $repo 'a.txt') 'two' -NoNewline
	git -C $repo commit -q -am two
	$c2 = (git -C $repo rev-parse HEAD).Trim()
	Push-Location $repo
	try {
		Expect-Skip 'git: the gate commit''s tree equals the head tree: skip' (Decide (Json (Run 501 $c1 '2026-10-22T10:00:00Z')) -HeadTree $t1 -DefaultLookup) 501
		Expect-Run 'git: the gate commit''s tree differs: run' (Decide (Json (Run 502 $c2 '2026-10-22T10:00:00Z')) -HeadTree $t1 -DefaultLookup)
		Expect-Run 'git: the gate commit is not in the repository: run' (Decide (Json (Run 503 (Sha '9f') '2026-10-22T10:00:00Z')) -HeadTree $t1 -DefaultLookup)
	} finally { Pop-Location }
}
catch {
	Check 'the test itself ran to the end' $false "$_"
}
finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "ci-skip-equal-tree: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'ci-skip-equal-tree: all checks passed' -ForegroundColor Green
exit 0
