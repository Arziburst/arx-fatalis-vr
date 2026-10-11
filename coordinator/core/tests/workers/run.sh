#!/usr/bin/env bash
# Checks of start-worker and cleanup-worker against temporary repositories, with stub `claude` and `gh`
# commands on PATH (the real claude is never called).
#   tests/workers/run.sh sh                    # the .sh scripts, with the bash running this file
#   tests/workers/run.sh ps pwsh               # the .ps1 scripts (or: ps powershell)
# Exit 1 if any check fails.
set -u
[ $# -gt 0 ] || { sed -n '2,6s/^# \{0,1\}//p' "$0"; exit 2; }
kind="$1"; shift
here="$(cd "$(dirname "$0")" && pwd)"; core="$(cd "$here/../.." && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
command -v cygpath >/dev/null 2>&1 && core="$(cygpath -m "$core")"

# stubs: claude logs its arguments; gh prints $STUB_HEADS (merged PR heads) or fails with $STUB_GH_RC
mkdir -p "$tmp/bin"
cat > "$tmp/bin/claude" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STUB_LOG"
EOF
cat > "$tmp/bin/gh" <<'EOF'
#!/usr/bin/env bash
[ "${STUB_GH_RC:-0}" = 0 ] || exit "$STUB_GH_RC"
[ -n "${STUB_HEADS:-}" ] && printf '%s\n' "$STUB_HEADS"
exit 0
EOF
cat > "$tmp/bin/claude.ps1" <<'EOF'
Add-Content -LiteralPath $env:STUB_LOG -Value ($args -join ' ')
exit 0
EOF
cat > "$tmp/bin/gh.ps1" <<'EOF'
if ($env:STUB_GH_RC -and $env:STUB_GH_RC -ne '0') { exit [int]$env:STUB_GH_RC }
if ($env:STUB_HEADS) { $env:STUB_HEADS }
exit 0
EOF
chmod +x "$tmp/bin/claude" "$tmp/bin/gh"
export PATH="$tmp/bin:$PATH" STUB_LOG="$tmp/claude.log"
: > "$STUB_LOG"

g() { git -c user.email=t@t -c user.name=t "$@"; }
git init -q --bare "$tmp/remote.git"
git init -q -b main "$tmp/repo" && g -C "$tmp/repo" commit -q --allow-empty -m init
git -C "$tmp/repo" remote add origin "$tmp/remote.git" && git -C "$tmp/repo" push -q origin main 2>/dev/null
printf 'Fix the thing.\nSecond line "quoted".\n' > "$tmp/brief.md"
if [ "$kind" = ps ]; then
  sed 's#C:\\\\ABSOLUTE\\\\PATH\\\\scripts\\\\hooks\\\\#'"$core"'/scripts/hooks/#g' "$core/settings.example.windows.json" > "$tmp/safety.json"
else
  sed "s#/ABSOLUTE/PATH#$core#g" "$core/settings.example.json" > "$tmp/safety.json"
fi
grep -q "$core/scripts/hooks/safety-guard" "$tmp/safety.json" || { echo "FAIL: could not point the safety settings at $core"; exit 1; }

start() {
  if [ "$kind" = ps ]; then
    local a=()
    while [ $# -gt 0 ]; do
      case "$1" in
        --repo) a+=(-Repo "$2"); shift 2 ;; --name) a+=(-Name "$2"); shift 2 ;;
        --branch) a+=(-Branch "$2"); shift 2 ;; --model) a+=(-Model "$2"); shift 2 ;;
        --mode) a+=(-Mode "$2"); shift 2 ;; --brief-file) a+=(-BriefFile "$2"); shift 2 ;;
        --safety) a+=(-Safety "$2"); shift 2 ;; --dry-run) a+=(-DryRun); shift ;;
        *) a+=("$1"); shift ;;
      esac
    done
    "${PS[@]}" -NoProfile -ExecutionPolicy Bypass -File "$core/scripts/start-worker.ps1" "${a[@]}"
  else bash "$core/scripts/start-worker.sh" "$@"; fi
}
cleanup() {
  if [ "$kind" = ps ]; then
    local a=()
    while [ $# -gt 0 ]; do
      case "$1" in
        --worktree) a+=(-Worktree "$2"); shift 2 ;; --session) a+=(-Session "$2"); shift 2 ;;
        --dry-run) a+=(-DryRun); shift ;; *) a+=("$1"); shift ;;
      esac
    done
    "${PS[@]}" -NoProfile -ExecutionPolicy Bypass -File "$core/scripts/cleanup-worker.ps1" "${a[@]}"
  else bash "$core/scripts/cleanup-worker.sh" "$@"; fi
}
PS=("$@")
fail=0; n=0
check() {   # check <expected exit> <description> -- command...
  local want="$1" what="$2" out rc; shift 3
  n=$((n + 1))
  out="$("$@" 2>&1)"; rc=$?
  [ "$rc" = "$want" ] || { echo "FAIL ($what): expected exit $want, got $rc"; printf '%s\n' "$out" | sed 's/^/    /'; fail=1; }
  LAST="$out"
}
expect_in() { case "$LAST" in *"$1"*) ;; *) echo "FAIL: output lacks '$1'"; printf '%s\n' "$LAST" | sed 's/^/    /'; fail=1 ;; esac; }
S=(--repo "$tmp/repo" --name "#12 worker: Fix" --branch 12-fix --model opus --brief-file "$tmp/brief.md" --safety "$tmp/safety.json")

