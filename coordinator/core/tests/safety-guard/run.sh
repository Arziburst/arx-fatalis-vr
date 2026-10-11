#!/usr/bin/env bash
# Run tests/safety-guard/cases.txt against a safety hook (macOS / Linux; Windows: run.ps1).
#   tests/safety-guard/run.sh bash scripts/hooks/safety-guard.sh
#   tests/safety-guard/run.sh pwsh -NoProfile -File scripts/hooks/safety-guard.ps1
# Exit 1 if any verdict differs from the expected one.
set -u
[ $# -gt 0 ] || { sed -n '2,5s/^# \{0,1\}//p' "$0"; exit 2; }
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
for b in main work; do
  git init -q -b "$b" "$tmp/$b" && git -C "$tmp/$b" -c user.email=t@t -c user.name=t commit -q --allow-empty -m init
  mkdir -p "$tmp/$b/.claude" && printf '# a project rule\nterraform[[:space:]]+destroy\n' > "$tmp/$b/.claude/coordinator-deny.txt"
done
fail=0; n=0
while IFS="$(printf '\t')" read -r expect branch cmd; do
  case "$expect" in ''|'#'*) continue ;; esac
  n=$((n + 1))
  dir="$tmp/$branch"; command -v cygpath >/dev/null 2>&1 && dir="$(cygpath -m "$dir")"   # Git Bash -> C:/...
  json="$(printf '%s' "$cmd" | awk -v cwd="$dir" '
    { gsub(/\\n/, "\001"); gsub(/\\t/, "\002"); gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); gsub(/\001/, "\\n"); gsub(/\002/, "\\t")
      printf "{\"tool_name\":\"Bash\",\"tool_input\":{\"command\":\"%s\"},\"cwd\":\"%s\"}", $0, cwd }')"
  printf '%s' "$json" | CLAUDE_PROJECT_DIR="$dir" "$@" >/dev/null 2>&1
  rc=$?
  got=allow; [ "$rc" = 2 ] && got=block
  [ "$got" = "$expect" ] || { echo "FAIL ($branch) expected $expect, got $got (exit $rc): $cmd"; fail=1; }
done < "$here/cases.txt"
echo "safety-guard cases: $n, $( [ $fail = 0 ] && echo all pass || echo FAILURES )"
exit $fail
