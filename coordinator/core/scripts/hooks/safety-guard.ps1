# PreToolUse safety hook for coordinator projects (Windows; macOS / Linux: safety-guard.sh - the same
# rules, checked against the same cases: tests/safety-guard/cases.txt).
# Reads the tool call JSON on stdin; exit 2 blocks the call and shows the reason to Claude.
# A safety net only - the real enforcement is branch protection on the code host (CORE section 2);
# deliberate obfuscation (iex, cmd /c, quoting tricks ...) is out of scope (TOOLKIT.md *Safety*).
# Extra project patterns (one regular expression per line, '#' comments): .claude/coordinator-deny.txt
# Self-test: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\hooks\safety-guard.ps1 -SelfTest
# Windows PowerShell 5.1 or PowerShell 7; nothing to install.
param([switch]$SelfTest)

if ($SelfTest) {
  $exe = (Get-Process -Id $PID).Path
  '{"tool_input":{"command":"git push --force origin main"}}' |
    & $exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath 2>$null | Out-Null
  if ($LASTEXITCODE -eq 2) { 'safety-guard: OK'; exit 0 }
  'safety-guard: NOT WORKING - STOP: do not start workers until it is fixed (TOOLKIT.md).'
  exit 0   # stdout reaches the session's context; a non-zero exit would only show it to the user
}

