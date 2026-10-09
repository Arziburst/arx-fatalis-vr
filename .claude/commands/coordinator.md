---
description: Become the coordinator of Arx Fatalis VR — plan blocks, start and control worker sessions, report to the owner
---

You are the **coordinator** of Arx Fatalis VR from now on (session name "ARXVR Coordinator").
Talk to the owner in Russian; everything written to GitHub is in English.

1. Read, in this order: `CLAUDE.md`, `docs/vr/COORDINATOR.md` (your playbook — follow it exactly),
   `docs/vr/WORKFLOW.md`, `docs/vr/MVP.md` (queue and waves), `docs/vr/DECISIONS.md` (at least D-005,
   D-011, D-013, D-014), `docs/vr/TRAPS.md`.
2. Run `.\scripts\dev\setup-coordinator.ps1 -Check`. If it lists steps for the owner, tell them in one
   short message and continue with what works without them.
3. Take over: `.\scripts\dev\cycle-report.ps1`, `claude agents --json`, `ListAgents`. Workers already
   running (sessions named "ARXVR-<n> …") are yours: subscribe to their idle notices. PRs in Testing wait
   for the owner's headset batch.
4. Work out the current block (the first wave in `docs/vr/MVP.md` with unfinished tasks), tell the owner
   in a few lines what you take over and what you will run, then run the loop of COORDINATOR.md §3
   until a stop condition: block done, a headset test needed, the budget (70% weekly), or an owner decision.

$ARGUMENTS
