#!/usr/bin/env bash
# Check a contract against CONTRACT.md before it is delegated (macOS / Linux, no dependencies;
# Windows: check-contract.ps1).
#   scripts/check-contract.sh <contract.md|contract.yaml> ...
# The contract is the YAML header between `---` lines, or a top-level `contract:` block.
# ERROR (exit 1) — the contract must not go out:
#   - id, title, intent, acceptance, non_goals or do_not_decide missing; intent empty;
#   - a criterion without `check`, with a check other than oracle / review / owner, or an `oracle`
#     check without `how` (a criterion with no nameable check is a planning gap, CONTRACT.md);
#   - non_goals without the two default lines (no changes beyond the fix …, do not weaken … tests …).
# WARN — allowed, but say why in the brief:
#   - budget.model, budget.escalate_to or touches missing; no criterion with an oracle check.
# Reads the structure line by line (no YAML parser), so keep the CONTRACT.md layout.
[ $# -gt 0 ] || { sed -n '2,13s/^# \{0,1\}//p' "$0"; exit 2; }
failed=0
for f in "$@"; do
  awk -v F="$f" '
    function val(s) { sub(/[ \t]+#.*$/, "", s); gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
    function err(m) { print F ": ERROR " m; ne++ }
    function warn(m) { print F ": WARN  " m; nw++ }
    { sub(/\r$/, ""); L[++n] = $0 }   # CRLF files read like LF ones (as in check-contract.ps1)
    END {
      # the contract block: front matter, or the body of a top-level "contract:", else the whole file
      s = 1; e = n
      if (L[1] ~ /^---[ \t]*$/) { s = 2; for (i = 2; i <= n; i++) if (L[i] ~ /^---[ \t]*$/) { e = i - 1; break } }
      else for (i = 1; i <= n; i++) if (L[i] ~ /^contract:[ \t]*$/) {
        s = i + 1; e = n; ind = 1000
        for (j = s; j <= n; j++) {
          if (L[j] ~ /^[^ \t]/) { e = j - 1; break }
          if (L[j] ~ /[^ \t]/) { match(L[j], /^[ \t]*/); if (RLENGTH < ind) ind = RLENGTH }
        }
        for (j = s; j <= e; j++) L[j] = substr(L[j], ind + 1)
        break
      }
      # top-level keys and the text of each
      for (i = s; i <= e; i++) {
        if (match(L[i], /^[a-z_]+:/)) { key = substr(L[i], 1, RLENGTH - 1); has[key] = 1; txt[key] = substr(L[i], RLENGTH + 1); first[key] = i }
        else if (key != "") { txt[key] = txt[key] "\n" L[i]; last[key] = i }
      }
      split("id title intent acceptance non_goals do_not_decide", req, " ")
      for (k = 1; k <= 6; k++) if (!(req[k] in has)) err("missing `" req[k] "`")
      it = txt["intent"]; gsub(/#[^\n]*/, "", it); gsub(/[>|\n \t]/, "", it)
      if (("intent" in has) && it == "") err("`intent` is empty")

      # acceptance criteria
      c = 0; ok_oracle = 0
      if ("acceptance" in has) {
        m = split(txt["acceptance"], A, "\n")
        for (i = 1; i <= m; i++) {
          line = A[i]
          if (line ~ /^[ \t]*-[ \t]*[{]?[ \t]*criterion:/) {
            c++; crit[c] = line; sub(/^[ \t]*-[ \t]*[{]?[ \t]*criterion:[ \t]*/, "", crit[c]); chk[c] = ""; how[c] = ""
            if (match(line, /check:[ \t]*[a-z]+/)) { t = substr(line, RSTART, RLENGTH); sub(/check:[ \t]*/, "", t); chk[c] = t }
            if (line ~ /how:[ \t]*[^ \t,}]/) how[c] = "y"
          } else if (c > 0 && match(line, /^[ \t]+check:/)) { chk[c] = val(substr(line, RLENGTH + 1)) }
          else if (c > 0 && match(line, /^[ \t]+how:/)) { if (val(substr(line, RLENGTH + 1)) != "") how[c] = "y" }
        }
        if (c == 0) err("`acceptance` has no criteria")
        for (i = 1; i <= c; i++) {
          if (chk[i] == "") err("criterion " i " has no `check` (" substr(crit[i], 1, 60) ")")
          else if (chk[i] !~ /^(oracle|review|owner)$/) err("criterion " i ": check `" chk[i] "` is not oracle / review / owner")
          else if (chk[i] == "oracle" && how[i] == "") err("criterion " i ": oracle check without `how`")
          if (chk[i] == "oracle") ok_oracle = 1
        }
        if (c > 0 && !ok_oracle) warn("no criterion has an oracle check: everything rests on opinion")
      }
      if ("non_goals" in has) {
        ng = tolower(txt["non_goals"])
        if (index(ng, "beyond the fix") == 0) err("non_goals lacks the default (no changes beyond the fix, its diagnostics and its test)")
        if (index(ng, "weaken") == 0) err("non_goals lacks the default (do not weaken or loosen tests, timeouts or tolerances)")
      }
      b = "\n" txt["budget"]
      if (b !~ /\n[ \t]+model:[ \t]*[^ \t\n]/) warn("budget.model missing (CORE §7)")
      if (b !~ /\n[ \t]+escalate_to:[ \t]*[^ \t\n]/) warn("budget.escalate_to missing (no stronger model to retry on)")
      tc = txt["touches"]; gsub(/#[^\n]*/, "", tc); gsub(/[ \t\n-]/, "", tc); gsub(/\[|\]/, "", tc)
      if (tc == "") warn("touches missing or empty (no conflict foresight, CORE §4a)")
      print F ": " (ne ? "FAIL" : "OK") " (" ne+0 " errors, " nw+0 " warnings)"
      exit (ne ? 1 : 0)
    }' "$f" || failed=1
done
exit $failed
