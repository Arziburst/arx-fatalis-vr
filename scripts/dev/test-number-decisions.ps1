<#
.SYNOPSIS
	Self-test of number-decisions.ps1 (#274): two branches that both add D-NEW get consecutive numbers when merged one after the other.

.DESCRIPTION
	Builds a throw-away git repository (no network, no game): main with two decisions, two task branches that each add a
	`## D-NEW:` decision and reference it in a source file; merges them one by one as the merge flow does (merge main,
	number, commit, merge into main). Also covers D-NEW-<tag> (two decisions in one branch), the d-new-literal marker, and the
	-Check gate (a numbered heading that clashes with main fails, a placeholder passes). Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'number-decisions.ps1'
$pwsh = (Get-Process -Id $PID).Path
$script:failures = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}

$repo = Join-Path ([IO.Path]::GetTempPath()) "numdec-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
New-Item -ItemType Directory -Path "$repo\docs\vr", "$repo\src" | Out-Null
function G { $out = & git -C $repo @args 2>&1; if ($LASTEXITCODE -ne 0) { throw "git $args : $out" }; $out }
function Num([string[]]$Extra = @()) { $o = & $pwsh -NoProfile -File $script -Root $repo -BaseRef main @Extra 2>&1; [pscustomobject]@{ Exit = $LASTEXITCODE; Out = ($o -join "`n") } }
function Read([string]$Rel) { Get-Content -Raw -LiteralPath (Join-Path $repo $Rel) }
function Put([string]$Rel, [string]$Text) { [IO.File]::WriteAllText((Join-Path $repo $Rel), $Text) }
function Add-Decision([string]$Branch, [string]$Tag, [string]$Title, [string]$Anchor = 'D-002') {
	G switch -q -c $Branch main
	$d = Read 'docs/vr/DECISIONS.md'
	# a different anchor per branch: two inserts at the same line would be an ordinary git conflict (keep both)
	Put 'docs/vr/DECISIONS.md' ($d -replace "(?m)^## $Anchor", "## ${Tag}: $Title`n`nbody`n`n## $Anchor")
	Put "src/$Branch.cpp" "// follows $Tag ($Title)`n// the placeholder itself is called $Tag d-new-literal`n"
	G add -A; G commit -q -m $Branch
}

