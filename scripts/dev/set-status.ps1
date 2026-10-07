<#
.SYNOPSIS
	Move an issue to a column on the project board.

.EXAMPLE
	.\scripts\dev\set-status.ps1 -Issue 13 -Status 'In Progress'
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)] [int]$Issue,
	[Parameter(Mandatory)] [ValidateSet('Backlog', 'Todo', 'In Progress', 'Testing', 'Done')] [string]$Status
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'board.ps1')

$item = Get-BoardItems | Where-Object { $_.Number -eq $Issue }
if (-not $item) {
	Write-Host "Issue #$Issue is not on the board yet, adding it"
	$id = gh project item-add $BoardNumber --owner $BoardOwner --url "https://github.com/$Repo/issues/$Issue" --format json -q .id
	$item = [pscustomobject]@{ Id = $id; Number = $Issue }
}

$field = Get-StatusField
$option = $field.options | Where-Object name -eq $Status
gh project item-edit --id $item.Id --project-id (Get-ProjectId) --field-id $field.id --single-select-option-id $option.id | Out-Null
if ($LASTEXITCODE) { throw "Failed to move #$Issue to $Status" }
Write-Host "#$Issue -> $Status"
