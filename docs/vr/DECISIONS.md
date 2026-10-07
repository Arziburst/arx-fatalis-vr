# Arx Fatalis VR — Decision Log

Significant technical and project decisions, newest first. Each entry records what was decided, why,
and what it rules out, so the reasoning survives after the conversation that produced it.

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
