<#
.SYNOPSIS
	Move an issue to a column on the project board.

.DESCRIPTION
	One GraphQL mutation (the board has no REST API), with the board read from the cache (board.ps1). When the
	GitHub GraphQL limit is spent the move is not lost and the script does not fail: it is queued in
	%LOCALAPPDATA%\ArxFatalisVR\board-queue.json, the output says QUEUED with the reset time, and the next run of any
	board script after the reset sends it. The queued status already shows in next-task.ps1 / cycle-report.ps1.

.PARAMETER Flush
	Only send the queued moves now (ignores the wait for the reset); no issue needed.

.EXAMPLE
	.\scripts\dev\set-status.ps1 -Issue 13 -Status 'In Progress'
	.\scripts\dev\set-status.ps1 -Flush
#>
[CmdletBinding(DefaultParameterSetName = 'Move')]
param(
	[Parameter(Mandatory, ParameterSetName = 'Move')] [int]$Issue,
	[Parameter(Mandatory, ParameterSetName = 'Move')] [ValidateSet('Backlog', 'Todo', 'In Progress', 'Testing', 'Done')] [string]$Status,
	[Parameter(Mandatory, ParameterSetName = 'Flush')] [switch]$Flush
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'board.ps1')

if ($Flush) {
	$left = @((Read-BoardQueue).moves).Count
	if (-not $left) { Write-Host 'no queued board moves'; return }
	Clear-ArxGhBlock # an explicit -Flush asks GitHub even inside a remembered rate-limit window
	$sent = @(Invoke-BoardQueueFlush -Force).Count
	$still = @(Get-BoardQueueSummary)
	Write-Host "$sent of $left queued move(s) sent"
	foreach ($line in $still) { Write-Host "still queued: $line" }
	return
}

$result = Set-BoardStatus $Issue $Status
if ($result -eq 'applied') { Write-Host "#$Issue -> $Status" }
else { Write-Host "#$Issue -> $Status (QUEUED: not on GitHub yet - mention it in the report if it is still queued at the end)" }
