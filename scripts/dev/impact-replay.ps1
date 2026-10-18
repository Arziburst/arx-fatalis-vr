<#
.SYNOPSIS
	Replay the impact map (scripts\dev\xrs\impact.txt, #522) over the last pull request CI runs: would the selection of
	a pull request have run every game check that failed?

.DESCRIPTION
	Takes the newest -Runs pull request runs of build-windows.yml whose game checks reached a verdict (passed or failed;
	a cancelled run, a run whose build failed and a docs-only run have none and are skipped). For each:
	  - base and head: the merge commit the run checked out ("Merge <head> into <base>" in its checkout log), so the
	    diff is the one the run saw, however far main moved since
	  - the changed files: GitHub's compare <base>...<head> (gh api), never a local checkout
	  - the failed phases: the "==> ci-smoke phase 'x': FAIL" lines and the FAIL / SKIPPED lines of the summary
	  - the selection the map computes for those files (Get-ArxImpactSelection, as ci-smoke.ps1 -Suite pr does)
	A failed phase is selected when it is in the selection or the selection is the full suite; save-library runs
	whenever a selected sequence loads a save, which the core does, so it counts as core. The table (Markdown) goes to
	the output, one row per run; then the totals and the selection sizes, with an estimate of each selection's own time
	from -DurationsFile.

	What a run needs from GitHub is cached in -CacheDir (one JSON per run), so a second replay - after a change of the
	map - costs no API calls. Needs gh, logged in.

	Exit code 0 when every failed phase is selected, 1 when one is not, 3 when GitHub could not be read.

.PARAMETER Runs
	How many runs with a game check verdict to replay (default 30).

.PARAMETER Map
	The impact map (default scripts\dev\xrs\impact.txt).

.PARAMETER Repo
	The repository (default Arziburst/arx-fatalis-vr).

.PARAMETER CacheDir
	Where the per-run facts are cached (default %LOCALAPPDATA%\ArxFatalisVR\impact-replay).

.PARAMETER DurationsFile
	Phase name -> seconds (ci-durations.json of the runner, or one written by hand) for the time estimate; none: no estimate.

.EXAMPLE
	.\scripts\dev\impact-replay.ps1
.EXAMPLE
	.\scripts\dev\impact-replay.ps1 -Runs 40 -DurationsFile $env:TEMP\ci-durations.json
#>
[CmdletBinding()]
param(
	[int]$Runs = 30,
	[string]$Map = (Join-Path $PSScriptRoot 'xrs\impact.txt'),
	[string]$Repo = 'Arziburst/arx-fatalis-vr',
	[string]$CacheDir = (Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\impact-replay'),
	[string]$DurationsFile
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')

function Invoke-Gh {
	# stderr apart: a warning of gh must not end up in the JSON it prints
	$all = @(& gh @args 2>&1)
	$err = @($all | Where-Object { $_ -is [Management.Automation.ErrorRecord] })
	if ($LASTEXITCODE -ne 0) { throw "gh $($args -join ' ') failed (exit $LASTEXITCODE): $($err -join ' ')" }
	$all | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] }
}

# The facts of one run, from the cache or GitHub: { Id; Pr; Branch; Created; Verdict (success | failure | $null when the
# game checks did not reach one); Base; Head; Failed (phase names); Files (the changed paths) }
function Get-RunFacts($Run) {
	$cache = Join-Path $CacheDir "$($Run.databaseId).json"
	if (Test-Path -LiteralPath $cache) { return Get-Content -LiteralPath $cache -Raw | ConvertFrom-Json }
	$facts = [ordered]@{ Id = $Run.databaseId; Pr = $null; Branch = $Run.headBranch; Created = $Run.createdAt; Verdict = $null; Base = $null; Head = $Run.headSha; Failed = @(); Files = @() }
	$steps = Invoke-Gh run view $Run.databaseId -R $Repo --json jobs | ConvertFrom-Json
	$checks = @($steps.jobs.steps | Where-Object { $_.name -like 'Game checks*' })
	if ($checks -and $checks[0].conclusion -in 'success', 'failure') {
		$facts.Verdict = $checks[0].conclusion
		$log = Invoke-Gh run view $Run.databaseId -R $Repo --log
		foreach ($line in $log) {
			# The checkout may print an older merge of the workspace first: the last one is what the run built
			if ($line -match 'HEAD is now at \w+ Merge ([0-9a-f]{40}) into ([0-9a-f]{40})') { $facts.Head = $Matches[1]; $facts.Base = $Matches[2] }
			if ($line -notlike '*Game checks*') { continue }
			if ($line -match "==> ci-smoke phase '([^']+)': (FAIL|SKIPPED)") { $facts.Failed += $Matches[1] }
			elseif ($line -match 'Z\s+(FAIL|SKIPPED)\s+(\S+)\s*$') { $facts.Failed += $Matches[2] }
		}
		$facts.Failed = @($facts.Failed | Sort-Object -Unique)
		# A merge of another head is not this run's (a log cut short): no diff rather than a wrong one
		if ($facts.Base -and $facts.Head -ne $Run.headSha) { $facts.Base = $null }
		if ($facts.Base) {
			$facts.Files = @(Invoke-Gh api --paginate "repos/$Repo/compare/$($facts.Base)...$($facts.Head)" --jq '.files[]?.filename' | Sort-Object -Unique)
		}
		$pr = @(Invoke-Gh api "repos/$Repo/actions/runs/$($Run.databaseId)" --jq '.pull_requests[].number')
		if (-not $pr) { $pr = @(Invoke-Gh pr list -R $Repo --head $Run.headBranch --state all --json 'number' --jq '.[].number') }
		if ($pr) { $facts.Pr = [int]$pr[0] }
	}
	New-Item -ItemType Directory -Force $CacheDir | Out-Null
	# Only a finished verdict is cached for good; a run without one is asked again next time (it may have been re-run)
	if ($facts.Verdict) { [pscustomobject]$facts | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $cache -Encoding utf8 }
	[pscustomobject]$facts
}

$impact = Read-ArxImpactMap $Map
if ($impact.Errors) { Write-Host "impact-replay: the map has errors:`n  $($impact.Errors -join "`n  ")" -ForegroundColor Red; exit 1 }
$durations = Read-ArxPhaseDurations $DurationsFile
$allNames = @(Get-ArxSequenceNames (Join-Path $PSScriptRoot 'xrs')) + $ArxImpactChecks

try {
	$list = Invoke-Gh run list -R $Repo -w build-windows.yml -e pull_request -L 200 --json 'databaseId,conclusion,status,headBranch,headSha,createdAt' | ConvertFrom-Json
} catch { Write-Host "impact-replay: $_" -ForegroundColor Red; exit 3 }

$rows = @()
foreach ($run in $list) {
	if ($rows.Count -ge $Runs) { break }
	if ($run.status -ne 'completed' -or $run.conclusion -notin 'success', 'failure') { continue }
	try { $facts = Get-RunFacts $run } catch { Write-Host "impact-replay: run $($run.databaseId): $_" -ForegroundColor Red; exit 3 }
	if (-not $facts.Verdict) { continue }
	$sel = Get-ArxImpactSelection $impact @($facts.Files)
	$names = if ($sel.Full) { $allNames } else { @($sel.Names) }
	$seconds = $null
	if ($durations.Count) {
		$seconds = 0.0
		foreach ($n in $names) {
			$phase = if ($n -in $ArxImpactChecks) { $n } else { "xrs-$n" }
			if ($durations.ContainsKey($phase)) { $seconds += $durations[$phase] }
			if ($durations.ContainsKey("$phase-readback")) { $seconds += $durations["$phase-readback"] }
		}
	}
	$verdicts = foreach ($phase in @($facts.Failed)) {
		$name = $phase -replace '^xrs-', '' -replace '-readback$', ''
		$how = if ($phase -in $ArxImpactChecks + 'save-library') { 'yes (core by definition)' }
			elseif ($sel.Reasons.ContainsKey($name)) { "yes ($($sel.Reasons[$name])$(if ($sel.Full) { ', and the full suite' }))" }
			elseif ($sel.Full) { 'yes (only by the full suite)' }
			else { 'NO' }
		[pscustomobject]@{ Phase = $phase; How = $how }
	}
	$rows += [pscustomobject]@{
		Run = $facts.Id; Pr = $facts.Pr; Verdict = $facts.Verdict; Files = @($facts.Files).Count; NoBase = -not $facts.Base
		Full = $sel.Full; FullBecause = @($sel.FullBecause); Selected = $names.Count; Seconds = $seconds; Failed = @($verdicts)
	}
}

Write-Output "Impact map replay: $Map over the last $($rows.Count) pull request runs with a game check verdict ($Repo)"
Write-Output ''
Write-Output '| run | PR | verdict | files | selection | failed phase | selected? |'
Write-Output '|---|---|---|---|---|---|---|'
foreach ($r in $rows) {
	$selection = if ($r.NoBase) { 'no merge commit in the log' }
		elseif ($r.Full) { "full ($(@($r.FullBecause)[0])$(if ($r.FullBecause.Count -gt 1) { " +$($r.FullBecause.Count - 1)" }))" }
		else { "$($r.Selected) checks$(if ($null -ne $r.Seconds) { ', ~{0:0} s' -f $r.Seconds })" }
	$failed = if ($r.Failed) { ($r.Failed | ForEach-Object Phase) -join ', ' } else { '-' }
	$how = if ($r.Failed) { ($r.Failed | ForEach-Object How) -join ', ' } else { '-' }
	Write-Output "| $($r.Run) | $(if ($r.Pr) { "#$($r.Pr)" } else { '?' }) | $($r.Verdict) | $($r.Files) | $selection | $failed | $how |"
}
$failures = @($rows | ForEach-Object { $_.Failed } | Where-Object { $_ })
$missed = @($failures | Where-Object How -eq 'NO')
$partial = @($rows | Where-Object { -not $_.Full -and -not $_.NoBase })
Write-Output ''
Write-Output "Runs: $($rows.Count) ($(@($rows | Where-Object Verdict -eq 'failure').Count) with failed game checks); failed phases: $($failures.Count), selected: $($failures.Count - $missed.Count), missed: $($missed.Count)"
Write-Output "Selections: $($partial.Count) partial, $(@($rows | Where-Object Full).Count) full"
if ($partial) {
	$sizes = @($partial | ForEach-Object Selected | Sort-Object)
	$line = "Partial selection size: median $($sizes[[int][Math]::Floor($sizes.Count / 2)]), max $($sizes[-1]) checks"
	$times = @($partial | Where-Object { $null -ne $_.Seconds } | ForEach-Object Seconds | Sort-Object)
	if ($times) { $line += "; own time (sum of phases, before lanes): median {0:0} s, max {1:0} s" -f $times[[int][Math]::Floor($times.Count / 2)], $times[-1] }
	Write-Output $line
}
if ($missed) {
	Write-Output "MISSED: $(($missed | ForEach-Object Phase) -join ', ')"
	exit 1
}
exit 0
