# Arx Fatalis VR — Development: how to build and run

For a developer (or a worker session) who has just cloned the repository: what to install, how to
build, how to run the game flat and in VR, how to check a change, and where things are. This page
collects the commands; the reasoning lives in the documents it links to. Everything here is for
**Windows** — the only platform of the VR port (the upstream engine's own Linux / macOS build
instructions are in the [ARX-LIBERTATIS.md](../ARX-LIBERTATIS.md)).

## Prerequisites

| What | Notes |
|---|---|
| Windows 10/11, 64-bit | Smart App Control can block a freshly built `arx.exe`; if the launch dies silently, check the CodeIntegrity event log first |
| Visual Studio 2022 or Build Tools 2022 | with the **Desktop development with C++** workload (MSVC + Windows SDK); found through `vswhere` |
| CMake, Git, PowerShell 7 | `pwsh`; the scripts are `.ps1` and need 7 |
| Arx Fatalis | the original game, **Steam app 1700**, installed in any Steam library — the game finds the data itself |
| For VR: SteamVR + a headset | target is the Valve Index; OpenXR with SteamVR as the runtime ([D-001](DECISIONS.md#d-001-use-openxr-not-openvr-as-the-vr-api)). Not needed for flat runs and tests |
| `gh` (GitHub CLI), logged in | only for the task workflow ([WORKFLOW.md](WORKFLOW.md)), not for building |

There is no Python on the project owner's PC; helpers are written as `.ps1`.

Dependencies (zlib, Boost, GLM, FreeType, OpenAL, SDL2, libepoxy, OpenXR, …) come from **vcpkg in manifest
mode** (`vcpkg.json`, triplet `x64-windows`). You do not install them: the build script clones and
bootstraps vcpkg on first run into `%LOCALAPPDATA%\ArxFatalisVR\vcpkg` (or uses `$env:VCPKG_ROOT` when
that is set before the script starts). The first build compiles the ports and takes a while; later
builds are incremental.

## Clone and branches

```powershell
git clone https://github.com/Arziburst/arx-fatalis-vr.git
git remote add upstream https://github.com/arx/ArxLibertatis.git   # optional: the original engine
```

`main` is the development branch and the default; task branches start from it and PRs go into it.
`master` is an untouched mirror of upstream at the fork point — never develop on it.
Naming of branches, commits and PRs: [WORKFLOW.md](WORKFLOW.md#commit-and-pull-request-naming).

## Build

```powershell
.\scripts\build-windows.ps1                  # Release
.\scripts\build-windows.ps1 -Config Debug -Clean
.\scripts\build-windows.ps1 -SkipTests       # do not run the host unit tests afterwards
```

The script loads the Visual Studio environment, sets up vcpkg, configures with CMake + Ninja (NMake as a
fallback), builds, and runs the host unit tests (`arxtest`, CppUnit, `tests/`); a failing suite fails
the script. `-Config` is `Release` (default), `RelWithDebInfo` or `Debug`.

- **Build directories are outside the source tree**: `%LOCALAPPDATA%\ArxFatalisVR\build\<folder>-<hash>\<Config>`,
  one per working copy (main clone or worktree), because the source tree may sit in a synced folder
  (OneDrive) and CMake cannot share a build directory between source trees. `scripts/build-paths.ps1`
  computes the key; `-BuildDir` overrides it.
- Besides `arx.exe` the build produces `xr_hello.exe` (CMake option `BUILD_XR_HELLO`, on by default),
  a probe of the OpenXR runtime on its own, and `built-from.txt` next to `arx.exe` naming the commit.
- CMake options that matter for the VR port: `USE_OPENXR` (on by default — the OpenXR backend),
  `BUILD_TESTS`, `RUN_TESTS`.
- **Close the game before rebuilding**: a running `arx.exe` locks the file and the link fails with
  `LNK1104` (`Get-Process arx` tells).

## Run

```powershell
.\scripts\run-windows.ps1                          # main menu, windowed
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0   # straight into save slot 0
.\scripts\run-windows.ps1 -Fullscreen              # use the fullscreen setting from cfg.ini
.\scripts\run-windows.ps1 -Isolated                # this working copy's own user dir; -Saves save0000 copies a save in
.\scripts\run-windows.ps1 -Rebuild                 # build first, then run
.\scripts\run-windows.ps1 -- --no-vr               # force flat mode
```

Everything after `--` goes to `arx.exe`. What the script does by default:

- **Game data**: the game finds the Arx Fatalis install in the Steam libraries itself (arx.log line
  `Steam data:`); `-DataDir <folder>` passes another install as `--data-dir`.
- **Windowed**: test runs get `--windowed`, so the game does not take over the desktop.
- **Development shortcuts**: a plain launch starts at the main menu, the publisher logos and the intro
  are skipped (`--intro` shows them) and every `.cin` cinematic is skipped (`--cinematics` shows them).
- **Straight into the game**: `--skiplogo --loadslot <n>` loads a save; `--loadlevel <n>` starts a level
  (the smoke test uses level 10). Saves are made in VR: a flat run (`--no-vr`) does not list or load a
  VR save, nor a VR run a flat one (D-NEW); a library of test saves for scenarios, made on the simulated
  headset, is described in [TESTING.md](TESTING.md).
- **User directory**: by default the game uses the player's `%USERPROFILE%\Saved Games\Arx Fatalis VR`
  (log `arx.log`, `cfg.ini`, saves, crash reports) - its own, never flat Arx Libertatis' (D-NEW); the first
  start copies `cfg.ini` from `Saved Games\Arx Libertatis` once, never the saves. `-Isolated` uses
  `%LOCALAPPDATA%\ArxFatalisVR\userdirs\<folder>-<hash>` instead, so a test never touches real saves.

### VR

1. Start SteamVR yourself (the headset comes on). The scripts **never start or stop SteamVR**
   ([D-014](DECISIONS.md#d-014-launch-control-for-parallel-workers--no-steamvr-start-one-game-at-a-time-own-fresh-build-ledger-budget)); only the owner's switch `-StartSteamVR` of `run-windows.ps1` and
   `scripts/dev/test-pr.ps1` does.
2. `.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0` — with `vrserver.exe` running the game starts in
   VR on its own; otherwise the launch is made flat with a note.
3. To see the product name and tile on SteamVR's loading card, register the build once per working copy
   and configuration, then restart SteamVR: `.\scripts\register-steamvr.ps1`. To launch from the SteamVR
   library, `.\scripts\add-steam-shortcut.ps1` (Steam must be exited first). Details:
   [STEAMVR.md](STEAMVR.md).

The desktop window mirrors the left eye. Without a headset use `--no-vr`.

### Launch rules (D-014)

Enforced by `scripts/launch-lib.ps1`, not by good manners — do not work around them:

- **One game at a time on this PC.** A launch holds the game lock and waits for the holder.
- **Only this working copy's own fresh build** runs: an `arx.exe` of another copy, or of another commit
  than `HEAD`, is refused (`-Rebuild` builds first; "dirty" is fine).
- **No SteamVR starts** from a script; without `vrserver.exe` a launch is flat and `smoke-test.ps1 -Vr` is
  skipped with exit code 2.
- **Launch budget**: at most 10 game launches per task. Every launch is written to
  `%LOCALAPPDATA%\ArxFatalisVR\launches.log`.

## Check a change

| What | Command | Needs the game / PC |
|---|---|---|
| Host unit tests | `.\scripts\build-windows.ps1` (runs `arxtest`) | no |
| Script self-tests (PowerShell) | `.\scripts\dev\run-script-tests.ps1` | no; seconds |
| Smoke test, flat | `.\scripts\dev\smoke-test.ps1` | yes, one launch |
| Smoke test, VR | `.\scripts\dev\smoke-test.ps1 -Vr` | only while the owner's SteamVR runs; otherwise exit 2 |
| Exercise what you changed | `.\scripts\dev\smoke-test.ps1 -Commands 'shot a','mark x' -- --loadslot 0` | yes |
| What CI runs after the build | `.\scripts\dev\ci-smoke.ps1 -OutDir $env:TEMP\ci-evidence` | yes |
| The last run in a screenful | `.\scripts\dev\log-digest.ps1 -Print` (also runs by itself after every launch) | no |

The smoke test starts the game with the test command channel, waits until it is playable, sends commands,
quits, and checks the exit code, `Clean shutdown`, crash reports, `[E]` lines and `status.json`. Read
the log through `log-digest.ps1`, not by hand. Everything about the channel (`game-cmd.ps1`,
`game-status.ps1`, `status.json`, screenshots) and what is reliable without a headset is in
[TESTING.md](TESTING.md). If you hit a symptom that makes no sense, read [TRAPS.md](TRAPS.md) first.

### CI

`.github/workflows/build-windows.yml` runs on the self-hosted runner on the owner's PC (label `arxvr`)
for every pull request into `main` and every push to `main` (docs-only changes are skipped): the script
self-tests, `build-windows.ps1 -Config Release` (which includes the unit tests), then `ci-smoke.ps1`. A red
step blocks the merge; wait for it with `.\scripts\dev\pr-checks.ps1 <pr> -Watch`.

## Parallel working copies

Several sessions work at once, each in its own **git worktree** outside OneDrive, so builds, logs, saves
and test channels never collide:

```powershell
.\scripts\dev\new-worktree.ps1 -Issue 18 -Type feat -Name per-eye-swapchains   # from origin/main
.\scripts\dev\new-worktree.ps1 -Issue 18 -Remove                                 # when merged
```

The worktree lives in `%USERPROFILE%\arx-fatalis-vr-wt\ARXVR-<issue>-<type>-<name>` (`-Root` or
`$env:ARXVR_WORKTREE_ROOT` change it). Its build directory, isolated user directory and channel
directory are derived from the same `<folder>-<hash>` key. Rules of parallel work:
[WORKFLOW.md](WORKFLOW.md#parallel-work).

To try a pull request by hand (build its worktree if needed and launch it):
`.\scripts\dev\test-pr.ps1 <pr>`.

## Where the VR code is

| What | Where |
|---|---|
| OpenXR instance, headset, session (`--no-vr`) | `src/vr/VRSystem.cpp`, called from `ArxGame::initialize` / `doFrame` / `shutdown` in `src/core/ArxGame.cpp` |
| Per-eye swapchains, frame pacing | `src/vr/VRSwapchain.cpp`, `src/vr/FramePacing.cpp` |
| Actions, input mapping, remapping | `src/vr/VRActions.cpp`, `src/vr/VRInput.cpp`, `src/vr/VRControls.cpp` |
| Comfort policy (no head bob, shake, FOV zoom) | `src/vr/VRComfort.cpp` |
| Rendering (OpenGL, the only backend) | `src/graphics/opengl/OpenGLRenderer.cpp` |
| Camera | `src/game/Camera.cpp` — `SetViewMatrix` / `SetProjectionMatrix` |
| Window | `src/window/SDL2Window.cpp` |
| Test command channel, `status.json` | `src/core/CommandChannel.cpp` |
| Host unit tests | `tests/` (`tests/vr/` for the VR decision cores) |
| SteamVR manifest and tile | `data/vr/` |

## Further reading

- [MVP.md](MVP.md) — the current goal and the task queue; [WORKFLOW.md](WORKFLOW.md) — issues, board, PRs, merging.
- [DECISIONS.md](DECISIONS.md) — why things are the way they are; add new decisions there.
- [TESTING.md](TESTING.md), [TRAPS.md](TRAPS.md), [STEAMVR.md](STEAMVR.md).
