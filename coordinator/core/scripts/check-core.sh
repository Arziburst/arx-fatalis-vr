#!/usr/bin/env bash
# Check the coordinator core's own references (macOS / Linux, no dependencies; Windows: check-core.ps1).
#   scripts/check-core.sh [repo-root]
# Fails (exit 1) on:
#   - a relative Markdown link to a file that does not exist;
#   - a section reference (§4b, CORE §12a, §1.16) to a section that does not exist — sections are the
#     `## <id>. ` headings of CORE.md and procedures/*.md, and §1.N must name an existing hard rule.
# Run in CI on every pull request and before proposing a core change (CORE §10).
set -u
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$root" || exit 2

sections=" $(cat CORE.md procedures/*.md 2>/dev/null | sed -n 's/^## \([0-9][0-9]*[a-z]\{0,1\}\)\. .*/\1/p' | sort -u | tr '\n' ' ')"
rules="$(awk '/^## 1\. /{on=1; next} /^## /{on=0} on && /^[0-9]+\. \*\*/{n++} END{print n+0}' CORE.md)"

errors=0; files=0
while IFS= read -r f; do
  files=$((files + 1))
  dir="$(dirname "$f")"
  # one record per link or § reference outside fenced code: "<line> L <target>" / "<line> S <sec> <sub>"
  while read -r n kind a b; do
    if [ "$kind" = L ]; then
      path="${a%%#*}"
      [ -z "$path" ] || [ -e "$dir/$path" ] || { echo "${f#./}:$n: broken link $a"; errors=$((errors + 1)); }
    else
      case "$sections" in
        *" $a "*) if [ "$a" = 1 ] && [ -n "$b" ] && { [ "$b" -lt 1 ] || [ "$b" -gt "$rules" ]; }; then
                    echo "${f#./}:$n: §1.$b does not exist (CORE §1 has $rules hard rules)"; errors=$((errors + 1)); fi ;;
        *) echo "${f#./}:$n: §$a does not exist"; errors=$((errors + 1)) ;;
      esac
    fi
  done < <(awk '
    { sub(/\r$/, "") }
    /^[ \t]*```/ { code = !code; next }
    code { next }
    {
      s = $0
      while (match(s, /\]\([^) \t]+\)/)) {
        t = substr(s, RSTART + 2, RLENGTH - 3); s = substr(s, RSTART + RLENGTH)
        if (t !~ /^[a-z]+:/ && t !~ /^#/) print NR, "L", t
      }
      s = $0
      while (match(s, /§[0-9]+[a-z]?(\.[0-9]+)?/)) {
        r = substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH)
        sub(/^§/, "", r); sec = r; subn = ""
        if (index(r, ".")) { sec = substr(r, 1, index(r, ".") - 1); subn = substr(r, index(r, ".") + 1) }
        print NR, "S", sec, subn
      }
    }' "$f")
done < <(find . -name '*.md' -not -path './.git/*' | LC_ALL=C sort)

nsec=$(echo $sections | wc -w | tr -d ' ')
echo "check-core: $files files, $nsec sections, $rules hard rules, $errors errors"
[ "$errors" -eq 0 ]
