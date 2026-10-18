# Arx Fatalis VR — Working Rules

These are the agreed rules for developing the VR version. They apply to humans and to Claude alike.
Update this file whenever the agreements change.

## Working through the coordinator core

Since 2026-10-11 the work runs through the coordinator core (`coordinator/` in the owner's copy:
the core in `coordinator/core/`, the Arx rules in `coordinator/PROJECT.md`). **This section wins over
any older text below** that mentions `/next-task`, `/accept-task`, `/reject-task`, `/review-pr`,
`start-reviewer.ps1`, worktrees in `arx-fatalis-vr-wt` or workers merging their own PRs:

- **The coordinator writes the task's contract** into the issue (a `## Contract` section, the core's
  CONTRACT.md format: intent, ordered criteria each with its check — `oracle` / `review` / `owner` —,
  non-goals, do-not-decide items, budget, `touches`). Workers no longer write their own Definition of done.
- **A worker** is started by `scripts\dev\start-worker.ps1 <n>` as the session `#<n> worker: <title>`
  with the worker role and the contract in its brief, in its own worktree
  `E:\dev\arx-fatalis-vr\.claude\worktrees\ARXVR-<n>-<type>-<name>`. It follows this file for naming,
  launches (D-014), building and testing; proves each `oracle` criterion itself; opens the PR; and sends
  the coordinator the core's YAML report (each result `oracle` / `claim` / `owner`). **It never merges.**
