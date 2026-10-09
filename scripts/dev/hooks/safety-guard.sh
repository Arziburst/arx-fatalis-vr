#!/usr/bin/env bash
# Fast front door of the safety hook (safety-guard.ps1, #185). Configured in .claude/settings.json.
#
# Starting PowerShell costs ~250 ms with a cold script, and the hook runs before every Bash / PowerShell /
# Edit / Write call, so this wrapper (bash builtins only, ~100 ms) hands the call to safety-guard.ps1 only
# when the tool input contains a word that one of its rules cares about: push, reset / clean / checkout /
# restore / switch, a delete command, the owner's copy folder name, a directory change, `git -C`, a shell
# run from the shell, a write to the main ref. Everything else is allowed without starting PowerShell.
# The filter is deliberately loose (a false "look closer" costs 0.4 s, a miss lets a rule down); the
# decision is always the PowerShell script's. The test (test-safety-guard.ps1) proves that no case that
# the rules block is let through here.
#
# SAFETY_GUARD_PREFILTER_ONLY=1 prints "slow" or "fast" instead of running the script (for the test).

IFS= read -r -d '' input

# Only the tool input counts: the session's cwd (the owner's copy for the coordinator) is not a reason.
rest=${input#*\"tool_input\"}

shopt -s nocasematch
word='([^[:alnum:]_]|^)'
end='([^[:alnum:]_]|$)'
pattern="push|reset|clean|checkout|restore|switch|git/refs|invoke-expression|-command|-encodedcommand|-enc[[:space:]]"
pattern="$pattern|${word}(rm|ri|del|erase|rd|rmdir|remove-item|cd|chdir|pushd|set-location|iex)${end}"
pattern="$pattern|arx-fatalis-vr([^-]|$)|-c[[:space:]]|[[:space:]]/[ck][[:space:]]"

if [[ $rest =~ $pattern ]]; then
	if [[ -n $SAFETY_GUARD_PREFILTER_ONLY ]]; then echo slow; exit 0; fi
	here=${BASH_SOURCE[0]//\\//}
	here=${here%/*}
	# the runtime switches trim the cold start of a one-shot pwsh (about 450 -> 390 ms)
	printf '%s' "$input" | POWERSHELL_TELEMETRY_OPTOUT=1 POWERSHELL_UPDATECHECK=Off DOTNET_TieredPGO=0 DOTNET_TC_QuickJitForLoops=1 \
		pwsh -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$here/safety-guard.ps1"
elif [[ -n $SAFETY_GUARD_PREFILTER_ONLY ]]; then
	echo fast
fi
exit 0
