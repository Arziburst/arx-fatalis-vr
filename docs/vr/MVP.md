# Arx Fatalis VR — MVP

The first goal of the project. Ship a short, rough but convincing demo of the core VR experience
as fast as possible, show it to people, and only invest in polish and the full game if it lands.

- **Milestone:** `M1 MVP`
- **Board:** every MVP task is in **Todo** with milestone `M1 MVP`; everything else is in **Backlog**.
- **Rule of thumb:** if a task is not needed to play the demo scenario below, it is not MVP.
- **Design decisions that shape the MVP:** stick forward = look direction (D-008), full body with IK
  arms (D-009), no rays from the hands — runes drawn in the air, grab by reach, a pointer only in menus
  (D-010). See [DECISIONS.md](DECISIONS.md); features taken from reference mods are in
  [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) (*Decision* column).

## Demo scenario (what the MVP must show)

Played in a Valve Index from a new game, at the start of Arx Fatalis (the goblin prison and the
first areas after it):

0. Start SteamVR and launch Arx Fatalis VR — it detects the headset and finds the Arx Fatalis data
   from the Steam library on its own and starts in VR, with no setup (like the Valheim VR mod).
1. Put on the headset — the world is in correct stereo and scale; looking around is head-tracked;
   looking down you see **your own body**, and your arms follow the controllers.
2. Walk around with the thumbstick (forward = where you look), turn with snap or smooth turn, crouch
   (physically or with the right stick), jump (A).
3. Pick up items, open doors and chests, pull a lever — **by reaching and grabbing with the hand**.
4. Open the inventory and character book on a **floating panel** and equip a weapon with the short
   pointer that appears only while a menu is open. Health and mana are **two bars on the left wrist**.
5. Fight a goblin by **physically swinging** the weapon held in the character's hand; hits register
   and feel right (damage from swing speed, haptics on impact).
6. Cast a basic spell (e.g. Magic Missile) by **drawing the runes in the air with the hand**; the spell
   flies where the hand points.
7. Open the main menu on the panel, save, and load.

The desktop window mirrors one eye, so the demo can be recorded and shown.

## In scope

| Area | MVP version |
|---|---|
| Launch | VR starts automatically when a headset is available; game data found in Steam libraries; launchable from SteamVR |
| Rendering | OpenXR stereo at the headset's native rate; desktop mirror (a copy of the left eye) |
| Camera | HMD orientation drives the view; eye height = the character's camera (D-006); correct world scale; recenter |
| Body | The full player model: legs and torso animated by the engine, head hidden, arms driven by the controllers via IK (D-009) |
| Locomotion | Thumbstick movement relative to the look direction, snap and smooth turn, physical crouch, crouch toggle (right stick down) and jump (A) |
| Input | OpenXR actions with Index bindings, mapped onto the engine's existing actions; the trigger always works as a fallback |
| Interaction | Reach and grab with the hand: pick up, throw, use doors / levers / chests / bodies — no rays (D-010) |
| Melee | The weapon in the model's hand; hits by contact plus a swing detector; damage from swing speed; haptics |
| Magic | Runes drawn in the air with the hand, fed into the existing recognizer; spell direction from the hand, invisible |
| UI | Menus, inventory, book, map, dialogs and notes on one floating panel with a pointer only while it is open; health and mana on the left wrist |
| Comfort | Head bob, screen shake and FOV zoom disabled |
| Testing | Workers verify without the headset first: smoke test and command channel, simulated VR backend with scripted sequences, host unit tests |

## Accepted for the MVP (known problems we live with)

- The menus are the original 2D interface on a flat floating panel — functional, not pretty.
- No finger tracking; the arms follow the controllers, but engine animations that take over the body
  (death, ladders, knockdowns) may look wrong.
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
| **A. Walk in VR** *(current)* | Stereo view, look around with the head, correct scale, walk and turn with the sticks, physical crouch. Started with `--skiplogo --loadslot <n>`, no in-VR menu yet |
| **B. Body and interaction** | Floating panel with menus, inventory and book; pointer in menus; your full body with arms following the controllers; health and mana on the left wrist; grab items, doors, chests, levers by hand; start a new game from the menu in VR; tester build for a friend |
| **C. Fight** | Weapon in the character's hand, physical swings, haptics on hit |
| **D. Cast** | Draw runes in the air with the hand to cast spells |
| **E. MVP done** | Launch from SteamVR with no setup, demo scenario end to end, gameplay video |

## Task queue

The exact order in which MVP tasks are picked up. The coordinating session hands out tasks in this
order; `/next-task` (via `scripts/dev/next-task.ps1`) takes the first task in this list that is in
**Todo** on the board. Keep it up to date: when a task is added to the MVP, insert it where its
dependencies are met; when a task is split, replace it with its parts.