# --- start-worker ---
if [ "$kind" = sh ]; then check 1 "start: --model without a value" -- start --repo "$tmp/repo" --model; fi
check 1 "start: bypassPermissions refused" -- start "${S[@]}" --mode bypassPermissions
expect_in "only the owner's own explicit setup"
check 1 "start: not a git repository" -- start --repo "$tmp" --name x --branch 12-fix --model opus --brief-file "$tmp/brief.md" --safety "$tmp/safety.json"
check 0 "start: dry run" -- start "${S[@]}" --dry-run
expect_in "[dry-run]"
[ -d "$tmp/repo/.claude/worktrees/12-fix" ] && { echo "FAIL: the dry run created the worktree"; fail=1; }
[ -s "$STUB_LOG" ] && { echo "FAIL: the dry run called claude"; fail=1; }
check 0 "start: real start with the stub claude" -- start "${S[@]}"
[ -d "$tmp/repo/.claude/worktrees/12-fix" ] || { echo "FAIL: no worktree after start"; fail=1; }
grep -q -- "--bg --name #12 worker: Fix --model opus --permission-mode auto --settings" "$STUB_LOG" ||
  { echo "FAIL: claude got unexpected arguments:"; sed 's/^/    /' "$STUB_LOG"; fail=1; }
grep -q 'Second line "quoted".' "$STUB_LOG" || { echo "FAIL: the brief did not reach claude intact"; fail=1; }

# --- cleanup-worker ---
wt="$tmp/repo/.claude/worktrees/12-fix"
check 3 "cleanup: mistyped worktree path" -- cleanup --worktree "$tmp/repo/.claude/worktrees/12-fxi" --session s1
expect_in "NO WORKTREE"
check 3 "cleanup: the main checkout" -- cleanup --worktree "$tmp/repo"
check 3 "cleanup: never pushed" -- cleanup --worktree "$wt"
expect_in "NO UPSTREAM"
g -C "$wt" commit -q --allow-empty -m work && git -C "$wt" push -q -u origin 12-fix 2>/dev/null
head="$(git -C "$wt" rev-parse HEAD)"
echo dirt > "$wt/dirt.txt"
check 3 "cleanup: uncommitted changes" -- cleanup --worktree "$wt"
expect_in "NOT CLEAN"
mv "$wt/dirt.txt" "$tmp/dirt.txt"
STUB_GH_RC=1 check 3 "cleanup: gh fails" -- cleanup --worktree "$wt"
expect_in "UNKNOWN"
STUB_HEADS="" check 3 "cleanup: no merged PR" -- cleanup --worktree "$wt"
expect_in "NOT MERGED"
STUB_HEADS="0123456789012345678901234567890123456789" check 3 "cleanup: merged PR of an older use of the branch name" -- cleanup --worktree "$wt"
expect_in "branch name reused"
: > "$STUB_LOG"
STUB_HEADS="$head" check 0 "cleanup: dry run" -- cleanup --worktree "$wt" --session s1 --dry-run
expect_in "[dry-run]"
[ -d "$wt" ] || { echo "FAIL: the dry run removed the worktree"; fail=1; }
[ -s "$STUB_LOG" ] && { echo "FAIL: the dry run called claude"; fail=1; }
STUB_HEADS="$head" check 0 "cleanup: merged" -- cleanup --worktree "$wt" --session s1
expect_in "cleaned: s1"
[ -d "$wt" ] && { echo "FAIL: the worktree is still there"; fail=1; }
grep -qx "stop s1" "$STUB_LOG" && grep -qx "rm s1" "$STUB_LOG" || { echo "FAIL: the session was not stopped and removed"; fail=1; }

echo "worker scripts ($kind): $n checks, $( [ $fail = 0 ] && echo all pass || echo FAILURES )"
exit $fail
