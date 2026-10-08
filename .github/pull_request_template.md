<!-- PR title: ARXVR-<issue> <type>: <description>   e.g. "ARXVR-18 feat: render into per-eye OpenXR swapchains"
     Branch:   ARXVR-<issue>-<type>-<short-name>   See docs/vr/WORKFLOW.md "Commit and pull request naming". -->
Closes #

## What
<!-- What changed and why, in a few sentences. -->

## How verified (evidence)
<!-- Measurements, not impressions, climbing the verification tiers (WORKFLOW.md): host tests, status/log
     assertions, flat or simulated runs, screenshots. Numbers where possible ("pairing 60/60, L/R diff 4.1
     vs floor 1.5"). A falsified hypothesis is a result too — say what you tried and what it showed.
     Name anything you could not verify and why (best effort). -->

## How to test
<!--
For PRs labeled `needs-vr-test`: ONE clearly defined question for the headset, then the steps.
  Question: <the one thing the owner judges, e.g. "Does the world stay still when you turn your head?">
  1. Run: `.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0` (or the desktop shortcut), SteamVR running
  2. Do: <exact actions>
  3. Expected: <what should happen>
  4. Outcomes: <what each possible answer means and what happens next>
For other PRs: "Nothing to test in VR" and why.
-->

## Deliberately not here
<!-- What this PR does not do on purpose, and every fault found but not fixed — each with its new issue number. -->

## Checklist
- [ ] CI is green
- [ ] Labeled `needs-vr-test` if the change can only be judged in the headset
- [ ] New tasks or bugs found along the way are created on the board
- [ ] A new trap (misleading symptom, stale state) is added to `docs/vr/TRAPS.md`
