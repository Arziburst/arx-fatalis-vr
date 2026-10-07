# Research: What It Takes to Ship a Playable v1

- **Date:** 2026-10-08
- **Question:** What do we need to build so that the whole game can be completed in VR — moving around,
  fighting with weapons, using menus, equipping the character, interacting with NPCs, enemies and items?
  How much work is it?
- **Basis:** a read-through of the Arx Libertatis source (commit `5b95e4c`) plus a survey of comparable
  VR ports of classic games. All file references are relative to the repository root.

## Definition of "playable v1"

A player with a Valve Index can start a new game, create a character, and finish the game without
touching keyboard or mouse:

- Head-tracked stereo view at headset refresh rate
- Thumbstick locomotion, snap/smooth turn, crouch, jump, stealth
- Melee with physical swings, two-handed bow, rune casting by drawing in the air
- Picking up, using, throwing, and combining items; doors, levers, chests, bodies
- Talking to NPCs, trading, reading notes, quests, map
- Inventory and equipment (paper doll), character creation, level-up
- Main menu, save/load, quick save/load, cutscenes, loading screens

**Out of scope for v1** (later polish): diegetic inventory (belt / holsters), finger tracking,
full-body IK, hand-drawn UI redesign, teleport locomotion.

## Key findings about the engine

1. **Fixed-function OpenGL, no shaders.** GL 1.5 compatibility context (`src/graphics/opengl/OpenGLRenderer.cpp:222`,
   `src/window/SDL2Window.cpp:400-415`). Eye targets need FBOs (available via GLEW) and OpenXR swapchain images.
2. **Most non-level geometry is projected on the CPU into screen pixels.** Entities, items, the player's
   weapon, particles, sprites, spell effects, flares and shadows are emitted as pre-transformed "TL"
   vertices via `g_preparedCamera.m_worldToScreen` (`src/graphics/data/Mesh.cpp:419`). Only level geometry
   goes through the GL matrices. **Stereo therefore means re-projecting this geometry per eye**
   (~63 projection call sites in 17 files). This is the single biggest technical risk.
3. **Simulation and rendering are interleaved.** Spells, particles and flares update while drawing;
   the book runs input inside `HudRoot::draw`; animation computes picking data during update.
   `renderLevel()` cannot simply be called twice — it must be split into *simulate once* and *draw per eye*.
4. **All 2D UI is mouse-driven** and laid out in `g_size` pixels (640×480 reference). The cheapest robust
   approach: render the whole 2D layer once per frame into a "virtual screen" texture, show it on a
   quad in the world, and turn the laser-pointer hit into a synthetic mouse position. Most UI code
   then works unchanged.
5. **Picking is screen-space** (`DANAEMouse`, 83 uses; `GetFirstInterAtPos` on projected bounding boxes),
   but a 3D ray picker already exists (`raycastScene` / `raycastEntities`, `src/graphics/Raycast.h`), and
   world drop/throw already takes a ray (`findSpotForDraggedEntity`, `src/gui/Dragging.cpp:100`).
6. **Melee hit detection is collision-based, not animation timing alone:** spheres at the weapon's
   `hit_*` points test for targets (`ARX_EQUIPMENT_Strike_Check`, `src/game/Equipment.cpp:597-720`).
   Driving the weapon from the controller and calling this check during fast swings reuses the
   existing damage system.
7. **Rune recognition accepts any 2D point stream** and is scale-invariant (`RuneRecognitionAlt::analyze`,
   `src/game/magic/SpellRecognition.cpp:415`). Projecting the controller stroke onto a plane and feeding
   `ARX_SPELLS_AddPoint` should work without touching the recognizer.
8. **AI, pathfinding and damage are independent of input and screen** — no changes beyond camera/culling.

## System-by-system plan

