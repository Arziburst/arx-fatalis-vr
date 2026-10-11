# Project rules — Arx Fatalis VR

Layer 2 of the coordinator. The core (layer 1) is the **local copy** in [core/](core/); the tools
(layer 3) are in [BINDINGS.md](BINDINGS.md); the journal is [LOG.md](LOG.md). Agreed with the owner
2026-10-09…11; changed only with the owner's yes (CORE §1.16). Worker-side project rules:
[docs/vr/WORKFLOW.md](../docs/vr/WORKFLOW.md); queue and waves: [docs/vr/MVP.md](../docs/vr/MVP.md);
decisions: [docs/vr/DECISIONS.md](../docs/vr/DECISIONS.md).

## Project
| Question | Arx Fatalis VR |
|---|---|
| **Strictness** | **swarm** |
| Overrides of the preset | **Which tasks run:** only the MVP queue in MVP.md and board **Todo** (not the whole milestone). **New tasks / research / follow-ups:** filed at once, started only on the owner's yes. **Owner-check tasks** (`needs-vr-test`): merged only after the owner's headset verdict. **Branch / commit / PR naming:** `ARXVR-<n>-<type>-<name>` / `ARXVR-<n> <type>: <desc>` (CLAUDE.md, WORKFLOW.md) |
| Session names | core default: `#<n> worker: <title>`, `#<n> verifier: <title>`, `researcher: <topic>`, coordinator `Coordinator` |
| Worker permission mode | `auto` |
| Core | local copy in `coordinator/core/` at claude-coordinator commit **0f098437a50c6d72eb64e25390e900230d3a8ebf**; local edits: (1) `scripts/start-worker.ps1 -LaunchFrom` — start from the trusted owner's copy, first action EnterWorktree (Claude Code refuses `--bg` in an untrusted worktree, TRAPS.md); (2) `scripts/hooks/safety-guard.ps1` — owner-copy protection for Edit/Write via `COORD_PROTECTED_ROOT`; (3) `core/CLAUDE.md` not copied (a nested CLAUDE.md would tell every session in the folder it is the coordinator) |
| Where the coordinator's files live | `coordinator/` as its own local git repository, excluded from the project's git (MIGRATE-LAYOUT, 2026-10-11) |
| State repository remote | private `Arziburst/arx-fatalis-vr-coordinator-state`, pushed after every cycle; pointer in CLAUDE.md: yes |
| Who reviews CI, deployment, migration and release-flag changes | the verifier |
| Project code to other services | allowed (swarm) |
| Voice | the owner's, no AI co-author lines (CORE §11; `attribution` off) |
| Standing approvals | the approved plan (MVP queue + Todo) runs without asking |
| Product, milestone | VR port of Arx Fatalis (Arx Libertatis engine, OpenXR / SteamVR, Valve Index), a free Steam app that requires the original game; milestone **M1 MVP** — the demo scenario in MVP.md |
| Scope owner, languages | the owner (solo); conversation Russian, all artifacts English (D-013) |

