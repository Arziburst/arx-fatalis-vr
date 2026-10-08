# Arx Fatalis VR — Testing without a headset

How a worker session verifies the game before the owner puts the headset on: the smoke test, the
test command channel with `status.json`, and the `xr_hello` runtime probe. What was measured on the
owner's PC (2026-10-08, #99), what is reliable enough to be a gate, and what is a manual check.
The rules that use this are in [WORKFLOW.md](WORKFLOW.md) (*Workers test the game themselves first*,
*Verification tiers*); the reference these tools follow is Dishonored VR's harness
([RESEARCH-reference-mods.md](RESEARCH-reference-mods.md), F-01, F-02, F-03, F-16).

## In one minute

```powershell
.\scripts\dev\smoke-test.ps1                                  # flat, --loadlevel 10: PASS / FAIL + evidence
.\scripts\dev\smoke-test.ps1 -Vr                              # the same in VR (SteamVR running)
.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5            # a table of 10 runs
.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0
.\scripts\dev\smoke-test.ps1 -Menu -Commands 'loadslot 0'       # plain launch (main menu), then a save through the channel
.\scripts\run-windows.ps1 -- --test-dir "$env:LOCALAPPDATA\ArxFatalisVR\test" --loadlevel 10   # a manual run with the channel
.\scripts\dev\game-status.ps1 -Watch                          # live state line
.\scripts\dev\game-cmd.ps1 'shot before' 'teleport -p marker_0001' 'shot after'
.\scripts\dev\game-cmd.ps1 quit
& "$buildDir\xr_hello.exe"                                    # the runtime alone: exit 0 / 1 / 2
```

`smoke-test.ps1` exits 0 only when every run passed; a worker quotes its verdict lines in the PR's
*How verified*.

## The smoke test (`scripts/dev/smoke-test.ps1`)

