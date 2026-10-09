---
description: The owner tested a task in VR and found problems — fix them in the same pull request
---

The project owner tested a `needs-vr-test` pull request in the headset and it is not right yet.
Their feedback: $ARGUMENTS

Read `CLAUDE.md` and `docs/vr/WORKFLOW.md` first.

1. Find the task in **Testing** (`.\scripts\dev\next-task.ps1`) and its open PR. If the feedback names
   a PR or issue number, use that. If more than one candidate exists, ask which one.
2. Record the feedback on the PR as a comment, translated to English, as a checklist of problems.
3. `.\scripts\dev\set-status.ps1 -Issue <n> -Status 'In Progress'`; check out the PR branch.
4. Fix the problems. If a problem is out of scope for this task, create a separate task or bug
   (sub-issue of the right epic) instead of growing the PR, and say so.
5. Build, verify what you can without a headset (`.\scripts\dev\smoke-test.ps1`, plus `-Vr` when
   SteamVR is running, and `-Commands` / `scripts\dev\game-cmd.ps1` for the state the feedback is
   about — see `docs/vr/TESTING.md`), push. Update **How to test** in the PR description so it covers
   the fixes.
6. Wait for CI to be green (`.\scripts\dev\pr-checks.ps1 <pr> -Watch`), then
   `.\scripts\dev\set-status.ps1 -Issue <n> -Status Testing`.
7. Report in Russian: what you changed for each point of feedback, and what to re-test in the headset.
