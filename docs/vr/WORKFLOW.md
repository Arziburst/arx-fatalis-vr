# Arx Fatalis VR — Working Rules

These are the agreed rules for developing the VR version. They apply to humans and to Claude alike.
Update this file whenever the agreements change.

## Language

All project artifacts are written in **English**: documentation, issues, epics, tasks, comments on issues,
commit messages, pull requests, and code comments.

## Where things live

| What | Where |
|---|---|
| Code | repository [Arziburst/arx-fatalis-vr](https://github.com/Arziburst/arx-fatalis-vr) |
| Plan and status | board [Arx Fatalis VR](https://github.com/users/Arziburst/projects/3) |
| Epics and tasks | GitHub Issues — the **single source of truth** for the plan |
| Rules | this file |
| Decisions and their reasoning | [DECISIONS.md](DECISIONS.md) |
| Current goal: the MVP | [MVP.md](MVP.md) |
| Research and scope of playable v1 | [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md) |
| Research: controllers (Valve Index, Quest 3) | [RESEARCH-controllers.md](RESEARCH-controllers.md) |

## Current goal: MVP first

The first goal is the **MVP** ([MVP.md](MVP.md)): a short, rough demo of the core VR experience,
shipped fast. Polish and the full game come after, if the MVP lands. When deciding whether to do
something now, ask: *is it needed to play the MVP demo scenario?* If not, it goes to Backlog.

## Plan structure

- **Milestone** — a major stage: `M1 MVP`, `M2 Full Game & Polish`, `M3 Steam Release`.
  Milestones are set on **tasks** only.
- **Epic** — an issue labeled `epic`: a large goal (e.g. "VR rendering"). An epic spans milestones —
  its MVP tasks are in `M1 MVP`, its polish tasks in `M2`. Its description holds the goal,
  prerequisites, definition of done, and dependencies.
- **Task** — an issue labeled `task`, created as a **sub-issue** of its epic, with a milestone.
  A task must be doable in one working session (hours to a couple of days). Anything bigger gets split.
- **Bug** — an issue labeled `bug`, also a sub-issue of the epic it belongs to.

Area labels: `vr` (VR-specific work), `legal` (rights, licensing, Steam).

## Board columns (Status field)

| Column | Meaning |
|---|---|
| **Backlog** | Planned, but not part of the current goal (the MVP) |
| **Todo** | Part of the current goal (the MVP); picked up in the order given in [MVP.md](MVP.md) |
| **In Progress** | Being worked on right now |
| **Testing** | PR is ready and CI is green; waiting for the owner to test it in VR (`needs-vr-test`) |
| **Done** | Finished and verified; issue closed |

No more than two tasks in **In Progress** at a time.

## The development loop

One task per session, one pull request per task. The owner drives it with three commands
(Claude Code project commands in `.claude/commands/`):

| Command | What happens |
|---|---|
| `/next-task` | Claude takes the next task from the [MVP task queue](MVP.md#task-queue), implements it on its own branch, opens a PR, waits for CI. Non-VR work is merged automatically; VR behavior goes to **Testing** with step-by-step test instructions. |
| `/accept-task` | The owner tested it in the headset and it is good → Claude merges the PR and moves the task to **Done**. |
| `/reject-task <what is wrong>` | The owner tested it and something is off → Claude fixes it in the same PR and puts it back to **Testing**. |

Start each task in a **new Claude Code session** in the owner's working copy, so every task starts
with a clean context; everything a session needs to know lives in the repository and on the board.

### Roles

- **Coordinator session** — one long-running session that plans and does not write feature code.
  It hands out the next task (in the order of the [task queue](MVP.md#task-queue)) as a ready-to-paste
  prompt, and after the owner reports a task done it checks that the PR is merged and `main` is green,
  tests whatever can be tested without a headset, updates the plan, and hands out the next task.
- **Worker session** — a fresh session per task, possibly with a different model. It implements exactly
  one task in its own PR (`/next-task <n>`), and handles the owner's verdict (`/accept-task`, `/reject-task`).
- **Owner** — tests VR features in the headset following the PR's *How to test*, and gives the verdict.

Tasks are strictly sequential: the next task starts only after the previous one is merged.

### Workers test the game themselves first

Before a task goes to **Testing** (or is merged without a VR test), the worker runs the game itself:
it launches the build in flat mode (`--no-vr`) and, when SteamVR is available, in VR; loads a level
(`--loadlevel 10`, the first level of a new game) or a save (`--loadslot <n>`); lets it run; reads
`%USERPROFILE%\Saved Games\Arx Libertatis\arx.log` and checks `...\crashes\` for new crash reports;
closes the game and checks for `Clean shutdown`. A crash, a hang or a new `[E]` log line blocks the
hand-off. Once #99 lands, `scripts/dev/smoke-test.ps1` does all of this in one command.
The owner's headset time is for judging how it feels in VR, not for finding crashes.

### Testing a task in the headset

The worker session works directly in the owner's working copy. When a task goes to **Testing**,
its branch stays checked out and freshly built there, so the owner tests exactly the PR's code:

```powershell
# SteamVR running, headset on
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
```

`--loadslot <n>` starts straight into a saved game (recorded in flat mode) — needed until the in-VR
menu exists (checkpoint B). `run-windows.ps1` starts the game in a window (`--windowed`; `-Fullscreen`
opts out), and a plain launch goes straight to the main menu — logos and the intro cinematic are
skipped during development (`--intro` shows them, #92 restores them for players).
After `/accept-task` the worker merges and switches the copy back to `main`.

Helper scripts used by the commands:
- `scripts/dev/next-task.ps1` — which task is next (Testing / In Progress first, then the queue)
- `scripts/dev/set-status.ps1 -Issue <n> -Status <column>` — move a task on the board
- `scripts/build-windows.ps1`, `scripts/run-windows.ps1` — build and run

## Task lifecycle

1. **Before starting**, move the task to *In Progress* and add a concrete *definition of done* to it — how we will verify it is finished.
2. **Branch**: `task/<number>-<short-name>` (e.g. `task/12-fix-rc-flags`), from `main`.
3. **Commits** reference the task: `#12` in the message.
4. **Pull request** into `main` uses the PR template: `Closes #12`, *What*, *How verified*, *How to test*.
5. **CI and merge** — see [Pull requests, CI and merging](#pull-requests-ci-and-merging).
6. **Done** = the definition of done is met and verified (build, launch, in-headset check — whatever applies). The task is closed and moves to *Done*.
7. If a task is blocked, comment on it with exactly what blocks it and move it back to *Todo* / *Backlog*.

## Pull requests, CI and merging

Every change goes through a pull request into `main`. **CI** (GitHub Actions, `Build (Windows)`)
builds the engine on every pull request. What happens next depends on whether the change needs a
human in a headset:

| PR type | Label | Who merges | Condition |
|---|---|---|---|
| Everything that does not need in-headset testing: build, CI, tooling, docs, refactoring, engine plumbing | — | **Claude, on its own** | CI is green |
| Gameplay / VR behavior that can only be judged in the headset | `needs-vr-test` | **Claude, after the owner's OK** (`/accept-task`) | CI is green **and** the owner tested it in VR |

- **Never merge with failing or pending CI.** Fix the PR instead.
- For `needs-vr-test` PRs, *How to test* is a step-by-step script for the headset: which save to load,
  what to do, what should happen, what to report if it does not.
- When unsure whether something needs VR testing, label it `needs-vr-test` — asking costs less than
  merging a broken feature.
- Merge with a merge commit (`gh pr merge --merge --delete-branch`).
- Claude never pushes directly to `main` or `master`, never force-pushes, never bypasses CI.

## Growing the plan as we go

The plan is alive. When something new comes up during work:

- **New work inside an epic** → a new task (sub-issue of the epic, label `task`, on the board in *Backlog* or *Todo*).
- **A bug is found** → an issue labeled `bug`, sub-issue of the relevant epic.
- **A task turns out too big** → split it into several sub-issues; close the original with links to the new ones.
- **A task is no longer relevant** → close it as *not planned* with a comment explaining why.
- **A new large goal appears** → a new epic, but only with the project owner's approval.

Claude creates and updates tasks on its own, and after each working session reports what it added, closed, or moved on the board.

## An epic is closed when

- all of its tasks are closed (Done or not planned), and
- the definition of done in the epic description is met.

## Decisions reserved for the project owner

- Creating new epics and changing milestones
- Publishing anything externally: making the repository public, contacting rights holders, Steam
- Changing these rules
