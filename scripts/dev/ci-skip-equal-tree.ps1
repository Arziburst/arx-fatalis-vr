<#
.SYNOPSIS
	Should a push to main repeat the game checks (#541)? Not when the merge gate already passed them on the same tree.

.DESCRIPTION
	The merge gate (coordinator/tools/mergeq.sh) runs the full suite on a pull request's head right before the merge,
	as a manual run of build-windows.yml with suite=full. The push run of the merge then gets the same tree and would
	repeat the same ~10 minutes of game checks on the single runner. This helper compares the pushed tree with the
	trees of the listed successful suite=full manual runs and decides:
	  skip - one of them ran on this very tree (the latest such run is named);
	  run  - none did, there is no such run, or anything could not be read (fail safe: the checks run as before).

	Which runs count: event workflow_dispatch, conclusion success, a headSha, and the token 'suite=full' in the title
	(the workflow's run-name names the suite of a manual run; gh run list does not show a run's inputs). Any of them
	may match, not only the latest: the single runner often runs a merged PR's push run after the next PR's gate.

	Output: one object { Decision = 'skip' | 'run'; RunId = <the matching run's databaseId> | $null; Reason }. The
	workflow prints Reason as its log line. Never throws.

.PARAMETER HeadTree
	The tree of the pushed commit (git rev-parse 'HEAD^{tree}').

.PARAMETER RunsJson
	A file with the runs, as gh run list --json databaseId,headSha,displayTitle,conclusion,event,createdAt writes them.
	Missing, empty or not a JSON array (an error text, an error object): run.

.PARAMETER TreeOf
	{ param($Sha) ... } -> the tree sha of the commit $Sha, $null or '' when unknown; may throw (that run then does not
	match). Default: git rev-parse in the current directory.

.EXAMPLE
	.\scripts\dev\ci-skip-equal-tree.ps1 -HeadTree (git rev-parse 'HEAD^{tree}') -RunsJson runs.json
#>
[CmdletBinding()]
param(
	[string]$HeadTree,
	[string]$RunsJson,
	[scriptblock]$TreeOf
)

function Get-SkipDecision {
	function Run-Decision([string]$Why) { [pscustomobject]@{ Decision = 'run'; RunId = $null; Reason = "the game checks run: $Why" } }

	$wanted = "$HeadTree".Trim()
	if (-not $wanted) { return Run-Decision 'the head tree is unknown' }
	if (-not $RunsJson -or -not (Test-Path -LiteralPath $RunsJson -PathType Leaf)) { return Run-Decision 'no list of gate runs (the runs file is missing)' }
	$text = [IO.File]::ReadAllText($RunsJson)
	if ([string]::IsNullOrWhiteSpace($text)) { return Run-Decision 'the list of gate runs is empty (the runs file is empty)' }
	try { $listed = ConvertFrom-Json -InputObject $text -NoEnumerate }
	catch { return Run-Decision "the list of gate runs cannot be read ($($_.Exception.Message))" }
	if ($listed -isnot [array]) { return Run-Decision "the list of gate runs is not a list: $($text.Trim() -replace '\s+', ' ')" }

	$gateRuns = @($listed | Where-Object {
		$_ -and $_.event -eq 'workflow_dispatch' -and $_.conclusion -eq 'success' -and "$($_.headSha)".Trim() -and
		"$($_.displayTitle)" -match '(?:^|\s)suite=full(?:\s|$)'
	})
	if (-not $gateRuns.Count) { return Run-Decision "no successful suite=full gate run among the $($listed.Count) listed run(s)" }

	# The latest first: when several ran on this tree, the latest is named
	$when = { try { [datetimeoffset]$_.createdAt } catch { [datetimeoffset]::MinValue } }
	$unread = 0
	foreach ($gateRun in @($gateRuns | Sort-Object -Property $when -Descending)) {
		$sha = "$($gateRun.headSha)".Trim()
		try { $runTree = "$(& $TreeOf $sha)".Trim() }
		catch { $runTree = ''; Write-Host "ci-skip-equal-tree: the tree of run $($gateRun.databaseId) ($sha) cannot be read: $($_.Exception.Message)" }
		if (-not $runTree) { $unread++; continue }
		if ($runTree -ieq $wanted) {
			return [pscustomobject]@{ Decision = 'skip'; RunId = $gateRun.databaseId
				Reason = "the game checks are skipped: the merge gate run $($gateRun.databaseId) (suite=full, $sha) passed them on this tree $wanted" }
		}
	}
	Run-Decision "none of the $($gateRuns.Count) successful suite=full gate run(s) ran on this tree $wanted$(if ($unread) { " ($unread of them with a tree that cannot be read)" })"
}

if (-not $TreeOf) {
	$TreeOf = {
		param($Sha)
		$found = git rev-parse --verify --quiet "$Sha^{tree}" 2>$null
		$global:LASTEXITCODE = 0
		$found
	}
}
try { $decision = Get-SkipDecision }
catch { $decision = [pscustomobject]@{ Decision = 'run'; RunId = $null; Reason = "the game checks run: the decision failed ($($_.Exception.Message))" } }
$decision
