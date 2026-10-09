# The coordinator — playbook

How the coordinator session of Arx Fatalis VR works, so that any new session on any machine can take
the role at once: the owner opens Claude Code in their working copy and types **`/coordinator`**.
The decisions behind it: D-005 (CI decides merges), D-011 (the coordinator starts workers, workers run
without permission prompts), D-013 (autonomous blocks, budget, models), D-014 (launch control, #147).
Roles and the worker side: [WORKFLOW.md](WORKFLOW.md); the queue and the waves: [MVP.md](MVP.md).

## 1. Who the coordinator is
- **The owner talks only to the coordinator** (in Russian; everything on GitHub in English). They do not
  open worker sessions; VR verdicts, questions and decisions all go through the coordinator.
- The coordinator **plans and controls**: writes and sharpens tasks, starts and follows workers, triages
  what they find, merges, cleans up, keeps the board and the plan current, reports.
- It **never investigates or fixes anything itself** — a problem the owner reports becomes a task with a
  Definition of done for a worker. A command or two to write a precise task is fine; diagnosis,
  reproduction and code are the worker's. Its own changes are limited to process docs and coordination
  tooling (this file, `/new-cycle`, `start-worker.ps1`, …), each in its own PR.
- **Player-experience design is agreed with the owner first** (D-017): before writing or changing the
  issue of a task about controls, interaction, comfort, UI placement or look, or feedback — and before
  starting its worker — send the owner a short design summary with concrete questions, play the design
  back in your own words, wait for a yes, record it in DECISIONS.md. Typical tasks: #31–#35, #37, #38,
  #49, #116, #43–#45. Technical internals need no owner.
- Session name: **"ARXVR Coordinator"** (set by the project hook on `/coordinator`, `/new-cycle`,
  "новый цикл"). Workers are named **"ARXVR-<n> <issue title>"** by `start-worker.ps1`.

## 2. First start on a machine
1. Clone the repository (that clone is "the owner's copy" / the main working copy), build once
   (`scripts\build-windows.ps1`).
2. `scripts\dev\setup-coordinator.ps1` — sets the personal settings the loop needs (status line with the
   usage limits, cross-session messages accepted, sessions continue after the 5-hour reset) and lists
   what only the owner can do:
   - accept the bypass-permissions disclaimer once: `claude --dangerously-skip-permissions`, choose
     **"Yes, I accept" with the arrow keys** (the default is "No, exit"), `/exit`;
   - trust the main working copy: `claude` in it, "Yes, proceed", `/exit` — background sessions start
     only in a trusted folder, and trust does not extend to subfolders or new worktrees;
   - `gh auth login`.
3. Open Claude Code in the owner's copy and type `/coordinator`.

The coordinator stays on `main` in the owner's copy and changes nothing there except `git pull`. For its
own docs or tooling PRs it uses a worktree (`scripts\dev\new-worktree.ps1 -Issue 1 -Type docs -Name <x>`).

## 3. The loop
Between the owner's messages the coordinator keeps going on its own, woken by events, never by polling.

**Look** — `scripts\dev\cycle-report.ps1` (PRs and CI, board, leftovers, the owner's copy, next task),
`claude agents --json` (workers: busy / idle), `ListAgents` (names to message), new issues filed by
workers (`gh issue list --search "created:>=<date>"`), the usage limits (§5).

**Act**
- **Review before merge (D-021, balanced by D-022):** a worker whose PR is ready (CI green) messages
  "PR #<n> ready for review" and waits. The coordinator decides:
  - **independent reviewer** (`scripts\dev\start-reviewer.ps1 <pr>`, Sonnet, effort **medium**, checks the
    Definition of done and the decisions only — not a general code review) for **risky PRs**: engine / VR
    code, `needs-vr-test`, tasks with several Definition-of-done items or a history of gaps;
  - **no reviewer** — read the PR body against the Definition of done yourself and say "merge" — for
    docs, research (the owner decides the catalog anyway) and small script changes.
  PASS → the worker merges (or, `needs-vr-test`, it goes to the owner's headset batch); GAPS → forward the
  list, re-check after the fix.
- **Merge** green, reviewed PRs without `needs-vr-test` (the worker merges, D-005); docs-only PRs after
  a short look. After a merge, bring other open PRs up to date (`gh pr update-branch`).
- **Clean up** a worker whose PR is merged: `claude stop <id>`, `claude rm <id>`,
  `scripts\dev\new-worktree.ps1 -Issue <n> -Remove`. Keep a worker alive while its PR waits for the
  owner's headset test (it fixes a rejection faster with its context). Pull the owner's copy
  (`git pull --ff-only`); after a merge that changes the desktop launcher, re-run
  `scripts\create-desktop-shortcut.ps1`. Never touch sessions of other projects.
- **Triage** what workers report (new bugs, follow-ups, scope notes on issues): real and MVP-blocking →
  milestone M1, board, into the queue/wave in MVP.md with its dependencies; otherwise Backlog / M2.
  Sharpen the Definition of done where it is vague.
- **Refill slots** with `scripts\dev\start-worker.ps1 <n> [-Type <type>] [-Model <m>]` (run it from the owner's
  copy): at most **3 workers**, tasks of **different areas**, dependencies merged, VR lane at most 2 in
  progress and 4 in Testing. It creates the worktree, starts `claude --bg` in the owner's copy with
  `/next-task <n> worktree=<path>` (the worker enters its worktree first), names the session, picks the
  model (§6). Then subscribe: `SendMessage(to: <worker name>, notify_when_idle: true)`.
- **Answer workers** — a worker that stops with a question: read `claude logs <id>`, answer with
  `SendMessage` (first line = the point). Only questions that are the owner's decision go to the owner.
- **Watch the workers (D-021):** the status line writes a ledger per session
  (`~/.claude/statusline/sessions/<id>.json`: task branch, model, effort, context %, spend, duration).
  A worker over ~60% context or busy for hours without a commit → read its log, help, or split the task;
  the spend per task calibrates the model choice (§6).
- **Pick the order by rule:** `scripts\dev\plan-next.ps1` (#187, once merged) lists the tasks ready to
  start; tasks labelled `needs-design` wait for the owner's design talk (D-017).
- **Keep the status page current:** the pinned issue **"ARXVR status"** (edited via REST each cycle):
  current block, running workers (task, model, state), PRs waiting for review or for the headset, the
  owner's open questions, weekly usage. The owner reads it in the browser; a new coordinator starts from it.
- **Wait** — idle notices, background CI waits (`gh pr checks --watch` or a small script with
  `run_in_background`) and the usage guard (a background script that exits at 80% weekly or 95% five-hour, and every
  ~45 min as a heartbeat to catch stuck workers) wake the coordinator. No sleep loops in the foreground,
  no "are you done?" messages.
- **Parallel tooling:** two build-area tasks may run together when their files do not overlap (e.g. a
  hook script and the board scripts); otherwise one area, one worker.

**Stop and report** only when:
1. **the block is done** — every task of the current wave is merged or waits for the headset;
2. **a PR needs the owner's headset test** — say so with the steps; work that needs no headset goes on;
   the VR lane waits for the verdict (owner, 2026-10-09: no stacking VR PRs on untested ones);
3. **the budget is reached** (§5);
4. a decision only the owner can make.

## 4. Blocks and headset tests
- A **block** is a wave of [MVP.md](MVP.md#waves). The coordinator plans it, fills it, and keeps the
  wave table current as tasks are added or split.
- The owner tests in the headset **at the end of a block**, all PRs in Testing in one sitting. Per PR:
  the one question from its *How to test* and `scripts\dev\test-pr.ps1 <pr>` (from the owner's copy;
  builds that PR's worktree if needed and launches it; SteamVR is started by the owner).
- The coordinator **launches the test for the owner** when they say "запускай #<n>" (`test-pr.ps1 <pr> -StartSteamVR`)
  and **closes the game and SteamVR right after the verdict** — SteamVR left running would let workers
  switch the headset on with their VR self-tests (D-014).
- Verdicts come to the coordinator ("#18 ok", "#18 judder when turning"): accepted → merge (like
  `/accept-task`), in dependency order; rejected → the feedback goes to the PR's worker (`SendMessage`),
  or a new worker with `/reject-task <pr> <feedback>` if the old one is gone. Post the feedback on the
  PR in English.
- **Ask for impressions (D-021):** after each verdict, ask the owner for a sentence or two — what felt
  good, what got in the way (e.g. "a little blurry" on #18). Triage them like worker findings: a note on the
  task that will fix it, a new task, or a question for the owner's next design talk.

## 4b. Finding what we do not know yet (D-021)
- **Research radar** — one reference per block (Sonnet research task, catalog rows with an empty
  Decision, the owner decides): OpenMW VR (#188, first), Gothic VR mods, Skyrim VR with VRIK / HIGGS /
  Spell Wheel, Blade & Sorcery (gesture magic), Doom 3 BFG VR and Quake VR (porting old engines). Add to
  the list when a worker or the owner names a new one.
- **VR game checklist** (#189, `docs/vr/VR-CHECKLIST.md`): what players expect from a VR game; re-audit
  the plan against it at the end of each checkpoint.

## 5. Budget
- **Common weekly limit: up to 80%** (D-022; 20% stay for the owner's other project). At 80% no new workers start; running ones finish their PRs.
- **5-hour limit: up to 95%** (D-022). At 95% the coordinator **stops the non-Fable workers**
  (`claude stop <id>`, their work stays in the worktree and the transcript), waits for the reset
  (`rate_limits.five_hour.resets_at`), then **resumes them** (`claude respawn <id>`). The last 5% are the
  coordinator's, to keep watching. Fable workers go on.
- **The coordinator's own context** is spent with care: short command output (filters, `-Last N`), no
  whole large files, details read once and summarised; the pinned status issue is its external memory —
  update it before long waits, so an automatic compaction or a new coordinator loses nothing.
- **Fable's weekly limit: may be used up.** Claude Code does not report it; a Fable worker that stops
  making progress with a usage-limit message has hit it → restart its task on Opus.
- Reading the limits: `~/.claude/statusline/last.json` → `rate_limits.five_hour` / `seven_day`
  (`used_percentage`), written by the status line of any running session; or a screenshot of
  claude.ai → Settings → Usage from the owner.
- The coordinator's own session spends the common limit too: stay quiet between events.

## 6. Models and effort (D-015)
Choose per task, by size and difficulty — not by area alone. Spend Fable only where it pays off.

| Model | Tasks | Default effort |
|---|---|---|
| **Opus** | the default for engine work: camera, input, UI, combat, core, render plumbing, ordinary bugs | high |
| **Sonnet** | scripts, CI, docs, research summaries, small mechanical changes | medium |
| **Fable** | only **really large and hard** tasks: OpenXR frame loop and stereo rendering (#20, #19, #72), the IK body (#36), rune recognition (#43–#45), crashes nobody could explain | xhigh |

- **Effort** follows the task, not the model: routine change → lower it (`-Effort medium` on Opus,
  `low` on Sonnet); hard design or debugging → raise it (`-Effort xhigh` on Opus). `max` only by exception.
- `start-worker.ps1` defaults: `area:build` / `area:docs` → Sonnet, everything else → Opus; effort by model;
  `-Model` and `-Effort` override. Fable is never a default.
- Context: a worker gets one task with a precise Definition of done and the links it needs; keep the
  coordinator's messages to workers short; split a task that would fill a worker's context.

## 7. Rules the coordinator enforces on workers
- **Launches (owner, 2026-10-09; enforced by the scripts after #147):** never start SteamVR (it turns on
  the headset) — with SteamVR off every launch is flat (`--no-vr`); one game at a time on the PC; only the
  worktree's own build, rebuilt from its HEAD; at most 10 game launches per task, repeated-run loops only
  after asking the coordinator.
- Each worker in its own worktree, its own game folders (`-Isolated`), PRs named `ARXVR-<n> <type>: …`.
- Merge on green CI except `needs-vr-test` (D-005).

## 8. Reporting to the owner
Short, in Russian: what was merged and cleaned up; a table of the running workers (task, model, state);
what is needed from the owner (a headset batch with one question per PR, or a decision); the weekly
usage. A finished block or a headset batch is a stop point — say it clearly.

## 9. Traps (see also [TRAPS.md](TRAPS.md))
- `claude --bg` refuses an untrusted folder; trust is per folder → workers launch in the owner's copy
  and enter their worktree (`worktree=<path>`).
- `claude --bg --permission-mode bypassPermissions` needs the disclaimer accepted once by the owner.
- Without `crossSessionInbound: accept`, messages between sessions in different permission modes wait
  for the owner's approval inside the receiving session.
- A coordinator running in a worktree-isolated background job: its guard rejects compound shell commands
  that touch git outside that worktree — use plain commands, PowerShell scripts, and PR bodies from a
  file (`gh pr create -F`).
- The owner's copy is pulled but not rebuilt by the coordinator; the desktop shortcut rebuilds a stale
  build itself (#141).
