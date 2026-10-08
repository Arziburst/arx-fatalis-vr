# Arx Fatalis VR — Decision Log

Significant technical and project decisions, newest first. Each entry records what was decided, why,
and what it rules out, so the reasoning survives after the conversation that produced it.

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
