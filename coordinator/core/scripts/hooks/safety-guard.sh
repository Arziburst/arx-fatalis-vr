#!/usr/bin/env bash
# PreToolUse safety hook for coordinator projects (Bash / PowerShell tools).
# Reads the tool call JSON on stdin; exit 2 blocks the call and shows the reason to Claude.
# A safety net only — the real enforcement is branch protection on the code host (CORE §2); deliberate
# obfuscation (eval, quoting tricks, aliases ...) is out of scope (TOOLKIT.md *Safety*).
# Extra project patterns (one extended regex per line, '#' comments): .claude/coordinator-deny.txt
# Self-test: scripts/hooks/safety-guard.sh --self-test   (prints to stdout so the session sees it)
# Works on macOS (bash 3.2, BSD awk / sed), Linux and Windows (Git Bash); jq is used when present.
# Same rules as safety-guard.ps1, both checked against tests/safety-guard/cases.txt.

if [ "${1:-}" = "--self-test" ]; then
  out=$(printf '%s' '{"tool_input":{"command":"git push --force origin main"}}' | bash "$0" 2>/dev/null; echo $?)
  if [ "$out" = "2" ]; then echo "safety-guard: OK"; exit 0; fi
  echo "safety-guard: NOT WORKING — STOP: do not start workers until it is fixed (TOOLKIT.md)."
  exit 0   # stdout reaches the session's context; a non-zero exit would only show it to the user
fi

input="$(cat)"

# Extract tool_input.command and cwd: jq if present, otherwise sed (the raw JSON string) and awk
# (decoding its escapes; linear, so a long heredoc stays fast with BSD awk).
JSON_DECODE='
{ all = (NR > 1) ? all "\n" $0 : $0 }
END {
  m = split(all, P, "\\"); printf "%s", P[1]
  for (i = 2; i <= m; i++) {
    p = P[i]
    if (p == "") { printf "\\"; i++; if (i <= m) printf "%s", P[i]; continue }
    c = substr(p, 1, 1); r = substr(p, 2)
    if (c == "n") c = "\n"; else if (c == "t") c = "\t"; else if (c == "r") c = "\r"; else if (c == "b" || c == "f") c = " "
    else if (c == "u") {
      v = 0; h = tolower(substr(p, 2, 4)); r = substr(p, 6)
      for (k = 1; k <= 4; k++) v = v * 16 + index("0123456789abcdef", substr(h, k, 1)) - 1
      c = (v > 0 && v < 128) ? sprintf("%c", v) : "?"
    }
    printf "%s%s", c, r
  }
}'
json_str() { sed -n -E 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"(([^"\\]|\\.)*)".*/\1/p' | head -n 1 | awk "$JSON_DECODE"; }
if command -v jq >/dev/null 2>&1; then
  cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty' 2>/dev/null)
  cwd=$(printf '%s' "$input" | jq -r '.cwd // empty' 2>/dev/null)
else
  cmd=$(printf '%s' "$input" | json_str command)
  cwd=$(printf '%s' "$input" | json_str cwd)
fi
nl=$'\n'; cr=$'\r'; tab=$'\t'
# Line continuations: backslash-newline (bash) and backtick-newline (PowerShell) join the lines.
cmd="${cmd//$cr$nl/$nl}"; cmd="${cmd//\\$nl/}"; cmd="${cmd//\`$nl/ }"
# Comment lines (a "#" at the start of a line) do not count for the project patterns.
lines=$(printf '%s\n' "$cmd" | grep -Ev '^[[:space:]]*#')
[ -z "$lines" ] && exit 0

block() {
  echo "Blocked by safety-guard: $1. If this is a false positive, ask the owner." >&2
  exit 2
}
has() { printf '%s' "$lines" | grep -Eiq -- "$1"; }

