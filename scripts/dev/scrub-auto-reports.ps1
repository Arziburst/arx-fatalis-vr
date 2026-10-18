<#
.SYNOPSIS
	Scrubs personal data out of the auto-reported issues already on GitHub (#387): the repository is public.

.DESCRIPTION
	Reads every issue labeled `auto-reported` (open and closed) and every comment on them, runs the title, the body
	and each comment through the digest's scrubber (Protect-ArxText in log-digest-lib.ps1: home paths, account and
	machine names, Steam ids and userdata paths, e-mails, IPs, GUIDs) and lists every change. A dry run by default;
	-Apply edits the issues and comments in place (never deletes one). Then re-scans every title, body and comment
	with the final check (Find-ArxPrivateData) - after -Apply as GitHub returns them.

	GitHub REST only (`gh api`), never GraphQL. GitHub keeps the edit history of an issue: the text before the
	scrub stays visible there (TESTING.md, "The run digest").

	Exit code 0 when the re-scan is clean, 1 when it still finds personal data.

.PARAMETER Apply
	Edit the issues and comments (without it nothing is changed).

.EXAMPLE
	.\scripts\dev\scrub-auto-reports.ps1           # dry run: what would change, and the re-scan of the result
.EXAMPLE
	.\scripts\dev\scrub-auto-reports.ps1 -Apply
#>
[CmdletBinding()]
param([switch]$Apply)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

function Get-AutoIssues {
	$issues = @()
	for ($page = 1; $page -le 10; $page++) {
		$batch = @(Invoke-ArxGitHub GET "repos/$script:ArxDigestRepo/issues?labels=auto-reported&state=all&per_page=100&page=$page")
		$issues += $batch
		if ($batch.Count -lt 100) { break }
	}
	@($issues | Where-Object { -not $_.pull_request } | Sort-Object number)
}

function Get-IssueComments($Issue) {
	if (-not $Issue.comments) { return @() }
	@(Invoke-ArxGitHub GET "repos/$script:ArxDigestRepo/issues/$($Issue.number)/comments?per_page=100")
}

# What changed in one text, a line per distinct replacement
function Write-Change([string]$What, [hashtable]$Changes) {
	Write-Host "  $What" -ForegroundColor Yellow
	foreach ($k in ($Changes.Keys | Sort-Object)) { Write-Host ("    {0,4} x {1}" -f $Changes[$k], $k) }
}

$issues = Get-AutoIssues
Write-Host "$($issues.Count) auto-reported issue(s) in $script:ArxDigestRepo$(if (-not $Apply) { ' - dry run, nothing is changed' })" -ForegroundColor Cyan
$edits = 0
$scanned = [Collections.Generic.List[object]]::new()     # (where, text) of the result, for the re-scan
foreach ($issue in $issues) {
	Write-Host "#$($issue.number) ($($issue.state)) $($issue.title)"
	$patch = @{}
	foreach ($field in 'title', 'body') {
		$changes = @{}
		$clean = Protect-ArxText $issue.$field $changes
		if ($clean -ne $issue.$field) { Write-Change $field $changes; $patch[$field] = $clean }
		$scanned.Add([pscustomobject]@{ Where = "#$($issue.number) $field"; Text = $clean })
	}
	if ($patch.Count) {
		$edits++
		if ($Apply) { $null = Invoke-ArxGitHub PATCH "repos/$script:ArxDigestRepo/issues/$($issue.number)" $patch }
	}
	foreach ($c in (Get-IssueComments $issue)) {
		$changes = @{}
		$clean = Protect-ArxText $c.body $changes
		if ($clean -ne $c.body) {
			Write-Change "comment $($c.id) by $($c.user.login)" $changes
			$edits++
			if ($Apply) { $null = Invoke-ArxGitHub PATCH "repos/$script:ArxDigestRepo/issues/comments/$($c.id)" @{ body = $clean } }
		}
		$scanned.Add([pscustomobject]@{ Where = "#$($issue.number) comment $($c.id)"; Text = $clean })
	}
}
Write-Host "$edits text(s) $(if ($Apply) { 'edited' } else { 'would be edited' })" -ForegroundColor Cyan

# The re-scan: after -Apply what GitHub returns now, in a dry run the scrubbed texts
if ($Apply) {
	$scanned.Clear()
	foreach ($issue in (Get-AutoIssues)) {
		$scanned.Add([pscustomobject]@{ Where = "#$($issue.number) title"; Text = $issue.title })
		$scanned.Add([pscustomobject]@{ Where = "#$($issue.number) body"; Text = $issue.body })
		foreach ($c in (Get-IssueComments $issue)) { $scanned.Add([pscustomobject]@{ Where = "#$($issue.number) comment $($c.id)"; Text = $c.body }) }
	}
}
$dirty = 0
foreach ($s in $scanned) {
	$found = @(Find-ArxPrivateData $s.Text)
	if ($found) { $dirty++; Write-Host "  still personal data in $($s.Where): $($found -join ', ')" -ForegroundColor Red }
}
Write-Host "re-scan of $($scanned.Count) title(s), body(ies) and comment(s)$(if ($Apply) { ' on GitHub' } else { ' as they would be' }): $(if ($dirty) { "$dirty with personal data" } else { 'clean' })" -ForegroundColor $(if ($dirty) { 'Red' } else { 'Green' })
exit ([int][bool]$dirty)