| # | System | Where in code | v1 approach | Difficulty | Epic |
|---|---|---|---|---|---|
| 1 | Stereo rendering | `ArxGame::renderLevel`, `Scene.cpp`, `Mesh.cpp`, `AnimationRender.cpp`, `Draw.cpp`, `OpenGLRenderer.cpp` | Split simulate/draw; combined-frustum culling once; per eye: bind swapchain FBO → per-eye `PrepareCamera` with OpenXR asymmetric projection → re-project TL geometry → draw | **High** | #2 |
| 2 | Camera & player | `Camera.cpp`, `ArxGame::updateFirstPersonCamera` (`ArxGame.cpp:1220-1311`), `Player.cpp` | Body camera = tracking origin (stable position, no head bob); eyes = origin + HMD pose; ignore `focal` changes, roll and shake | Medium-high | #3 |
| 3 | Forced cameras | Speech cinematics, death camera, scripted cameras, `playerlookat`, screen shake, bow/Magic Sight zoom (`ArxGame.cpp:1313-1488`, `Quake.cpp`) | Show on a "theater" quad or fade-cut; drop forced pitch; replace shake with haptics | Medium | #3, #8 |
| 4 | Locomotion | `managePlayerControls` (`Interface.cpp:642-1241`), `Player.cpp` movement | Analog move vector into `tm`; snap/smooth turn into yaw; room-scale delta into `g_moveto` (collision cylinder follows); physical crouch threshold; disable edge-turning | Low-medium | #4 |
| 5 | Input layer | `src/input/Input.cpp`, `InputBackend.h`, `EERIEMouseButton` | New VR input module: OpenXR actions → action states, mouse-button bits, move vector, hand poses; synthetic absolute mouse for the UI panel | Medium | #4 |
| 6 | UI panel (menus, HUD, inventory, book, map, notes, character creation, shop) | `src/gui/**` (~11k LOC) | Render the 2D phase once into a `g_size` UI FBO; floating panel + laser pointer → synthetic mouse; ESC/menu, loading screens and logos onto the panel | Low (panel) | #7 |
| 7 | Cinematics & loading screens | `src/cinematic/*`, `LoadLevelScreen.cpp`, `MainMenu.cpp` | Render to the UI/theater FBO, show on a large quad; loading screens submit frames too | Low | #7 |
| 8 | World interaction | `FlyingOverObject` / `InterClick` / `GetFirstInterAtPos` (`Interactive.cpp:1627-1735`) | `GetFirstInterAlongRay` using existing raycasts, same filters and distance limits; ~8 call sites; trigger = ACTION, grip = USE; light ignition by ray-sphere | Medium | #5 |
| 9 | Pick up / drop / throw | `Dragging.cpp` | Start drag from hand ray; place via `findSpotForDraggedEntity(controllerPos, dir)`; throw with controller velocity | Medium | #5 |
| 10 | Equipment / paper doll | `Book.cpp:1020-1249` | Works on the UI panel as-is (drag between inventory and doll with the laser) | Low | #7 |
| 11 | Melee | `ManageCombatModeAnimations` (`Core.cpp:496-947`), `Equipment.cpp` | Weapon transform from controller pose; run `ARX_EQUIPMENT_Strike_Check` while tip speed exceeds a threshold, strength from swing speed; hide first-person arms | Medium-high | #5 |
| 12 | Bow | `Core.cpp:741-943` | Draw distance between hands → aim ratio; release → `ARX_THROWN_OBJECT_Throw` from controller pose; no zoom | Medium | #5 |
| 13 | Rune magic | `Spells.cpp:441-595`, `SpellRecognition.cpp`, `MagicFlare.cpp` | Project controller tip onto an HMD-facing plane → `ARX_SPELLS_AddPoint`; 3D flare overload at the tip; aim/target with hand ray; precasts on buttons | Low-medium | #6 |
| 14 | Dialog & subtitles | `Speech.cpp` | Subtitles on a head-locked or speaker-anchored quad; skip on a button | Low-medium | #7 |
| 15 | Full-screen effects | blood splash, paralysis, fades, letterbox | Per-eye full-viewport quads; vignette instead of letterbox | Low | #8 |
| 16 | Save thumbnails | `getSnapshot` → `glReadPixels` (`OpenGLRenderer.cpp:862-868`) | Read from an eye or UI FBO | Low | #7 |
| 17 | Enemies & AI | `NPC.cpp`, `Damage.cpp`, `src/ai/*` | No change beyond camera/culling | Low | — |

## Effort estimate

Estimates assume focused work by Claude on code plus the project owner testing in the headset.
"Week" means roughly one week of full-time-equivalent work; calendar time depends on how often
in-headset testing happens.

