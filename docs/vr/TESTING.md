# Arx Fatalis VR — Testing without a headset

How a worker session verifies the game before the owner puts the headset on: the smoke test, the
test command channel with `status.json`, and the `xr_hello` runtime probe. What was measured on the
owner's PC (2026-10-08, #99), what is reliable enough to be a gate, and what is a manual check.
The rules that use this are in [WORKFLOW.md](WORKFLOW.md) (*Workers test the game themselves first*,
*Verification tiers*); the reference these tools follow is Dishonored VR's harness
([RESEARCH-reference-mods.md](RESEARCH-reference-mods.md), F-01, F-02, F-03, F-16).

## In one minute

```powershell
.\scripts\build-windows.ps1                                   # builds AND runs the host unit tests (arxtest, CppUnit, tests/); -SkipTests to skip the run
.\scripts\dev\smoke-test.ps1                                  # flat, --loadlevel 10: PASS / FAIL + evidence
.\scripts\dev\smoke-test.ps1 -Vr                              # the same in VR - only while the owner has SteamVR running (SKIPPED, exit 2, otherwise; never starts it)
.\scripts\dev\smoke-test.ps1 -VrSim basic                     # VR on the simulated headset (#112): no runtime, no SteamVR, any time; per-eye shots judged
.\scripts\dev\eye-check.ps1                                   # the two-eyes check (F-06) on the simulated headset: six legs, one launch
.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5            # a table of 10 runs
.\scripts\dev\smoke-test.ps1 -Saves save0000 -- --loadslot 0  # copy a save into the isolated user directory, then load it
.\scripts\dev\smoke-test.ps1 -Save mvp-goblin-fight           # a scenario of the test save library (#70): the only save, --loadslot 0
.\scripts\dev\make-test-saves.ps1                             # produce the test save library, one launch; -Verify loads it all in a fresh game (#70)
.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0
.\scripts\dev\smoke-test.ps1 -Menu -Commands 'loadslot 0'       # plain launch (main menu), then a save through the channel
.\scripts\dev\smoke-test.ps1 -Commands 'drawcheck 120'         # two draws of the scene per frame must give one picture (#71)
.\scripts\dev\comfort-check.ps1                               # shake, FOV zoom and bob with the VR comfort policy off and on, one run (#76)
.\scripts\dev\head-check.ps1                                  # a simulated head pose drives the camera: turn, move, lean limit, recenter, off - one run (#24)
.\scripts\dev\body-check.ps1                                  # the body after the head on the simulated headset: ±40° drag, ~1.5 s settle, camera still, heading turn, recenter - one run (#25)
.\scripts\dev\spell-drawcheck.ps1                             # the 26 spells of #71 really launched, then drawcheck 240 (#155)
.\scripts\dev\input-check.ps1                                 # the VR controls' table (#30): taps, chords, a remap and a reset through vr-input, one flat run
.\scripts\dev\game-input-check.ps1                            # the VR input module (#74): VR buttons -> inventory, book, crouch, jump, game menu; the move vector; one flat run
.\scripts\dev\run-script-tests.ps1                            # the PowerShell self-tests of the dev scripts (seconds, no game); CI runs them first
.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence      # what CI runs after the build: smoke test + drawcheck, spell draw check, evidence in one folder (#127)
.\scripts\run-windows.ps1 -Isolated -TestChannel -- --no-vr --loadlevel 10   # a manual run with the channel, in this copy's own user dir
.\scripts\dev\log-digest.ps1 -Print                           # the last run in a screenful + automatic bug filing; runs by itself after every launch (#131), never cat arx.log
.\scripts\dev\game-status.ps1 -Watch                          # live state line
.\scripts\dev\game-cmd.ps1 'shot before' 'teleport -p marker_0001' 'shot after'
.\scripts\dev\game-cmd.ps1 quit
& "$buildDir\xr_hello.exe"                                    # the runtime alone: exit 0 / 1 / 2
```

`smoke-test.ps1` exits 0 only when every run passed (1: a run failed; 2: the test was skipped — `-Vr`
while SteamVR is not running, or the game lock stayed busy); a worker quotes its verdict lines in the
PR's *How verified*. Every launch counts against the budget of 10 per task (WORKFLOW.md, D-014).

