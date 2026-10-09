# Arx Fatalis VR — Decision Log

Significant technical and project decisions, newest first. Each entry records what was decided, why,
and what it rules out, so the reasoning survives after the conversation that produced it.

**Any decision can be revisited.** A decision is never edited away or deleted: a new entry replaces
it, and the old one gets `Status: Superseded by D-xxx` with a one-line reason. The same holds for the
*Decision* column of research catalogs (e.g. [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md)):
a changed decision keeps its history in that cell.

---

## D-014: Launch control for parallel workers — no SteamVR start, one game at a time, own fresh build, ledger, budget

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner (relayed during #93, enforced by #147)

### Decision
Workers launch the game often and in parallel, on the owner's desktop. The rules, enforced by the
launch scripts (`scripts/launch-lib.ps1`, used by `run-windows.ps1`, `smoke-test.ps1`, `test-pr.ps1`
and the desktop shortcut), not by the workers' discipline:

1. **No SteamVR start without the owner.** A launch without `--no-vr` would let the OpenXR loader
   start SteamVR, which turns on the owner's headset and base stations. The scripts make such a
   launch flat (`--no-vr` added, said in the output) when `vrserver.exe` is not already running;
   `smoke-test.ps1 -Vr` is then *skipped* (exit 2). Only the owner's switch `-StartSteamVR`
   (`run-windows.ps1`, `test-pr.ps1`) starts SteamVR. Workers never start or close SteamVR by hand
   either (no `vrstartup.exe`, no `Stop-Process vrmonitor`).
2. **One game at a time on this PC.** Every script launch, flat or VR, holds a machine-wide game lock
   slot (`%LOCALAPPDATA%\ArxFatalisVR\game-<n>.lock`; a VR launch also `vr.lock`) while the game runs,
   waits in line with a timeout and names the holder (working copy, branch, pid). A raw `arx.exe`
   outside the lock counts as a holder. `settings.json` (`maxGames`) may allow 2 flat games later.
3. **Only the working copy's own fresh build.** A launch refuses an `arx.exe` from another copy's
   build directory, or whose `built-from.txt` does not name the copy's `HEAD` (`dirty` is allowed),
   and says to build first (`-Rebuild` does it).
4. **Launch ledger.** Every launch is one line in `%LOCALAPPDATA%\ArxFatalisVR\launches.log` (time,
   copy, branch, build, flat or VR, pid, exit code, duration); `cycle-report.ps1` shows launches per
   working copy, so the coordinator sees a worker that launches 20 times.
5. **Launch budget:** at most **10 game launches per task**. Repeated-run evidence loops (`-Repeat`)
   only after asking the coordinator, sequential and flat. Launches are announced in the narration.
6. **Self-test windows do not take the focus** from the owner's work (SDL shows them without
   activation).

Replaces the #123 rule that isolated flat runs may run side by side: isolation of *files* stays
(own user, config and channel directories per copy), but game *runs* are one at a time.

### Why
On 2026-10-08/09 unannounced loops of launches confused the owner, and a worker of #93 started
SteamVR for 15 VR runs, waking the headset; two games at once also fight for the GPU and the desktop.
The owner wants the desktop quiet while workers run, and the headset untouched unless they put it on.

