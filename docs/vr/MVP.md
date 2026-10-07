# Arx Fatalis VR — MVP

The first goal of the project. Ship a short, rough but convincing demo of the core VR experience
as fast as possible, show it to people, and only invest in polish and the full game if it lands.

- **Milestone:** `M1 MVP`
- **Board:** every MVP task is in **Todo** with milestone `M1 MVP`; everything else is in **Backlog**.
- **Rule of thumb:** if a task is not needed to play the demo scenario below, it is not MVP.

## Demo scenario (what the MVP must show)

Played in a Valve Index from a new game, at the start of Arx Fatalis (the goblin prison and the
first areas after it):

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
- No CI, no Steam build.

## Definition of done

- The demo scenario can be played end to end in the headset without keyboard or mouse.
- No motion-sickness triggers caused by the engine (forced camera motion, head bob, shake) in the scenario.
- Frame rate holds the headset's refresh rate in the scenario on the owner's PC.
- A gameplay video of the scenario is recorded from the desktop mirror.

## Order of work

1. **Build & run** — epic #1 (the game must run before anything can be tested)
2. **Stereo rendering** — epic #2 (largest and riskiest; de-risk first)
3. **Floating UI panel + laser pointer** — epics #7 and #4 (makes the whole game reachable in VR early)
4. **Head tracking & camera** — epic #3
5. **Input module & locomotion** — epic #4
6. **Interaction** — epic #5
7. **Melee** — epic #5
8. **Rune casting** — epic #6
9. **Comfort defaults, demo run, video** — epics #8 and #9

## Estimate

Roughly **8–12 weeks** of full-time-equivalent work, most of it in stereo rendering (3–5 weeks).
See [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md) for the analysis behind the numbers.

## After the MVP

`M2 Full Game & Polish`: everything needed to finish the whole game comfortably — bow and shield,
diegetic UI, cutscene handling, comfort options, performance work, full playthrough.
`M3 Steam Release`: rights, licensing, store page, publishing.
