# Arx Fatalis VR — Working Rules

These are the agreed rules for developing the VR version. They apply to humans and to Claude alike.
Update this file whenever the agreements change.

## Language

All project artifacts are written in **English**: documentation, issues, epics, tasks, comments on issues,
commit messages, pull requests, and code comments.

## Where things live

| What | Where |
|---|---|
| Code | repository [Arziburst/arx-vr](https://github.com/Arziburst/arx-vr) |
| Plan and status | board [Arx Fatalis VR](https://github.com/users/Arziburst/projects/3) |
| Epics and tasks | GitHub Issues — the **single source of truth** for the plan |
| Rules | this file |
| Decisions and their reasoning | [DECISIONS.md](DECISIONS.md) |

## Plan structure

- **Milestone** — a major stage: `M1 Prototype`, `M2 Playable`, `M3 Steam Release`.
- **Epic** — an issue labeled `epic`: a large goal within a milestone. Its description holds the goal,
  prerequisites, definition of done, and dependencies.
- **Task** — an issue labeled `task`, created as a **sub-issue** of its epic, in the same milestone.
  A task must be doable in one working session (hours to a couple of days). Anything bigger gets split.
- **Bug** — an issue labeled `bug`, also a sub-issue of the epic it belongs to.

Area labels: `vr` (VR-specific work), `legal` (rights, licensing, Steam).

## Board columns (Status field)

| Column | Meaning |
|---|---|
| **Backlog** | Planned, but not soon |
| **Todo** | Next up; everything needed to start is available |
| **In Progress** | Being worked on right now |
| **Done** | Finished and verified; issue closed |

No more than two tasks in **In Progress** at a time.

## Task lifecycle

1. **Before starting**, move the task to *In Progress* and add a concrete *definition of done* to it — how we will verify it is finished.
2. **Branch**: `task/<number>-<short-name>` (e.g. `task/12-fix-rc-flags`), from the current working branch.
3. **Commits** reference the task: `#12` in the message.
4. **Pull request** contains `Closes #12` and describes what was done and how it was verified.
5. **Done** = the definition of done is met and verified (build, launch, in-headset check — whatever applies). The task is closed and moves to *Done*.
6. If a task is blocked, comment on it with exactly what blocks it and move it back to *Todo* / *Backlog*.

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