### Consequences
- `scripts/vr-lock.ps1` became `scripts/launch-lib.ps1` (the VR lock is part of the game lock).
- The VR self-test exists only while the owner has SteamVR running; otherwise the PR says so and
  the simulated VR backend (#112) is the long-term answer for VR evidence without the headset.
- TESTING.md, WORKFLOW.md "Parallel work" and `.claude/commands/next-task.md` carry the rules.

---

## D-013: The coordinator runs development on its own, in blocks, within a usage budget

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- **The owner talks only to the coordinator.** The coordinator picks the tasks of the current block
  (a wave of [MVP.md](MVP.md#waves)), writes or sharpens their issues, starts the workers
  (`start-worker.ps1`), follows them (idle notices, `claude logs`, cross-session messages), triages
  what they find (new bugs and follow-ups → issues, into the queue if they block the MVP), merges and
  cleans up, and keeps the slots full — without the owner opening worker sessions.
- **The coordinator does not investigate or fix things itself**: a problem becomes a task for a worker.
- **It stops and reports** only when (1) the block is done, (2) a PR needs the owner's headset test —
  work that needs no headset goes on meanwhile — or (3) the budget is reached. The owner gives VR
  verdicts to the coordinator ("#18 ok" / what is wrong); the coordinator merges or passes the feedback
  to the worker.
- **Budget:** the common weekly limit up to **70%** — from there no new workers start, running ones finish
  their PRs; the 5-hour limit may be used up (sessions wait for its reset and continue,
  `autoContinueAtUsageLimit`); Fable's separate weekly limit may be used up.
- **Models** (replaces D-012's table): **Fable** for every engine task (any area except build and docs)
  while its limit lasts, **Sonnet** for build, CI, scripts, docs and research, **Opus** when Fable's limit
  runs out (`start-worker.ps1 -Model opus`).

### Why
The owner wants continuous progress without babysitting sessions or prompts, and a hard ceiling on the
week's usage; Fable's separate limit is spent first to spare the common one.

---

## D-012: Worker model by task difficulty — Fable, Opus, Sonnet

- **Date:** 2026-10-09
- **Status:** Superseded by D-013 — the owner gave Fable every engine task, not only VR runtime and rendering
- **Decided by:** project owner

### Decision
`scripts/dev/start-worker.ps1` picks the worker's model from the issue's area label; the coordinator
overrides it with `-Model` when a task is harder or easier than its area suggests.

| Model | Areas | Typical tasks |
|---|---|---|
| **Fable** | `area:vr-runtime`, `area:render` | swapchains, frame loop, stereo rendering, IK body; hard crashes (override) |
| **Opus** | everything else (`core`, `camera`, `input`, `ui`, `combat`, `magic`) | engine work: camera, input, UI, combat |
| **Sonnet** | only `area:build` / `area:docs` | scripts, CI, docs, research |

Usage is watched in `/new-cycle`: the plan's limits (the session, the week, and Fable's separate weekly
limit) are read from the status line's last input (`~/.claude/statusline/last.json`, if Claude Code
passes them) or from the owner's screenshot of claude.ai → Settings → Usage. When the weekly use runs
ahead of the week, the coordinator runs fewer workers at once (2 instead of 3) or moves tasks to the
model with room left. The one-time free full reset is kept for a week that runs out mid-wave.

### Why
Fable has its own weekly limit, so giving it the hardest tasks spends a separate budget and keeps the
common one for the rest; three workers in parallel spend about three times one.

---

## D-011: The coordinator starts worker sessions itself; workers run without permission prompts

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- In parallel mode the **coordinator starts the worker sessions itself** instead of handing the owner
  prompts to paste: `scripts/dev/start-worker.ps1 <n>` creates the task's worktree and runs
  `claude --bg` there with `/next-task <n>`, named "ARXVR-<n> <issue title>".
- Worker sessions run with **`--permission-mode bypassPermissions`** (the owner's choice over "auto mode
  plus an allow-list" and over normal prompts), so they never stop to wait for a permission click.
- The owner follows them with `claude agents`, opens one with `claude attach <id>` (questions,
  `/accept-task`, `/reject-task`); the coordinator can message them and stop them.
- Prerequisite: the owner accepts the bypass-mode disclaimer once (`claude --dangerously-skip-permissions`,
  interactively, choosing "Yes, I accept" — the default is "No, exit"); nobody can do that for them.
- Workers are launched in the owner's copy (a trusted folder) and enter their worktree as their first
  step (`/next-task <n> worktree=<path>`): Claude Code's workspace trust is per folder and does not
  extend to new worktrees, and asking the owner to trust every worktree would defeat the point.

### Why
The owner wants the loop to run without copying prompts and without babysitting permission dialogs;
the coordinator already knows which tasks may run together.

### Trade-offs accepted
- Nothing stops a worker's mistaken destructive command on the owner's PC. Mitigations that stay:
  each worker has its own worktree outside OneDrive and its own game folders (#123), the project rules
  (never push to `main`/`master`, no force-push, never discard uncommitted work) and GitHub's history.
- Each worker spends tokens on its own; three in parallel cost about three times one.

---

## D-010: No rays from the hands; runes drawn in the air with the hand; grab by reach

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner (while deciding on [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md))

### Decision
- **No rays or lasers from the hands in the game world.**
- **Runes are drawn in the air with the hand:** the glowing trail is where the hand moves. No laser
  cursor; the projection onto a plane that the 2D recognizer needs is internal and invisible (#43–#45).
- **Spell and projectile direction comes from the hand**, without a visible ray (F-75, F-76, #44).
- **World interaction by reach:** items, doors, levers, chests and bodies are used by reaching and
  grabbing with the hand (F-57, #37, #38); the engine still decides what is under the hand.
- **Menus and the inventory:** a short pointer appears **only while a menu or the inventory is open**
  (VHVR-like, F-81, #34) — chosen over touch UI for precision on the original 2D interface.

### Why
Immersion: in a hand-driven VR game a beam from the hand reads as a tool, not as the character's hand.
The old 2D menus are small, so a pointer stays — but only where there is a menu.

---

## D-009: Full body in VR — the player model with arms driven by IK, in the MVP

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
Arx Fatalis already has a full player model with legs, torso, arms and animations. In VR the player
sees **their whole body**, not floating hands:
- legs and torso keep the engine's animations (walk, crouch, jump); the head is hidden; the camera sits
  at the eyes (D-006);
- the **arms are driven by the controllers via IK** (F-52, Dishonored VR `docs/dishonored/ARM_IK.md`,
  MIT): shoulders relative to the head frame, body yaw following the head with a dead zone, the
  shoulder sliding when a hand is out of reach;
- the weapon is held by the IK hand (#75); while IK drives the arms the engine's arm attack animation
  is suppressed (F-67);
- health and mana are **two bars on the left wrist** (F-83, #116).

**In the MVP, before combat** (checkpoint B). Replaces the earlier plan of placeholder hands and
hidden first-person arms (#36, #75). Hand-back to engine animations (death, ladders — F-53) is after
the MVP.

### Why
The owner wants the game to feel like VHVR, which shows the full body; Arx has the model and
animations already. Doing it before combat means the weapon is held by the model's hand from the
start instead of being redone later. Cost: roughly 1–2 weeks more for the MVP.

---

## D-008: Stick forward = where you look

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner (catalog row F-33)

### Decision
Thumbstick locomotion moves relative to the **head (look) direction**. Controller-relative movement is
an option after the MVP. The body yaw is still decoupled from the head (#25) — for the full-body model
and for where the hands and the UI anchor live — but it does not steer movement.

### Why
Both reference mods default to it (Dishonored VR `HeadBasedMovement`, VHVR
`JoyStickForwardDirection=LookDirection`, which the owner plays); Dishonored VR's body-relative mode
kept drifting.

---

## D-007: The product is named in `VERSION`; user directory and icon name stay as in Arx Libertatis

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** Claude, per the owner's request in #89

### Decision
- `VERSION` reads `Arx Fatalis VR 0.1-dev`. Everything the player can see that is derived from it —
  window title, main menu version line, OpenXR `applicationName`, Windows file properties, error
  dialogs — says **Arx Fatalis VR**. The engine name stays **Arx Libertatis** in the OpenXR
  `engineName` and in the credits (`AUTHORS`).
- The **user directory** stays `%USERPROFILE%\Saved Games\Arx Libertatis` (`USER_DIR` in
  `CMakeLists.txt`, independent of `VERSION`): saves and `cfg.ini` recorded with earlier builds keep
  working, and a save made in VR can be opened in flat Arx Libertatis.
- The **icon name** stays `arx-libertatis` (used for the X11 window class, the OpenAL device
  override and the Linux `.desktop` file): renaming it means renaming the icon files for a
  non-Windows audience the MVP does not have. Revisit for the Steam release (`M3`).
- The **Windows icon** is the owner's prebuilt `data/vr/ARXVR.ico`, compiled into `arx.exe` (and the
  crash reporter / profiler) as a resource; the window takes it from the exe (#103). The upstream
  icon generated from SVG stays off (`-DICON_TYPE=none`), so the build needs no ImageMagick or Inkscape.
- SteamVR learns the name and the 460×215 tile from an **app manifest** registered by
  `scripts\register-steamvr.ps1` ([STEAMVR.md](STEAMVR.md)); the game has no OpenVR dependency (D-001).

### Why
The product should call itself by its own name everywhere (D-002), without breaking anyone's saves
or adding a second VR API just for a tile image.

### Consequences
- `arx_version_number` restarts at 0.x: it is only stored in saves for information, never compared.
- The crash reporter and tools (not built for the MVP) would also carry the new name.

---

## D-006: Eye height follows the character for the MVP; real crouch = game crouch

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- For the MVP the VR camera sits at the **character's in-game camera height** (the stable first-person
  camera position, without head bob). The HMD provides the head **orientation**; the player's real
  height does not change the eye height.
- **Physical crouching** is detected from the HMD dropping below a threshold relative to the standing
  height captured at recenter, and makes the character crouch in the game (#100).
- **Height calibration** (matching the eye height to the player's real height, seated play) comes
  after the MVP (#101).

### Why
The level geometry, doors and collision are built around the character's eye height; tying the camera
to it keeps the world consistent and avoids clipping through floors and ceilings, while physical
crouching keeps the most natural body movement.

### Consequences
- #27 covers eye height = character height and the meters ↔ Arx units conversion for head offsets
  and eye separation.
- Positional head tracking beyond that (leaning, room-scale) stays post-MVP (#26).

---

## D-005: CI gates merges; the owner only tests VR features

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
The owner does not review every pull request. Every PR is built by CI. Claude merges PRs on its own
when CI is green, **except** gameplay / VR behavior that can only be judged in the headset: those PRs
are labeled `needs-vr-test`, wait in the **Testing** column, and are merged only after the owner
tested them in VR (`/accept-task`).

### Why
The owner's time is best spent where only a human in a headset can judge the result. Build, tooling,
documentation and engine plumbing are verified by CI.

### Consequences
- CI (task #15) is part of the MVP: without it Claude cannot merge.
- The board has a **Testing** column; the PR template has *How verified* and *How to test* sections.
- The development loop (`/next-task`, `/accept-task`, `/reject-task`) is described in
  [WORKFLOW.md](WORKFLOW.md#the-development-loop).
- The MVP tasks are ordered in a [task queue](MVP.md#task-queue) so that every session picks the same next task.

---

## D-004: Zero-setup launch, like the Valheim VR mod

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
The player starts SteamVR, launches Arx Fatalis VR, and the game sets itself up:

- **VR is detected automatically.** If an OpenXR runtime with a headset is available, the game starts
  in VR. There is no `--vr` flag; `--no-vr` forces flat mode.
- **Game data is found automatically** in the player's Steam libraries (Steam path from the registry →
  `libraryfolders.vdf` → `appmanifest_1700.acf` → install folder). No paths to configure.
- **Launchable from SteamVR.** Until it is a Steam app with the VR flag, it is added to Steam as a
  non-Steam shortcut; whether such shortcuts appear in the SteamVR dashboard is to be verified (task #80).

### Notes for implementation
- Creating an OpenXR instance with SteamVR as the active runtime may start SteamVR by itself.
  If that is unwanted when the player intends to play flat, detect whether SteamVR is already running
  before initializing OpenXR, and fall back to flat mode otherwise.
- Today the engine finds data only next to its executable, in the working directory, via `--data-dir`,
  or in the `Software\ArxLibertatis` registry key (`src/io/fs/SystemPaths.cpp`).

### Consequences
- Task #17 covers automatic VR start; new MVP tasks #79 (Steam data detection) and #80 (launch from SteamVR).

---

## D-003: MVP first, polish later

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
The first milestone is an **MVP** ([MVP.md](MVP.md)): a short demo of the core VR experience —
stereo, locomotion, physical melee, rune casting, and the original 2D UI on a floating panel —
reached as fast as possible. Polish and the full game (`M2 Full Game & Polish`) come only after
the MVP, and only if it is well received.

### Why
Validate interest early with the smallest convincing demo instead of spending months on polish
for a project nobody may want.

### Consequences
- The board is organized around the MVP: MVP tasks are in **Todo** with milestone `M1 MVP`;
  everything else is in **Backlog**.
- Known rough edges are accepted in the MVP (flat floating UI, placeholder hands, no bow or shield,
  cutscene and screen-effect issues) — listed in [MVP.md](MVP.md).
- Milestones were renamed: `M1 Prototype` → `M1 MVP`, `M2 Playable` → `M2 Full Game & Polish`.

---

## D-002: Project name is "Arx Fatalis VR"

- **Date:** 2026-10-08
- **Status:** Accepted (internal use); public use pending permission
- **Decided by:** project owner

### Decision
The project is called **Arx Fatalis VR** — in the repository, on the board, and in documentation.

### Caveat
"Arx Fatalis" is a trademark of the game's rights holder (Arkane / ZeniMax / Microsoft).
Using it for a public Steam release requires their permission. This is tracked in epic #9 (task #66);
if permission is not granted, a non-infringing fallback name will be chosen for the public release.

---

## D-001: Use OpenXR (not OpenVR) as the VR API

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner

### Decision
The VR layer is built on **OpenXR**, with SteamVR as the OpenXR runtime. We do not use the OpenVR API.

### Why
- Valve has stopped adding features to OpenVR; all new SteamVR functionality ships through OpenXR.
  OpenVR keeps working but will not evolve, and this project will run for years.
- SteamVR passes the OpenXR 1.0 conformance tests on Windows for OpenGL, which is the engine's only renderer.
- OpenXR has a standard Valve Index controller profile (`/interaction_profiles/valve/index_controller`),
  and players can still rebind controls in the SteamVR UI.
- Other headsets that Steam players use (Quest via Link / Steam Link, Pico, WMR, future devices) work
  without dedicated support — relevant for a free Steam release.
- The OpenXR loader is Apache-2.0, compatible with the engine's GPLv3+.

### Trade-offs accepted
- More bootstrap code than OpenVR (instance, session, swapchains, reference spaces, action sets).
- Fewer reference implementations: most classic-game VR mods (Half-Life: VR Mod, Quake VR) use OpenVR.
- Index finger tracking goes through `XR_EXT_hand_tracking`; SteamVR support for it must be verified
  (not needed for the first playable version).

### Consequences
- Epic #2 tasks are expressed in OpenXR terms (`xrCreateSession` with `XR_KHR_opengl_enable`,
  swapchains, `xrWaitFrame` / `xrEndFrame`).
- Input uses OpenXR actions and suggested bindings instead of a SteamVR action manifest.
