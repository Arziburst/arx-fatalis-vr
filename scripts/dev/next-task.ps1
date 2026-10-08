<#
.SYNOPSIS
	Show which task to work on next, following the MVP task queue in docs/vr/MVP.md.

.DESCRIPTION
	1. If a task is waiting in Testing, it is listed first: the owner's verdict comes before new work.
	2. If a task is already In Progress, that one is next (finish before starting another).
	3. Otherwise the first task of the queue that is in Todo.
#>
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'board.ps1')

$items = Get-BoardItems
$byNumber = @{}; foreach ($i in $items) { $byNumber[$i.Number] = $i }
$tasks = $items | Where-Object { $_.Labels -contains 'task' -or $_.Labels -contains 'bug' }

$testing = @($tasks | Where-Object Status -eq 'Testing')
foreach ($t in $testing) { "TESTING      #$($t.Number) $($t.Title)" }

$inProgress = @($tasks | Where-Object Status -eq 'In Progress')
if ($inProgress) {
	foreach ($t in $inProgress) { "IN PROGRESS  #$($t.Number) $($t.Title)" }
	"NEXT         #$($inProgress[0].Number)"
	return
}

foreach ($n in Get-MvpQueue) {
	$t = $byNumber[$n]
	if ($t -and $t.Status -eq 'Todo') {
		"NEXT         #$n $($t.Title)"
		return
	}
}
'NEXT         none — the MVP queue is empty (check docs/vr/MVP.md and the board)'
