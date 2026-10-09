<#
.SYNOPSIS
	CI result of a pull request over REST - the replacement for `gh pr checks <pr> [--watch]`, which polls GraphQL.

.DESCRIPTION
	Reads the check runs and commit statuses of the PR's head commit (github-lib.ps1, #186). With -Watch it polls every
	-IntervalSeconds until nothing is running any more (or -TimeoutMinutes pass). A GitHub rate-limit error while
	watching is waited out by the library or reported, never read as "green".
	Exit code: 0 all green, 1 something failed, 2 no checks (yet) or still running at the timeout.

.EXAMPLE
	.\scripts\dev\pr-checks.ps1 191 -Watch
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory, Position = 0)] [int]$Pr,
	[switch]$Watch,
	[int]$IntervalSeconds = 30,
	[int]$TimeoutMinutes = 40
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$Repo = 'Arziburst/arx-fatalis-vr'
. (Join-Path $PSScriptRoot 'github-lib.ps1')

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$lastLine = ''
while ($true) {
	$pull = Get-GhPull $Pr
	$result = Get-GhCommitChecks $pull.headRefOid
	$line = "PR #$Pr @ $($pull.headRefOid.Substring(0, 9)): $($result.State)"
	if ($line -ne $lastLine) {
		Write-Host $line
		foreach ($c in $result.Checks) { Write-Host "  $($c.Name): $($c.Status) $($c.Conclusion)" }
		$lastLine = $line
	}
	if ($result.State -in 'green', 'failed') { exit $(if ($result.State -eq 'green') { 0 } else { 1 }) }
	if (-not $Watch -or (Get-Date) -gt $deadline) {
		if ($result.State -eq 'none') { Write-Host 'no checks reported: a PR that conflicts with main gets no run (TRAPS.md); docs-only PRs have none' }
		exit 2
	}
	Start-Sleep -Seconds $IntervalSeconds
}
