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
| Research: reference VR mods (Dishonored VR, VHVR) — feature catalog, headless testing | [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) |
| SteamVR app manifest: name and tile on the loading card | [STEAMVR.md](STEAMVR.md) |
| Testing without a headset: smoke test, command channel, `status.json`, `xr_hello` — what is reliable and what is not | [TESTING.md](TESTING.md) |
| Traps: misleading symptoms that already cost time — **read before debugging** | [TRAPS.md](TRAPS.md) |

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

Work-in-progress limits: see [Parallel work](#parallel-work) (sequential mode: one task at a time).

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
The session names itself after the task: a project hook (`.claude/settings.json` →
`scripts/dev/hooks/session-title.ps1`) turns `/next-task 124` into the session title
"ARXVR-124 <issue title>" (also for `/accept-task <n>` and `/reject-task <n>`), shown in `/resume`
and the terminal tab.

### Roles

- **Coordinator session** — one long-running session that plans and does not write feature code.
  It hands out the next task (in the order of the [task queue](MVP.md#task-queue)) as a ready-to-paste
  prompt, and after the owner reports a task done it checks that the PR is merged and `main` is green,
  tests whatever can be tested without a headset, updates the plan, and hands out the next task.
  The owner triggers this with **"новый цикл"** (or `/new-cycle`): the coordinator runs
  `scripts/dev/cycle-report.ps1`, merges what is ready, fixes board inconsistencies, deletes leftover
  branches, syncs the owner's working copy, and then gives either the next task's prompt or the reason
  progress is blocked (a PR waiting for a VR test, red CI, a blocking bug, a decision needed).
- **Worker session** — a fresh session per task, possibly with a different model. It implements exactly
  one task in its own PR (`/next-task <n>`), and handles the owner's verdict (`/accept-task`, `/reject-task`).
  In parallel mode the coordinator starts workers itself as background sessions
  (`scripts/dev/start-worker.ps1 <n>`, decision D-011): each in its own worktree, without permission
  prompts. The owner sees them in `claude agents` and opens one with `claude attach <id>`.
- **Owner** — tests VR features in the headset following the PR's *How to test*, and gives the verdict.

By default tasks are sequential: the next task starts only after the previous one is merged. Once
the tooling of #123 is merged, the coordinator can switch on **parallel work** (next section) — several
workers at once, with headset tests still one at a time.

### Parallel work

The headset and the owner are the only shared bottleneck; code can be written in parallel as long as
workers do not step on each other's files, game folders or the headset.

**What runs in parallel and what does not** — VR tasks are not excluded: everything about them runs in
parallel except the moments the real headset is used.

| Stage of a task | In parallel? |
|---|---|
| Writing code, building, host unit tests | Yes — each worker in its own working copy |
| Checks without a headset: flat launch, the simulated VR backend (#112), scripted sequences (#113), the two-eyes check | Yes |
| A worker's self-test in real VR (SteamVR running, nobody wearing the headset) | **One at a time** — VR launch lock, a few minutes each |
| The owner's test in the headset | **One at a time**, in batches |

The owner's headset is the bottleneck by design; the rules shrink it: the simulator answers "does it
work", so the headset only answers "how does it feel"; one question per launch; several PRs per
sitting; and the plain lane plus the next VR task keep going while PRs wait in Testing.
"Isolated" below means **separate game folders** (log, config, saves) per working copy — not "without
the headset".

**Two lanes**
- **VR lane** — tasks whose PR will need the owner's headset test (`needs-vr-test`). At most **2 in
  progress** and at most **4 waiting in Testing**; when Testing is full, VR work pauses until the owner
  tests a batch.
- **Plain lane** — everything else: build, CI, tooling, tests, refactoring, engine plumbing, docs,
  research. Merged on green CI, no owner involvement.
- Overall at most **3 workers at once** (start with 2 and raise it when merges stay clean).

**Which tasks may run together** — the coordinator decides in `/new-cycle` and hands out one prompt per
worker. Two tasks may run at the same time only if (1) both have their dependencies merged, and (2)
they are in **different areas**:

| Area | Typical files |
|---|---|
| Build, CI, scripts | `CMakeLists.txt`, `cmake/`, `vcpkg.json`, `scripts/`, `.github/` |
| VR runtime and frame loop | `src/vr/`, `src/graphics/opengl/` |
| Scene rendering | `src/scene/`, `src/graphics/` (except opengl), `src/animation/` |
| Camera and player | `src/game/Camera.*`, `src/game/Player.*`, camera code in `src/core/ArxGame.cpp` |
| Input | `src/input/`, controls in `src/gui/Interface.cpp` |
| UI | `src/gui/` (except controls) |
| Combat and magic | `src/core/Core.cpp` combat, `src/game/Equipment.*`, `src/game/Spells.*`, `src/game/magic/` |
| Docs and research | `docs/`, `.claude/` |

When in doubt, run them one after the other.

**Each worker in its own working copy** — in parallel mode a worker never works in the owner's copy.
It works in its own git worktree under `C:\Users\arzib\arx-fatalis-vr-wt\ARXVR-<n>-<type>-<name>`
(outside OneDrive), created from `origin/main` with `scripts/dev/new-worktree.ps1` (#123). Build
directories are per copy already. The owner's copy on the desktop stays on `main` and serves as the
test station.

**Isolation of game runs**
- Self-test launches use their own user and config folders (`run-windows.ps1 -Isolated` →
  `--user-dir` / `--config-dir` per working copy, #123), so workers never share `arx.log`, `cfg.ini`
  or saves with each other or with the owner.
- **One game in VR at a time:** VR launches take a machine-wide lock (#123). A worker that cannot get
  it in time skips its VR self-test and says so (best effort). Flat launches need no lock.
- **No workarounds for a second-instance limit** (owner's rule): the engine has no single-instance
  check, so isolated flat and simulated runs should work side by side — #123 proves it. If they do
  not, the lock covers **every** game launch and game runs are always one at a time; only code,
  builds and host tests run in parallel.
- CI has one runner on the owner's PC: parallel PRs queue for it (about a minute each). A second
  runner can be added if the queue gets long.

**Merging parallel PRs** — first green, first merged. Before merging, a PR must be up to date with
`main` and green on that state (`gh pr update-branch <pr>`, wait for CI). Conflicts are resolved by
the PR's worker. After each merge the coordinator updates the other open PRs in `/new-cycle`.

**Headset tests in batches** — the owner tests when it suits them, several PRs in one sitting:
1. "новый цикл" → the coordinator lists the PRs in **Testing**, each with its **one question** and the
   command to run it: `.\scripts\dev\test-pr.ps1 <pr>` (from the owner's copy; builds and launches
   that PR's working copy, #123).
2. The owner answers per PR, e.g. "#131 ok, #133 judder when turning".
3. The coordinator merges the accepted ones (`/accept-task <pr>` works from any session), posts the
   feedback on the rejected ones and gives a `/reject-task <pr> <feedback>` prompt for a worker.

### Worker sessions prepare everything themselves

The owner only opens a session and types `/next-task <n>`. Everything else at the start of a task is
the worker's job, without asking: fetch and pull `main`, leave a branch of an already merged task,
build `main` once (which also sets up vcpkg and dependencies) to make sure the base is healthy, and
install or add anything missing for the task (tools via `winget`, libraries via `vcpkg.json`).
The worker asks the owner only for things that need the owner's account, payment, or hardware —
and never discards uncommitted changes it finds; it reports them instead.

### Workers test the game themselves first

Before a task goes to **Testing** (or is merged without a VR test), the worker runs the game itself
with the smoke test ([TESTING.md](TESTING.md)):

```powershell
.\scripts\dev\smoke-test.ps1                 # flat, --loadlevel 10: start, play, quit, check the evidence
.\scripts\dev\smoke-test.ps1 -Vr             # the same in VR, with SteamVR running (session state recorded)
.\scripts\dev\smoke-test.ps1 -- --loadslot 0 # a save instead of a level; any arx.exe arguments after --
```

It starts the build with the test channel (`--test-dir`), waits until `status.json` says the game is
playable, quits through the channel and fails on: exit code ≠ 0, no `Clean shutdown` in a fresh
`arx.log`, a new crash report in `%USERPROFILE%\Saved Games\Arx Libertatis\crashes\`, an `[E]` log
line, `errors ≠ 0` in `status.json`. The flat run is a **gate**: a failure caused by the task is fixed
(or filed as a bug, if unrelated) before the PR. Checks that need game state use the channel
(`-Commands 'shot before','teleport -p marker_0001','shot after'`, `scripts/dev/game-cmd.ps1`), so a
worker asserts on `status.json` numbers and screenshots instead of reading the log by eye.

The VR run is **best effort**: with nobody wearing the headset the OpenXR session is created but
stays short of `FOCUSED` (measured in #99, numbers in TESTING.md), so it proves the VR start-up and
shutdown paths, not the rendering. `xr_hello.exe` (built next to `arx.exe`) answers "is the runtime
or the game at fault?" without the game. When a self-test cannot run (SteamVR off, no headset), the
worker says so in the PR's *How verified* instead of blocking the task on it, and the owner's test decides.
The owner's headset time is for judging how it feels in VR, not for finding crashes.

**Verification tiers** — always climb in this order and stop at the lowest tier that answers the question:
1. **Numbers**: host unit tests of pure logic (#114), `status.json` and log assertions (#99).
2. **Flat or simulated visuals**: flat launch, the simulated VR backend (#112) with scripted
   sequences and per-eye screenshots (#113), the two-eyes check.
3. **The owner in the headset** — last, for how it *feels*. Asking the owner to put the headset on for
   a question the simulator could answer wastes a session. Prove once that each automated check can
   fail (break the thing on purpose) before trusting it.

**One question per headset launch.** A `needs-vr-test` PR asks the owner exactly **one** clearly
defined question in *How to test*, with the expected result and what each possible outcome means
("smooth → done; judder on head turns → pacing, see the rate line"). The verdict and the build id go
on the issue.

**Logging rules for VR code** (`arx.log` is the instrument every test and every tester sends back):
log state changes, not frames — nothing unbounded per frame (log once, every N ms, or the first N);
every guard that refuses something logs why, with the values; name the owner of a decision before the
result; an expected zero says so explicitly.

**Traps.** Before debugging something that makes no sense, read [TRAPS.md](TRAPS.md). When a task
hits a new trap (a misleading symptom, stale state, an instrument that lied), add an entry in the same PR.

### Testing a task in the headset

The worker session works directly in the owner's working copy. When a task goes to **Testing**,
its branch stays checked out and freshly built there, so the owner tests exactly the PR's code:

```powershell
# SteamVR running, headset on
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
```

Or double-click the **Arx Fatalis VR** desktop shortcut (created by `scripts/create-desktop-shortcut.ps1`):
it runs the latest build of whatever branch is checked out in the owner's working copy and shows the
branch name in its console window.

`--loadslot <n>` starts straight into a saved game (recorded in flat mode) — needed until the in-VR
menu exists (checkpoint B). `run-windows.ps1` starts the game in a window (`--windowed`; `-Fullscreen`
opts out), and a plain launch goes straight to the main menu — logos, the intro and every `.cin`
cinematic are skipped during development (`--intro` shows the logos and the intro, `--cinematics`
every cinematic; #92 restores them for players).
After `/accept-task` the worker merges and switches the copy back to `main`.

Helper scripts used by the commands:
- `scripts/dev/next-task.ps1` — which task is next (Testing / In Progress first, then the queue)
- `scripts/dev/set-status.ps1 -Issue <n> -Status <column>` — move a task on the board
- `scripts/build-windows.ps1`, `scripts/run-windows.ps1` — build and run (`-Isolated`: this copy's
  own user directory; a launch without `--no-vr` holds the VR lock, `scripts/vr-lock.ps1`)
- `scripts/dev/smoke-test.ps1` — the worker's self-test: start, play, quit, check the evidence
- `scripts/dev/game-cmd.ps1`, `scripts/dev/game-status.ps1` — talk to a running game through the
  test channel (`--test-dir`), see [TESTING.md](TESTING.md)
- `scripts/dev/new-worktree.ps1 -Issue <n> -Type <type> -Name <name>` — a worker's own working copy
  (a git worktree from `origin/main`, outside OneDrive); `-Remove` once the task is merged
- `scripts/dev/test-pr.ps1 <pr>` — the owner's test station: the PR's worktree, built if needed, its
  *How to test*, the build id, launch
- `scripts/dev/cycle-report.ps1` — the coordinator's report: PRs, board, leftovers, worktrees, next task

## Task lifecycle

1. **Before starting**, move the task to *In Progress* and add a concrete *definition of done* to it — how we will verify it is finished: **ordered pass criteria** (numeric and flat/simulated checks first, the headset check last, with the observation that would disprove it) and **non-goals** (what this task deliberately does not do). A task with no pass criteria cannot be finished, only abandoned.
2. **Branch**: `ARXVR-<issue>-<type>-<short-name>` (e.g. `ARXVR-12-fix-rc-flags`), from `main`.
3. **Commits** follow the [naming convention](#commit-and-pull-request-naming): `ARXVR-12 fix: ...`, `Refs #12` in the body.
4. **Pull request** into `main`: title in the same format, PR template body with `Closes #12`, *What*, *How verified*, *How to test*.
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
- **CI runs on a self-hosted runner on the owner's PC** (Windows service, label `arxvr`, installed in
  `C:\actions-runner`). vcpkg and the build directory persist there, so a typical PR builds in minutes.
  If the PC is off, CI waits in the queue — that is expected, not a failure.
- **One vcpkg for the whole machine:** `C:\vcpkg-shared` holds the vcpkg checkout, source downloads and
  binary packages, used by CI (workflow `env`) and by the owner's builds (user environment variables
  `VCPKG_ROOT`, `VCPKG_DOWNLOADS`, `VCPKG_DEFAULT_BINARY_CACHE`). A dependency built once — locally or
  in CI — comes from the cache everywhere after that.
- **Docs-only PRs do not run CI** (`docs/**`, `*.md`, `.claude/**`, issue/PR templates — see `paths-ignore`
  in the workflow). With no check on the PR, Claude merges them right away; a PR that touches anything
  else always gets a build.
- For `needs-vr-test` PRs, *How to test* is a step-by-step script for the headset: which save to load,
  what to do, what should happen, what to report if it does not.
- When unsure whether something needs VR testing, label it `needs-vr-test` — asking costs less than
  merging a broken feature.
- Merge with a merge commit whose subject is the PR title plus the PR number:
  `gh pr merge <pr> --merge --delete-branch --subject "<PR title> (#<pr>)"`.
- Claude never pushes directly to `main` or `master`, never force-pushes, never bypasses CI.

## Commit and pull request naming

```
ARXVR-<issue> <type>: <description>
```

Examples:

```
ARXVR-18 feat: render into per-eye OpenXR swapchains
ARXVR-12 fix: keep MSVC compiler-only flags away from rc.exe
ARXVR-99 test: add a flat-mode smoke test script
ARXVR-1 docs: record the commit naming convention
```

- **`ARXVR-<issue>`** — the GitHub issue the change belongs to (the task, or the bug). If no task fits
  (small process or docs changes), use the epic it belongs to, e.g. `ARXVR-1` for build and process.
- **`<type>`** — one of:

  | Type | For |
  |---|---|
  | `feat` | new behavior the player or developer can use |
  | `fix` | a bug fix |
  | `perf` | performance |
  | `refactor` | restructuring without behavior change |
  | `build` | build system, dependencies, scripts that build or run |
  | `ci` | GitHub Actions |
  | `docs` | documentation, plan, rules |
  | `test` | tests and test tooling |
  | `chore` | anything else (assets, housekeeping) |

- **`<description>`** — imperative, lowercase start, no trailing period; the whole first line ≤ 72 characters.
- **Commit body** — what and why, wrapped at 72 characters; ends with `Refs #<issue>` so GitHub links the
  commit to the issue (GitHub does not turn `ARXVR-18` into a link in this repository), followed by
  the `Co-Authored-By` line when Claude wrote it.
- **Pull request title** — same format; several commits in one PR still get one title for the task.
  The PR body starts with `Closes #<issue>` (the template does this).
- **Merge commit** — `<PR title> (#<pr>)`, see above.
- **Branch** — named like the PR, in a form git accepts (no spaces or colons):
  `ARXVR-<issue>-<type>-<short-name>`, e.g. `ARXVR-18-feat-per-eye-swapchains`. Lowercase after the key,
  words joined with `-`.
- Commits that come from the upstream engine (Arx Libertatis) keep their original messages.

## Research

- **All research is a task on the board**, done by a worker session in its own PR — never an ad-hoc
  investigation in a conversation. The coordinator only takes a quick look to scope the task
  (what exists, the license, where the source is) and writes the issue.
- **Research on reference projects extends one catalog**, [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md):
  each new project is a "Research continued (#107)" task that adds its summary, license line and rows
  with continuing IDs (F-111, …); duplicates are noted on existing rows, not repeated.
- Research tasks create **no** follow-up tasks: the owner decides from the catalog (*Decision* column),
  the coordinator turns the decisions into tasks and orders the queue.

## Development diagnostics — automatic

Every game run is evidence, with nothing for the owner to do (plan decided on 2026-10-09: #129–#132):
- **D1 (#129)** the log is always on disk (flushed on warnings / errors and every second) and the last
  10 runs are kept, so a hang or a forced close does not lose the end of the log.
- **D2 (#130)** a game event log: one line per game and VR state change (level, save/load, death,
  session state, controller profile, menus, spells, hits, refused actions with their reason).
- **D3 (#131)** after **every** game exit started by our tools (desktop shortcut, `run-windows.ps1`,
  `test-pr.ps1`, the smoke test) a digest runs automatically; a **new** error signature or a crash is
  filed as an issue labeled `bug` + `auto-reported`, a known one gets a throttled "seen again" comment.
- **D4 (#132)** crash reports verified under SteamVR, debug symbols kept for every tester build.

**Triage:** the coordinator goes through new `auto-reported` issues in every `/new-cycle`: confirm or
close as noise (and add the line to the allow-list), link to the right epic as a sub-issue, set the
milestone, and put it into the MVP queue if it blocks the MVP. Workers never ignore an
`auto-reported` issue that their change caused — they fix it in the same PR.

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
- Picking features from research catalogs (the *Decision* column); the coordinator turns the picks into tasks

**Any decision can be revisited** — by the owner at any time. A changed decision is recorded as a new
entry in [DECISIONS.md](DECISIONS.md) that supersedes the old one (never deleted), and affected tasks
are updated.