### A. Walk in VR
1. #17 — Integrate OpenXR; start in VR automatically when a headset is available *(done)*
2. #89 — Product name and image in SteamVR *(done)*
3. #103 — Product icon in `arx.exe` *(done)*
4. #98 — Dev launch finds the engine's own data (`data/core`) *(done)*
5. #107 — Research reference VR mods *(done — decisions in the catalog)*
6. #99 — Smoke test and command channel: `status.json`, script-console commands, `xr_hello` probe, flat and VR launch (F-01, F-02, F-03, F-16)
7. #115 — Log banner and the OpenXR runtime that answered (F-18, F-98)
8. #18 — Per-eye swapchains; first visible result: the flat frame on a head-locked quad (F-20)
9. #20 — Frame loop with pose-matched submission and a rate / cadence line (F-07, F-21, F-26)
10. #21 — Desktop mirror = a copy of the left eye (F-24)
11. #112 — Simulated VR backend `--vr-sim` and the two-eyes check (F-04, F-06)
12. #113 — Scripted VR test sequences and screenshot comparison (F-05, F-14)
13. #71 — Split `renderLevel()` into simulate-once and draw-per-eye (F-22)
14. #19 — Render the scene twice with per-eye view and projection; accepted by the two-eyes check (F-06, F-39)
15. #72 — Per-eye re-projection of CPU-projected (TL) geometry
16. #73 — Decouple the UI virtual screen size from the eye render target size
17. #24 — HMD orientation → camera orientation, no neck model (F-21, F-31, F-37)
18. #27 — World scale and eye height = the character's camera (D-006)
19. #76 — VR comfort defaults: no view bobbing, screen shake, FOV zoom (F-31)
20. #28 — Recenter on a held button chord (F-34)
21. #114 — Host unit tests in the build and CI (F-08)
22. #100 — Physical crouch (D-006, F-29)
23. #30 — OpenXR action set with gameplay-named actions
24. #35 — Suggested bindings: Index first, Quest 3, simple controller (F-41, F-47)
25. #74 — VR input module: logical action snapshot, tap / hold (F-42, F-43)
26. #25 — Decouple head direction from body direction (D-008)
27. #31 — Smooth stick locomotion relative to the look direction (D-008)
28. #32 — Snap turn and smooth turn (F-32)

### B. Body and interaction
29. #49 — Floating UI panel for menus, inventory, book, map (F-80, F-82)
30. #34 — Menu pointer only while a menu or the inventory is open (D-010, F-81)
31. #33 — Jump on A, crouch toggle on right stick down, stealth by partial deflection (F-47)
32. #70 — Test save library for MVP scenarios *(scripts via the #99 channel where possible; the owner records the rest)*
33. #36 — Full body: the player model with arms driven by the controllers via IK (D-009, F-52, F-49)
34. #116 — Health and mana as two bars on the left wrist (F-83)
35. #37 — Pick up, use and throw items with the hand — reach and grab, no ray (D-010, F-57, F-72)
36. #38 — Doors, levers, chests and bodies with the hand (D-010, F-57)
37. #79 — Find the Arx Fatalis data automatically in Steam libraries (F-97)
38. #88 — Tester build for a friend (F-91, F-96, F-100) *(then the friend tests on Quest 3 — #87, Backlog until then)*

### C. Fight
39. #75 — Weapon held by the IK hand; no engine arm attack animation while IK drives the arms (F-48, F-67)
40. #39 — Melee: contact hits, swing detector, stab rule, per-target cooldown, damage from swing speed (F-58–F-63, F-66, F-74)
41. #42 — Haptic feedback (F-44)

### D. Cast
42. #43 — Magic mode: the rune trail follows the hand in the air (D-010, F-77)
43. #44 — Stroke → recognizer; spell direction from the hand, invisible (F-75, F-76, F-77)
44. #45 — Tune recognition tolerances

### E. MVP done
45. #80 — Launch from SteamVR
46. #16 — Developer documentation: how to build and run
47. #77 — Record an MVP gameplay video

## Estimate

Roughly **9–14 weeks** of full-time-equivalent work: stereo rendering is the largest part (3–5 weeks);
the full body with IK arms (D-009) adds about 1–2 weeks; the testing tools (#99, #112, #113, #114) cost
a few days up front and save headset sessions afterwards. See
[RESEARCH-playable-v1.md](RESEARCH-playable-v1.md) for the analysis behind the base numbers.

## After the MVP

`M2 Full Game & Polish`: everything needed to finish the whole game comfortably — shield and blocking
first (F-68, F-69), bow (F-70), diegetic UI (book in hand, radial menu), cutscene handling, comfort
options, performance work, full playthrough. The *Later* rows of
[RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) are the candidate list.
`M3 Steam Release`: rights, licensing, store page, publishing.
