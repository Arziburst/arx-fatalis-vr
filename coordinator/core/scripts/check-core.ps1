# Check the coordinator core's own references (Windows; macOS / Linux: check-core.sh - same checks).
#   pwsh -File scripts/check-core.ps1 [repo-root]      (or: powershell -File ...)
# Fails (exit 1) on a relative Markdown link to a missing file, or a section  reference to a section that does
# not exist (the `## <id>. ` headings of CORE.md and procedures/*.md; section 1.N must be an existing hard rule).
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$sect = [string][char]0x00A7   # not a literal: Windows PowerShell 5.1 reads a BOM-less script as ANSI
$Root = (Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $Root

$heads = @('CORE.md') + @(Get-ChildItem -Path procedures -Filter '*.md' -File | ForEach-Object { "procedures/$($_.Name)" })
$sections = @{}
foreach ($h in $heads) {
  foreach ($l in [IO.File]::ReadAllLines((Join-Path $Root $h))) {
    if ($l -match '^## (\d+[a-z]?)\. ') { $sections[$Matches[1]] = $true }
  }
}
$rules = 0; $on = $false
foreach ($l in [IO.File]::ReadAllLines((Join-Path $Root 'CORE.md'))) {
  if ($l -match '^## 1\. ') { $on = $true; continue }
  if ($l -match '^## ') { $on = $false }
  if ($on -and $l -match '^\d+\. \*\*') { $rules++ }
}

$errors = 0; $files = 0
# -Force: dot-folders (.github ...) too, as find does; ordinal order of the relative paths, as LC_ALL=C sort
$all = @(Get-ChildItem -Path . -Filter '*.md' -File -Recurse -Force | Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' })
$rels = [string[]]@($all | ForEach-Object { $_.FullName.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/' })
$byRel = New-Object System.Collections.Hashtable ([StringComparer]::Ordinal); for ($i = 0; $i -lt $all.Count; $i++) { $byRel[$rels[$i]] = $all[$i] }
[Array]::Sort($rels, [StringComparer]::Ordinal)
foreach ($rel in $rels) {
  $file = $byRel[$rel]
  $files++
  $code = $false; $n = 0
  foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
    $n++
    if ($line -match '^\s*```') { $code = -not $code; continue }
    if ($code) { continue }
    foreach ($m in [regex]::Matches($line, '\]\(([^)\s]+)\)')) {
      $t = $m.Groups[1].Value
      if ($t -match '^[a-z]+:' -or $t.StartsWith('#')) { continue }
      $path = $t.Split('#')[0]
      if ($path -and -not (Test-Path -LiteralPath (Join-Path $file.DirectoryName $path))) {
        "${rel}:${n}: broken link $t"; $errors++
      }
    }
    foreach ($m in [regex]::Matches($line, $sect + '(\d+[a-z]?)(?:\.(\d+))?')) {
      $sec = $m.Groups[1].Value; $sub = $m.Groups[2].Value
      if (-not $sections.ContainsKey($sec)) { "${rel}:${n}: ${sect}$sec does not exist"; $errors++ }
      elseif ($sec -eq '1' -and $sub -and ([int]$sub -lt 1 -or [int]$sub -gt $rules)) {
        "${rel}:${n}: ${sect}1.$sub does not exist (CORE ${sect}1 has $rules hard rules)"; $errors++
      }
    }
  }
}
"check-core: $files files, $($sections.Count) sections, $rules hard rules, $errors errors"
if ($errors) { exit 1 }