- **The verifier** (the core's role, as a subagent) checks the PR against the contract; findings go back
  to the worker.
- **The coordinator merges** on fully green CI and a confirmed verdict; `needs-vr-test` PRs only after the
  owner's headset verdict. The owner's verdict reaches the worker through the coordinator.
- Artifacts are in the owner's voice: no AI co-author lines (`attribution` off).

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
| Developer guide: prerequisites, build, run, tests, CI, worktrees | [DEVELOPMENT.md](DEVELOPMENT.md) |
| Research and scope of playable v1 | [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md) |
| Research: controllers (Valve Index, Quest 3) | [RESEARCH-controllers.md](RESEARCH-controllers.md) |
| Research: reference VR mods (Dishonored VR, VHVR, Titanfall 2 VR, OpenMW VR, Skyrim VR mods) — feature catalog, headless testing | [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) |
| VR game checklist: what players expect from a VR game, audited against our plan — gaps for the owner's decision | [VR-CHECKLIST.md](VR-CHECKLIST.md) |
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

One task per session, one pull request per task. The owner does not start tasks by command: the coordinator
takes the next task from the [MVP task queue](MVP.md#task-queue), writes its contract and starts a worker
with `scripts/dev/start-worker.ps1`; the worker implements it on its own branch, opens a PR and waits for CI.
VR behavior goes to **Testing** with step-by-step test instructions; the owner gives the headset verdict
(OK, or what is wrong) to the coordinator, who merges on OK or sends the worker back to fix it in the same PR.

Start each task in a **new Claude Code session** in the owner's working copy, so every task starts
with a clean context; everything a session needs to know lives in the repository and on the board.
Workers are named by start-worker (`#<n> worker: <title>`). A project hook (`.claude/settings.json` →
`scripts/dev/hooks/session-title.ps1`) names the coordinator's session "ARXVR Coordinator" (unique per project) when it starts
(`/coordinator`, `/new-cycle` or "новый цикл"); that name is the workers' report address.

### Roles

- **Coordinator session** — one long-running session that plans and does not write feature code or
  investigate. It writes the tasks, starts, briefs and follows the workers, triages, merges, cleans up
  and runs the work on its own while the usage limits last (D-013, D-022, D-027); the owner talks only
  to it, VR verdicts included, and triggers a cycle with **"новый цикл"** (or `/new-cycle`). Its
  complete rule set: [coordinator/](../../coordinator/) — the local core plus PROJECT.md and BINDINGS.md
  (`/coordinator` on any machine).
- **Worker session** — a fresh session per task, possibly with a different model. It implements exactly
  one task in its own PR, and fixes what the coordinator relays from the owner's verdict.
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
| Checks without a headset: flat launch, the simulated VR backend (#112), scripted sequences (#113), the two-eyes check | Code and files yes; **game runs one at a time** on the PC (the game lock, D-014) — a launch waits for the running game |
| A worker's self-test in real VR (only while the owner has SteamVR running; nobody wearing the headset) | **One at a time** — the game lock plus the VR lock, a few minutes each; skipped when SteamVR is off |
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
- No fixed worker cap (D-027): every runnable task runs until the usage caps stop it; the game lock and the launch budget handle contention.

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
It works in its own git worktree under `%USERPROFILE%\arx-fatalis-vr-wt\ARXVR-<n>-<type>-<name>`
(outside OneDrive), created from `origin/main` with `scripts/dev/new-worktree.ps1` (#123). Build
directories are per copy already. The owner's copy on the desktop stays on `main` and serves as the
test station.

**Isolation of game runs**
- Self-test launches use their own user and config folders (`run-windows.ps1 -Isolated` →
  `--user-dir` / `--config-dir` per working copy, #123), so workers never share `arx.log`, `cfg.ini`
  or saves with each other or with the owner.
- CI has one runner on the owner's PC: parallel PRs queue for it (about a minute each). A second
  runner can be added if the queue gets long.

**Launch rules (D-014, enforced by `scripts/launch-lib.ps1` in every launch script — #147)**
- **Never start SteamVR, never close it.** A launch without `--no-vr` may run in VR only when
  `vrserver.exe` is already running (the owner started SteamVR); otherwise the scripts make it flat
  (`--no-vr` added, said in the output) and `smoke-test.ps1 -Vr` is skipped (exit 2). Only the owner
  passes `-StartSteamVR` (`run-windows.ps1`, `test-pr.ps1`) and `-StopSteamVR` (`test-pr.ps1`,
  `scripts/dev/stop-steamvr.ps1`). Workers do not run `vrstartup.exe` or stop `vrmonitor` / `vrserver` by hand.
- **One game at a time on this PC.** Every launch, flat or VR, holds a game lock slot
  (`%ProgramData%\ArxFatalisVR\locks\game-<n>.lock`, writable by every Windows account so the CI runner
  service shares it with the owner and the workers, #205; VR launches also `vr.lock`) while the game runs. A
  second launch waits (default 300 s) and prints the holder — working copy, branch, pid; the smoke
  test is skipped (exit 2) when the wait runs out, `run-windows.ps1` fails naming the holder. The OS
  releases a lock when its holder dies; a raw `arx.exe` outside the lock is treated as a holder.
  `%ProgramData%\ArxFatalisVR\locks\settings.json` with `{"maxGames": 2}` allows two flat games (owner's
  call); replaces the #123 rule of isolated flat runs side by side.
- **Only your own fresh build.** A launch runs only the working copy's own build directory, and only
  when its `built-from.txt` names the copy's `HEAD` (`<sha> dirty` is fine for uncommitted work).
  Otherwise it refuses and says to build first; `-Rebuild` on `run-windows.ps1` / `smoke-test.ps1` does it.
- **Launch ledger.** Every launch appends one line to `%LOCALAPPDATA%\ArxFatalisVR\launches.log`
  (`time= script= copy= branch= build= mode=flat|vr pid= exit= duration= args=`);
  `cycle-report.ps1` shows launches per working copy in the last hours and marks a copy over budget.
- **Launch budget: at most 10 game launches per task.** Plan them: a flat smoke test is one launch,
  `-Repeat 10` is ten. Repeated-run evidence loops only after asking the coordinator, sequential and
  flat. Say in the narration how many launches are coming before starting them. The scripts print a
  note past the budget; the coordinator sees the count in `/new-cycle`.
- **Self-test windows stay in the background:** every test run (`--test-dir`: the smoke test,
  `xrs-run.ps1`, CI, `run-windows.ps1 -TestChannel`) shows the game window without activating it,
  never lets it be activated, puts it in the bottom right corner and takes no real mouse or keyboard
  (#487) - a test never takes the focus or the input from the owner's work, and the owner's input never
  disturbs a test. Drive it through the channel (`desktop-key`, `desktop-mouse`, `vr-sim`);
  `status.json` → `window.activation` `never`, `realInput`; the smoke test fails a run whose window holds
  the foreground. `run-windows.ps1 -Background` keeps a launch without the channel unfocused too.

**Merging parallel PRs** — first green, first merged. Before merging, a PR must be up to date with
`main` and green on that state (`gh pr update-branch <pr>`, wait for CI). Conflicts are resolved by
the PR's worker. After each merge the coordinator updates the other open PRs in `/new-cycle`.

**Decision numbers are assigned at merge (#274).** Nobody picks a number while a PR is open: a new <!-- d-new-literal -->
decision is written `## D-NEW: <title>` and referenced as `D-NEW` (code comments, TESTING, TRAPS, the PR body); a second decision <!-- d-new-literal -->
in the same PR is `D-NEW-<tag>`. <!-- d-new-literal --> Right after `git merge origin/main` and right before the merge, run
`.\scripts\dev\number-decisions.ps1 -Pr <pr>`: the next free numbers replace the placeholders in every file the branch changed and
in the PR body (a line carrying the marker `d-new-literal` is left alone — for docs that talk about the placeholder); commit and push
(`ARXVR-<n> docs: number the decisions`) and let CI run again. A branch that hand-numbers a `D-0xx` main already has fails the
`Decision numbers` check (a step of the build workflow, no runner slot of its own, #296). Two PRs inserting at the very same line of DECISIONS.md still conflict in git: keep both entries.

**Headset tests in batches** — the owner tests several PRs in one sitting, run by the coordinator
(`test-pr.ps1 <pr> -Saves mvp-new-game -StartSteamVR`, one question per PR); the procedure, accept and
reject: [coordinator/PROJECT.md](../../coordinator/PROJECT.md#acceptance--the-owners-headset-test).

### The safety hook (#185, D-021)

Workers run without permission prompts (D-011), but Claude Code hooks still run. A `PreToolUse` hook in
`.claude/settings.json` (`scripts/dev/hooks/safety-guard.sh` → `safety-guard.ps1`) checks every Bash,
PowerShell, Edit, Write and NotebookEdit call of **every** session in this repository and denies exactly
these, with a reason that names the safe alternative:

| Rule (in the denial) | Denied | Instead |
|---|---|---|
| `push-main` | `git push` to `main` / `master` in any spelling (`HEAD:main`, `:main`, `--delete main`, a URL as the remote, `--all`, a push from a copy that is on `main`); `gh api` that changes the `main` ref | push the task branch, merge the PR with `gh pr merge` |
| `force-push` | `--force`, `--force-with-lease`, `-f`, `+refspec`, `--mirror` | `git fetch` + `git merge origin/main`, push normally; rewriting a remote branch only via the coordinator |
| `delete-outside` | a recursive delete (`rm -rf`, `Remove-Item -Recurse`, `rd /s`) of a path outside the session's own worktree and its job tmp dir (`$env:CLAUDE_JOB_DIR\tmp`) | delete inside the worktree; `build-windows.ps1 -Clean` for build output |
| `owner-write` | a write (redirection, `Set-Content`, `Copy-Item` to, `Remove-Item`, `git add/commit/pull/...`, the Edit / Write tools) into the owner's copy from a session whose directory is another worktree | work in your own worktree |
| `owner-discard` | `git reset --hard`, `git clean -f`, `git checkout -- .` / `-f`, `git restore .` in a main working copy: the owner's copy, also reached with `git -C` from a session in another clone | report uncommitted work to the coordinator; never discard it |

**It never stops a task.** Everything else passes: branch pushes (`git push -u origin <branch>`), `gh pr merge`
and REST merges, builds, game launches, writes and deletes inside the own worktree, reading the owner's copy,
`new-worktree.ps1 -Remove` and the coordinator's other scripts (the hook sees the command line, not what a
script does inside). On its own error it **fails open** (the call goes through; the error is logged).
Text inside quotes, here-strings and heredocs (commit messages, PR bodies) is not a command.

**Seeing why.** The denial message in the session starts with `Blocked [<rule>]:`. Every denial and every
fail-open error is one line in `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log` (time, rule, session, working
directory, command); the coordinator reads it in `/new-cycle`. A false positive is reported to the
coordinator, who fixes the rule and adds the case to the test — never worked around by splitting the command.

**Speed.** `safety-guard.sh` is a bash front door (~30 ms) that lets a call through unless it contains a word
a rule cares about (`push`, `reset`/`clean`/`checkout`/`restore`/`switch`, a delete command, `cd`, `-C`/`-c`,
`iex`, the folder name `arx-fatalis-vr`, `git/refs`); only those go to PowerShell (~0.4–0.5 s, mostly the
start of `pwsh`). The owner's copy must therefore keep the repository's folder name `arx-fatalis-vr`.

**Limits.** It is a guard rail against mistakes, not a sandbox: a path built in a variable it cannot resolve
(`Remove-Item -Recurse $dir`), a pipeline that feeds `Remove-Item`, or what a script does internally is let
through. Test: `.\scripts\dev\hooks\test-safety-guard.ps1` (159 sample commands, the hook process, fail-open,
the log, the timing); extend its table when a rule changes.

### Worker sessions prepare everything themselves

The coordinator starts the worker with the task number. Everything else at the start of a task is
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

The VR run is **best effort and only while the owner has SteamVR running** (D-014: a worker never
starts SteamVR; `smoke-test.ps1 -Vr` is skipped with exit 2 otherwise): with nobody wearing the
headset the OpenXR session is created but stays short of `FOCUSED` (measured in #99, numbers in
TESTING.md), so it proves the VR start-up and shutdown paths, not the rendering. `xr_hello.exe`
(built next to `arx.exe`) answers "is the runtime or the game at fault?" without the game — it
refuses to start SteamVR too. When a self-test cannot run (SteamVR off, no headset), the worker says
so in the PR's *How verified* instead of blocking the task on it, and the owner's test decides.
The owner's headset time is for judging how it feels in VR, not for finding crashes. Every launch
counts against the **budget of 10 per task** ([Launch rules](#parallel-work)).

**Verification tiers** — always climb in this order and stop at the lowest tier that answers the question:
1. **Numbers**: host unit tests of pure logic (`arxtest`, run by `build-windows.ps1` and CI), `status.json` and log assertions (#99).
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
result; an expected zero says so explicitly. A state change a player or tester causes (a feature
switching on, a gesture recognised, an attack refused and why) is a **game event**:
`gameevent::Event("<area>.<what>")("key", value)` (`src/core/GameEvent.h`), one greppable
`GameEvent:` line with a `t=` time and a count in `status.json` — see "The game event log" in
[TESTING.md](TESTING.md) (#130).

**Pure VR decision cores get host tests.** Logic that decides something from numbers — swing detector,
rune stroke projection, snap turn, crouch threshold — lives in a header that takes positions and
timestamps (no engine globals, no OpenXR, no clock reads) and is covered by a CppUnit test in `tests/`
(add the files to `arxtest_TESTCASES` in `tests/CMakeLists.txt`). `build-windows.ps1` builds `arxtest`
and runs it, so CI runs it on every PR: a red suite fails the build. A suite that does not compile is
not a passing suite — if a test cannot be built, the PR is not ready. Prove a new test can fail
(break the code on purpose once) before trusting it. `s32`/`u32` are `long` on MSVC, so cast expected
values: `CPPUNIT_ASSERT_EQUAL(u32(0), x)`.

**Traps.** Before debugging something that makes no sense, read [TRAPS.md](TRAPS.md). When a task
hits a new trap (a misleading symptom, stale state, an instrument that lied), add an entry in the same PR.

### Testing a task in the headset

The worker session works directly in the owner's working copy. When a task goes to **Testing**,
its branch stays checked out and freshly built there, so the owner tests exactly the PR's code:

```powershell
# SteamVR running, headset on
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
```

Or double-click the **Arx Fatalis VR** desktop shortcut (created by `scripts/create-desktop-shortcut.ps1`, runs `scripts/launch-shortcut.ps1`):
it rebuilds first when the build is stale (no `built-from.txt`, another commit than HEAD, uncommitted changes; the game never starts on a failed build), then runs whatever branch is checked out in the owner's working copy and shows the
branch name in its console window.

`--loadslot <n>` starts straight into a saved game (recorded in flat mode) — needed until the in-VR
menu exists (checkpoint B). `run-windows.ps1` starts the game in a window (`--windowed`; `-Fullscreen`
opts out), and a plain launch goes straight to the main menu — logos, the intro and every `.cin`
cinematic are skipped during development (`--intro` shows the logos and the intro, `--cinematics`
every cinematic; #92 restores them for players).
After the owner's OK the coordinator merges and the copy goes back to `main`.

Helper scripts used by the commands:
- `scripts/dev/next-task.ps1` — which task is next (Testing / In Progress first, then the queue)
- `scripts/dev/set-status.ps1 -Issue <n> -Status <column>` — move a task on the board; under the GraphQL limit the move
  is queued and sent later (`-Flush` sends it now). GitHub access of all dev scripts: `scripts/dev/github-lib.ps1` +
  `board.ps1` (#186) — REST for everything but the board, a 5-minute board cache, rate-limit errors with the reset time;
  `scripts/dev/test-github-lib.ps1` is the self-test
- `scripts/dev/pr-checks.ps1 <pr> [-Watch]` — CI of a PR over REST (replaces `gh pr checks --watch`)
- `scripts/build-windows.ps1`, `scripts/run-windows.ps1` — build and run (`-Isolated`: this copy's
  own user directory; `-Background`: no focus; `-StartSteamVR`: the owner's switch; `-Rebuild`)
- `scripts/launch-lib.ps1` — launch control shared by every launch script (D-014): SteamVR guard,
  game lock, fresh-build check, ledger, budget note, background window
- `scripts/dev/smoke-test.ps1` — the worker's self-test: start, play, quit, check the evidence
- `scripts/dev/log-digest.ps1` — the run digest and automatic bug filing (#131); runs by itself after
  every launch of `run-windows.ps1` / `smoke-test.ps1`; `test-log-digest.ps1` is its self-test
- `scripts/add-steam-shortcut.ps1` — the game as a non-Steam shortcut in the Steam / SteamVR library (#80,
  [STEAMVR.md](STEAMVR.md#launching-from-steamvr)); `scripts/dev/test-steam-shortcut.ps1` is its self-test
- `scripts/dev/game-cmd.ps1`, `scripts/dev/game-status.ps1` — talk to a running game through the
  test channel (`--test-dir`), see [TESTING.md](TESTING.md)
- `scripts/dev/new-worktree.ps1 -Issue <n> -Type <type> -Name <name>` — a worker's own working copy
  (a git worktree from `origin/main`, outside OneDrive); `-Remove` once the task is merged
- `scripts/dev/test-pr.ps1 <pr>` — the owner's test station: the PR's worktree, built if needed, its
  *How to test*, the build id, launch. Needs no GitHub API (#171): when it does not answer (rate limit, no
  network) the PR is resolved with `git ls-remote` + the local worktree (`-Branch` skips even that), and
  `-StartSteamVR` (the owner's switch) starts SteamVR explicitly — Steam URL, then `vrstartup.exe` — and waits for
  `vrserver` + `vrcompositor` before the game starts in VR; `-StopSteamVR` (the owner's switch) quits SteamVR after the
  game exits the way its own "Exit SteamVR" does (`vrmonitor://quit`, nothing killed) so the headset powers down
- `scripts/dev/stop-steamvr.ps1` — the same SteamVR quit on its own (the owner's switch; workers never run it)
- `scripts/package-tester.ps1` — the tester zip (#88): `arx.exe` + DLLs (MSVC runtime app-local) + `data/core` + launchers +
  `HOW-TO-USE.txt` + support bundle script (sources in `scripts/tester/`); checks the DLL closure and starts the **extracted**
  game with a Windows-only `PATH` (1 launch, game lock). CI runs it and uploads the artifact `tester-build`. Details: [TESTING.md](TESTING.md#the-tester-package-88)
- `scripts/dev/cycle-report.ps1` — the coordinator's report: PRs, board, leftovers, worktrees, next task
- `scripts/dev/plan-next.ps1` — the coordinator's task order by rule (#187): the tasks ready to start now (dependencies Done,
  earliest open wave, free area, VR lane and worker limits, no `needs-design`), each with its `start-worker.ps1` line, and the
  reason every other Todo task waits; rules in `scripts/dev/plan-next-lib.ps1`, self-test `test-plan-next.ps1` (run by CI via run-script-tests.ps1)

## Task lifecycle

1. **Before starting**, move the task to *In Progress* and add a concrete *definition of done* to it — how we will verify it is finished: **ordered pass criteria** (numeric and flat/simulated checks first, the headset check last, with the observation that would disprove it) and **non-goals** (what this task deliberately does not do). A task with no pass criteria cannot be finished, only abandoned.
2. **Branch**: `ARXVR-<issue>-<type>-<short-name>` (e.g. `ARXVR-12-fix-rc-flags`), from `main`.
3. **Commits** follow the [naming convention](#commit-and-pull-request-naming): `ARXVR-12 fix: ...`, `Refs #12` in the body.
4. **Pull request** into `main`: title in the same format, PR template body with `Closes #12`, *What*, *How verified*, *How to test*.
   After each meaningful push to an open PR (a fix after review or a headset verdict, a change of approach, a merge with
   conflicts), add a short PR comment: what was done or fixed and how it was verified. Trivial pushes (typos, a re-run) need none.
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
| Gameplay / VR behavior that can only be judged in the headset | `needs-vr-test` | **Claude, after the owner's OK** (the owner's verdict, given to the coordinator) | CI is green **and** the owner tested it in VR |

- **Never merge with failing or pending CI.** Fix the PR instead.
- **CI runs on a self-hosted runner on the owner's PC** (Windows service, label `arxvr`, installed in
  `C:\actions-runner`). vcpkg and the build directory persist there, so a typical PR builds in minutes.
  If the PC is off, CI waits in the queue — that is expected, not a failure.
- **CI is the automatic gate (D-021, #127):** the script self-tests (`scripts/dev/test-*.ps1`), the build
  with the host unit tests, then the flat game checks — smoke test with the draw check and the spell
  draw check — run on every PR, a few minutes in total. A red step blocks the merge; a game check that
  was skipped (game lock busy) is red too. A new `scripts/dev/test-*.ps1` must be added to
  `scripts/dev/run-script-tests.ps1`. The game runs in the runner service's session; evidence is the
  artifact `game-checks` — see [TESTING.md](TESTING.md#the-ci-gate-127). Since #522 it is uploaded **only when
  the job failed**: a green run keeps its evidence on the runner (the service's
  `%LOCALAPPDATA%\ArxFatalisVR\ci-evidence\<run id>-<attempt>`, the last 10 runs), and the game checks run on two
  lanes (two games at once, one while another game of the PC holds a lock slot).
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
  Read `scripts/dev/log-digest.ps1 -Print` instead of `arx.log` ([TESTING.md](TESTING.md#the-run-digest-131));
  GitHub calls of the filing are REST only (the GraphQL quota is shared by all sessions). The repository
  is public: every report is scrubbed of personal data first, and one that cannot be made clean stays
  queued (#387, [TESTING.md](TESTING.md#scrubbing-personal-data-387)).
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
- **Player-experience design** (D-017): controls, interaction, comfort, UI placement and look, feedback —
  agreed with the owner (questions, design played back, a yes) before the issue is written and before a
  worker starts; technical internals are decided without the owner

**Any decision can be revisited** — by the owner at any time. A changed decision is recorded as a new
entry in [DECISIONS.md](DECISIONS.md) that supersedes the old one (never deleted), and affected tasks
are updated.