## Sources of truth
| Kind | Main system | The coordinator may |
|---|---|---|
| Code and review | GitHub `Arziburst/arx-fatalis-vr` | read, write through PRs |
| Tasks and statuses | GitHub issues + board projects/3 | read, change status |
| Requirements | `docs/vr/MVP.md`, the issues' contracts | read only |
| Decisions and their reasons | `docs/vr/DECISIONS.md` | write through the change path |
| Acceptance | the owner's headset verdict | read only |
| Design | none (the game's own content) | — |
| The coordinator's memory | the state repository (`coordinator/`), status page #193 as its summary | write |

## Tracking
| Question | Arx Fatalis VR |
|---|---|
| Tracker, board | GitHub issues (sub-issues of epics), board "Arx Fatalis VR" (projects/3): Backlog / Todo / In Progress / Testing / Done |
| Labels | `epic` `task` `bug`, `area:*`, `lane:vr`, `needs-vr-test`, `needs-design`, `model:fable`; milestone `M1 MVP` |
| What becomes an issue | every piece of work; research is always a task; the owner's wishes → an issue at once, in his words |
| The contract | written by the coordinator into the issue body (a `## Contract` section, CONTRACT.md format) and passed in the brief; `check-contract` passes before delegation |
| Status page / progress line | pinned issue #193; "MVP ≈ N% (closed/all M1 tasks)" after each merge |

## The cycle
| Question | Arx Fatalis VR |
|---|---|
| Recipe | limits → merged / open PRs and CI → clean up merged work → worker reports, PR feedback, headset verdicts → plan from the approved plan with conflict foresight and merge order → delegate (contracts) → verifier → headset batch when ready → merge queue → journal + #193 → report |
| Parallel tasks | as many as runnable within the caps (often 10–16); VR lane ≤ 2 in progress, ≤ 4 in Testing |
| Approval gate | auto within the approved plan; everything else asked |
| Periodic tick | autopilot on, cron `:17` / `:47` (recreate after a restart) |
| Report | only for a headset batch, a block done (+ the owner's build), a limit or an owner decision |

## The change flow
| Step | Arx Fatalis VR |
|---|---|
| Branch / commit / PR | `ARXVR-<n>-<type>-<name>` / `ARXVR-<n> <type>: <desc>` + `Refs #n` / title as the commit, `Closes #n` (WORKFLOW.md) |
| Review | the verifier role on every product PR (CORE §6), as a subagent; the coordinator reads docs-only PRs itself |
| Who merges | the coordinator, on **fully green CI** + a confirmed verifier verdict — never over a red or pending check; `needs-vr-test` only after the owner's headset verdict |
| Merge queue | one at a time: red-main fix → rule changes → owner-accepted → dependencies → the rest; accepted PRs get the single CI runner first; no CI polling loops (`gh run watch --interval 120` at most) |
| Decisions | workers write `D-NEW`; `number-decisions.ps1 -Pr <pr>` right before the merge (#274) |
| Progress comments | a short PR comment after each meaningful push |
| Docs-only PRs | no CI; merged by the coordinator, rule changes first |

## Acceptance — the owner's headset test
- Contract criteria with `check: owner` are the headset questions. Batch PRs when ready, order and minutes per PR;
  `test-pr.ps1 <pr> -Saves <save> -StartSteamVR` (saves: `mvp-new-game`, `mvp-items`, `mvp-goblin-fight`).
  One question, steps, expected, possible answers — in Russian.
- **Quiet PC** during the batch: workers do no builds, launches or CI pushes; performance log on (#327).
- Read logs yourself where they answer the question; close the game after each verdict; `stop-steamvr.ps1` at the end.
- Accept → the coordinator merges in the queue. Reject → save `arx.log` and screenshots first; the worker
  gets the findings as a contract update (or a new worker, `escalate_to` on a model failure).
- **A change requested in a headset test is re-tested in the headset before merging.**
- Combat and magic tests run in the playground once it exists (#321).
- After each closed block: pull the owner's copy and build Release (desktop icon).

## Workers — project rules (in every brief, details in WORKFLOW.md)
- Launch rules (D-014): never start or stop SteamVR; one game at a time; only the worktree's own fresh
  build; ≤ 10 launches per task; flat / `--vr-sim` while the owner is not testing.
- Verify with host tests (`arxtest`), `--vr-sim` sequences, `eye-check.ps1`, `smoke-test.ps1`; these are
  the `oracle` checks of a contract.
- Build with `scripts\build-windows.ps1`; builds live in `%LOCALAPPDATA%\ArxFatalisVR\build\`.

## Budget and models
| Question | Arx Fatalis VR |
|---|---|
| Caps | weekly 90%, 5-hour 95%; over only with the owner's permission for that occasion; Fable has its own weekly limit |
| Models | Sonnet medium: docs, tests, tooling, research, verifier · Opus medium: moderate engine work · Opus high: render, input core, head math · Fable xhigh: stereo, full-body IK, rune recognition, unexplained crashes (`model:fable`); `escalate_to` one level up |

## Research
| Question | Arx Fatalis VR |
|---|---|
| Radar | reference VR mods / ports (OpenMW VR, Skyrim VR mods, Valheim VR, Gothic VR, Blade & Sorcery, Doom 3 BFG VR, Quake VR), one per block, researcher role (Sonnet); pre-approved |
| Where results go | `docs/vr/RESEARCH-reference-mods.md` (rows F-…, licence column); the owner decides rows per block |
| Expectations checklist | `docs/vr/VR-CHECKLIST.md`, re-audited at checkpoints |
| Other research | on the owner's yes |

## Lessons (newest last)
- 2026-10-10: never merge over a red or pending check, even a known flake.
- 2026-10-10: serialise merges — parallel merges thrash on shared doc tables.
- 2026-10-10: workers never poll CI in loops — 16 sessions tripped GitHub's secondary rate limit.
- 2026-10-10: a change requested in a headset test is re-tested before merging.
- 2026-10-10: test with `-Saves` from the library — the owner's own slots may be empty.
- 2026-10-10: decision-number clashes → `D-NEW` numbered at merge time.
- 2026-10-10: quiet PC during headset tests (FPS dips while CI and workers ran).
- 2026-10-10: write memory files with the Write / Edit tools, not shell commands.
- 2026-10-11: the approved plan runs itself, new work needs the owner's yes.
- 2026-10-11: the project moved out of OneDrive to `E:\dev\arx-fatalis-vr` so worktrees and the coordinator's own repository can live in the project folder.
