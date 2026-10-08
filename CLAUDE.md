# Arx Fatalis VR

VR port of Arx Fatalis (OpenXR with SteamVR as the runtime, target headset: Valve Index) built on the Arx Libertatis engine.
Key decisions and their reasoning are logged in [docs/vr/DECISIONS.md](docs/vr/DECISIONS.md) — add new ones there.

**Current goal: the MVP** — see [docs/vr/MVP.md](docs/vr/MVP.md). Only work needed for the MVP demo
scenario gets done now; everything else waits in Backlog.
Goal: a free standalone Steam app that requires the original Arx Fatalis (app 1700) on the user's account.

## Working rules — mandatory

Before doing any work, read and follow [docs/vr/WORKFLOW.md](docs/vr/WORKFLOW.md):
tasks are sub-issues of epics on GitHub, board at https://github.com/users/Arziburst/projects/3,
statuses Backlog / Todo / In Progress / Testing / Done, PRs with `Closes #N`.
Naming: commits and PR titles `ARXVR-<issue> <type>: <description>`, branches `ARXVR-<issue>-<type>-<short-name>`
(see [Commit and pull request naming](docs/vr/WORKFLOW.md#commit-and-pull-request-naming)).
Create new tasks and bugs discovered during work yourself and list them in your report.
Before debugging something that makes no sense, read [docs/vr/TRAPS.md](docs/vr/TRAPS.md); add new traps there in the same PR.
Design decisions that shape every VR task: stick forward = look direction (D-008), full body with IK arms (D-009),
no rays from the hands — runes drawn in the air with the hand, grab by reach, a pointer only in menus (D-010).

**Development loop:** `/next-task` takes the next task from the queue in [docs/vr/MVP.md](docs/vr/MVP.md#task-queue)
and implements it in its own PR; `/accept-task` and `/reject-task` handle the owner's VR test verdict.
Merge on your own when CI is green, except `needs-vr-test` PRs — those wait for the owner
(see [Pull requests, CI and merging](docs/vr/WORKFLOW.md#pull-requests-ci-and-merging)).

**All project artifacts (docs, issues, commits, PRs, code comments) are in English.**
Conversation with the project owner is in Russian.

## Remotes and branches

- `origin` — https://github.com/Arziburst/arx-fatalis-vr (private, ours)
- `upstream` — https://github.com/arx/ArxLibertatis (original engine; add it with
  `git remote add upstream https://github.com/arx/ArxLibertatis.git` if missing)
- `main` — our development branch and the default branch. Task branches start from it, PRs go into it.
- `master` — untouched mirror of the upstream engine at the point we forked; not used for development.

The project owner's working copy is `C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr`.

## Development environment (Windows)

Requirements: VS 2022 or Build Tools 2022 with the C++ workload, CMake, Git, PowerShell 7.
Dependencies come from vcpkg in manifest mode (`vcpkg.json`, triplet `x64-windows`).

```powershell
.\scripts\build-windows.ps1              # Release build of this working copy
.\scripts\build-windows.ps1 -Config Debug -Clean
.\scripts\run-windows.ps1                # runs the build with Arx Fatalis data found in the Steam libraries
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0   # extra arguments go to arx.exe; load save slot 0 directly
.\scripts\run-windows.ps1 -Fullscreen    # test runs are windowed by default (arx.exe --windowed); this opts out
.\scripts\register-steamvr.ps1           # tell SteamVR the name and tile of this build (loading card, SteamVR library)
.\scripts\dev\smoke-test.ps1             # worker self-test: start, play, quit, check the evidence (docs/vr/TESTING.md)
```

- SteamVR shows "Arx Fatalis VR" and the tile only for a registered build: run `register-steamvr.ps1`
  once per working copy / config and restart SteamVR — see [docs/vr/STEAMVR.md](docs/vr/STEAMVR.md).
- Build directories live in `%LOCALAPPDATA%\ArxFatalisVR\build\<folder>-<hash>\<Config>` — one per
  working copy, never inside the source tree (which may sit in a synced folder, OneDrive).
- `--skiplogo --loadslot <n>` starts straight into a saved game — the way to test VR before the
  in-VR menu exists. The owner records saves in flat mode.
- A plain launch starts at the main menu: publisher logos and the intro are skipped during development
  (`--intro` shows them), and every `.cin` cinematic — the new-game intro, in-game cutscenes — is
  skipped too (`--cinematics` shows them; restoring all of it for players is #92). Test runs are
  windowed (`--windowed`, added by `run-windows.ps1`); `--no-vr` forces flat mode.
- vcpkg: `$env:VCPKG_ROOT` if set, else `%LOCALAPPDATA%\ArxFatalisVR\vcpkg` (cloned on first build).
- Game data on the owner's machine: `D:\games\SteamLibrary\steamapps\common\Arx Fatalis` (Steam app 1700).

## Key code locations for VR

- Rendering: `src/graphics/opengl/OpenGLRenderer.cpp` (OpenGL, the only backend)
- Camera: `src/game/Camera.cpp` — `SetViewMatrix` / `SetProjectionMatrix`
- Window: `src/window/SDL2Window.cpp`
- VR runtime (OpenXR instance, headset, session; `--no-vr`): `src/vr/VRSystem.cpp`, called from
  `ArxGame::initialize` / `doFrame` / `shutdown` in `src/core/ArxGame.cpp`
