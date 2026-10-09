---
description: Pick the next task from the MVP queue and implement it in its own pull request
---

You are working on Arx Fatalis VR. Follow the project rules exactly.

## 0. Load the rules
Read `CLAUDE.md`, `docs/vr/WORKFLOW.md` and `docs/vr/MVP.md` before anything else.
All artifacts (code comments, commits, PRs, issue comments) are in English; talk to the owner in Russian.

## 1. Prepare the working copy yourself
**Started with `worktree=<path>`** (the coordinator's `scripts/dev/start-worker.ps1` does that): your
**very first action**, before reading files or running any command, is the EnterWorktree tool with
`path` = that folder. You were launched in the owner's copy only because Claude Code starts background
sessions only in a trusted folder; never change anything in the owner's copy. After entering, you are
in parallel mode (next paragraph).

**Parallel mode** (see "Parallel work" in `docs/vr/WORKFLOW.md`): if this session runs in a worktree
under `C:\Users\arzib\arx-fatalis-vr-wt\`, that folder is yours alone — it was created from `origin/main`
on your task's branch. Do not switch it to `main`; bring it up to date with `git fetch origin` +
`git merge origin/main` instead. Never work in the owner's copy in parallel mode. Self-test launches
use `run-windows.ps1 -Isolated` (own log, config and saves); VR launches respect the VR lock and are
skipped with a note when the lock is busy.

The owner does not prepare anything before a task — you do. Do all of this without asking:
- `git fetch --prune origin`.
- If the working tree has uncommitted changes, stop and tell the owner exactly what is uncommitted —
  never discard anything.
- If a branch of an already merged task is still checked out (e.g. after a VR test), switch away from it;
  delete the local branch if it is merged.
- `git switch main`, `git pull --ff-only`.
- Build `main` once before changing anything: `.\scripts\build-windows.ps1`. This also sets up or
  updates vcpkg and the dependencies. If `main` does not build, that is the first thing to fix or
  report — do not start the task on a broken base.
- If something the task needs is missing on this machine (a tool, an SDK, a vcpkg port), install or
  add it yourself (`winget`, `vcpkg.json`) and mention it in the report; ask only if it needs the
  owner's account, payment, or hardware.

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
- Branch from `main`, named like the PR: `git switch -c ARXVR-<n>-<type>-<short-name>`
  (e.g. `ARXVR-18-feat-per-eye-swapchains`).

## 4. Implement and verify
- Build with `.\scripts\build-windows.ps1`. The build must pass.
- Test the game yourself before handing it over (see "Workers test the game themselves first" in
  `docs/vr/WORKFLOW.md` and `docs/vr/TESTING.md`): run `.\scripts\dev\smoke-test.ps1` (flat,
  `--loadlevel 10`; it starts the game with the test channel, waits until it is playable, quits it and
  checks exit code, `Clean shutdown`, crash reports, `[E]` lines and `status.json`). The flat run is a
  gate: a failure caused by the task must be fixed (or filed as a bug, if unrelated) before the PR.
  Add `-Commands '<channel or script-console command>',...` and `-- --loadslot <n>` to exercise what the
  task changed; assert on `status.json` numbers and `shot` screenshots, not on reading the log by eye.
  When SteamVR is running, also run `.\scripts\dev\smoke-test.ps1 -Vr` (best effort: with nobody
  wearing the headset the session stays short of FOCUSED; `xr_hello.exe` next to `arx.exe` tells
  whether the runtime itself is fine). If a self-test cannot run, say so in *How verified* rather than
  blocking on it. Quote the smoke test's verdict lines in the PR.
- Record new problems you find as tasks or bugs (sub-issues of the right epic, on the board).

## 5. Pull request
- Commit as `ARXVR-<n> <type>: <description>` with `Refs #<n>` in the body (see "Commit and pull request
  naming" in `docs/vr/WORKFLOW.md`); push; open a PR into `main` with a title in the same format, using the
  PR template.
- Fill **How verified** and **How to test**. For VR behavior, **How to test** is a precise
  step-by-step script for the headset (save to load, actions, expected result, what to report).
- Label the PR `needs-vr-test` if the result can only be judged in the headset.

## 6. CI and merge
- Wait for CI: `gh pr checks <pr> --watch`. If it fails, fix and push until it is green.
- **No `needs-vr-test` label, CI green** → merge:
  `gh pr merge <pr> --merge --delete-branch --subject "<PR title> (#<pr>)"`;
  `.\scripts\dev\set-status.ps1 -Issue <n> -Status Done`; `git switch main; git pull --ff-only`.
- **`needs-vr-test`, CI green** → `.\scripts\dev\set-status.ps1 -Issue <n> -Status Testing`. Leave the
  task branch checked out with a fresh build (`.\scripts\build-windows.ps1`), so the owner tests exactly
  this code. Then stop: give the owner the How to test steps and the PR link. Testing in VR starts with
  `.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0` (SteamVR running). The owner replies with
  `/accept-task` or `/reject-task`.

## 7. Report (in Russian)
What was done, the PR link, whether it was merged or waits for a VR test, and every task you created,
split, or moved on the board.