One run = start the Release build of this working copy (same lookup as `run-windows.ps1`) with
`--windowed --test-dir <dir>` plus `--no-vr` unless `-Vr`, and `--loadlevel 10` unless other
`arx.exe` arguments follow `--`; wait until `status.json` says `GAMEPLAY` or `CINEMATIC` (`MENU` for
a plain launch); send each `-Commands` line through the channel; stay `-Seconds` (default 10); quit
through the channel (`-CloseWindow`: WM_CLOSE instead, the player's path); then check:

| Check | Evidence | Fails when |
|---|---|---|
| exit code | `Process.ExitCode` | ≠ 0 (a crash exits with 1) |
| fresh log | `arx.log` `LastWriteTime` ≥ launch time | the game died before writing it, or never started |
| clean shutdown | `Clean shutdown` in that log | missing (crash, kill, hang) |
| no crash | new directories in `%USERPROFILE%\Saved Games\Arx Libertatis\crashes\` | one appeared |
| no errors | `[E]` / `[C]` lines in the log **and** `errors` in `status.json` | any |
| commands | every `-Commands` ack has `errors=0` | a command was refused |

It prints the state timeline (`+1.1s state GAMEPLAY (frame 2)`), the acks, the `[E]`/`[W]` lines,
the last lines of the log, the head of a new crash report, and `PASS` / `FAIL` with every reason.
`-Repeat N` adds a table and a `passed X/N` line. The game runs with `ARX_UNATTENDED=1`: a crash
writes its report and exits instead of waiting for someone to click the crash dialog.

The test refuses to start while another `arx.exe` runs (the channel directory and the log are
shared) and, with `-Vr`, while `vrserver.exe` is not running (the game would start flat).

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
- **flushes `arx.log`** at the same cadence (the file logger never flushes on its own, see
  [TRAPS.md](TRAPS.md)), so `[game] state:` and `[mark]` lines are readable while the game runs.
- **logs `[game] state: STARTUP|MENU|LOADING level N|GAMEPLAY|CINEMATIC|EXITED`** on every change.
- sets `ARX_UNATTENDED=1` in its own environment, inherited by the crash processor: no crash dialog.

### Commands

| Line | Effect |
|---|---|
| `status` | nothing extra — the status is written after every batch anyway |
| `shot [name]` | after the next rendered frame, save it as `<dir>\capture\<name>.bmp` (default `shot<N>`); `captureSeq` and `lastCapture` in the status |
| `mark <text>` | `[mark] <text>` in the log — an anchor for "the line I wait for must come after this" |
| `quit` | ask the game to quit at the end of the frame (the same path as closing the window) |
| `loadslot <n>` | load save slot `n` (from the menu or in-game), refused with the slot count if it does not exist |
| `loadlevel <n> [target]` | change to level `n` in a running game (refused from the menu: start with `--loadlevel` or `loadslot`) |
| `recenter` | refused with `not implemented yet (#28)` until recentering exists |
| anything else | the **script console**: `[entity.]command args`, exactly as typed in the in-game console (`` ` ``). Context defaults to the player. Refused (and counted in the ack) when the entity or the command is unknown |

Useful script-console commands for test scenes (the full list is what `ScriptEvent` registers,
`src/script/Scripted*.cpp`): `teleport -p <marker>` / `teleport -l <level> <marker>`, `spawn npc
<object> <marker>`, `playeradd`, `addgold`, `addxp`, `setlife`, `invulnerability on`, `noclip`,
`setplayercontrols off`, `herosay <text>`, `showvars`, `sendevent <event> <entity>`, `worldfade`.
A refused line costs nothing: `[W] [channel] script command failed: Unknown command: …` and
`errors=1` in the ack, no `[E]`.

### `status.json`

```json
{"pid":34120,"version":"0.1-dev + cead4a3","state":"GAMEPLAY","cleanShutdown":false,
 "frame":618,"uptimeMs":6240,"fps":120.2,"errors":0,"warnings":0,
 "menuMode":"ingame","area":10,"paused":false,"cinematic":false,
 "player":{"x":...,"y":...,"z":...,"yaw":...,"life":...,"lifeMax":...,"mana":...,"manaMax":...},
 "window":{"width":640,"height":480,"focused":true,"minimized":false,"fullscreen":false},
 "vr":{"supported":true,"enabled":false,"sessionState":"none"},
 "cmdSeq":3,"lastCmd":"mark hello","lastCmdError":"","captureSeq":1,"lastCapture":"...\\first.bmp"}
```

- `state`: `STARTUP` (before the first level or menu), `MENU` (main menu, pause menu, credits,
  character creation), `LOADING` (written from inside the level loader — a level load is one long
  frame), `GAMEPLAY`, `CINEMATIC`, `EXITED` (with `cleanShutdown: true`, written right before
  `Clean shutdown` is logged).
- `errors` / `warnings`: log lines at `[E]`/`[C]` and `[W]` level since the process started, counted
  by a logger backend — assert on these instead of grepping a buffered log.
- `fps`: frames over the last status interval (0 in the final status).
- `vr.sessionState`: `none` without a session, else the OpenXR state as the runtime reported it
  (`idle`, `ready`, `synchronized`, `visible`, `focused`, `stopping`, …).
- `player` is present only while a level is loaded. More fields (per-eye frame ids, the rate line)
  come with #20; the `.xrs` runner (#113) asserts on these keys with `@modassert`.

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
| `main` before this task: `--no-vr --loadlevel 10`, close the window after 30 s | **1 of 5 clean**; 4 crashed on exit in `Entity::cleanReferences` — bug #93, because `--loadlevel 10` played the intro cinematic. Not a flaky script: a real bug on a path every dev launch took |
| Dev launches skip the intro cinematic (this task): `smoke-test.ps1 -Repeat 10 -Seconds 5`, quit through the channel | **10 of 10** `PASS`; `LOADING` → `GAMEPLAY` 1.1 s after launch, 120 fps windowed, 0 errors, 0 warnings |
| The same, closed with WM_CLOSE (`-CloseWindow -Repeat 5`) | **5 of 5** `PASS` |
| A refused command (`bogus_command 1`) | the run is `FAIL` with the ack as the reason — the gate can fail |
| `xr_hello --frames 120` with SteamVR running and the headset idle on the desk | exit 0: `IDLE → READY` at once, `SYNCHRONIZED` after 0.13 s, 120 frames in 1.48 s (display period 12.5 ms = 80 Hz), orderly `STOPPING → IDLE → EXITING`; **never `VISIBLE` or `FOCUSED`** |
| `smoke-test.ps1 -Vr` with SteamVR running and the headset idle | `PASS`: instance, system and session created, state `ready` (the game does not begin the session until #20), clean shutdown with the session destroyed before the window |
| `smoke-test.ps1 -Menu -Commands 'loadslot 0'` (plain launch, then a save through the channel) | `MENU` 0.8 s after launch; `loadslot 0` refused with `no save in slot 0 (have 0)` — the owner's machine has no saves yet (#70), so the success path of `loadslot` is **not verified** here; it is the same `LOADQUEST_SLOT` path as `--loadslot` |

Conclusions:

- **Reliable, used as a gate:** the flat smoke test (start, level, channel commands, quit, evidence).
- **Reliable for what it covers:** the VR launch with an idle headset proves instance / session
  creation, the OpenGL binding and the shutdown order. With nobody wearing the headset SteamVR runs
  the frame loop in `SYNCHRONIZED` but never grants `VISIBLE`/`FOCUSED`, so anything that needs
  focus (input actions, "is the picture right") cannot be judged this way: that is the simulated
  backend's job (#112, #113) or the owner's. The SteamVR null driver (F-03) is **not needed**: an
  idle Index already runs frames; the driver would not reach `FOCUSED` either without input.
- **Manual, not automated:** anything visual in VR, performance at the headset's rate, and the
  `--intro` path (#93 is still open there).

## Writing a channel-driven check

1. Start with the state you need: `--loadlevel N`, `--loadslot N` (saves from #70) or a plain launch.
2. Put the game where the feature is: `teleport -p <marker>`, `loadlevel`, `spawn`.
3. `mark <name>` before the action, do it, then assert on numbers (`status.json`, the ack) or on
   `shot` captures (`capture\*.bmp`, compared by #113's `img-diff`).
4. Prove the check can fail once (break the thing on purpose) before trusting it.
5. Quote the verdict lines in the PR's *How verified*.

Scripts share `scripts/dev/test-lib.ps1` (`Get-ArxTestDir`, `Read-ArxStatus`, `Wait-ArxStatus`,
`Send-ArxCommand`); the channel directory is `$env:ARXVR_TEST_DIR` or
`%LOCALAPPDATA%\ArxFatalisVR\test`.
