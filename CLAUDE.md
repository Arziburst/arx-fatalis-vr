# Arx Fatalis VR

VR port of Arx Fatalis (OpenXR with SteamVR as the runtime, target headset: Valve Index) built on the Arx Libertatis engine.
Key decisions and their reasoning are logged in [docs/vr/DECISIONS.md](docs/vr/DECISIONS.md) — add new ones there.

**Current goal: the MVP** — see [docs/vr/MVP.md](docs/vr/MVP.md). Only work needed for the MVP demo
scenario gets done now; everything else waits in Backlog.
Goal: a free standalone Steam app that requires the original Arx Fatalis (app 1700) on the user's account.

## Working rules — mandatory

Before doing any work, read and follow [docs/vr/WORKFLOW.md](docs/vr/WORKFLOW.md):
tasks are sub-issues of epics on GitHub, board at https://github.com/users/Arziburst/projects/3,
statuses Backlog / Todo / In Progress / Done, branches `task/<number>-<short-name>`, PRs with `Closes #N`.
Create new tasks and bugs discovered during work yourself and list them in your report.

**All project artifacts (docs, issues, commits, PRs, code comments) are in English.**
Conversation with the project owner is in Russian.

## Remotes

- `origin` — https://github.com/Arziburst/arx-fatalis-vr (private, ours)
- `upstream` — https://github.com/arx/ArxLibertatis (original engine)

## Development environment (Windows)

- MSVC: VS Build Tools 2022 (`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`)
- CMake: `C:\Program Files\CMake\bin\cmake.exe`
- vcpkg: `C:\Users\arzib\arx-vr\vcpkg`, triplet `x64-windows`
- Game data: `D:\games\SteamLibrary\steamapps\common\Arx Fatalis`

Configure (from a `VsDevCmd.bat -arch=x64` environment):

```
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/Users/arzib/arx-vr/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows -DUSE_STATIC_LIBS=OFF ^
  -DBUILD_TOOLS=OFF -DBUILD_CRASHREPORTER=OFF -DBUILD_TESTS=OFF
cmake --build build
```

## Key code locations for VR

- Rendering: `src/graphics/opengl/OpenGLRenderer.cpp` (OpenGL, the only backend)
- Camera: `src/game/Camera.cpp` — `SetViewMatrix` / `SetProjectionMatrix`
- Window: `src/window/SDL2Window.cpp`
