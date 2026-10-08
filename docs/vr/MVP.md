# Arx Fatalis VR — MVP

The first goal of the project. Ship a short, rough but convincing demo of the core VR experience
as fast as possible, show it to people, and only invest in polish and the full game if it lands.

- **Milestone:** `M1 MVP`
- **Board:** every MVP task is in **Todo** with milestone `M1 MVP`; everything else is in **Backlog**.
- **Rule of thumb:** if a task is not needed to play the demo scenario below, it is not MVP.

## Demo scenario (what the MVP must show)

Played in a Valve Index from a new game, at the start of Arx Fatalis (the goblin prison and the
first areas after it):

0. Start SteamVR and launch Arx Fatalis VR — it detects the headset and finds the Arx Fatalis data
   from the Steam library on its own and starts in VR, with no setup (like the Valheim VR mod).
1. Put on the headset — the world is in correct stereo and scale; looking around is head-tracked.
2. Walk around with the thumbstick, turn with snap or smooth turn, crouch, jump.
3. Pick up items, open doors and chests, pull a lever — by pointing the controller and pressing a button.
4. Open the inventory and character book on a **floating panel**, equip a weapon with the laser pointer.
5. Fight a goblin by **physically swinging** the weapon held in the controller; hits register and feel
   right (haptics on impact).
6. Cast a basic spell (e.g. Magic Missile) by **drawing the runes in the air** with the controller.
7. Open the main menu on the panel, save, and load.

The desktop window mirrors one eye, so the demo can be recorded and shown.

## In scope

| Area | MVP version |
|---|---|
| Launch | VR starts automatically when a headset is available; game data found in Steam libraries; launchable from SteamVR |
| Rendering | OpenXR stereo at the headset's native rate; desktop mirror |
| Camera | HMD orientation drives the view; head decoupled from body; correct world scale; recenter |
| Locomotion | Thumbstick movement, snap and smooth turn, crouch and jump on buttons |
| Input | OpenXR actions with Index bindings, mapped onto the engine's existing actions |
| Interaction | Controller ray to select, pick up, use doors / levers / chests |
| Melee | Weapon follows the controller; hits from swing speed; haptics on hit |
| Magic | Gesture rune drawing fed into the existing recognizer |
| UI | The entire 2D layer (menus, HUD, inventory, book, map, dialogs, notes) on one floating panel, laser pointer as mouse |
| Comfort | Head bob, screen shake and FOV zoom disabled |

## Accepted for the MVP (known problems we live with)

- UI is the original 2D interface on a flat floating panel — functional, not pretty.
- No visible hand models beyond simple placeholders; no finger tracking.
- Bow, shield, prepared spells, spell book in VR: not in MVP (they may work through the panel, or not).
- Cutscenes, death camera and scripted cameras may behave badly.
- Screen-space effects (flares, fog, fades) may look wrong in stereo.
- No positional tracking safeguards (leaning into walls), no vignette, no VR settings menu.
- No Steam build.

## Definition of done

- The demo scenario can be played end to end in the headset without keyboard or mouse.
- No motion-sickness triggers caused by the engine (forced camera motion, head bob, shake) in the scenario.
- Frame rate holds the headset's refresh rate in the scenario on the owner's PC.
- A gameplay video of the scenario is recorded from the desktop mirror.

## Checkpoints

The MVP is reached through checkpoints, each one something the owner can try in the headset.
Tasks are done strictly one after another, each tested before the next starts.

| Checkpoint | What the owner can do in the headset |
|---|---|
| **A. Walk in VR** *(current)* | Stereo view, look around with the head, correct scale, walk and turn with the sticks. Started with `--skiplogo --loadslot <n>`, no in-VR menu yet |
| **B. Interact** | Floating panel with menus, inventory and book; laser pointer; pick up items, doors, chests, levers; start a new game from the menu in VR |
| **C. Fight** | Weapon in the controller hand, physical swings, haptics on hit |
| **D. Cast** | Draw runes in the air to cast spells |
| **E. MVP done** | Launch from SteamVR with no setup, demo scenario end to end, gameplay video |

## Task queue

The exact order in which MVP tasks are picked up. The coordinating session hands out tasks in this
order; `/next-task` (via `scripts/dev/next-task.ps1`) takes the first task in this list that is in
**Todo** on the board. Keep it up to date: when a task is added to the MVP, insert it where its
dependencies are met; when a task is split, replace it with its parts.

### A. Walk in VR
1. #17 — Integrate OpenXR; start in VR automatically when a headset is available
2. #89 — Product name and image in SteamVR (the owner's key art: `data/vr/key-art.png`)
3. #18 — Per-eye swapchains, render into swapchain images via FBO
3. #20 — Frame loop: `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`
4. #21 — Desktop mirror (one eye)
5. #71 — Split `renderLevel()` into simulate-once and draw-per-eye
6. #19 — Render the scene twice with per-eye view and projection
7. #72 — Per-eye re-projection of CPU-projected (TL) geometry
8. #73 — Decouple the UI virtual screen size from the eye render target size
9. #24 — HMD orientation → camera orientation
10. #27 — World scale: meters to Arx units, height calibration
11. #76 — VR comfort defaults: no view bobbing, screen shake, FOV zoom
12. #28 — Recenter; seated / standing modes
13. #30 — OpenXR action set
14. #35 — Suggested bindings for the Index controller profile
15. #74 — VR input module: actions → engine action states, mouse buttons, analog move vector
16. #25 — Decouple head direction from body direction
17. #31 — Smooth stick locomotion
18. #32 — Snap turn and smooth turn

### B. Interact
19. #49 — Floating UI panel for the whole 2D layer
20. #34 — Laser-pointer interaction as mouse on the panel
21. #33 — Jump, crouch, stealth
22. #70 — Test save library for MVP scenarios *(the owner records the saves; Claude prepares the list and instructions)*
23. #36 — Placeholder hand models at controller poses
24. #37 — Object selection by ray; picking up and throwing items
25. #38 — Doors, levers, chests, searching bodies
26. #79 — Find the Arx Fatalis data automatically in Steam libraries
27. #88 — Tester build: zip package that runs on another PC *(then a friend tests on Quest 3 — #87, Backlog until then)*

### C. Fight
28. #75 — Equipped weapon follows the controller pose
29. #39 — Melee: hits driven by controller speed and trajectory
30. #42 — Haptic feedback

### D. Cast
31. #43 — Magic mode: glowing trail behind the controller
32. #44 — Project the 3D stroke onto a plane → 2D strokes into the recognizer
33. #45 — Tune recognition tolerances

### E. MVP done
34. #80 — Launch from SteamVR
35. #16 — Developer documentation: how to build and run
36. #77 — Record an MVP gameplay video

## Estimate

Roughly **8–12 weeks** of full-time-equivalent work, most of it in stereo rendering (3–5 weeks).
See [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md) for the analysis behind the numbers.

## After the MVP

`M2 Full Game & Polish`: everything needed to finish the whole game comfortably — bow and shield,
diegetic UI, cutscene handling, comfort options, performance work, full playthrough.
`M3 Steam Release`: rights, licensing, store page, publishing.
