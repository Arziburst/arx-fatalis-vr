# Arx Fatalis VR — Decision Log

Significant technical and project decisions, newest first. Each entry records what was decided, why,
and what it rules out, so the reasoning survives after the conversation that produced it.

**Any decision can be revisited.** A decision is never edited away or deleted: a new entry replaces
it, and the old one gets `Status: Superseded by D-xxx` with a one-line reason. The same holds for the
*Decision* column of research catalogs (e.g. [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md)):
a changed decision keeps its history in that cell.

---

## D-024: One frame clock in VR — `xrWaitFrame` paces the game; pose-matched submission; an orderly exit

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker of #20 (technical internals, D-017), from research rows F-07, F-21, F-26, F-116, F-117

### Decision
- **While an OpenXR session exists, the headset's `xrWaitFrame` is the only thing that paces the
  game.** The frame starts with it (`vr::beginFrame()` at the top of `ArxGame::doFrame`), so the
  simulation runs on the runtime's predicted display time; the window's vsync and the engine's own
  frame limiter (`fps_limit`) are switched off for the session and restored when it ends — `cfg.ini`
  is not modified, the menu's vsync choice is kept for flat mode.
- **Pose-matched submission.** The eyes are located once per frame, right after `xrWaitFrame`, for
  that frame's predicted display time in the world-locked `LOCAL` space; the renderer (#19, #24) and
  the projection layer both use exactly that sample (`vr::frameViews()`), never a fresh locate at
  submission. Both eyes or none in the layer. A frame that must be ended without a new picture (a
  level load, a session stop) re-submits the last shown layers with their render poses — zero layers
  would show nothing, not hold the picture.
- **Even cadence over raw rate.** `vr_frame_divisor` in `cfg.ini` (`--vr-frame-divisor N` overrides
  it; default 1) makes the game take every n-th display slot by sleeping past the slots to skip and
  letting `xrWaitFrame` snap to the next one — never a timer of our own as the clock.
- **The rate / cadence line is the instrument.** One `[vr] rate:` line per 3 s window (frames
  against display slots, `xrWaitFrame` + `xrEndFrame` as one budget, slots per frame with the
  jitter, the worst frame and the worst run, MATCHED / OVER-SUBMITTING / UNDER-SUBMITTING and EVEN /
  UNEVEN), the same numbers in `status.json` → `vr.pacing`, frame gaps as they happen. Its decision
  core is pure logic with host tests (`src/vr/FramePacing.h`, `tests/vr/FramePacingTest.cpp`).
- **An orderly exit.** On quit the session is asked to exit (`xrRequestExitSession`) and given up to
  2 s to reach `STOPPING` (`xrEndSession`) and `EXITING` before it is destroyed.

