<!-- PR title: ARXVR-<issue> <type>: <description>   e.g. "ARXVR-18 feat: render into per-eye OpenXR swapchains"
     Branch:   ARXVR-<issue>-<type>-<short-name>   See docs/vr/WORKFLOW.md "Commit and pull request naming". -->
Closes #

## What
<!-- What changed and why, in a few sentences. -->

## How verified
<!-- What you ran or checked: build, launch, unit of behavior, logs. -->

## How to test
<!--
For PRs labeled `needs-vr-test`: a step-by-step script for the headset.
  1. Build and run: `.\scripts\build-windows.ps1` then `.\scripts\run-windows.ps1`
  2. Load save: <name from the test save library>
  3. Do: <exact actions>
  4. Expected: <what should happen>
  5. If not: <what to report — screenshot, log file, frame rate>
For other PRs: "Nothing to test in VR" and why.
-->

## Checklist
- [ ] CI is green
- [ ] Labeled `needs-vr-test` if the change can only be judged in the headset
- [ ] New tasks or bugs found along the way are created on the board
