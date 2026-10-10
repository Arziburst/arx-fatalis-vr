# Project rules — Arx Fatalis VR

Layer 2 of the coordinator. The core (layer 1) is the **local copy** in [core/](core/) (version in
`core/VERSION`); the tools (layer 3) are in [BINDINGS.md](BINDINGS.md). Agreed with the owner on
2026-10-09…11; changed only with the owner's yes (CORE §1.16). Worker-side rules: [WORKFLOW.md](../docs/vr/WORKFLOW.md);
queue and waves: [MVP.md](../docs/vr/MVP.md); decisions: [DECISIONS.md](../docs/vr/DECISIONS.md).

## Project
| Question | Arx Fatalis VR |
|---|---|
| Core: remote or local | **local copy** (`coordinator/core/`), editable for Arx; "update the core" on command |
| Commercial or not | **non-commercial** — own project, a free Steam app that requires the original game |
| Swarm or sequential | **swarm**: as many parallel workers as the limits and conflict foresight allow (often 10–16) |
| Autonomy level | **autonomous**; workers run in bypass mode on the owner's own machine via `start-worker.ps1` (the owner's deliberate choice), the project's safety hook as the net |
| Autopilot and tick | **on** — cron tick `:17` / `:47`; recreate after every restart |
| Product, milestone | VR port of Arx Fatalis (Arx Libertatis engine, OpenXR / SteamVR, Valve Index); milestone **M1 MVP** — the demo scenario in MVP.md |
| Scope owner, languages | the owner; conversation Russian, all artifacts English (D-013) |
| Session names | coordinator `ARXVR Coordinator`; workers `ARXVR-<n> <title>`; reviewers via `start-reviewer.ps1` |
| Coordinator's files | in the repository: `coordinator/` (this folder) |
| Journal | the status page, pinned issue **#193** (body = current state, updated every cycle); no separate LOG.md |
| Voice | no matter (own project); English |
| Standing approvals | the approved plan (below) runs without asking |

## The approved plan and the approval gate
- **The approved plan** = the MVP task queue in `MVP.md` and the tasks in **Todo** on the board. The
  coordinator starts those itself, in swarm, in dependency order — no question per task.
- **Anything not in the plan** — a new task, a research, a follow-up the coordinator thinks of, a scope
  change — is proposed in one line and starts only after the owner's yes (CORE §1.11, §4b). Findings
  are filed as issues at once (Backlog), the owner moves them into the plan.
- Player-experience questions (D-017) are asked **ahead** so no worker waits.

## The cycle (autopilot tick and "start a new cycle")
1. Limits (`usage-plan.ps1`), merged / open PRs and CI (`cycle-report.ps1`), running workers.
2. Clean up merged workers: `claude stop`, `claude rm` (separate command), `new-worktree.ps1 -Remove`.
3. Worker reports, PR feedback, the owner's headset verdicts → triage.
4. Plan from the approved plan (`plan-next.ps1`), conflict foresight (CORE §4a), merge order.
5. Delegate (gate: auto within the approved plan) → review → headset batch when ready → merge queue.
6. Update #193; report only for a headset batch, a block done (+ owner's build), a limit or an owner decision.

## Tracking
| Question | Arx Fatalis VR |
|---|---|
| Tracker | GitHub issues of `Arziburst/arx-fatalis-vr` (sub-issues of epics), board "Arx Fatalis VR" (projects/3): Backlog / Todo / In Progress / Testing / Done |
| Labels | `epic` `task` `bug`, `area:*`, `lane:vr`, `needs-vr-test`, `needs-design`, `model:fable`; milestone `M1 MVP` |
| Every piece of work | an issue; research is always a task; owner's wishes → an issue at once, in his words |
| Progress line | after each merge: "MVP ≈ N% (closed/all M1 tasks)" |

