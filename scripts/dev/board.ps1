# Shared helpers for the project board. Dot-source from the other scripts in this folder.

$Repo        = 'Arziburst/arx-fatalis-vr'
$BoardOwner  = 'Arziburst'
$BoardNumber = 3

function Get-ProjectId {
	gh project view $BoardNumber --owner $BoardOwner --format json -q .id
}

function Get-StatusField {
	(gh project field-list $BoardNumber --owner $BoardOwner --format json | ConvertFrom-Json).fields |
		Where-Object name -eq 'Status'
}

function Get-BoardItems {
	(gh project item-list $BoardNumber --owner $BoardOwner --limit 500 --format json | ConvertFrom-Json).items |
		Where-Object { $_.content.number } |
		ForEach-Object {
			[pscustomobject]@{
				Id        = $_.id
				Number    = [int]$_.content.number
				Title     = $_.content.title
				Status    = $_.status
				Milestone = $_.milestone.title
				Labels    = @($_.labels)
			}
		}
}

function Get-MvpQueue {
	# Issue numbers from the "Task queue" section of docs/vr/MVP.md, in order.
	$mvp = Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot '..\..\docs\vr\MVP.md')
	$section = [regex]::Match($mvp, '(?ms)^## Task queue\s*$(.*?)(?=^## [^#]|\z)').Groups[1].Value
	[regex]::Matches($section, '(?m)^\s*\d+\.\s+#(\d+)') | ForEach-Object { [int]$_.Groups[1].Value }
}
