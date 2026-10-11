# Project rules — <project name>

Layer 2 of the coordinator (see `CORE.md`). Filled through the init quiz (procedures/INIT.md §3),
revised whenever the owner changes how the project works. Every row: the question, **this project's
answer**, and a default — per the strictness preset (CORE §3) where they differ. Only a few rows are
**asked** (INIT 3.1); the rest are **found** in the project and confirmed in one table (3.2) or filled
**by default** (3.3), and all of them are shown in one summary for the owner's yes. A row marked *(if
any)* stays `n/a` when the project has no such thing. Work starts only when no `<fill>` is left and the
owner said yes to the summary (CORE §1.12).

## Project
| Question | This project | Default to offer |
|---|---|---|
| **Strictness** (CORE §3) | `<fill>` | swarm · standard · strict — asked first; sets which tasks run, parallel workers, who merges, review, new tasks, production writes, autopilot, reports |
| Overrides of the preset (row and value) | `<fill>` | none |
| Session names | `<fill>` | `#<task> <role>: <title>` (role = worker / verifier; tester and scout run as subagents), `researcher: <topic>`, coordinator `Coordinator`; no project prefix |
| Worker permission mode | `<fill>` | **`auto`** in every preset · `acceptEdits` (cautious) where the owner wants prompts |
| Named production actions (strict) | `<fill>` | none |
| Core (CORE §2) | `<fill>` | always a local copy in `coordinator/core/` at claude-coordinator commit `<sha>`; local edits: `<list or none>` |
| Where the coordinator's files live (`PROJECT.md`, `BINDINGS.md`, `LOG.md`) | `<fill>` | **`coordinator/` as its own local git repository** (state repository, CORE §12b), excluded from the project's git, committed every cycle |
| State repository remote (always private) | `<fill>` | solo: the owner's account · team: the team's organization · local only (lost with the disk) — pushed after every cycle; pointer in `CLAUDE.md`: yes / no (never in a client's repository) |
| Who reviews CI, deployment, migration and release-flag changes (CORE §1.1) | `<fill>` | per the strictness: swarm the verifier · standard the owner · strict a human |
| Project code to other services — web search, third-party tools (CORE §1.13) | `<fill>` | strict: ask · swarm, standard: allowed |
| Standing approvals (CORE §1, Definitions) | `<fill>` | none |
| Product, users; solo or team | `<fill>` | — |
| Current milestone / sprint and what "done" means | `<fill>` | a demo scenario / the sprint's issues |
| Who owns the scope | `<fill>` | solo: the owner · a team or a client: the business analyst / product owner (INIT 3.1, question 3) |
| Owner's language / artifacts' language | `<fill>` | owner's language / English |

## Sources of truth (procedures/SOURCES.md)
| Kind | Main system | The coordinator may |
|---|---|---|
| Code and review | `<fill>` | read, write through PRs |
| Tasks and statuses | `<fill>` | read, change status |
| Requirements | `<fill>` | read only |
| Decisions and their reasons | `<fill>` | write through the change path |
| Acceptance | `<fill>` | read only |
| Design | `<fill>` | read only |
| The coordinator's memory | the state repository | write |

Other sources: the catalog `coordinator/SOURCES.md`. Missing tools (no tracker, code host or git): `<none or the mode>`.

## Tracking
| Question | This project | Default to offer |
|---|---|---|
| Tracker, board, statuses | `<fill>` | GitHub issues + project board: Backlog / Todo / In Progress / Testing / Done · no tracker: `coordinator/TASKS.md` |
| Hierarchy and labels | `<fill>` | epics → tasks / bugs; `epic` `task` `bug`, `area:*`, `needs-design`, `needs-owner-test` |
| What becomes an issue | `<fill>` | every piece of work |
| Unplanned but needed work | `<fill>` | per the strictness (CORE §3): swarm filed and queued · standard filed, starts on the scope owner's yes · strict proposed only |
| The owner's wishes | `<fill>` | an issue at once, in the owner's words |
| Issue fields | `<fill>` | scope, out of scope, **do not decide**, depends on, acceptance criteria, verification, estimate |
| Status page / progress line | `<fill>` | a pinned issue; "milestone ≈ N% (closed/all)" after each merge |

## The cycle (CORE §4)
What "start a new cycle" runs here — an ordered recipe, agreed at init, revised any time.

| Question | This project | Default to offer |
|---|---|---|
| Recipe (ordered steps) | `<fill>` | sync PRs → clean up merged work → collect and triage new comments → plan next work and merge order → approval gate → delegate → report |
| Parallel tasks per cycle | `<fill>` | per the strictness: swarm as many as runnable (e.g. 10) · standard 2–3 · strict one |
| Approval gate | `<fill>` | swarm: auto for queued and obvious items, ask for the rest · standard, strict: announce the plan (item → issue/PR → what and why → model) and wait for the owner's approval |
| Obvious review comments | `<fill>` | swarm, standard: fixed automatically · strict: listed in the plan like any other item |
| Stale comments | `<fill>` | flagged with the reason (task changed / comment obsolete); never acted on without the owner |
| Periodic tick | `<fill>` | swarm: every ~30 min · standard, strict: only on the owner's "start a new cycle" |

## Coding standards (CORE §4b)
| Question | This project | Default to offer |
|---|---|---|
| Language / framework / library rules | `<fill>` | established best practice for the stack as short rules (e.g. React: components, hooks, server vs client; TypeScript strict) |
| Architecture rules | `<fill>` | folder structure, data access layer, error handling, naming |
| Where they live | `<fill>` | `docs/standards.md`, linked here; reviewers check against it |

## Research (CORE §4b)
| Question | This project | Default to offer |
|---|---|---|
| Start research | `<fill>` | propose in one line with the model it would use; start only on the owner's yes |
| Where results go | `<fill>` | a catalog document in the project's docs (`docs/research/<topic>.md`) or a table in the coordinator session |
| Radar: references and competitors | `<fill>` | proposed from the init research (similar products, open-source projects, mods); one per block, a light-model researcher; rows decided by the owner in one batch at the end of the block · strict: proposed per block |
| Expectations checklist | `<fill>` | `docs/expectations.md`; re-audited at each milestone checkpoint |

## The change flow
Walked end to end at init (CORE §3, topic d); revise as the project changes.

| Step | This project | Default to offer |
|---|---|---|
| Branch name | `<fill>` | `<issue>-<type>-<slug>` (e.g. `37-feat-hand-reticle`); no project prefix, the repository names the project |
| Commit message | `<fill>` | `type: subject (#<issue>)` (never start with `#`: git drops such lines as comments); body with what and why; types feat / fix / docs / refactor / test / chore |
| How often to commit / push | `<fill>` | a commit + push per finished block |
| PR | `<fill>` | **draft** at the first push; title as the commit (`feat: items with a reticle per hand (#37)`); `Closes #<issue>`; Summary / Test plan (tick only what was run) |
| AI review | `<fill>` | independent reviewer agent per CORE §6; re-review until approval with no Blocker / Major |
| Who marks ready | `<fill>` | the coordinator after approval + green CI; never the author |
| Human review | `<fill>` | swarm: none · standard: the owner on risky changes · strict: developer and/or business analyst; their comments go back to the same branch + re-review |
| Who merges | `<fill>` | per the strictness (CORE §3): swarm the coordinator on green CI + a confirmed verdict · standard the same, the owner for risky changes · strict humans |
| Progress comments | `<fill>` | a short PR comment after each meaningful push |
| Multi-issue PRs | `<fill>` | each issue linked and moved on the board |
| Deploy | `<fill>` | preview per PR; production from the main branch |
| Docs-only changes | `<fill>` | PR without CI; merged first if they change rules |

## Testing and CI
| Question | This project | Default to offer |
|---|---|---|
| Definition-of-done checks per task | `<fill>` | lint + typecheck + build + unit tests (+ DB tests with a migration) |
| End-to-end check of the task | `<fill>` | a headless-browser scenario that clicks through the changed pages on a database seeded with test data |
| CI layout | `<fill>` | a fast set on every PR, heavy checks by path filter, caching |
| Improvements | `<fill>` | the coordinator proposes checks that save time or owner attention (CORE §4b); the owner decides |

## Voice in artifacts
Always the owner's: first person, no mention of AI, no AI co-author lines (CORE §11).

| Question | This project | Default to offer |
|---|---|---|
| Comment style | `<fill>` | terse bullets |

## Acceptance
| Question | This project | Default to offer |
|---|---|---|
| Who accepts, how | `<fill>` | the owner / QA / business analyst on preview or the real target, one question per item |
| After a requested change | `<fill>` | re-accepted before merging |
| Machine during the test | `<fill>` | quiet: no builds, launches, CI pushes |

## Environments and production
| Question | This project | Default to offer |
|---|---|---|
| Environments | `<fill>` | local · preview (per PR) · production |
| Release path to production | `<fill>` | merge to main → automatic deploy; DB migrations via the CI workflow (swarm, standard: agents may also apply them when a task needs it, backup first) |
| Database changes | `<fill>` | migration files; never edit an applied migration · strict: no dashboard edits, no direct pushes from a dev machine |
| Production backup before an irreversible change *(if any)* | `<fill>` | the command and destination in `BINDINGS.md` (e.g. a database dump / platform snapshot), filled before the first such write |
| Release flags *(if any)* | `<fill>` | features that act on real users or money ship disabled behind an env flag; enabled after merge in a documented order |
| Production rights of tools / MCP | `<fill>` | per the strictness (swarm, standard: write when the task needs it, backup first · strict: read-only) |

## Parallel work and resources
| Question | This project | Default to offer |
|---|---|---|
| Parallel workers allowed | `<fill>` | yes, isolated |
| Per-worker isolation | `<fill>` | working copy + DB stack + ports derived from the task number (BINDINGS) |
| Shared resources to guard | `<fill>` | the main DB stack, a single device, the CI runner |
| Serial identifiers | `<fill>` | migration versions: check main + open PRs + branches, each branch owns its slot |
| Processes after a task pauses | `<fill>` | stop; restart when it continues (keep / cache if start-up is slow) |

## Risk tiers
| Question | This project | Default to offer |
|---|---|---|
| High-risk areas | `<fill>` | money, roles, permissions / RLS, audit, business time, customer data |
| Extra rules there | `<fill>` | stronger model, mandatory review, negative tests, owner told before merge |

## Budget, time and models
| Question | This project | Default to offer |
|---|---|---|
| Usage caps | `<fill>` | weekly 90%, short window 95%; over only with permission |
| Models available | `<fill>` | Fable (own limit), Opus, Sonnet, Haiku |
| Time estimates | `<fill>` | strict: optimistic / realistic / pessimistic per task; aim for realistic, warn before pessimistic · swarm, standard: none |
| Time tracking | `<fill>` | strict: a timesheet draft shown to the owner before it is written · swarm, standard: none |

## Client dependencies
| Question | This project | Default to offer |
|---|---|---|
| Access, accounts, approvals the client owes *(if any)* | `<fill>` | a list with owner and date (`docs/dependencies.md`) |

## MCP and integrations
| Technology found | MCP / connector | Rights | Connected? | CLI fallback |
|---|---|---|---|---|
| `<fill>` | `<fill>` | `<read / write>` | `<yes/no>` | `<fill>` |

## Lessons (CORE §10) — one line each, newest last
- `<date>`: `<lesson>`