# --- split the command into simple commands, one per line, words separated by tabs ---
# Quote-aware: separators (newline ; | & && || ( )) inside quotes do not split, quotes are removed.
# $(...) and `...` leave a "$SUBST" marker in the word and become commands of their own; so do the
# inner command of bash -c / pwsh -Command and a heredoc fed to a shell (other heredoc bodies are data).
# An unbalanced quote makes the whole command split again with quotes ignored (stricter, never looser).
SCAN_AWK='
function wrapper(t) { return t ~ /^([{}!]|then|do|else|elif|if|while|until|time|nohup|command|exec|sudo|env|xargs)$/ }
function base(t,    b) { b = tolower(t); sub("^.*[/\\\\]", "", b); sub(/\.exe$/, "", b); return b }
function closep(s, j,    n, dep, c, q, hd, k) {
  n = length(s); dep = 1; q = ""; hd = ""
  for (; j <= n; j++) {
    k = q != "" ? runto(s, j, q, "") : runto(s, j, "\047", "[\"()<\n]"); if (k > 0) { j += k - 1; continue }
    c = substr(s, j, 1)
    if (q != "") { if (c == q) q = ""; continue }
    if (c == "\n" && hd != "") { j = hdend(s, j + 1, hd, HDDASH); hd = ""; continue }
    if (c == "\047" || c == "\"") { q = c; continue }
    if (c == "<" && substr(s, j + 1, 1) == "<" && substr(s, j + 2, 1) != "<") { hd = hdword(s, j + 2); j = HDJ; continue }
    if (c == "(") dep++
    else if (c == ")") { dep--; if (dep == 0) return j }
  }
  return n + 1
}
# the number of ordinary characters from i: up to the string ch or the regex re (a 256-character window)
function runto(s, i, ch, re,    w, k, j) {
  w = substr(s, i, 256); k = length(w)
  if (re != "" && match(w, re)) k = RSTART - 1
  if (ch != "") { j = index(w, ch); if (j && j - 1 < k) k = j - 1 }
  return k
}
function closet(s, j,    n, c) {
  n = length(s)
  while (j <= n) {
    j += runto(s, j, "`", ""); if (j > n) break
    c = substr(s, j, 1)
    if (c == "`" && substr(s, j - 1, 1) != "\\") return j
    if (c == "`") j++
  }
  return n + 1
}
# heredoc word after "<<": sets HDJ (its last index) and HDDASH; returns the delimiter
function hdword(s, j,    n, c, w) {
  n = length(s); HDDASH = 0; w = ""
  if (substr(s, j, 1) == "-") { HDDASH = 1; j++ }
  while (j <= n && (substr(s, j, 1) == " " || substr(s, j, 1) == "\t")) j++
  for (; j <= n; j++) { c = substr(s, j, 1); if (c ~ /[ \t\r\n;|&()<>]/) break; if (c != "\047" && c != "\"" && c != "\\") w = w c }
  HDJ = j - 1; return w
}
# heredoc body from j up to the delimiter line: sets HDBODY; returns the index of that line s newline
function hdend(s, j, delim, dash,    n, k, line, st) {
  n = length(s); st = j
  while (j <= n) {
    k = index(substr(s, j), "\n"); if (k == 0) k = n - j + 2
    line = substr(s, j, k - 1); if (dash) sub(/^\t+/, "", line); sub(/\r$/, "", line)
    if (line == delim) { HDBODY = substr(s, st, j - st); return j + k - 1 }
    j += k
  }
  HDBODY = substr(s, st); return n
}
function isshell(toks, nt,    j) {
  j = 1
  while (j <= nt && (wrapper(tolower(toks[j])) || toks[j] ~ /^[A-Za-z_][A-Za-z0-9_]*=/)) j++
  SHJ = j
  return j <= nt && base(toks[j]) ~ /^(bash|sh|zsh|dash|ksh|pwsh|powershell)$/
}
function seg(toks, nt, depth, qm,    j, t, line, k, m, inner, lt) {
  if (nt == 0) return
  line = ""
  for (j = 1; j <= nt; j++) { t = toks[j]; gsub(/[\t\r\n]/, " ", t); line = line ((j > 1) ? "\t" : "") t }
  OUT = OUT line "\n"
  if (!isshell(toks, nt)) return
  j = SHJ
  for (k = j + 1; k <= nt; k++) { lt = tolower(toks[k]); if (lt ~ /^-[a-z]*c$/ || lt == "-command") break }
  if (k >= nt) return
  inner = toks[k + 1]; for (m = k + 2; m <= nt; m++) inner = inner " " toks[m]
  scan(inner, depth + 1, qm)
}
function scan(s, depth, qm,    n, i, c, d, p, q, tok, st, toks, nt, k, hd, hdd, hdsh, body) {
  if (depth > 8) return
  n = length(s); q = ""; tok = ""; st = 0; nt = 0; hd = ""
  for (i = 1; i <= n; i++) {
    k = (q == "\047") ? runto(s, i, "\047", "") : (q == "\"") ? runto(s, i, "", "[\"\\\\$`]") : runto(s, i, "\047", "[ \t\r\n;|&()<>\"\\\\$`#]")
    if (k > 0) { tok = tok substr(s, i, k); i += k - 1; continue }
    c = substr(s, i, 1); d = substr(s, i + 1, 1); p = (i > 1) ? substr(s, i - 1, 1) : ""
    if (q == "\047") { if (c == "\047") q = ""; else tok = tok c; continue }
    if (q == "\"") {
      if (c == "\\" && (d == "\"" || d == "\\" || d == "$" || d == "`")) { tok = tok d; i++; continue }
      if (c == "\"") { q = ""; continue }
      if (c == "$" && d == "(") { k = closep(s, i + 2); scan(substr(s, i + 2, k - i - 2), depth + 1, qm); tok = tok "$SUBST"; i = k; continue }
      if (c == "`") { k = closet(s, i + 1); scan(substr(s, i + 1, k - i - 1), depth + 1, qm); tok = tok "$SUBST"; i = k; continue }
      tok = tok c; continue
    }
    if (qm && c == "\\" && (d == "\"" || d == "\047" || d == "\\")) { tok = tok d; st = 1; i++; continue }
    if (c == "\047" || c == "\"") { if (qm) { q = c; st = 1 }; continue }
    if (c == "$" && d == "(") { k = closep(s, i + 2); scan(substr(s, i + 2, k - i - 2), depth + 1, qm); tok = tok "$SUBST"; st = 1; i = k; continue }
    if (c == "`") { k = closet(s, i + 1); scan(substr(s, i + 1, k - i - 1), depth + 1, qm); tok = tok "$SUBST"; st = 1; i = k; continue }
    if (c == " " || c == "\t" || c == "\r") { if (tok != "") toks[++nt] = tok; tok = ""; st = 0; continue }
    if (c == "#" && tok == "" && !st) { while (i < n && substr(s, i + 1, 1) != "\n") i++; continue }
    if (c == "<" && d == "<" && substr(s, i + 2, 1) != "<") {
      if (tok != "") toks[++nt] = tok; tok = ""; st = 0
      hdsh = isshell(toks, nt); hd = hdword(s, i + 2); hdd = HDDASH; i = HDJ; continue
    }
    if (c == ">" || c == "<") {
      if (tok != "" && tok !~ /^[0-9]+$/ && tok != "&" && tok != "*" && tok !~ /[<>]$/) { toks[++nt] = tok; tok = "" }
      tok = tok c; st = 1; continue
    }
    if (c == "&" && (p == ">" || p == "<" || d == ">")) { tok = tok c; st = 1; continue }
    if (c == "\n" || c == ";" || c == "|" || c == "&" || c == "(" || c == ")") {
      if (tok != "") toks[++nt] = tok; tok = ""; st = 0
      seg(toks, nt, depth, qm); nt = 0
      if (c == "\n" && hd != "") { k = hdend(s, i + 1, hd, hdd); body = HDBODY; hd = ""; if (hdsh) scan(body, depth + 1, 0); i = k }
      continue
    }
    tok = tok c
  }
  if (q != "") FAIL = 1
  if (tok != "") toks[++nt] = tok
  seg(toks, nt, depth, qm)
}
{ all = (NR > 1) ? all "\n" $0 : $0 }
END {
  OUT = ""; FAIL = 0; scan(all, 0, 1)
  if (FAIL) { OUT = ""; scan(all, 0, 0) }
  printf "%s", OUT
}'
segments=$(printf '%s' "$cmd" | awk "$SCAN_AWK")

