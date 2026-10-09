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
.\scripts\dev\smoke-test.ps1 -Vr                              # the same in VR (SteamVR running; SKIPPED, exit 2, while another game holds the VR lock)
.\scripts\dev\smoke-test.ps1 -Repeat 10 -Seconds 5            # a table of 10 runs
.\scripts\dev\smoke-test.ps1 -Saves save0000 -- --loadslot 0  # copy a save into the isolated user directory, then load it
.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0
.\scripts\dev\smoke-test.ps1 -Menu -Commands 'loadslot 0'       # plain launch (main menu), then a save through the channel
.\scripts\run-windows.ps1 -Isolated -TestChannel -- --no-vr --loadlevel 10   # a manual run with the channel, in this copy's own user dir
.\scripts\dev\game-status.ps1 -Watch                          # live state line
.\scripts\dev\game-cmd.ps1 'shot before' 'teleport -p marker_0001' 'shot after'
.\scripts\dev\game-cmd.ps1 quit
& "$buildDir\xr_hello.exe"                                    # the runtime alone: exit 0 / 1 / 2
```

`smoke-test.ps1` exits 0 only when every run passed (1: a run failed; 2: a VR run was skipped because
another game holds the VR lock); a worker quotes its verdict lines in the PR's *How verified*.

Every path a test run touches is **per working copy** (the key `<folder>-<hash>` of
`scripts/build-paths.ps1`, the same one the build directory uses): the game's user and config
directory `%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>` (`arx.log`, `cfg.ini`, saves, crash reports)
and the channel directory `%LOCALAPPDATA%\ArxFatalisVR\test\<key>`. Parallel workers therefore never
read each other's log or overwrite each other's `status.json`, and never touch the player's
`%USERPROFILE%\Saved Games\Arx Libertatis` (`-Shared` opts into it on purpose). See
[Isolation and the VR lock](#isolation-and-the-vr-lock-parallel-work).

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

It prints the state timeline (`+1.1s state GAMEPLAY (frame 2)`), the acks, the `[E]`/`[W]` lines,
the last lines of the log, the head of a new crash report, and `PASS` / `FAIL` with every reason.
`-Repeat N` adds a table and a `passed X/N` line. The game runs with `ARX_UNATTENDED=1`: a crash
writes its report and exits instead of waiting for someone to click the crash dialog.

The test refuses to start while an `arx.exe` **from the same build** runs (same channel directory,
same log, and a rebuild could not relink it); with `-Shared` while *any* `arx.exe` runs (they would
all write the player's log); and, with `-Vr`, while `vrserver.exe` is not running (the game would
start flat). A fresh isolated directory has no `cfg.ini` yet, so the first run logs one warning
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
`setplayercontrols off`, `herosay <text>`, `showvars`, `sendevent <event> <entity>`, `worldfade`,
`cine <name>` (a `.cin` cinematic, e.g. `cine akbaa_dream_a`; development launches skip it with a
`Skipping cinematic` log line and the state never shows `CINEMATIC` — `--cinematics` plays it, #124).
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
 "window":{"width":640,"height":480,"focused":true,"minimized":false,"fullscreen":false},
 "vr":{"supported":true,"enabled":false,"sessionState":"none","runtime":"",
       "manifest":"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json"},
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
- `build`: the same build identification as the [log banner](#the-log-banner): the full commit and
  the branch compiled into `Version.cpp` (`branch` empty when detached), the CMake configuration.
- `vr.runtime`: the OpenXR runtime that answered this run (`SteamVR/OpenXR 2.17.10`), empty while no
  instance was created; `vr.manifest`: the runtime manifest the loader uses (`XR_RUNTIME_JSON`, then
  the registry), empty when no runtime is installed — the same values as the banner's runtime lines.

## The log banner

Every `arx.log` starts with four `Key: value` lines (#115, research rows F-18 and F-98), so a log says
which build wrote it and how it was started before anything else; the run digest (#131) reads them.
Keep their shape when touching them.

```
[I] Startup:252  Starting Arx Fatalis VR 0.1-dev + f8b3585
[I] Startup:111  Build: f8b3585637414910c931b733d27661cb8f4c407f on ARXVR-115-feat-log-banner-runtime, Release, MSVC 19.44, x86_64
[I] Startup:141  Executable: "C:\Users\…\Release\arx.exe", linked 2026-10-09 14:02:55, built from f8b3585637414910c931b733d27661cb8f4c407f dirty
[I] Startup:163  Run: 2026-10-09 15:10:03 local (12:10:03 UTC), pid 1234, command line: --windowed --no-vr --loadlevel 10
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
| `smoke-test.ps1 -Vr` with SteamVR running and the headset idle | `PASS`: instance, system and session created, state `ready` (the game does not begin the session until #20), clean shutdown with the session destroyed before the window |
| `smoke-test.ps1 -Menu -Commands 'loadslot 0'` (plain launch, then a save through the channel) | `MENU` 0.8 s after launch; `loadslot 0` refused with `no save in slot 0 (have 0)` — the owner's machine has no saves yet (#70), so the success path of `loadslot` is **not verified** here; it is the same `LOADQUEST_SLOT` path as `--loadslot` |
| 2026-10-09, #124: `--loadlevel 10` with the intro `.cin` **loaded** (preloaded by the level 10 script, never played), `-CloseWindow -Repeat 5` | **2 of 5 crashed** in `Entity::cleanReferences` (#93); the channel `quit` was 5 of 5 clean on the same build. A loaded cinematic is enough for #93, so the development skip never loads the file |
| 2026-10-09, #124: cinematics skipped without loading — default `-CloseWindow -Repeat 3`; `-Commands 'cine play' -CloseWindow -Repeat 2`; `-Commands 'cine play','cine akbaa_dream_a' -Repeat 2` | **3 of 3, 2 of 2, 2 of 2** `PASS`; one `Skipping cinematic <name>` line per cinematic, state never `CINEMATIC`, 0 errors. With `--cinematics` (or `cine play` with `--intro`) the state becomes `CINEMATIC` — the check can fail |
| 2026-10-09, #93 before the fix: a detector in `~Entity` (log every entity whose `owner()` is the one being deleted) | **1 of 1** exits from level 10 report `human_base_0033` deleted while it still owns `long_sword_0021` — the dangling owner is there on every exit, the crash only depends on the heap |
| 2026-10-09, #93 after the fix: `-Repeat 10 -Seconds 3` (channel quit) | **10 of 10** `PASS`, 0 errors, 0 warnings; the detector reports nothing |
| 2026-10-09, #93 after the fix: `-Repeat 10 -Seconds 3 -CloseWindow -- --no-vr --loadlevel 10 --intro` (intro `.cin` loaded, window closed — 2 of 5 crashed in #124) | **10 of 10** `PASS` |
| 2026-10-09, #93 after the fix: `-Vr -Repeat 10` + `-Vr -Repeat 5`, SteamVR running, headset idle | **13 of 15** `PASS`, **0** exit crashes (before: 2 of 3); 2 runs ended mid-run with exit code -1 and no crash report — a different problem, #146 |
| 2026-10-09, #115 log banner: flat smoke test, then `status.json` and the first log lines asserted by script | `PASS`; `build.commit` = `git rev-parse HEAD` of the worktree **after a commit and a rebuild** (before #115 a worktree kept its first build's commit), `build.branch`, `build.config` = `Release`, `vr.manifest` = the SteamVR manifest from `HKLM`, `vr.runtime` = `""` in a flat run; the four banner lines match their documented shape. `XR_RUNTIME_JSON` → a missing file (flat, `--no-vr`): the manifest line says `from XR_RUNTIME_JSON, overriding the registry's "…steamxr_win64.json"`, exactly **1** `[W] … does not exist`, `vr.manifest` = that path, `PASS`. `built-from.txt` edited to another commit: exactly **1** `[W] built-from.txt names commit …`, `PASS`. The `OpenXR runtime answered` line needs an instance (SteamVR running): not exercised in that session, it is the renamed `Using OpenXR runtime` line |

Conclusions:

- **Reliable, used as a gate:** the flat smoke test (start, level, channel commands, quit, evidence).
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
   `shot` captures (`capture\*.bmp`, compared by #113's `img-diff`).
4. Prove the check can fail once (break the thing on purpose) before trusting it.
5. Quote the verdict lines in the PR's *How verified*.

Scripts share `scripts/dev/test-lib.ps1` (`Get-ArxTestDir`, `Get-ArxUserDir`, `Read-ArxStatus`,
`Wait-ArxStatus`, `Send-ArxCommand`); the channel directory is `$env:ARXVR_TEST_DIR` or
`%LOCALAPPDATA%\ArxFatalisVR\test\<key>` of the working copy the scripts belong to.

## Isolation and the VR lock (parallel work)

Several workers build and test at the same time ([WORKFLOW.md](WORKFLOW.md), *Parallel work*; #123).
What keeps their runs apart — all derived from the working copy's key `<folder>-<hash>`
(`scripts/build-paths.ps1`):

| Shared before | Now |
|---|---|
| the build | `%LOCALAPPDATA%\ArxFatalisVR\build\<key>\<Config>`; `built-from.txt` in it records the commit a build came from (`<sha>` or `<sha> dirty`), read by `test-pr.ps1` to decide whether to rebuild |
| `arx.log`, `cfg.ini`, saves, crash reports | `run-windows.ps1 -Isolated` and the smoke test pass `--user-dir` / `--config-dir` = `…\userdirs\<key>`. The player's `Saved Games\Arx Libertatis` is touched only by a plain `run-windows.ps1` (the owner's headset test, `test-pr.ps1`) or `smoke-test.ps1 -Shared` |
| the channel (`status.json`, `command.txt`) | `--test-dir` = `…\test\<key>`; a copy's `game-cmd.ps1` / `game-status.ps1` talk to that copy's game; `run-windows.ps1 -TestChannel` starts a manual run with it |
| saves for `--loadslot` | `-Saves save0000` copies slots into the isolated directory from `-SaveLibrary`, `$env:ARXVR_SAVE_LIBRARY` or the player's `save` folder (#70 defines the library) |
| the headset | the VR lock below |

**The VR lock** (`scripts/vr-lock.ps1`): `%LOCALAPPDATA%\ArxFatalisVR\vr.lock`, opened for writing
without write sharing by the launching script and held until the game exits. Windows drops the handle
with the process, so a crash or a kill cannot leave a stale lock. The holder writes
`pid=… since=… what=…` into the file; whoever finds the lock busy prints that line. Rules: every
launch that *may* start in VR (no `--no-vr`) takes it — `run-windows.ps1` waits `-VrLockTimeoutSec`
(default 120 s) and then fails naming the holder; `smoke-test.ps1 -Vr` waits (default 60 s) and then
**skips** with exit 2 (the VR self-test is best effort — say so in the PR); flat launches (`--no-vr`)
never take it.

Measured (owner's PC, 2026-10-09, #123):

| Scenario | Result |
|---|---|
| `smoke-test.ps1` (isolated by default) | `PASS`; `arx.log`, `cfg.ini`, `save\`, `crashes\` appear in `userdirs\<key>`; the player's `Saved Games\Arx Libertatis\arx.log` keeps its `LastWriteTime`. First run in a fresh directory: 1 warning (no `cfg.ini` yet), later runs 0 |
| **Two smoke tests at the same time** (this worktree's build and the owner's build, separate user and channel directories), `-Seconds 8` each | **2 of 2** `PASS`, both done in 11 s; `GAMEPLAY` after 0.5 s and 1.1 s, 0 errors — **no second-instance limit**, so the lock covers VR launches only (the fallback of WORKFLOW.md is not needed) |
| VR lock held by another process: `smoke-test.ps1 -Vr -VrLockTimeoutSec 3` | `SKIPPED`, exit 2, holder printed: `pid=2580 since=… what=lock-test holder` |
| the same, `run-windows.ps1 -VrLockTimeoutSec 2` without `--no-vr` | fails: `another game is running in VR (pid=2580 … what=lock-test holder). Wait for it to end, or add --no-vr` |
| the same, flat `smoke-test.ps1` | `PASS` — a flat run ignores the lock |
| the holder is killed (`Stop-Process`) | the lock is free at once; after a release no `vr.lock` file is left |
| `smoke-test.ps1 -Vr` with SteamVR running, headset idle, lock free (3 runs) | the lock is taken and released, VR `enabled=True session=ready` in all three; **2 of 3 crashed on exit** with the #93 stack (`Entity::cleanReferences` ← `DanaeClearLevel`), no cinematic loaded — the VR exit path is exposed to #93 where the flat path is clean (4 of 4 the same hour), so `-Vr` is **not a gate** until #93 is fixed (evidence on the issue). *#93 fixed on 2026-10-09: 0 exit crashes in 13 VR exits; `-Vr` stays best effort because of #146* |