try {
	G init -q -b main; G config user.email t@t; G config user.name t; G config commit.gpgsign false; G config core.autocrlf false
	Put 'docs/vr/DECISIONS.md' "# Log`n`n## D-002: second`n`nx`n`n## D-001: first`n`nx`n"
	Put 'src/a.cpp' "// base`n"
	G add -A; G commit -q -m base

	Add-Decision 'a' 'D-NEW' 'alpha'
	Add-Decision 'b' 'D-NEW' 'beta' 'D-001'

	# the CI gate on a placeholder branch
	$r = Num @('-Check'); Check 'check passes with a placeholder' ($r.Exit -eq 0) $r.Out

	# merge flow, branch a first
	G switch -q a; G merge -q --no-edit main
	$r = Num; Check 'a: script ran' ($r.Exit -eq 0) $r.Out
	Check 'a: heading numbered D-003' ((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-003: alpha')
	Check 'a: code comment numbered' ((Read 'src/a.cpp') -match '// follows D-003 \(alpha\)')
	Check 'a: literal marker line kept' ((Read 'src/a.cpp') -match 'called D-NEW d-new-literal')
	G add -A; G commit -q -m numbered; G switch -q main; G merge -q --no-edit a

	# branch b after a is on main: consecutive number
	G switch -q b; G merge -q --no-edit main
	$r = Num; Check 'b: script ran' ($r.Exit -eq 0) $r.Out
	Check 'b: heading numbered D-004' ((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-004: beta')
	Check 'b: a keeps D-003' ((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-003: alpha')
	Check 'b: code comment numbered' ((Read 'src/b.cpp') -match '// follows D-004 \(beta\)')
	G add -A; G commit -q -m numbered; G switch -q main; G merge -q --no-edit b
	$ids = @([regex]::Matches((Read 'docs/vr/DECISIONS.md'), '(?m)^## (D-\d+):') | ForEach-Object { $_.Groups[1].Value })
	Check 'main: every number once' (($ids | Sort-Object -Unique).Count -eq $ids.Count -and $ids.Count -eq 4) ($ids -join ',')

	# nothing to number
	$r = Num; Check 'no placeholder: exit 0' ($r.Exit -eq 0 -and $r.Out -match 'nothing to number') $r.Out

	# two decisions in one branch
	G switch -q -c c main
	$d = Read 'docs/vr/DECISIONS.md'
	Put 'docs/vr/DECISIONS.md' ($d -replace '(?m)^## D-004', "## D-NEW-hands: hands`n`nx`n`n## D-NEW-menu: menu`n`nx`n`n## D-004")
	Put 'src/c.cpp' "// D-NEW-menu and D-NEW-hands`n"
	G add -A; G commit -q -m c
	$r = Num; Check 'c: script ran' ($r.Exit -eq 0) $r.Out
	Check 'c: tags numbered in heading order' (((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-005: hands') -and ((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-006: menu'))
	Check 'c: code comment follows the tags' ((Read 'src/c.cpp') -match 'D-006 and D-005')

	# the gate: a hand-numbered decision that main already has
	G add -A; G commit -q -m numbered
	G switch -q -c d main
	$d = Read 'docs/vr/DECISIONS.md'
	Put 'docs/vr/DECISIONS.md' ($d -replace '(?m)^## D-004', "## D-004: someone else's`n`nx`n`n## D-004")
	G add -A; G commit -q -m clash
	$r = Num @('-Check'); Check 'check fails on a clashing number' ($r.Exit -eq 1 -and $r.Out -match 'clashes') $r.Out
	G switch -q main
	$r = Num @('-Check'); Check 'check passes on main' ($r.Exit -eq 0) $r.Out
	$r = Num @('-Check', '-Strict'); Check 'strict check passes on a fully numbered main' ($r.Exit -eq 0) $r.Out

	# an unnumbered placeholder that reached main (#383): the strict check names the heading, the plain one still passes
	G switch -q -c e main
	$d = Read 'docs/vr/DECISIONS.md'
	Put 'docs/vr/DECISIONS.md' ($d -replace '(?m)^## D-004', "## D-NEW-turning: turning`n`nx`n`n## D-004")
	G add -A; G commit -q -m unnumbered
	$r = Num @('-Check', '-Strict'); Check 'strict check fails on an unnumbered heading, naming it' ($r.Exit -eq 1 -and $r.Out -match 'D-NEW-turning: turning') $r.Out
	$r = Num @('-Check'); Check 'plain check still passes a placeholder' ($r.Exit -eq 0) $r.Out

	# a doc line about the placeholder itself, in a file the branch changed, stays as written (#383)
	G switch -q -c f main
	Put 'docs/vr/WORKFLOW.md' "A new decision is written ## D-NEW: title and referenced as D-NEW. <!-- d-new-literal -->`n"
	$d = Read 'docs/vr/DECISIONS.md'
	Put 'docs/vr/DECISIONS.md' ($d -replace '(?m)^## D-004', "## D-NEW: real`n`nx`n`n## D-004")
	G add -A; G commit -q -m docline
	$r = Num; Check 'f: script ran' ($r.Exit -eq 0) $r.Out
	Check 'f: doc line about the placeholder unchanged' ((Read 'docs/vr/WORKFLOW.md') -match 'written ## D-NEW: title and referenced as D-NEW\.')
	Check 'f: heading numbered' ((Read 'docs/vr/DECISIONS.md') -match '(?m)^## D-005: real')
} finally {
	Remove-Item -LiteralPath $repo -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "number-decisions test: $($script:failures) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'number-decisions test: all passed' -ForegroundColor Green
exit 0