# --- helpers (case-insensitive matching: GIT PUSH ORIGIN MAIN counts) ---
shopt -s nocasematch
RE_ASSIGN='^[A-Za-z_][A-Za-z0-9_]*='
RE_REDIR='^[0-9*]*&?[<>]'
RE_REDIR_OP='^[0-9*]*&?[<>]+[&|]?$'
RE_RMREC='^-[rRfFvViIdDpP]*[rR][rRfFvViIdDpP]*$'
RE_PSREC='^-[rR]([eE]([cC]([uU]([rR]([sS][eE]?)?)?)?)?)?(:\$?[tT][rR][uU][eE])?$'
RE_WT='(^|[/\\])\.claude[/\\]worktrees([/\\][^/\\]+)?$'

dir="${cwd:-${CLAUDE_PROJECT_DIR:-.}}"   # where the next command runs (follows cd / Set-Location)
ov_dir=""; ov_branch=""                  # a checkout / switch earlier in this command: dir and branch

is_protected() { case "$1" in main|master) return 0 ;; esac; return 1; }
# A path relative to a base; "?" when it cannot be known (variables).
compose() {
  case "$2" in
    '') printf '%s' "$1" ;;
    *'$'*|*'%'*) printf '?' ;;
    '~'|'~/'*|'~\'*) printf '%s%s' "$HOME" "${2#?}" ;;
    /*|\\*|[A-Za-z]:*) printf '%s' "$2" ;;
    *) if [ "$1" = "?" ]; then printf '?'; else printf '%s/%s' "$1" "$2"; fi ;;
  esac
}
branch_in() {
  if [ "$1" = "$ov_dir" ]; then printf '%s' "$ov_branch"; return; fi
  [ "$1" = "?" ] && return
  git -C "$1" symbolic-ref --short -q HEAD 2>/dev/null
}
# refs/heads/main^{} , heads/main , main~0 , HEAD@{0} -> the branch name
norm_ref() {
  local x
  x=$(printf '%s' "$1" | sed -E 's/(\^\{[^}]*\}|[~^][0-9]*|@\{[^}]*\})+$//')
  case "$x" in refs/heads/*) x="${x:11}" ;; heads/*) x="${x:6}" ;; esac
  printf '%s' "$x"
}
is_current() { case "$1" in ''|HEAD|@|*'$'*) return 0 ;; esac; return 1; }

# --- git push: protected targets, every-branch pushes, pushes resolving to a protected current branch ---
# Work branches may be pushed, force-pushed (e.g. after a rebase) and deleted freely.
push_seg() {
  local d="$1" remote=0 refs=0 cur=0 tags=0 skip=0 t r dst br
  shift
  for t in "$@"; do
    if [ $skip = 1 ]; then skip=0; continue; fi
    case "$t" in
      --mirror) block "mirror push (overwrites every branch, incl. protected ones)" ;;
      --all|--branches) block "push of every branch (incl. protected ones)" ;;
      --tags) tags=1; continue ;;
      --repo=*) remote=1; continue ;;
      --repo) remote=1; skip=1; continue ;;
      -o|--push-option|--receive-pack|--exec) skip=1; continue ;;
      -*) continue ;;
    esac
    if [ $remote = 0 ]; then remote=1; continue; fi
    refs=$((refs + 1))
    r="${t#+}"
    case "$r" in ''|:) block "push of all matching branches (incl. protected ones)" ;; esac
    case "$r" in *:*) dst="${r#*:}"; [ -z "$dst" ] && dst="${r%%:*}" ;; *) dst="$r" ;; esac
    dst=$(norm_ref "$dst")
    is_protected "$dst" && block "push to a protected branch"
    is_current "$dst" && cur=1
  done
  [ $refs = 0 ] && [ $tags = 1 ] && return   # tags only: not a branch push
  if [ $refs = 0 ] || [ $cur = 1 ]; then
    br=$(branch_in "$d")
    is_protected "$br" && block "push while on $br"
  fi
}
# --- deleting a protected branch (any delete flag, the name anywhere in the arguments) ---
branch_seg() {
  local del=0 t; local names=()
  for t in "$@"; do
    case "$t" in --delete) del=1 ;; --*) ;; -*d*) del=1 ;; -*) ;; *) names+=("$t") ;; esac
  done
  [ $del = 1 ] || return
  for t in "${names[@]}"; do is_protected "$(norm_ref "$t")" && block "deleting a protected branch"; done
}
updref_seg() {
  local del=0 t; local names=()
  for t in "$@"; do case "$t" in -d|--delete) del=1 ;; -*) ;; *) names+=("$t") ;; esac; done
  [ $del = 1 ] || return
  for t in "${names[@]}"; do is_protected "$(norm_ref "$t")" && block "deleting a protected branch"; done
}
# git checkout / switch: a later bare push in the same directory pushes that branch.
checkout_seg() {
  local d="$1" t newb="" first="" nx=0
  shift
  for t in "$@"; do
    if [ $nx = 1 ]; then newb="$t"; nx=0; continue; fi
    case "$t" in
      --) return ;;
      -b|-c|--create|--force-create|--orphan) nx=1 ;;
      --detach) newb="(detached)" ;;
      -*) ;;
      *) [ -z "$first" ] && first="$t" ;;
    esac
  done
  if [ -n "$newb" ]; then ov_dir="$d"; ov_branch="$newb"; return; fi
  [ -n "$first" ] || return
  if is_protected "$(norm_ref "$first")" || { [ "$d" != "?" ] && git -C "$d" show-ref -q --verify "refs/heads/$first" 2>/dev/null; }; then
    ov_dir="$d"; ov_branch="$first"
  fi
}
git_seg() {
  local eff="$dir" sub
  while [ $# -gt 0 ]; do
    if [ "$1" = "-C" ]; then eff=$(compose "$eff" "${2:-}"); [ $# -ge 2 ] && shift; shift; continue; fi
    case "$1" in
      -c|--git-dir|--work-tree|--namespace|--config-env|--super-prefix) [ $# -ge 2 ] && shift; shift ;;
      -*) shift ;;
      *) break ;;
    esac
  done
  [ $# -gt 0 ] || return
  sub="$1"; shift
  case "$sub" in
    push) push_seg "$eff" "$@" ;;
    branch) branch_seg "$@" ;;
    update-ref) updref_seg "$@" ;;
    checkout|switch) checkout_seg "$eff" "$@" ;;
  esac
}
cd_seg() {
  local t x=""
  for t in "$@"; do case "$t" in -*) ;; *) x="$t"; break ;; esac; done
  [ -z "$x" ] && x='~'
  [ "$x" = "-" ] && { dir="?"; return; }
  dir=$(compose "$dir" "$x")
}
# --- irreversible removals: / , a drive, home, or a worktree by hand ---
rm_target() {
  local x="$1" y="$1"
  while :; do case "$y" in */|*\\) y="${y%?}" ;; *) break ;; esac; done
  [[ $y =~ $RE_WT ]] && block "removing a worktree by hand (use the cleanup script)"
  while :; do case "$x" in */\*|*\\\*|*/.|*\\.) x="${x%??}" ;; */|*\\) x="${x%?}" ;; *) break ;; esac; done
  case "$x" in
    ''|'~'|'$HOME'|'${HOME}'|'$env:USERPROFILE'|'${env:USERPROFILE}'|'$env:HOME'|'$env:HOMEDRIVE'|'$env:SystemDrive'|'%USERPROFILE%'|'%HOMEDRIVE%'|'%SystemDrive%'|[A-Za-z]:)
      block "recursive removal of /, a drive or home" ;;
  esac
}
rm_seg() {
  local b="$1" rec=0 dd=0 t; local tg=()
  shift
  for t in "$@"; do
    if [ $dd = 1 ]; then tg+=("$t"); continue; fi
    case "$t" in
      --) dd=1; continue ;;
      --recursive) rec=1; continue ;;
      --*) continue ;;
      /s) case "$b" in rd|rmdir|del|erase) rec=1; continue ;; esac ;;
      /q|/f) case "$b" in rd|rmdir|del|erase) continue ;; esac ;;
      -*) if [[ $t =~ $RE_RMREC ]] || [[ $t =~ $RE_PSREC ]]; then rec=1; fi; continue ;;
    esac
    tg+=("$t")
  done
  [ $rec = 1 ] || return
  for t in "${tg[@]}"; do rm_target "$t"; done
}
# find <path> -delete: the paths before the first expression
find_seg() {
  local t del=0 on=1; local tg=()
  for t in "$@"; do
    case "$t" in -delete) del=1 ;; -*|"!") on=0 ;; *) [ $on = 1 ] && tg+=("$t") ;; esac
  done
  [ $del = 1 ] || return
  for t in "${tg[@]}"; do rm_target "$t"; done
}

