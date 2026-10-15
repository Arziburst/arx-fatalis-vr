# Arx Fatalis VR — Testing without a headset

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
.\scripts\dev\smoke-test.ps1 -Playground                      # the test playground (#321): the generated room, a max-level hero, GAMEPLAY in area 30
.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0
.\scripts\dev\smoke-test.ps1 -Menu -Commands 'loadslot 0'       # plain launch (main menu), then a save through the channel
.\scripts\dev\smoke-test.ps1 -Commands 'drawcheck 120'         # two draws of the scene per frame must give one picture (#71)
.\scripts\dev\comfort-check.ps1                               # shake, FOV zoom and bob with the VR comfort policy off and on, one run (#76)
.\scripts\dev\head-check.ps1                                  # a simulated head pose drives the camera: turn, move, lean limit, recenter, off - one run (#24)
.\scripts\dev\recenter-check.ps1                              # the player's recenter on the simulated headset: sit down, long press of the left stick, back - one run (#28); -NoPress must FAIL
.\scripts\dev\eye-height-check.ps1                            # the eye height on the simulated headset: loaded on the desk, put on, crouch 20 cm, auto-raise, off and on seated - one run (#27)
.\scripts\dev\body-check.ps1                                  # the body after the head on the simulated headset: ±40° drag, ~1.5 s settle, camera still, heading turn, recenter - one run (#25)
.\scripts\dev\spell-drawcheck.ps1                             # the 26 spells of #71 really launched, then drawcheck 240 (#155)
.\scripts\dev\input-check.ps1                                 # the VR controls' table (#30): taps, chords, a remap and a reset through vr-input, one flat run
.\scripts\dev\game-input-check.ps1                            # the VR input module (#74): VR buttons -> inventory, book, crouch, jump, game menu; the move vector; one flat run
.\scripts\dev\run-script-tests.ps1                            # the PowerShell self-tests of the dev scripts (seconds, no game); CI runs them first
.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence      # what CI runs after the build: smoke test + drawcheck, spell draw check, evidence in one folder (#127)
.\scripts\run-windows.ps1 -Isolated -TestChannel -- --no-vr --loadlevel 10   # a manual run with the channel, in this copy's own user dir
.\scripts\dev\log-digest.ps1 -Print                           # the last run in a screenful + automatic bug filing; runs by itself after every launch (#131), never cat arx.log
.\scripts\dev\perf-summary.ps1                                # the last run's arx-perf.log: worst seconds, every hitch with its cause, the frame budget split (#327)
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
`-EvidenceDir <dir>` keeps every **failed** run's evidence in `<dir>\runNN-<time>\` before the next run
replaces it (#146): `arx.log`, `stdout.txt`, `stderr.txt`, `status.json`, the run's new crash reports,
`reasons.txt` and, with `-Vr`, the SteamVR logs written during the run (`Steam\logs\vrserver*`,
`vrcompositor*`, `vrmonitor*`, `vrclient_arx*`, `xrclient_arx*`).

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

## Launch kinds (#394)

A plain `arx.exe` is the **player's launch**: the logos FISHTANK and ARKANE, then the level 10 intro (the original
game's start: a scripted scene that plays `introduction.cin` and ends in the main menu), then the main menu, and every
`.cin` plays. `--dev-launch` is the development launch: no logos, no intro, no cinematics (`--intro` /
`--cinematics` show them anyway). `smoke-test.ps1` (and everything on it: `xrs-run.ps1`, `ci-smoke.ps1`),
`run-windows.ps1` and `test-pr.ps1` add `--dev-launch` themselves; `-Player` on them (`@player` in a sequence)
launches as a player. `smoke-test.ps1 -Player` without arx.exe arguments waits for `LOGO` (or `MENU`) instead of the
`--loadlevel 10` default. A sequence that wants the main menu at once says `@args --dev-launch` (a plain development
launch reaches `MENU` in about a second; `--skiplogo` would play the level 10 scene first, about 59 s).

**The owner runs the whole path in the headset** as a player does: the desktop shortcut (`launch-shortcut.ps1` passes
`-Player`), or `.\scripts\dev\test-pr.ps1 <pr> -Player -StartSteamVR`, or `.\scripts\run-windows.ps1 -Player`. The
logos and the intro are on a screen in the room (A, B or a trigger skips them), then the menu, New game and the
character creation by pointer.

**What the CI checks of it:** `first-run.xrs` (`@player`, one launch) walks the whole path - both logos skipped (A, the
trigger), the intro on the VR screen skipped with A during its scripted scene (state `INTRO`, before
`introduction.cin`), the menu with no leaked click, Options (the crosshair off by pointer) and back, New game, the
character creation by pointer clicks (the average hero in `status.json`), the prison (area 1) in `GAMEPLAY`, its
intro (a scripted scene, `scene state=start`) skipped with A through its own skip handlers as Esc does (`scene.skip
how=vr result=sent`, the controls back, the hero at `MARKER_0112`, no jump), a walk with the left stick; `@shot` of the
intro and the menu in both eyes. The parts have their own faster sequences: `first-run-intro` (a player's launch up to
`introduction.cin`, its skip with B, New game, the prison intro played to its end without a skip, and the walk),
`first-run-menu` (a development launch at the main menu) and `first-run-chargen` (`--skiplogo` until #399).
`ci-smoke.ps1` runs all of them on every pull request, and after `first-run-menu` its **read-back**
`readback/first-run-menu-readback.xrs` in the same user directory (the crosshair setting, written to `cfg.ini` on quit,
read back by the second launch).

**What it does not cover (#92, #54):** only the intro's `introduction.cin` is played and skipped on the VR screen. The
in-game cutscenes (the forced cameras that #29 / #54 are about) are not walked by any sequence: #92 (restore cinematics
for players) must not ship before they are covered.

## The exit hook (#146)

Every way the process ends names itself in `arx.log`. The game's own shutdown logs `Clean shutdown`; a crash
writes a crash report; every quit request logs `Quit requested: <who>` (the window closed, the main menu, the
channel's `quit`, the simulated headset's `quit`, the benchmark); the runtime ending the VR session logs
`OpenXR session ended by the runtime (exiting|loss pending)` or `OpenXR runtime is shutting down (instance loss
pending)` and the game goes on flat. What is left - `exit()` or `ExitProcess` called inside the process by any
module before the shutdown finished, the shape of #146 - is caught by `platform/ExitTrace` (an `atexit` handler,
and a TLS callback of `arx.exe` that the loader calls for every `ExitProcess`), which flushes the log and appends:

```
[E] ExitTrace:0  The process is ending outside the game's own shutdown (#146): ExitProcess (or _exit / abort) was called inside the process, not exit() of the game's C runtime - the stack names the module that called it, exit code -1 (0xffffffff)
[E] ExitTrace:0  Last cause noted before it: OpenXR session ended by the runtime (exiting), 195 ms earlier
[E] ExitTrace:0  Called from (innermost first): ntdll.dll+0xe83fb < ... < KERNEL32.DLL+0x4141b < arx.exe+0x21682e < ...
```

The same lines go to stderr (`stderr.txt` of a smoke run). Read the stack from the left: after the loader's
`ntdll.dll` frames, `KERNEL32.DLL` is `ExitProcess` (or `ucrtbase.dll` for `exit()`), and the **next module is the
caller** - `arx.exe` is ours (resolve the offset with the build's `arx.pdb`), anything else is a library
(`vrclient_x64.dll`: SteamVR's client, a driver ...). With `exit()` a fourth line follows: `The process ended with
exit code N`. **No ExitTrace lines, no crash report and no `Clean shutdown`** means nothing in the process ran on
the way out: a `TerminateProcess` from outside (the task manager, a launcher, a runtime killing a hung app).

- `scripts\dev\xrs\vr-session-end.xrs`, `vr-session-loss.xrs` (CI runs them): the simulated runtime ends the session
  (`vr-sim session exiting`, `vr-sim runtime lost`, `vr-sim session loss-pending`); the game must go on flat, name the
  cause and still end with `Clean shutdown` and no ExitTrace line. The three warnings are in the digest allowlist.
- `scripts\dev\exit-trace-check.ps1 [-Fault exit-process|exit] [-Code -1]`: one launch on the simulated headset,
  the session ended by the runtime, then `vr-sim fault ...` ends the process from inside. The smoke run FAILS (on
  purpose); the check PASSES when the exit code, the way out, the cause and a stack with `arx.exe` are in `arx.log`.
  Bug filing is off for the run (`ARXVR_AUTOREPORT=0`).
- The simulated headset cannot reproduce what SteamVR does: the real hunt is the owner's, with SteamVR running and
  only on his word (D-014): `.\scripts\dev\smoke-test.ps1 -Vr -Repeat 10 -Seconds 3 -EvidenceDir
  $env:LOCALAPPDATA\ArxFatalisVR\evidence\146` - each failed run keeps its `arx.log`, stdout / stderr and the SteamVR
  logs.

## The hang watchdog (#440)

With the test channel the game watches its main thread (`platform/HangWatchdog`, from the first frame): the main
thread beats every frame, when a level load starts and at every step of a load's progress bar. When no beat came
for the threshold (5 s, below the 10 s a test waits for an ack; the channel's `watchdog <ms>` changes it) a thread
of the watchdog suspends the main thread just long enough to walk its stack, resumes it and writes the report to
`<dir>\hang.txt` (first, without the logger) and as `[W] ... [hang]` lines of `arx.log`:

```
[hang] main thread stalled for 5019 ms (report 1 of at most 3), last beat at level load, then loading screen: presenting the frame
[hang]   #0 ntdll.dll+0x164a14 ZwWaitForAlertByThreadId
[hang]   #10 OPENGL32.dll+0x817e wglSwapBuffers
[hang]   #13 arx.exe+0x10122b LoadLevelScreen (LoadLevelScreen.cpp:128)
[hang]   #14 arx.exe+0x1907de loadLevel (LoadLevel.cpp:324)
[hang] main thread responsive again after 6230 ms
```

A stall that goes on is reported again every threshold (three reports at most, so a stack that moves shows it),
and the first beat after it is reported too. "then <step>" names the last step noted after the beat (the loading
screen notes its present). Functions and lines come from the build's `.pdb` next to `arx.exe`, read when the
watchdog starts — never during a report, since a thread stuck in the loader holds the lock that loading symbols
waits for. A thread that does not stop for the walk within 1 s (inside the kernel with suspension blocked, as in a
driver call) is reported without its stack. `Send-ArxCommand` (`scripts/dev/test-lib.ps1`) puts the new reports of
`hang.txt` into its timeout: `the game hangs: no ack for '<command>' after 10 s, and its hang watchdog reported
(hang.txt): ...` instead of a bare timeout. `smoke-test.ps1 -EvidenceDir` and the CI evidence keep `hang.txt`.

- Host test `HangWatchdogTest`: a stalled thread is reported within its threshold with `platform::stallThisThread`
  and the test function in its stack, three times, then responsive again; a beating thread never.
- `scripts\dev\xrs\level-load-hang.xrs` (CI runs it): `watchdog 1000`, `stall load 3000`, `loadlevel 1` - the report
  names the stall and the load under it, the main thread is responsive again and level 1 loads.
- A load that goes on slowly beats at every progress step and is no hang: what it spends its time on is the
  `[channel] level N loaded in <ms> ms, the loading screen drew <frames> frames in <ms> ms` line of a `loadlevel`.

## Background runs are silent (`--mute`, #345)

CI, smoke tests, `.xrs` sequences, `--vr-sim` runs and a worker's own launches happen on the owner's PC
while he works or tests in the headset, so they play no sound. `arx.exe --mute` keeps every audio mixer
at volume 0 for that run and logs one line, `Audio is muted (--mute): ...`, to `arx.log`. The flag lives
in the mixer only: `cfg.ini` keeps the owner's volumes (like `--windowed`, it never leaks into the config).

| Launch | `--mute` |
|---|---|
| `smoke-test.ps1` (so `ci-smoke.ps1`, `xrs-run.ps1`, the CI job, every `--vr-sim` sequence) | yes; `-Sound` opts out |
| `run-windows.ps1` with `-Background` or `-TestChannel` (a worker's / test launch) | yes; `-Sound` opts out |
| `run-windows.ps1` by hand (neither switch) | no; `-Mute` silences it |
| `make-test-saves.ps1`, `package-tester.ps1` verify run | yes |
| `test-pr.ps1` (the owner's test station, with or without `-StartSteamVR`) | no - it passes `-Sound` |
| the desktop icon (`launch-shortcut.ps1`) | no - it passes `-Sound` |
| the Steam non-Steam shortcut, `arx.exe` started by hand | no |

The owner's headset tests always have sound: `test-pr.ps1` passes `-Sound` on every path it launches
(with and without `-StartSteamVR`, with `-Saves`), `run-windows.ps1` started by hand has sound, and only
background launches (CI, smoke-test, `--vr-sim`, workers' `-Background` / `-TestChannel` runs) are muted. `run-windows.ps1 -DryRun` (and `test-pr.ps1 <pr> -DryRun`) prints the
`arx.exe` command line without starting the game, SteamVR (`-StartSteamVR` only prints "would start
SteamVR") or copying saves - no `--mute` with `-Sound`. `test-pr.ps1 -DryRun` still prepares the
worktree, fast-forwards and builds first (only the launch is a dry one). A smoke-test run prints the
command line (`=== run ...: arx.exe ...`) and `arx.log` has it in the `command line:` banner; `launches.log`
records only the extra arguments, not `--mute`.

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
| `shot [name]` | after the next rendered frame, save it as `<dir>\capture\<name>.bmp` (default `shot<N>`); `captureSeq` and `lastCapture` in the status, and `capture`: what each quarter of the picture shows (#334) |
| `window <width> <height>` | resize the game window as a player dragging its border does (#334): the interface is laid out again for the new size at the next frame (`window` and `vr.interface.windowWidth/Height` in the status). At least 320 × 240; refused in fullscreen. The size is saved to `cfg.ini` when the game quits |
| `mark <text>` | `[mark] <text>` in the log — an anchor for "the line I wait for must come after this"; also an `M` line in [the performance log](#the-performance-log-arx-perflog-327) |
| `stall <ms>` | the main thread sleeps 1..5000 ms in this frame: a forced hitch, which the performance log must mark with `cause=channel` (#327) |
| `stall load <ms>` | the **next level load** sleeps 1..5000 ms right after its `Loading level` line: a stalled load for the [hang watchdog](#the-hang-watchdog-440) (#440, `level-load-hang.xrs`) |
| `watchdog <ms>` | the hang watchdog's threshold, 100..60000 ms (default 5000): a main thread without a beat for this long is reported with its stack (#440); `hangWatchdog.thresholdMs` in the status |
| `quit` | ask the game to quit at the end of the frame (the same path as closing the window) |
| `save <name>` | save the running game as a new slot named `<name>` (no thumbnail); the newest save is slot 0, so `loadslot 0` loads it back. A `save slot=save000N name=<name> ok=yes\|no` event either way; refused outside a running game (#130) |
| `loadslot <n>` | load save slot `n` (from the menu or in-game), refused with the slot count if it does not exist. The command-line `--loadslot <n>` of a missing slot logs an `[E]` and starts at the main menu (before #24 it started an empty level: a black world with only the HUD, TRAPS.md); `run-windows.ps1` / `test-pr.ps1` refuse such a launch before it starts |
| `ui inventory open\|close` | what the inventory key does (the book has the script command `book open\|close`); a `ui.inventory` event once the inventory is open / has finished closing. While a level script holds the player (the level 10 start: letterbox, controls blocked) the HUD is not drawn and a close never finishes — `setplayercontrols on` and `cinemascope off` first (#130) |
| `loadlevel <n> [target]` | change to level `n` in a running game (refused from the menu: start with `--loadlevel` or `loadslot`) |
| `newgame` | what the menu does for "New game" and the end of the character creation, without the screens: an average hero (all attributes 10, skills 2, nothing to distribute), level 1. Refused outside the main menu (a plain launch). The hero then lies in the start cell and `player.controlsBlocked` stays `true` for 30-40 s while the level script plays the awakening (#70) |
| `entities [filter]` | write one line per entity to `<dir>\entities.txt`: `<id> npc\|item\|fix\|marker\|camera\|other show=<n> pos=<x> <y> <z> [life=<n>] interactive=<0\|1>`; `filter` is a substring of the id. `show` is the engine's `EntityShowState`: 0 in the scene, 2 in an inventory, 6 equipped by the player. The position of a floor object (lever, chest, item) is on the **floor** (#70). `interactive=0`: its script switched it off, no hand reticle or mouse hovers it (#210) |
| `setpos <x> <y> <z> [yaw]` | put the player at exact coordinates, optionally facing `yaw`. `y` is the **feet** (the engine's teleport): the eye, `player.y` in the status, ends 170 units higher (smaller `y`). A `y` taken from the eye puts the player 170 units above the floor (TRAPS.md, #210). About 100 units are a metre. Refused outside gameplay (#70) |
| `approach <entity> [<distance>]` | put the player where the hand reticles reach an entity, facing it — the spot is found by the game, not by trying spots in launches (#210): every 15° on rings 80-320 units around the entity, the first floor within 100 units of the entity's own, room for the player's cylinder, the eye within the pick's reach (350 units of the entity's position), and no wall before the entity's faces on the rays from the eye and from both simulated hands to the middle of its box. The spot nearest to `distance` along the floor (default 170) wins; one `[channel] approach` log line with the spot and the counts of rejected spots. Call it **twice**: far from the player an entity is not updated (box, faces), so the first call tests only the level, the second the entity's faces. Refused (with the counts) when no spot qualifies |
| `lookat <entity>` | turn the player (yaw and pitch) towards an entity, aiming 40 units above its floor position. The pitch is not kept by a save and does not matter in VR (the head decides); the yaw is kept (#70) |
| `place <entity> <ahead> [<right>]` | put an entity on the floor `ahead` units along the player's heading (and `right` units to its right), facing the player — a target in reach for melee checks (#39; `goblin_base_0036.setspeed 0` first keeps an NPC from walking off). Refused outside gameplay or for an unknown entity |
| `drawcheck [frames]` | for the next `frames` frames (default 1) draw the scene view **twice** per frame with the same camera and compare the two pictures pixel by pixel; result in `drawCheck` of the status and one `[render] drawcheck N done:` log line — see [The draw check](#the-draw-check-drawcheck). Refused outside gameplay or while a check runs |
| `vr-input down\|up\|tap <control>`, `vr-input stick left\|right <x> <y>`, `vr-input bind <event> <binding>`, `vr-input reset`, `vr-input turn snap\|smooth\|angle <deg>\|speed <deg/s>\|repeat <s>` | the VR controls' table without a headset (#30, `vr/VRInput.h`): a control (`right.a`, `left.grip`, `right.stick_down` …) held, let go or tapped as if a controller did it, merged into what OpenXR reports and resolved by the same table at the next frame; the game then reacts as to the controllers (#74: inventory, book, jump, crouch toggle, the game menu, the mouse buttons). `stick` holds a stick deflected (the analog value only — its direction controls are `down` / `up`), seen in `vr.input.game.moveX/moveY/turn`; `bind` remaps one event (`bind jump left.grip+left.a`, `bind menu left.b:long`, `bind magic_mode none`); `reset` restores the default layout for the current controllers and lets go of everything (`bind` and `reset` are not saved); `turn` sets the turn settings as `cfg.ini` does (#32: mode, snap angle, the repeat pause of a held stick (0 = none), smooth speed — `vr.input.turn` shows them with `snaps`, `lastSnap`, `turned` degrees right positive, `allowed`, and for the last push `pushRepeats` and `repeatInterval` — the mean seconds by the clock from its first snap to its last repeat (#285); one `vr.turn.snap … repeat=no|yes` game event per snap, `vr.turn.smooth` at the start and stop of a smooth turn). Refused with the reason for an unknown control, event or binding. `vr.input` in the status (`vr.input.game.held`: the actions, buttons and keys the engine is given); one `[vr] input: <event> (<binding>)` log line per event press, one `[vr] input: layout …` line per layout change |
| `vr-input capture <event>`, `vr-input cancel`, `vr-input default` | the VR settings page's remapping (#178, `vr/BindingCapture.h`): `capture` is what a click on the event's binding does — the next controls pressed (`vr-input down` / `up` / `tap`, or the simulated controllers) become its binding: released → single, held ≥ 0.5 s → `:long`, a second control of the same hand while the first is held → a chord; the walk stick and the turn stick's left / right are refused with the reason; nothing pressed for 8 s cancels. While capturing, the controls reach nothing else. The result is applied at once and **saved** for the controller type (`vr-controls-<type>.ini` in the config directory: only the events that differ from the default). `cancel` ends a capture; `default` is the page's "reset to default" (#35's layout, the file removed); `reload` reads the saved layout again at the next frame, as a start does. `vr.input.capture` and `vr.input.remapped` / `conflicts` in the status; a `vr.remap` game event per result |
| `menu pause\|resume\|none\|options\|input\|vr\|vr-turning\|loadsave\|interface\|quit\|language`, `menu step <row> left\|right`, `menu drag <slider> <0..1>` | the main / pause menu without clicks (#178): `pause` opens the pause menu from a running game (as Escape does), `resume` goes back to the game ("Resume game"); the others open that page of the main or pause menu (`none` closes the options window). On the VR turning page (#260, D-043, the page open): `drag` is a slider's dot dragged to that point of its bar (0 left end, 1 right end) and let go — it lands on the nearest notch: `angle` (30–90° in 5° steps), `speed` (very slow / slow / medium / fast / very fast = 60 / 90 / 120 / 180 / 240°/s), `repeat` (off, 0.25, 0.4, 0.6, 0.8, 1 s); `step` is one notch of a slider (the arrow keys) or a click on the left / right arrow of the rows `turn` (snap / smooth) and `height` (−20 to +20 cm in 2 cm steps); a change is a `vr.settings row=… value=…` game event and applies at once. `menuPage` in the status |
| `stick-for left\|right <x> <y> <units> [<seconds>]` | a walk bounded by its distance (#370): holds the stick deflected as `vr-input stick` does and lets it go in the first frame the player is `units` or more from where it started on the ground (x, z) - however slow the frames or the runner are. The player still coasts a few units after the release (≈ 28 at 90 fps). `stickFor` in the status (`@until stickFor.active eq false`, then assert on `stickFor.walk`); one `[channel] stick-for:` log line per release. A player who never gets that far (a wall) is let go after `seconds` of game time (default 10) with `stickFor.timedOut` true and a `[W]` log line - assert `stickFor.timedOut eq false`. Refused outside gameplay |
| `vr-input height <meters>` | the character height setting as `cfg.ini` (`vr_height_offset`) sets it (#260), clamped to ±0.2: the head camera sees from this far above the character's eye; `vr.head.heightOffset` in the status (updated while the head drives the camera, not in the menu) |
| `pick [<entity>]` | what mouse picking (`FlyingOverObject`) finds, on an 8-pixel grid over the interface's screen (#72; the window's size in flat mode, the quad's own size in VR, #73): `pick.center` (the entity at its center), `pick.scan` (`<entity> <hits> <x> <y>` per entity found, most hits first) and, for an entity, `targetHits`, `targetHitX/Y` (the middle of its hits), `targetSeenX/Y` (where the camera prepared last — the head camera at the interface's size — sees the middle of its bounding box), `targetOffset` (the distance between the two), `targetBox` (its `bbox2D`) and `targetSeenInBox`. A thin item (a key) can fall between the grid points: `targetSeenInBox` still tests its box. Refused outside gameplay or for an unknown entity; one `[channel] pick:` log line |
| `recenter` | the head is measured from its next sample: a new head reference (#24; the player's button chord is #28). A `vr.head.reference reason=channel` event once a head is there |
| `headpose <yaw> <pitch> <roll> [<x> <y> <z>]` / `headpose off` | a simulated head for checks without a headset (#24): degrees as a person says them — yaw left, pitch up, roll left positive — and optionally the position in metres in the OpenXR LOCAL space (x right, y up, z back). It drives the first-person camera exactly like the headset (`vr.head` and `camera` in the status); the first pose takes the reference, later ones move the camera from it. A real headset pose wins over it; `off` removes it |
| `vr-point left\|right item [<kind>] [<dx> <dy>]`, `vr-point left\|right <x> <y>` | a simulated hand (`--vr-sim` only) turned so its aim ray hits the floating panel (#34) at the first item of the shown inventory bag - of that kind when one is named, e.g. `item potion_life` (#376) - (optionally that many pixels beside it — a hand moving while the trigger is held) or at a pixel of the 2D interface; only aims — what the pointer makes of the ray is the game's path. The target is in `ui.target`, `ui.targetX/Y`. Refused without the simulator, without a panel shown, or with no item in the bag |
| `vr-point left\|right container [<dx> <dy>]`, `vr-point left\|right bag` | the same on the panel (#38): `container` at the first item of the open container (a chest, a body; optionally that many pixels beside it — move it past the click dead zone, 16 px, before going far, or the drag starts far from the item, TRAPS.md); `bag` at the middle of the first free slot of the shown bag, and while an item is dragged so that its **icon** (which hangs beside the cursor, where the engine puts the item) is over the slot. Refused without a container open / without the bag shown or a free slot |
| `vr-point left\|right face <id>` | like `entity`, but at the middle of the entity's mesh face nearest the middle of its box that the hand's ray reaches without the level in the way (#38): a thin object — a lever on a wall, an open door — can be all air at the middle of its box, and a pulled lever's handle lies against the wall. Aim twice, a few frames apart: turning a simulated hand moves the start of its ray (TRAPS.md) |
| `freelook on\|off` | the engine's free look without the mouse (#323): `on` leaves the cursor mode, where the engine keeps the inventory open on the panel. In VR the game turns it on when it enters `GAMEPLAY` (#394); opening a chest or a body leaves it again (the engine's own rule). Refused without a level |
| `vr-radial point left\|right <item>` | the radial menus (#376, `--vr-sim` only, the hand's menu open): the simulated hand's grip moved to the middle of an item of its open menu - `center`, `r1` … `r3`, `l1` … `l3`, `game_menu`, `character`, `spells`, `map`, `quests`, `quick_save`, `quick_load`, `recenter`, `cancel_spell`; an item of the half-ring is reached through it (up over the ring first, then across three frames later), an item of the ring from the half-ring through the middle. Only moves the hand: what the menu makes of it is the game's path. `vr.radial` in the status |
| `vr-radial place hand\|gaze_when_off [<degrees>]` | where the radial menus open (#377), as `cfg.ini` sets it (`vr_radial_place`, `vr_radial_gaze_deg`, live, not saved): `hand` (the default) at the hand; `gaze_when_off` before the eyes at the hand's height when the hand is more than `<degrees>` (default 45) off the gaze - the hand still selects from where it opened. `vr.radial.<hand>.placed` (`hand`, `gaze`) and `place=` in the open event |
| `vr-hud frame on\|off`, `vr-hud keep <names>\|none`, `vr-hud levelup both\|radial\|wrist\|off`, `vr-hud reset` | the screen-frame HUD in VR (#469), as `cfg.ini` sets it (`vr_hud_frame`, `vr_hud_keep`, `vr_levelup`; live, not saved): the old frame back on the 2D interface, single elements kept there (names as in `vr.hud.drawn`, separated by commas or spaces; an unknown name is refused), the level-up indication; `reset` zeroes `vr.hud.drawn` |
| `vr-point left\|right menu <name>` | the same aim at the middle of a menu target (#394): in the main menu an entry (`resume`, `newquest`, `loadsave`, `options`, `credits`, `quit`, `language`), `back` (the open page's back button) or the **localisation key** of a text or a checkbox's label on the open page (`system_menus_options_interface`, `system_menus_options_video_crosshair`, `system_no`) — names, not texts: the game data may be in any language; in the character creation `quickgen`, `skin`, `done` or an attribute or skill of the book (`strength`, `mind`, `dexterity`, `constitution`, `stealth`, `mecanism`, `intuition`, `etheral_link`, `object_knowledge`, `casting`, `close_combat`, `projectile`, `defense`: the trigger adds a point, grip + trigger takes it back since #377); in the game with the book open its bookmarks (#377): `book_stats`, `book_spells`, `book_map`, `book_quests` (not the page shown). `ui.menuHover` says whether the engine sees the mouse on it. Refused outside the main menu, the character creation and the open book, or for a name not shown |
| `vr-point left\|right entity <id> [<dx> <dy> <dz>]`, `vr-point left\|right at <x> <y> <z>` | a simulated hand (`--vr-sim` only) turned so its hidden ray (#37) points at the middle of an entity's box in the world (optionally that many game units beside it) or at a point of the world (game units, y down) — from where the hand is this frame, through the inverse of the hand's mapping into the world. Only aims: what the reticle hits and what the engine makes of it is the game's path (`vr.reticle`). Refused without the simulator, for an unknown entity, or while the reticles are off (no ray in the world) |
| `watch <id>` / `watch off` | report one entity's place in `status.json` → `watch` (#37): where an item is, whether it is dragged, in the scene or in an inventory |
| `handpose left\|right <yaw> <pitch> <roll> [<x> <y> <z>]` / `handpose left\|right off` | a simulated controller for the body without one (#36): the grip pose in the same terms as `headpose` (degrees as a person says them, metres in the OpenXR LOCAL space); the body poses that arm towards it exactly as towards a located controller (`vr.body.<hand>` in the status, `source=sim`). A located controller wins over it; `off` removes it. Needs a head too (`headpose`, or a headset / `--vr-sim`), since hands are placed rigid with the head camera |
| `vr-sim <command>` | a command of the simulated headset (#112), only in a `--vr-sim` run: `head rot 25 0 0`, `head pos 0 -0.3 0`, `action trigger right 1`, `shot <name>`, `session visible`, `fault swap-eyes on`, … — now, or `@+<n> <command>` n frames from now on its timeline. See [The simulated headset](#the-simulated-headset---vr-sim-112) |
| `combat on\|off` | the engine's combat mode without a key (#75): what the weapon key does — `on` draws the weapon (ready clips), `off` puts it away; a strike is then the mouse button — or, with the weapon in the VR hand, a physical swing (#39: the trigger only charges). VR has no control for it (D-019); `vr.weapon.combatMode` in the status |
| `crouch on\|off` | the crouch toggle without an input device, for flat runs (#263): `on` crouches the character, `off` lets it stand up when there is room for its head. `player.crouchToggle` / `player.crouched` in the status |
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
`specialfx newspell 1` (the book icon's screen-space particles; the command reads a word after
`newspell`, without it a `[W] unexpected newline`), `human_base_0033.dodamage player 1000` on level 10 after `setplayercontrols on` (the player's death;
self-inflicted `dodamage` and damage while the controls are blocked do nothing), `attachnpctoplayer`
(an obsolete command: a `[W]` and a `script.error` event, no `[E]`).
A refused line costs nothing: `[W] [channel] script command failed: Unknown command: …` and
`errors=1` in the ack, no `[E]`.

In the test playground (area 30, #321.2): `playground pull mode|respawn` (the lever gets exactly what a hand pull
gives it: `SM_ACTION` from the player), `playground kill` (the NPC dies as from a hit), `playground place <ahead> [<right>]`
(the NPC stands there facing the hero, like `place`) and `playground hit <n>` (the hero deals n damage through
`damageNpc`, as a weapon hit). status.json has `playground` (`npc`, `npcMode` peaceful|aggressive, `npcAlive`, `npcLife`,
`npcLifeMax`, `npcAtSpot`, `items`) in area 30, and `player.level` / `player.allRunes` always. The things worked by hand
(#38), each with its `id`: `playground.leverMode` / `leverRespawn` (`pulled`), `playground.door` (`open`, `locked` —
the class script's `§open` / `§unlock`), `playground.chest` and `playground.corpse` (`open`: its inventory is the one
open on the panel, `items`: how many it holds; the body also `alive`).
The script command `revive [-i] [-t <entity>]` can revive another NPC (a dead one runs no script events).

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
                "layout":{"interact_left":"left.trigger",…,"jump":"right.stick_up",…}},
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

- `state`: `STARTUP` (before the first level or menu), `LOGO` (a publisher logo of a player's launch, #394),
  `MENU` (main menu, pause menu, credits, character creation), `LOADING` (written from inside the level loader — a
  level load is one long frame), `INTRO` (the level 10 intro's scripted scene; its `introduction.cin` is
  `CINEMATIC`), `GAMEPLAY`, `CINEMATIC`, `EXITED` (with `cleanShutdown: true`, written right before
  `Clean shutdown` is logged). `vr.panel.mode` is `screen` while a logo, the intro or a cinematic plays on the VR
  screen.
- `errors` / `warnings`: log lines at `[E]`/`[C]` and `[W]` level since the process started, counted
  by a logger backend — assert on these instead of grepping a buffered log.
- `hangWatchdog` (#440): `watching` (the [hang watchdog](#the-hang-watchdog-440) has read its symbols and checks
  the beats, about a second after the first frame), `thresholdMs`, `stalls` (stalls reported so far).
- `fps`: frames over the last status interval (0 in the final status).
- `notifications` (#370): the timed texts at the top of the 2D interface on screen now (a level script's
  tutorial hint, `herosay`, "cannot cast"): each stays 2 s + 60 ms per character of **game time** and is
  drawn on the HUD quad. A check that compares HUD pixels over time waits for `notifications eq 0` first
  (`mvp-new-game` starts with one for about 6 s; see TRAPS.md).
- `stickFor` (#370, after the first `stick-for`): `seq` holds started, `active` while the stick is held,
  `units` the distance asked for, `moved` the distance walked (the last hold's, once let go), `timedOut` the hold ended at its time limit instead, `walk` the stick
  walk of the hold's last walking frame - `walking`, `yaw`, `dirX`, `dirZ` as in `vr.input.walk`, kept after
  the release so a check reads them whenever it gets to it.
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
  names the eye size, the crop and where the flat frame quad lands in the window. `overlays` (#271): the quad
  layers after the flat frame (the menu pointer's beam, the hand reticles) the last mirrored frame drew over it
  (0 when none is in view of the window, which crops the eye to its aspect: the bag at the eye's bottom edge
  is outside it until the head looks down). A channel `shot` is taken after the mirror is drawn, so in VR it
  shows the window as a spectator sees it. While a menu without the world is shown — the main and pause menu, the
  character creation, the credits — the window shows the flat frame itself stretched over the whole window instead
  of the eye (#334: in the eye the menu is the floating panel, a trapezoid at the bottom, while the desktop mouse is
  mapped onto the whole interface); `overlays` is then 0. One log line on each switch: `[vr] desktop mirror: a menu
  without the world - the window shows the 2D interface 918x689 filling it` / `the window shows the right eye again`.
- `capture` (#334): the last `shot`'s `name`, `width`, `height` and, per quarter (`topLeft`, `topRight`,
  `bottomLeft`, `bottomRight`), the pixel stats and verdict of the simulated headset's shots (`verdict`, `mean`,
  `stddev`, `black`, `background`). A menu that fills the window is `world` in all four; the menu on the panel seen
  from the eye left the top quarters `uniform` (the eye's background).
- `vr.panel` (#49): the floating UI panel. `mode`: `world` while a menu is open and the panel is placed
  in the play space, `head` otherwise (the head-locked quad - in VR without the HUD frame since #469; also
  while a menu waits for a tracked head); `shown`: the panel is shown for an open menu (#469: only for the
  real panels, never in play); `menu`: the open menu (`mainmenu`, `charactercreation`, `credits`, `note`, `book`,
  `container`, `inventory`) or empty; `placements` since the start and `lastPlacement` (`open`,
  `recenter`, `tracking`); `x`, `y`, `z` the panel's centre in LOCAL (metres; the simulated head starts at
  the origin, so `0, -0.3, -0.9` straight ahead with the defaults), `yaw` its heading (degrees, + left),
  `tilt` how far its face turns up to the eyes (18.4° for the defaults), `width` / `height` as last
  submitted, and the settings `distance` / `drop` (`vr_panel_distance`, `vr_panel_drop`, `vr_panel_width`
  in `cfg.ini`).
- `vr.pointer` (#34): the menu pointer. `shown`: the panel is shown, so the pointer is on; `active`: the
  owning hand's aim ray is on the panel — the engine's mouse is `x`, `y` (the 2D interface's pixels) and the
  beam is requested; `owner`: `left` / `right`, the hand whose trigger was pressed last (or that took over
  an idle cursor); `beam`: the beam was in the last submitted frame; `leftOnPanel` / `rightOnPanel`: each
  ray hits the panel; `clicks`: presses on the panel; `offPanelPresses`: presses with the ray off it,
  kept from the engine; `ownerChanges`. Since #37 the same arbiter covers the world: `shown` is still the
  panel, but `active` is also true while the owner's reticle is in the world (`x` is then in the world band,
  below); `leftOnWorld` / `rightOnWorld`: each hand's reticle is a world target; `worldClicks`: presses
  with the reticle in the world; `offPanelPresses` counts presses on no target at all.
- `vr.reticle` (#37): the hand reticles in the world. `on` / `off` (why not: `no session`, `menu`,
  `cinematic`, `controls blocked`, `not first person`, `no head reference`, `book`, `note`, `combat mode`,
  `magic`); `cursor`: the engine's mouse is a world reticle, standing for `cursorEntity`; `presses`;
  `acts` / `lastAct` / `lastActHand` (#38): a hand's act (grip + trigger, `act_left` / `act_right`) on the door,
  lever, chest, NPC or corpse under its reticle — the engine's double click on it (`vr.reticle.act` event);
  `hoverChanges`; `picks` / `blocked`: how often the engine's pick asked about a world pixel / an interface
  pixel (which never picks the world while the reticles are on). Per hand (`left`, `right`): `aimed` (an aim
  pose), `hit` (the ray hits something within 20 m), `onWorld` (a target for the mouse: hit, not on the panel,
  in front of the head), `ring` (asked for this frame), `shown` (in the last submitted frame), `bright` (over
  an item that can be taken), `under` (the interactive entity in reach under the ray this frame), `entity`
  (the hover after the hysteresis: 2 frames in, 150 ms out), `distance` (game units), `pixelX` / `pixelY`
  (the engine mouse for the hit: the projection with the head camera plus 16384 in x — the world band, where
  no widget of the interface is), `origin` / `direction` / `point` (the ray and its hit in the world).
- `ui` (#34): the 2D interface under the mouse. `mouseX`, `mouseY`: the engine's mouse on the interface
  (in VR the pointer's pixel while it is active); `mouseInWindow`; `hover`: the item under the mouse
  (`FlyingOverIO`) or `none`; `drag`: the item being dragged or `none`; `container`: the chest or body whose
  inventory is open (#38) or `none`; `target`, `targetX`, `targetY`: what the last `vr-point` aimed at.
  `player.inventoryItems` (#38): the items in the hero's bag.
  `menuHover` (#394): in the main menu and the character creation, the name a
  `vr-point ... menu` aimed at while the widget under the mouse is that one — what a click lands on; `other` for
  another widget of the main menu, `none` for nothing (a greyed-out entry counts as nothing: the engine does not click
  it); `menuTargetEnabled`: the aimed main menu entry can be clicked (Resume game is greyed out without a save).
- `watch` (#37): the entity named by the `watch` command — `id`, `found`, `show` (the engine's show state,
  0 = in the scene), `inScene`, `inInventory`, `dragged`, `x`, `y`, `z`.
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
- `vr.body` (#36, D-009): the full body — the player model drawn under the tracked head with the arms
  posed towards the controllers, after the last update of the player's skeleton. `bound`: arm chains
  were found on the model (`model`, `chains`: the group names, `right_arm>right_elbow>right_wrist>right_hand`
  on `human_base`); `active`: the body was shifted and at least one arm posed this frame, else `inactive`
  says why (`no head`, `head not referenced yet`, `no tracked hand`, `no arm chain on the model`);
  `posedFrames`; `shiftX/Z`: the head's lean applied to the model (units, = `vr.head.offsetX/Z`);
  `facingYaw`: where the model was turned to face — #25's body yaw (`vr.head.bodyYaw`), the heading
  plus `vr.head.bodyOffset`.
  Per hand (`left`, `right`), the two poses of research row F-185: `source` (`xr`: a controller located
  for this frame, `sim`: the channel's `handpose`, `none`), `tracked`, `posed` (the IK drove that arm;
  `false` = the engine's animation), the controller in LOCAL (`x`, `y`, `z` metres, `yaw`, `pitch`,
  `roll` engine convention) and in the world (`controller`: the grip, units), `target` (where the wrist
  should be for the model's palm to sit on the grip), the posed joints `shoulder`, `elbow`, `wrist`,
  the landmarks `palm` (the attach vertex, on the grip when reached) and `tip` (the fingertips) after
  the vertices were transformed, `reach` (shoulder to target), `armLength` (the model's, 58.1 units on
  `human_base`), `error` (wrist to target: 0 when the controller is reached), `slide` (the shoulder
  moved towards an out-of-reach target, up to 20) and `stretched`.
  Where the hand's roll went (#304, D-050, degrees): `handRoll` (the hand's roll about the forearm
  against the base bend plane, unwrapped past a half turn), `elbowSwing` (how far it swung the elbow
  about the shoulder-target line), `residualRoll` (what is left against the final bend plane),
  `forearmRoll` and `upperRoll` (each bone's roll off that plane), `wristTwist` (= residual − forearm:
  the band between the forearm and the hand), `elbowTwist` (= forearm − upper: the band at the elbow;
  the shoulder band shows `upperRoll`), `rollOverflow` (0, +1, −1: the side the roll continues on past
  a half turn). The shares in use are at the body level: `forearmShare`, `upperShare`, `elbowShare`,
  `twistOverflow` (cfg.ini `vr_arm_forearm_twist`, `vr_arm_upper_twist`, `vr_arm_elbow_roll`,
  `vr_arm_twist_overflow`). The joint numbers alone cannot see a twist (TRAPS.md, 2026-10-10): judge
  the arm's roll by these, and the mesh by a capture.
  The fingers (#305, D-041), per hand under `fingers`: `source` — `joints` (the hand joints of
  `XR_EXT_hand_tracking`), `controls` (no joints: the trigger is the index finger, the grip the three-finger
  block, the thumb's rest a closed thumb), `held` (a weapon in the hand: closed on the grip whatever the
  fingers do), `none` (the arm is the engine's, or the model has no finger groups — `groups`); `joints`
  (the runtime located this hand's joints this frame), `holds`; `curl` — the bend of each segment in degrees
  (`thumb`, `indexRoot`, `indexTip`, `fingersRoot`, `fingersTip`; the limits are 60 / 90 / 95 / 90 / 95, a fist)
  approaching `target` (what the source asked for) at 720°/s. `tipAlong` / `tipOut` next to the landmarks: the
  fingertip vertex from the wrist along the hand's long axis and out towards the palm's face (units) — a closing
  hand brings the tip back and out. `fingerGroups` names the groups found per hand (`human_base`: `thumb
  right_hand_thumb, index right_hand_index_root>right_hand_index_tip, block right_hand_fingers_root>right_hand_finger_tip`).
  The custom glove hand's five fingers (#385.4), per hand under `custom.fingers`: `source` (`held` | `joints` |
  `controls` | `none` as above; `none` also while the custom arm is not posed) and per finger (`thumb`, `index`,
  `middle`, `ring`, `little`) `root`, `mid`, `tip` (the three joints' bend, degrees, 0 straight; the limits are
  90 / 100 / 70, the thumb 40 / 50 / 70; the thumb's first joint is read past its 30° rest) and `spread` (the
  knuckle's sideways turn, + toward the thumb), approaching the source's pose at 720°/s.
- `vr.hands` (#305): `XR_EXT_hand_tracking` — `extension` (the runtime lists it and the instance has its
  functions), `supported` (the headset reports hand tracking), `trackers` (one per hand on the session),
  `locates` / `failures`, and per hand `active` (joints located in the last frame) and `activeFrames`. All
  false in flat mode and on a runtime without the extension: the fingers then follow the controls.

- `vr.magic` (#43, D-010): the rune trail at the hand. `handsDraw`: the hands draw the runes (a VR session
  or a `handpose` hand) — the cursor's trail and the magic cursor are then off; `stance`: the casting stance
  (the magic action held and casting allowed: not crouched, controls free); `drawing`, `hand`, `strokes`
  (since the start); `length` (units = cm) and `ms` of the current or last stroke, `lastEnd`
  (`trigger`, `stance`, `lost`); `point`: where the trail is drawn (8 cm ahead of the grip, world units);
  `flares`: trail flares put at the hand since the start, `lastFlare` the last one's point. Per hand
  (`left`, `right`): `located`, `ignored` (a trigger pressed while the other hand drew, waiting for its
  release), `point`.
- `vr.radial` (#376, D-042): the radial menu (the right B by default, #377; `left` reports a `radial_left` a player bound).
  Per hand (`left`, `right`): `open`, `halfRing`, `hovered` (the item under the hand: `none`, `r1` … `r3`, `l1` … `l3`,
  `game_menu` …), `chosen` and `result` of the last release (`none`, `game_menu`, `book_open`, `book_closed`, `no_spells`, `quick_save`, `quick_load`, `recenter`,
  `cancel_spell`, `slot_filled`, `used`, `nothing_left`, `torch_out`, `empty_slot`, `closed` - released over nothing with the book, a container or a note open; the bag stays open, #377), `refused` (why the last press
  opened nothing: `menu`, `cinematic`, `no_head`, `hand_not_tracked`, `no_player`; else `none`), `opens`, `choices`,
  `placed` (#377: where the last menu opened, `hand` or `gaze` - `vr_radial_place`), `gameMenuKeys` (#377: refused presses that were the game menu key - `menu` (any menu, the credits and the
  character creation too), or `no_head` / `hand_not_tracked` in the game: the game menu opens, or goes back; a `vr.radial.gameMenuKey` game event each),
  `shown` (the menu was in the last submitted frame), `pointX` / `pointY` (the hand in the ring's plane, metres),
  `center` (the ring in LOCAL). `slots.r1` … `r3`, `slots.l1` … `l3` (#377: the kind each quick slot of the ring holds - the
  right half the right hand's, the left half the left hand's - `none` when empty; the save's `$vr_quickslots_right` / `_left`). `torchHand`: the hand whose menu lit the torch that burns
  now (`none`); `drawn`: menu images drawn; `images`: both menus' swapchains were made with the session (without
  them the menus work unseen - the owner's first headset run; the simulated runtime refuses a swapchain made while
  another one's image is acquired, as SteamVR does, TRAPS.md). With them (#376): `gameTimeMs` at the top (the game clock: it runs while a
  radial menu is open, stops in the pause menu), `player.bookPage` (`stats`, `spells`, `map`, `quests`) and
  `player.torch` (the lit torch's id, or `none`).
- `vr.hud` (#469): the original screen-frame HUD in VR ([The HUD frame in VR](#the-hud-frame-in-vr-469)).
  `vr`: a VR session is on, so the frame is off unless `frame` is `on` (`cfg.ini` `vr_hud_frame`, the comparison
  switch) or the element is in `keep` (`vr_hud_keep`, `none` for none); `frames`: HUD updates (frames the HUD
  was drawn in); `drawn.<element>`: per element the frames it was let through to draw - it draws when it has
  something to show (`health`, `mana`, `level_up`, `book`, `backpack`, `purse`, `active_spells`,
  `precast_spells`, `stealth`, `torch`, `steal`, `damaged_equipment`, `change_level`, `quick_save`,
  `memorized_runes`, `hit_strength`, `mechanism_hint`, `screen_arrows`, `minimap`, `notifications`, `speech`); 0 in VR play; `drawnTotal` their sum.
  `levelUp`: the level-up indication - `mode` (`vr_levelup`: `both`, `radial`, `wrist`, `off`), `pending`
  (points to spend, the flat icon's condition), `radialMark` (the radial menu's Character item marked),
  `wristNotice` (the gold notice on the wrist, 8 s after a level-up or until the points are spent),
  `notices` (how many started), `radialImages` / `wristViews` (radial images / eye views the mark / the notice
  was drawn into). `vr-hud reset` zeroes `drawn`.
- `vr.wrist` (#116, D-037): health and mana on the left wrist. `shown`: the left arm was posed from a
  controller this frame, so the bars are on the wrist and the HUD's orbs are not drawn; else `hidden` says
  why (`no left controller`, `left arm not posed`, `body not posed this frame`). `health`, `mana`: the
  fractions drawn; `facingDeg`: the angle between the inner wrist's normal and the direction to the eye of
  the last view drawn, `alpha` from it (1 within 50°, 0 from 80°); `drawnViews`: views the bars were drawn
  into (two per frame in stereo); `center`: the middle of the bars in the world; `normal` (out of the inner
  wrist), `across` (the direction the bars fill, towards the thumb), `along` (elbow → hand): unit vectors in
  engine axes (y down).
- `vr.weapon` (#75, D-033): the equipped weapon and the IK hand. `item`, `kind` (`dagger`, `1h`, `2h`, `bow`,
  `none`), `place` (`hand`: linked to the right hand by the VR body; `engine`: where the engine keeps it — the
  back, or the hand in combat mode; `none`), `inHand`, `sheathed` / `toggles` (#377, D-042: the weapon control - the
  right stick click - sheathed it, the hand is empty and its trigger drags; how often it drew or sheathed; one
  `vr.weapon.toggle result=drawn|sheathed|no_weapon` game event per press; the game starts drawn), `handFrames`,
  `relinks` (links this module changed);
  the blade read from the mesh: `measured`, `bladeLength` (units, attach vertex to tip), `assetAngle` (degrees
  between the asset's blade and the hand's handle axis in the fist), `correction` (the turn applied, = `assetAngle`);
  after the weapon was drawn: `drawn`, `hilt` and `tip` (world), `bladeGrip` (the blade's direction in the
  controller's grip axes, OpenXR: -Z is the handle), `bladeAngle` (degrees from grip -Z: ~0 when held right),
  `hiltToPalm` (units; its meaning depends on `frame`: `arx` - the weapon's attach vertex to the Arx hand's palm
  attach point; `glove` - the drawn handle anchor to the glove's hold point, the same as `hiltToGrip`);
  the weapon in the glove (#385.10): `frame` (`glove`: drawn from the glove's hilt frame with the custom, forearm
  and hands models; `arx`: from the Arx hand, the arx model), `weaponClass` (`sword`, `dagger`, `axe`, `mace`,
  `twoHanded`: from the item's type and class name), `handleLength` (units, the handle read from the mesh: pommel
  end to guard or head), `anchor` (units from the pommel end to the point held on the glove's grip axis: the class's
  row of the anchor table), `handWidth` (units, the glove's breadth: a two-handed weapon's anchor), `hiltToGrip`
  (units, the drawn anchor to the glove's hold point, ~0), `edgeToKnuckles` (degrees, the drawn edge to the glove's
  knuckle direction across the blade), `tilt` (the configured sword tilt: `0`, `-15`, `-30`) and `tiltApplied`
  (what the drawn weapon has: the tilt for a sword in the glove, else 0); the anchor, the distances and the edge are
  0 with `arx`;
  the engine's arm layer (F-67): `armAnimSuppressed`, `armAnimation` (the clip it plays), `suppressedFrames`;
  `combatMode`.
- `vr.melee` (#39, D-038): physical melee. `active` (swings count now) or `refused` (why not: `no weapon in the
  hand`, a menu's name such as `inventory`, `controls blocked`, `an item is dragged`, …); the swing detector:
  `swinging`, `kind` (`slash`, `stab`, `none`; of the swing in progress or the last one), `speed` (m/s of the point
  0.125 m along the blade, relative to the head, now), `peak`, `factor` (0.5..1), `swings`, `gaps` and `jumps` (samples
  refused: a time step outside 4-100 ms, a speed over 20 m/s), `nearMisses` / `lastNearMiss` (motions that peaked
  within 20 % below the swing speed, F-60); `charge` (`held`, `charging`, `spent` — a charged hit used it and the
  trigger is still held — `seconds`, `ratio` 0.1..1 while charging, `fullAimSeconds` = the player's `Full_AimTime`);
  `hits` (NPCs touched by swings), `contacts` (anything touched), `chargedHits`; `last` (`target`, `aim` — the
  engine's `ratioaim`: 0.25 × factor plain, the charge × factor charged — `damage`, `charged`, `speed`, `swing`);
  `params` (`swingSpeed`, `endSpeed`, `fullSpeed`, `weakAim`, `targetCooldown`, from cfg.ini); `placed`, `hilt`,
  `tip` (the weapon placed for this frame's check, world).
- `player` is present only while a level is loaded; the `.xrs` runner (#113) asserts on these keys
  with `@modassert`.
  `player.y` is the **eye** (the camera is within a few units of it), not the feet.
  `player.controlsBlocked` is `BLOCK_PLAYER_CONTROLS`: scripts set it during scripted scenes — the
  awakening of a new game on level 1 holds it for 30-40 s — and a save made while it is `true` is not
  playable (#70): wait for `false` (`@until player.controlsBlocked False` in a save scenario).
  `player.freelook` is `TRUE_PLAYER_MOUSELOOK_ON`: `false` is the engine's cursor mode, which keeps the
  inventory open (`player.inventoryOpen`) and refuses melee; in VR it is turned on whenever the game
  enters `GAMEPLAY` (#394, a `vr.freelook` event when it was off).
  `player.crouchToggle` is the crouch toggle (the button, `crouch on|off`); `player.crouched` is the
  character actually crouching (`PLAYER_CROUCH`) — it stays `true` after the toggle goes off while
  something is in the way of the head, and every refused stand-up logs one `player.standup.blocked`
  event naming what is there (#263).
  `player.stealth`: this update's movement is the stealth walk (the key or a slight stick push, #33).
  `player.animSpeed`: how fast the current body animation moves the player, units per second — the
  walk speed the engine uses, independent of walls and the frame rate (run 192, the stealth walk 135.6
  on `mvp-new-game`, #33).
- `camera` (with `player`): the first-person camera as the frame before left it — position, angles,
  `fov` in degrees, `bob` = its distance from the stable head position of the paused animation (view
  bobbing; 0 when off), `shake` = the quake amplitude applied that frame (0 without a quake or when
  screen shake is off). `camera.prepared`: the camera the last frame's interface pass ended with —
  its projection's scale `m00`/`m11`, off-axis shift `m20`/`m21` and viewport `width`/`height`; in
  VR the head camera's union projection, the same with the book open as closed (#247). `comfort`: who decides the VR comfort policy (`mode`: `auto` / `on` / `off`),
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
  (`squeeze/value`: on the Index how far the strapped hand is closed — TRAPS.md), `thumbTouch`: the thumb
  rests on the thumbstick (the `thumb_touch` action, #305: the fingers' fallback, no gameplay event), `presses`: how often
  each event came on since the start, `layout`: the binding of every event (`none`, `right.stick_up`,
  `left.grip+left.trigger`, `left.stick_click:long`; the defaults in [The default controls](#the-default-controls-d-042-377)). Nothing in the game reacts to the events yet (#74); `input-check.ps1`
  asserts on `presses` and `layout`. The VR settings' remapping (#178): `remapped` (the layout differs
  from the default of these controllers), `layoutFile` (where it is saved, in the config directory),
  `conflicts` / `conflictList` (events sharing a binding, `jump+inventory`), `capture.state`
  (`idle`, `waiting`, `holding`, `done`, `cancelled`), `capture.event`, `capture.binding` (what is held so
  far, or the result), `capture.refused` / `capture.why` (the last refused control), `capture.count`.
  `walk` (#31): the stick walk the player controls applied in the last update — `walking`, `yaw` (the
  stick's forward, engine degrees [0, 360): the look, D-008), `heading` (`player.yaw` then), `dirX` / `dirZ`
  (the world direction, unit length), `walks` (episodes since the start), `sneak` (#33: a slight
  deflection — the stealth walk; below cfg `vr_sneak_below` 0.6 of the dead-zoned travel a run turns
  into a sneak, at `vr_run_above` 0.7 a sneak into a run), `paceChanges` (sneak ↔ run switches within
  a walk since the start; a game event `vr.walk state=pace pace=…` each, and `pace=` on `state=start`).
- `menuPage` (#178): the page of the main / pause menu shown — `none`, `options`, `input`, `vr`, `vr-turning`,
  `loadsave`, `interface`, `quit`, `language` (#394) or `other`; `none` outside the menu.
- `hero` (#394, in the character creation and while a level is loaded): the attributes `strength`, `mind`,
  `dexterity`, `constitution` and the `skills` (`stealth`, `mecanism`, `intuition`, `etheral_link`,
  `object_knowledge`, `casting`, `close_combat`, `projectile`, `defense`) before modifiers, the points still to
  spend (`attributePoints`, `skillPoints`) and the `skin`. A fresh hero has 6 in every attribute and 16 + 18 points;
  the quick generation's first click makes the average hero (10 each, skills 2, nothing to spend), each later
  click a random one — one `chargen.roll kind=average|random` game event per click.
- `settings` (#394): `crosshair` — `config.interface.showCrosshair` (`show_crosshair` in `cfg.ini`), the setting
  `first-run-menu` changes by pointer clicks and its second launch reads back.
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
| `mvp-lever` | a sword equipped; `lever_0012` (opens `porticullis_0015`) 1.6 m ahead on the floor, in the hand reticles' reach | #38: pull a lever |
| `mvp-door` | a sword equipped; the closed portcullis `porticullis_0015` 1.6 m ahead, in the hand reticles' reach | #38: a door |
| `mvp-chest` | a sword equipped; `chest_metal_0052` 1.6 m ahead, in the hand reticles' reach | #38: open a chest |

`mvp-items` and `mvp-goblin-fight` were placed with `setpos` before it was known that `setpos` takes the
feet: their player stands 170 units above the floor (TRAPS.md); #417 regenerates them.

**A save at an object** (a lever, a door, a chest, an NPC): `approach <entity>` twice in the scenario, then
`save` (the three above; one run, no search). Each save has its reticle check, one launch each:

```powershell
.\scripts\dev\make-test-saves.ps1 -VrSim idle -Only mvp-lever,mvp-door,mvp-chest   # made in VR (the simulated headset); only these replaced
.\scripts\dev\make-test-saves.ps1 -Verify -VrSim idle                               # every save loaded in a fresh VR game
.\scripts\dev\smoke-test.ps1 -VrSim idle -Sequence reach-lever -Save mvp-lever      # the right reticle on the lever: under, hover; then a load with the sword held
.\scripts\dev\smoke-test.ps1 -VrSim idle -Sequence reach-door -Save mvp-door
.\scripts\dev\smoke-test.ps1 -VrSim idle -Sequence reach-chest -Save mvp-chest
```

`-VrSim <script>` makes (or verifies) the saves on the simulated headset instead of a flat game; `-Only`
copies only the named saves of the run into the library (every save is still made and loaded again).
`manifest.json` says how each save was made (`made`: `flat` or `vr-sim idle`).

**How a save is made** (`scripts/dev/make-test-saves.ps1`, scenario file
`scripts/dev/test-saves/scenarios.txt`): one game (flat, or `-VrSim`) from the main menu; `newgame`; `@until
player.controlsBlocked False`; per scenario `setpos` + `lookat` or `approach` + `save <name>`; then the **same game
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

## The test playground (#321)

One generated room for combat, magic and item tests, in minutes and again and again, without walking
the story. **Combat and magic headset tests run here.**

```powershell
.\scripts\run-windows.ps1 -Playground -StartSteamVR             # the owner in the headset: the playground, a fresh max-level hero
.\scripts\dev\test-pr.ps1 <pr> -Playground -StartSteamVR        # the same for a PR under test
.\scripts\run-windows.ps1 -Isolated -Playground -TestChannel -- --no-vr   # flat, with the channel
.\scripts\dev\smoke-test.ps1 -Playground                       # a worker's check: GAMEPLAY in area 30
.\scripts\dev\xrs-run.ps1 playground-levers                    # any sequence with @playground starts there
```

**What it is.** Our own level in area 30 (`PlaygroundArea`, `src/game/Levels.h`), built by
[arx-level-generator](https://github.com/arx-tools/arx-level-generator) (MIT) from `scripts/dev/playground/`
with the original game's textures by name. The repository holds only the generator; the level is
generated on the machine that runs it into `%LOCALAPPDATA%\ArxFatalisVR\playground\<folder>-<hash>`
(one per working copy) and passed to the game as `--data-dir <level> --loadlevel 30`. The first
`-Playground` launch generates it (node 20+ needed, a few seconds); later launches reuse it until a
generator source changes. No original level is touched: a launch without `-Playground` does not see it.

**The room.** The hero starts at the south wall facing north.
- **Ahead, the main table** (north wall): potions, food and a scroll of magic missile.
- **Left, the weapons table** (west wall): one of every weapon of the game (32 classes, the list in
  `layout.mjs`).
- **The goblin** in the middle of the room: **peaceful** by default — it stands and takes hits without
  fighting back.
- **Two levers** on the north wall to the right of the main table, pulled by hand like any lever (#38):
  - the **mode lever** (nearer the table): down = the goblin turns **aggressive** and attacks at once;
    up = **peaceful** again (it is also healed and goes back to its spot);
  - the **respawn lever**: the goblin is back at its spot, alive, with full life, in the mode it was in.
    The mode lever does nothing while the goblin is dead — pull the respawn lever.
- **Worked by hand** (#38) — each hand's reticle on it, then **grip held + trigger** (the act):
  - a **door** standing free ahead-left of the start (the game's light door, closed, not locked): opens,
    and closes again;
  - a **metal chest** by the east wall (right) holding a life potion and bread: opens on the panel; drag an
    item onto the bag;
  - a **dead goblin** a few steps ahead holding cheese and an apple: searched like the chest.
  An open chest or body closes when the hero walks away (as in the game).
- **The hero** is at the game's maximum level (10) and knows every rune; the level-up message shows at
  the start (attribute points to spend in the book).
- **Death costs nothing:** when the hero dies here he is back at the start at once, with full life and
  mana, no death screen, no load; his level, runes, inventory and the room stay as they were.
- **Damage numbers:** every hit on an NPC shows the damage over the target for about a second, in both
  eyes, drawn over everything. Only here — the original levels show none.

Checks: `playground-levers.xrs` (peaceful, the mode lever, the respawn lever, the hero's level and runes,
the items), `playground-hands.xrs` (#38: the lever, the door, the chest and the body by each hand's act,
an item from the chest and from the body dragged into the bag, the right hand then the left),
`playground-respawn.xrs` (death and back), `damage-numbers.xrs` / `damage-numbers-off.xrs`
(numbers in both eyes, none in an original level). The channel's `playground` commands and status fields
are under [Commands](#commands). CI installs node 22 with `actions/setup-node` (cached in the runner's tool cache, #412) and runs these
sequences; a machine without node (a local `ci-smoke.ps1` included) reports them **NOT RUN** with a warning in the job summary, never a pass. The tester zip does not carry
the playground yet (#359).

### Switching the arms: custom, forearm, hands, Arx or classic (#385, #466)

The VR body has five arm models (D-NEW-arms "Own VR arms", D-NEW-armvariants "a choice of four", D-051):
- **custom** (the default) — the leather glove hand (Godot XR Tools, CC0) and a cloth sleeve from the shoulder to
  the wrist, textures in `data/core/vr/arms/`;
- **forearm** — the glove and a short forearm: the sleeve from the wrist along the forearm, cut at 0.6 of its length
  and closed by a cuff before the elbow; no upper arm, no elbow;
- **hands** — the glove alone, its wrist opening closed by a cuff (Half-Life: Alyx);
- **arx** — the original Arx arms;
- **classic** — the original Arx arms with one elbow, the forearm one rigid piece (#466, below).

The IK, the reach and the roll rules are the same for all five; only what is drawn differs. **Flat mode and the
inventory book always show the Arx arms.**

**How to switch** (any one of these; live, saved to cfg.ini):
- **The arms lever** (a third lever on the lever wall of the playground, role `lever_arms`): each pull steps to the
  next model — custom → forearm → hands → arx → classic → custom. Its channel twin is `playground pull arms`.
- **The channel command** (`--test-dir`): `arms custom`, `arms forearm`, `arms hands`, `arms arx` or `arms classic`;
  `arms scale <factor>` sets the hand size.
- **cfg.ini** (read at start): `vr_arms_model` = `custom` | `forearm` | `hands` | `arx` | `classic` (default `custom`) and
  `vr_hand_scale` (default `1.15`, range `0.8` – `1.5`; the Arx hands are too small, so the custom hand is a little
  larger).

status.json shows the state: `vr.body.armsModel` (`custom` | `forearm` | `hands` | `arx` | `classic`), `vr.body.arxArmsHidden`,
`vr.body.<left|right>.handScale` and `vr.body.<left|right>.custom.*` — among them `gloveDrawn`, `sleeveDrawn` (custom
and forearm), `upperArmDrawn` (custom only) and `forearmLength` (the forearm as drawn: the fitted one for custom,
0.6 of it for forearm, 0 for hands). Sequences: `arms-model.xrs`, `arms-variants.xrs` and `arms-variants-lever.xrs`
(`.\scripts\dev\xrs-run.ps1 arms-variants`).

**The custom hand's five fingers** (#385.4, D-041, D-042) bend joint by joint, each about its own flexion axis read
from the glove's rest skeleton (`src/vr/HandRig.*`). They follow, in this order: a **held** weapon closes all five
on the grip (either hand), else the runtime's 26 hand **joints**, else the **controls** (trigger = index finger,
grip = middle, ring and little finger, thumb rest = thumb), else **none**. status.json:
`vr.body.<left|right>.custom.fingers.source` (`held` | `joints` | `controls` | `none`) and
`vr.body.<left|right>.custom.fingers.<thumb|index|middle|ring|little>.<root|mid|tip|spread>` (degrees, 0 straight;
root is the thumb's first joint or a finger's knuckle). Sequence: `finger-five.xrs`
(`.\scripts\dev\xrs-run.ps1 finger-five`). The Arx hand's three finger groups (`vr.body.<hand>.fingers.*`) are
unchanged.

**What to compare** in the headset, one arm model after the other, in the same spot (the playground):
- **Fingers:** size and thickness against your real hand; do they follow your finger poses (fist, point, open)?
- **Hand size:** try `vr_hand_scale` 1.0, 1.15 and 1.3 — which looks like your own hand?
- **The wrist and the elbow:** the seam between glove and sleeve (there is no bracer, the seam stays visible for
  now), twisting the wrist, bending the elbow — does the sleeve fold or tear?
- **Reach:** the arm should reach where your hand is without the shoulder sliding (arm length is fitted to your
  reach with a small stretch); the elbow drops when the hand holds a weapon.
- **The look:** dark worn leather glove and coarse cloth sleeve are a first proposal — say whether you want
  bare hands or the glove, and what colour and material the sleeve should be.
- **Anything near the eye:** the arms are not hidden when the hand comes close to your face; note if it bothers you.
- **Forearm and hands** (#385.8): does a missing elbow bother you less than a wrong one? Is the forearm's length
  right (it ends at 0.6 of the forearm)? How do the cuffs look — the forearm's end and the closed wrist of the hands —
  hanging, held ahead and with the wrist rolled 90° and 180°?

#### The classic Arx arm and its hand (#466)

**classic** is the fifth model (D-051): the original Arx arm with **one elbow** — the forearm is one rigid
piece from the elbow to the wrist instead of folding in its middle (the measured cause: [TRAPS.md](TRAPS.md),
2026-10-14). It is re-skinned in VR only; **arx** stays the engine's arm, for comparison. The lever cycle is
custom → forearm → hands → arx → classic → custom; the channel command is `arms classic`, cfg.ini
`vr_arms_model = classic`.

The classic arm's **hand** is a second switch, to compare the Arx hand with our five-finger glove on the same arm:
- **arx** (the default) — the Arx hand of the player model, its three finger groups;
- **glove** — the Arx hand is not drawn; the glove of the custom arms (five fingers, the runtime's joints or the
  controls, the hilt pose on a held weapon) sits at the Arx wrist, turned like the Arx hand, and the Arx forearm ends
  in its cuff (no sleeve). Its closed fingers lie some 30 degrees off the handle (`custom.curlAngle`) — the price of
  meeting the Arx wrist.

How to switch (live, saved to cfg.ini): `arms classic-hand arx` or `arms classic-hand glove`; cfg.ini
`vr_classic_hand` = `arx` | `glove`. It only matters with `vr_arms_model = classic`. status.json:
`vr.body.<left|right>.classic.hand` (the switch), `vr.body.<left|right>.classic.arxHandHidden` (the Arx hand faces
left out this frame — true only with the glove on a posed arm), `vr.body.<left|right>.custom.gloveDrawn` and
`vr.hands.<left|right>.hiltPose` (the glove's, as on the custom arms); the one elbow:
`vr.body.<left|right>.classic.creases` (bands bent past 20 degrees, 1 on a bent arm) and `.forearmTwistSpread`.
Sequences: `arms-classic.xrs` (one elbow) and `arms-classic-glove.xrs` (the hand switch, sword save `mvp-items`):
`.\scripts\dev\xrs-run.ps1 arms-classic-glove`.

**What to compare:** classic against arx (the second elbow gone? the elbow band with the hand rolled 90° and 180°),
then the classic arm with `classic-hand arx` against `classic-hand glove`: does the glove meet the Arx forearm without a
gap or a kink at the wrist, hanging, held ahead and rolled; is its size right next to the Arx arm (`vr_hand_scale`);
which hand do you want on the classic arm?

**The weapon in the glove** (#385.10, research #453). With the custom, forearm and hands models the held weapon is
drawn from the glove's hilt frame, not from the hidden Arx hand: a point of its handle sits on the line the glove's
closed fingers wrap, the blade runs along that line out of the thumb side of the fist, and the blade's edge points
toward the glove's knuckles (the wrist-to-knuckles direction: forward, F-489). The glove keeps its own place (past
the stretch limit it stops short of the controller) and the weapon follows it. With `arx` the weapon keeps the old
placement. Where along the handle the fingers close depends on the class (`src/vr/WeaponGrip.cpp`, the anchor table,
the research's first proposal): sword and dagger at the handle's centre, axe and mace (club, hammer) 20 % of the
handle from its end, a two-handed weapon in the right hand a hand-width above the pommel.

**The sword's tilt** — three variants for the owner to pick (the default 0 until then):
- **cfg.ini** `vr_sword_tilt` = `0` | `-15` | `-30` (degrees; another value snaps to the nearest), or live through the
  channel: `grip tilt 0`, `grip tilt -15`, `grip tilt -30` (saved to cfg.ini).
- The tilt turns the blade about the palm's normal, its tip leaning **forward, toward the knuckles** (the Index's
  pointing frame lies about 55° from its handle, F-488), in the plane of the edges. Only a sword leans; the other
  classes stay along the handle.

**What to compare** with a weapon in the hand (the playground's weapon table has every class):
- Does the handle sit inside the closed fingers — the guard just above the fist, the pommel below it?
- The edge: does the sharp side face forward (where the knuckles point) whichever way the hand turns?
- The tilt: 0, -15 and -30 with the sword held ready — which one feels like holding a sword?
- Axe, club and the two-handed sword: is the hand at the right place along the handle?
- With the hand stretched out past the arm's reach: does the weapon stay in the glove?

### Switching the body: full or hidden (#385.11)

Two variants of the own body in VR, to pick by feel: **full** — the whole player model, pushed back behind the
eyes so it never blocks the view (#385.9) — and **hidden** — no torso, legs, neck or head, only the arms and hands
(Half-Life: Alyx style): the custom arms (glove and sleeve) with `vr_arms_model = custom` (less of them with
`forearm` and `hands`), the Arx arms with their shoulders closed by a cap with `vr_arms_model = arx`. The held weapon, the wrist bars and the reticle are the same
in both. **Flat mode and the inventory book always show the whole model.** The floor shadow (the engine's blob
shadow under the player) stays in both variants.

**How to switch** (live, saved to cfg.ini):
- **The channel command** (`--test-dir`): `body full` or `body hidden`.
- **cfg.ini** (read at start): `vr_body` = `full` | `hidden` (default `full`).

status.json shows the state: `vr.body.mode` (`full` | `hidden`), `vr.body.drawnBodyFaces` (the body's faces the
last frame drew — torso, legs, the neck cap, the head; 0 in hidden) and `vr.body.drawnArmFaces` (the Arx arms'
faces and their shoulder caps; above 0 only with the Arx arms). Sequence: `body-hidden.xrs`
(`.\scripts\dev\xrs-run.ps1 body-hidden`).

**What to compare** in the headset, one variant after the other, standing and looking down: does the body (full)
still bother you, do floating arms (hidden) feel right, and with which arm model.
## The default controls (D-042, #377)

What a headset test plays with unless the player remapped (VR settings, D-039). The same on the Index and the Touch
(the Touch's X / Y are the left A / B); `vr/VRControls.cpp` `defaultLayout`, every event asserted
by `VRControlsTest::defaultLayoutTest`. Played through in `controls-v2.xrs` (the playground).

| Input | Event (`vr.input.layout`) | What it does |
|---|---|---|
| Trigger, empty hand | `interact_left` / `interact_right` | Click, grab and drag with that hand's reticle (on the panel: the pointer's click) |
| Trigger, weapon drawn in the hand | the same | Charges the strike (`vr.melee.charge`), no click in the world (the right hand until #380; on the panel it clicks) |
| Grip, then trigger | `act_*` and `use_*` (`left.grip+left.trigger`) | Use / interact: a door, lever, chest, NPC or corpse is acted on (`vr.reticle.lastAct`); an item is used - eaten, drunk, equipped (the original's right click). Not on the item into the bag any more. The trigger alone - also a double click - no longer opens doors, levers or chests |
| Grip alone | — | Nothing (the model's fingers only) |
| Right B held | `radial_right` | The radial menu (the right B only, the owner): one ring of 6 quick slots - the right half R1 (top), R2 (upper right), R3 (lower right) the right hand's, the left half L1 (upper left), L2 (lower left), L3 (bottom) the left hand's; above the ring the game menu, the book's pages, quick save / load, recenter, cancel the spell. A slot is filled by dragging an item onto it; used, a consumable is used (a torch lights in the slot's side's hand), equipment is equipped - a weapon from an L slot still goes to the right hand until #380 (`vr.radial.use ... note=weapon_right_hand_until_380`). Status: `vr.radial.slots.r1` … `l3`; a save of the first 7-a-hand layout keeps its first three per hand. Where it opens is a variant for the owner (`cfg.ini` `vr_radial_place`: `hand`, the default, or `gaze_when_off` - before the eyes when the hand is more than `vr_radial_gaze_deg`, 45, off the gaze). Every slot shows its side's letter, R or L, on its plus and on a filled slot alike (`vr.radial.sideLetters`). It opens over the open book or bag too, and the bag stays open with it: hold the right B, drag an item out of the bag with the LEFT hand (its trigger takes the panel's cursor) onto a segment, let B go - the slot holds it, the ring closes, the bag stays open. Released over nothing while the book, a container or a note is open, it closes them - never the bag, which only the left A closes (the owner, #377). Where no radial menu can open - in any menu (the game menu, its pages, the character creation, the credits), or in the game with the head or the hand not tracked - a press is the game menu key (Escape): the game menu opens, or goes back (`vr.radial.right.gameMenuKeys`) |
| Left B (Y on the left Touch) | `map` | The book on its map page; pressed again, closed (the engine's map key). No ring opens on the left B |
| Left A (X on the left Touch) | `inventory` | The bag: opened, pressed again closed |
| Right stick left / right | (the turn axis) | Turn, snap or smooth as set - unchanged |
| Right stick up | `jump` | Jump: up beyond 0.7 and more up than sideways (a diagonal turn does not jump) |
| Right stick down | `crouch_toggle` | Crouch on / off |
| Right stick click | `weapon` | Draw / sheathe the equipped weapon (`vr.weapon.sheathed`); the game starts with it drawn |
| Left stick | (the walk axes) | Walk along the look; the deflection sets sneak / walk / run - unchanged |
| Right A held | `magic_mode` | The casting stance: a trigger draws runes with its hand (#43); none while an item is dragged. Its press first closes everything open - the radial menus, the book, the bag, a container, a note (not the game menu; `vr.panels.closed` event). The left A is the bag |
| Trackpads, the Touch's left menu button | — | Unbound; the player may bind them (the Touch is the Index, the owner) |

Not bound by default: `radial_left` (the left hand's radial menu; a player may bind it - it opens the same ring), `book`,
`recenter` (both in the radial menu), `menu` on both controllers (the radial menu and the game menu key), `stash_left` / `stash_right` (D-042 dropped "straight into the bag"; a player can still bind it -
on grip + trigger it shares the chord with the act as the use does). The simple controller keeps its own layout
(select clicks; left menu = game menu, held = recenter; right menu = inventory, held = book).

What the D-042 table names but the game does not have yet - kept as below until its part lands:

| D-042 | Now | Lands with |
|---|---|---|
| A held: **each hand's** casting stance | One stance for both hands on the right A (`magic_mode`, the owner); the left A is the bag (the owner) | #378 (casting, two hands) |
| A weapon equipped from an L slot into the left hand | Equipped in the right hand, the engine's one weapon hand, noted in the log | #380 |
| Stick click: the weapon **of that hand** | The right hand holds the only weapon: the right stick click; the left stick click is unbound | #380 (a weapon per hand) |
| Two-handed weapons on both hands | A two-handed weapon is held by the right hand like any other | #380 |
| Two dragged items, one per hand | One dragged item (the engine's) | #379 |

## The HUD frame in VR (#469)

The flat game spreads its HUD along the edges of the monitor. In VR that frame was the 2D interface's rectangle -
on the head-locked quad in play, on the panel around the book and the bag - and the owner removed it
(2026-10-14, #464): in VR no element of it is drawn on the 2D interface, nor takes a click; the panel shows only for
a real panel (the bag, the book, a container, a note, a menu). Flat mode draws it as before. Every element and
where its function lives in VR (`vr/HudFrame.cpp`, host test `HudFrameTest`; status `vr.hud`, sequences
`hud-frame` and `hud-frame-flat`):

| Element (`vr.hud.drawn.*`) | Flat HUD | VR home |
|---|---|---|
| `health`, `mana` | the red and blue orbs | the bars on the left wrist (#116). Without a posed left arm (no left controller) the orbs come back on the head-locked quad - **owner question**: keep that fallback? |
| `level_up` | the level-up icon, a click opens the character page | a gold mark on the radial menu's **Character** item (the half-ring; on its faint arc while the half-ring is closed) while points wait, and a pulsing gold square beside the wrist bars for 8 s after a level-up - **a first proposal** behind `cfg.ini` `vr_levelup` = `both` (default) \| `radial` \| `wrist` \| `off` |
| `book` | the book icon; its flash for a new spell | the radial menu's Character, Spells, Map and Quests items (#376). The new-spell flash is not shown in VR - **owner question** |
| `backpack` | the bag icon (also a drop target) | the radial menu's Inventory item (#376) |
| `torch` | the burning torch's icon, a click puts it out | a torch in a radial quick slot lights it and puts it out (#376) |
| `purse` | the gold purse: the amount, a click takes gold out | **owner question** |
| `active_spells` | the running spells' icons, a click cancels one | **owner question**; the radial menu's Cancel spell ends the last one |
| `precast_spells` | the precast spells' icons, a click launches one | **owner question**: neither shown nor launchable in VR yet |
| `stealth` | the stealth gauge while sneaking unseen | **owner question** |
| `steal` | the steal icon at a container that can be stolen from | **owner question** (the container's panel still opens) |
| `damaged_equipment` | the damaged equipment figure | **owner question** |
| `change_level` | the zone-change icon at a level's exit, a click goes | jump goes (the default `quick_level_transition` = jump, right stick up); the "you can go" sign - **owner question** |
| `quick_save` | the quick save's flash | **owner question**: the radial menu's Quick save gives no confirmation |
| `memorized_runes` | the runes of the spell being drawn | **owner question**: only the rune trail (#43) |
| `hit_strength` | the combat mode's swing charge | **owner question**: the held trigger charges a swing (#39), nothing shows it |
| `mechanism_hint`, `screen_arrows` | the cursor mode's hint cog, the edge arrows that turn the view | none needed: the hands are the mouse and the head turns the view |
| `minimap` | the minimap in the top right corner | the radial menu's Map item opens the book's map page (#376); a map always in view - **owner question** |
| `notifications` | the info messages down the top left (tutorial hints, "you cannot …", the level-up message) | **owner question** (the owner, #469: they go to a VR element he picks); they still run their time and count in `notifications` |
| `speech` | the characters' speech text under a conversation's letterbox | **owner question** (the owner, #469); the voice still plays |

The switches, for comparing in the headset (`cfg.ini` [video], live through `vr-hud`): `vr_hud_frame = true` brings
the whole old frame back in VR; `vr_hud_keep = precast_spells, stealth` keeps single elements on the 2D interface
(the head-locked quad in play, the panel's edges with a panel open) until an element has its VR home.

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
| `startup.logo` (#394) | a publisher logo is shown / ends | `name=fishtank\|arkane`, `state=shown\|end`, `how=time\|skip\|key` |
| `startup.intro` (#394) | the level 10 intro starts / ends | `state=start area=10`; `state=end how=done\|skip` |
| `cinematic.start` / `cinematic.end` (#394) | a `.cin` plays / ends | `name`; `how=skip\|done` |
| `vr.screen` / `vr.screen.placed` (#394) | the VR screen (logos, intro, cinematics) goes on / off, is placed in the room | `state=on\|off`, `for=` |
| `vr.skip` (#394) | A, B or a trigger skips a logo, the intro, a cinematic or a scripted scene | `what=<state>\|SCENE` |
| `vr.freelook` (#394) | in VR the game enters `GAMEPLAY` with freelook off (a `--loadlevel` start, New game from the pause menu or a development launch, the return from the pause menu) and the VR side turns it on | `state=on`, `was=<state>` |
| `scene` (#394) | a scripted scene in the game (the letterbox outside a cinematic and the startup intro: the prison intro after New game) starts / ends | `state=start\|end`, `area`, `camera` (the scripted camera, `none` before it is set), `controlsBlocked` |
| `scene.skip` (#394) | Esc (`how=key`) or the VR skip (`how=vr`) skips a scripted scene: its scripts get `key_pressed` and their skip handlers end it | `how`, `area`, `camera`, `result=sent\|early` (`early`: a fade runs, or the letterbox has not been on for 3 s - nothing sent), `refused` (some script refused it: the speech plays on; the scene is still skipped, see TRAPS.md) |
| `chargen.roll` (#394) | a click of the quick generation | `kind=average\|random` |
| `player.death` | the player dies | `source` (entity id), `damage`, `type` (damage type flags) |
| `player.respawn` | the player died in the test playground (#321) and is back at its start, alive, in the same frame | `x` `y` `z` (the start), `fromX` `fromZ` (where he died), `life` |
| `player.combat` | the weapon is drawn / put away | `state=on\|off`, `weapon` |
| `player.jump` / `player.crouch` | a jump starts / the crouch toggle (`C`, the right stick down in VR) changes (#74) | `state=on\|off` (crouch) |
| `ui.inventory` / `ui.book` / `ui.container` / `ui.note` | opened or closed while playing | `state=open\|closed`; `bags=one\|all`, `page=stats\|spells\|map\|quests`, `entity` |
| `rune.drawn` / `rune.unknown` | a stroke was recognised as a rune / was not | `rune`, `moves` (numpad directions the recognizer saw), `index` |
| `spell.unknown` | the drawn runes are no spell | `runes` (`aam+yok`) |
| `spell.cast` | a spell launches (player or NPC) | `spell`, `caster`, `level`, `target` |
| `spell.precast` | the player stores a spell | `spell`, `level` |
| `spell.refused` | the player's spell does not launch | `spell`, `reason` (`runes-not-learned`, `no-mana` with `mana` / `cost`, `magic-disabled-by-level`, `spell-cannot-launch` — the spell's own silent check, `no-demon-within-900`, `not-implemented`) |
| `melee.hit` | a weapon strike reaches an NPC and the player is the attacker or the target — one per swing (a VR swing: one per target it touches, #39) | `source`, `target`, `weapon`, `damage` (0 = the armour-class roll made it a miss), `aim` |
| `damage.number` | a damage number over an NPC closes, only in the test playground (#321): every hit on an NPC shows its damage above the target for 1.2 s; hits on one target within 250 ms add up into one number, logged 250 ms after its first hit. `damageNumbers.visible` in the status: the numbers shown now | `entity`, `amount` (the hits' damage), `dealt` (the life taken: less on a killing blow), `hits`, `views` (the scene views it was drawn into in the last frame: 2 = both eyes), `x0` `y0` / `x1` `y1` (where in view 0 / 1 - the left / right eye image in VR - in pixels) |
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
| `vr.body.step` | the body starts walking after the tracked head, or stops (after 0.5 s without a step) (#26) | `state=start\|end`, `leanM`, `walkedM` on end (limit 100) |
| `vr.body.blocked` | the collision holds the body while the head goes on (a wall, a table), or lets it go (#26) | `state=start\|end`, `askedCm`, `movedCm`, `leanM` (limit 100) |
| `vr.head.wall` | the head goes into geometry - `inside` (its near plane touches a wall the game's eye does not) or `behind` (a wall between the character's eye and the head) - or comes out (`clear`) (#26) | `state=inside\|behind\|clear`, `leanM`, `fade` (limit 100) |
| `vr.head.far` | the head is far enough from a body that cannot follow to fade the world, or back (#26) | `state=start\|end`, `leanM`, `startM` (limit 100) |
| `vr.hand` | a controller comes into tracking or leaves it (#36) | `hand`, `state=tracked\|lost` (limit 200) |
| `vr.body.model` | the body bound to the player model: the arm chains found (once per model, also after an armor tweak) | `model`, `chains`, `bound` |
| `vr.body` | the body starts being posed from the controllers, or stops and why (#36) | `state=posed\|engine`, `reason` (`no head`, `head not referenced yet`, `no tracked hand`, `no arm chain on the model`; limit 200) |
| `vr.body.arm` | one arm switches between the IK and the engine's animation | `hand`, `posed`, `source=xr\|sim\|none` (limit 200) |
| `vr.body.fingers` | a hand's fingers change what they follow (#305) | `hand`, `source=joints\|controls\|held\|none`, `holds` (limit 200) |
| `vr.hands.trackers` | the `XR_EXT_hand_tracking` trackers were created on the session (#305) | `state=created` |
| `vr.hands.joints` | the runtime starts or stops giving a hand's joints (#305) | `hand`, `state=active\|inactive` (limit 200) |
| `vr.radial` | a radial menu opens (`state=open`, the ring's `x` `y` `z` in LOCAL), runs the item under the hand on the release (`state=chosen`, `item`, `result`) or closes unrun (`state=close`, `why`) (#376) | `hand`, `state` |
| `vr.radial.hover` / `vr.radial.refused` | the item under a hand changes; a press of B that opens nothing, with the reason | `hand`, `item` (limit 2000) / `hand`, `reason` (limit 200) |
| `vr.radial.slot` / `vr.radial.use` / `vr.radial.torch` | a dragged item fills a quick slot (`back=inventory\|dropped`); a slot is used (`result=used\|nothing_left\|torch_out`, `left`: how many of the kind are left, `note`: `weapon_right_hand_until_380` for a weapon from an L slot, else `none`); the torch lit from a slot burns, in that hand. `hand` is the slot's side (`slot` `r1` … `l3`, #377) | `hand`, `slot`, `item` |
| `vr.wrist` | health and mana move between the left wrist and the HUD's orbs (#116) | `state=wrist\|orbs`, `reason` (limit 200) |
| `vr.hud` | the screen-frame HUD's switches change, and once at the start (#469) | `vr=yes\|no`, `frame=on\|off`, `keep` |
| `vr.levelup` | a level-up notice starts: points to spend appeared, or the level rose again while they wait (#469) | `level`, `mode`, `radial=yes\|no`, `wrist=yes\|no` |
| `vr.panel.placed` | the floating UI panel was placed (#49): a menu opened (after > 250 ms closed), a recenter while it was open, or the head came back to tracking | `reason=open\|recenter\|tracking`, `menu`, `x`, `y`, `z` (LOCAL, metres), `yawDeg`, `distanceM`, `dropM`, `widthM`, `headY` |
| `vr.panel` | the 2D interface moved between the panel and the head-locked quad (#49) | `mode=world\|head`, `menu` (`none` when closed) |
| `magic.stance` | the casting stance starts or ends (#43): the magic action held with casting allowed | `state=on\|off`, `hands` (the hands draw: VR or a simulated hand; limit 200) |
| `magic.stroke` | a rune stroke starts or ends (#43) | `state=start\|end`, `hand`; at the end `reason=trigger\|stance\|lost`, `lengthCm`, `ms` (limit 500) |
| `vr.pointer` | the menu pointer went on or off with the panel (#34) | `state=on\|off`, `hand` (the owner), `tracked` (hands with an aim pose, 0-2) |
| `vr.pointer.owner` | the other hand took the cursor: its trigger, or the owner pointed away (#34) | `hand` |
| `vr.pointer.click` | a trigger pressed with the ray on the panel: the engine clicks there (#34) | `hand`, `x`, `y` (the 2D interface's pixels) |
| `vr.pointer.muted` | a trigger pressed with the ray off the panel: no click reaches the engine (#34) | `hand`, `reason` (`off the panel`; `no target` while the reticles are on: off the panel and at nothing in front, #37) |
| `vr.body.armAnim` | the clip of the player's arm layer changes, or starts / stops being suppressed while the IK drives the arms (#75, F-67) | `state=suppressed\|applied`, `anim` (the clip's path; limit 200) |
| `vr.weapon` | the equipped weapon goes into the IK hand or back to the engine (#75) | `state=hand\|engine\|none`, `reason`, `item`, `kind` (`dagger`, `1h`, `2h`, `bow`, `none`; limit 200) |
| `vr.weapon.grip` | a weapon's blade was read from its mesh (once per weapon and hand frame) | `item`, `bladeLength` (units), `assetAngle` (degrees between the asset's blade and the handle axis), `class` (the anchor's class, #385.10), `handle` (units) |
| `vr.weapon.tilt` | the sword's tilt in the glove was set (`grip tilt <deg>`, #385.10) | `tilt` (the variant: 0, -15, -30), `asked` (degrees) |
| `melee.state` | physical melee (#39) is switched on or off, and why | `state=on\|off`, `reason` (`allowed`, `no weapon in the hand`, `the right controller is not tracked`, a menu, `controls blocked`, `an item is dragged`, `dead`; limit 200) |
| `melee.swing` | a swing of the weapon in the IK hand ended (#39): one line per swing | `kind=slash\|stab`, `result=hit\|miss\|lost` (lost: tracking or the gate dropped it), `peak` (m/s), `factor`, `charge` (the ratio when it started), `aim` (of its hits), `charged=yes\|no` (its charge was spent), `hits`, `ms` (limit 1000) |
| `melee.charge` | the trigger's charge (#39): `start`, `full` (the ratio reached 1), `spent` (a charged hit used it), `released` (let go while charging) | `state`, `full` (seconds of `Full_AimTime`), `seconds`, `ratio` (limit 1000) |
| `melee.nearMiss` | a hand motion peaked within 20 % below the swing speed without becoming a swing (#39, F-60): the census to tune `vr_melee_swing_speed` | `peak`, `swingSpeed` (limit 200) |
| `vr.reticle` | the hand reticles went on or off (#37) | `state=on\|off`, `reason` (`game`, or why off — see `vr.reticle.off` in the status; limit 200) |
| `vr.reticle.hover` | a hand's hover changed, after the hysteresis (#37) | `hand`, `item` (`none` when it left), `takeable=yes\|no` (limit 500) |
| `vr.reticle.press` | a trigger pressed with the hand's reticle in the world: the engine clicks there (#37) | `hand`, `item` (the hover at the press, `none`), `distance` (game units; limit 500) |
| `vr.body.drag` | the head reached the 25° dead zone and pulls the body along, and when it let go (5° inside) (#25) | `state=start\|end`, `head`, `body` (degrees from the heading), `deadZone` (limit 100) |
| `vr.walk` | the left stick left its dead zone and the player walks, and when it came back to rest (#31) | `state=start\|end`, `yaw` (the stick's forward = the look), `heading`, `look` (yaw − heading, the head's turn) (limit 200) |

Not here yet, by design: the recenter chord with #28; locomotion, interaction and rune-drawing in VR add their own events in the
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
sessions share). The repository is **public** (since 2026-10-12), so every title, body and comment is
scrubbed of personal data on its way out ([below](#scrubbing-personal-data-387)):

| Situation | What happens |
|---|---|
| A new `[E]` / `[C]` signature, or a crash | An issue labeled `bug` + `auto-reported`: title from the signature, body with the build and branch, command line, exit code, how often, the stack or a log excerpt around the first occurrence, the digest; scrubbed of personal data (the home folder of every path is `<home>`). A hidden marker `arxvr-auto-report: sig=<hash>` in the body |
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

### Scrubbing personal data (#387)

`Protect-ArxText` in `log-digest-lib.ps1` replaces, in this order: a home folder (`C:\Users\<name>`,
`C:/Users/<name>`, `/Users/<name>`, `/home/<name>`) with `<home>`; the account id of a Steam `userdata`
path, a SteamID64, `STEAM_0:…` and `[U:1:…]` with `<steam-id>`; a network path (`\\server\share`) with
`<path>`; e-mails, GUIDs, Windows SIDs, MAC and IPv4 addresses with `<email>`, `<guid>`, `<sid>`, `<mac>`,
`<ip>` (not after `version` / `v` / `build` / `driver`, so a four-part version stays); and the Windows
account and machine names wherever they stand alone with `<user>` / `<machine>` (not inside a longer word:
the GitHub login stays). Everything technical stays: the engine's `file:line`, error texts, counts, the game
data and Program Files paths. The flush scrubs every queued item again, so items queued by an older script
go out clean too.

`Find-ArxPrivateData` is the **final check**: the same patterns plus what the scrubber does not know how
to clean — a `C:\Users` left over, a user folder outside the home (`D:\OneDrive - …\`, `\Desktop\`,
`\Documents\`, …), an identity value (`username=`, `steamid:`, `e-mail:` …). An item it flags is **not
uploaded**: it stays in `pending-reports\` with the reason in `lastError`, the flush goes on with the
others and prints a warning. Teach `Protect-ArxText` the new pattern (with a case in
`test-log-digest.ps1`) or delete the item.

`log-digest.ps1 -Preview` prints the queue as it would be uploaded, the refusals included, and sends
nothing. `scrub-auto-reports.ps1` runs the scrubber over the issues already on GitHub (all
`auto-reported` issues, open and closed, and their comments): a dry run lists every change, `-Apply`
edits in place (never deletes), and both re-scan the result with the final check. GitHub keeps an
issue's **edit history**: the text before a scrub stays readable there; only the author can delete a
revision, in the web UI (no API).

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

## The performance log (`arx-perf.log`, #327)

**Every run writes it, nobody has to ask:** `arx-perf.log` next to `arx.log` — the owner's runs
(`test-pr.ps1`, `run-windows.ps1`, the desktop shortcut) in `%USERPROFILE%\Saved Games\Arx Libertatis`,
a working copy's isolated runs (`smoke-test.ps1`, `--vr-sim`, `-Isolated`) in
`%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>`. The previous nine runs are kept as `arx-perf.1.log` ..
`arx-perf.9.log`, like `arx.log`; `ci-smoke.ps1` collects them with the `arx*.log` files. `arx.log`
names the file in a `[perf] performance log:` line. `--no-perf-log` turns it off.

**Read it with one command** — never by hand:

```powershell
.\scripts\dev\perf-summary.ps1                 # the last run: this copy's isolated one if it has a log, else the owner's Saved Games
.\scripts\dev\perf-summary.ps1 -Previous 1     # the run before it (arx-perf.1.log)
.\scripts\dev\perf-summary.ps1 -Path <file> -Top 10
```

```
Performance log: arx-perf.log
Run: 33 s with frames, 2761 frames, mean 11.66 ms (85.8 fps); display 90.0 Hz (11.11 ms) (33 of 33 s in VR); missed display slots 107; hitches 10; the log's own cost 0.9 us per frame (max 98 us)
Frame budget (mean ms per frame over the run, share of the mean frame):
  vrwait 9.66 (83%), render 0.78 (7%), load 0.40 (3%), update 0.32 (3%), physics 0.16 (1%), channel 0.15 (1%), input 0.09 (1%), present 0.07 (1%), other 0.02 (0%), script 0.01 (0%)
Worst seconds (by p95):
  t=   0 s  p95  214.01  max   233.9  mean  25.33 ms  frames  30  missed  32  hitches  2  vr    top: vrwait 10.89, channel 7.00, load 6.14
  t=   1 s  p95   14.14  max  1067.0  mean  63.72 ms  frames  20  missed  72  hitches  1  vr    top: load 45.47, vrwait 16.93, render 0.43
Hitches (10), frames over 1.5 expected frame times:
  by cause: vrwait 6, load 2, channel 1, input 1
  t=    0.914 s    214.0 ms (19.3 slots)  cause channel    missed 18  uploads 0  | channel 203.7, vrwait 9.6, render 0.4, update 0.2
  t=    1.336 s   1067.0 ms (96.0 slots)  cause load       missed 72  uploads 523  | load 909.4, vrwait 148.0, textures 6.3, channel 3.3
Marks:
  t=    0.824 s  before-stall
```

(a `--vr-sim` run with `stall 200` and `loadlevel 11` through the channel, shortened). Workers attach
this output to their reports when a change can cost frame time; for the owner's headset complaints
it is the first thing to run after the test.

**What is in the file** — one line per second, one per hitch, one per mark; times in ms since the log
started (`t`), phases in a fixed order:

```
S t=12 frames=90 min=10.88 mean=11.11 p95=11.52 max=12.40 hz=90.0 period=11.11 div=1 vr=1 missed=0 hitches=0 | other=0.02 input=0.09 channel=0.15 load=0.00 save=0.00 update=0.32 script=0.01 physics=0.16 particles=0.00 render=0.78 textures=0.00 present=0.07 limit=0.00 vrwait=9.50 vrend=0.01 | uploads=0 self=0.9 selfmax=28
H t=1.336 frame=212 ms=1067.0 slots=96.0 period=11.11 missed=72 uploads=523 cause=load | other=0.4 input=0.1 channel=3.3 load=909.4 ... vrwait=148.0 vrend=0.0
M t=0.824 before-stall
```

| Part | Meaning |
|---|---|
| `S` `min mean p95 max` | the frame times of that second: a frame is from the top of `ArxGame::doFrame` to the next one (in VR one OpenXR frame, D-024). p95 is the nearest rank: 95 % of the frames took at most that long. A second with no frame starting in it (a long load) has no line |
| `hz`, `period`, `div`, `vr` | the display: OpenXR's predicted display period in VR (`vr=1`), the monitor's refresh or `fps_limit` in flat mode; `div` = `vr_frame_divisor` |
| `missed` | display slots OpenXR's predicted display time jumped over beyond the expected `div`, counted for the frame that came late (the jump shows in the next `xrWaitFrame`) — the missed frames as the runtime saw them |
| `hitches` | frames of the second over **1.5 expected frame times** (period × `div`); each has an `H` line |
| phases | the mean ms per frame of each phase; they are **exclusive** (a level load started by a channel command counts as `load`, not `channel`) and add up to the mean frame |
| `uploads`, `self`, `selfmax` | textures uploaded to the GPU; the log's own work per frame in µs, mean and the worst frame |
| `H` | one hitch: its length in ms and in display slots, the missed slots it cost, its texture uploads, **`cause`** = the phase that took the most time, and every phase's ms |
| `M` | a channel `mark` |

| Phase | What it times |
|---|---|
| `input` | OpenXR events, `vr::beginFrame` without its wait (views, hands, the frame's eye targets), the controllers' actions, the engine's input |
| `channel` | the test channel: its commands (`stall <ms>` sleeps here on purpose — a forced hitch) and `status.json` |
| `load`, `save` | the start-up transitions, a new game, a level change, a save game load (`--loadslot`, quickload, `loadslot`, `loadlevel`), a quicksave |
| `update` | the game's update outside the phases below: player, entities, spells, damages, missiles, the HUD logic |
| `script`, `physics`, `particles` | the script timers and the script event stack; `ARX_PHYSICS_Apply`; the particle manager, particles, sparks, magical flares, poly booms — the effects that grow and are collected every frame |
| `render`, `textures` | drawing: the scene (per eye in VR), the interface, menus, cinematics, the VR layers and the desktop mirror; `textures` = `GLTexture::upload` (inside render or load) with its count in `uploads` |
| `present` | the window's buffer swap |
| `limit` | flat mode only: the engine's frame limiter sleeping towards `fps_limit` |
| `vrwait`, `vrend` | `xrWaitFrame` with the even-cadence cap's sleep — the headset pacing the game, mostly idle time; a long `vrwait` in a hitch is the runtime or the GPU making the game wait; `xrEndFrame` |
| `other` | what no phase claimed (the time update, the game events, the log's own work) |

Not measurable on the frame: the sound system mixes on its own thread, so its work never shows in a
frame's phases (it only competes for the CPU).

**Its cost:** the frame thread reads the clock twice per phase and appends a line to a memory buffer;
the file is written by a thread of its own, the frame never waits for it (it does not even wait for
that thread's lock: `PerfWriter`). Measured on two 30 s `--vr-sim` runs at 90 Hz (`idle.vrsim`,
`stall 200` and `loadlevel 11` first), the 3 s `[vr] rate:` windows at full rate: with the log
`game` 0.111 ms mean, interval sd 0.697 ms, worst frame 12.8–13.4 ms; with `--no-perf-log` 0.117 ms,
0.697 ms, 12.7–13.3 ms — no difference. The log's own work: 0.9 µs per frame on average (0.01 % of
11.1 ms), 98 µs in its worst frame of the run (the frame that formats a second's line), no hitch of
its own. A first build that took the lock in every `push` measured 574 µs per frame on average in
one second while the PC's CPU was starved by parallel builds (most likely the writer thread was
descheduled holding the lock); since then `push` only tries the lock.

The numbers are pure logic (`src/vr/PerfStats.h`, test `tests/vr/PerfStatsTest.cpp`: the second's
stats and p95, the 1.5-period threshold with and without the cap, no verdict without a period, the
cause, exclusive phases add up); the writer has `tests/vr/PerfWriterTest.cpp` (order, a push never
waits for a stuck sink nor for a held lock, drops when the sink falls far behind, nothing lost at
the end); the summary script has `scripts/dev/test-perf-summary.ps1` (in `run-script-tests.ps1`).

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
  size during the draw, so full-screen effects follow the eye image; sprite sizes follow the eye's
  projection (#72, below).
- `vr::submitFrame()` submits the eye images as the projection layer with the frame's poses and
  the quad in front; a frame whose scene was not drawn (a menu) clears both eyes to the dark
  background — both eyes or none (F-116). The desktop mirror (#21) copies the mirrored eye in
  `endEyeRender()` and the quad before its release and draws them into the window (the quad
  blended by its alpha, then the pointer beam and the reticles); with `vr_mirror = 0` the window shows the flat frame alone. Any other
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

**CPU-projected geometry per eye (#72).** Everything the scene pass projects on the CPU is projected
while it is drawn, so with the eye's camera: world sprites, particles, sparks, light flares, the
magic flares of rune drawing (world points 75 units in front of the head), spell effects, trails,
items and projectiles; animated entities are projected again for the eye. What #72 added:
- **Sprite size from the projection.** `EERIECreateSprite` scales by the prepared camera's
  `m_pixelsPerUnit` (`projectionPixelsPerUnit()`, `game/Camera.h`): exactly the old
  `min(width, height) / 480 · focal` for the engine's own camera, the eye's own vertical scale in a
  portrait eye image, where the old formula was 10 % short (host tests in `EyeProjectionTest`).
- **Picking reads the head camera.** The eyes leave their clip positions (and the items' `bbox2D`)
  in the entities, at the eye image's size; after the eyes `ProjectEntitiesForPicking()` projects
  them again with the head camera at the interface's size, where the mouse is (#73). `pick` measures it.
- **Screen-space particles on the flat frame.** `PARTICLE_2D` particles (the book icon's FX, the
  book's fire and rune sparkles) are window coordinates: in a stereo frame they are drawn with the
  interface on the quad (`ParticleLayer::Screen`), not into the eyes.
- **Additive light on the quad covers nothing.** A blend that keeps the destination (`dst = ONE`:
  additive glows and particles) leaves the quad's alpha as it is; it used to write the texture's
  alpha, so the black around an additive particle hid the world as a dark square.

`scripts\dev\xrs\tl-reprojection.xrs` is the check, on the simulated headset with the `mvp-items`
save: `.\scripts\dev\xrs-run.ps1 tl-reprojection` (`@save mvp-items`; see
[What was measured](#what-was-measured-owners-pc-index-steamvr-21710-2026-10-08)). Still with the
head camera: the visibility test of light flares (raycast from the head, once per frame) and the
interface itself; screen-space picking with the mouse stays until #37 replaces it with hand
reticles. The draw check is refused in a stereo frame: its two views would be the two eyes.

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
`hand left|right pos|rot …`, `hand left|right on|off`, `hand left|right fingers <thumb> <index> <middle> <ring> <little>`
(how far each real finger is closed, 0 open to 1 a fist — the hand joints the simulated runtime's
`XR_EXT_hand_tracking` trackers give, #305, five fingers #385; the three-value form
`fingers <thumb> <index> <rest>` closes the middle, ring and little finger together;
`hand left|right fingers off|on`: no joints for that hand, as
on a runtime that cannot read fingers, or joints again), `profile index|touch|simple|none|<path>`,
`action <name> left|right|both <value> [<y>]` (the game's OpenXR action names: `trigger`, `grip`,
`button_a`, `button_b`, `thumbstick`, `thumbstick_click`, `trackpad_press`, `menu`, `thumb_touch`), `session
focused|visible|idle` (`idle`: the headset is taken off - the session falls back to `synchronized`, the eyes are still located, nothing is shown, #27), `session exiting|loss-pending` and `runtime lost` (the runtime ends the session on its own - stopping, then exiting after `xrEndSession`, as SteamVR quitting the app; the session about to be lost; the runtime shutting down: the game goes on flat and names the cause, #146, see [The exit hook](#the-exit-hook-146)), `shot [name]`, `mark <text>`, `quit`, `fault swap-eyes on|off`, `fault exit|exit-process <code>` (the process ends from inside with `std::exit` or `ExitProcess`, the shape of #146 - only for `exit-trace-check.ps1`), `layers on|off` (shots also save each eye's projection layer without the quad, `<name>-left-layer.bmp` / `-right-layer.bmp`: what the game drew into the eye itself, #72). The defaults: Index
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
drawn); `quad.covered` counts the flat frame's pixels that cover the eyes (alpha at least half, #72).
`lr` has the left/right difference (mean luma levels on a 160-column grid), the horizontal
shift of the left picture against the right one (> 0 is right) and the eye separation; `versus` the
difference of each eye to every earlier shot. Per eye, `quadPixels` counts the eye pixels the first quad
(the flat frame / the floating panel) covers and `overlayPixels` those of every later quad layer, drawn
over it in order — the menu pointer's beam (#34), the hand reticles' rings (#37). `vr.sim` also has the rig, the eye poses as last
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

### Eye MSAA (`msaa.vrsim`, #226)

`vr_msaa` in `cfg.ini` (samples per pixel, 0 = off, default 4, D-035) or `--vr-msaa N`. The log says
`[vr] eye MSAA: 4x from vr_msaa in cfg.ini` and, per eye, `rendered with 4x MSAA, resolved into the
swapchain image`. `scripts\dev\vrsim\msaa.vrsim` draws the eyes at the Index's 2468×2740 **unpaced**
(`rate 0`): the `[vr] rate:` lines then show how many frames per second the PC draws (judge the
windows before the shot, not the first one, which holds the level's fade-in); the `edges` shot at
frame 6000 is for comparing edges between sample counts.

```powershell
.\scripts\dev\smoke-test.ps1 -VrSim msaa -Seconds 12 -- --loadlevel 10 --vr-msaa 0   # then 4, 8
```

| Date, PC | 0x | 4x | 8x |
|---|---|---|---|
| 2026-10-10, owner's PC (RX 6800 XT), shared with CI and other workers | 652 / 765 frames/s | 654 / 982 frames/s | 274 → 59 / 95 frames/s |
| hard edge steps (neighbours ≥ 64 luma levels apart) in the `edges` shot, left / right | 249 / 232 | 66 / 72 | (another moment of the scene) |

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
.\scripts\dev\smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence walk-forward   # the same through the smoke test, on a test save
.\scripts\dev\img-diff.ps1 -A a.bmp -B b.bmp [-Grid 16 | -Bands 16]   # two shots: mean difference 0-255, which regions changed
```

| Line | Meaning |
|---|---|
| `# ...` | comment (also after a step, as ` # ...`) |
| `<command>[; <command>]` | any [channel command](#commands) (`vr-sim head rot 25 0 0`, `vr-input tap right.a`, `loadlevel 10`, a script-console line); `;` makes one batch, applied in the same frame. A refused command fails the step |
| `@wait <ms>` / `@frames <n>` | sleep / wait for n more rendered frames |
| `@until <path> <op> <v> [<n>s]` | wait up to n s (default 30) until `status.json` satisfies the condition |
| `@assert <path> <op> <v>` | `status.json` now. `path` is dotted, `"quoted"` where a key holds a dot (`events.byName."spell.cast"`); `op` is `eq ne gt ge lt le`; numbers compare as numbers, anything else as text |
| `@remember <name> <path>` | keep a value; later `@assert` / `@until` values can be `{name}`, `{name+5}` / `{name-5}` (the yaw before a turn, the position before a walk) or `{name*0.9}` (a relative threshold: "at least 10 % less") |
| `@mark`, `@log <regex> [<n>s]`, `@nolog <regex>` | `arx.log` since the mark (the sequence start is the first mark): wait up to n s (default 5) for a matching line — a match moves the mark past it, so consecutive `@log` lines assert an order — / fail on a matching line |
| `@shot <name>` | on the simulated headset both eyes and the flat frame (`capture\<name>-left.bmp`, `-right.bmp`, `-quad.bmp`), otherwise `capture\<name>.bmp` |
| `@diff <a> <b> [max] <op> <v> [left\|right\|quad\|left-layer\|right-layer]` | `img-diff` of two shots: the mean absolute per-channel difference, or with `max` the largest change of one channel — what a small bright effect shows when the mean drowns in the scene's flicker (#72). `left-layer` / `right-layer` compare the eye's own layer without the quad (after `vr-sim layers on`). **The floor of a still shot of level 10 is about 1.8** (animated fire and HUD, measured), a 40° head turn with a following camera is far above; set thresholds against a still pair taken in the same sequence |
| `@key <name> [n] [ms]` | a keyboard message posted to the game window, no focus change (Escape, Enter, Space, A-Z, 0-9, F1-F12 …). **Not yet run against a live game** — verify it with a first sequence that needs it |
| `@pending #<issue> <step>` | a step the game cannot pass yet, because issue N builds the feature. Its failure is printed as `pending`; **once it passes the run fails** until the `@pending` is removed, so an expectation cannot go stale. Pick a step that fails for the right reason (a first version of `snap-turn` had a "turned back" step that passed trivially) |
| `@sim <name>`, `@args <...>` | what `xrs-run.ps1` launches: the `scripts\dev\vrsim` script (default `idle`: no timed lines, no shots) and the arx.exe arguments (default `--loadlevel 10`); ignored while running |
| `@player` | a player's launch (#394; `smoke-test.ps1 -Player`): no `--dev-launch`, so the logos, the intro and every cinematic play; without `@args` the sequence starts while the first logo is up (`LOGO`). `@args --dev-launch` is the other extreme: the main menu at once, no default `--loadlevel 10` |
| `@save <name>` | start from this save of the test save library (#70; `smoke-test.ps1 -Save`, the only save of the isolated user directory), with `@args --loadslot 0`. Anything the **body** does — turning, walking — needs a first-person save: on level 10 the intro conversation's camera owns the view (TRAPS.md) |
| `@playground` | start in the test playground (#321; `smoke-test.ps1 -Playground`: the generated level, area 30, a fresh max-level hero). CI sets up node itself (#412); on a machine without node it is NOT RUN with a warning |

Rules: a sequence waits for `state eq GAMEPLAY` and for `vr.sim.timelineFrame ge 270` (3 s) before its
first shot — the level fades in from black (TRAPS.md). A timeout must carry its `s` (`60s`); a bare
number is rejected, not taken for part of the value. Shots stall their frame, so the pacing line of
a run with shots reads UNDER-SUBMITTING — judge pacing without them. The shipped sequences:

| Sequence | Real today | `@pending` until |
|---|---|---|
| `look-around` | the rig and both located eyes follow a 40° yaw and a pitch; two shots of a still head agree; the pictures change with the head (#24) | — |
| `snap-turn` | on `mvp-new-game`, the repeat off: the right stick held for a second turns the body exactly 45° once (`vr.turn.snap`), the view turns (picture change 16.9), the head turned back 45° gives the first view again (5.3, HUD and lights); a second push, a push left, a half push that does nothing; the repeat 0.4 s on: held until the snap + 4 repeats, the interval the game measures by its clock (`vr.input.turn.repeatInterval`, 0.400–0.403) within 0.38–0.6 s — not a count of snaps, which depends on when the release reaches the game (#285); smooth turn by time and no snaps in it (#32) | — |
| `walk-forward` | the player stands still without input; the stick is held and let go | #31: the player moves |
| `recenter` | a head offset is set and read back | #28: `recenter` is accepted (it logs one expected `[W]`) |
| `finger-tracking` | the fingers follow the real fingers (#305): on `mvp-new-game` the simulated runtime's hand trackers are on (`vr.hands`), finger groups are found under both hands, the open hand bends nothing; pointing (`hand right fingers 1 0 1`) closes the thumb and the block to their limits and leaves the index finger straight, a fist closes every segment and brings the fingertip vertex back along the hand and out towards the palm (`tipAlong`, `tipOut`), half closed is half; with joints the trigger does nothing; `fingers off` → `source=controls`: a trigger pulled 0.4 bends the index finger to 36°, a grip of 0.45 the block to 40.5°, `thumb_touch` the thumb to 60°; `fingers on` gives the joints back; a controller off gives the whole arm back. Shots `hand-open-a/b`, `hand-point`, `hand-fist` of the hand before the face are for the eye (the fingers are a small part of the frame and the torch's flicker on the arm moves the whole-frame difference by up to ~0.5: no picture threshold beyond the still pair): `smoke-test.ps1 -VrSim idle -Sequence finger-tracking -Save mvp-new-game` | — |
| `finger-held` | a hand holding a weapon is closed on the grip (#305, #75): on `mvp-items` (a short sword equipped) the right hand's `source=held` with every segment at its limit whatever its joints say, the left hand follows its own joints; the right controller off gives the arm back, on holds again; shot `sword-fist`: `smoke-test.ps1 -VrSim idle -Sequence finger-held -Save mvp-items` | — |
| `finger-five` | the custom hand's five fingers (#385.4, D-041, D-042) on `mvp-items`: the left hand's fingers closed one at a time by its joints (`hand left fingers <thumb> <index> <middle> <ring> <little>`) bend only that finger's three joints (`vr.body.left.custom.fingers.<finger>.<root\|mid\|tip>`, the thumb's first joint read past its 30° rest), a fist bends all five; `fingers off` → `source=controls`: a trigger of 0.4 bends the index finger, a grip of 0.4 the other three; the right hand holding the sword is `source=held` with all five closed whatever its joints say; its controller off → `none`, on → `held`. Shots `finger-five-thumb` … `-fist` and `finger-five-weapon-fist`: `smoke-test.ps1 -VrSim idle -Sequence finger-five -Save mvp-items` | — |
| `arms-follow` | the full body (#36): both arm chains found, both arms posed from the idle rig's controllers with `error` 0; the hand takes the controller's orientation (the identity grip is a hand hanging down in the OpenXR grip pose: the wrist 8 units above the grip and the fingertips below the wrist; pitched up 90°, fingers ahead and thumb up: wrist and fingertips level with the grip); a hand moved 30 cm moves the wrist with it and changes both eyes' pictures of the body seen from above (> 2, floor < 1.5); 1.2 m out: `stretched`, `slide` 20; a controller switched off gives its arm back to the engine; a 30 cm lean shifts the body by the camera's offset and keeps `error` 0. **Needs the first-person save** (level 10's conversation camera is nowhere near the body): `smoke-test.ps1 -VrSim idle -Sequence arms-follow -Save mvp-new-game` | — |
| `arms-roll` | the roll-aware arm IK (#304, D-050) on `mvp-new-game` (`smoke-test.ps1 -VrSim idle -Sequence arms-roll -Save mvp-new-game`): the right hand hanging down and pointing forward, rolled 0 / 45 / 90 / 180° — the eight poses of the #298 baseline (`docs/vr/research/arms-roll.xrs`, section 16g) — with `error` < 1 in every pose and the roll shared along the arm: at 90° the wrist band and the elbow band each under 35°, at 180° under 70° (the whole 90 / 180 sat on the wrist band before); the hanging hand at the identity grip — not the model's rest relation, whose bands are 0 in the host test: its bend plane is the pole's, 30° from the hand's — keeps every band under 6°; rolled on from 170° to 200° the roll keeps its side (`rollOverflow` ≠ 0) and flips past 225°. Eight shots, made into a sheet by `scripts/dev/shot-sheet.ps1` (game captures stay in the CI artifact and the local capture folder, never in git) | — |
| `rune-trail` | the rune trail (#43) on `mvp-new-game`: the right trigger alone is a click (no stroke, no flare); the right A held (D-042, #377 - the owner until #378; the trackpad before) is the stance and draws nothing; the trigger in it draws with the right hand, the last flare at the trail's point; the hand moved 30 cm in six steps: `length` 30, more than 20 new flares, the point stays level; pitched up 90°: the point rises 8 units (the controller's tip, not the grip); the left trigger is ignored while the right draws and until released; the release ends the rune (`trigger`), no flare after it; a left-hand stroke ended by releasing the stance (`stance`); the drawn shots differ from a still pair in both eyes | — |
| `wrist-vitals` | health and mana on the left wrist (#116): with the idle rig the bars are on the wrist (orbs off) at full value; the left hand raised in front of the chest, knuckles right, the back of the hand up → `alpha` 0; the palm turned to the eyes like a watch → `alpha` 1, drawn into both eyes, the forearm to the right (`along.x` > 0.5: red left), filling upwards (`across.y` < −0.6); `specialfx heal -9` / `specialfx mana -6` → `vr.wrist.health` / `mana` drop (the picture judged on the captures: a still pair already differs by 2.2); turned 30° further → fading (`alpha` 0.61); the left controller off → `shown` false and the orbs back on the quad (quad picture changes). Needs the first-person save: `smoke-test.ps1 -VrSim idle -Sequence wrist-vitals -Save mvp-new-game` | — |
| `walk-forward` | on `mvp-new-game` (`-Save`; level 10 never moves the player, TRAPS.md): still without input, the stick held forward moves him | — (#31) |
| `walk-look` | on `mvp-new-game`: with the head turned ±90° the stick walks where the head looks (D-008), with the head ahead along the heading; the heading stays, one `vr.walk` start / end per push (`vr.input.walk` in status.json). Each push is `stick-for left 0 1 100`: bounded by the distance, so a busy PC cannot walk it into the start cell's wall (#370) | — (#31) |
| `sneak-jump-crouch` | on `mvp-new-game`: a full push runs, a slight one sneaks (`player.stealth`, `player.animSpeed` < 0.8 × the run's), the hysteresis band keeps a run, below it the walk turns into a sneak; `right.stick_up` with the stick drifting sideways jumps without turning (D-042, #377); `right.stick_down` likewise toggles the crouch on without turning, a tap toggles it off | — (#33) |
| `arms-follow` | the full body (#36): both arm chains found, both arms posed from the idle rig's controllers with `error` 0; the hand takes the controller's orientation (the identity grip is a hand hanging down in the OpenXR grip pose: the wrist 8 units above the grip and the fingertips below the wrist; pitched up 90°, fingers ahead and thumb up: wrist and fingertips level with the grip); a hand moved 30 cm moves the wrist with it and changes both eyes' pictures of the body seen from above (> 2, floor < 1.5); 1.2 m out: `stretched`, `slide` 20; a controller switched off gives its arm back to the engine; a 30 cm lean shifts the body by the camera's offset and keeps `error` 0. **Needs the first-person save** (level 10's conversation camera is nowhere near the body): `smoke-test.ps1 -VrSim idle -Sequence arms-follow -Save mvp-new-game` | — |
| `stand-up` | flat, on the test save `mvp-new-game` (`smoke-test.ps1 -Sequence stand-up -Save mvp-new-game`): `crouch on` / `crouch off` in the start cell — the character crouches (eye 170.1 → 122.7) and stands up where it stood (eye 170.1, same x/z); crouched inside the cell's portcullis it stays down with one `player.standup.blocked` event, and stands up once back at the start spot (#263) | — |
| `recenter` | a head offset is set and read back; `recenter` is accepted (#28) | — |
| `tl-reprojection` | picking reads the head camera after the eyes, the book icon's particles are on the quad and add no coverage (#72), on the save `mvp-items`; the book icon is kept for that part (`vr-hud keep book`: VR draws no HUD frame since #469) | — |
| `real-steps` (#26; run with `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence real-steps`) | 8 cm is a lean (the body stays); 1 m forward is a real step (the body walks 0.9 m, the lean stays 0.1 m, no fade); into the cell wall the body is held, the head goes `inside` then `behind` it, both eyes black; back out, the fade clears | — |
| `ui-panel` | the floating UI panel (#49) on the test save `mvp-new-game` — run with `smoke-test.ps1 -VrSim idle -Sequence ui-panel -Save mvp-new-game`: the head-locked HUD covers the same eye pixels with the head turned 40° (24105 = 24105; the pair waits for `notifications eq 0`, since the save's tutorial hint on the HUD expires by game time, #370); the inventory's panel is placed at (0, −0.3, −0.9) facing the eyes, stays there when the head turns (eye pixels of the drawn interface 152703 → 110911, back to 151950 with the head back), is placed anew on reopening and on a recenter. The 250 ms grace is below the channel's round trip (≈ 600 ms): host test `UIPanelTest` | — |
| `menu-pointer` | the menu pointer (#34) on the test save `mvp-items` — run with `smoke-test.ps1 -VrSim idle -Sequence menu-pointer -Save mvp-items`: no pointer and no beam without a menu (`overlayPixels` 0); the key from the table put in the bag, the inventory opened, the right hand aimed at it (`vr-point right item`): the engine's mouse on the key's pixel (192, 606 = the target), `ui.hover` the key, the beam drawn above the panel (`overlayPixels` 2125 left / 1997 right); the trigger clicks (`vr.pointer.click`), a hand move of 8 px keeps the cursor on the key (dead zone, no drag), 40 px drags **the key the click landed on**; released over the bag it goes back; the left hand's trigger takes the cursor and its click lands on the key too; both hands pointing behind the head (since #37 a hand pointing ahead in the game is a world reticle): no cursor, no beam, a press is muted (`offPanelPresses` 1, nothing dragged); closed: the pointer goes. Host test `MenuPointerTest` | — |
| `physical-crouch` | physical crouch (#100, D-034) on `mvp-new-game` — `smoke-test.ps1 -VrSim idle -Sequence physical-crouch -Save mvp-new-game`: the button crouches and stands at that spot first; the head 25 cm down follows the view and stands, 35 cm crouches the character (one `vr.head.crouch` episode), back at 25 cm stays crouched, 15 cm stands; the view never above the head nor twice as low; the crouch button still crouches and stands with the head up | — |
| `weapon-hold` | the weapon in the IK hand (#75, D-033): on `mvp-items` the equipped short sword is in the right hand without the combat mode (`vr.weapon.place` `hand`), its blade read from the mesh (65.8 units; the asset 9.1° off the handle axis) and drawn 0.1° from the grip's -Z, the hilt on the palm attach point; the handle pitched up puts the tip 66 units above the hilt; a moved hand keeps the hilt on the palm; a controller switched off gives the sword back to the engine (`engine`), switched on it is back; `combat on` + the trigger held: the arm layer's ready and wait clips are `suppressed`, no strike clip starts (since #39 the swing is the strike), the shoulder stays, the sword stays in the hand. The shots `sword-ahead`, `sword-up` and `sword-at-face` (the raised blade 0.1 m from the eyes, F-39) are judged by eye. With the default custom arms the weapon is drawn from the glove's frame (#385.10): "the hilt on the palm" is then the handle anchor on the glove's hold point (`hiltToPalm` = `hiltToGrip`). `smoke-test.ps1 -VrSim idle -Sequence weapon-hold -Save mvp-items` | — |
| `weapon-in-glove` | the weapon in the glove (#385.10, research #453 F-485, F-489) on `mvp-items`: with the custom arms the sword is drawn from the glove's hilt frame (`vr.weapon.frame` `glove`, `hiltToGrip` < 0.5, `edgeToKnuckles` < 5), held ahead, raised to the face and with the hand 1.5 m out past the reach (the arm stretched, the weapon still in the glove); the forearm and hands models take the same frame; `grip tilt -15` / `-30` lean the blade by 15 / 30° (`bladeAngle`), `0` puts it back, `grip tilt 40` snaps to a variant; `arms arx` gives the Arx placement (`frame` `arx`). `xrs-run.ps1 weapon-in-glove` | — |
| `weapon-classes` | one item of each class from the playground's weapon table in the glove (#385.10, F-487): long sword and dagger anchored at the handle's centre, axe and club 15 - 25 % from the handle's end, the two-handed sword a hand-width above the pommel; a capture of each for the owner. Playground only (not in CI): `xrs-run.ps1 weapon-classes` | — |
| `melee-swing` | physical melee (#39, D-038) on `mvp-goblin-fight`: the goblin held still and `place`d 80 units ahead, the sword at its chest; a slow sweep through it (under 1.3 m/s) is no swing; a plain swing (0.85 m in 8 frames) hits it once with aim 0.25 × the speed factor (`melee.hit`, `vr.melee.last`); the trigger held to a full charge (`melee.charge state=full`), a swing hits with aim ≥ 0.5 (measured 1.00 and damage 7.4 against 0.25 and 1.8) and spends the charge; held on, it does not charge again and the next swing is plain; released and pressed, it charges again; `combat on` + the trigger: no engine strike clip, no hit; the inventory open: no swing (`refused=inventory`). Shots `goblin-in-reach`, `after-hits` judged by eye. Broken on purpose (the charge never spent): fails at `vr.melee.charge.spent`. `smoke-test.ps1 -VrSim idle -Sequence melee-swing -Save mvp-goblin-fight` | — |
| `hand-reticle` | the hand reticles (#37) on the test save `mvp-items` — run with `smoke-test.ps1 -VrSim idle -Sequence hand-reticle -Save mvp-items`: in the game both hands' rings are drawn (`overlayPixels` > 0 in both eyes), dim, nothing hovered; the player put on the floor by the table (the save stands it on the wall top, TRAPS.md); the right hand at the note: `under` / `entity` the note, `bright`, the engine's hover the note; the left hand onto the form takes the idle mouse (D-019); the right trigger takes it back and presses on the note (`vr.reticle.press`), the hand moved: the note is dragged and follows the ring, released: `item.drop how=place` on the floor at the ring; the bag open, the left hand drags the form from the table onto the bag on the panel: `item.toinventory`; the sword sheathed with the right stick click first (a drawn weapon charges with the trigger, #377); grip + trigger on the note with the stash bound to that chord as a player can (no default since D-042): straight into the bag; the book open: the reticles off (`book`). Host test `HandReticleTest` | — |
| `playground-hands` | doors, levers, chests and bodies by hand (#38, D-016, D-042) in the playground — `smoke-test.ps1 -Playground -VrSim idle -Sequence playground-hands`: the door, the chest (two items), the dead goblin (two items) and the respawn lever are there (`entities _900`, `playground.*`); free look on and the bag closed (the playground starts in the cursor mode); with the **right** hand, then again with the **left**: the reticle on the lever (`under` / `entity`), grip + trigger pulls it (`playground.leverRespawn.pulled`, `vr.reticle.lastAct` / `lastActHand`); the door opened and closed (`playground.door.open`); the chest opened on the panel (`ui.container`), its first item dragged onto the bag (`player.inventoryItems` +1, `playground.chest.items` −1); walked off, the chest closes; the body searched the same way. 10 acts in all, and no act also clicked (`@nolog item.take` of the lever, door, chest or body). Host test `VRControlsTest` (the act shares the use chord by design since #377 - and a stash a player binds there: no conflict; any other event on it is one) | — |
| `controls-v2` | the default controls of D-042 (#377) played through in the playground on the simulated Index — `smoke-test.ps1 -Playground -VrSim idle -Sequence controls-v2`: the layout as the game has it (every event of [The default controls](#the-default-controls-d-042-377)); the left A opens and closes the bag (`player.inventoryOpen`, no ring, no stance), the left B the book on its map page (`player.bookPage` `map`, `vr.radial.left.opens` 0); the radial menu on the right B (`r1` on top) opens and closes the book (`player.bookOpen`, the character item twice) and the game menu (`menuMode` `mainmenu`), and in the game menu the right B is the game menu key back into the game (`refused` `menu`, `gameMenuKeys`); the right hand untracked (`vr-sim hand right off`): its B opens the game menu and goes back (`hand_not_tracked`); `menu` never pressed. The book from the half-ring: no bag with it, freelook stays; a trigger click on its quests bookmark turns the page in freelook (`vr-point right menu book_quests`); the radial menu opens over it and its half-ring switches the page; B released over nothing closes it (`result` `closed`), the reticles back; the bag: the ring opens over it and B over nothing leaves it open (`result` `none`), the left A closes it; the book and the bag open, a B tap over nothing closes the book only; the chest closed by B leaves the engine's bag open for the left A; with the book and the bag open the right A closes both and starts the stance (`vr.panels.closed`); the left A is the bag, never the stance; the game menu item closes the book first. The right stick through the simulated runtime: up 0.65 nothing, up 0.9 with 0.3 sideways a jump and no turn, 0.9 sideways / 0.8 up a turn and no jump, down twice the crouch on and off. The door: a double trigger click leaves it closed, no act; grip + trigger (left) opens it; the respawn lever by the right hand; grip + trigger on the apple pie: used (`watch.found` false), no act, nothing into the bag; on the short sword: equipped and drawn in the hand (`vr.weapon.inHand`, `vr.melee.active`); the trigger charges (`vr.melee.charge.charging`) with no world click; the right stick click sheathes it (`sheathed`, `place` `engine`, melee off), the empty hand's trigger drags the mana potion; the click draws it again (`toggles` 2); the left stick click does nothing (`recenter` 0) | — |
| `radial-menu` | the radial menu (#376, D-042; the owner's layout of #377) on `mvp-new-game` (not the playground, which CI runs only with node on the runner; `rune all` for the spells page) on the simulated Index: the layout (`radial_right` = right B, `radial_left` unbound, `map` = left B, `inventory` = left A, `book` unbound); the hands before the chest; the menu opens on the right B, is drawn, and released in the middle runs nothing; the left B opens no ring (`vr.radial.left.opens` 0) and toggles the book on its map page; the game time runs while the menu is open (`gameTimeMs` +1 s in 1.5 s, not paused); the hand over each of the 6 segments: `r1` `r2` `r3` `l3` `l2` `l1`; shots `one-open` (the menu in both eyes: `overlayPixels` above the closed shot's + 20000), `half-ring`, and the two-eyes check's `still-a` / `still-b` / `yaw25` of an open menu (`eye-check.ps1 -StatusFile <test dir>\status.json` after the run); the half-ring opens the book on `stats`, `spells`, `map`, `quests`, recenters (`vr.head.reference reason=radial`) and ends a running `magic_sight`; a potion, a torch and the prison's short sword put in the bag (opened and closed with the left A), the potion dragged onto R1, the torch onto R2 and, dragged by the left hand, onto L3, the sword onto L1; then the owner's flow - the right B held first with the bag open (it stays open), the LEFT hand's trigger takes the cursor (`vr.pointer.owner` left) and drags the potion onto R3 and the torch onto L2, the release closes the ring, the bag stays open, a B tap over nothing leaves it open, the R / L letters drawn (`vr.radial.sideLetters`) (`slot_filled`, `vr.radial.slot hand=left slot=l3`, the item back in the bag, nothing dragged once the trigger is let go; R3 stays empty); the potion used from R1 (`used`, then `nothing_left`), the torch lit from R2 (`torchHand` right), put out from L3 (`torch_out`), lit from L3 (`torchHand` left); the sword from L1 equipped in the right hand (`vr.weapon.item`, `note=weapon_right_hand_until_380`); quick save from the half-ring, a slot changed after it (`set $vr_quickslots_right changed`), quick load: the saved slots are back; a save of the first 7-a-hand layout (`set $vr_quickslots_left` with 7 entries): its first three are L1 … L3, shown and used (`nothing_left`) without a crash; the game menu from the menu (`menuMode` `mainmenu`), `menu resume`. Leaves a quick save in the user directory. `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence radial-menu` | — |
| `vr-remap` | the VR settings' remapping (#178) on the simulated Index controllers: `menu pause` + `menu vr` open the page; `jump` captured as `right.b` (a conflict with `radial_right` since #376: `conflicts` 1, shown in red), `magic_mode` as the chord `left.grip+left.a`, `crouch_toggle` as `right.stick_click:long`; the walk stick refused, a cancel. Leaves the layout saved | — |
| `vr-remap-saved` | the saved layout read back as a start reads it: `jump` captured as `right.b` (saved), `vr-input reset` (not saved), `vr-input reload` → `remapped`, `jump` = `right.b`; `vr-input default` → the file removed, `reload` gives the default. Every #178 sequence starts and ends with the default layout: CI runs all sequences in one user directory, in any order | — |
| `vr-settings-flat` | a plain launch (`@args --skiplogo`: the main menu) on the simulated headset — "VR" in the Options list after "Input", the VR page with the default Index layout, shots `options` and `vr-menu` (the name is from its first, flat version) | — |
| `vr-turning-settings` | the VR settings' turning and height page (#260) on `mvp-new-game`: `menu pause` + `menu vr-turning` open it; the snap angle slider's dot dragged to the middle of the bar makes the snap 60°, to the right end 90° (past it still 90°, one step left 85°) at once (`vr.input.turn.snapAngle`, a `vr.settings row=angle` game event); the turning row snap → smooth → snap, the speed slider at its right end "very fast" (240) and in the middle "medium" (120), the repeat slider off → 0.25 s → off, each read back from `vr.input.turn` at once; five `menu step height right` make the character 10 cm taller; back in the game (`menu resume`) `vr.head.heightOffset` is 0.1, `vr.head.offsetY` 10 units lower and `camera.eyeAboveFeet` 10 units higher, and one push of the stick turns 90°. Leaves `vr_snap_turn_angle=90` and `vr_height_offset=0.1` in the run's `cfg.ini` | — |
| `first-run-menu` | the main menu as a player meets it (#394, #334), a plain development launch (`@args --dev-launch`: the main menu after about a second): the flat frame has the interface's own size (918 × 689) and covers part of both eyes; the window shot is `world` in all four quarters (before the fix the top ones were the eye's flat background); every entry under the right and then the left hand's pointer (`ui.menuHover`; Resume game greyed out without a save: the mouse on it, `menuTargetEnabled` false); Load / save, Quit (No), the language page and the credits (left A back) opened and closed by clicks; Options > Interface > Crosshair turned off — `settings.crosshair` false, one click per press (`vr.pointer.clicks`); both hands behind the head: a press is muted (`offPanelPresses` + 1), nothing changes; back twice; the window resized to 1024 × 600: the interface laid out again, the window still filled, the pointer still on Options. Leaves `show_crosshair=false` in `cfg.ini` | — |
| `readback/first-run-menu-readback` | not a sequence of its own (one level below, so `ci-smoke.ps1` never runs it on a fresh user directory; it runs it itself right after `first-run-menu`, in that sequence's user directory): the **second launch** after `first-run-menu` in the same user directory — `settings.crosshair` is `false` at the start, read from `cfg.ini`; turned on again by pointer, so the pair leaves the directory as it found it. `smoke-test.ps1 -VrSim idle -Sequence first-run-menu -UserDir <dir> -- --dev-launch`, then `smoke-test.ps1 -VrSim idle -Sequence scripts\dev\xrs\readback\first-run-menu-readback.xrs -UserDir <dir> -- --dev-launch` | — |
| `first-run` | the whole first run from one player launch (`@player`, #394.3): logos skipped with A and the trigger, the level 10 intro on the VR screen skipped with A in `INTRO` (before `introduction.cin`, no `cinematic.start`), the menu with no leaked click (`vr.pointer.clicks` 0), shots `intro` and `menu` in both eyes, Options > Interface > Crosshair off by pointer and back, New game, the quick generation and Done by pointer clicks, the prison (area 1, `GAMEPLAY`, `player.controlsBlocked` false) with the made hero, a left-stick walk that moves `player.z` | — |
| `first-run-intro` | a player's launch up to the main menu (#394): the logos on the screen (`vr.panel.mode screen`, distance 2.5), A and the trigger skip them, the intro (`INTRO`) drawn flat, `introduction.cin` shown (`CINEMATIC`) and skipped with B through `cinematicEnd()`, no click leaks into the menu, New game, the prison and a walk | — |
| `first-run-chargen` | New game and the character creation by pointer clicks only (#394): New game clicked; a fresh hero (6 each, 16 + 18 points); the screen fills its panel and the window (all four quarters `world`); the trigger on strength spends one point (7, 15 left); both hands behind the head, the trigger and the grip change nothing; the quick generation makes the average hero (`chargen.roll` 1) and, clicked again, a random one (2); the grip takes a point back from strength and a skill point from stealth, the trigger spends them on dexterity and — with the left hand — on casting; Done starts the game with the rolled hero (`hero.strength` / `mind` kept into GAMEPLAY). Arx asks for no name | — |
| `level-load-hang` | a stalled level load is reported as a hang with its stack (#440): `watchdog 1000`, `stall load 3000`, `loadlevel 1` - `[hang] main thread stalled ... last beat at level load`, `platform::stallThisThread` and the load under it in the stack, `responsive again`, level 1 loaded, `hangWatchdog.stalls` ≥ 1 ([The hang watchdog](#the-hang-watchdog-440)) | — |
| `hud-frame` | VR without the old HUD frame (#469) on `mvp-new-game` - `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence hud-frame`: in play no panel (`vr.panel.shown` false), no pointer, none of the frame's 21 elements drawn, the quad empty (`quadPixels` 0) (`vr.hud.drawnTotal` 0); `vr-hud frame on` brings the bag and book icons back (the quad's pixels grow), `keep book` only the book; the bag and the book on the panel with nothing of the frame; `addxp` levels up: the game's level-up message is there (`notifications` > 0) but not drawn (`drawn.notifications` 0, the quad still empty), the radial Character item marked and the gold wrist notice (drawn into the eyes in the watch pose, on the arc of the open ring, a `vr.levelup` event), each `vr_levelup` mode, the notice gone after 8 s with the mark kept; the radial ring (the right B, #377's layout) opened six times over the bag and six times over the book, `vr.radial.right.shown` and `submitted` in every checked frame, `undrawn` none (#456's "ring not drawn", #474); released over nothing the bag stays open and the left A closes it, the book closes (#377). Shots `noframe`, `frame`, `bag`, `book`, `levelup-message`, `levelup-wrist`, `levelup-radial`, `ring-bag`, `ring-book` | — |
| `hud-frame-flat` | the HUD frame flat (#469), `--no-vr` on `mvp-new-game`: the orbs, the bag, book and level-up icons and the active spells drawn as before (`vr.hud.drawn.*` > 0); a level-up counts the flat icon and draws none of the VR indication | — |

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
| 2026-10-09, #72: `smoke-test.ps1 -VrSim idle -Sequence tl-reprojection -Save mvp-items` (simulated Index, 640x480 window = the interface before #73, head pitched 35° down at the table) | `PASS`, 24 of 24 steps. `pick key_base_0005`: seen at **308, 327**, its `bbox2D` **296 320 – 320 334** contains it (the key is too thin for the grid). `pick blank_form_0001`: **4** grid hits around 280, 304, seen at 283, 307 — **3.9 px** apart. `specialfx newspell 1`: the quad changes by **0.56** (two still quads: **0**), `quad.covered` **6315 → 6315** |
| 2026-10-09, #72: **the checks can fail** — the same sequence on a build without `ProjectEntitiesForPicking()` | `FAIL` at `targetSeenInBox`: the key's `bbox2D` is **597 1066 – 657 1113** (the right eye's, at 1440x1600) against 308, 327 seen in the window, and the whole 4800-point grid finds **nothing** |
| 2026-10-09, #72: the same on a build with the old quad alpha (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA` for every blend) | `FAIL` at the coverage step: `quad.covered` **6347 → 8626** — the book icon's additive particles cover 2279 more pixels of the eyes (dark squares around them) |
| 2026-10-09, #72: `eye-check.ps1`; `smoke-test.ps1 -Commands 'drawcheck 120'` (flat) | `PASS` on all six legs: pairing 428 / 428, separation 64 mm, difference 4.42 over a floor of 0.96, parallax +14 columns, yaw 25° with the pictures changed by 6.5 / 6.9. Flat: `PASS`, **0 of 120** frames differ, 0 errors, 0 warnings |
| 2026-10-09, #72 after merging #73 (the interface at its own 918x689 on the quad): the same `tl-reprojection` run | `PASS`, 24 of 24. `pick blank_form_0001`: **6** grid hits around 404, 440, seen at 406, 440 — **1.8 px**; the key seen at 442, 469 inside its box 424 459 – 460 479. The FX changes the quad by **0.21** (the same particles on a bigger quad; the threshold is now 0.1 over a floor of 0), `quad.covered` **7020 → 7020** |
| 2026-10-09, #72 review: the sequence with `vr-sim layers on` (28 steps) | `PASS`, 28 of 28. The FX: the left eye as seen (quad on top) changes by **max 245**, the eyes' own layers by **max 3** (left and right) against a still floor of **max 7** — the particles reach the eyes only through the quad. Picking as above (6 hits, 1.8 px) |
| 2026-10-09, #72 review: flat, `smoke-test.ps1 -Save mvp-items -Commands 'setpos 10700 2935 11400','lookat key_base_0005',…,'pick key_base_0005','pick blank_form_0001'` | `PASS`. The key seen at 308, 269 inside its box 294 261 – 321 278; the form **6** grid hits around 280, 244, seen at 280, 245 — **0.3 px**: flat picking unchanged |
| 2026-10-10, #72 after merging #49 (the play-space panel) and #27: the same sequence | Steps 1–22 pass (picking unchanged: 6 hits, 1.8 px; the key in its box). The eye as seen changes by **max 124** with the FX (the panel shows it smaller than the head-locked quad did): the threshold of 128 failed and is now **64**; the remaining steps read from the saved shots: the eyes' own layers **max 2 / 2**, `quad.covered` **7020 → 7020**, 0 errors. Not re-run with the new threshold (launch budget) |
| 2026-10-10, #72 after merging #36 (the player's body with IK arms) | CI failed the FX part: the arms follow the simulator's controllers and moved between two still shots (layer floor **max 121** looking down at the table, **86** looking level with the hands in front). The FX part now looks level with the hands hanging by the sides: `PASS`, 32 of 32 — floor **max 4**, the FX in the eyes' own layers **max 4 / 4**, in the eye as seen **max 224**, `quad.covered` **7020 → 7020** |
| 2026-10-09, #24: the owner's headset run of #215 (`test-pr.ps1 215`, shared user dir, `--skiplogo --loadslot 0`) showed a **black world with only the HUD** | Not the camera: the shared save folder was empty, so no level was loaded — `game.state` stayed `STARTUP` for the whole run, no `level.loaded`, rate line `game 0.1 mean` ms (the #21 run before it: the same). After the fix, flat `smoke-test.ps1 -- --skiplogo --loadslot 5` with one save: state **`MENU`** at 0.8 s (before: an empty level), one `[E] --loadslot 5: no such save, 1 save(s) in the save game dir - starting at the main menu instead of an empty level` (the smoke test fails on it, as it should). `test-save-library.ps1`: `Get-ArxLoadSlotProblem` refuses `--loadslot 0` with no saves and `--loadslot 1` with one, accepts `--loadslot 0` with one (6 new checks) |
| 2026-10-09, #24 on the simulated headset (#112): `eye-check.ps1` (level 10), then `smoke-test.ps1 -VrSim head-motion`, then `head-check.ps1` again | **Before** (the head applied in `updateFirstPersonCamera`): eye check `FAIL (yaw)` — both eyes turned 25°, picture changed **1.34** (floor 0.81, needs > 2.42); the `yaw25` flat frame identical to `still-a`: level 10's intro conversation camera overwrote the head (TRAPS.md). **After** (the head applied in `updateActiveCamera` to the camera the frame renders with, taken off again next frame): eye check **`PASS`** — world, pairing 428/428, separation 64 mm, difference 5.887 vs floor 0.844, parallax 16 columns, **yaw: picture changed 4.246 / 4.245**. `head-motion`: shots `center`, `right10cm`, `crouch30cm`, `turned` all **left world, right world, flat frame world**; the `turned` frame faces the right wall and more floor, `crouch30cm` sits lower. `head-check.ps1` still `PASS` with the same numbers |
| 2026-10-09, #27: `eye-height-check.ps1` (`--vr-sim eye-height`, `mvp-new-game`; simulated floor 1.7 m below the standing head) | `PASS`, 24 samples. **desk** (`session idle`, head 0.8 m above the floor): session `synchronized`, worn false, reference **provisional** at y −0.900, a settled one still pending. **standing** (put on at frame 200, lifted over 60 frames): **settled** 1.38 s after putting it on (`wornS`), refY **0.000**, `headAboveFloorM=1.700`, offset 0, view **170.07** units above the feet = the character's eye (`characterEyeM=1.701`). **crouch 20 cm**: offset y **+20.00**, view 150.07. **10 cm above**: refY **0.100**, offset 0, `vr.head.raised byM=0.100`. **back** at 0: offset **+10.00**. **off and on seated** (0.5 m lower): settled `reason="put on again"` at refY **−0.500**, 1.20 m above the floor, view at the eye again. The first run `FAIL`ed on a real defect (the event read the character's eye after it was cleared: `characterEyeM=unknown`). Host tests `HeadReferenceGateTest` (10 cases) fail 5 with the worn condition or auto-raise broken on purpose. `head-check.ps1`, `recenter-check.ps1` (with the #28 branch merged: `back` refY 0 = auto-raise), `eye-check.ps1` and the flat smoke test `PASS` |
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
| 2026-10-10, #305: `FingerCurlTest` (10 cases: the synthetic hand's frame, open / fist / pointing / thumbs-up / half closed from the joints, the same curl in any space and for either hand, the controls fallback, the closed hand, the approach step) and `SimRigTest::testFingers`; `smoke-test.ps1 -VrSim idle -Sequence finger-tracking -Save mvp-new-game` (117 steps), `-Sequence finger-held -Save mvp-items` (42), `arms-follow` again, the flat smoke test | Host tests `OK (241 tests)`; with the knuckle mapping broken on purpose (the wrong flexion scale) **3 cases fail** (`fist: indexRoot: expected 90, got 50.6`). `finger-tracking` **117 of 117** `PASS`: the simulated runtime lists the extension, `supportsHandTracking`, trackers for both hands, joints active every frame (`vr.hands.failures` 0); finger groups found under both hands (`thumb right_hand_thumb, index right_hand_index_root>right_hand_index_tip, block right_hand_fingers_root>right_hand_finger_tip`; the palm's face (0.08, 0.99, 0.10), finger axis (−0.11, 0.11, −0.99) in the wrist's rest frame); the open hand bends every segment < 1°; pointing (`fingers 1 0 1`) → thumb **60**, block **90 + 95**, index **0**; a fist → every segment at its limit and the fingertip vertex from **18.98 → 4.02** units along the hand, **−3.02 → +5.59** out towards the palm; half closed → knuckles **45**; with joints a trigger of 0.4 changes nothing; `fingers off` → `source=controls`, trigger 0.4 → index **36 / 38**, grip 0.45 → block **40.5 / 42.75**, `thumb_touch` → thumb **60**; `fingers on` → joints again; the controller off → `none`. The captures (the right hand 35 cm before the face, thumb up): the open hand a straight slab, the point an index finger ahead over three curled fingers, the fist all four curled towards the palm's face — whole-frame differences to the open shot **0.78** (point) and **0.31** (fist) against a **0.15** still pair, too small and flicker-prone for a threshold. `finger-held` **42 of 42** `PASS`: on `mvp-items` the right hand `source=held`, every segment at the fist whatever its joints say, the left hand on its joints; the controller off and on → `none` then `held` again. `arms-follow` **79 of 79** `PASS` (the fingers now straight instead of the wait animation's curl: the hanging hand's tip still below the wrist). Flat smoke test `PASS`; pacing MATCHED 90/s; 0 errors in every run; **5 launches**. SteamVR was in the owner's hands the whole session: no real-runtime run — the owner's test decides whether SteamVR's joints land as the simulator's do |
| 2026-10-10, #178: `xrs-run.ps1 vr-remap` (simulated Index controllers, level 10, the pause menu's VR page), then `xrs-run.ps1 vr-remap-saved` (a new start), then `xrs-run.ps1 vr-settings-flat` (the main menu; its first, flat version ran `smoke-test.ps1 -Menu -Sequence`) | `PASS` 63 / 63, 18 / 18 and 16 / 16 steps (after the merge of #27 / #28; a first version with `down` / `up` pairs recorded `right.b:long` — TRAPS.md): captures `jump` = `right.b`, `magic_mode` = `left.grip+left.a`, `crouch_toggle` = `right.stick_click:long`; `conflicts` 1 (`jump+inventory`, the two rows in red); `left.stick_up` refused ("the left stick walks"); the next start logged `layout saved for index (…\vr-controls-index.ini)` with the three bindings; `default` → `remapped` false, the file gone. Host test `BindingCaptureTest` (14 cases): broken on purpose (long press at twice the time) → `Failures: 2`; `brokenLayoutFileTest` (a bad line, an unknown event, an unknown control in line 2 after a good line 1 → `readSavedLayout` false, the error names the line, the layout is the default) with the all-or-nothing guard removed → `Failures: 2` (`brokenLayoutFileTest`, `VRControlsTest::layoutTextTest`) |
| 2026-10-09, #36: `ArmIKTest` (9 cases: the IK, the shoulder slide, hand placement against `headToCamera`, the hand frame from a mesh, `handToGrip`); `smoke-test.ps1 -VrSim idle -Sequence arms-follow -Save mvp-new-game` (73 steps); `eye-check.ps1`; a flat `smoke-test.ps1 -Save mvp-new-game` with `headpose 0 -60 0`, `handpose right/left 0 -90 0 …`, `shot` | Host tests `OK (173 tests)`; one case failed before the fold-at-the-shoulder fix (the elbow went across the hint) and `near` had to be renamed (a `windows.h` macro). The sequence: **73 of 73** `PASS` — both chains found (`right_arm>right_elbow>right_wrist>right_hand`, lengths **27.6 + 30.5**), `error` **0** for both arms with the idle rig's controllers; the identity grip (a hand hanging down in the OpenXR grip pose, after #75's review caught the axes turned 180° about the palm normal — grip −Z, not +Z, runs from the little finger to the thumb): the wrist above the grip by the palm's 7.9 units, the fingertips (curled) below the wrist; pitched up 90° (fingers ahead, thumb up): wrist and fingertips level with the grip (within 3 / 6); a hand raised 25 cm: `target.y` and `wrist.y` **−25**, `error` 0; the still floor of the body seen from above **0.68 / 0.64** (left / right eye), the raised hand changes the pictures by **2.43 / 6.67**; 1.2 m out: `stretched`, `slide` **20**, `error` **37.5**; a controller off: that arm `posed=false`, the other still posed; a 30 cm lean: `shiftX` **30.000** = `vr.head.offsetX`, `error` 0. Two sequence runs on `--loadlevel 10` before that showed **no body at all** in the shots (the intro's conversation camera, TRAPS.md), with the same status numbers. `eye-check.ps1` `PASS` (world, pairing 429/429, separation 64 mm, difference 4.28 vs floor 0.99, parallax +14, yaw 5.9 / 6.2). Flat with simulated head and hands: `PASS`, `vr.body.active` true, `source=sim`, `error` 0, 1015 posed frames; the shot shows the chest, both arms and the hands with the fingers ahead. A startup crash (`entities.player()` asked by the status writer before the player entity exists) was found by the smoke test and fixed. SteamVR was off the whole session: no real-controller run. After the owner's #27 run ("looking down I see through the hollow body"): a `body-straight-down` shot (pitch −89°) with the model drawn two-sided still showed the neck opening and the torso's inside through it; with the cap (`neck opening closed with 5 cap face(s) over 1 boundary loop(s) (7 boundary edges)`) the opening is a skin-coloured plate and the chest reads solid — 80 of 80 steps PASS, launches 12 (the last two asked for by the coordinator) |
| 2026-10-10, #304: `ArmRollTest` (9 cases: the swing-twist split, RENIK's overflow rule, the rest bend reference, the rest relation, the shares, a 170° roll, the overflow through the solver, a straight rest arm, the old rule); `smoke-test.ps1 -VrSim idle -Sequence arms-roll -Save mvp-new-game` (113 steps, the eight shots of the §16g baseline), `-Sequence arms-follow`, `eye-check.ps1`, the flat `smoke-test.ps1` | Host tests `OK (239 tests)`; with the forearm share forced to 0 (the old rule) the suite fails (the distribution cases) — proven. `arms-roll` **113 of 113** `PASS`: the hanging hand at the identity grip sits **30°** from the model's rest relation (its elbow swung 10°, wrist band **4.4°**); yawed 45 / 90 / 180° the bands stay at **0.2 / 5 / 20°** (wrist, elbow and shoulder alike — thirds of what is left after the elbow swing); the hand ahead rolled ±90 / 180° about the forward axis: **15 / 25 / 34°** per band; `error` **0** in every pose. The by-the-side hand yawed in steps: roll 123 → 165 → **209** (`rollOverflow` 1, continuing past 180) → **−100** after 225 (flipped, 0). The roll sheet against the #298 baseline sheet: no ribbon at the wrist at 90 / 180°, the elbow swings with the roll; the upper arm still reads thin from above at some elbow directions (its cross-section is a flat slab — the model, not the roll). Found on the way: `human_base` faces −z in mesh space (the right arm along −x, the thumbs on −z, the elbows bent 10° to +z at rest), so the rest triangle names the bend reference and a "model's back" prior of (0, 0, −1) turned the bend plane round (a 138° roll in the natural pose, dropped); #36's elbow pole (down, half out, a fifth back) put a hanging arm's elbow outward, 50° from the thumb's bend — a relaxed pole (down, back, 0.3 out) halves the natural pose's roll. `arms-follow` 79 of 79 (the hand-up picture change **5.0**, its threshold lowered from 4 to 2.5 after a 3.6 on the first pole), `eye-check` PASS (pairing 428/428, difference 4.24, parallax 14, yaw 5.9 / 6.2), flat smoke PASS. SteamVR off all session: no VR run. Launches 10 of 10 |
| 2026-10-10, #43: `RuneTrailTest` (6 cases); `smoke-test.ps1 -VrSim idle -Sequence rune-trail -Save mvp-new-game`; `eye-check.ps1`; a flat `smoke-test.ps1` | Host tests `OK (227 tests)`; the tip direction reversed on purpose turns them red. The sequence **108 of 108** `PASS`: no stroke or flare from the trigger alone or the stance alone; the stroke at the right hand with the last flare exactly at `vr.magic.point`; the hand moved 30 cm in six steps: `length` **30.0**, flares 25 → **188**, the point level (y unchanged); pitched up 90°: the point **8.0** units higher; the left trigger `ignored` while the right drew; ended `trigger` (no flare after it: 291 = 291), the second stroke ended `stance`; `magic.stroke … lengthCm=52.6 ms=6067` (30 cm plus the 90° pitch and back, 2 × 11.3). Pictures: the still pair **0.10 / 0.09** (left / right eye), drawn **8.07 / 7.82**, the line **10.83 / 13.09**. The first run failed one step only: the event log writes a bool as `yes`. `eye-check.ps1` `PASS` (difference 4.25, parallax +14, yaw 5.9 / 6.2); flat smoke test `PASS`; CI (runner service): `rune-trail` 108 of 108. SteamVR ran for the owner's batch: no real-controller run; launches 4 |
| 2026-10-10, #116: `WristVitalsTest` (5 cases: placement on the inner wrist, fill by value, the watch pose reads red-left / blue-right upright, the fade 50°→80°, degenerate arms); `smoke-test.ps1 -VrSim idle -Sequence wrist-vitals -Save mvp-new-game`; flat `smoke-test.ps1` | `OK (226 tests)`; **the suite can fail**: health and mana swapped on purpose → 2 failures (`testBarsOnTheInnerWrist`, `testWatchPoseReadsLikeTheFlatHud`). Sequence `PASS` 49 of 49: away `facingDeg` 148 → `alpha` 0; watch pose `facingDeg` 37.6 → `alpha` 1, `along` (0.70, −0.56, 0.43), `across` (−0.62, −0.78, 0.00); `heal -9` / `mana -6` → 0.551 / 0.407; turned 30° → `alpha` 0.61; left controller off → `shown false`, the orbs back on the quad (quad diff 0.31). Captures eye-checked. Flat: `PASS`, `vr.wrist.shown false` (`no left controller`) |
| 2026-10-13, #38: `VRControlsTest` (the act's default chord, the shared stash / act pair, a conflict of any other event on it, the resolver firing both); `smoke-test.ps1 -Playground -VrSim idle -Sequence playground-hands`; `smoke-test.ps1 -VrSim idle -Sequence hand-reticle -Save mvp-items` (the stash and click path) | `OK (338 tests)`. Sequence `PASS` 243 of 243: `entities _900` lists `light_door_9001` (7850, 0, 8150), `chest_metal_9001` (8420, 0, 8060), `goblin_base_9002` (8100, 0, 7950, life 0); 10 acts (`vr.reticle.act`) — lever 158 / 164 units, door 214–236, chest 194, body 199, right then left; the potion, the cheese, the bread and the apple `item.toinventory container=player`; no `item.take` of a fix entity or the body. `hand-reticle` `PASS` 99 of 99. Found on the way: the playground starts in the cursor mode with the bag open (the panel took the first press), a stash chord that acts also clicked (a drag of the chest every frame, fixed), and the drag icon offset and the thin lever (TRAPS.md) |

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
| Sequences (#255) | the same script, phase `sequences`: every `scripts/dev/xrs/*.xrs` (`look-around`, `snap-turn`, `walk-forward`, `stand-up`, `recenter`, `ui-panel`, see [Scripted sequences](#scripted-sequences-xrs-113)) as one launch each — `smoke-test.ps1 -Sequence <name>` with the `@sim` / `@args` / `@save` the file declares (idle simulated headset by default, flat when its `@args` say `--no-vr`) | a step fails, **a `@pending` step starts passing** (remove the `@pending`), or the launch was skipped (exit 2). A new `.xrs` file is picked up by itself |
| Upload | the artifact `game-checks`, also when a step failed | — |

**The save library on the runner (#255).** Sequences with a `@save` (`snap-turn`, `stand-up`, `ui-panel`: `mvp-new-game`)
need the test saves (D-023: derived from the game's data, generated per machine, never committed). The phase
`save-library` runs first: in the service's own profile (`C:\Windows\ServiceProfiles\NetworkService\AppData\Local\ArxFatalisVR\savelib`)
it runs `make-test-saves.ps1 -Verify` (one launch, ~20 s: a fresh game loads every save); when the library is missing, lacks a
save a sequence names, or `-Verify` fails (an engine save format change made the saves stale), it runs
`make-test-saves.ps1` (one launch, ~1 min) and the library is regenerated — nothing to do by hand. A sequence whose save the phase could
not provide fails (not skipped).

`game-checks` holds, per phase (`smoke-test`, `spell-drawcheck`, `save-library`, `xrs-<sequence>`): the transcript (`<phase>.log`), then
in `<phase>\` every `arx*.log`, `status.json`, `ack.txt`, `stdout.txt` / `stderr.txt` of the channel,
`capture\` (the draw check pair as `.bmp`; a sequence's `@shot` pictures), `crashes\` and the run `digest\`. Read the
digest, not the log; the failing step of a sequence is the `FAIL` line of `xrs-<sequence>.log`. A whole green job takes about
four minutes on the owner's PC (script tests 6 s, an incremental build about a minute, game checks 17 s, sequences
about 1.5 min, plus ~20 s to verify the save library; ~1 min more when it is regenerated).

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
