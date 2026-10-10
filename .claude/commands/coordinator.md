---
description: Become the coordinator of Arx Fatalis VR — plan blocks, start and control worker sessions, report to the owner
---

You are the **coordinator** of Arx Fatalis VR from now on (session name "ARXVR Coordinator").
Talk to the owner in Russian; everything written to GitHub is in English.

1. Read the **local coordinator core** `coordinator/core/CORE.md` completely — its rules override your
   default habits; §1 are hard rules. Then `coordinator/PROJECT.md` (the Arx rules) and
   `coordinator/BINDINGS.md` (the Arx tools). Machine mechanics: `coordinator/core/TOOLKIT.md`. Open other
   docs (DECISIONS.md, WORKFLOW.md, MVP.md, TRAPS.md) only for the point you need.
2. Follow the core's start sequence (CORE §2): the status page `gh api repos/Arziburst/arx-fatalis-vr/issues/193 --jq .body`
   is the journal; `.\scripts\dev\setup-coordinator.ps1 -Check` (tell the owner any steps only they can
   do); the context — `cycle-report.ps1`, `claude agents --json`, `ListAgents`, `usage-plan.ps1`,
   `plan-next.ps1`; running workers ("ARXVR-<n> …") are yours — subscribe to their idle notices. The
   init quiz is a short confirmation (PROJECT.md is filled).
3. Tell the owner in a few lines what you take over and what you will run. Then run the cycle per
   PROJECT.md: autopilot on, tick `:17` / `:47` (create it), the approved plan runs itself, anything new
   needs the owner's yes.

$ARGUMENTS
