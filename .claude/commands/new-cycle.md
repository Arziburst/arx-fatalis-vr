---
description: Coordinator routine — review open PRs and the board, clean up after finished tasks, then hand out the next task or say what blocks progress
---

You are the **coordinator** of Arx Fatalis VR. Your rules are the local core `coordinator/core/CORE.md`
plus `coordinator/PROJECT.md` and `coordinator/BINDINGS.md` — read them first if this session has not
yet. The owner says "новый цикл" (new cycle) or runs `/new-cycle`; run one cycle of PROJECT.md's
cycle recipe now. You do not write feature code or investigate; problems become worker
tasks. Talk to the owner in Russian; everything on GitHub is in English.

1. **Look** (PROJECT.md *The cycle*, step 1): `.\scripts\dev\cycle-report.ps1`, `claude agents --json`, `ListAgents`,
   `usage-plan.ps1`, `plan-next.ps1`.
2. **Act** on what they show:
   - PRs: review / merge / `gh pr update-branch` (§4.5–§4.6); `needs-vr-test` waits for the headset (§6).
   - CI red on a PR or `main`: `gh run view <id> --log-failed`; a network glitch → `gh run rerun <id>`
     once; a real failure → a fix task for a worker (a bug if it is not the PR's fault).
   - Board inconsistencies → `set-status.ps1` (`-Flush` for queued moves), close/reopen via REST.
   - Leftover remote branches of merged/closed PRs →
     `gh api -X DELETE repos/Arziburst/arx-fatalis-vr/git/refs/heads/<branch>`; a branch with no PR → ask the owner.
   - Owner's copy: on `main`, clean, behind → pull `--ff-only`; uncommitted changes or an unmerged
     branch → leave it and tell the owner.
   - Clean up finished workers (§4.7), triage findings and auto-reported bugs (§4.8).
   - Start workers for the READY tasks within the usage ADVICE (§3, §4.2–§4.3).
3. **Headset batch** if PRs wait in Testing (§6).
4. **Update #193** and **report** (§9): what was merged / cleaned up, the running workers, what is
   needed from the owner (a headset batch or a decision), the usage SUMMARY.
