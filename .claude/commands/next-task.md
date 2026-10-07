---
description: Pick the next task from the MVP queue and implement it in its own pull request
---

You are working on Arx Fatalis VR. Follow the project rules exactly.

## 0. Load the rules
Read `CLAUDE.md`, `docs/vr/WORKFLOW.md` and `docs/vr/MVP.md` before anything else.
All artifacts (code comments, commits, PRs, issue comments) are in English; talk to the owner in Russian.

## 1. Sync
- `git fetch origin`, make sure the working tree is clean, `git switch main`, `git pull --ff-only`.
- If the tree is not clean, stop and tell the owner what is uncommitted — do not discard anything.

## 2. Pick the task
Run `.\scripts\dev\next-task.ps1`.
- If a task is in **Testing**, remind the owner it is waiting for their VR test, then continue.
- Take the task shown as **NEXT** (an In Progress task first, otherwise the first Todo task of the MVP queue).
- If `$ARGUMENTS` names an issue number, take that one instead.
- Read the issue and its epic (`gh issue view <n>`), and the code it touches.
- Tell the owner in one or two sentences which task you took and what you plan to do. Do not wait
  for confirmation unless the task is ambiguous or blocked.

## 3. Start
- `.\scripts\dev\set-status.ps1 -Issue <n> -Status 'In Progress'`
- If the issue has no concrete **Definition of done**, write one into the issue now (`gh issue edit`).
- If the task is too big for one session, split it into sub-issues of the same epic, add them to the
  queue in `docs/vr/MVP.md`, and take the first part.
- Branch from `main`: `git switch -c task/<n>-<short-name>`.

## 4. Implement and verify
- Build with `.\scripts\build-windows.ps1`. The build must pass.
- Verify everything you can without a headset (build, launch in flat mode with
  `.\scripts\run-windows.ps1 -- --no-vr` where relevant, logs, code paths).
- Record new problems you find as tasks or bugs (sub-issues of the right epic, on the board).

## 5. Pull request
- Commit with `#<n>` in the message; push; open a PR into `main` using the PR template.
- Fill **How verified** and **How to test**. For VR behavior, **How to test** is a precise
  step-by-step script for the headset (save to load, actions, expected result, what to report).
- Label the PR `needs-vr-test` if the result can only be judged in the headset.

## 6. CI and merge
- Wait for CI: `gh pr checks <pr> --watch`. If it fails, fix and push until it is green.
- **No `needs-vr-test` label, CI green** → merge: `gh pr merge <pr> --merge --delete-branch`;
  `.\scripts\dev\set-status.ps1 -Issue <n> -Status Done`.
- **`needs-vr-test`, CI green** → `.\scripts\dev\set-status.ps1 -Issue <n> -Status Testing`, and stop:
  give the owner the How to test steps and the PR link. The owner replies with `/accept-task` or `/reject-task`.

## 7. Report (in Russian)
What was done, the PR link, whether it was merged or waits for a VR test, and every task you created,
split, or moved on the board.