[Console]::InputEncoding = [Text.Encoding]::UTF8
$raw = [Console]::In.ReadToEnd()
try { $d = $raw | ConvertFrom-Json } catch { exit 0 }
# LOCAL EDIT (Arx Fatalis VR, 2026-10-11): the owner's working copy is never edited by a worker.
# With COORD_PROTECTED_ROOT set (worker safety settings "env"), Edit / Write / NotebookEdit under that
# root are blocked, except inside its .claude\worktrees\ (workers' own copies).
$protRoot = [string]$env:COORD_PROTECTED_ROOT
$fp = [string]$d.tool_input.file_path
if (-not $fp) { $fp = [string]$d.tool_input.notebook_path }
if ($protRoot -and $fp -and ([string]$d.tool_name) -match '^(Edit|Write|NotebookEdit|MultiEdit)$') {
  $n = { param($p) ($p -replace '/', '\').TrimEnd('\').ToLowerInvariant() }
  $root = & $n $protRoot; $file = & $n $fp
  if ($file.StartsWith($root + '\') -and -not $file.StartsWith($root + '\.claude\worktrees\')) {
    [Console]::Error.WriteLine("Blocked by safety-guard: the owner's working copy is never edited by a worker ($fp). Work in your worktree.")
    exit 2
  }
}
$cmd = [string]$d.tool_input.command
$cwd = [string]$d.cwd
# Line continuations: backslash-newline (bash) and backtick-newline (PowerShell) join the lines.
$cmd = $cmd.Replace("`r`n", "`n").Replace("\`n", '').Replace('`' + "`n", ' ')
# Comment lines (a "#" at the start of a line) do not count for the project patterns.
$lines = @($cmd -split "`n" | Where-Object { $_ -notmatch '^\s*#' })
if (-not ($lines -join '')) { exit 0 }

function Block([string]$why) {
  [Console]::Error.WriteLine("Blocked by safety-guard: $why. If this is a false positive, ask the owner.")
  exit 2
}
# Patterns are tested line by line, like grep: ^ and $ are a line's ends.
function Has([string]$re) { foreach ($l in $lines) { if ($l -match $re) { return $true } }; $false }

# --- split the command into simple commands, each a list of words ---
# Quote-aware: separators (newline ; | & && || ( )) inside quotes do not split, quotes are removed.
# $(...) and `...` leave a "$SUBST" marker in the word and become commands of their own; so do the
# inner command of bash -c / pwsh -Command and a heredoc fed to a shell (other heredoc bodies are data).
# An unbalanced quote makes the whole command split again with quotes ignored (stricter, never looser).
$script:segs = New-Object System.Collections.Generic.List[object]
$script:fail = $false
$WRAP = '^([{}!]|then|do|else|elif|if|while|until|time|nohup|command|exec|sudo|env|xargs)$'
$ASSIGN = '^[A-Za-z_][A-Za-z0-9_]*='
$SHELLS = '^(bash|sh|zsh|dash|ksh|pwsh|powershell)$'
function BaseName([string]$t) { $b = ($t.ToLowerInvariant() -split '[/\\]')[-1]; if ($b.EndsWith('.exe')) { $b = $b.Substring(0, $b.Length - 4) }; $b }
# characters that end a run of ordinary text: in single quotes, in double quotes, unquoted, in ClosePar
$RUN_SQ = [char[]]@([char]39)
$RUN_DQ = [char[]]@([char]34, [char]92, [char]36, [char]96)
$RUN_UQ = [char[]]@(32, 9, 13, 10, 59, 124, 38, 40, 41, 60, 62, 39, 34, 92, 36, 96, 35)
$RUN_CP = [char[]]@(39, 34, 40, 41, 60, 10)
# the number of ordinary characters from i
function RunTo([string]$s, [int]$i, [char[]]$set) { $k = $s.IndexOfAny($set, $i); if ($k -lt 0) { $k = $s.Length }; $k - $i }
function Ch([string]$s, [int]$i) { if ($i -ge 0 -and $i -lt $s.Length) { [string]$s[$i] } else { '' } }
function ClosePar([string]$s, [int]$j) {
  $dep = 1; $q = ''; $hd = ''
  for (; $j -lt $s.Length; $j++) {
    $k = if ($q) { RunTo $s $j ([char[]]@([char]$q)) } else { RunTo $s $j $RUN_CP }; if ($k -gt 0) { $j += $k - 1; continue }
    $c = Ch $s $j
    if ($q) { if ($c -eq $q) { $q = '' }; continue }
    if ($c -eq "`n" -and $hd) { $j = HereEnd $s ($j + 1) $hd $script:hdDash; $hd = ''; continue }
    if ($c -eq "'" -or $c -eq '"') { $q = $c; continue }
    if ($c -eq '<' -and (Ch $s ($j + 1)) -eq '<' -and (Ch $s ($j + 2)) -ne '<') { $hd = HereWord $s ($j + 2); $j = $script:hdJ; continue }
    if ($c -eq '(') { $dep++ } elseif ($c -eq ')') { $dep--; if ($dep -eq 0) { return $j } }
  }
  $s.Length
}
function CloseTick([string]$s, [int]$j) {
  while ($true) {
    $j = $s.IndexOf([char]96, $j); if ($j -lt 0) { return $s.Length }
    if ((Ch $s ($j - 1)) -ne '\') { return $j }
    $j++
  }
}
# heredoc word after "<<": sets hdJ (its last index) and hdDash; returns the delimiter
function HereWord([string]$s, [int]$j) {
  $script:hdDash = $false; $w = ''
  if ((Ch $s $j) -eq '-') { $script:hdDash = $true; $j++ }
  while ((Ch $s $j) -eq ' ' -or (Ch $s $j) -eq "`t") { $j++ }
  for (; $j -lt $s.Length; $j++) {
    $c = Ch $s $j
    if ($c -match '[ \t\r\n;|&()<>]') { break }
    if ($c -ne "'" -and $c -ne '"' -and $c -ne '\') { $w += $c }
  }
  $script:hdJ = $j - 1; $w
}
# heredoc body from j up to the delimiter line: sets hdBody; returns the index of that line's newline
function HereEnd([string]$s, [int]$j, [string]$delim, [bool]$dash) {
  $st = $j
  while ($j -lt $s.Length) {
    $k = $s.IndexOf("`n", $j); if ($k -lt 0) { $k = $s.Length }
    $line = $s.Substring($j, $k - $j); if ($dash) { $line = $line -replace '^\t+', '' }; $line = $line -replace '\r$', ''
    if ($line -ceq $delim) { $script:hdBody = $s.Substring($st, $j - $st); return $k }
    $j = $k + 1
  }
  $script:hdBody = $s.Substring([Math]::Min($st, $s.Length)); $s.Length - 1
}
function IsShell($toks) {
  $j = 0
  while ($j -lt $toks.Count -and ($toks[$j].ToLowerInvariant() -match $WRAP -or $toks[$j] -match $ASSIGN)) { $j++ }
  $script:shJ = $j
  ($j -lt $toks.Count) -and ((BaseName $toks[$j]) -match $SHELLS)
}
function Seg($list, [int]$depth, [bool]$qm) {
  $toks = @($list)
  if ($toks.Count -eq 0) { return }
  $clean = New-Object string[] $toks.Count
  for ($j = 0; $j -lt $toks.Count; $j++) { $clean[$j] = $toks[$j] -replace '[\t\r\n]', ' ' }
  $script:segs.Add($clean)
  if (-not (IsShell $toks)) { return }
  for ($k = $script:shJ + 1; $k -lt $toks.Count; $k++) { $lt = $toks[$k].ToLowerInvariant(); if ($lt -match '^-[a-z]*c$' -or $lt -eq '-command') { break } }
  if ($k -ge $toks.Count - 1) { return }
  Scan ($toks[($k + 1)..($toks.Count - 1)] -join ' ') ($depth + 1) $qm
}
function Scan([string]$s, [int]$depth, [bool]$qm) {
  if ($depth -gt 8) { return }
  $n = $s.Length; $q = ''; $tok = ''; $st = $false; $hd = ''; $hdd = $false; $hdsh = $false
  $toks = New-Object System.Collections.Generic.List[string]
  for ($i = 0; $i -lt $n; $i++) {
    $set = if ($q -eq "'") { $RUN_SQ } elseif ($q) { $RUN_DQ } else { $RUN_UQ }
    $k = RunTo $s $i $set
    if ($k -gt 0) { $tok += $s.Substring($i, $k); $i += $k - 1; continue }
    $c = [string]$s[$i]; $d = if ($i + 1 -lt $n) { [string]$s[$i + 1] } else { '' }; $p = if ($i -gt 0) { [string]$s[$i - 1] } else { '' }
    if ($q -eq "'") { if ($c -eq "'") { $q = '' } else { $tok += $c }; continue }
    if ($q -eq '"') {
      if ($c -eq '\' -and ($d -eq '"' -or $d -eq '\' -or $d -eq '$' -or $d -eq '`')) { $tok += $d; $i++; continue }
      if ($c -eq '"') { $q = ''; continue }
      if ($c -eq '$' -and $d -eq '(') { $k = ClosePar $s ($i + 2); Scan $s.Substring($i + 2, $k - $i - 2) ($depth + 1) $qm; $tok += '$SUBST'; $i = $k; continue }
      if ($c -eq '`') { $k = CloseTick $s ($i + 1); Scan $s.Substring($i + 1, $k - $i - 1) ($depth + 1) $qm; $tok += '$SUBST'; $i = $k; continue }
      $tok += $c; continue
    }
    if ($qm -and $c -eq '\' -and ($d -eq '"' -or $d -eq "'" -or $d -eq '\')) { $tok += $d; $st = $true; $i++; continue }
    if ($c -eq "'" -or $c -eq '"') { if ($qm) { $q = $c; $st = $true }; continue }
    if ($c -eq '$' -and $d -eq '(') { $k = ClosePar $s ($i + 2); Scan $s.Substring($i + 2, $k - $i - 2) ($depth + 1) $qm; $tok += '$SUBST'; $st = $true; $i = $k; continue }
    if ($c -eq '`') { $k = CloseTick $s ($i + 1); Scan $s.Substring($i + 1, $k - $i - 1) ($depth + 1) $qm; $tok += '$SUBST'; $st = $true; $i = $k; continue }
    if ($c -eq ' ' -or $c -eq "`t" -or $c -eq "`r") { if ($tok) { $toks.Add($tok) }; $tok = ''; $st = $false; continue }
    if ($c -eq '#' -and -not $tok -and -not $st) { while ($i -lt $n - 1 -and (Ch $s ($i + 1)) -ne "`n") { $i++ }; continue }
    if ($c -eq '<' -and $d -eq '<' -and (Ch $s ($i + 2)) -ne '<') {
      if ($tok) { $toks.Add($tok) }; $tok = ''; $st = $false
      $hdsh = IsShell $toks; $hd = HereWord $s ($i + 2); $hdd = $script:hdDash; $i = $script:hdJ; continue
    }
    if ($c -eq '>' -or $c -eq '<') {
      if ($tok -and $tok -notmatch '^[0-9]+$' -and $tok -ne '&' -and $tok -ne '*' -and $tok -notmatch '[<>]$') { $toks.Add($tok); $tok = '' }
      $tok += $c; $st = $true; continue
    }
    if ($c -eq '&' -and ($p -eq '>' -or $p -eq '<' -or $d -eq '>')) { $tok += $c; $st = $true; continue }
    if ($c -eq "`n" -or $c -eq ';' -or $c -eq '|' -or $c -eq '&' -or $c -eq '(' -or $c -eq ')') {
      if ($tok) { $toks.Add($tok) }; $tok = ''; $st = $false
      Seg $toks $depth $qm; $toks = New-Object System.Collections.Generic.List[string]
      if ($c -eq "`n" -and $hd) { $k = HereEnd $s ($i + 1) $hd $hdd; $body = $script:hdBody; $hd = ''; if ($hdsh) { Scan $body ($depth + 1) $false }; $i = $k }
      continue
    }
    $tok += $c
  }
  if ($q) { $script:fail = $true }
  if ($tok) { $toks.Add($tok) }
  Seg $toks $depth $qm
}
$joined = $cmd
Scan $joined 0 $true
if ($script:fail) { $script:segs.Clear(); Scan $joined 0 $false }

# --- helpers (case-insensitive matching: GIT PUSH ORIGIN MAIN counts) ---
$REDIR = '^[0-9*]*&?[<>]'
$REDIR_OP = '^[0-9*]*&?[<>]+[&|]?$'
$RMREC = '^-[rfvidp]*r[rfvidp]*$'
$PSREC = '^-r(e(c(u(r(se?)?)?)?)?)?(:\$?true)?$'
$WT = '(^|[/\\])\.claude[/\\]worktrees([/\\][^/\\]+)?$'

$script:dir = if ($cwd) { $cwd } elseif ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { '.' }
$script:ovDir = ''; $script:ovBranch = ''   # a checkout / switch earlier in this command: dir and branch

function IsProtected([string]$x) { $x -match '^(main|master)$' }
# A path relative to a base; "?" when it cannot be known (variables).
function Compose([string]$b, [string]$p) {
  if (-not $p) { return $b }
  if ($p.Contains('$') -or $p.Contains('%')) { return '?' }
  if ($p -eq '~' -or $p.StartsWith('~/') -or $p.StartsWith('~\')) { return $HOME + $p.Substring(1) }
  if ($p -match '^([/\\]|[A-Za-z]:)') { return $p }
  if ($b -eq '?') { return '?' }
  "$b/$p"
}
function BranchIn([string]$dd) {
  if ($dd -ceq $script:ovDir) { return $script:ovBranch }
  if ($dd -eq '?') { return '' }
  $b = git -C $dd symbolic-ref --short -q HEAD 2>$null
  if ($LASTEXITCODE -eq 0) { "$b".Trim() } else { '' }
}
# refs/heads/main^{} , heads/main , main~0 , HEAD@{0} -> the branch name
function NormRef([string]$x) {
  $x = $x -creplace '(\^\{[^}]*\}|[~^][0-9]*|@\{[^}]*\})+$', ''
  if ($x -match '^refs/heads/') { $x = $x.Substring(11) } elseif ($x -match '^heads/') { $x = $x.Substring(6) }
  $x
}
function IsCurrent([string]$x) { $x -eq '' -or $x -eq 'HEAD' -or $x -eq '@' -or $x.Contains('$') }

# --- git push: protected targets, every-branch pushes, pushes resolving to a protected current branch ---
# Work branches may be pushed, force-pushed (e.g. after a rebase) and deleted freely.
function PushSeg([string]$dd, [string[]]$a) {
  $remote = $false; $refs = 0; $cur = $false; $tags = $false; $skip = $false
  foreach ($t in $a) {
    if ($skip) { $skip = $false; continue }
    if ($t -eq '--mirror') { Block 'mirror push (overwrites every branch, incl. protected ones)' }
    if ($t -eq '--all' -or $t -eq '--branches') { Block 'push of every branch (incl. protected ones)' }
    if ($t -eq '--tags') { $tags = $true; continue }
    if ($t -like '--repo=*') { $remote = $true; continue }
    if ($t -eq '--repo') { $remote = $true; $skip = $true; continue }
    if ($t -eq '-o' -or $t -eq '--push-option' -or $t -eq '--receive-pack' -or $t -eq '--exec') { $skip = $true; continue }
    if ($t.StartsWith('-')) { continue }
    if (-not $remote) { $remote = $true; continue }
    $refs++
    $r = if ($t.StartsWith('+')) { $t.Substring(1) } else { $t }
    if ($r -eq '' -or $r -eq ':') { Block 'push of all matching branches (incl. protected ones)' }
    if ($r.Contains(':')) { $dst = $r.Substring($r.IndexOf(':') + 1); if (-not $dst) { $dst = $r.Substring(0, $r.IndexOf(':')) } } else { $dst = $r }
    $dst = NormRef $dst
    if (IsProtected $dst) { Block 'push to a protected branch' }
    if (IsCurrent $dst) { $cur = $true }
  }
  if ($refs -eq 0 -and $tags) { return }   # tags only: not a branch push
  if ($refs -eq 0 -or $cur) {
    $br = BranchIn $dd
    if (IsProtected $br) { Block "push while on $br" }
  }
}
# --- deleting a protected branch (any delete flag, the name anywhere in the arguments) ---
function BranchSeg([string[]]$a) {
  $del = $false; $names = @()
  foreach ($t in $a) {
    if ($t -eq '--delete') { $del = $true } elseif ($t.StartsWith('--')) { } elseif ($t.StartsWith('-')) { if ($t -match 'd') { $del = $true } } else { $names += $t }
  }
  if (-not $del) { return }
  foreach ($t in $names) { if (IsProtected (NormRef $t)) { Block 'deleting a protected branch' } }
}
function UpdRefSeg([string[]]$a) {
  $del = $false; $names = @()
  foreach ($t in $a) { if ($t -eq '-d' -or $t -eq '--delete') { $del = $true } elseif (-not $t.StartsWith('-')) { $names += $t } }
  if (-not $del) { return }
  foreach ($t in $names) { if (IsProtected (NormRef $t)) { Block 'deleting a protected branch' } }
}
# git checkout / switch: a later bare push in the same directory pushes that branch.
function CheckoutSeg([string]$dd, [string[]]$a) {
  $newb = ''; $first = ''; $nx = $false
  foreach ($t in $a) {
    if ($nx) { $newb = $t; $nx = $false; continue }
    if ($t -eq '--') { return }
    if ($t -eq '-b' -or $t -eq '-c' -or $t -eq '--create' -or $t -eq '--force-create' -or $t -eq '--orphan') { $nx = $true }
    elseif ($t -eq '--detach') { $newb = '(detached)' }
    elseif ($t.StartsWith('-')) { }
    elseif (-not $first) { $first = $t }
  }
  if ($newb) { $script:ovDir = $dd; $script:ovBranch = $newb; return }
  if (-not $first) { return }
  $known = IsProtected (NormRef $first)
  if (-not $known -and $dd -ne '?') { git -C $dd show-ref -q --verify "refs/heads/$first" 2>$null | Out-Null; $known = ($LASTEXITCODE -eq 0) }
  if ($known) { $script:ovDir = $dd; $script:ovBranch = $first }
}
function GitSeg([string[]]$a) {
  $eff = $script:dir; $i = 0
  while ($i -lt $a.Count) {
    $t = $a[$i]
    if ($t -ceq '-C') { $v = if ($i + 1 -lt $a.Count) { $a[$i + 1] } else { '' }; $eff = Compose $eff $v; $i += 2; continue }
    if ($t -match '^(-c|--git-dir|--work-tree|--namespace|--config-env|--super-prefix)$') { $i += 2; continue }
    if ($t.StartsWith('-')) { $i++; continue }
    break
  }
  if ($i -ge $a.Count) { return }
  $sub = $a[$i]; $rest = @(if ($i + 1 -lt $a.Count) { $a[($i + 1)..($a.Count - 1)] })
  switch ($sub.ToLowerInvariant()) {
    'push' { PushSeg $eff $rest }
    'branch' { BranchSeg $rest }
    'update-ref' { UpdRefSeg $rest }
    'checkout' { CheckoutSeg $eff $rest }
    'switch' { CheckoutSeg $eff $rest }
  }
}
function CdSeg([string[]]$a) {
  $x = ''
  foreach ($t in $a) { if (-not $t.StartsWith('-')) { $x = $t; break } }
  if (-not $x) { $x = '~' }
  if ($x -eq '-') { $script:dir = '?'; return }
  $script:dir = Compose $script:dir $x
}
# --- irreversible removals: / , a drive, home, or a worktree by hand ---
function RmTarget([string]$x) {
  $y = $x -replace '[/\\]+$', ''
  if ($y -match $WT) { Block 'removing a worktree by hand (use the cleanup script)' }
  while ($true) {
    if ($x -match '[/\\][*.]$') { $x = $x.Substring(0, $x.Length - 2) }
    elseif ($x -match '[/\\]$') { $x = $x.Substring(0, $x.Length - 1) }
    else { break }
  }
  if ($x -match '^(|~|\$HOME|\$\{HOME\}|\$env:USERPROFILE|\$\{env:USERPROFILE\}|\$env:HOME|\$env:HOMEDRIVE|\$env:SystemDrive|%USERPROFILE%|%HOMEDRIVE%|%SystemDrive%|[A-Za-z]:)$') {
    Block 'recursive removal of /, a drive or home'
  }
}
function RmSeg([string]$b, [string[]]$a) {
  $rec = $false; $dd = $false; $tg = @()
  foreach ($t in $a) {
    if ($dd) { $tg += $t; continue }
    if ($t -eq '--') { $dd = $true; continue }
    if ($t -eq '--recursive') { $rec = $true; continue }
    if ($t.StartsWith('--')) { continue }
    $cmdStyle = $b -match '^(rd|rmdir|del|erase)$'
    if ($t -eq '/s' -and $cmdStyle) { $rec = $true; continue }
    if (($t -eq '/q' -or $t -eq '/f') -and $cmdStyle) { continue }
    if ($t.StartsWith('-')) { if ($t -match $RMREC -or $t -match $PSREC) { $rec = $true }; continue }
    $tg += $t
  }
  if (-not $rec) { return }
  foreach ($t in $tg) { RmTarget $t }
}
# find <path> -delete: the paths before the first expression
function FindSeg([string[]]$a) {
  $del = $false; $on = $true; $tg = @()
  foreach ($t in $a) { if ($t -eq '-delete') { $del = $true } elseif ($t.StartsWith('-') -or $t -eq '!') { $on = $false } elseif ($on) { $tg += $t } }
  if (-not $del) { return }
  foreach ($t in $tg) { RmTarget $t }
}

foreach ($segT in $script:segs) {
  # redirections (2>&1, > out.txt) are not arguments
  $words = @(); $skip = $false
  foreach ($t in $segT) {
    if ($skip) { $skip = $false; continue }
    if ($t -match $REDIR) { if ($t -match $REDIR_OP) { $skip = $true }; continue }
    $words += $t
  }
  # the command word: after wrappers ({ ( ! then do sudo env ...) and VAR=value assignments
  $i = 0
  while ($i -lt $words.Count -and ($words[$i] -match $WRAP -or $words[$i] -match $ASSIGN)) { $i++ }
  if ($i -ge $words.Count) { continue }
  $cw = BaseName $words[$i]
  $rest = @(if ($i + 1 -lt $words.Count) { $words[($i + 1)..($words.Count - 1)] })
  switch -regex ($cw) {
    '^git$' { GitSeg $rest }
    '^(cd|pushd|chdir|set-location|sl|push-location)$' { CdSeg $rest }
    '^(rm|remove-item|ri|del|erase|rd|rmdir)$' { RmSeg $cw $rest }
    '^find$' { FindSeg $rest }
  }
}

# --- project-specific patterns (POSIX classes such as [[:space:]] work in both scripts) ---
$proj = if ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { '.' }
$denyFile = Join-Path $proj '.claude/coordinator-deny.txt'
if (Test-Path -LiteralPath $denyFile) {
  foreach ($p in [IO.File]::ReadAllLines((Resolve-Path -LiteralPath $denyFile).Path)) {
    if (-not $p -or $p.StartsWith('#')) { continue }
    $re = $p.Replace('[:space:]', '\s').Replace('[:alnum:]', 'A-Za-z0-9').Replace('[:alpha:]', 'A-Za-z').Replace('[:digit:]', '0-9')
    if (Has $re) { Block "project rule: $p" }
  }
}
exit 0
