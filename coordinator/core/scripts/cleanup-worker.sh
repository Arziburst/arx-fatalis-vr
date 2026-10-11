#!/usr/bin/env bash
# Clean up after a merged task: stop and remove the worker session, remove its worktree and branch —
# only if the worktree exists, is clean, every commit is on the remote and a merged PR has exactly the
# pushed head (a reused branch name with newer commits does not count) (CORE §8).
# Otherwise report and change nothing.
#   scripts/cleanup-worker.sh --worktree <path> [--session <id>] [--dry-run]
# Exit codes: 0 cleaned (or would be, with --dry-run); 1 usage error or a failed removal;
# 3 a check failed, nothing changed (Windows: cleanup-worker.ps1, the same checks and codes).
set -euo pipefail

session="" wt="" dry=0
usage="usage: --worktree <path> [--session <id>] [--dry-run]"
need() { [ $# -ge 2 ] && [ -n "$2" ] || { echo "missing value for $1; $usage" >&2; exit 1; }; }
while [ $# -gt 0 ]; do
  case "$1" in
    --session) need "$@"; session="$2"; shift 2 ;;
    --worktree) need "$@"; wt="$2"; shift 2 ;;
    --dry-run) dry=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
done
[ -n "$wt" ] || { echo "$usage" >&2; exit 1; }

run() { if [ "$dry" = 1 ]; then echo "[dry-run] $*"; else "$@"; fi; }
stop_here() { echo "$1 - left as is" >&2; exit 3; }

# The main repository is found from the worktree itself, so this runs from any folder.
[ -d "$wt" ] || stop_here "NO WORKTREE: $wt does not exist (check the path)"
common="$(git -C "$wt" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)" ||
  stop_here "NOT A WORKTREE: $wt is not a git working copy"
gitdir="$(git -C "$wt" rev-parse --path-format=absolute --git-dir)"
[ "$gitdir" = "$common" ] && stop_here "MAIN CHECKOUT: $wt is the repository itself, not a worker worktree"
root="$(dirname "$common")"
[ -n "$(git -C "$wt" status --porcelain)" ] && stop_here "NOT CLEAN: $wt has uncommitted changes"
branch="$(git -C "$wt" rev-parse --abbrev-ref HEAD)"
git -C "$wt" rev-parse -q --verify '@{u}' >/dev/null || stop_here "NO UPSTREAM: $branch was never pushed"
git -C "$wt" fetch -q || stop_here "FETCH FAILED: cannot confirm $branch is on the remote"
[ -n "$(git -C "$wt" log --oneline '@{u}..')" ] && stop_here "UNPUSHED: $branch has commits not on its remote"
up="$(git -C "$wt" rev-parse '@{u}')"
command -v gh >/dev/null 2>&1 || stop_here "NO gh: cannot verify the PR for $branch is merged"
heads="$(cd "$wt" && gh pr list --head "$branch" --state merged --json headRefOid --jq '.[].headRefOid' 2>/dev/null)" ||
  stop_here "UNKNOWN: could not check the PR for $branch"
[ -n "$heads" ] || stop_here "NOT MERGED: no merged PR for $branch"
printf '%s\n' "$heads" | grep -qx "$up" ||
  stop_here "NOT MERGED: no merged PR for $branch has its pushed head ${up:0:7} (branch name reused?)"

if [ -n "$session" ]; then
  run claude stop "$session" || true
  run claude rm "$session" || true
fi
run git -C "$root" worktree remove "$wt" || { echo "REMOVE FAILED: git worktree remove $wt" >&2; exit 1; }
run git -C "$root" branch -d "$branch" || echo "branch $branch kept (not fully merged locally)"
echo "cleaned: ${session:-no session} ${wt}"
