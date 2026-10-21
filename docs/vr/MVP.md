# Arx Fatalis VR — MVP

- **Milestone:** `M1 MVP`
- **Board:** every MVP task is in **Todo** (or later) with milestone `M1 MVP`; everything else is in **Backlog**.
- **Design decisions that shape the MVP:** stick forward = look direction (D-008), full body with IK
  arms (D-009), no rays from the hands — runes drawn in the air, a pointer only in menus (D-010); world
  interaction — both hands work as the mouse, each with its own reticle; the inventory opens in the
  centre of the view (D-016); the camera is where the head is (D-018); the MVP control scheme (D-019).
  See [DECISIONS.md](DECISIONS.md); features taken from reference mods are in
  [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) (*Decision* column).

## Definition of done (owner, 2026-10-22)

The whole of Arx Fatalis can be played through in a Valve Index, from a plain launch to the end credits,
**without keyboard or mouse**.

- **Bugs and clumsy-but-working paths are accepted.** A player has to be able to get past every obstacle in VR;
  a keyboard workaround does not count. Example: using and combining items works today by a double trigger click,
  and throwing works but is clumsy — both count as playable.
- **Every feature the owner asked for in M1 is part of the MVP**: the body, effects in stereo, damage numbers, two
  hands, the torch in the hand, haptics, fist damage. Otherwise it is a half port. There is no separate "polish"
  class, no "wow" label and no early demo checkpoint.
- **Every mechanic the main quest needs works in VR:** locomotion, ladders, doors, levers and chests, keys and
  lockpicks, using an item on something, the bag and equipment, the book (level-up, spells, map, quests), notes and
  books, merchants (including giving gold), melee, the spells drawn by hand, target spells, scenes and cinematics,
  level exits by walking in, saving and loading, death and reload, the ending and the credits.
- **Every game level (0-8, 11-23) loads and plays in VR.**
- **No new motion-sickness triggers** beyond those accepted below.
- **Frame rate: guaranteed in level 1 only.** Elsewhere a drop found later is a bug, not a blocker (the game is old
  and has no heavy levels; the Index also runs at 80 Hz).

## How it is verified

1. **CI on the simulated headset** (`--vr-sim`), green on main: the level tour (#596: every level loads, renders
   and logs its frame time), the mechanics probe (#55: shop buy and sell with prices, a note and a multi-page
   book, level-up points, a second bag, a ladder, death and reload, key on door, item on item, item to NPC), and the
   story saves (#597: saves at the level entries made without the owner's hand recording; a check from the last
   save through ENDGAME to the credits).
2. **The owner's own play** in the headset with marks (D-062). The marks become bugs, which are fixed in blocks.
   Game captures stay in CI artifacts and local folders, never in git.

## In scope

| Area | MVP version |
|---|---|
| Launch | VR starts automatically when a headset is available; game data found in Steam libraries; launchable from SteamVR |
| Rendering | OpenXR stereo at the headset's native rate; desktop mirror (a copy of the right eye; `vr_mirror_eye`); effects correct in stereo |
| Camera | The camera is where the head is: HMD position and orientation; continuous height with a game crouch below a threshold; leaning and real steps with the body following and a wall fade; correct world scale; recenter, seated play (D-018) |
| Body | The full player model: legs and torso animated by the engine, head hidden, arms and fingers driven by the controllers via IK (D-009) |
| Locomotion | Thumbstick movement relative to the look direction, snap and smooth turn, physical crouch, crouch toggle and jump |
| Input | OpenXR actions with Index bindings, mapped onto the engine's actions; remapping in our own VR settings; the trigger always works as a fallback |
| Interaction | Both hands work as the mouse, each with its own reticle and no visible beam: items, doors, levers, chests, bodies, use and combine (D-016) |
| Melee and hands | The weapon in the model's hand; contact hits plus a swing detector; damage from swing speed; two held items, dual wielding, fist damage; the torch in the hand; haptics |
| Magic | Runes drawn in the air with the hand, fed into the existing recognizer; both hands; target spells chosen with an icon on the hand |
| UI | Menus, inventory, loot, book, map, dialogs and notes on floating panels, readable text, a pointer only while a menu is open; health and mana on the left wrist; NPC lines above the head |
| Comfort | Head bob, screen shake and FOV zoom disabled |

## Accepted problems

- Engine animations that take over the body (death, ladders, knockdowns), cutscenes, the death camera and scripted
  cameras may look wrong.
- Bow, shield, stealing, pausing on the SteamVR dashboard and other combat extras wait in M2 or Backlog unless a
  sprint below lists them.
- No Steam build (M3).

## Sprints

A sprint = **2-4 headset tasks the owner tests together in one session** (one combined test branch, variants
switchable on the #588 panel) **+ 2-4 headset-free tasks**. The issues carry the label `sprint-N`. The plan below
was approved by the owner on 2026-10-22; (Fable) marks the sprints whose mechanics are fundamental and run on the
strongest model.

| Sprint | Headset | Headset-free | Why now |
|---|---|---|---|
| 1 Picture to the outside | #576 mirror + streaming view (radials and reticles drawn like the subtitles, visible in the mirror; #518 folded in); #484 (PR #538) orbs; #406 focus loss | #569, #486, #596, #541 | #576 unblocks #437 and the video; #486 unblocks #548; #569 unblocks #583 |
| 2 Readable text | #586 font (variants); #521 NPC lines above the head; #571 instant skip; #477 message panel | #55 mechanics probe, #597 story saves (research), #528, #494 | one "reading" session; the panels need the font first |
| 3 Saves, controls, playground | #583 save tabs + autosave with #417 VR saves; #547 new controls (+#474); #481 playground v2 | #597 (implementation), #535, #540, #561 | #547 before #378; the playground serves the combat and magic sprints |
| 4 Magic (Fable) | #378 casting with both hands (runes in a line below the view centre, quest spells); #595 target spells | fixes from #55, #554, #462 | the biggest playthrough gap |
| 5 Hands and inventory (Fable) | #598 inventory + loot windows (the owner shows with marks first); #288 reticles with the weapon drawn; #476, #475; #478 purse + giving gold; #570 | #473, #566 | all touch the inventory and the hand reticles |
| 6 UI in the world | #464 free panels without the old frame; #369 no old screen rectangle; #479 zone-change sign | #435, #543 | after #598 so two tasks do not edit the same windows |
| 7 Body and stereo (Fable) | #548 hands and body; #437 (#22, #58) effects in stereo | — | after #486 and the mirror |
| 8 Two hands (Fable) | #379 two held items; #380 dual wielding; #599 fist damage; closes #375 | — | after magic, reticles and the body |
| 9 Combat feel | #542 damage numbers + crits; #393 torch in the hand; #42 haptics (the owner describes it) | — | the torch needs #380's per-hand slots; haptics on the final events |
| 10 Menus | #335 VR main menu; #336 character creation | — | the 2D menus on the panel work today |
| 11 Playthrough | the owner's play with marks, bug fixes in blocks; #77 video | the level tour and story saves green on main | the MVP's end |

## History

Until 2026-10-22 the MVP was a short level-1 demo, reached through checkpoints A-E (walk, body and interaction,
fight, cast, done) and a task queue; those parts are replaced by the definition and the sprints above. The
research behind the new definition is kept in the coordinator's research notes; the decisions in
[DECISIONS.md](DECISIONS.md).

## After the MVP

`M2 Full Game & Polish`: shield and blocking (F-68, F-69), bow (F-70), diegetic UI, cutscene handling, comfort
options, performance work. The *Later* rows of [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md) are the
candidate list. `M3 Steam Release`: rights, licensing, store page, publishing.
