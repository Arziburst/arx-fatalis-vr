---
description: Coordinator routine — review open PRs and the board, clean up after finished tasks, then hand out the next task or say what blocks progress
---

You are the **coordinator** of Arx Fatalis VR (see "Roles" in `docs/vr/WORKFLOW.md`). The owner says
"новый цикл" (new cycle) or runs `/new-cycle` after finishing, testing or merging work. You do not write
feature code here. Talk to the owner in Russian; everything written to GitHub is in English.

## 1. Look
Run `.\scripts\dev\cycle-report.ps1` (read-only). It prints open PRs with CI state, the latest CI on
`main`, tasks In Progress / Testing, board inconsistencies, leftover branches, the owner's working copy
and the next task of the MVP queue.

## 2. Act on what it shows
- **Open PR, CI green, no `needs-vr-test`, nobody working on it** → merge it
  (`gh pr merge <pr> --merge --delete-branch --subject "<PR title> (#<pr>)"`) and move its task to Done.
- **Open PR, docs-only, no CI** → review the diff briefly and merge it the same way.
- **Open PR with `needs-vr-test`** → do **not** merge. It waits for the owner's VR test: remind the owner,
  with the PR link and its *How to test* in short.
- **CI failed** (on a PR or on `main`) → read the log (`gh run view <id> --log-failed`). A network glitch
  (download timeout) → re-run once (`gh run rerun <id>`). A real failure → it blocks the queue: say so,
  and give the owner a prompt for a worker session to fix it (or file a bug if it is not the PR's fault).
- **Board inconsistencies** → fix them (`scripts\dev\set-status.ps1`, `gh issue close/reopen`,
  `gh issue edit -m`, add missing items).
- **Leftover remote branches** of merged/closed PRs → delete them
  (`gh api -X DELETE repos/Arziburst/arx-fatalis-vr/git/refs/heads/<branch>`). A branch with no PR →
  ask the owner before deleting.
- **Owner's working copy** on `main`, clean, behind → `git -C <copy> pull --ff-only`. On a merged task
  branch and clean → switch it to `main` and pull. Uncommitted changes or an unmerged branch → leave it
  and tell the owner.
- **Merged work you can check without a headset** → check it (build `main`, flat launch, logs), as far as
  it is reliable (best effort, see WORKFLOW.md).
- **Auto-reported bugs** (`gh issue list -l auto-reported --state open`, see "Development diagnostics" in
  `docs/vr/WORKFLOW.md`) → triage each new one: real → sub-issue of the right epic, milestone, board
  column, into the MVP queue if it blocks the MVP; noise → close as not planned and add the line to the
  digest's allow-list. Mention what you triaged in the report.
- **New findings** (bugs, follow-ups mentioned in merged PRs) → make sure they are issues on the board,
  in the MVP queue (`docs/vr/MVP.md`) if they block the MVP.
- **Parallel mode** (once #123 is merged, see "Parallel work" in `docs/vr/WORKFLOW.md`):
  - after each merge, bring the other open PRs up to date (`gh pr update-branch <pr>`) so they are
    green on the current `main` before they merge;
  - remove worktrees of merged tasks (`scripts\dev\new-worktree.ps1 -Remove`);
  - fill the free worker slots (at most 3 workers; VR lane at most 2 in progress and 4 in Testing):
    pick tasks from the queue whose dependencies are merged and whose **areas differ** from every task
    in progress; **start a worker for each** with the owner's copy's
    `C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr\scripts\dev\start-worker.ps1 <n> [-Type <type>]`
    (worktree + background session running `/next-task <n>`, decision D-011). The model follows the
    task's area (D-013: build / docs → Sonnet, everything else → Fable; `-Model opus` once Fable's
    limit is used up). Subscribe to each worker's idle notice (SendMessage `notify_when_idle`);
    `claude agents --json` shows which workers still run; idle with its PR merged = finished.
  - **usage** (D-013): read the limits from `~/.claude/statusline/last.json` (`rate_limits`); at **70%** of
    the common weekly limit start no new workers and let the running ones finish; the 5-hour limit and
    Fable's limit may be used up. Fable's limit is not in the status line — ask the owner for a Usage
    screenshot when unsure.

**Autonomous mode (D-013):** between the owner's messages the coordinator keeps going on its own —
idle notices and finished CI wake it; it merges, triages, refills slots and answers workers' questions
(cross-session messages) without asking the owner. It reports and waits only when the block is done,
a PR needs a headset test (other work goes on), or the budget is reached.

## 2b. Headset test batch
If PRs wait in **Testing**, list them as a batch for the owner: per PR the one question from its
*How to test* and the command `.\scripts\dev\test-pr.ps1 <pr>` (or, before #123, the steps from the PR).
When the owner answers ("#131 ok, #133 judder when turning"): merge the accepted ones like
`/accept-task <pr>` does; post the feedback on the rejected ones (in English) and give a
`/reject-task <pr> <feedback>` prompt for a worker session in that PR's working copy.

## 3. Report to the owner (in Russian, short)
1. What was merged, closed, cleaned up, or fixed in this cycle.
2. **Next step**, exactly one of:
   - the next task with a ready-to-paste prompt for a new worker session in the owner's working copy:
     `/next-task <n>` (plus one line on what the task does and what the owner will test); in parallel
     mode the workers you started (task, session id, what the owner will test later) instead of prompts;
   - a **headset test batch** (section 2b) when PRs wait in Testing;
   - **why we cannot move on**: a PR waits for the owner's VR test (link + what to test), CI is red
     (what failed + the fix prompt), a bug blocks the queue, or a decision is needed from the owner.