| Phase | Content | Estimate |
|---|---|---|
| 0 | Build, run the game, CI (epic #1) | 2–4 days |
| 1 | OpenXR session + stereo rendering incl. simulate/draw split and per-eye TL re-projection (epic #2) | 3–5 weeks |
| 2 | UI panel, laser pointer, menus, cinematics, loading screens (epic #7, panel approach) | 1.5–2.5 weeks |
| 3 | HMD camera, forced-camera handling (epic #3) | 1–2 weeks |
| 4 | VR input module, locomotion, turning, crouch (epic #4) | 1–1.5 weeks |
| 5 | Ray/hand interaction, pick up / drop / throw (epic #5, part) | 1.5–2 weeks |
| 6 | Rune casting (epic #6) | ~1 week |
| 7 | Melee and bow driven by controllers (epic #5, part) | 2–3 weeks |
| 8 | Comfort pass and a full playthrough with bug fixing (epic #8) | 2–4 weeks |
| | **Total to playable v1** | **≈ 14–24 weeks** |

For comparison: Half-Life: VR Mod (a single developer, part-time) took about five years to reach
its Steam release, including a lot of polish beyond v1. Quake VR reached dual-wielding, melee and
hand interaction within its first few public versions.

### Biggest risks

1. **Per-eye re-projection of CPU-projected geometry** (finding 2). If it proves too invasive, the fallback
   is the larger change of emitting world-space vertices and letting GL apply per-eye matrices.
2. **Performance:** the scene is drawn twice and much of it is CPU-transformed; fixed-function GL
   at 2×~2000×2200 pixels at 90 Hz needs profiling early.
3. **Simulate/draw split** in `renderLevel` touches many systems that currently update during drawing.
4. **Forced cameras in scripted scenes** may need per-scene handling found only during a playthrough.

## Recommended order

1. Epic #1 — build and run (in progress)
2. Epic #2 — stereo rendering, including the simulate/draw split (de-risk first)
3. Epic #7 (panel approach) — makes every menu reachable early, so the game is testable in VR
4. Epics #3 and #4 — camera and locomotion
5. Epic #5 — interaction, then melee and bow
6. Epic #6 — runes
7. Epic #8 — comfort pass and full playthrough

## Work not yet on the board

These came out of the code review and should become tasks when the relevant epic starts
(per [WORKFLOW.md](WORKFLOW.md), the plan grows as we go):

- Epic #2: split `renderLevel()` into simulate-once / draw-per-eye; combined-frustum culling;
  per-eye re-projection of TL geometry; decouple `g_size` (UI) from the eye viewport;
  screen-space sprite billboarding per eye (`Draw.cpp:72-124`)
- Epic #3: handle speech-driven cinematic cameras, death camera, scripted cameras, `playerlookat`, FOV changes
- Epic #4: VR input module (OpenXR actions → `EERIEMouseButton` / action states / analog move vector);
  disable edge-turning; room-scale delta into the collision cylinder
- Epic #5: `GetFirstInterAlongRay`; combine-items flow; light ignition by ray; hide first-person arms
- Epic #7: frames during loading screens and logos; save thumbnails from FBO; character creation on the panel
- Epic #8: replace cinematic letterbox with a vignette; full-screen effects per eye

## Sources

- [Road to VR — Quake VR with 6DOF controls and melee](https://roadtovr.com/?p=95191)
- [UploadVR — Quake VR mod update](https://www.uploadvr.com/quake-vr-mod-update/)
- [Mixed News — Half-Life VR mod released with Valve's blessing](https://mixed-news.com/en/?p=9009)
- [Road to VR — Half-Life VR mod coming to Steam](https://roadtovr.com/half-life-vr-mod-steam-quest-2-release/)
- [Gaming Trend — Skyrim VR review (floating menu panels)](https://gamingtrend.com/reviews/foos-ro-dahhhooh-my-goodness-the-elder-scrolls-v-skyrim-vr-vive-review)
- [CGM — Skyrim VR review (menu navigation criticism)](https://www.cgmagonline.com/review/elder-scrolls-v-skyrim-vr-review-fresh-ideas-bad-controls)