### Why
Two clocks (the monitor's vsync or a sleep-based limiter next to `xrWaitFrame`) pace the headset
twice: the Index's 80–144 Hz against a 60 Hz monitor, or a cap that slips a frame every 50 against
the display period (Titanfall 2 VR, F-117). Dishonored VR's "exceptional improvement in world
stability" came from submitting with the pose each eye was rendered with (F-21); their ghosting at
120 Hz came from frames landing 1.05–1.11 slots apart, fixed by an even cadence at a lower rate
(F-26). A log that says MATCHED / EVEN or names the phase of a gap settles "does it hold the rate"
without a headset session (F-07).

### Consequences
- The MVP's "holds the headset's refresh rate" is judged from the rate line: MATCHED and EVEN at the
  native rate, or EVEN at a clean divisor of it.
- #21 (desktop mirror) and #19 (stereo) keep this loop: render between `beginFrame` and
  `submitFrame`, take the poses from `frameViews()`. The loader frame pump of F-117 (keep the
  compositor fed through a level load) comes after the MVP.
- `vr_frame_divisor` becomes a row of the VR Video settings after the MVP (#181).
---

## D-023: The test save library is generated on each machine, not committed

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker (technical, #70); the owner can revisit

### Decision
- The test saves (`mvp-new-game`, `mvp-items`, `mvp-goblin-fight`, …) are **produced by a script**
  (`scripts/dev/make-test-saves.ps1`, scenarios in `scripts/dev/test-saves/scenarios.txt`), in one flat
  launch, into `%LOCALAPPDATA%\ArxFatalisVR\savelib`; they are **not committed**.
- A worker uses one with `smoke-test.ps1 -Save <name>` / `run-windows.ps1 -Isolated -Save <name>`: it is
  made the **only** save of the isolated user directory, so slot 0 is it.
- `make-test-saves.ps1 -Verify` checks the whole library in a fresh game; regenerate after a change to
  the engine's save format.
- What the script cannot place (a lever, chest or door in reach) is recorded by the owner in flat mode
  and copied into the library by hand.

### Why
- A save is a snapshot of the original game's level data and of the engine's save format: committing
  ~0.3 MB binaries per scenario would put game-derived data into a repository that may become public
  (the project requires the owner's own copy of Arx Fatalis, CLAUDE.md), and every save-format change
  would silently rot them.
- A script is reviewable, regenerates in about 2.5 minutes, and doubles as a test of `newgame`, `save`
  and `loadslot` themselves (the same game loads each save again and checks it).
- Slot numbers are positions in a list sorted by file time (newest first): only a single-save directory
  makes "this scenario" a stable `--loadslot 0`.

### Rules out
- Saves in git or in release assets; scripts that assume `--loadslot 1` is a given scenario.

---

## D-022: Budget raised to 80% weekly / 95% five-hour; checks kept in balance with time and usage

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the budget of D-013; refines points 1–3 of D-021)
- **Decided by:** project owner

### Decision
- **Weekly limit: up to 80%** (was 70%; 20% stay for the owner's other project). At 80% no new workers; running ones finish.
- **Five-hour limit: up to 95%.** At 95% the coordinator stops the non-Fable workers, waits for the reset
  and resumes them; the last 5% are for the coordinator to keep watching. Fable workers go on.
- **Going over a limit only with the owner's explicit permission**, given for that occasion; without it the coordinator stops at the limits above and reports.
- **The safety hook never stops a task**: it denies only the dangerous command, says what to do instead,
  and logs the denial for the coordinator; work goes on while there is budget.
- **CI is balanced with time**: GitHub-hosted runners are out (their minutes would run out); the
  self-hosted runner on the owner's PC costs task time. Every PR gets the **fast set** (build, host unit
  tests, one flat smoke test — a few minutes); the heavy checks (`drawcheck`, the spell draw check, longer
  runs) only when the PR touches the code they guard (path filters) or on demand.
- **Independent reviews are selective**: they spend usage, so only risky PRs get a reviewer, it checks
  the Definition of done and the decisions only (Sonnet, effort medium), and the coordinator checks small
  PRs itself.
- **The coordinator watches its own context**: short outputs, the status issue as external memory.
- **Worker count from measured usage (#217):** the coordinator plans the number of workers from data, not guesses -\r\n  `scripts\dev\usage-plan.ps1` (samples of both limits and of every session's cost, the calibration "% of a limit per dollar",\r\n  the cost of finished tasks per model, a forecast for the READY tasks). Run every cycle and before starting a worker; it\r\n  names the binding limit and the number of workers that keeps the week at <= 80% by its reset and the 5-hour window < 95%.\r\n  The coordinator starts no more than it recommends; one line of its output goes to the status page (#193).

### Why
The owner wants the work to keep moving while there is budget, with quality checks that pay for
themselves in time and usage.

---

## D-021: Quality and automation package — review before merge, CI gates, a safety hook, automatic order, research radar

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner (all ten proposals of the coordinator)

### Decision
1. **Every PR is reviewed before merge** by an independent reviewer session (`/review-pr`, Sonnet): each
   Definition-of-done item and coordinator addition needs evidence; decisions are checked. Workers wait
   for PASS (three "done" claims with a missing item on 2026-10-09: #141, #151, #71).
2. **CI is the automatic gate**: the flat smoke test, host unit tests, `drawcheck` and the spell draw
   check on every PR (#127).
3. **A safety hook** blocks dangerous commands of every session even with prompts off (#185).
4. **Automatic task order**: `plan-next.ps1` computes the tasks ready to start (#187).
5. **The dev scripts survive the GitHub GraphQL limit** (REST first, a board cache) (#186).
6. **Session control and spend**: a per-session ledger from the status line; stuck or overfull workers are
   caught; spend per task calibrates model choice.
7. **A status page**: the pinned issue "ARXVR status", updated by the coordinator every cycle.
8. **Research radar**: one reference project per block, OpenMW VR first (#188).
9. **A VR game checklist** to audit the plan against (#189).
10. **The owner's impressions** after each headset test are collected and triaged.

### Why
The owner wants development to run on its own with the quality checked automatically and unknown
features found through research, not by chance. Details: [COORDINATOR.md](COORDINATOR.md) §3, §4, §4b.

---

## D-020: Our own VR settings — the MVP has the Controls section, with remapping

- **Date:** 2026-10-09
- **Status:** Accepted (moves the left-handed mode of D-019 into the MVP)
- **Decided by:** project owner (agreed by questions, D-017)

### Decision
- The game gets **its own VR settings**, section by section. **The MVP has "Controls"**; sound,
  graphics and the rest stay in the game's own settings for now and get VR sections later.
- **Controls** in the MVP:
  - **remapping:** a list of gameplay events (jump, inventory, book, crouch, use, …); for each event the
    player assigns a **single button, a chord of two buttons on one hand, or a long press**; conflicts are
    shown; "reset to the default layout";
  - **turning:** snap or smooth, the snap angle (30 / 45 / 90, default 30, D-019), the smooth speed;
  - **crouch and height:** physical crouch on / off and its threshold, seated mode, "calibrate height";
  - **left-handed** mode (dominant hand).
- **Default layouts per controller type**: the game detects the controllers (OpenXR interaction profile —
  Index, Touch first) and offers the standard layout for them (the D-019 table); a remapped layout is
  **saved per controller type** and picked automatically.
- The VR settings open from the **main menu and the pause menu** (an entry "VR" next to the game's own
  options), on the centred panel with the menu pointer.

### Why
Players should be able to fit the controls to themselves inside the game, on any supported controllers;
the controls are the base of the VR settings that grow later (sound, graphics).

### Consequences
- **#30** builds the remapping into the action set from the start: one OpenXR action per physical
  control, the game's own table maps controls (single / chord / long press) to gameplay events; the
  interaction profile is detected. SteamVR's binding UI stays usable on top (F-139).
- **#35** provides the default layouts per controller type as those tables.
- A new task builds the **VR settings menu with the Controls section** (on #49's panel and #34's pointer):
  #178.
- **Later sections, agreed for after the MVP:** **Video** (#181) — render resolution %, MSAA, anisotropic
  filtering, draw distance, VR brightness and gamma, desktop mirror mode, an FPS counter; **Comfort**
  (#182) — a movement / turn vignette and world scale %. Not settings: FOV, foveated rendering, refresh
  rate and motion smoothing (the headset's / SteamVR's).

---

## D-019: The MVP control scheme

- **Date:** 2026-10-09 (game menu, recenter and the grip rule added 2026-10-10)
- **Status:** Accepted
- **Decided by:** project owner (agreed by questions, D-017; builds on D-008, D-010, D-016, D-018)

### Decision
Valve Index first; Quest 3 Touch the same (A/B on the right, X/Y on the left).

| Action | Control |
|---|---|
| Walk | left stick, forward = where you look (D-008); **speed by deflection** — slightly = sneak / walk, fully = run; no run button |
| Turn | right stick left / right; **snap 30° by default**, smooth turn selectable in the settings |
| Jump | A (right) |
| Crouch | physically (D-018) or a toggle on the right stick down |
| Reticle per hand | a hidden ray from each hand; the reticle turns yellow with the item's name on an interactive object (D-016) |
| Click / drag | the trigger of either hand: press = click; hold and move = drag; release = drop where the game says (inventory, floor, surface, throw) |
| Use an item (eat, equip) | grip on the item (the original's right click). **The grip is a closed hand:** the Index is strapped on, so a **light squeeze = grab, an open hand = nothing** (no hard squeeze needed); **"use" fires when the hand opens again** if no trigger came while it was closed, however long it was closed *(owner, 2026-10-10, after the #30 headset run)* |
| Straight into the inventory | **grip held + trigger** on a world item (the original's stealth-click), one hand, without opening the inventory; the hand closed + the trigger = **stash only, never a use** *(owner, 2026-10-10)* |
| Doors, levers, chests, bodies, talking | **one trigger press acts at once** (sent to the game as its double click) |
| Inventory | **B on the right**; the panel opens in the centre of the view and stays where it appeared while open; **the weapon stays in the hand** (no sheathing in VR) |
| Character book (stats, map, diary, spells) | **B on the left** (Y on Quest), on the same centred panel |
| Menus | a pointer only while a menu is open (D-010); both hands can use it |
| Game menu (open / close) | **A on the left** on the Index (free in this table; the Index has no menu button), the **left menu button** on the Touch *(owner, 2026-10-10, after the #30 headset run)* |
| Two reticles on different items | the name shown is the one of the **hand that moved last** |
| Recenter | ~~hold a button chord (F-34)~~ → **long press of the left stick click** *(owner, 2026-10-10, after the #30 headset run)* |
| Runes | drawn in the air with the hand (D-010); the button that starts casting is decided with the magic block (#43–#45) |
| Left-handed mode | ~~after the MVP~~ → in the MVP, in the VR settings (D-020) |

### Why
The original's mouse model stays the engine's (each hand produces the cursor), so the controls add little
new mechanics; the owner's key case — sword in the right hand, open the inventory, drag an item from the
floor into it with the left hand — works with these buttons.

### Consequences
#30 action set and #35 bindings follow this table; #32 snap 30° default + smooth option; #33 jump, crouch
toggle, speed by deflection; #34 menu pointer for both hands; #37 / #38 the reticle rules (the proposal in
RESEARCH-reference-mods.md 13g with these answers); #49 the centred panel for the inventory and the book.

---

## D-018: The camera is where the head is — full head tracking in the MVP

- **Date:** 2026-10-09
- **Status:** Accepted (replaces D-006)
- **Decided by:** project owner (design agreed by questions, D-017)

### Decision
- The VR camera follows the **headset's position as well as its orientation** — "the camera flies where
  the head is", as in other VR games. A fixed eye height kills VR.
- **Height:** the camera moves with the head continuously (head 20 cm lower → camera 20 cm lower). When
  the head drops below a threshold relative to the standing height, the character **also crouches in the
  game** (stealth, low passages, hitbox); standing up makes it stand (#100).
- **Leaning and steps:** horizontal head movement moves the camera — peek around a corner, take a few real
  steps; the character's body follows the head with the game's collision. A head pushed into geometry
  **fades the screen** (the usual VR rule) instead of seeing through walls.
- **Player height:** at recenter the player's **standing head is mapped to the character's eye height**,
  so tall and short players see the world from the character's eyes and doors and tables keep their size.
  Seated play: recentering while seated does the same (#28).

### Why
Immersion: real crouching, leaning and stepping are what make VR feel physical; the owner tried the
fixed height in thought and rejected it.

### Consequences
- #24 becomes "HMD pose (orientation and position) → camera"; #27 maps the standing head to the
  character's eye height; #100 crouch threshold on top of the continuous height; #26 (leaning, room-scale,
  wall fade) moves from M2 into the MVP; #25 makes the body follow the head with collision; #28 captures
  the standing height at recenter and covers seated play.

---

## D-017: Player-experience design is agreed with the owner before any work starts

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- Anything the player experiences — controls and bindings, world interaction, locomotion and comfort,
  where UI sits and how it looks (position, colour, shape, size, behaviour), feedback (haptics, sounds,
  highlights) — is **discussed with the owner before** its issue is written or changed and before a
  worker starts: the coordinator sends a short design summary with concrete questions, plays the design
  back in its own words, and waits for a yes. The agreed design is recorded here as a decision.
- **Technical internals** — where a value comes from, how it is updated and rendered, architecture,
  tooling — the coordinator and the workers decide on their own.
- Example: the HP bar — its place, colour and shape are the owner's; reading HP from the engine and
  redrawing the bar are technical.

### Why
D-016 was first recorded as "left hand drags, right hand takes to the inventory" when the owner meant
"both hands work as the mouse". Agreeing the design first avoids building the wrong thing.

---

## D-016: World interaction — both hands work as the mouse, each with its own reticle; the inventory opens in the centre of the view

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the "world interaction by reach" point of D-010). Corrected the same day:
  the first wording split the hands ("left drags, right takes to the inventory") — a misreading of the
  owner; the decision below is the owner's intent.
- **Decided by:** project owner

### Decision
- **Both hands emulate the mouse fully.** Each hand has its own **reticle** — a small crosshair where the
  hand points, with **no visible beam** — within the engine's own interaction distance; the engine's own
  "what is under the cursor" test runs at the reticle (the hidden hand ray of F-57 replaces the mouse).
- With either hand the player does what the mouse does in the original: pick an item up and **drag** it,
  move it over surfaces and set it down on any plane, drag it **into the open inventory**, use doors,
  levers, chests, bodies and characters (the trigger is the game's click).
- **The inventory opens as a panel in the centre of the player's view** (for now; the panel itself is #49).
- **Design case:** the right hand holds a sword in a fight → the player opens the inventory, drags an
  item from the floor into it with the left hand, closes it and fights on.
- Unchanged from D-010: no rays or lasers from the hands, runes drawn in the air with the hand, spell
  direction from the hand, a pointer only while a menu or the inventory is open (#34).

### Why
In the original the player walks up to an item, aims the mouse at it and drags it over surfaces or into
the inventory. Grabbing with the hand and placing by hand would need new carry-and-place mechanics and
much more work; a reticle keeps the game's own logic and is what VR ports commonly do. Two hands keep it
natural. The reference to learn from is **Valheim VR (VHVR)** — its world interaction is studied in detail
before #37 / #38.

### Consequences
- #37 and #38 become "a reticle per hand + the engine's drag / use" instead of reach-and-grab; #49 opens
  the inventory in the centre of the view; #36 (full body with IK arms) no longer has to hold items in
  the palm.
- Catalog: F-57 changed (reticle, no grab by reach); F-138 (distance-grab gesture) not taken.

---

## D-015: Model and effort by task size — Opus by default, Fable only for really hard tasks

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the model part of D-013)
- **Decided by:** project owner

### Decision
- **Opus** is the default model for engine work, **Sonnet** for scripts, CI, docs and research summaries.
- **Fable** only for really large and hard tasks (the OpenXR frame loop and stereo rendering, the IK body,
  rune recognition, crashes nobody could explain) — chosen by the coordinator per task, never by default.
- **Effort** is set per task by the coordinator (`start-worker.ps1 -Effort`): lower for routine work,
  higher for hard design or debugging; defaults Sonnet medium, Opus high, Fable xhigh.
- Context is spent carefully but fully: one task per worker with a precise Definition of done, short
  coordinator messages, big tasks split.

### Why
Half of Fable's weekly limit went in the first hours with Fable on every engine task (D-013); most of
those tasks did not need it. The playbook table: [COORDINATOR.md §6](COORDINATOR.md#6-models-and-effort-d-015).

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
   slot (`%LOCALAPPDATA%\ArxFatalisVR\game-<n>.lock`; since #205 `%ProgramData%\ArxFatalisVR\locks`, shared by every Windows account including the CI runner service; a VR launch also `vr.lock`) while the game runs,
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
- *(Superseded by D-015: Opus by default, Fable only for really hard tasks.)* **Models** (replaced D-012's table): **Fable** for every engine task (any area except build and docs)
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
- **Status:** Accepted, except "world interaction by reach" — superseded by D-016 (a hand-aimed reticle from either hand)
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
- **Status:** Superseded by D-018 — the owner wants full head tracking: a fixed eye height kills VR
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