## The change flow
| Step | Arx Fatalis VR |
|---|---|
| Branch / commit / PR | `ARXVR-<n>-<type>-<name>` / `ARXVR-<n> <type>: <desc>` + `Refs #n` / title as the commit, `Closes #n`, What / How verified / How to test (WORKFLOW.md) |
| Progress comments | a short PR comment after each meaningful push |
| Decisions | workers write `D-NEW`; `number-decisions.ps1 -Pr <pr>` numbers them after merging main, right before the merge (#274) |
| Review | reviewer agent (`start-reviewer.ps1`, Sonnet medium) for engine core, render, input, the VR frame path, shared scripts; the coordinator reads docs / test / small tooling PRs itself |
| Who merges | the worker, on **fully green CI** + review pass — never over a red or pending check (only the owner can waive it); `needs-vr-test` PRs only after the owner's headset verdict |
| Merge queue | one at a time: red-main fix → global-rule changes → owner-accepted → dependencies → the rest; accepted PRs get the single CI runner first; workers never poll CI in loops (`gh run watch --interval 120` at most) |
| Global rules | process docs written by the coordinator itself, merged first, announced to every running session |
| Docs-only PRs | no CI; the coordinator merges them |

## Acceptance — the owner's headset test
- Only when PRs are ready; batch them with order and minutes per PR. `test-pr.ps1 <pr> -Saves <save> -StartSteamVR`
  (saves from the library: `mvp-new-game`, `mvp-items`, `mvp-goblin-fight`). One question, steps, expected, possible answers — in Russian.
- **Quiet PC** during the batch: workers do no builds, launches or CI pushes; the performance log is on (#327).
- Read logs yourself where they answer the question. After each verdict close the game; at the end `stop-steamvr.ps1`.
- Accept → `/accept-task <n>` (owner's words + build id). Reject → copy `arx.log` and screenshots aside first, `/reject-task <n>`.
- **A change requested in a headset test is re-tested in the headset before merging** — never merged on host or sim proof.
- Combat and magic tests run in the test playground once it exists (#321).
- After each closed block: pull the owner's copy and build Release so the desktop icon runs the latest game.

## Environments, resources, testing
| Question | Arx Fatalis VR |
|---|---|
| Environments | the owner's PC: SteamVR + Valve Index; no server production |
| Parallel work | own worktree per task; one game at a time (game lock); ≤ 10 launches per task; workers never start SteamVR (D-014) |
| Shared resources | the single self-hosted CI runner; the headset; the owner's copy (pulled, never edited) |
| Testing | host tests (`arxtest`), `--vr-sim` sequences (`.xrs`), `eye-check.ps1`, `smoke-test.ps1`; CI fast set on every PR, heavy checks by path |
| Risk tiers | render, stereo, input core, full-body IK → stronger model, reviewer |

## Budget and models
| Question | Arx Fatalis VR |
|---|---|
| Caps | weekly **90%**, 5-hour **95%**; over only with the owner's permission for that occasion; Fable has its own weekly limit (ask the owner) |
| Models | Sonnet medium: build, docs, tests, tooling, research, reviewers · Opus medium: moderate engine work · Opus high: render, input core, head math · Fable xhigh: stereo, full-body IK, rune recognition, unexplained crashes (`model:fable` label) |

## Research
Reference VR mods / ports (OpenMW VR, Skyrim VR mods, Valheim VR, Gothic VR, Blade & Sorcery, Doom 3 BFG VR, Quake VR), one per block, Sonnet researcher; findings in `docs/vr/RESEARCH-reference-mods.md` (rows F-…, licence column); the owner reviews one table per block; taken rows → tasks. VR checklist `docs/vr/VR-CHECKLIST.md` re-audited at checkpoints.

## MCP and integrations
| Technology | Via | Rights |
|---|---|---|
| GitHub | `gh` CLI — REST (`gh api repos/…`, `pr-checks.ps1`); GraphQL only for the board | write (issues, PRs, board) |

## Lessons (newest last)
- 2026-10-10: never tell a worker to merge over a red or pending check, even a known flake.
- 2026-10-10: serialise merges — parallel merges thrash on shared doc tables (TESTING.md, TRAPS.md).
- 2026-10-10: workers never poll CI in loops — 16 sessions tripped GitHub's secondary rate limit.
- 2026-10-10: a change requested in a headset test is re-tested before merging (#21, #32 were merged on host proof before the rule).
- 2026-10-10: the owner's own saves may be empty — always test with `-Saves` from the library.
- 2026-10-10: decision-number clashes (4×) → `D-NEW` numbered at merge time.
- 2026-10-10: FPS dips during headset tests while CI and workers ran → quiet PC during tests.
- 2026-10-10: write memory files with the Write / Edit tools, not PowerShell (protected path prompts).
- 2026-10-11: autopilot "find more work" vs strict mode → resolved: the approved plan runs itself, new work needs the owner's yes.
