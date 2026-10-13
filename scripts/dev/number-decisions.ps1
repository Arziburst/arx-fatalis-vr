<#
.SYNOPSIS
	Give the decisions of a branch their real numbers at merge time (#274): D-NEW becomes the next free D-0xx.

.DESCRIPTION
	While a PR is open, a new decision is written as `## D-NEW: <title>` and referenced as `D-NEW` (code comments, TESTING,
	TRAPS, the PR body). A second decision in the same PR is `D-NEW-<tag>` (`D-NEW-hands`). Nobody picks a number, so two PRs
	never clash.

	Right after `git merge origin/main` into the task branch and right before the merge, run this script:
		.\scripts\dev\number-decisions.ps1 [-Pr <number>]
	It takes the highest `## D-nnn` heading in docs/vr/DECISIONS.md (which now includes everything merged so far),
	gives every distinct placeholder the next number in the order of its heading, rewrites it in every file the branch
	changed (compared with -BaseRef), and with -Pr also in the PR body (REST). Then commit and push the result; CI runs again.

	Lines that carry the marker "d-new-literal" are left alone (documentation that talks about the placeholder itself).
	The files of this tool are never touched.

	-Check is the CI gate: fails when DECISIONS.md adds a numbered D-nnn heading that already exists on -BaseRef with another
	title, or that appears twice. A D-NEW placeholder passes (it is numbered at merge).
	With -Strict (CI on a push to main) a placeholder heading fails too: nothing unnumbered may sit on main (#383).
	Every documentation line that talks about the placeholder itself must carry the d-new-literal marker.

.PARAMETER Root
	The working copy (default: the one this script belongs to).
.PARAMETER BaseRef
	What the branch is compared with (default origin/main).
.PARAMETER Pr
	Also rewrite the body of this pull request.
.PARAMETER Check
	Only verify, change nothing; exit 1 on a clash.
.PARAMETER Strict
	With -Check: also fail on any unnumbered D-NEW heading (for main).
#>
[CmdletBinding()]
param(
	[string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
	[string]$BaseRef = 'origin/main',
	[int]$Pr = 0,
	[switch]$Check,
	[switch]$Strict
)

$ErrorActionPreference = 'Stop'
$decisions = 'docs/vr/DECISIONS.md'
$placeholder = '\bD-NEW(?:-[A-Za-z0-9]+)?\b'
$skipFiles = @('scripts/dev/number-decisions.ps1', 'scripts/dev/test-number-decisions.ps1')

function Invoke-Git { $out = & git -C $Root @args 2>&1; if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed: $out" }; $out }
function Get-Headings([string[]]$Lines) {
	foreach ($line in $Lines) { if ($line -match '^## D-(\d+):\s*(.*?)\s*$') { [pscustomobject]@{ Number = [int]$Matches[1]; Title = $Matches[2] } } }
}

$path = Join-Path $Root $decisions
$mine = @(Get-Headings (Get-Content -LiteralPath $path))

if ($Check) {
	$base = @(Get-Headings ((Invoke-Git show "${BaseRef}:$decisions") -split "`r?`n"))
	$problems = @()
	if ($Strict) {
		# main (push): every decision must be numbered by now (#383)
		foreach ($line in (Get-Content -LiteralPath $path)) {
			if ($line -match "^## ($placeholder):") { $problems += "$decisions still has an unnumbered decision '$line' on main - run number-decisions.ps1 and commit the result" }
		}
	}
	foreach ($g in ($mine | Group-Object Number | Where-Object Count -gt 1)) { $problems += "D-$($g.Name) appears $($g.Count) times in $decisions" }
	foreach ($h in $mine) {
		$onBase = @($base | Where-Object Number -eq $h.Number)
		if ($onBase.Count -gt 0 -and -not ($onBase | Where-Object Title -eq $h.Title)) {
			$problems += "D-$('{0:d3}' -f [int]$h.Number) '$($h.Title)' clashes with '$($onBase[0].Title)' already on $BaseRef - write the new decision as D-NEW and run number-decisions.ps1 before the merge"
		}
	}
	if ($problems) { $problems | ForEach-Object { Write-Host "number-decisions: $_" -ForegroundColor Red }; exit 1 }
	Write-Host "number-decisions: ok ($($mine.Count) decisions, no clash with $BaseRef)"
	exit 0
}

# Placeholders in the order of their headings; one that has no heading of its own is a mistake worth stopping for
$text = Get-Content -LiteralPath $path -Raw
$tokens = @([regex]::Matches($text, "(?m)^## ($placeholder):") | ForEach-Object { $_.Groups[1].Value })
if (-not $tokens) { Write-Host 'number-decisions: no D-NEW heading in DECISIONS.md, nothing to number'; exit 0 }
$duplicate = @($tokens | Group-Object | Where-Object Count -gt 1)
if ($duplicate) { throw "two decisions use the same placeholder: $($duplicate.Name -join ', ') (use D-NEW-<tag>)" }

$next = [int]($mine | Measure-Object Number -Maximum).Maximum
$map = [ordered]@{}
foreach ($t in $tokens) { $next++; $map[$t] = 'D-{0:d3}' -f $next }

function Convert-Text([string]$Value) {
	$lines = $Value -split "(?<=\n)"
	$sb = New-Object System.Text.StringBuilder
	foreach ($line in $lines) {
		if ($line -match 'd-new-literal') { [void]$sb.Append($line); continue }
		[void]$sb.Append([regex]::Replace($line, $placeholder, { param($m) if ($map.Contains($m.Value)) { $map[$m.Value] } else { $m.Value } }))
	}
	$sb.ToString()
}

$mergeBase = (Invoke-Git merge-base HEAD $BaseRef | Select-Object -First 1)
$changed = @(Invoke-Git diff --name-only $mergeBase | Where-Object { $_ -notin $skipFiles })
$touched = @()
foreach ($rel in $changed) {
	$file = Join-Path $Root $rel
	if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
	$bytes = [System.IO.File]::ReadAllBytes($file)
	if ($bytes -contains 0) { continue }
	$old = [System.Text.Encoding]::UTF8.GetString($bytes)
	$new = Convert-Text $old
	if ($new -ne $old) {
		[System.IO.File]::WriteAllBytes($file, (New-Object System.Text.UTF8Encoding($false)).GetBytes($new))
		$touched += $rel
	}
}

foreach ($k in $map.Keys) { Write-Host "number-decisions: $k -> $($map[$k])" }
Write-Host "number-decisions: rewrote $($touched.Count) file(s): $($touched -join ', ')"

if ($Pr -gt 0) {
	$body = (& gh api "repos/Arziburst/arx-fatalis-vr/pulls/$Pr" --jq '.body') -join "`n"
	$newBody = Convert-Text $body
	if ($newBody -ne $body) {
		$tmp = New-TemporaryFile
		try {
			@{ body = $newBody } | ConvertTo-Json | Set-Content -LiteralPath $tmp -Encoding utf8
			& gh api -X PATCH "repos/Arziburst/arx-fatalis-vr/pulls/$Pr" --input $tmp | Out-Null
			if ($LASTEXITCODE -ne 0) { throw "could not update the body of PR #$Pr" }
		} finally { Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue }
		Write-Host "number-decisions: PR #$Pr body updated"
	}
}
Write-Host 'number-decisions: now commit ("ARXVR-<n> docs: number the decisions") and push'
exit 0
