# Arx Fatalis VR — MVP

The first goal of the project. Ship a short, rough but convincing demo of the core VR experience
as fast as possible, show it to people, and only invest in polish and the full game if it lands.

- **Milestone:** `M1 MVP`
- **Board:** every MVP task is in **Todo** with milestone `M1 MVP`; everything else is in **Backlog**.
- **Rule of thumb:** if a task is not needed to play the demo scenario below, it is not MVP.
- **Design decisions that shape the MVP:** stick forward = look direction (D-008), full body with IK
  arms (D-009), no rays from the hands — runes drawn in the air, a pointer only in menus (D-010); world
  interaction — both hands work as the mouse, each with its own reticle; the inventory opens in the
  centre of the view (D-016). See [DECISIONS.md](DECISIONS.md); features taken from reference mods are in
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
3. Pick up items, open doors and chests, pull a lever — **each hand works as the mouse with its own
   reticle**; with a sword in the right hand, open the inventory and drag an item from the floor into it
   with the left hand (D-016).
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
| Rendering | OpenXR stereo at the headset's native rate; desktop mirror (a copy of the right eye — the owner's choice of 2026-10-10; `vr_mirror_eye`) |
| Camera | The camera is where the head is: HMD position and orientation; continuous height with a game crouch below a threshold; leaning and real steps with the body following and a wall fade; the standing head = the character's eye height; correct world scale; recenter, seated play (D-018) |
| Body | The full player model: legs and torso animated by the engine, head hidden, arms driven by the controllers via IK (D-009) |
| Locomotion | Thumbstick movement relative to the look direction, snap and smooth turn, physical crouch, crouch toggle (right stick down) and jump (A) |
| Input | OpenXR actions with Index bindings, mapped onto the engine's existing actions; the trigger always works as a fallback |
| Interaction | Both hands work as the mouse, each with its own reticle and no visible beam: drag items over surfaces and into the inventory (which opens in the centre of the view), use doors / levers / chests / bodies (D-016; reference: VHVR, #166) |
| Melee | The weapon in the model's hand; hits by contact plus a swing detector; damage from swing speed; haptics |
| Magic | Runes drawn in the air with the hand, fed into the existing recognizer; spell direction from the hand, invisible |
| VR settings | Our own VR settings, opened from the main and pause menus: in the MVP the **Controls** section — remapping (button, chord, long press per event; per controller type; reset to default), turning, crouch and height, left-handed mode (D-020); sound and graphics stay in the game's settings |
| UI | Menus, inventory, book, map, dialogs and notes on one floating panel with a pointer only while it is open; health and mana on the left wrist |
| Comfort | Head bob, screen shake and FOV zoom disabled |
| Testing | Workers verify without the headset first: smoke test and command channel, simulated VR backend with scripted sequences, host unit tests |

## Accepted for the MVP (known problems we live with)

- The menus are the original 2D interface on a flat floating panel — functional, not pretty.
- The arms follow the controllers and the fingers the real fingers (#305; three curls per hand — the model has
  the middle, ring and little finger as one block), but engine animations that take over the body
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
| **B. Body and interaction** | Floating panel with menus, inventory and book; pointer in menus; your full body with arms following the controllers; health and mana on the left wrist; items, doors, chests, levers with the hand-aimed reticle; start a new game from the menu in VR; tester build for a friend |
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
7. #124 — Development launch: skip all cinematics (new-game intro, in-game `.cin`) — straight into the game or the menu
7. #123 — Parallel development tooling: worktrees, isolated self-test folders, test-pr station, VR lock (switches on parallel work)
7. #93 — Bug: crash on exit after a level load (dangling `m_owner` in `DanaeClearLevel`) — breaks VR self-tests and headset exits
7. #115 — Log banner and the OpenXR runtime that answered (F-18, F-98)
7. #129 — Diagnostics D1: the log always on disk (flush on warnings/errors and every second), last 10 runs kept
7. #131 — Diagnostics D3: automatic log digest after every run and automatic bug filing (`auto-reported`)
7. #130 — Diagnostics D2: game event log — one line per game and VR state change
7. #127 — Flat smoke test in CI on the self-hosted runner (follow-up of #99)
7. #147 — Launch control for parallel workers: never start SteamVR, one game at a time, own fresh build, launch ledger (D-014)
8. #18 — Per-eye swapchains; first visible result: the flat frame on a head-locked quad (F-20)
9. #20 — Frame loop with pose-matched submission and a rate / cadence line (F-07, F-21, F-26)
10. #21 — Desktop mirror = a copy of one eye, the right one by default (F-24)
11. #112 — Simulated VR backend `--vr-sim` and the two-eyes check (F-04, F-06)
12. #113 — Scripted VR test sequences and screenshot comparison (F-05, F-14)
13. #71 — Split `renderLevel()` into simulate-once and draw-per-eye (F-22)
14. #19 — Render the scene twice with per-eye view and projection; accepted by the two-eyes check (F-06, F-39)
15. #72 — Per-eye re-projection of CPU-projected (TL) geometry
16. #73 — Decouple the UI virtual screen size from the eye render target size
17. #24 — HMD pose (orientation and position) → camera, no neck model (D-018, F-21, F-31, F-37)
18. #27 — World scale; the standing head = the character's eye height (D-018)
19. #76 — VR comfort defaults: no view bobbing, screen shake, FOV zoom (F-31)
20. #28 — Recenter = calibration on a long press of the left stick (D-018, D-019, F-34)
21. #114 — Host unit tests in the build and CI (F-08)
22. #100 — Physical crouch: the game crouch below a height threshold (D-018, F-29)
22. #26 — Leaning and real steps: the body follows the head with collision; wall fade (D-018)
23. #30 — OpenXR action set with gameplay-named actions
24. #35 — Suggested bindings: Index first, Quest 3, simple controller (F-41, F-47)
25. #74 — VR input module: logical action snapshot, tap / hold (F-42, F-43)
26. #25 — Decouple head direction from body direction (D-008)
27. #31 — Smooth stick locomotion relative to the look direction (D-008)
28. #32 — Snap turn and smooth turn (F-32)

### B. Body and interaction
29. #118 — Research continued (#107): TinyBlackDog's open-source Titanfall 2 VR — extends the reference-mods catalog
29. #120 — Research continued (#107): CircuitLord's Titanfall 2 VR (closed source) — full body IK, weapons, installer
*(the owner decides on the new catalog rows before #36, #116, #49 and #88)*
29. #49 — Floating UI panel for menus, inventory, book, map (F-80, F-82)
30. #34 — Menu pointer only while a menu or the inventory is open (D-010, F-81)
30. #178 — VR settings screen with the Controls section: remapping, turning, crouch and height, left-handed (D-020)
31. #33 — Jump on A, crouch toggle on right stick down, stealth by partial deflection (F-47)
32. #70 — Test save library for MVP scenarios *(done: new game, items, goblin fight; levers, doors and chests are #210, recorded by the owner)*
33. #36 — Full body: the player model with arms driven by the controllers via IK (D-009, F-52, F-49)
34. #116 — Health and mana as two bars on the left wrist (F-83)
35. #166 — Research: world interaction in Valheim VR — the reference for D-016, #37, #38
35. #37 — Items with a reticle per hand: both hands work as the mouse — drag, set down, into the inventory (D-016, F-57, F-72)
36. #38 — Doors, levers, chests and bodies with the same reticle (D-016, F-57)
37. #79 — Find the Arx Fatalis data automatically in Steam libraries (F-97)
38. #88 — Tester build for a friend (F-91, F-96, F-100) *(then the friend tests on Quest 3 — #87, Backlog until then)*
38. #132 — Diagnostics D4: crash reports under SteamVR, debug symbols kept for every tester build

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

## Parallel plan

How the queue splits into **waves** of tasks that can run at the same time (see "Parallel work" in
[WORKFLOW.md](WORKFLOW.md)). Every task carries an area label (`area:…`) and, if it will need the
owner's headset, `lane:vr`. Within a wave, tasks of **different areas** run together; no worker cap (D-027), the VR lane at most 2 in progress and 4 in Testing. The queue order above stays the
tie-breaker. The coordinator keeps this plan current in `/new-cycle`.

### Dependencies

| Task | Area | Lane | Depends on |
|---|---|---|---|
| #99 smoke test, command channel | build | plain | — |
| #124 skip cinematics | ui | plain | — |
| #123 parallel tooling | build | plain | #99 |
| #93 bug: crash on exit after a level load | core | plain | — |
| #115 log banner | core | plain | — |
| #129 D1 log always on disk, 10 runs | core | plain | #115 |
| #131 D3 auto digest and bug filing | build | plain | #129, #115, #123 |
| #130 D2 game event log | core | plain | #129 |
| #127 smoke test in CI | build | plain | #99, #131 |
| #147 launch control: no SteamVR start, game lock, fresh build, ledger (D-014) | build | plain | #123 |
| #18 swapchains, quad first | vr-runtime | VR | — |
| #20 frame loop | vr-runtime | VR | #18 |
| #21 desktop mirror | vr-runtime | VR | #20 |
| #112 simulated VR backend | vr-runtime | plain | #20, #21, #99 |
| #113 scripted sequences | build | plain | #112 |
| #71 simulate once, draw per eye | render | plain | — |
| #19 render twice per eye | render | VR | #71, #20 (accepted via #112) |
| #72 per-eye TL re-projection | render | VR | #19 |
| #73 UI size vs eye size | ui | plain | #19 |
| #24 HMD orientation → camera | camera | VR | #20 |
| #27 scale and eye height | camera | VR | #24 |
| #76 comfort defaults | camera | plain | — |
| #28 recenter (calibration, long press) | camera | VR | #24, #74 |
| #114 host unit tests in CI | build | plain | — |
| #100 physical crouch | camera | VR | #24, #28, #114 |
| #26 leaning, real steps, wall fade | camera | VR | #24, #25, #27 |
| #30 OpenXR action set | input | plain | — |
| #35 bindings | input | VR | #30 |
| #74 input module | input | VR | #30 |
| #25 head / body decouple | camera | VR | #24 |
| #31 locomotion | input | VR | #74, #25 |
| #32 turning | input | VR | #74 |
| #118, #120 research | docs | plain | — (decisions before #36, #116, #49, #88) |
| #49 UI panel | ui | VR | #73 |
| #178 VR settings: Controls | ui | VR | #30, #35, #49, #34, #32, #33, #28, #100 |
| #34 menu pointer | ui | VR | #49, #74 |
| #33 jump, crouch toggle, stealth | input | VR | #74 |
| #70 test saves | build | plain | #99, #123 |
| #36 full body with IK arms | render | VR | #72, #24, #74, decisions on #118 / #120 |
| #116 wrist vitals | ui | VR | #36 |
| #166 research: VHVR world interaction | docs | plain | — (owner's decisions before #37 / #38) |
| #37 a reticle per hand: drag, into the inventory | combat | VR | #74, #166 (no longer #36, D-016) |
| #38 doors, levers, chests | combat | VR | #37 |
| #79 Steam data detection | core | plain | — |
| #88 tester build | build | plain | #79, #123, decisions on #118 / #120 |
| #132 D4 crash reports under SteamVR, symbols | build | plain | #88, #131 |
| #75 weapon in the IK hand | combat | VR | #36 |
| #39 melee | combat | VR | #75, #114 |
| #42 haptics | input | VR | #74 |
| #43, #44, #45 runes | magic | VR | #36, #74 |
| #80 launch from SteamVR | core | plain | — |
| #16 developer docs | docs | plain | late, once the tooling settles |
| #77 gameplay video | docs | VR | everything else |

### Waves

| Wave | VR lane (needs the headset) | Plain lane (no headset) |
|---|---|---|
| **0** — sequential, until #123 is merged | — | #99 → #124 → #123 |
| **1** | #18 → #20 → #21 | #147, #93, #71, #114, #115 → #129, #76, #118, #120, #79, #141, #151, #155 |
| **2** | #24, #19 | #112 → #113, #30, #131, #130, #166 |
| **3** | #74, #72, #27, #25, #35 | #73, #70, #127 |
| **4** — finishes checkpoint A | #28, #100, #26, #31, #32, #42 | #80 |
| **5** — checkpoint B | #49, #33, #36 | #88 → #132 *(after the owner's decisions on #118 / #120)* |
| **6** | #34, #116, #37, #75, #178 | — |
| **7** — checkpoints C, D | #38, #39, #43 → #44 → #45 | #16 |
| **8** — MVP done | #77 | — |

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
