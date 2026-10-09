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
.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5            # a table of 10 runs
.\scripts\dev\smoke-test.ps1 -Saves save0000 -- --loadslot 0  # copy a save into the isolated user directory, then load it
.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0
.\scripts\dev\smoke-test.ps1 -Menu -Commands 'loadslot 0'       # plain launch (main menu), then a save through the channel
.\scripts\dev\smoke-test.ps1 -Commands 'drawcheck 120'         # two draws of the scene per frame must give one picture (#71)
.\scripts\dev\comfort-check.ps1                               # shake, FOV zoom and bob with the VR comfort policy off and on, one run (#76)
.\scripts\dev\spell-drawcheck.ps1                             # the 26 spells of #71 really launched, then drawcheck 240 (#155)
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
`-Saves <slots>` copies save folders into it (`save0000`, … from `-SaveLibrary`, `$env:ARXVR_SAVE_LIBRARY`
or the player's `save` folder) for `-- --loadslot` runs.

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
| `loadslot <n>` | load save slot `n` (from the menu or in-game), refused with the slot count if it does not exist |
| `ui inventory open\|close` | what the inventory key does (the book has the script command `book open\|close`); a `ui.inventory` event once the inventory is open / has finished closing. While a level script holds the player (the level 10 start: letterbox, controls blocked) the HUD is not drawn and a close never finishes — `setplayercontrols on` and `cinemascope off` first (#130) |
| `loadlevel <n> [target]` | change to level `n` in a running game (refused from the menu: start with `--loadlevel` or `loadslot`) |
| `drawcheck [frames]` | for the next `frames` frames (default 1) draw the scene view **twice** per frame with the same camera and compare the two pictures pixel by pixel; result in `drawCheck` of the status and one `[render] drawcheck N done:` log line — see [The draw check](#the-draw-check-drawcheck). Refused outside gameplay or while a check runs |
| `recenter` | refused with `not implemented yet (#28)` until recentering exists |
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
 "player":{"x":...,"y":...,"z":...,"yaw":...,"life":...,"lifeMax":...,"mana":...,"manaMax":...},
 "camera":{"x":...,"y":...,"z":...,"pitch":...,"yaw":...,"roll":...,"fov":75.0,"bob":0.0,"shake":0.0},
 "spells":{"active":2,"launched":3,"activeByType":{"speed":1,"fire_field":1},
           "launchedByType":{"speed":1,"heal":1,"fire_field":1}},
 "comfort":{"mode":"auto","active":false,"viewBobbing":true,"screenShake":true,"fovZoom":true},
 "window":{"width":640,"height":480,"focused":true,"minimized":false,"fullscreen":false},
 "vr":{"supported":true,"enabled":false,"sessionState":"none","frames":0,"shownFrames":0,"runtime":"",
       "manifest":"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json"},
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
- `player` is present only while a level is loaded. More fields (the rate line)
  come with #20; the `.xrs` runner (#113) asserts on these keys with `@modassert`.
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
- `drawCheck` appears once a `drawcheck` was requested: `{"seq":1,"running":false,"frames":120,
  "framesDiffering":0,"pixels":307200,"maxPixelsDiffering":0,"maxDelta":0,"capture":""}` —
  `frames` compared so far, `framesDiffering` of them with at least one differing pixel,
  `maxPixelsDiffering` the worst frame, `maxDelta` the largest difference of one color channel
  (0–255), `capture` the base name of the most differing pair (`<capture>_a.bmp` / `_b.bmp`).

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
| `vr.refspace` | the runtime moves a reference space (its own recenter, a new play area) | `space`, `poseValid` |

Not here yet, by design: melee attacks *refused* by a gate (F-66) come with the VR swing detector
(#39); recenter with #28; locomotion, interaction and rune-drawing in VR add their own events in the
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
.\scripts\dev\test-start-steamvr.ps1                 # self-test of how -StartSteamVR starts SteamVR (#171): stubbed processes and clock, SteamVR is not started
$env:GH_HOST='bogus.invalid'; .\scripts\dev\test-pr.ps1 <pr> -NoLaunch   # the test station with the GitHub API unreachable (#171)
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
| `smoke-test.ps1 -Menu -Commands 'loadslot 0'` (plain launch, then a save through the channel) | `MENU` 0.8 s after launch; `loadslot 0` refused with `no save in slot 0 (have 0)` — the owner's machine has no saves yet (#70), so the success path of `loadslot` is **not verified** here; it is the same `LOADQUEST_SLOT` path as `--loadslot` |
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
| 2026-10-09, #115 log banner: flat smoke test, then `status.json` and the first log lines asserted by script | `PASS`; `build.commit` = `git rev-parse HEAD` of the worktree **after a commit and a rebuild** (before #115 a worktree kept its first build's commit), `build.branch`, `build.config` = `Release`, `vr.manifest` = the SteamVR manifest from `HKLM`, `vr.runtime` = `""` in a flat run; the four banner lines match their documented shape. `XR_RUNTIME_JSON` → a missing file (flat, `--no-vr`): the manifest line says `from XR_RUNTIME_JSON, overriding the registry's "…steamxr_win64.json"`, exactly **1** `[W] … does not exist`, `vr.manifest` = that path, `PASS`. `built-from.txt` edited to another commit: exactly **1** `[W] built-from.txt names commit …`, `PASS`. The `OpenXR runtime answered` line needs an instance (SteamVR running): not exercised in that session, it is the renamed `Using OpenXR runtime` line |
| 2026-10-09, #129 **before** (the `main` build): `run-windows.ps1 -Isolated -Background -- --no-vr --loadlevel 10`, no channel, `Stop-Process` long after the level had loaded | `arx.log` = **4137 bytes, 42 lines, cut mid-line** (`[I] Localisation` and nothing after it): the `ofstream` buffer; the end of the level load lost, no `Clean shutdown` |
| 2026-10-09, #129 **after**: the same, killed 15.5 s after the start | `arx.log` = **4787 bytes, 49 complete lines**, last line `[I] LoadLevel:511  Done loading level` (the last line the game wrote), ends with a newline; the previous run's truncated log sits in `arx.1.log`; banner `Log: "…\arx.log", previous runs kept as arx.1.log .. arx.9.log` |
| 2026-10-09, #129: `smoke-test.ps1 -Seconds 5 -Commands 'mark flush-check'`, then `-Seconds 3` on the final build | `PASS` both, 0 errors, 0 warnings; after the second run the history reads `arx.log` (pid 35468) ← `arx.1.log` ← `arx.2.log` (the killed `main` run), each `Run:` line one run older — the smoke test no longer deletes `arx.log` before a run |
| 2026-10-09, #129, **the degraded path**: `arx.1.log` held open by PowerShell (`FileShare::ReadWrite`, no delete sharing) during `smoke-test.ps1 -Seconds 3` | `PASS` with exactly **1** warning: `[W] Startup:211  Log rotation failed: could not move "…\arx.1.log" to "…\arx.2.log" - nothing was deleted, this run appends to the previous log`; `Log: …, appended to the previous run's log`; `arx.log` grew from 5114 to 10583 bytes with two `Starting` lines, `arx.1.log` untouched. `fs::rename` retries 10 × 100 ms first (its `[W] Failed to move file … 32 = … being used by another process` lines go to stderr — the file logger does not exist yet), so `STARTUP` came at +1.0 s instead of +0.3 s |
| 2026-10-09, #129, **the test can fail**: `FileLoggerTest` built with both flushes removed on purpose | `Run: 39  Failures: 2`: `testWarningIsOnDiskAtOnce` and `testInfoIsOnDiskWithinTheInterval` (after its 2 s wait); `testExplicitFlush` and the three rotation tests stay green, as they should. Restored: `OK (39 tests)` |
| 2026-10-09, #155: `spell-drawcheck.ps1` — the 26 spells of #71 **really launched** (23 by the player with `-fsm`, `summon_creature` / `raise_dead` / `fake_summon` by `human_base_0033` on the player), then `drawcheck 240` on level 10 | `PASS`: **26 of 26** in `spells.launchedByType`, each in the `status.json` write right after its own command; **24 spell types active during the check** (all but `harm` and `mana_drain`, which had ended), **0 of 240 frames differ** (max 0 of 307200 pixels, delta 0), 0 errors, 0 warnings. The #71 split holds with every spell effect on screen — no bug to file against it |
| 2026-10-09, #155: the same with `-Flags fs` — the cast of the #71 rows, the broken state on purpose | `FAIL` of the check (the smoke test passed, 0 of 240 frames differ): **0 of 23** player spells launched; the NPC's 3 launched one command later through the NPC rune drawing. The launch evidence catches the #71 mistake |
| 2026-10-09, #155: the player's own `summon_creature` and `raise_dead` (`-fsm`): at the start point; after `teleport -ap 0/90/180/270 player`; after `teleport -p human_base_0033` | **never launched** — `CanLaunch` finds no valid spot 3 m ahead on level 10, a silent fizzle; hence the NPC caster above |

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

**The game lock** (`scripts/launch-lib.ps1`): `%LOCALAPPDATA%\ArxFatalisVR\game-<n>.lock` (slots
`1..maxGames`, `maxGames` = 1 unless `settings.json` next to it says otherwise) plus `vr.lock` for a
VR launch — each a file opened for writing without write sharing by the launching script and held
until the game exits. Windows drops the handle with the process, so a crash or a kill cannot leave a
stale lock. The holder writes `pid=<script> game=<arx.exe pid> since=… what=… copy=… branch=…` into the
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