Every path a test run touches is **per working copy** (the key `<folder>-<hash>` of
`scripts/build-paths.ps1`, the same one the build directory uses): the game's user and config
directory `%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>` (`arx.log`, `cfg.ini`, saves, crash reports)
and the channel directory `%LOCALAPPDATA%\ArxFatalisVR\test\<key>`. Parallel workers therefore never
read each other's log or overwrite each other's `status.json`, and never touch the player's
`%USERPROFILE%\Saved Games\Arx Libertatis` (`-Shared` opts into it on purpose). Game *runs*, however,
are one at a time on the PC. See [Isolation and the game lock](#isolation-and-the-game-lock-parallel-work).

## The smoke test (`scripts/dev/smoke-test.ps1`)

One run = start the Release build of this working copy (same lookup as `run-windows.ps1`) with
`--windowed --test-dir <dir> --user-dir <userdir> --config-dir <userdir>` (the isolated directories
above) plus `--no-vr` unless `-Vr`, and `--loadlevel 10` unless other `arx.exe` arguments follow
`--`; wait until `status.json` says `GAMEPLAY` or `CINEMATIC` (`MENU` for a plain launch); send each
`-Commands` line through the channel; stay `-Seconds` (default 10); quit through the channel
(`-CloseWindow`: WM_CLOSE instead, the player's path); then check:

| Check | Evidence | Fails when |
|---|---|---|
| exit code | `Process.ExitCode` | ≠ 0 (a crash exits with 1) |
| fresh log | `arx.log` `LastWriteTime` ≥ launch time | the game died before writing it, or never started |
| clean shutdown | `Clean shutdown` in that log | missing (crash, kill, hang) |
| no crash | new directories in the user directory's `crashes\` | one appeared |
| no errors | `[E]` / `[C]` lines in the log **and** `errors` in `status.json` | any |
| commands | every `-Commands` ack has `errors=0` | a command was refused |
| draw check | `drawCheck` in `status.json`, when a `drawcheck` was among the commands | it did not finish, or `framesDiffering ≠ 0` |
| game events | `events.byName` in the final `status.json` ([game event log](#the-game-event-log), #130): always `vr.mode=1` and `game.state>=2`, plus every `-ExpectEvents 'name=N'` (exactly) / `'name>=N'` (at least) | no `events` in the status, or a count differs (`game events: spell.cast=0 (expected 1)`) |

The event check of #130, the one to re-run after touching anything that logs a game event:

```powershell
.\scripts\dev\smoke-test.ps1 -Commands 'setplayercontrols on','cinemascope off','book open','status','book close',
  'ui inventory open','status','ui inventory close','status','status','inventory playeradd magic/potion_life/potion_life',
  'addxp 3000','spellcast -fsm 5 magic_sight self','spellcast -fsm 5 fake_summon self','attachnpctoplayer',
  'save eventtest','status','human_base_0033.dodamage player 1000','status','status' `
  -ExpectEvents 'game.new=1','level.load=1','level.loaded=1','game.state>=3','ui.book=2','ui.inventory>=2',
  'item.toinventory=1','player.levelup=1','spell.cast=1','spell.refused=1','script.error=1','save=1','player.death=1'
# => [ok] game events as expected (...; total=21)   =>  PASS   (the attachnpctoplayer [W] is intended)
```

It prints the state timeline (`+1.1s state GAMEPLAY (frame 2)`), the acks, the `[E]`/`[W]` lines,
the last lines of the log, the head of a new crash report, and `PASS` / `FAIL` with every reason.
`-Repeat N` adds a table and a `passed X/N` line. The game runs with `ARX_UNATTENDED=1`: a crash
writes its report and exits instead of waiting for someone to click the crash dialog.

Before the first run the test applies the launch rules of D-014 (`scripts/launch-lib.ps1`): it runs
only this working copy's own build made from its `HEAD` (`built-from.txt`; `-Rebuild` builds first),
is **skipped** (exit 2) with `-Vr` while `vrserver.exe` is not running — it never starts SteamVR —
and holds the machine-wide **game lock** for all its runs, waiting up to `-LockTimeoutSec` (300 s)
while another game runs, printing the holder, and skipping (exit 2) when the wait runs out. The game
window is shown without taking the focus (`window: focused=False` in the output; `status.json` says
the same). Every run is one line in the ledger `%LOCALAPPDATA%\ArxFatalisVR\launches.log`.
A fresh isolated directory has no `cfg.ini` yet, so the first run logs one warning
(`Could not read config files cfg.ini and cfg_default.ini, using defaults`) and writes the file;
`-Saves <slots>` copies save folders into it (from `-SaveLibrary`, `$env:ARXVR_SAVE_LIBRARY`,
`%LOCALAPPDATA%\ArxFatalisVR\savelib`; a name that is not there but is in the player's own `save` folder
is taken from there) for `-- --loadslot` runs; `-Save <name>` makes one library save the **only** save of
the directory and defaults the arguments to `--loadslot 0` (see [The test save library](#the-test-save-library-70)).

## The test command channel (`--test-dir DIR`)

Enabled by `arx.exe --test-dir <dir>`; off otherwise (no files, no cost). While enabled the game:

- **polls `<dir>\command.txt`** every 100 ms: one command per line, `#` comments, consumed on read
  (deleted), a file older than the process start is discarded with a log line. Write it with
  `scripts/dev/game-cmd.ps1` or `Send-ArxCommand` from `scripts/dev/test-lib.ps1`: both write a
  temporary file and rename it, so the game never reads a half-written batch, and wait for the ack.
- **answers every batch in `<dir>\ack.txt`**: `seq=<batch> frame=<frame> errors=<refused lines>
  last=<last line> error=<reason or ->`.
- **rewrites `<dir>\status.json`** every 250 ms, after every batch and on every state change —
  atomically (`status.json.tmp`, then rename), so a reader sees whole files only.
- **flushes `arx.log`** at the same cadence, so `game.state` events and `[mark]` lines are readable
  while the game runs (since #129 the file logger flushes on its own after every warning or error
  and at least once a second — see *Log:* in [The log banner](#the-log-banner); the channel only
  makes it fresher).
- reports the state that the [game event log](#the-game-event-log) logs on every change as
  `game.state to=STARTUP|MENU|LOADING|GAMEPLAY|CINEMATIC|EXITED` — always, with or without the
  channel (#130 replaced the channel's own `[game] state:` lines).
- sets `ARX_UNATTENDED=1` in its own environment, inherited by the crash processor: no crash dialog.

### Commands

| Line | Effect |
|---|---|
| `status` | nothing extra — the status is written after every batch anyway |
| `shot [name]` | after the next rendered frame, save it as `<dir>\capture\<name>.bmp` (default `shot<N>`); `captureSeq` and `lastCapture` in the status |
| `mark <text>` | `[mark] <text>` in the log — an anchor for "the line I wait for must come after this" |
| `quit` | ask the game to quit at the end of the frame (the same path as closing the window) |
| `save <name>` | save the running game as a new slot named `<name>` (no thumbnail); the newest save is slot 0, so `loadslot 0` loads it back. A `save slot=save000N name=<name> ok=yes\|no` event either way; refused outside a running game (#130) |
| `loadslot <n>` | load save slot `n` (from the menu or in-game), refused with the slot count if it does not exist. The command-line `--loadslot <n>` of a missing slot logs an `[E]` and starts at the main menu (before #24 it started an empty level: a black world with only the HUD, TRAPS.md); `run-windows.ps1` / `test-pr.ps1` refuse such a launch before it starts |
| `ui inventory open\|close` | what the inventory key does (the book has the script command `book open\|close`); a `ui.inventory` event once the inventory is open / has finished closing. While a level script holds the player (the level 10 start: letterbox, controls blocked) the HUD is not drawn and a close never finishes — `setplayercontrols on` and `cinemascope off` first (#130) |
| `loadlevel <n> [target]` | change to level `n` in a running game (refused from the menu: start with `--loadlevel` or `loadslot`) |
| `newgame` | what the menu does for "New game" and the end of the character creation, without the screens: an average hero (all attributes 10, skills 2, nothing to distribute), level 1. Refused outside the main menu (a plain launch). The hero then lies in the start cell and `player.controlsBlocked` stays `true` for 30-40 s while the level script plays the awakening (#70) |
| `entities [filter]` | write one line per entity to `<dir>\entities.txt`: `<id> npc\|item\|fix\|marker\|camera\|other show=<n> pos=<x> <y> <z> [life=<n>]`; `filter` is a substring of the id. `show` is the engine's `EntityShowState`: 0 in the scene, 2 in an inventory, 6 equipped by the player. The position of a floor object (lever, chest, item) is on the **floor** (#70) |
| `setpos <x> <y> <z> [yaw]` | put the player at exact coordinates, optionally facing `yaw`. `y` is the **eye**: the floor is 170 units lower (a `y` at floor level puts the eye in the floor: black screen). About 100 units are a metre. Refused outside gameplay (#70) |
| `lookat <entity>` | turn the player (yaw and pitch) towards an entity, aiming 40 units above its floor position. The pitch is not kept by a save and does not matter in VR (the head decides); the yaw is kept (#70) |
| `drawcheck [frames]` | for the next `frames` frames (default 1) draw the scene view **twice** per frame with the same camera and compare the two pictures pixel by pixel; result in `drawCheck` of the status and one `[render] drawcheck N done:` log line — see [The draw check](#the-draw-check-drawcheck). Refused outside gameplay or while a check runs |
| `vr-input down\|up\|tap <control>`, `vr-input stick left\|right <x> <y>`, `vr-input bind <event> <binding>`, `vr-input reset` | the VR controls' table without a headset (#30, `vr/VRInput.h`): a control (`right.a`, `left.grip`, `right.stick_down` …) held, let go or tapped as if a controller did it, merged into what OpenXR reports and resolved by the same table at the next frame; the game then reacts as to the controllers (#74: inventory, book, jump, crouch toggle, the game menu, the mouse buttons). `stick` holds a stick deflected (the analog value only — its direction controls are `down` / `up`), seen in `vr.input.game.moveX/moveY/turn`; `bind` remaps one event (`bind jump left.grip+left.a`, `bind menu left.b:long`, `bind magic_mode none`); `reset` restores the default layout for the current controllers and lets go of everything. Refused with the reason for an unknown control, event or binding. `vr.input` in the status (`vr.input.game.held`: the actions, buttons and keys the engine is given); one `[vr] input: <event> (<binding>)` log line per event press, one `[vr] input: layout …` line per layout change |
| `recenter` | the head is measured from its next sample: a new head reference (#24; the player's button chord is #28). A `vr.head.reference reason=channel` event once a head is there |
| `headpose <yaw> <pitch> <roll> [<x> <y> <z>]` / `headpose off` | a simulated head for checks without a headset (#24): degrees as a person says them — yaw left, pitch up, roll left positive — and optionally the position in metres in the OpenXR LOCAL space (x right, y up, z back). It drives the first-person camera exactly like the headset (`vr.head` and `camera` in the status); the first pose takes the reference, later ones move the camera from it. A real headset pose wins over it; `off` removes it |
| `vr-sim <command>` | a command of the simulated headset (#112), only in a `--vr-sim` run: `head rot 25 0 0`, `head pos 0 -0.3 0`, `action trigger right 1`, `shot <name>`, `session visible`, `fault swap-eyes on`, … — now, or `@+<n> <command>` n frames from now on its timeline. See [The simulated headset](#the-simulated-headset---vr-sim-112) |
| `comfort auto\|on\|off` | who decides the VR comfort policy (#76, `vr::comfort()`): `auto` = the VR session (the default: view bobbing, screen shake and FOV zoom off in VR, `cfg.ini` decides in flat mode), `on` / `off` force the VR rules on or off — so a flat run can measure them. `comfort` and `camera` in the status; one `[vr] comfort:` log line per change |
| anything else | the **script console**: `[entity.]command args`, exactly as typed in the in-game console (`` ` ``). Context defaults to the player. Refused (and counted in the ack) when the entity or the command is unknown |

Useful script-console commands for test scenes (the full list is what `ScriptEvent` registers,
`src/script/Scripted*.cpp`): `teleport -p <marker>` / `teleport -l <level> <marker>`, `spawn npc
<object> <marker>`, `playeradd`, `addgold`, `addxp`, `setlife`, `invulnerability on`, `noclip`,
`setplayercontrols off`, `herosay <text>`, `showvars`, `sendevent <event> <entity>`, `worldfade`,
`cine <name>` (a `.cin` cinematic, e.g. `cine akbaa_dream_a`; development launches skip it with a
`cinematic.skip` event and the state never shows `CINEMATIC` — `--cinematics` plays it, #124).
Commands that drive the [game event log](#the-game-event-log) (verified in #130): `book open|close`,
`inventory playeradd magic/potion_life/potion_life`, `addxp 3000` (a level-up), `spellcast -fsm 5
magic_sight self` (a cast), `spellcast -fsm 5 fake_summon self` (a refused cast),
`human_base_0033.dodamage player 1000` on level 10 after `setplayercontrols on` (the player's death;
self-inflicted `dodamage` and damage while the controls are blocked do nothing), `attachnpctoplayer`
(an obsolete command: a `[W]` and a `script.error` event, no `[E]`).
A refused line costs nothing: `[W] [channel] script command failed: Unknown command: …` and
`errors=1` in the ack, no `[E]`.

### `status.json`

```json
{"pid":34120,"version":"0.1-dev + cead4a3",
 "build":{"commit":"cead4a3…(40 hex)","branch":"ARXVR-115-feat-log-banner-runtime","config":"Release"},
 "state":"GAMEPLAY","cleanShutdown":false,
 "frame":618,"uptimeMs":6240,"fps":120.2,"errors":0,"warnings":0,
 "menuMode":"ingame","area":10,"paused":false,"cinematic":false,
 "player":{"x":...,"y":...,"z":...,"yaw":...,"life":...,"lifeMax":...,"mana":...,"manaMax":...,"controlsBlocked":false},
 "camera":{"x":...,"y":...,"z":...,"pitch":...,"yaw":...,"roll":...,"fov":75.0,"bob":0.0,"shake":0.0},
 "spells":{"active":2,"launched":3,"activeByType":{"speed":1,"fire_field":1},
           "launchedByType":{"speed":1,"heal":1,"fire_field":1}},
 "comfort":{"mode":"auto","active":false,"viewBobbing":true,"screenShake":true,"fovZoom":true},
 "window":{"width":640,"height":480,"focused":true,"minimized":false,"fullscreen":false},
 "vr":{"supported":true,"enabled":false,"sessionState":"none","frames":0,"shownFrames":0,
       "resubmittedFrames":0,"discardedFrames":0,"frameGaps":0,"frameDivisor":1,
       "mirror":{"state":"flat mode","eye":"right","frames":0},"runtime":"",
       "manifest":"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json",
       "input":{"controllers":"index","profile":{"left":"/interaction_profiles/valve/index_controller","right":"…"},
                "syncs":5120,"down":"right.grip","active":"","trigger":{"left":0.0,"right":0.12},
                "grip":{"left":0.0,"right":0.81},"presses":{"interact_left":0,…,"jump":2,…},
                "layout":{"interact_left":"left.trigger",…,"jump":"right.a",…}},
       "pacing":{"reports":3,"seconds":3.0,"frames":241,"shown":1,"divisor":1,"periodMs":12.5,
                 "slotsPerSecond":80.0,"framesPerSecond":80.0,"waitMeanMs":11.9,"waitMaxMs":13.1,
                 "endMeanMs":0.3,"endMaxMs":0.9,"budgetMeanMs":12.2,"budgetMaxMs":13.4,
                 "gameMeanMs":0.3,"gameMaxMs":1.2,"slotsPerFrame":1.0,"cadenceSdMs":0.2,
                 "worstFrameMs":13.5,"worstRunFrames":0,"worstRunMs":0.0,
                 "verdict":"MATCHED","cadence":"EVEN"}},
 "events":{"total":13,"byName":{"game.new":1,"game.state":2,"level.load":1,"level.loaded":1,
           "spell.cast":1,"ui.book":2,"vr.mode":1}},
 "cmdSeq":3,"lastCmd":"mark hello","lastCmdError":"","captureSeq":1,"lastCapture":"...\\first.bmp"}
```

- `events` (#130): how many [game events](#the-game-event-log) happened since the start, in all and
  by name — **logged or not** (a kind with a limit is only counted past it). Assert on these numbers
  (`events.byName.'spell.refused' -eq 1`) instead of grepping the log.

- `state`: `STARTUP` (before the first level or menu), `MENU` (main menu, pause menu, credits,
  character creation), `LOADING` (written from inside the level loader — a level load is one long
  frame), `GAMEPLAY`, `CINEMATIC`, `EXITED` (with `cleanShutdown: true`, written right before
  `Clean shutdown` is logged).
- `errors` / `warnings`: log lines at `[E]`/`[C]` and `[W]` level since the process started, counted
  by a logger backend — assert on these instead of grepping a buffered log.
- `fps`: frames over the last status interval (0 in the final status).
- `vr.sessionState`: `none` without a session, else the OpenXR state as the runtime reported it
  (`idle`, `ready`, `synchronized`, `visible`, `focused`, `stopping`, …).
- `vr.frames`: OpenXR frames submitted with `xrEndFrame` since the start (one per shown game frame
  while the session runs, #18); `vr.shownFrames`: of those, the frames that carried layers, i.e. the
  runtime asked for rendering (`shouldRender`). With an idle headset the session stays
  `synchronized`, SteamVR sets `shouldRender` only for the first frame, and `shownFrames` stays at 1
  while `frames` grows — expected, not a broken frame loop (see TRAPS.md). `smoke-test.ps1 -Vr`
  fails a run with `vr.enabled` and `vr.frames == 0`.
- `vr.resubmittedFrames` (#20): frames ended with the previous picture because no new one was drawn
  (a level load, a session stop — expected to be a handful per run); `vr.discardedFrames`: frames
  the runtime discarded (an `xrBeginFrame` before the previous `xrEndFrame` — expected 0);
  `vr.frameGaps`: frames that came at least 80 ms and 2.5× the mean interval after the previous one
  (level loads produce them; in gameplay each one is a felt hitch); `vr.frameDivisor`: the expected
  display slots per frame (`vr_frame_divisor`, 1 = every slot).
- `vr.mirror` (#21): the desktop mirror — `state` is `on` while the window shows what one eye sees,
  `eye` which one (`right` by default — the owner's choice of 2026-10-10; `vr_mirror_eye` in
  `cfg.ini`: 0 = left, 1 = right; `vr_mirror = 0` or `--no-vr-mirror` keeps the flat frame in the
  window), otherwise why not (`flat mode`, `off: --no-vr-mirror`, `off: vr_mirror = 0 in cfg.ini`,
  `off: OpenGL has no glCopyImageSubData ...`, `off: the VR session ended`); `frames` grows with
  `vr.shownFrames` while it is `on` (0 in flat mode). One `[vr] desktop mirror:` log line per session
  names the eye size, the crop and where the flat frame quad lands in the window.
- `vr.pacing` (#20): the last rate / cadence report — present once the first 3 s window of a
  session closed (a shorter session gets its partial window at the end). See
  [The frame loop and the rate line](#the-frame-loop-and-the-rate-line-20) for every field and the
  verdicts; the two to assert on are `verdict` (`MATCHED`) and `cadence` (`EVEN`).
- `vr.stereo` (#19): `active` — the current frame draws the scene once per eye (a session that
  runs, a frame the runtime wants rendered, located eyes); `frames` — frames whose scene reached
  both eyes since the start (grows while a level is shown in VR; stays 0 with an idle headset, like
  `shownFrames`); `eyeWidth` × `eyeHeight` — the eye images; `ipdMm` and `cantDeg` — the distance
  and the angle between the two located eyes; `fovLeft`, `fovRight`, `fovUnion` — the four
  half-angles in degrees (left and down negative) of each eye's projection and of the frustum both
  eyes are culled with. See [Stereo rendering](#stereo-rendering-19).
- `vr.head` (#24): head tracking after the last first-person camera update. `source`: `hmd` (the
  eyes located for that frame), `held` (the last headset pose while the runtime has none), `sim` (the
  channel's `headpose`), `none` (flat: the camera is the engine's). `referenced` / `references`: a
  reference was taken / how many since the start; `refYaw`: its heading (engine degrees). `pitch`,
  `yaw`, `roll`: the head's own angles in LOCAL, engine convention (positive pitch looks down, yaw grows
  to the left); `x`, `y`, `z`: the head in LOCAL, metres; `offsetX/Y/Z`: what the head adds to the
  character's eye, game units (y down). Leaning and real steps (#26, D-018): `leanX` / `leanZ` / `leanM` —
  the head from the body (metres, the reference frame: x right, z forward; within `vr_body_follow_radius`
  = 0.1 m while the body can follow); `anchorX` / `anchorZ` — what the body walked after the head since the
  reference; `walkedM` — metres walked after the head since the start; `stepping` / `steps` — the body is
  walking after the head / episodes; `bodyBlocked` / `blocks` — the collision holds the body while the head
  goes on / episodes; `fade` (0..1) — how black the world is drawn; `wall` — `inside` (the head's near plane
  touches geometry the game's eye does not), `behind` (a wall between the character's eye and the head) or
  empty; `walls` — episodes; `farFade` / `fars` — the part of the fade a head too far from the body asks for
  (0.7 → 1.0 m) / episodes. Game events: `vr.body.step`, `vr.body.blocked`, `vr.head.wall`, `vr.head.far`
  (start / end, never per frame).
  The camera angles themselves are in `camera` (`camera.yaw` = heading + head yaw − `refYaw`, the
  heading being `player.yaw`, what the stick and the mouse turn). The body (#25, D-009): `lookYaw` =
  `camera.yaw`, the stick's forward (D-008); `bodyYaw` = heading + `bodyOffset`, where the player model
  faces (`player.modelYaw` shows the model really does); `bodyDragged` / `bodyDrags`: the head is at the
  25° dead zone's edge pulling the body / how many such episodes. Without a head all three are the heading.
  `camera.fov` stays cfg.ini's `fov` (the flat camera's): in a stereo frame the eyes render with
  their own projection from the headset's angles, reported in `vr.stereo` (#19; the quad-angle FOV
  #24 used while the flat frame was on the quad is gone).
- `player` is present only while a level is loaded; the `.xrs` runner (#113) asserts on these keys
  with `@modassert`.
  `player.y` is the **eye** (the camera is within a few units of it), not the feet.
  `player.controlsBlocked` is `BLOCK_PLAYER_CONTROLS`: scripts set it during scripted scenes — the
  awakening of a new game on level 1 holds it for 30-40 s — and a save made while it is `true` is not
  playable (#70): wait for `false` (`@until player.controlsBlocked False` in a save scenario).
- `camera` (with `player`): the first-person camera as the frame before left it — position, angles,
  `fov` in degrees, `bob` = its distance from the stable head position of the paused animation (view
  bobbing; 0 when off), `shake` = the quake amplitude applied that frame (0 without a quake or when
  screen shake is off). `comfort`: who decides the VR comfort policy (`mode`: `auto` / `on` / `off`),
  whether the VR rules apply (`active`) and what is allowed right now (#76). The status is written
  at the **start** of a frame, so the write that acknowledges a `comfort` command still carries the
  previous policy's camera — a check skips that sample (`comfort-check.ps1`).
- `spells` (with `player`, #155): `active` spells right now and `launched` since the level was loaded
  (the spell manager is cleared on a level change), each also by type (script names, only the types
  with a non-zero count). A spell counts as launched once it passed its own `CanLaunch` and started —
  the evidence that a `spellcast` did something, which the ack never is (TRAPS.md).
- `build`: the same build identification as the [log banner](#the-log-banner): the full commit and
  the branch compiled into `Version.cpp` (`branch` empty when detached), the CMake configuration.
- `vr.runtime`: the OpenXR runtime that answered this run (`SteamVR/OpenXR 2.17.10`), empty while no
  instance was created; `vr.manifest`: the runtime manifest the loader uses (`XR_RUNTIME_JSON`, then
  the registry), empty when no runtime is installed — the same values as the banner's runtime lines.
- `vr.input` (#30): the controllers and the game's control table. `controllers`: the type the layout
  is for (`index`, `touch`, `simple`; `unknown` until a runtime reports a profile — always in a flat
  run), `profile.left` / `profile.right`: the OpenXR interaction profile per hand (empty: no device for
  that hand yet — SteamVR binds a few frames after the session is `focused`, so an idle headset never
  shows one), `syncs`: action syncs while focused, `down`: the controls held after the last frame
  (space separated), `active`: the events on now, `trigger` / `grip`: the analog values per hand
  (`squeeze/value`: on the Index how far the strapped hand is closed — TRAPS.md), `presses`: how often
  each event came on since the start, `layout`: the binding of every event (`none`, `right.a`, `left.grip+left.trigger`,
  `left.stick_click:long`). Nothing in the game reacts to the events yet (#74); `input-check.ps1`
  asserts on `presses` and `layout`.
- `drawCheck` appears once a `drawcheck` was requested: `{"seq":1,"running":false,"frames":120,
  "framesDiffering":0,"pixels":307200,"maxPixelsDiffering":0,"maxDelta":0,"capture":""}` —
  `frames` compared so far, `framesDiffering` of them with at least one differing pixel,
  `maxPixelsDiffering` the worst frame, `maxDelta` the largest difference of one color channel
  (0–255), `capture` the base name of the most differing pair (`<capture>_a.bmp` / `_b.bmp`).

## The test save library (#70)

Saves that put a worker at an MVP scenario point at once, so a task about picking things up or fighting
does not start with a minute of walking. **Not in the repository** (D-023): they derive from the
original game's data and go stale with the engine's save format; they are produced on the machine that
needs them, by one script and one game launch.

```powershell
.\scripts\dev\make-test-saves.ps1                    # produce the library: one launch, ~2.5 min (30-40 s of it the awakening)
.\scripts\dev\make-test-saves.ps1 -Verify            # a FRESH game (--loadslot 0) loads every save and checks it: one launch
.\scripts\dev\smoke-test.ps1 -Save mvp-goblin-fight  # a worker's use: that save, nothing else, --loadslot 0
.\scripts\run-windows.ps1 -Isolated -Save mvp-items -TestChannel -- --no-vr   # the same by hand, in a window
```

The library is `%LOCALAPPDATA%\ArxFatalisVR\savelib` (`$env:ARXVR_SAVE_LIBRARY` or `-SaveLibrary`
elsewhere): one folder per scenario with `gsave.sav`, and `manifest.json` (name, description, level,
player position, commit, date). It is shared by every working copy of the machine. **Regenerate it**
(`make-test-saves.ps1`) when the engine's save format changes, or a save fails to load; `-Verify` says
which.

| Save | Where the player stands (level 1, the goblin prison) | For |
|---|---|---|
| `mvp-new-game` | in the start cell after the awakening, controls free, nothing equipped | demo step 1-2: look, walk, turn |
| `mvp-items` | a sword equipped; 2.3 m from a table with a key, a note and a form (`key_base_0005`, `note_0004`, `blank_form_0001`); dark room, the items are low in the view | demo step 3: pick up, drag into the inventory (#37) |
| `mvp-goblin-fight` | a sword equipped; `goblin_base_0036` 3.5 m ahead in a corridor, walking away | demo step 5: melee (#39) |

**Not in the library: levers, doors, chests.** Nothing could put the player where one is in reach and
visible; the owner records those by hand (follow-up #210). What that attempt
learned is in TRAPS.md ("A lever, a chest, a door cannot be found by coordinates").

**How a save is made** (`scripts/dev/make-test-saves.ps1`, scenario file
`scripts/dev/test-saves/scenarios.txt`): one flat game from the main menu; `newgame`; `@until
player.controlsBlocked False`; per scenario `setpos` + `lookat` + `save <name>`; then the **same game
loads every save again** (`loadslot`, oldest first) and checks the level, that the player stands where it
was saved (within 1 m) and that the controls are free, with a screenshot per save. The script **fails**
(nothing is copied into the library) when a command is refused, the game ends unclean, or a check fails.
A scenario file has `@`-directives (`@until`, `@expect`, `@near`, `@entity`, `@print`, …) — the header of
the script lists them. The parser (`scripts/dev/test-saves/scenario-lib.ps1`) rejects what would only
fail after a minute in the game — an unknown directive, a missing or non-numeric argument, a save name
used twice or unusable as a folder name — with the file and line, before any launch.

**Host test** (`scripts/dev/test-save-library.ps1`, run by `run-script-tests.ps1`, so by CI): the real
scenario file, the parser and its 13 error cases, the verification steps (no `loadslot` for the slot the
command line loaded, the position formatted the same in every culture), the slot order (newest save
file first), and `Use-ArxSave` / `Get-ArxSaveLibrary` (exactly one save left, the file time kept, a
missing name names what the library has). It was shown to fail with 7 deliberate breaks, one check
each: the duplicate-name check removed, the old saves not removed, the slot order reversed, the
position formatted with the current culture (`8540,5`), unknown directives accepted, any save name
accepted, the copy losing the file time.

What a save does and does not keep (measured, #70): the position, the yaw, the inventory and what is
equipped, the entities' state (the goblin's patrol goes on from where it was). Not the pitch of the view
(irrelevant in VR). The verification waits 4 s after a `loadslot` before its screenshot (the level fades
in from black, TRAPS.md).

## The log banner

Every `arx.log` starts with six `Key: value` lines (#115, #129 and #130, research rows F-18, F-98,
F-92 and F-146), so a log says which build wrote it, how it was started and where it is written
before anything else; the run digest (#131) reads them. Keep their shape when touching them.

```
[I] Startup:252  Starting Arx Fatalis VR 0.1-dev + f8b3585
[I] Startup:111  Build: f8b3585637414910c931b733d27661cb8f4c407f on ARXVR-115-feat-log-banner-runtime, Release, MSVC 19.44, x86_64
[I] Startup:141  Executable: "C:\Users\…\Release\arx.exe", linked 2026-10-09 14:02:55, built from f8b3585637414910c931b733d27661cb8f4c407f dirty
[I] Startup:163  Run: 2026-10-09 15:10:03 local (12:10:03 UTC), pid 1234, command line: --windowed --no-vr --loadlevel 10
[I] Startup:203  Launch: parent "pwsh.exe" (pid 23768), elevated no, proxy DLLs next to the executable: none
[I] Startup:208  Log: "C:\Users\…\arx.log", previous runs kept as arx.1.log .. arx.9.log
```

- `Build:` the commit and branch compiled into `Version.cpp` (`(detached)` without a branch), the
  CMake configuration, the compiler and the architecture. A Debug build, or a Release build with
  debug output (`-DDEBUG=ON`), ends with `performance numbers not comparable`: its timings are never
  compared with a release build's.
- `Executable:` the path and link time of `arx.exe` and `built from <sha>[ dirty]` as
  `built-from.txt` next to it says (written by `build-windows.ps1`; `unknown` without the file). A
  `[W] built-from.txt names commit … but the executable carries version information from …` follows
  when the two commits differ: the compiled-in version is stale — rebuild (see [TRAPS.md](TRAPS.md)).
- `Run:` the start time in local time and in UTC (crash report folders are named in UTC), the pid
  and the command line.
- `Launch:` (#130, research row F-146) the process that started the game (`pwsh.exe` for the
  scripts, `steam.exe`, `explorer.exe` for a double-click, `vrmonitor.exe` / `vrstartup.exe` from
  SteamVR), whether the game runs **elevated** (as administrator — an elevated game and a
  non-elevated runtime do not mix well), and any **proxy DLL** next to `arx.exe` that overlays,
  injectors and graphics mods use to hook into a game: `opengl32.dll` (ReShade's OpenGL mode),
  `dxgi.dll`, `d3d9.dll`, `d3d11.dll`, `dinput8.dll`, `version.dll`, `winmm.dll` — none ships with
  this build. A found one is also a `[W] Found <names> next to arx.exe in "<folder>" …`. Checked
  with an empty `d3d9.dll` next to the build (#130): the line and the `[W]` appear, the run passes.
- `Log:` the file this run writes and what became of the previous runs (#129, research row F-92).
  At startup `arx.log` → `arx.1.log` → … → `arx.9.log`, the oldest dropped: **ten runs** per user
  directory (an isolated self-test directory rotates its own; the smoke test no longer deletes
  `arx.log` before a run). When a file cannot be moved — another process holds it open — nothing is
  deleted: the line says `appended to the previous run's log`, a `[W] Log rotation failed: could not
  move … to …` names the files, and this run's banner is simply the next `Starting` line in the same
  file. The log is **kept on disk while the game runs**: a warning, error or critical line is flushed
  at once, everything else by the logger's own flush thread at least once a second (also while the
  main thread sits in a level load or a hang), so a hang, a `Stop-Process` or a crash loses at most
  the last second. Info lines are not flushed one by one on purpose: a flush per line was a measured
  stutter source in Dishonored VR.

The VR runtime follows a few lines later, **on every run, flat ones included**:

```
[I] VRSystem:…  OpenXR runtime manifest: "C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json" (from HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime)
[I] VRSystem:…  OpenXR runtime answered: SteamVR/OpenXR 2.17.10
```

- The manifest line names the file the loader will use and where it was selected from:
  `XR_RUNTIME_JSON` (then also the registry value it overrides) or the registry key
  (`HKLM`/`HKCU\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`); `No OpenXR runtime manifest` when none is
  installed. A manifest file that does not exist is a `[W]` — a leftover `XR_RUNTIME_JSON` is the
  classic reason for "it started flat".
- `OpenXR runtime answered` appears once an instance exists. When the runtime that answered is not
  the one the manifest path suggests (SteamVR, Oculus, Windows Mixed Reality, Varjo, Monado, Virtual
  Desktop), a `[W]` names both.

## The game event log

One `arx.log` line per game and VR state change, **always on** (with or without the channel), so a
log alone says what was happening before a problem (#130, diagnostics D2; research rows F-105, F-118).
Every line comes from one place, so `GameEvent:` greps the whole stream:

```
[I] GameEvent:66   game.state to=GAMEPLAY was=LOADING area=10 t=0.54
[I] GameEvent:66   spell.refused spell=fake_summon reason=spell-cannot-launch t=3.02
```

Shape: `<area>.<what>` followed by `key=value` fields, the last one `t=` = seconds since the first
event of the run (`vr.mode`, at startup — `arx.log` lines carry no time of their own). Integers as
they are, floats with fixed decimals, booleans `yes` / `no`, strings bare when they are one word,
otherwise in double quotes with `"` and `\` escaped (`src/core/GameEventFormat.h`, host-tested by
`GameEventTest`). Events are state changes, never per frame; a kind that a script can repeat without
bound has a limit (`script.error`: the first 50, then one `event.limit name=script.error logged=50
rest=counted` line). `status.json` `events` counts every event, logged or not.

| Event | When | Fields |
|---|---|---|
| `vr.mode` | once at startup, after the VR decision | `mode=vr\|flat` (the lines before it say why it is flat) |
| `game.state` | the state changes | `to`, `was` (`STARTUP`, `MENU`, `LOADING`, `GAMEPLAY`, `CINEMATIC`, `EXITED`), `area` with `LOADING` / `GAMEPLAY` |
| `game.new` | a new playthrough starts (character creation or `--loadlevel`) | — |
| `level.load` / `level.loaded` | a level starts / finishes loading (new game, save, level change) | `area`; `ok`, `ms` |
| `save` / `load` | a save is written / a save starts loading | `slot` (`save0003`), `name`, `ok`; `area`, `quick` |
| `cinematic.skip` | a development launch skips a `.cin` (#124) | `name`, `reason` |
| `player.levelup` | the player gains a level | `level`, `xp` |
| `player.death` | the player dies | `source` (entity id), `damage`, `type` (damage type flags) |
| `player.combat` | the weapon is drawn / put away | `state=on\|off`, `weapon` |
| `player.jump` / `player.crouch` | a jump starts / the crouch toggle (`C`, the right stick down in VR) changes (#74) | `state=on\|off` (crouch) |
| `ui.inventory` / `ui.book` / `ui.container` / `ui.note` | opened or closed while playing | `state=open\|closed`; `bags=one\|all`, `page=stats\|spells\|map\|quests`, `entity` |
| `rune.drawn` / `rune.unknown` | a stroke was recognised as a rune / was not | `rune`, `moves` (numpad directions the recognizer saw), `index` |
| `spell.unknown` | the drawn runes are no spell | `runes` (`aam+yok`) |
| `spell.cast` | a spell launches (player or NPC) | `spell`, `caster`, `level`, `target` |
| `spell.precast` | the player stores a spell | `spell`, `level` |
| `spell.refused` | the player's spell does not launch | `spell`, `reason` (`runes-not-learned`, `no-mana` with `mana` / `cost`, `magic-disabled-by-level`, `spell-cannot-launch` — the spell's own silent check, `no-demon-within-900`, `not-implemented`) |
| `melee.hit` | a weapon strike reaches an NPC and the player is the attacker or the target — one per swing | `source`, `target`, `weapon`, `damage` (0 = the armour-class roll made it a miss), `aim` |
| `item.take` / `item.drop` | an item goes onto / off the cursor | `item`, `from=world\|inventory`; `how=throw\|drop\|place`, `count` |
| `item.toinventory` | an item enters the player's inventory or the container the player has open | `item`, `container` |
| `item.use` / `item.equip` | the player uses / equips an item | `item` |
| `script.error` | a script command fails (not the suppressed ones); the `[W]` / `[E]` before it says why | `entity`, `event`, `command`, `pos`, `abort` (limit 50) |
| `vr.session` | the OpenXR session state changes (F-118) | `state`, `was`, `meaning` (e.g. `synchronized`: frames are timed but not displayed — the runtime may pace the game as slowly as it likes) |
| `vr.focus` | the session leaves `focused` (Windows) | `state=lost`, `foregroundPid`, `own` (our process: the runtime's overlay took the input; another: the desktop focus went elsewhere), `class`, `title`, `hint` |
| `vr.profile` | the runtime's interaction profile changed | `left`, `right` (`/interaction_profiles/valve/index_controller`, `none`, or `error:XR_ERROR_ACTIONSET_NOT_ATTACHED` until #30 adds actions) |
| `vr.refspace` | the runtime moves a reference space (its own recenter, a new play area); for `local` the head reference is taken again (#24) | `space`, `poseValid` |
| `vr.head` | where the head that drives the camera comes from changed (#24) | `source=hmd\|held\|sim\|none`, `was` |
| `vr.head.reference` | a head reference was taken: the first head sample after the start, a level load, a new source, the runtime's recenter, the channel's `recenter` | `reason` (`level`, `runtime`, `channel`, `source hmd`, …), `source`, `yaw` (the head's heading, engine degrees), `bodyYaw`, `x`, `y`, `z` (the head in LOCAL, metres) |
| `vr.head.lean` | the lean reaches the interim limit, and when it is back within (#24, until #26) | `state=clamped\|free`, `limitM` (limit 100) |
| `vr.body.drag` | the head reached the 25° dead zone and pulls the body along, and when it let go (5° inside) (#25) | `state=start\|end`, `head`, `body` (degrees from the heading), `deadZone` (limit 100) |

Not here yet, by design: melee attacks *refused* by a gate (F-66) come with the VR swing detector
(#39); the recenter chord with #28; locomotion, interaction and rune-drawing in VR add their own events in the
same shape (#31, #37, #44). Measured in #130 (flat, `--loadlevel 10`, 60 s idle): 13 events in the
whole run, none per frame — after `GAMEPLAY` (t=0.54) only the level 10 intro script's own work:
`cinematic.skip` at t=29.55, four NPC `spell.cast` between t=52.67 and t=56.39, and
`game.state to=MENU` at t=58.41 — the intro sequence ends in the menu by itself (see TRAPS.md).

## The run digest (#131)

**Never `cat` an `arx.log` — read the digest.** After **every** game exit started by our tools
(`run-windows.ps1` — so the desktop shortcut and `test-pr.ps1` too — and `smoke-test.ps1`) the script
`scripts/dev/log-digest.ps1` runs by itself and prints 3–6 lines: the digest path, build and branch, exit
code, duration, errors/warnings (how many are **new**), crashes. The owner does nothing.

```powershell
.\scripts\dev\log-digest.ps1 -Print                  # the last run of this working copy (isolated dir, else Saved Games), digest on screen
.\scripts\dev\log-digest.ps1 -Id                     # who wrote this log: the banner lines, nothing else
.\scripts\dev\log-digest.ps1 -Find 'OpenXR' -Last 5  # the last 5 lines of the run matching a regex
.\scripts\dev\log-digest.ps1 -Condense               # the log in a screenful (logdig, F-112)
.\scripts\dev\log-digest.ps1 -DryRun -Log "$env:USERPROFILE\Saved Games\Arx Libertatis\arx.2.log"   # an older run, nothing written or filed
.\scripts\dev\log-digest.ps1 -Flush                  # send the queued reports now (a run does this itself)
.\scripts\dev\test-log-digest.ps1                    # the self-test: fixture logs, a fake GitHub, no game, no network
.\scripts\dev\test-github-lib.ps1                    # self-test of the dev scripts' GitHub access (#186): REST, board cache, rate limit, move queue
.\scripts\dev\test-start-steamvr.ps1                 # self-test of how -StartSteamVR starts and -StopSteamVR quits SteamVR (#171): stubbed processes and clock, SteamVR is not touched
.\scripts\dev\stop-steamvr.ps1                       # the OWNER's switch: quit SteamVR like its own "Exit SteamVR" so the headset powers down; workers never run it (D-014)
$env:GH_HOST='bogus.invalid'; .\scripts\dev\test-pr.ps1 <pr> -NoLaunch   # the test station with the GitHub API unreachable (#171)
.\scripts\dev\test-plan-next.ps1                     # self-test of plan-next.ps1 (#187): MVP.md parsing and every start rule; CI runs it with the other script tests (run-script-tests.ps1)
```

**What it writes:** `%LOCALAPPDATA%\ArxFatalisVR\runs\<yyyyMMdd-HHmmss>-pid<pid>\` with `digest.md` (build and
branch from the banner, command line, exit code, duration, `Clean shutdown` yes/no, crash reports with their top
frames, errors and warnings **grouped by signature** with the count, *NEW* or "seen in N runs" and the issue,
a timeline of the state lines, and the log in a screenful), `digest.json` (the same as data), a copy of
`arx.log` and of each new `crash.txt`. The last 30 run folders are kept. The timeline is in log order and
holds every [game event](#the-game-event-log) (`GameEvent:` lines, #130) with its `t=` seconds since
startup — the only times the log has; other lines join it through `$script:ArxTimelinePatterns` in
`log-digest-lib.ps1`.

**The screenful** (the `logdig` of F-112): lines are grouped into *families* (numbers, hex and paths replaced
by `N` / `<path>`); a family seen fewer than 6 times is printed in full and in order, a common one as
`count x last sample`. A 9,000-line log reads in one screen and the rare lines are where the answers are.

**Signatures.** A problem is a `[W]`, `[E]` or `[C]` line, or a crash report. Its signature is the
level + the source file (not the line number, which moves with every edit) + the message with numbers,
hex, ids and paths normalised, hashed to 12 hex digits; for a crash the title, the exception and the top
four frames of `arx.exe` without line numbers. `%LOCALAPPDATA%\ArxFatalisVR\signatures.json` remembers
every signature (first and last seen, in how many runs, the issue number). A run that is digested twice
counts once.

**Automatic bug filing** — fully automatic, GitHub **REST** only (`gh api`; never GraphQL, whose quota the
sessions share), only into the private repository (the flush checks `private` first):

| Situation | What happens |
|---|---|
| A new `[E]` / `[C]` signature, or a crash | An issue labeled `bug` + `auto-reported`: title from the signature, body with the build and branch, command line, exit code, how often, the stack or a log excerpt around the first occurrence, the digest; the user-profile part of every path is replaced by `<user>`. A hidden marker `arxvr-auto-report: sig=<hash>` in the body |
| A new `[W]` signature | Nothing yet — filed only when it shows up in a **second** run (new *and* repeated) |
| A known signature with an issue | A "Seen again" comment, at most **once per 24 h** per issue; the first comment waits a day after the issue was created. A **closed** issue whose problem comes back is **reopened** with a comment — close noise together with an allow-list line |
| More than 5 new problems in one run | One root cause logs several lines and a broken build hundreds: at most 5 new issues per run (crashes first); the rest stay in the digest (*new, not filed*) and are filed by a later run if they come back |
| A line that matches `scripts/dev/log-digest-allowlist.txt` | Shown in the digest, never filed. One .NET regex per line against `[W] Context:12  message`; add a line when you close an auto-reported issue as noise |
| `signatures.json` was lost, or a second machine | The open and closed `auto-reported` issues are searched by the marker before anything is created — no duplicate |
| No network, or the rate limit is spent | The report stays in `%LOCALAPPDATA%\ArxFatalisVR\pending-reports\` and the next run sends it (exactly once; a lock serialises two senders) |

It **never blocks the owner**: the digest is local and takes a moment; `run-windows.ps1` starts the GitHub
part as a hidden background process (`-Detach`), the smoke test sends after it released the game lock; any
failure is only a warning, the game's exit code and the smoke verdict are unchanged. After an unclean exit
it waits up to 20 s for the crash processor (`arx.exe --crashinfo`) to write its report. Opt out for one
shell with `$env:ARXVR_AUTOREPORT = 0` (digest only, nothing queued) or `-NoReport`; `-DryRun` also files
nothing and writes nothing outside a scratch folder.

**Triage** is the coordinator's (WORKFLOW.md, *Development diagnostics*): confirm or close as noise (and
extend the allow-list), link the issue to its epic, set the milestone.

## The draw check (`drawcheck`)

In VR the scene is drawn once per eye, so a level frame is *simulate once, draw per view* (#71):
`ArxGame::updateLevel()` advances the game, `renderSceneView()` draws the 3D world for the current
camera and may run any number of times, `renderInterface()` draws the HUD once. Anything that still
advances while drawing — a particle aging, a random walk, a light moving, a spell applying damage —
would run once per eye and give the eyes two different pictures.

`drawcheck [frames]` is the instrument: for the next `frames` level frames the scene view is drawn
**twice** with the same camera (`channel::sceneViewCount()` returns 2) and the two back buffers are
read back and compared pixel by pixel. The expected result is **0 differing pixels in every frame**;
the numbers land in `drawCheck` of `status.json`, one `[render] drawcheck N done:` line in the log,
and the most differing pair is saved as `capture\drawcheckN_a.bmp` / `_b.bmp` for a look. The smoke
test treats a `drawcheck` among its `-Commands` as a gate (it waits for the check to finish and fails
on `framesDiffering ≠ 0`).

What the check relies on and what it cannot see:

- **Random numbers.** Draw code jitters things on purpose (missile trails, lightning bolts, halos,
  the fissure's edges). `renderLevel()` saves the generator (`Random::getState()`) before the first
  view and restores it before every further view, so each view draws the same random numbers. That is
  also what the eyes need: the same flicker in both. A random *walk* that accumulates in draw code
  (`x += random`) still differs, and is a bug the check finds.
- **Per-view culling and projection** (`worldToClipSpace`, `VisibleSphere`) are fine in draw code:
  the same camera gives the same result. Game decisions that used to depend on the camera (a blood
  splat lands only in view, a field animates only in view) now use the frame's camera in the update.
- It compares the whole scene view, screen-space overlays included (hit blood, Magic Sight, the
  paralysis tint), but **not** the interface, which is drawn once per frame anyway.
- It needs flat rendering: in a stereo frame (#19) the two views are the two eyes with different
  cameras, so the command is refused there (`drawcheck: needs flat rendering`).
- It cannot see state that only affects the *next* frame (a counter incremented in a draw path and
  read later). Those show up as a visibly faster effect, not as a differing pair.

A typical check exercises the systems that were split, all at once — the spells check (#155):

```powershell
.\scripts\dev\spell-drawcheck.ps1              # PASS: 26 of 26 spells launched, 0 differing frames
.\scripts\dev\spell-drawcheck.ps1 -Flags fs    # the broken cast of #71 on purpose: must FAIL
```

It runs the smoke test with `invulnerability on`, 23 spells the player casts on itself
(`spellcast -fsm 5 <spell> self`), the other 3 cast on the player by an NPC
(`human_base_0033.spellcast -fsm 5 <spell> player`) and `drawcheck 240`; it samples `status.json`
meanwhile and fails unless every spell shows up in `spells.launchedByType`, some spells are in
`spells.activeByType` while the check runs, and the smoke test (with its draw check gate) passed. It
prints which command launched each spell and which spells were on screen during the check.

- **The flags matter:** on the **player** a `spellcast` launches at once only with `-s` **and** `-m`
  (`NOANIM` + `NODRAW`); with `-fs` alone it is queued for the rune drawing, which skips the player,
  so nothing is cast and every later `spellcast` on the player is ignored (TRAPS.md, found in #76).
  The first #71 spell row below used `-fs`: its spells never launched; #155 re-ran it.
- **Three spells need an NPC caster.** `fake_summon` launches only for a caster other than the
  player (`FakeSummonSpell::CanLaunch`). `summon_creature` and `raise_dead` cast by the player on
  level 10 never launch: their `CanLaunch` finds no valid spot 3 m ahead
  (`ARX_INTERACTIVE_ConvertToValidPosForIO`) — tried at the start point facing 0°, 90°, 180° and
  270°, and next to `human_base_0033`. Cast by that NPC they launch; the fissure drawn is the same.
- Order: the long-lived spells first, the projectiles and the NPC's summons last, so they are still
  in flight or open when the check starts (`harm` and `mana_drain` end on their own before that).

## The frame loop and the rate line (#20)

In VR one game frame is one OpenXR frame (D-024): `vr::beginFrame()` at the top of
`ArxGame::doFrame` blocks in `xrWaitFrame` until the headset wants the next frame — **the only
thing that paces the game in VR**: the window's vsync and the engine's `fps_limit` are off while a
session exists (`[vr] pacing:` log line) — then `xrBeginFrame` and `xrLocateViews` for the frame's
predicted display time; the game simulates and renders; `vr::submitFrame()` ends the frame with
exactly those poses in the projection layer (pose-matched submission, F-21 / F-116). A frame drawn
outside the loop (the loading screen) begins its own; a frame begun but never shown is ended with
the previous picture (`vr.resubmittedFrames`). On quit, `xrRequestExitSession` → `stopping` →
`xrEndSession` → `exiting` within 2 s (`OpenXR session exited in order after N ms`), else a warning
and the session is destroyed while running.

**The rate line** — one `[vr] rate:` line per 3 s window while the session runs (research row F-07),
the same numbers in `status.json` → `vr.pacing`:

```
[vr] rate: 241 frames in 3.00 s = 80.0/s on 80.0 slots/s (hmd 12.50 ms), 1 shown | wait+end 12.2 mean / 13.4 max ms (wait 11.9, end 0.3; game 0.3 mean / 1.2 max) | 1.00 slots/frame, sd 0.21 ms: EVEN CADENCE | MATCHED | worst frame 13.5 ms, worst run 0 frames 0 ms
```

| Part | Meaning |
|---|---|
| `N frames in S s = F/s on R slots/s (hmd P ms)`, `shown` | frames submitted per second against the headset's display slots per second (`periodMs` = one slot); `shown` = frames with layers |
| `wait+end mean / max` | `xrWaitFrame` + `xrEndFrame` of one frame: **one pacing budget** (F-117) — the runtime may put its wait into either call, so judge the sum against the period and read the split (`wait`, `end`) only as detail; `game` = from `xrEndFrame` to the next `xrWaitFrame`, the time the game itself takes |
| `slots/frame, sd` | the mean interval between frames in display slots and its jitter (`cadenceSdMs`) |
| `EVEN CADENCE` / `UNEVEN CADENCE` | EVEN: a whole number of slots per frame (within 0.1) with a jitter under 0.2 slot; UNEVEN is what ghosts on head turns (F-26: 1.05–1.11 slots at 120 Hz) |
| `MATCHED` / `OVER-SUBMITTING` / `UNDER-SUBMITTING` | frames against expected slots (slots / `divisor`, 5 % tolerance): MATCHED is healthy; OVER means nothing paces the loop (not synchronized, or the runtime does not block); UNDER means slots go unfilled — the game's work does not fit, look at `game` |
| `worst frame`, `worst run` | the longest interval, and the longest run of consecutive late frames (over 1.5 expected slots) with its total time — a pause the wearer feels is a run, which no mean shows |
| `(cap: every n. slot)` | `vr_frame_divisor` > 1 |

Between the lines, `[vr] frame gap: 143.2 ms between frames (mean 12.5 ms), in the game phase`
names a frame that came at least 80 ms and 2.5× the mean interval late and the phase the time sat
in (`wait`, `end` or `game`); the first 10 are logged, the rest counted (`vr.frameGaps`, and
`, N frame gaps` on the rate line). Level loads produce gaps by design; in gameplay each one is a
hitch to explain.

**The even-cadence cap** (F-26): when the native rate does not hold evenly, `vr_frame_divisor = 2`
in `cfg.ini` (`[video]`; `--vr-frame-divisor 2` on the command line overrides it) makes the game take
every second display slot: `beginFrame` sleeps past the slots to skip and lets `xrWaitFrame` snap
to the next one, so the clock stays the runtime's. The rate line must then read `2.00 slots/frame
… EVEN CADENCE | MATCHED (cap: every 2. slot)`. 1 = every slot (default), up to 8.

The verdict logic is pure (`src/vr/FramePacing.h`: timestamps in, report out) and covered by
`tests/vr/FramePacingTest.cpp` (`arxtest`, run by `build-windows.ps1` and CI): a healthy loop, the
cap, half rate without the cap (UNDER), 9 / 16 ms jitter at 80 fps (MATCHED but UNEVEN), 1 / 2 slot
alternation, an unpaced loop (OVER), a late run, the gap phases, the window boundary.

## Stereo rendering (#19)

In a frame the runtime wants rendered, the 3D scene is drawn **once per eye** into the eye
swapchains (2468×2740 on the Index) and everything 2D — menus, HUD, inventory, loading screens,
cinematics, the console — into the flat frame quad's swapchain, which the compositor blends over
the eyes by its alpha (research rows F-115, F-162, F-163; the design is D-025):

- `vr::beginFrame()` sets the frame up after `xrLocateViews`: the **eye rig** — each eye relative to
  the head between them (half the eye separation in game units, the cant as a rotation, the four
  FOV angles) — and the **union frustum** of both eyes; then the quad image becomes the render
  target, cleared to transparent.
- `ArxGame::updateActiveCamera()` prepares the head camera with the union projection and the VR
  near plane (0.09 m, F-39): rooms, portals and entities are culled once for both eyes, and the
  update projects with it at the interface's size (`g_size`, the quad's own resolution, #73).
- `ArxGame::renderStereoViews()` draws `renderSceneView()` per eye between `vr::beginEyeRender()`
  and `vr::endEyeRender()`: the eye's camera is the head camera moved to the eye and turned by its
  cant, the projection is the eye's own off-axis matrix straight from its `XrFovf`
  (`src/vr/EyeProjection.h`, pure math, host tests in `tests/vr/EyeProjectionTest.cpp`); animated
  entities are projected again for the eye (`g_reprojectEntitiesPerView`); `g_size` is the eye's
  size during the draw, so full-screen effects and sprite sizes follow the eye image.
- `vr::submitFrame()` submits the eye images as the projection layer with the frame's poses and
  the quad in front; a frame whose scene was not drawn (a menu) clears both eyes to the dark
  background — both eyes or none (F-116). The desktop mirror (#21) copies the mirrored eye in
  `endEyeRender()` and the quad before its release and draws them into the window (the quad
  blended by its alpha); with `vr_mirror = 0` the window shows the flat frame alone. Any other
  frame while the session runs (an idle headset with `shouldRender` false, eyes not located, the
  loading screen through `vr::beginInterfaceFrame()`) draws the flat frame straight into the quad's
  image, cleared to opaque black, and the window shows it scaled; such a frame is ended without
  layers when the runtime does not want it rendered.

The log says once per session what the eyes are:

```
[vr] stereo: the scene is drawn per eye - left 2468x2740 fov L -54.0 R 40.0 U 44.0 D -55.0 deg, right 2468x2740 fov L -40.0 R 54.0 U 44.0 D -55.0 deg, eye separation 64.0 mm, canted displays: 5.0 deg between the eyes, union fov L -59.0 R 59.0 U 46.3 D -57.1 deg (one cull for both eyes), near 0.09 m = 9.0 units; the 2D interface stays on the head-locked quad (1528x1146, blended by its alpha)
```

### The interface's own resolution (#73)

The 2D interface is laid out on a virtual screen, `g_size` / `g_sizeRatio` (fonts, HUD scale,
menus). In flat mode that is the window. While an OpenXR session runs, from its first located eyes
on, it is the **head-locked quad's own resolution**: the eye image's pixels per unit of tangent
(`eyeWidth / (tan right − tan left)`) times the quad's tangent width (2.4 m / 1.75 m), with the
window's aspect ratio, clamped to 640…4096 wide and to the largest swapchain
(`src/vr/InterfaceSize.h`, host tests in `tests/vr/InterfaceSizeTest.cpp`). On the Index that is
**1528 px wide** for a 4:3 window (1528×1146) at 19.4 px per degree, whatever the window's size —
before #73 the quad had the window's pixels (a 640×480 test window: a blurry quad). Size, distance
and shape of the quad are unchanged (D-030).

- `ArxGame::doFrame()` compares `vr::interfaceSize(window)` with `g_size` after `vr::beginFrame()`
  and lays the interface out again (`AdjustUI`, like a resized window) when the session starts and
  stops: `Interface laid out at 1528x1146 (was 640x480, window 640x480)`.
- The mouse is measured on the interface: `Input` maps the window's cursor to `g_size` and back
  (`setMousePosAbs`), identity in flat mode.
- The scene is still drawn at the eye size (#19): `g_size` is the eye's only inside
  `renderSceneView()` per eye.
- A frame drawn into the window while the quad has its own size (the first frame after a level
  load ended the game loop's frame) goes without the quad for that one frame.

Evidence, once per session and per change: `[vr] interface: 918x689 on the head-locked quad (2.40 m
at 1.75 m, 11.7 px per degree like the eyes) instead of the window's 640x480` (the simulator's
1440×1600 eyes), a `vr.interface` game event, and `status.json` → `vr.interface` (`width`/`height` =
`g_size`, `quadWidth`/`quadHeight`, `eyePxPerDeg`). A `shot` in a VR run saves the quad's image at
that size.

plus one `vr.stereo` game event with the same numbers, and `status.json` → `vr.stereo`
(`active`, `frames`, the sizes and angles). `OpenXR session stopped after N frames (M shown, K in
stereo)` counts the stereo frames at the end.

What stays with the head camera until #72 (and looks flat or misplaced in the headset):
particles, sparks, light flares and the mouse-drawn rune sparkles (update-phase 2D data at the
interface's size); screen-space picking with the mouse (#37 replaces it with hand reticles). The
draw check is refused in a stereo frame: its two views would be the two eyes.

**Without a headset:** the per-eye path runs unchanged on the simulated headset (`--vr-sim`,
below), and `eye-check.ps1` is its acceptance. Measured on #19 alone (2026-10-09, before #24): `world`
(both eyes `world`, the flat frame `transparent`), `pairing` 426 of 426, `separation` 64 mm,
`difference` 4.24 over a floor of 0.96, `parallax` +14 of 160 columns pass; `yaw` fails until #24
makes the camera follow the head (the pictures changed by 1.4 on a 25° turn). With `-Fault
swap-eyes` the parallax leg reads −14 columns and fails: the check sees these eyes.

## The simulated headset (`--vr-sim`, #112)

`arx.exe --vr-sim <script>` runs the game **in VR without a headset and without a runtime**: the VR
module calls OpenXR through one table of function pointers (`src/vr/XRFunctions.h`), and `--vr-sim`
puts a simulated runtime (`src/vr/VRSimulator.cpp`) behind it instead of the OpenXR loader. The game
runs its real VR code — session states, `xrWaitFrame` pacing, swapchains (real OpenGL textures),
the projection and quad layers, the desktop mirror, the controllers' actions — and the simulator
answers from a script: head pose, hand poses, action values, focus. Code added to `src/vr` later
(#19's per-eye render, #24's head tracking) runs in the simulator unchanged. It never contacts a
runtime, so it runs whether SteamVR is on or off and wins over `--no-vr` (which the launch scripts
add, D-014); a script with an error means flat mode, with the line named in the log.

```powershell
.\scripts\dev\smoke-test.ps1 -VrSim basic                                   # scripts\dev\vrsim\basic.vrsim: the defaults written out, one shot
.\scripts\dev\smoke-test.ps1 -VrSim head-motion                             # head offsets (10 cm right, 30 cm crouch) and a turn ramp, shots
.\scripts\dev\smoke-test.ps1 -VrSim basic -Commands 'vr-sim head rot 30 0 0','vr-sim @+30 shot turned'
.\scripts\dev\smoke-test.ps1 -VrSim startup -StartTimeoutSec 9 -- --loadslot 0   # shots from the session start: menus, a level that never loads
.\scripts\dev\eye-check.ps1                                                 # the two-eyes check, below
```

**The script** (`scripts\dev\vrsim\*.vrsim`; the language is in `src/vr/SimRig.h`): set-up lines run
at once, `@<frame> <command>` at that frame of the timeline, `@<a>-<b> <command>` moves the numbers
there linearly (a ramp), `@+<n>` counts from the previous timed line. Frame 0 of the timeline is the
first frame the game is playable (`timeline gameplay`, the default) or of the session (`timeline
session`). Commands: `rate <hz>` (0 = no pacing), `eye-size <w> <h>`, `ipd <m>`, `fov left|right <l>
<r> <u> <d>` (degrees, OpenXR signs), `head pos <x> <y> <z>` (meters, the room's LOCAL space: x right,
y up, −z forward), `head rot <yaw> <pitch> <roll>` (degrees: yaw + turns left, pitch + looks up),
`hand left|right pos|rot …`, `hand left|right on|off`, `profile index|touch|simple|none|<path>`,
`action <name> left|right|both <value> [<y>]` (the game's OpenXR action names: `trigger`, `grip`,
`button_a`, `button_b`, `thumbstick`, `thumbstick_click`, `trackpad_press`, `menu`), `session
focused|visible`, `shot [name]`, `mark <text>`, `quit`, `fault swap-eyes on|off`. The defaults: Index
panel size 1440 × 1600, IPD 64 mm, 90 Hz, the asymmetric, mirrored field of view of research row
F-165, both Index controllers in front of the body.

**The compositor** counts, per submitted frame, whether both eye images were released in the same
frame (`vr.sim.compositor`: `projectionFrames`, `pairedFrames`, `staleFrames`, `freshFrames`,
`quadFrames`). A **shot** composes what each eye sees — the projection layer with the head-locked
quad on top, ray by ray as a compositor would — and saves `<test dir>\capture\<name>-left.bmp`,
`-right.bmp` and `-quad.bmp` (the flat frame). Each gets pixel stats and a **verdict** for the region
around where the eye looks (`scene`; `layer` is the projection layer alone; `quad.stats` the middle of
the flat frame, away from the HUD): `world` (something with detail), `black` (≥ 90 % of the pixels at
luma ≤ 4), `background` (≥ 90 % the eyes' clear color: the eye got no picture), `uniform` (one flat
color), `transparent` (a quad the game submits blended by its alpha — the 2D interface over the
stereo world, #19 — with under 10 % coverage in its middle; the compositor blends such a quad by
its alpha plane, premultiplied, like the runtime does, so the eyes show through where nothing is
drawn). `lr` has the left/right difference (mean luma levels on a 160-column grid), the horizontal
shift of the left picture against the right one (> 0 is right) and the eye separation; `versus` the
difference of each eye to every earlier shot. `vr.sim` also has the rig, the eye poses as last
located (`eyes`, with `separation`) and the timeline's state; a `vr.sim.shot` game event per shot.
A shot stalls its frame for about 100 ms (read-back, composition, three BMPs at eye size): the rate
line of a run with shots shows those frames as gaps — judge pacing from a run without shots.

**The black world of #215 is visible here:** a level that did not load (`--loadslot 0` without a
save, before #24's fix) shows the HUD on black — verdict `black` (97 % black in the middle), where
`--loadlevel 10` gives `world`. A shot in the first second after a level load is black too: the level
fades in (TRAPS.md); the scripts shoot 3 s in (`@270`).

### The two-eyes check (`eye-check.ps1`, F-06)

One `--vr-sim` launch with `scripts\dev\vrsim\eye-check.vrsim` (two shots of one frame with the head still, one
after a 25° yaw), judged by `scripts\dev\eye-check-lib.ps1` (self-test `test-eye-check.ps1`):

| Leg | Passes when |
|---|---|
| `world` | both eyes' verdict is `world` in the still shot |
| `pairing` | every frame showed both eye images from one frame, no stale eye |
| `separation` | the eyes are the IPD apart (± 0.5 mm) |
| `difference` | left vs right > 3 × the same-eye floor (two shots of one eye taken in the same frame: no game time passes between them, so slow frames cannot raise it, #232) and > 0.5, < 60 luma levels |
| `parallax` | the left picture is shifted right of the right one |
| `yaw` | both eyes turned 25° (± 0.5°) and both pictures changed above the floor |

It is the acceptance check of the per-eye render (#19) and the per-eye re-projection (#72). Measured
on `main` with the flat frame on the head-locked quad (2026-10-09): world, pairing (410 / 410),
separation (64 mm), difference (5.9 vs floor 0.8), parallax (+16 of 160 columns) pass; **yaw fails**
(the pictures change by 1.3, the floor's noise) — the camera does not follow the head until #24.
`-Fault swap-eyes` (each eye shown the other's picture) fails the parallax (−16): the check can fail.

## Scripted sequences (`.xrs`, #113)

A sequence is a text file in `scripts\dev\xrs` — test-channel commands with asserts, log checks and
shots — that runs against one launch of the game and fails the run at its first failed step. It is
how a worker proves a VR behaviour (look around, snap turn, walk, recenter) repeatably instead of by
eye. Adapted from the harness of Dishonored VR (`xrsim-run.ps1`, `img-diff.ps1`, MIT, `LICENSE.xrsim`).

```powershell
.\scripts\dev\xrs-run.ps1 look-around          # launches the game (idle simulated headset, --loadlevel 10), runs it, the usual smoke checks
.\scripts\dev\xrs-run.ps1 -Check               # parse every sequence: syntax errors with their lines, no game
.\scripts\dev\xrs-run.ps1 snap-turn -- --loadslot 0   # other arx.exe arguments replace the sequence's @args
.\scripts\dev\xrs-run.ps1 recenter -Attach     # against a game that already runs with --test-dir (writing a sequence step by step)
.\scripts\dev\smoke-test.ps1 -VrSim idle -Sequence walk-forward   # the same through the smoke test
.\scripts\dev\img-diff.ps1 -A a.bmp -B b.bmp [-Grid 16 | -Bands 16]   # two shots: mean difference 0-255, which regions changed
```

| Line | Meaning |
|---|---|
| `# ...` | comment (also after a step, as ` # ...`) |
| `<command>[; <command>]` | any [channel command](#commands) (`vr-sim head rot 25 0 0`, `vr-input tap right.a`, `loadlevel 10`, a script-console line); `;` makes one batch, applied in the same frame. A refused command fails the step |
| `@wait <ms>` / `@frames <n>` | sleep / wait for n more rendered frames |
| `@until <path> <op> <v> [<n>s]` | wait up to n s (default 30) until `status.json` satisfies the condition |
| `@assert <path> <op> <v>` | `status.json` now. `path` is dotted, `"quoted"` where a key holds a dot (`events.byName."spell.cast"`); `op` is `eq ne gt ge lt le`; numbers compare as numbers, anything else as text |
| `@remember <name> <path>` | keep a value; later `@assert` / `@until` values can be `{name}` or `{name+5}` (the yaw before a turn, the position before a walk) |
| `@mark`, `@log <regex> [<n>s]`, `@nolog <regex>` | `arx.log` since the mark (the sequence start is the first mark): wait up to n s (default 5) for a matching line — a match moves the mark past it, so consecutive `@log` lines assert an order — / fail on a matching line |
| `@shot <name>` | on the simulated headset both eyes and the flat frame (`capture\<name>-left.bmp`, `-right.bmp`, `-quad.bmp`), otherwise `capture\<name>.bmp` |
| `@diff <a> <b> <op> <v> [left\|right\|quad]` | `img-diff` of two shots: the mean absolute per-channel difference. **The floor of a still shot of level 10 is about 1.8** (animated fire and HUD, measured), a 40° head turn with a following camera is far above; set thresholds against a still pair taken in the same sequence |
| `@key <name> [n] [ms]` | a keyboard message posted to the game window, no focus change (Escape, Enter, Space, A-Z, 0-9, F1-F12 …). **Not yet run against a live game** — verify it with a first sequence that needs it |
| `@pending #<issue> <step>` | a step the game cannot pass yet, because issue N builds the feature. Its failure is printed as `pending`; **once it passes the run fails** until the `@pending` is removed, so an expectation cannot go stale. Pick a step that fails for the right reason (a first version of `snap-turn` had a "turned back" step that passed trivially) |
| `@sim <name>`, `@args <...>` | what `xrs-run.ps1` launches: the `scripts\dev\vrsim` script (default `idle`: no timed lines, no shots) and the arx.exe arguments (default `--loadlevel 10`); ignored while running |

Rules: a sequence waits for `state eq GAMEPLAY` and for `vr.sim.timelineFrame ge 270` (3 s) before its
first shot — the level fades in from black (TRAPS.md). A timeout must carry its `s` (`60s`); a bare
number is rejected, not taken for part of the value. Shots stall their frame, so the pacing line of
a run with shots reads UNDER-SUBMITTING — judge pacing without them. The shipped sequences:

| Sequence | Real today | `@pending` until |
|---|---|---|
| `look-around` | the rig and both located eyes follow a 40° yaw and a pitch; two shots of a still head agree | #24: the pictures change with the head |
| `snap-turn` | `vr-input tap right.stick_right` reaches the game (syncs advance), no errors | #32: the player turns |
| `walk-forward` | the player stands still without input; the stick is held and let go | #31: the player moves |
| `recenter` | a head offset is set and read back | #28: `recenter` is accepted (it logs one expected `[W]`) |
| `real-steps` (#26; run with `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence real-steps`) | 8 cm is a lean (the body stays); 1 m forward is a real step (the body walks 0.9 m, the lean stays 0.1 m, no fade); into the cell wall the body is held, the head goes `inside` then `behind` it, both eyes black; back out, the fade clears | — |

A new feature's task un-pends its lines (and adds its own sequence). The runner is checked by
`scripts\dev\test-xrs.ps1` (a fake game answering the channel files; in `run-script-tests.ps1`, so in CI).
## `xr_hello` — the runtime alone

`xr_hello.exe` is built next to `arx.exe` (CMake option `BUILD_XR_HELLO`, on by default with OpenXR;
adapted from Dishonored VR's `xr_hello32`, MIT, see `LICENSE.xr_hello`). It is a 64-bit OpenGL
OpenXR client with nothing of the game in it: active runtime manifest, extensions, instance, system,
view sizes, a hidden WGL context, the runtime's OpenGL requirements, a session, N empty frames
(`--frames`, default 60), an orderly exit; every session state with a timestamp.

| Exit | Meaning |
|---|---|
| 0 | the session ran N frames — the runtime and the graphics binding are fine; if the game fails, the game is the suspect |
| 1 | loader, runtime or API failure (no runtime, no OpenGL extension, `xrCreateSession` failed, …) |
| 2 | partial: no headset (`XR_ERROR_FORM_FACTOR_UNAVAILABLE`), SteamVR not running (it refuses to start it, like the game — `--start-runtime` overrides), the session never became `READY`, or `--require-focused` and no `FOCUSED` |

## What was measured (owner's PC, Index, SteamVR 2.17.10, 2026-10-08)

| Scenario | Result |
|---|---|
| `main` before this task: `--no-vr --loadlevel 10`, close the window after 30 s | **1 of 5 clean**; 4 crashed on exit in `Entity::cleanReferences` — bug #93, because `--loadlevel 10` left the intro cinematic *loaded* (the level 10 script preloads it; it never played — see the 2026-10-09 rows). Not a flaky script: a real bug on a path every dev launch took |
| Dev launches skip the intro cinematic (this task): `smoke-test.ps1 -Repeat 10 -Seconds 5`, quit through the channel | **10 of 10** `PASS`; `LOADING` → `GAMEPLAY` 1.1 s after launch, 120 fps windowed, 0 errors, 0 warnings |
| The same, closed with WM_CLOSE (`-CloseWindow -Repeat 5`) | **5 of 5** `PASS` |
| A refused command (`bogus_command 1`) | the run is `FAIL` with the ack as the reason — the gate can fail |
| `xr_hello --frames 120` with SteamVR running and the headset idle on the desk | exit 0: `IDLE → READY` at once, `SYNCHRONIZED` after 0.13 s, 120 frames in 1.48 s (display period 12.5 ms = 80 Hz), orderly `STOPPING → IDLE → EXITING`; **never `VISIBLE` or `FOCUSED`** |
| `smoke-test.ps1 -Vr` with SteamVR running and the headset idle | `PASS`: instance, system and session created, state `ready` (the game did not begin the session before #18), clean shutdown with the session destroyed before the window |
| `smoke-test.ps1 -Vr`, headset idle, after #18 (2026-10-09) | `PASS`: eye swapchains 2468x2740 `GL_SRGB8_ALPHA8` with complete framebuffers, `ready → running → synchronized`, quad swapchain 640x480, display period 12.5 ms (80 Hz), **834 frames submitted in ~10 s, 1 shown** (`shouldRender` only on the first frame with an idle headset — see TRAPS.md), 0 `[E]`, clean shutdown with the session destroyed while running (no `STOPPING` before our quit) |
| `smoke-test.ps1 -Menu -Commands 'loadslot 0'` (plain launch, then a save through the channel) | `MENU` 0.8 s after launch; `loadslot 0` refused with `no save in slot 0 (have 0)` — the owner's machine has no saves yet (#70), so the success path of `loadslot` is **not verified** here; it is the same `LOADQUEST_SLOT` path as `--loadslot` *(verified in #70: see the rows below)* |
| 2026-10-09, #124: `--loadlevel 10` with the intro `.cin` **loaded** (preloaded by the level 10 script, never played), `-CloseWindow -Repeat 5` | **2 of 5 crashed** in `Entity::cleanReferences` (#93); the channel `quit` was 5 of 5 clean on the same build. A loaded cinematic is enough for #93, so the development skip never loads the file |
| 2026-10-09, #124: cinematics skipped without loading — default `-CloseWindow -Repeat 3`; `-Commands 'cine play' -CloseWindow -Repeat 2`; `-Commands 'cine play','cine akbaa_dream_a' -Repeat 2` | **3 of 3, 2 of 2, 2 of 2** `PASS`; one `Skipping cinematic <name>` line per cinematic, state never `CINEMATIC`, 0 errors. With `--cinematics` (or `cine play` with `--intro`) the state becomes `CINEMATIC` — the check can fail |
| 2026-10-09, #71: `-Commands 'drawcheck 120'` on level 10 **before** the simulate / draw split (`renderLevel()` still aged particles, flares, decals and spells while drawing) | **23 of 120 frames differ** between the two draws (max 3558 of 307200 pixels, max delta 59) — the torches' fire particles advance once per draw |
| 2026-10-09, #71: the same after the split | `PASS`, **0 of 120 frames differ** (max 0 pixels, delta 0), 0 errors, 0 warnings |
| 2026-10-09, #71: 26 spells cast on the player (`spellcast -fs 5 <spell> self`, the list in [The draw check](#the-draw-check-drawcheck)), then `drawcheck 240` | `PASS`, **0 of 240 frames differ**, every `spellcast` acknowledged with `errors=0`, 0 errors, 0 warnings. **No spell was active:** with `-fs` a spell on the player is queued and never launched (TRAPS.md), so this row covers the level without spells — superseded by the #155 rows below |
| 2026-10-09, #71: **the check can fail** — a build with `ARX_PARTICLES_Update()` deliberately moved back into `renderSceneView()`, `drawcheck 120` on level 10 | `FAIL`, **35 of 120 frames differ** (max 10 of 307200 pixels, max delta 97), the most differing pair saved as `capture\drawcheck1_a.bmp` / `_b.bmp`; the other six checks stay `[ok]` — only the draw check catches this class of bug |
| 2026-10-09, #71: the committed code — `shot level10`, the 26 spells plus `negate_magic`, `drawcheck 120` | `PASS`, **0 of 120 frames differ**, 0 errors, 0 warnings. The `shot` at frame 15 was **all black**: the level's fade-in, not the renderer (TRAPS.md). The spells were cast with `-fs` like the row above: none launched |
| 2026-10-09, #71: 20 × `status` (≈ 5 s), `shot level10-after-fade`, `drawcheck 120` | `PASS`, **0 of 120 frames differ**; the shot at frame 517 shows the scene (58 % of sampled pixels non-black, the same as the draw-check captures from frame 86 on) |
| 2026-10-09, #93 before the fix: a detector in `~Entity` (log every entity whose `owner()` is the one being deleted) | **1 of 1** exits from level 10 report `human_base_0033` deleted while it still owns `long_sword_0021` — the dangling owner is there on every exit, the crash only depends on the heap |
| 2026-10-09, #93 after the fix: `-Repeat 10 -Seconds 3` (channel quit) | **10 of 10** `PASS`, 0 errors, 0 warnings; the detector reports nothing |
| 2026-10-09, #93 after the fix: `-Repeat 10 -Seconds 3 -CloseWindow -- --no-vr --loadlevel 10 --intro` (intro `.cin` loaded, window closed — 2 of 5 crashed in #124) | **10 of 10** `PASS` |
| 2026-10-09, #93 after the fix: `-Vr -Repeat 10` + `-Vr -Repeat 5`, SteamVR running, headset idle | **13 of 15** `PASS`, **0** exit crashes (before: 2 of 3); 2 runs ended mid-run with exit code -1 and no crash report — a different problem, #146 |
| 2026-10-09, #76: `comfort-check.ps1` — one flat run, `comfort off` then `comfort on`, in each phase `quake 300 3000 400` and Magic Sight, status sampled every 100 ms (`camera.shake`, `camera.roll`, `camera.fov`, `camera.bob`) | smoke test `PASS`, 0 errors. **off** (18 samples): shake max **2.622**, roll max 0.428°, fov **75.00 → 80.65**, bob max **1.472** (the idle animation moves the head too). **on** (27 samples): shake **0**, roll **0**, fov **75.00** in every sample, bob **0**. Log: one `[vr] comfort:` line per policy change, one `screen shake suppressed - intensity 300, 3000 ms, period 400` and one `FOV zoom suppressed - focal -30 (Magic Sight)` per episode, nothing per frame. The `off` phase is the broken state on purpose: the instrument sees each of the three motions, so the check can fail |
| 2026-10-09, #76: the first version of the same check, Magic Sight cast with `spellcast -fsx 5 magic_sight self` | `FAIL` of the check only (the smoke test passed): fov **75.00 in both phases** — the spell never launched. On the player `spellcast` without `-m` is queued for the rune drawing, which skips the player (TRAPS.md); `-fsmx` zooms (row above). The same `-fs` cast the #71 spell rows above — #155 re-runs them |
| 2026-10-09, #24: `head-check.ps1` — one flat run on `mvp-new-game`, phases `comfort on` (flat), `headpose 0 0 0 0 1.7 0`, `headpose 30 -20 10 0 1.7 0`, `headpose 0 0 0 0.3 1.5 0`, `headpose 0 0 0 1.0 1.7 0`, `recenter`, `headpose off`, status sampled every 100 ms | `PASS`, 25 samples, smoke test 0 errors. Body yaw 359.75 throughout. **flat**: source `none`, camera yaw 359.75 = body. **ahead**: source `sim`, 1 reference, camera = body, offset 0. **turn**: camera yaw **29.75** (+30), pitch **20.00**, roll **350.00**, position unchanged. **move**: offset **(30.00, 20.00, −0.13)** = 30 cm right and 20 cm down turned by the body's yaw, and the camera moved by it. **lean** (1 m): **clamped**, horizontal offset **60.00**. **recenter**: 2 references, offset 0. **off**: source `none`, camera = body again. Six `vr.head*` events, nothing per frame. The shots `head-ahead` / `head-turn` show the cell door moving right and the floor coming into view. The plain smoke test (`--loadlevel 10`) logs no `vr.head` event at all: flat play is untouched. The host tests (`HeadPoseTest`, 10 cases) fail 5 of 10 with the yaw turn's sign flipped on purpose. After the VR-only FOV of the quad was added, `comfort-check.ps1` still `PASS` with fov **75.00** in flat mode (46 samples): the quad FOV needs a session. The quad FOV itself (a VR session) was not exercised: the owner's headset tests held SteamVR, workers ran flat only |
| 2026-10-09, #24: the owner's headset run of #215 (`test-pr.ps1 215`, shared user dir, `--skiplogo --loadslot 0`) showed a **black world with only the HUD** | Not the camera: the shared save folder was empty, so no level was loaded — `game.state` stayed `STARTUP` for the whole run, no `level.loaded`, rate line `game 0.1 mean` ms (the #21 run before it: the same). After the fix, flat `smoke-test.ps1 -- --skiplogo --loadslot 5` with one save: state **`MENU`** at 0.8 s (before: an empty level), one `[E] --loadslot 5: no such save, 1 save(s) in the save game dir - starting at the main menu instead of an empty level` (the smoke test fails on it, as it should). `test-save-library.ps1`: `Get-ArxLoadSlotProblem` refuses `--loadslot 0` with no saves and `--loadslot 1` with one, accepts `--loadslot 0` with one (6 new checks) |
| 2026-10-09, #24 on the simulated headset (#112): `eye-check.ps1` (level 10), then `smoke-test.ps1 -VrSim head-motion`, then `head-check.ps1` again | **Before** (the head applied in `updateFirstPersonCamera`): eye check `FAIL (yaw)` — both eyes turned 25°, picture changed **1.34** (floor 0.81, needs > 2.42); the `yaw25` flat frame identical to `still-a`: level 10's intro conversation camera overwrote the head (TRAPS.md). **After** (the head applied in `updateActiveCamera` to the camera the frame renders with, taken off again next frame): eye check **`PASS`** — world, pairing 428/428, separation 64 mm, difference 5.887 vs floor 0.844, parallax 16 columns, **yaw: picture changed 4.246 / 4.245**. `head-motion`: shots `center`, `right10cm`, `crouch30cm`, `turned` all **left world, right world, flat frame world**; the `turned` frame faces the right wall and more floor, `crouch30cm` sits lower. `head-check.ps1` still `PASS` with the same numbers |
| 2026-10-09, #25: `body-check.ps1` — one run on the simulated headset (`-VrSim basic`, `mvp-new-game`), the head turned through the channel (`vr-sim head rot 0 / 40 / -40`), then `setpos` here with heading + 90°, then `recenter`; status sampled every 50 ms | `PASS`, 49 samples, smoke test 0 errors. **ahead**: offset 0, model = heading 359.75. **left** (+40°): `vr.body.drag start head=40.0 body=15.0` in that frame, then the body settles on the head — offset 38.76 at 1.47 s, 39.95 at 3 s; look = camera yaw = 39.75 throughout; camera position unchanged. **right** (−40°): dragged to −15.0, settled at −39.98. **heading** 89.75: offset kept (−39.99), body = model = 49.76. **recenter**: offset 0, body = model = heading. Every frame: look = heading + head, the head ≤ 25° from the body. Proven to fail: with the eye compensation switched off the camera moved **1.11** units at ±40° → `FAIL`. `head-check.ps1` still `PASS` (same numbers as #24), `eye-check.ps1` `PASS` on the second run (the first failed only `difference` on a loaded PC, #232), flat smoke test `PASS`. `BodyYawTest` 8 cases in `arxtest` |
| 2026-10-10, #26: `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence real-steps` (85 steps), then `head-check.ps1`, `body-check.ps1`, `eye-check.ps1`, the flat smoke test | **real-steps `PASS` 85/85.** Head 8 cm forward: lean 0.080, no step. 1 m forward in quarter metres: `player.z` 8460 → **8550.0** (the body walked 0.90 m), lean **0.100**, `fade` 0, shot changed 13.3. Back: the body walks back (z 8470). Right into the cell wall: the body walks 2.44 m and is held at x **8783.9** (`vr.body.blocked start askedCm=4.6 movedCm=0.0`); head 3.0 m: lean **0.577** (< 0.7, `farFade` 0), `wall=inside`, `fade` **1**, both eyes **black** in the shot; head 3.5 m: `wall=behind`, the body stays at 8783.9; head back: `wall` empty, `fade` 0, `bodyBlocked` false, both eyes world again. Pacing MATCHED / EVEN 90.0/s. **The check can fail:** the first run used the engine's `CheckBackgroundInSphere` for the near plane and failed `walls ge 1` with the head 5 cm into the wall face (TRAPS.md); the exact triangle distance passes. `head-check` `PASS` with the new expectations: offset (10, 20, -0.04) = the 10 cm radius, camera moved 30 / 100 units, the body 20 / 90 units (and 12-16 units up: the cell floor is uneven, the same height the stick walk reaches). `body-check` `PASS` (unchanged numbers), `eye-check` `PASS` (yaw 5.80 / 6.11), flat smoke `PASS` (`--loadlevel 10`: 7 game events, 0 errors, 0 warnings). `HeadLeanTest` 9 cases; with the follow radius ignored on purpose 4 of them fail |
| 2026-10-09, #115 log banner: flat smoke test, then `status.json` and the first log lines asserted by script | `PASS`; `build.commit` = `git rev-parse HEAD` of the worktree **after a commit and a rebuild** (before #115 a worktree kept its first build's commit), `build.branch`, `build.config` = `Release`, `vr.manifest` = the SteamVR manifest from `HKLM`, `vr.runtime` = `""` in a flat run; the four banner lines match their documented shape. `XR_RUNTIME_JSON` → a missing file (flat, `--no-vr`): the manifest line says `from XR_RUNTIME_JSON, overriding the registry's "…steamxr_win64.json"`, exactly **1** `[W] … does not exist`, `vr.manifest` = that path, `PASS`. `built-from.txt` edited to another commit: exactly **1** `[W] built-from.txt names commit …`, `PASS`. The `OpenXR runtime answered` line needs an instance (SteamVR running): not exercised in that session, it is the renamed `Using OpenXR runtime` line |
| 2026-10-09, #129 **before** (the `main` build): `run-windows.ps1 -Isolated -Background -- --no-vr --loadlevel 10`, no channel, `Stop-Process` long after the level had loaded | `arx.log` = **4137 bytes, 42 lines, cut mid-line** (`[I] Localisation` and nothing after it): the `ofstream` buffer; the end of the level load lost, no `Clean shutdown` |
| 2026-10-09, #129 **after**: the same, killed 15.5 s after the start | `arx.log` = **4787 bytes, 49 complete lines**, last line `[I] LoadLevel:511  Done loading level` (the last line the game wrote), ends with a newline; the previous run's truncated log sits in `arx.1.log`; banner `Log: "…\arx.log", previous runs kept as arx.1.log .. arx.9.log` |
| 2026-10-09, #129: `smoke-test.ps1 -Seconds 5 -Commands 'mark flush-check'`, then `-Seconds 3` on the final build | `PASS` both, 0 errors, 0 warnings; after the second run the history reads `arx.log` (pid 35468) ← `arx.1.log` ← `arx.2.log` (the killed `main` run), each `Run:` line one run older — the smoke test no longer deletes `arx.log` before a run |
| 2026-10-09, #129, **the degraded path**: `arx.1.log` held open by PowerShell (`FileShare::ReadWrite`, no delete sharing) during `smoke-test.ps1 -Seconds 3` | `PASS` with exactly **1** warning: `[W] Startup:211  Log rotation failed: could not move "…\arx.1.log" to "…\arx.2.log" - nothing was deleted, this run appends to the previous log`; `Log: …, appended to the previous run's log`; `arx.log` grew from 5114 to 10583 bytes with two `Starting` lines, `arx.1.log` untouched. `fs::rename` retries 10 × 100 ms first (its `[W] Failed to move file … 32 = … being used by another process` lines go to stderr — the file logger does not exist yet), so `STARTUP` came at +1.0 s instead of +0.3 s |
| 2026-10-09, #129, **the test can fail**: `FileLoggerTest` built with both flushes removed on purpose | `Run: 39  Failures: 2`: `testWarningIsOnDiskAtOnce` and `testInfoIsOnDiskWithinTheInterval` (after its 2 s wait); `testExplicitFlush` and the three rotation tests stay green, as they should. Restored: `OK (39 tests)` |
| 2026-10-09, #155: `spell-drawcheck.ps1` — the 26 spells of #71 **really launched** (23 by the player with `-fsm`, `summon_creature` / `raise_dead` / `fake_summon` by `human_base_0033` on the player), then `drawcheck 240` on level 10 | `PASS`: **26 of 26** in `spells.launchedByType`, each in the `status.json` write right after its own command; **24 spell types active during the check** (all but `harm` and `mana_drain`, which had ended), **0 of 240 frames differ** (max 0 of 307200 pixels, delta 0), 0 errors, 0 warnings. The #71 split holds with every spell effect on screen — no bug to file against it |
| 2026-10-09, #155: the same with `-Flags fs` — the cast of the #71 rows, the broken state on purpose | `FAIL` of the check (the smoke test passed, 0 of 240 frames differ): **0 of 23** player spells launched; the NPC's 3 launched one command later through the NPC rune drawing. The launch evidence catches the #71 mistake |
| 2026-10-09, #30: `input-check.ps1` — one flat run on level 10, 12 `vr-input` commands: a tap of `right.a`; `right.grip` held + a tap of `right.trigger`; a tap of `right.grip`; `bind jump left.b+left.a`, then `left.b` held + a tap of `left.a`; a tap of `left.b`; a tap of `right.stick_down`; `reset` | `PASS`: presses `jump` **2**, `stash_right` **1**, `use_right` **1**, `book` **1**, `crouch_toggle` **1**, every other event **0** (the chord neither clicked nor used, the chord's leader tapped on its release, the remapped chord did not open the book); `layout.jump` = `right.a` after `reset`, nothing held at the end; smoke test `PASS`, 0 errors. One `[vr] input:` line per press, none per frame |
| 2026-10-09, #30: the same with `-Unbind` (`bind jump none` first) — the broken state on purpose | `FAIL` of the check (the smoke test passed): `jump fired 1 times, expected 2`. The host tests of the table (`VRControlsTest`, 13 tests) failed **4 of 13** with the deferral of a chord leader's own binding removed on purpose; restored, `OK (59 tests)` |
| 2026-10-09, #74: `game-input-check.ps1` — one flat run on level 10: `setplayercontrols on`, `cinemascope off`, then `vr-input` taps of `right.b` ×2, `left.b` ×2, `right.stick_down` ×2, `right.a`, `stick left 0 1`, `left.a` ×2, two `mark`s after each | `PASS`: after the first `[vr] input:` line of each button the game logged **inventory `closed -> open`** (the level start shows the bag), **book `open -> closed`**, **crouch `on -> off`**, a `player.jump`, and **`game.state` `MENU -> GAMEPLAY`** (the game menu and back, by the left A); presses `inventory 2, book 2, crouch_toggle 2, jump 1, menu 2`, every other event **0** (no click, use or stash from these buttons); `vr.input.game` move **(0, 1)** with the left stick forward, nothing held at the end; smoke test `PASS`, 0 errors. `input-check.ps1` of #30 still `PASS` |
| 2026-10-09, #74: the same with `-Unbind` (`bind inventory none` first) — the broken state on purpose | `FAIL` of the check: `inventory: expected two opposite state changes after its presses, got ''; inventory fired 0 times, expected 2`. The host tests (`VRGameInputTest`, 8 tests) with the stealth kept for the release and the two-update Escape removed on purpose: **2 of 8** fail (`stashTest`, `menuTest`); restored, `OK (88 tests)` — `OK (102 tests)` after merging `main` |
| 2026-10-09, #74 after #112: `smoke-test.ps1 -VrSim basic` — the real controller path on the simulated headset (Index profile, focused): `vr-sim action button_b right 1` / `@+5 … 0`, then the same for `button_b left`, `button_a right`, `thumbstick right 0 -1`, `trigger right`, and `thumbstick left 0 1` held, two `mark`s after each | `PASS`: `vr.input.controllers=index`, **578 action syncs**, presses `inventory 1, book 1, jump 1, crouch_toggle 1, interact_right 1`, every other event **0**; the game logged the inventory closing, the book opening, `player.jump`, `player.crouch state=on` (`-ExpectEvents` ok); `vr.input.game` move **(0, 1)**, nothing held at the end; 585 VR frames, both eyes paired, 0 errors. Flat `game-input-check.ps1` on the same merge: `PASS`; host tests `OK (135 tests)` |
| 2026-10-09, #155: the player's own `summon_creature` and `raise_dead` (`-fsm`): at the start point; after `teleport -ap 0/90/180/270 player`; after `teleport -p human_base_0033` | **never launched** — `CanLaunch` finds no valid spot 3 m ahead on level 10, a silent fizzle; hence the NPC caster above |
| 2026-10-09, #20: `FramePacingTest` (11 cases over synthetic frame timings, see [The frame loop and the rate line](#the-frame-loop-and-the-rate-line-20)) | `OK (57 tests)`. **The suite can fail:** with `UnderSubmitFactor` set to 0 and `EvenMaxSdSlots` to 10 on purpose, `Run: 57 Failures: 3` — exactly the UNDER case and the two UNEVEN cases (half rate without the cap, 9 / 16 ms jitter, 1 / 2 slot alternation); the other 8 stay green. Restored: `OK (57 tests)` |
| 2026-10-09, #20: flat smoke test, `-Seconds 8` with 6 `status` + `mark`, channel quit; then `-Seconds 5 -CloseWindow` | **2 of 2** `PASS`, 0 errors (1 warning in the first: no `cfg.ini` yet in the fresh isolated directory). Flat mode untouched: 1175 frames in 10.6 s (the engine's limiter and vsync as before), `vr.enabled=false`, `vr.frameDivisor=1`, no `vr.pacing`, no `[vr] pacing:` line — the VR clock never engages without a session |
| 2026-10-09, #20: `smoke-test.ps1 -Vr` | **SKIPPED** (exit 2): SteamVR was off the whole session and a worker never starts it (D-014). The VR frame loop (xrWaitFrame at the frame start, the rate line, the orderly exit) is **not measured** in this task — the owner's headset run and the next worker's `-Vr` self-test with SteamVR running are the evidence; the expected lines are in the section above |
| 2026-10-09, #70: `make-test-saves.ps1` on `main` + the task — `newgame` from the main menu, wait for `player.controlsBlocked False`, `inventory playeraddfromscene short_sword_0030`, `short_sword_0030.equip player`, `setpos` + `lookat` + `save` for `mvp-items` and `mvp-goblin-fight`, then every save loaded again in the same game | `PASS`: `GAMEPLAY` 12 s after the launch, **controls free after 30-42 s** (3 runs), 3 saves of **0.3 MB** each, `short_sword_0030` `show=6` (equipped), each reload ended `gameplay in level 1`, the player within 100 units of the saved position, `controlsBlocked` false; 0 errors, clean shutdown |
| 2026-10-09, #70: `make-test-saves.ps1 -Verify` — a fresh game with `--loadslot 0`, then `loadslot 1` and `loadslot 2` through the channel | `PASS` (3 of 3): slot 0 = `mvp-goblin-fight` (the newest), slot 1 = `mvp-items`, slot 2 = `mvp-new-game` — the slot order is the file time, newest first; the first state `GAMEPLAY` 25 s after the launch, 0 errors, clean shutdown |
| 2026-10-09, #70: `equip short_sword_0030` as a console line (a player-context command) | acked `errors=0`, nothing equipped (`show=2`): `equip <target>` is the **item's** command (`ARX_EQUIPMENT_Equip(target, context entity)`); `short_sword_0030.equip player` equips (`show=6`) |
| 2026-10-09, #70: a `status.json` field `hover` (the entity under the reticle) read at 160 positions on rings of 150 / 250 units around 5 levers, 2 chests, 2 portcullises and a key, then at the 20 level markers within 700 units of those (twice) — `hover` = `FlyingOverIO`, then `FlyingOverObject(screen centre)` | **0 hits** for a lever, chest, portcullis or the key in all three passes (3 hits for `note_0004` on the rings, with `FlyingOverIO`); `FlyingOverIO` is cleared in mouse look, and the pick at the centre found nothing either; the mechanism was dropped, see TRAPS.md |

Conclusions:

- **Reliable, used as a gate:** the flat smoke test (start, level, channel commands, quit, evidence),
  the draw check (#71) and the comfort check (#76) on top of it.
- **Reliable for what it covers:** the VR launch with an idle headset proves instance / session
  creation, the OpenGL binding and the shutdown order. With nobody wearing the headset SteamVR runs
  the frame loop in `SYNCHRONIZED` but never grants `VISIBLE`/`FOCUSED`, so anything that needs
  focus (input actions, "is the picture right") cannot be judged this way: that is the simulated
  backend's job (#112, #113) or the owner's. The SteamVR null driver (F-03) is **not needed**: an
  idle Index already runs frames; the driver would not reach `FOCUSED` either without input.
- **Manual, not automated:** anything visual in VR, performance at the headset's rate, and the
  `--intro` / `--cinematics` paths (no longer exposed to the exit crash #93, fixed on 2026-10-09).

## Writing a channel-driven check

1. Start with the state you need: `--loadlevel N`, `--loadslot N` (saves from #70) or a plain launch.
2. Put the game where the feature is: `teleport -p <marker>`, `loadlevel`, `spawn`.
3. `mark <name>` before the action, do it, then assert on numbers (`status.json`, the ack) or on
   `shot` captures (`capture\*.bmp`, compared by #113's `img-diff`). Not in the first second after
   `GAMEPLAY`: the level fades in from black and an early `shot` is all black (TRAPS.md).
4. Prove the check can fail once (break the thing on purpose) before trusting it.
5. Quote the verdict lines in the PR's *How verified*.

Scripts share `scripts/dev/test-lib.ps1` (`Get-ArxTestDir`, `Get-ArxUserDir`, `Read-ArxStatus`,
`Wait-ArxStatus`, `Send-ArxCommand`); the channel directory is `$env:ARXVR_TEST_DIR` or
`%LOCALAPPDATA%\ArxFatalisVR\test\<key>` of the working copy the scripts belong to.

## The CI gate (#127)

Every pull request that touches more than docs runs `.github/workflows/build-windows.yml` on the
self-hosted runner (D-021: CI is the automatic gate; a red step blocks the merge). The steps, fast ones first:

| Step | What | Fails when |
|---|---|---|
| Script self-tests | `scripts/dev/run-script-tests.ps1`: every `scripts/dev/test-*.ps1` in its own pwsh (`test-log-digest.ps1` incl. the `-Id` / `-Find` / `-Condense` / `-Print` modes on a fixture log, `test-github-lib.ps1`, `test-launch-lib.ps1`) | a test exits non-zero, or a `test-*.ps1` is in neither list of the script (a new test cannot be forgotten) |
| Build | `build-windows.ps1 -Config Release`, which also builds and runs the host unit tests (`arxtest`) | the build, or a CppUnit test, fails |
| Game checks | `scripts/dev/ci-smoke.ps1`: `smoke-test.ps1 -Seconds 3 -Commands 'drawcheck 120'`, then `spell-drawcheck.ps1` — the same scripts a worker runs, flat, `--loadlevel 10` | either returns `FAIL`, **or was skipped** (exit 2: the game lock stayed busy for 15 min) — a gate that did not run is not a pass, re-run the job |
| Upload | the artifact `game-checks`, also when a step failed | — |

`game-checks` holds, per phase (`smoke-test`, `spell-drawcheck`): the transcript (`<phase>.log`), then
in `<phase>\` every `arx*.log`, `status.json`, `ack.txt`, `stdout.txt` / `stderr.txt` of the channel,
`capture\` (the most differing draw check pair as `.bmp`), `crashes\` and the run `digest\`. Read the
digest, not the log. A whole green job takes about two minutes on the owner's PC (script tests 6 s, an
incremental build about a minute, game checks 17 s).

How the runner differs from a worker's copy — none of it needs action, all of it explains a surprise:
- **The runner service is `NT AUTHORITY\NETWORK SERVICE` in session 0** (no desktop), and the game runs
  there: it creates its window and OpenGL context, reaches `GAMEPLAY` in about a second, 120 fps. No
  interactive runner is needed.
- Its `%LOCALAPPDATA%` is `C:\Windows\ServiceProfiles\NetworkService\AppData\Local`: the build, user
  directory (`arx.log`, `crashes\`), test channel and `launches.log` of CI are there, not in the owner's
  profile. The **game lock** is not: it lives in `%ProgramData%\ArxFatalisVR\locks`, which both accounts
  can write, so CI waits for a game the owner or a worker runs and they wait for CI (#205, see
  [the game lock](#isolation-and-the-game-lock-parallel-work)). CI's own jobs never overlap (one runner).
- The service has no Steam registry entry of the owner, so the workflow passes the install folder:
  `ARXVR_DATA_DIR` in the workflow `env` (read by `ci-smoke.ps1`, passed as `--data-dir`).
- `ARXVR_AUTOREPORT=0`: a CI run files no bug issues by itself (#131); the failed step is the report.
- The cleanup stops only `arx.exe` processes that run from the CI build directory, never a game it did not start.

Run it by hand: `.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence` (needs a fresh build of the
copy; `-Phase smoke|spells` runs one half; exit 0 pass, 1 fail, 2 skipped, 3 cannot run here).

## The tester package (#88)

`.\scripts\package-tester.ps1` writes `dist\ArxFatalisVR-<version>-<sha>.zip` (version = number in `VERSION`, `-dirty`
appended for an uncommitted tree; `-symbols.zip` next to it). Inside: `arx.exe`, every DLL of
the build, the MSVC runtime copied app-local from the Visual Studio redistributable folder, `data/core` copied **next to
`arx.exe`** (the engine looks there, `RUNTIME_DATADIR=.`), `Start Arx Fatalis VR.cmd`, `Start flat (no VR).cmd`
(`--no-vr`, the kill switch), `Collect support bundle.cmd` + `collect-support.ps1` (logs, cfg, crashes, SteamVR logs,
OpenXR `ActiveRuntime`, GPU and driver into one zip on the Desktop, nothing uploaded), `HOW-TO-USE.txt`,
`BUG-REPORT-TEMPLATE.md`, `BUILD.txt` (commit, branch, tag). Sources of the tester files: `scripts/tester/`.
No `data.dirs` (CMake writes it for the build directory and it names a path in the working copy) and no game data:
the tester's game finds Arx Fatalis in their Steam libraries (#79).

The script fails (exit 1) on: an import (`dumpbin /dependents`, recursively) that is neither in the package nor a Windows
DLL; no OpenXR loader; `data.dirs` or `*.pak` in the zip; and — the real proof — the **extracted** zip, started from a
clean temp folder with `PATH` reduced to the Windows folders, not reaching `GAMEPLAY`, exiting ≠ 0, lacking
`Clean shutdown`, writing an `[E]` line or a crash report, or an `arx.log` that mentions the working copy's path. That is
one game launch (game lock, ledger). `-SkipVerify` stops after the numeric checks; `-NoBuild` refuses to build.

CI runs it after the game checks (`-NoBuild`) and uploads the zip as the artifact `tester-build` (14 days). The `-symbols.zip`
(`arx.pdb` + `BUILD.txt`, ~7 MB) is for reading tester crash reports and is not uploaded by CI.

## Isolation and the game lock (parallel work)

Several workers build and test at the same time ([WORKFLOW.md](WORKFLOW.md), *Parallel work*; #123,
launch rules D-014 / #147). What keeps their runs apart — all derived from the working copy's key
`<folder>-<hash>` (`scripts/build-paths.ps1`):

| Shared before | Now |
|---|---|
| the build | `%LOCALAPPDATA%\ArxFatalisVR\build\<key>\<Config>`; `built-from.txt` in it records the commit a build came from (`<sha>` or `<sha> dirty`), read by `test-pr.ps1` to decide whether to rebuild and by every launch script, which refuses a build of another copy or of another commit than `HEAD` (`-Rebuild` builds first) |
| `arx.log`, `cfg.ini`, saves, crash reports | `run-windows.ps1 -Isolated` and the smoke test pass `--user-dir` / `--config-dir` = `…\userdirs\<key>`. The player's `Saved Games\Arx Libertatis` is touched only by a plain `run-windows.ps1` (the owner's headset test, `test-pr.ps1`) or `smoke-test.ps1 -Shared` |
| the channel (`status.json`, `command.txt`) | `--test-dir` = `…\test\<key>`; a copy's `game-cmd.ps1` / `game-status.ps1` talk to that copy's game; `run-windows.ps1 -TestChannel` starts a manual run with it |
| saves for `--loadslot` | `-Saves save0000` copies slots into the isolated directory from `-SaveLibrary`, `$env:ARXVR_SAVE_LIBRARY` or the player's `save` folder (#70 defines the library) |
| the PC (GPU, desktop) and the headset | the game lock below: one game at a time, flat or VR |
| SteamVR | nobody but the owner starts it: a launch without `--no-vr` is made flat while `vrserver.exe` is not running; `-StartSteamVR` is the owner's switch |
| the owner's focus | self-test windows are shown without activation (`SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN` from the environment; `run-windows.ps1 -Background`, always in the smoke test) |
| who launched what | the ledger `%LOCALAPPDATA%\ArxFatalisVR\launches.log`, one line per launch; `cycle-report.ps1` → `LAUNCHES` |

**The game lock** (`scripts/launch-lib.ps1`): `%ProgramData%\ArxFatalisVR\locks\game-<n>.lock` (slots
`1..maxGames`, `maxGames` = 1 unless `settings.json` next to it says otherwise) plus `vr.lock` for a
VR launch — each a file opened for writing without write sharing by the launching script and held
until the game exits. Windows drops the handle with the process, so a crash or a kill cannot leave a
stale lock.

*Between Windows accounts (#205).* The folder is machine-wide because the CI runner service
(`NETWORK SERVICE`) and the owner are different accounts with different `%LOCALAPPDATA%`. The first
launch creates it with an ACL that gives Authenticated Users and `BUILTIN\Users` an inheritable
**Modify** rule, so a lock file created by one account can be opened, and deleted, by the other: a file
left behind by a killed process is simply free for everyone (the open handle, not the file's owner,
decides). Nothing to set up; no password or admin right is needed. `$env:ARXVR_LOCK_DIR` points the
scripts at another folder (the self-test uses it; set it the same for every account if you ever need
to). The old `%LOCALAPPDATA%\ArxFatalisVR\game-<n>.lock` is **still taken and checked together with the
new one**, so a working copy that has not merged `main` yet (older `launch-lib.ps1`) keeps seeing the
locks of the other copies of its own account; the transition ends when no such copy is left (then the
old location can be dropped). A game started by an *older* script (in another account's
lock folder) is seen by the new scripts as a game outside the lock (WMI, not `Get-Process`: the
`.Path` of another account's process is empty), so the new scripts wait for it as well; the older
scripts do not see games of another account - merge `main` into a copy that still has them. `settings.json` is read from the lock
folder first. `scripts/dev/test-launch-lib.ps1` covers the folder, the ACL, both locations, holders,
stale files and games outside the lock without a second account (CI runs it). The holder writes `pid=<script> game=<arx.exe pid> since=… what=… copy=… branch=…` into the
file; whoever finds the lock busy prints those lines. An `arx.exe` that no lock names (started by
hand) counts as a holder too; the crash processor (`arx.exe --crashinfo`) does not. Rules: **every**
launch takes a slot — `run-windows.ps1` waits `-LockTimeoutSec` (default 300 s) and then fails naming
the holders; `smoke-test.ps1` waits the same and then **skips** with exit 2. The old `vr-lock.ps1`
(#123, VR launches only) is gone.

Measured (owner's PC, 2026-10-09, #147 — SteamVR off the whole time, `vrserver.exe` absent before and after every row):

| Scenario | Result |
|---|---|
| `run-windows.ps1 -Isolated -TestChannel -Background` **without** `--no-vr`, SteamVR off | `SteamVR is not running and this script never starts it (D-014): launching flat (--no-vr added)`; the log says `VR disabled on the command line (--no-vr)`; the foreground window stayed the owner's terminal; ledger line `mode=flat … args=--no-vr` |
| `smoke-test.ps1 -Vr`, SteamVR off | `SKIPPED - -Vr given but SteamVR (vrserver.exe) is not running, and a worker never starts it`, exit 2, no launch |
| two smoke tests started 4 s apart from the same copy | the second printed `game lock busy (all 1 game slot(s) are held) … held: game-1.lock: pid=30384 game=35904 … branch=ARXVR-147-…`, waited, then ran; **2 of 2** `PASS`, both in the ledger; no `.lock` left behind |
| a lock held by another process, `smoke-test.ps1 -LockTimeoutSec 3` | `SKIPPED - another game kept the game lock for 3 s`, exit 2, holder printed; after `Stop-Process` on the holder the lock was free at once |
| `built-from.txt` edited to another sha | `stale build: built from 012345678, HEAD is 7de3879d0. Build first … or pass -Rebuild`, no launch; with the stamp restored the launch runs |
| `-BuildDir` of the owner's copy | `… is not a build of this working copy. Launches run only the copy's own build …`, no launch |
| the no-focus window | before #147 `status.json` said `window.focused: true` while the foreground window was the terminal — `SDL2Window` assumed focus after `SDL_ShowWindow` (TRAPS.md); now `window: focused=False minimized=False 640x480` in both smoke runs, 120 fps, `GAMEPLAY` after 0.8 s and 1.2 s |

Measured (owner's PC, 2026-10-09, #123):

| Scenario | Result |
|---|---|
| `smoke-test.ps1` (isolated by default) | `PASS`; `arx.log`, `cfg.ini`, `save\`, `crashes\` appear in `userdirs\<key>`; the player's `Saved Games\Arx Libertatis\arx.log` keeps its `LastWriteTime`. First run in a fresh directory: 1 warning (no `cfg.ini` yet), later runs 0 |
| **Two smoke tests at the same time** (this worktree's build and the owner's build, separate user and channel directories), `-Seconds 8` each | **2 of 2** `PASS`, both done in 11 s; `GAMEPLAY` after 0.5 s and 1.1 s, 0 errors — **no second-instance limit** in the engine. *Superseded by D-014 (#147): the owner wants one game at a time anyway, so every launch takes the game lock now and the second test waits* |
| VR lock held by another process: `smoke-test.ps1 -Vr -VrLockTimeoutSec 3` | `SKIPPED`, exit 2, holder printed: `pid=2580 since=… what=lock-test holder` |
| the same, `run-windows.ps1 -VrLockTimeoutSec 2` without `--no-vr` | fails: `another game is running in VR (pid=2580 … what=lock-test holder). Wait for it to end, or add --no-vr` |
| the same, flat `smoke-test.ps1` | `PASS` — a flat run ignored the VR lock *(since #147 a flat run waits for the game lock too)* |
| the holder is killed (`Stop-Process`) | the lock is free at once; after a release no `vr.lock` file is left |
| `smoke-test.ps1 -Vr` with SteamVR running, headset idle, lock free (3 runs) | the lock is taken and released, VR `enabled=True session=ready` in all three; **2 of 3 crashed on exit** with the #93 stack (`Entity::cleanReferences` ← `DanaeClearLevel`), no cinematic loaded — the VR exit path is exposed to #93 where the flat path is clean (4 of 4 the same hour), so `-Vr` is **not a gate** until #93 is fixed (evidence on the issue). *#93 fixed on 2026-10-09: 0 exit crashes in 13 VR exits; `-Vr` stays best effort because of #146* |

## Crash reports under SteamVR and the symbol archive (D4, #132)

- **The crash filter is taken back.** The engine reports crashes through `SetUnhandledExceptionFilter`; an overlay, the
  OpenXR runtime or a driver loaded into the process can replace it and silence every report. `ArxGame::doFrame` calls
  `CrashHandler::rearm()` once a second: if another filter is installed, ours is restored and `arx.log` gets
  `[W] [crash] the unhandled exception filter was replaced by another module, restored`. No vectored-exception fallback
  (research F-93): add it only if a tester's crash is ever missing a report while that warning is absent.
- **Check without a headset.** Channel `crash` faults on the main thread; `crash steal` replaces the filter the way an overlay
  would and faults 3 s later (after the re-arm). Run: `smoke-test.ps1 -VrSim basic -Commands 'crash steal'` - expected
  `FAIL` (the game is meant to die) with a new `crashes\<utc>\crash.txt` (`ACCESS_VIOLATION`, stack with file:line), the
  re-arm warning and the deliberate-crash warning in the log. 2026-10-09: 2 of 2 runs wrote the report on the simulated
  headset (session focused, 270 frames). A run with `[crash] deliberate crash requested` in its log is digested but never
  filed as an auto-report. The real SteamVR overlay is not simulated: the first tester crash under SteamVR confirms it.
- **Symbols per build.** `package-tester.ps1` writes the exe's SHA-256 into `BUILD.txt` (`arx.exe sha256:`), packs
  `arx.pdb` + `BUILD.txt` into `*-symbols.zip` and keeps a copy in `%LOCALAPPDATA%\ArxFatalisVR\symbols\<sha256>\`. CI uploads
  the zip as the artifact `tester-symbols` (90 days). A tester's support bundle (`system.txt`, `BUILD.txt`) names the hash of
  the exe that crashed: take the pdb of that hash, nothing else matches.
- **The support bundle** already holds `arx.log` + previous runs and `crashes\` (#88); it now also has the exe hash and `BUILD.txt`.