while IFS="$tab" read -r -a T; do
  # redirections (2>&1, > out.txt) are not arguments
  A=(); skip=0
  for t in "${T[@]}"; do
    if [ $skip = 1 ]; then skip=0; continue; fi
    if [[ $t =~ $RE_REDIR ]]; then [[ $t =~ $RE_REDIR_OP ]] && skip=1; continue; fi
    A+=("$t")
  done
  # the command word: after wrappers ({ ( ! then do sudo env ...) and VAR=value assignments
  i=0; n=${#A[@]}
  while [ $i -lt $n ]; do
    case "${A[$i]}" in
      '{'|'}'|'!'|then|do|else|elif|if|while|until|time|nohup|command|exec|sudo|env|xargs) i=$((i + 1)) ;;
      *) if [[ ${A[$i]} =~ $RE_ASSIGN ]]; then i=$((i + 1)); else break; fi ;;
    esac
  done
  [ $i -lt $n ] || continue
  w="${A[$i]}"; w="${w##*/}"; w="${w##*\\}"; case "$w" in *.exe) w="${w%????}" ;; esac
  case "$w" in
    git) git_seg "${A[@]:$((i + 1))}" ;;
    cd|pushd|chdir|set-location|sl|push-location) cd_seg "${A[@]:$((i + 1))}" ;;
    rm|remove-item|ri|del|erase|rd|rmdir) rm_seg "$w" "${A[@]:$((i + 1))}" ;;
    find) find_seg "${A[@]:$((i + 1))}" ;;
  esac
done <<SAFETY_GUARD_SEGMENTS_END
$segments
SAFETY_GUARD_SEGMENTS_END
shopt -u nocasematch

# --- project-specific patterns ---
deny_file="${CLAUDE_PROJECT_DIR:-.}/.claude/coordinator-deny.txt"
if [ -f "$deny_file" ]; then
  while IFS= read -r pattern || [ -n "$pattern" ]; do
    case "$pattern" in ''|'#'*) continue ;; esac
    has "$pattern" && block "project rule: $pattern"
  done < "$deny_file"
fi

exit 0
