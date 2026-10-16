# Arx Fatalis VR — Decision Log

Significant technical and project decisions, newest first. Each entry records what was decided, why,
and what it rules out, so the reasoning survives after the conversation that produced it.

**Any decision can be revisited.** A decision is never edited away or deleted: a new entry replaces
it, and the old one gets `Status: Superseded by D-xxx` with a one-line reason. The same holds for the
*Decision* column of research catalogs (e.g. [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md)):
a changed decision keeps its history in that cell.

<!-- d-new-literal -->**Numbering:** while a PR is open a new decision is `## D-NEW: <title>`; the number is assigned at merge by
`scripts/dev/number-decisions.ps1` (see [WORKFLOW.md](WORKFLOW.md#parallel-work)). Do not pick one by hand.

---

## D-052: The desktop window takes no input in VR, and a test run's window none at all

- **Date:** 2026-10-16
- **Status:** Accepted
- **Decided by:** project owner (#487, 2026-10-16: test runs steal his focus, mouse and keyboard; in VR "the desktop
  window is only the mirror of the eye" - this answers #338's open keyboard question)
- **Amends:** #338 (PR #458: the desktop mouse dropped in VR)

### Decision
- **VR:** while a VR session runs, neither the desktop mouse (#338) nor the desktop keyboard drives the game - real or
  injected by the channel (`desktopMouse`, `desktopKeyboard` in `status.json`). Only the headset and the controllers do.
  Flat mode keeps both as in the original.
- **Test runs** (`--test-dir`): the game itself shows its window without activation, never lets it be activated
  (`WS_EX_NOACTIVATE`), puts it in the bottom right corner of the main display, never warps the real cursor, and drops
  every real mouse and keyboard event (`realInput`). The channel's injected events (`desktop-mouse`, `desktop-key`) are
  marked as injected in `SDL2InputBackend` and pass; `real-input` posts window messages - the real devices' path - to
  prove the drop. The owner's own launches (no `--test-dir`) are unchanged.

### Why
- The owner works at the same PC while workers and leads run tests: a test window popping up in the middle of the
  screen took his focus, and his mouse and typing reached the game and could break a running test.
- In VR the window is a mirror: a key or a mouse pressed by someone else at the desk must not act in the player's game.

### Trade-offs accepted
- A test run cannot be poked by hand any more; launch without the channel for that (`run-windows.ps1`).
- The window is placed in a corner, not minimised: a minimised window renders no frames (`ArxGame::run`).
- The proof posts window messages instead of `SendInput`: `SendInput` reaches only the window with the focus, which a
  test run never has, and does not exist in the CI runner's session. A real keyboard or mouse reaches the game the same
  way: Windows turns its input into `WM_KEYDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` / `WM_MOUSEWHEEL` messages for the
  game window, and SDL's window procedure (WndProc) turns those into SDL events. A posted message enters that same
  WndProc path, so it is what the game sees from a real device.

### Consequences
- `status.json`: `realInput`, `desktopKeyboard`, `window.activation` / `placement` / `x` / `y`; the channel's
  `real-input`; `real-input.xrs`, `real-input-flat.xrs`; the smoke test fails a run whose window holds the foreground.
  `@key` is now real input, dropped by a test run. [TESTING.md](TESTING.md), [TRAPS.md](TRAPS.md).

---

## D-051: The classic Arx arm - one elbow, the forearm one rigid piece, re-skinned by ring in VR; the glove optional at its wrist

- **Date:** 2026-10-14
- **Status:** Accepted; the owner's pick (2026-10-16, headset test of #472: "the bend is gone; the arms satisfy me fully")
  - classic is the default and the only model a player chooses (D-047). Its hand defaults to the Arx hand; the glove is
  the other `vr_classic_hand`
- **Decided by:** project owner (#466: the Arx arm one solid piece up to the elbow, a fifth model behind
  `vr_arms_model`; asked whether the classic arm could wear the five-finger glove); #466.1 and #466.2 for the formulation
- **Amends:** D-047 (a fifth model, `classic`, the row it reserved)

### Decision
`vr_arms_model = classic` draws the player model's own arm with **one elbow**: in VR only, after the engine's transform,
the arm's vertices are re-skinned by the ring of the arm they lie in, not by their group (`src/vr/ArxArmFix.*`,
`skinPlayerArms`) - the rings before the elbow ring on the upper arm, the elbow ring up to the wrist ring on the forearm,
the wrist ring and the hand on the hand. The arm folds in one band at the elbow and the forearm is rigid. It is a row of
the model table (`arxArms` true: the logical arm stays on the drawn, pushed shoulder), its first: the default.

Its hand is a second switch, `vr_classic_hand` = `arx` (default) | `glove` (`arms classic-hand <hand>`): with `glove`
the Arx hand faces (every corner on the classic hand: the wrist ring and past it) are left out and the custom arms'
five-finger glove (#385.7: joints, controls, the hilt pose) is drawn with its cuff on the Arx wrist joint, turned by the
same grip frame as the Arx hand (D-032, research F-281); no sleeve - the Arx forearm ends in the glove's cuff.

### Why
- **The measured cause** of the second elbow (TRAPS.md 2026-10-14, `human_base`, both arms): the forearm group holds
  only the forearm's first third (11 vertices, 2.5 .. 11.3 units past the elbow of 30.5); the other two thirds are in
  the wrist group, so every wrist bend and the wrist band's share of the roll fold the forearm in its middle; 3 faces
  join a wrist vertex straight to the upper arm, and the elbow ring mixes both groups. The research's flat-ring (H1) and
  stray-face (H2 alone) hypotheses were not it. Re-skinning by ring removes the cause without new art.
- The owner keeps the Arx look as an option beside the own arms (D-047) and asked for the five fingers on
  it; the glove already exists with its finger sources, so it is an option, not new work.

### Trade-offs accepted
- The re-skin is VR only: flat mode and the inventory book keep the engine's arm (and its second elbow).
- The glove at the Arx wrist follows the Arx hand's frame, not its own curl's turn onto the handle (#385.7): measured in
  the sim's idle pose, the curl's turn tilts the glove some 30 degrees off the Arx hand and leaves the end of the Arx
  forearm open beside it, while the Arx frame lays its palm along the Arx palm (9.6 vs 8.0 units below the wrist, the
  same direction) - at the price of the closed fingers' line lying 31 degrees off the handle (`custom.curlAngle`, 0 on
  the own arms). The owner judges the seam and the grip in the headset.

### Consequences
- status.json `vr.body.<hand>.classic.{skin, drawn, creases, creaseThreshold, creaseBands, maxBend, elbowBend,
  forearmTwistSpread}` (#466.1) and `.hand`, `.arxHandHidden` (#466.2); `arms-classic.xrs`, `arms-classic-glove.xrs`.
  The hand's switch: [TESTING.md](TESTING.md).

---

## D-047: The VR arms are the classic Arx arm (D-051) - picked by the owner from five arm models compared live in the headset

- **Date:** 2026-10-14 (the comparison), 2026-10-16 (the pick)
- **Status:** Accepted: **classic** (D-051) is the default and the only model a player chooses; the other models are
  kept for their tests only until they are deleted (#486)
- **Decided by:** project owner (2026-10-13, via the coordinator: build the variants and switch them live; 2026-10-16,
  headset test of #472: "keep this variant; you can delete the others" - of custom and forearm earlier: "delete, never
  come back to them"; the library hand of #467 was dropped unmerged); #385.8 for the formulation
- **Amends:** D-009 (full body with IK arms: the VR arm is the classic Arx arm, not a full own arm) and D-049 (the own
  arms are no longer drawn for a player)

### Decision
cfg.ini `vr_arms_model` = `classic` (the default; any other value a build saved before reads as classic), status.json
`vr.body.armsModel`. The playground's arms lever that cycled the models is gone. Compared before the pick, each a row of
the model table in `src/vr/VRArms.cpp` and still reachable through the test channel's `arms <model>`:
- **custom** - the glove and the whole sleeve, shoulder to wrist (D-049, D-048);
- **forearm** - the glove and a short forearm: the sleeve from the glove's wrist along the forearm of the custom arm's
  own solve, cut at `ForearmKept` = 0.6 of the forearm's length and closed there by a cuff; no upper arm, no elbow;
- **hands** - the glove alone, its wrist opening closed by a cuff (Half-Life: Alyx);
- **arx** - the player model's own arms, as the engine skins them (with the second elbow D-051 fixes);
- **library** - the WebXR generic hand (#467, PR #470 closed unmerged).
The forearm and hands models pose the same custom arm and draw less of it. Flat mode and the inventory book keep the
engine's Arx arms.

### Why
- No arm solver matches the player's real elbow (15-22 cm off in motion, research F-461); the owner found the full
  custom arm a rubber tube whose elbow never matches.
- Players ignore missing limbs more easily than wrong ones (F-305); Half-Life: Alyx shows gloved hands only (F-303).
  A forearm ending before the elbow keeps some arm and hides the wrong elbow.
- The owner picked by feel in the headset: the classic arm keeps the game's look and has one elbow (D-051), where the
  own arms' elbows never matched his.

### Trade-offs accepted
- Ultraleap's forearm (Apache-2.0, F-467) is FBX and needs Blender to convert; the forearm is the #430 sleeve cut short
  instead. A modelled forearm or full arms v2 come later.
- Until #486 removes them, the unused models and their tests stay in the code (the custom solve shares
  `poseCustomArm` with the glove the classic arm can wear).

### Consequences
- status.json `vr.body.armsModel` (`classic`); the test-only models' `vr.body.<hand>.custom.*`, checked by
  `arms-variants.xrs` and `arms-model.xrs` until #486. [TESTING.md](TESTING.md).

---

## D-048: The custom arm solves its own chain - fitted to the player's reach, a small stretch, the elbow down with a weapon

- **Date:** 2026-10-13
- **Status:** Superseded for the player by D-047 (2026-10-16: the classic Arx arm, D-051): the custom arm's own solve is test-only until #486
- **Decided by:** project owner (2026-10-13: "the arm's own length must fit first", "arm stretch yes", "elbow drop yes"); #385.6
- **Builds on:** D-049 (own arms), the roll-aware IK (D-050 of #313)

### Decision
- The custom arm (D-049) is posed by **its own** roll-aware solve, from the logical Arx shoulder to the glove's
  wrist, with **its own segment lengths**; the logical Arx arm keeps its own solve for the weapon, reticle, wrist bars
  and melee.
- **Length fit:** first from the character height setting (#260; Drillis and Contini's segment ratios, the Arx upper
  arm to forearm ratio), then from the **measured reach** - a running maximum of the shoulder-to-wrist distance at
  full extension, one outlier ignored, used once enough samples are in (`src/vr/ArmFit.h`). The drawn lengths ease to
  a new fit over half a second.
- **Stretch instead of the shoulder slide:** past its fitted length the custom arm stretches by at most
  `vr_arm_max_stretch` (default 0.10, cfg.ini) to keep the hand on the controller; past that the hand stops short.
  The shoulder no longer slides for the custom arm.
- **Elbow drop with a weapon** (VHVR's rule, F-300): a hand holding a weapon, on either hand, turns its elbow toward
  its lowest position about the shoulder-target line by `vr_arm_weapon_elbow_drop` (default 0.75, cfg.ini).
- **Fallback:** a glove that cannot be loaded logs an error (status `vr.body.customArms`) and the arm stays the Arx arm.

### Why
- A fixed-length arm on a real player either falls short or folds; a too-long or rubbery arm was ruled out by the owner.
- Stretch kept the best embodiment in a user study (F-307, CHI'24), where a sliding shoulder moves the body.
- A low elbow reads as holding a weight; VHVR does the same.

### Trade-offs accepted
- The first extension refits the arm once (eased); a long reach held for a while lengthens the fit (for a player taller
  than the height setting).
- Two solves per arm per frame (logical and custom), plus one more while a weapon is held; cheap.

### Consequences
- status.json `vr.body.<hand>.custom.*` shows the fit (`upperLength`, `forearmLength`, `fitSource`), `stretch`,
  `error` and `elbowDrop`; `arm-fit.xrs` checks them.

---

## D-049: Own VR arms behind `vr_arms_model` — the Godot XR Tools glove hand (CC0), cgltf and CPU skinning, the logical Arx arm kept

- **Date:** 2026-10-13
- **Status:** Superseded for the player by D-047 (2026-10-16: the owner kept the classic Arx arm, D-051); the glove stays as
  the classic arm's optional hand (`vr_classic_hand = glove`), the own arms are test-only until #486
- **Decided by:** project owner (variant B of #385); the worker of #385.5 for the formulation; within D-009 / D-032

### Decision
The VR body can wear **its own arms** (#385) instead of the Arx arm models:
- **Variant B:** the hand is the low-poly glove hand of Godot XR Tools (`Hand_Glove_low_L/R.gltf`, CC0, skinned to the 26
  joints of `XR_EXT_hand_tracking`), plus a generated low-poly sleeve along the arm. The files are read with **cgltf**
  and skinned **on the CPU** (`src/vr/ArmMesh.h`); the engine's renderer draws the result, no new rendering path.
- **The logical Arx arm is kept:** the Arx arm skeleton and its IK of D-009 / D-032 still drive the pose; the custom
  meshes follow its joints, and the Arx arm models are hidden while the custom ones are shown. Reach, shoulder and roll
  rules do not change.
- **A switch, not a replacement:** cfg.ini `vr_arms_model` (`custom` | `arx`, default `custom`) and `vr_hand_scale`
  (default 1.15, range 0.8 - 1.5; the owner found the Arx fingers too small and thin). In the test playground: the
  `lever_arms` lever and the channel commands `arms custom|arx` / `arms scale <factor>`.
- **Flat mode and the inventory book always keep the Arx arms.**
- **Visible full only:** no forearm-only or hands-only modes; no hiding near the eye; **no bracer** — the hand/sleeve
  seam stays visible for now.
- The arm's length is fitted to the player's reach with a small stretch (~10%) instead of the shoulder slide, and the
  elbow drops when the hand holds a weapon (both in #385.6).
- The textures are a first proposal from CC0 sources only (`data/core/vr/arms/README.md`): a dark worn leather glove and
  a coarse cloth sleeve.

### Why
- The Arx hands are too small and thin for VR; arms as good as Valheim VR's need a real hand mesh with fingers that
  follow the tracked joints.
- The Godot XR Tools glove is CC0, already rigged to the OpenXR hand joints and light enough to skin on the CPU.
- Keeping the logical Arx arm means the tuned arm rules stay and there is an easy way back.

### Trade-offs accepted
- Two arm models to maintain; the book and flat mode still show the Arx one.
- CPU skinning costs a little per frame; fine for two small meshes.
- A visible seam between glove and sleeve until the owner picks a look.

### Consequences
- The owner compares the two in the playground ([TESTING.md](TESTING.md)); the final look (bare or glove, sleeve
  material, colours) is decided after that. No per-armour sleeve textures for now.

---

## D-050: The VR arms are roll-aware — rotations from the bend plane, the hand's roll shared along the arm (RENIK's rules)

- **Date:** 2026-10-10
- **Status:** Accepted (the owner's pick of option A1 from research section 16e; the shares are the owner's to tune in the headset)
- **Decided by:** project owner (A1), the worker of #304 for the formulation; within D-009 / D-032

### Decision
How the arms of D-032 are rolled (#304, `src/vr/ArmRoll.h`, research row F-212):
- **Every arm rotation is built from the bend plane, not on top of the animation.** The upper arm and the
  forearm each get the world rotation that puts the bone on its joints *and* its rest bend reference
  (the inside of the bend, opposite the elbow) into the plane of the posed arm. Nothing of the engine's
  animation roll is kept for these two bones; the hand bone takes the controller's orientation as before.
  The rest bend reference comes from the model's own rest pose — the rest triangle when the rest arm
  bends, opposite the thumb when it is straight — so there is no calibrated constant.
- **The hand's roll is measured against the neutral forearm** (the swing-twist split of the hand's
  rotation relative to that forearm, about the forearm's axis), and **unwrapped past a half turn with
  RENIK's overflow rule**: near ±180° the roll remembers which side it came from and continues on it
  (190°, 200° …) until it is `vr_arm_twist_overflow` (45°) beyond the half turn, then flips to the
  complement. So a slowly turning wrist never makes the arm snap at 180°.
- **The roll is shared** (RENIK's arm defaults, named constants in `ArmRoll.h`, cfg.ini keys):
  the elbow swings about the shoulder-target line by `vr_arm_elbow_roll` (⅓) of the roll — the bend plane
  follows the hand, so a far-rolled hand moves the elbow instead of the mesh; the forearm takes
  `vr_arm_forearm_twist` (⅔) of what is left against the final bend plane; the upper arm
  `vr_arm_upper_twist` (½) of the forearm's. The wrist band of faces shows the remainder, the elbow band
  the difference between the forearm and the upper arm, the shoulder band the upper arm's roll: a
  controller rolled 90° leaves 15–25° on each band and 180° leaves 20–35° (measured by the `arms-roll`
  sequence on the simulated headset) — against 90° and 180° on the one wrist band before (the candy
  wrapper of research section 16a).
- **The base pole is a relaxed elbow** — down, back and a little outward in the body frame (#36's
  pole pointed a hanging arm's elbow outward, 50° from where the hand's thumb says the bend is) — and
  is only swung by the roll. The shares are reported per hand in `status.json` (`handRoll`, `elbowSwing`,
  `residualRoll`, `forearmRoll`, `upperRoll`, `wristTwist`, `elbowTwist`, `rollOverflow`) and asserted by
  the `arms-roll` sequence; a change of the overflow side is a `vr.body.roll` game event.
- **Licence:** the rules are ported from RENIK (V-Sekai/renik, MIT) — `LICENSE.renik` keeps its notice;
  Final IK (the Unity package VHVR uses) was read to understand VRIK and nothing of it is copied.

### Why
The owner judged #36's arms "not for the MVP": a rolled controller wrung the forearm into a ribbon and the
upper arm into a spiral, because the engine skins one bone per vertex and #36 rolled only the hand. The
research (section 16) found that Valheim VR's arms look right through exactly these rules (VRIK's) plus
skin weights Arx does not have; A1 ports the rules and leaves the weights to A2 (synthetic twist rings,
F-213) only if the wrist band still reads as a ribbon in the headset. A pure core with host tests and a
simulated-headset sequence means the headset answers only "how does it look", not "does it work".

### Rules out
Keeping the animation's roll on the arm bones; a fixed elbow with the whole roll on the wrist band; a
separate VR arm rig or mesh (F-216) and hands-only (F-217) for the MVP; shares hard-coded without cfg keys.

---

## D-046: No screen-frame HUD in VR - every element on a VR element or an open question for the owner

- **Date:** 2026-10-14
- **Status:** Accepted (the owner, #464 / #469); the level-up indication and the homes of the elements without an
  obvious one are proposals behind switches, for the owner to judge in the headset
- **Decided by:** the owner (remove the frame and its edge icons in VR entirely; health and mana stay on the wrist;
  the radial menu reaches the character page, the inventory and the book); the worker for the switches and the proposals

### Decision
- **In VR no element of the original HUD frame is drawn on the 2D interface**, nor takes a click: the orbs, the
  level-up, book, bag and purse icons, the active and precast spells, the stealth gauge, the torch, steal, damaged
  equipment, zone-change and quick-save icons, the memorized runes, the swing charge, the cursor mode's hint and edge
  arrows, the corner minimap, the info messages and the characters' speech text (`vr/HudFrame.h`). In play nothing of it is on the head-locked quad; the panel is shown only for a real panel
  (the bag, the book, a container, a note, a menu) and carries only that panel. Flat mode is unchanged.
- **Each element's VR home** is listed in TESTING.md ([The HUD frame in VR](TESTING.md#the-hud-frame-in-vr-469)):
  health and mana on the left wrist (#116; the orbs come back only without a posed left arm); the book, the bag and the
  torch in the radial menu (#376); the level-up as a gold mark on the radial menu's Character item and a gold notice
  beside the wrist bars for 8 s (`vr_levelup`: `both`, `radial`, `wrist`, `off`). The others have no VR home yet and
  are listed as owner questions.
- **Switches:** `vr_hud_frame` brings the whole old frame back in VR (to compare); `vr_hud_keep` keeps single elements
  on the 2D interface until they have a home.

### Why
The frame was the monitor's edge. In VR it is a rectangle floating in the view (the head-locked quad, the panel's
borders around the book and the bag) that clutters the view, and its icons duplicate what the radial menu and the wrist
already do.

### Rules out
The flat HUD frame in VR by default; new HUD elements on the 2D interface in VR.

---

## D-045: The radial menus — at the hand, drawn by the engine into a quad layer per hand; each hand's quick slots kept in the save as script variables

- **Date:** 2026-10-13
- **Status:** Accepted for the technique; the look and the sizes are proposals the owner judges in the headset (#376)
- **Decided by:** the owner (each hand has its own seven quick slots; the torch lit through the engine's own path, its
  hand recorded - drawing it in that hand is a separate task), relayed by the coordinator; the worker for the internals

### Decision
- **Where it hangs.** Holding B places the hand's ring at the hand's grip, pushed 4 cm further from the eyes, facing
  them, upright, fixed in the OpenXR LOCAL space - the room - until B is let go. Stick walking and turning move the
  world, not LOCAL, so the ring stays with the player and in reach; the hand moving selects. Both rings can be open.
- **Selection** (`vr/RadialMenu.h`, host test `RadialMenuTest`, meters in the ring's plane): the middle (4 cm) selects
  nothing; outside it, the segment in the hand's direction, kept 6° past its edge; the hand above the ring's top
  edge (12.5 cm) opens the half-ring - 9 items left to right over 180°: game menu, character, spells, map, quests,
  quick save, quick load, recenter, cancel spell - which closes when the hand comes back inside the ring (10 cm) or
  drops below its middle. Released over nothing, nothing runs.
- **Drawing.** Each open menu is drawn by the engine's own 2D drawing (filled sectors, line icons, the game's fonts and
  item icons) into a 512 × 512 swapchain of its hand - made with the session: SteamVR refuses one made in a frame
  while another image is acquired (TRAPS.md) - and submitted as a quad layer 0.46 m wide on top of every other
  layer - never hidden by the world or the hand, the same in both eyes. The proposed look (after the owner's
  reference): dark see-through segments, the one under the hand lit amber with an arc outside it and its name in a
  dark disc in the middle, white line icons, a faint arc above the ring saying the half-ring is there, a dot where the
  hand is. Names are the game's own strings where it has them (its key-binding names, item names), so they follow
  its language; "Game menu" and "Recenter" have none and are English. The desktop mirror (#21) does not show the
  menus yet: its overlays are fixed images, a menu's image changes every frame.
- **Running the items.** The engine's own paths: the inventory, quick save / load and cancel spell as their keys pressed
  for one update; the game menu as Escape (an open book or note is closed first, so it always opens the menu); the
  book's pages as their keys do - the book opens on the page or turns to it, and the same item closes the book it
  shows (like the inventory item, what an item opened it closes: the owner's headset run); recenter as a new head reference (`reason=radial`). A quick slot uses an item of
  its kind from the bag as the item's own use does (a potion is drunk, food eaten, a torch lit); a lit torch of the
  kind is put out instead. Time is not paused.
- **Quick slots.** Each hand has 7, holding an item **kind** (`potion_life`), so the next one of the kind refills the
  slot. An item dragged out of the bag and released (B) onto a slot fills it; the item goes back into the bag and the
  held trigger is kept up for the engine until let go, so it picks nothing up again. The slots are the global script
  text variables `$vr_quickslots_left` / `$vr_quickslots_right` (`potion_life|torch|||||`): the save keeps global
  variables already, so no save format changes, and old saves load with empty slots.
- **The torch's hand.** The torch lit from a hand's slot is recorded as that hand's (`vr.radial.torchHand`, a
  `vr.radial.torch` event); the engine's torch is a light at the player and no held model, so drawing it in that hand
  is a separate task.

### Why
A layer per hand keeps the menu readable over any scene and lets the engine draw it with what it already has (text in
the game's language, item icons) instead of a 3D text renderer. Kinds in script variables survive save and load with
no change to the save format the original game shares. Running the items through the engine's own key paths keeps
their rules (no quick save while the controls are blocked, the inventory's own toggle).

### Rules out
A head-locked or panel menu; items running on the press of B; slots that hold one particular item; a save format
change for the slots.

---

## D-044: The player's launch is the original start; logos, the intro and cinematics play on a screen fixed in the room

- **Date:** 2026-10-13
- **Status:** Proposed - the screen's distance and size are the worker's proposal for the owner to judge in the headset (#394)
- **Decided by:** the coordinator (the original flow, player vs. development launch, the skip buttons A / B / triggers);
  the screen's numbers are a proposal (D-017: the owner decides how it looks)

### Decision
- **A plain `arx.exe` is the player's launch, the original game's start**: the FISHTANK and ARKANE logos, then the
  **intro** - level 10, a scripted scene around the `introduction.cin` cinematic, which ends in the main menu by itself -
  then the main menu; New game starts in the prison (area 1). Every `.cin` cinematic of the game plays. The desktop
  shortcut, the Steam shortcut, the SteamVR library entry and the tester zip launch that way. **Development and test
  launches add `--dev-launch`** (the scripts do it themselves; `-Player` / `@player` opt out): no logos, no intro, no
  cinematics, as before (#124); `--intro` / `--cinematics` bring them back.
- **In VR the logos, the intro and the cinematics play on a screen fixed in the room**, never on the head-locked quad and
  never per eye: the intro's scripted 3D scene is drawn flat onto the screen like a film, and its scripted camera does not
  take the head. The screen is placed when one starts where the player looks (yaw only) and stays put while the head
  moves, like the menu panel (#49). **Proposal:** the screen's centre **2.5 m ahead at eye height**, **2.6 m wide**
  (about 55 degrees of the view - a cinema seat in the middle rows; the height follows the picture, about 1.95 m at 4:3),
  not tilted. A logo shows as the logo's own picture on that screen; the rest of the screen stays clear. One place to
  change them: `ScreenDistance` / `ScreenDrop` / `ScreenWidth` in `src/vr/UIPanel.h`.
- **Skip:** A, B or the trigger of either hand skips what is on screen: a logo (the next part follows), **the whole
  intro** (straight to the main menu; a playing `introduction.cin` ends through `cinematicEnd()`), any other cinematic
  (through `cinematicEnd()`, as Escape does: the scripts get `cine_end`). Only while one of them plays; the press is
  kept from the game until it is released, so it never clicks in the menu, opens a radial menu or jumps. In flat mode
  the keys work as in the original (a key during the logos goes to the main menu, Escape ends a cinematic).

### Why
- A picture glued to the face (the head-locked quad) is uncomfortable and reads as broken in VR; a screen at a fixed
  place in the room is how VR players watch flat video. Far enough for the eyes to rest (2.5 m), big enough to fill the
  centre of the view without turning the head.
- Players must see the game's real start; developers and the test runs must not wait through it.

### Rules out
- Logos and cinematics on the head-locked quad in VR; a plain `arx.exe` that skips the logos or the cinematics.

---

## D-042: Control scheme v2 — two equal hands: a casting stance and a radial menu per hand, dual wielding

- **Date:** 2026-10-13
- **Status:** Accepted; replaces D-019's button layout and D-040's stance binding and cast rule. Implemented by #375 ("Control scheme v2",
  #376–#381); each part is judged in the headset in the test playground. The radial menu and the A / B buttons were
  changed by the owner on 2026-10-14 (one ring on the right B, 3 + 3 slots, the map on the left B, the bag on the left A):
  see "The owner's layout" at the end
- **Decided by:** project owner (the scheme, by a design discussion with the coordinator, 2026-10-13); the coordinator
  for the defaults the owner accepted (jump threshold, eight radial segments, the quick-slot rules)

### Decision
**Both hands are equal.** Each hand can hold a weapon, cast and interact; no action belongs to one hand. The same
layout on both controllers (Index; on the Touch, X / Y take A / B's places):

| Input | Action (each hand) |
|---|---|
| **A** (lower face button), held | That hand's **casting stance**. A hand holding a weapon does not enter it: a short haptic pulse |
| **Trigger in the stance** | **The pen**: pressed, the hand draws a rune in the air in front of the player; released, the rune is done. Both hands draw at the same time, each its own rune |
| **A released** | When the **last** A is released, the spell is cast: all runes of both hands, **in the order they were finished**, as one spell, from the hand released last or along the gaze (per spell; every spell is tested) |
| **B** (upper face button), held | That hand's **radial menu**; two can be open at once. Selected by moving the hand; released, the item under the hand runs |
| **Trigger, empty hand, no stance** | **Grab and drag** an item with that hand's reticle. Two hands drag two items at once (an engine change) |
| **Trigger, weapon in the hand** | **Charge the strike**; the strike is that hand's swing |
| **Grip + trigger** (grip first) | **Use / interact**: doors, levers, chests, bodies, items. Empty hand only |
| **Grip alone** | Nothing (the model's fingers only) |
| **Stick click** | **Draw / sheathe** the weapon equipped in that hand |
| **Left stick** | Walk along the look; the deflection sets sneak / walk / run (as now) |
| **Right stick** | Left / right: turn (snap or smooth, as set). **Up: jump** (up beyond 0.7 and more than sideways, so a diagonal turn does not jump). **Down: crouch toggle** |
| **Trackpads** | Unbound by default; the player binds them. Defaults never rely on a trackpad: the game is played on controllers without one |

**The radial menu** (the owner's reference: the radial menu of the VR game Strayed; the screenshot is kept outside this public repository and described in the radial-menu issue):
- a ring of **8 segments** that hangs in the air in front of the hand and moves with the player, within reach; the
  centre names the segment under the hand;
- the **top segment is the inventory**: released there, the inventory opens;
- moving the hand **above** the ring opens a **half-ring** over it: game menu, the book (character, spells, map,
  quests), quick save, quick load, recenter, cancel the running spell;
- the other **7 segments are quick slots** for items (potions, food, torch). An item is put into a slot by dragging it
  from the inventory onto the segment; used from the menu of a hand, it acts for that hand (a torch lights in it, a
  potion is drunk);
- time does not stop while a menu is open;
- the menu is the only way to the game menu, with no hold fallback: it must not break, and CI covers it.

**Dual wielding (MVP, after the arms are fixed):** a weapon is equipped per hand (a sword in the left, a dagger in the
right) and both hands strike; a hand can hold a weapon while the other casts. Two-handed weapons (bows, two-handed
swords) take both hands.

**Dropped:** the grip + trigger chord that sent an item straight to the inventory; precast spells (the flat game's
1 / 2 / 3); the game menu on the left A; recenter on a long left-stick click (now in the radial half-ring); the casting
stance on the left trackpad. No guard against an accidental stick click while running (a VR experience, the owner).

**Later, a separate PR for the owner's test:** the trigger alone uses (a click) and grabs (a hold), replacing
grip + trigger.

### Why
The owner wants the game to feel like VR, not a port of the mouse and keyboard: the hands are the player's own and equal,
spells are drawn with both hands at once, and the many quick keys of 2002 move into one radial menu per hand. Both face
buttons on both controllers stay meaningful, which also fits the Touch (X / Y). The stance plus the trigger as the pen
keeps one stroke per rune, as in the original, so the recognizer knows where a rune ends; releasing the last stance
gives a clear moment to cast without a pause or an extra gesture.

### Consequences
- **Replaces** D-019's layout (its locomotion and turning rules stay) and, in D-040, the stance on the left trackpad and
  the "first trigger wins" rule; the trail's look stays. D-016 (both hands work as the mouse, each with its reticle) and
  D-010 (no rays, runes drawn in the air) stand.
- **Engine changes:** two strokes and recognizers at once and one spell built from both; two held items; a weapon slot
  per hand, damage and the character sheet for two weapons, two-handed weapons on both hands.
- **The first game restriction:** a hand with a weapon cannot cast. It may be revisited after the headset tests.
- PR #310 (runes in the air, #44) and the haptics PR #264 wait until this scheme lands; #310 is reviewed against it.
- Issues #324 (full rebinding) and #325 (radial menu) are folded into the epic; the remapping screen (D-039) keeps
  working on the new actions.
- **The defaults as built (#377, the worker's calls the coordinator delegated; the owner judges them in the headset):**
  grip + trigger on an **item uses it** (the original's right click: eaten, drunk, equipped) - the table's "use", and
  the stash chord is dropped; the stash event stays bindable. Until each hand has its stance (#378) the one stance is
  the **right A** (the owner, the headset test of #456; the left A is free); until a weapon per hand (#380) the
  **right** stick click draws / sheathes the right hand's weapon, the left stick click is free; the game starts with the weapon **drawn** (as the hand held
  it before). The Touch's **left menu button is free** like the rest (the owner, #456: the Touch is the Index, the
  game menu is in the radial menu and on B's game menu key; first built with the game menu on it). "The game
  menu must not break": a B press that **cannot open a radial menu** - in any menu (the game menu, the character
  creation, the credits: the Index has no other Escape), or in the game with the head or the hand not tracked - is
  the **game menu key** (it opens the game menu or goes back); not a hold fallback.
  The trigger's double click no longer acts on doors, levers, chests or NPCs while the hands are the mouse.
- **Amendments by the owner (headset test of #456, 2026-10-14):** B **released over nothing** while the book, the
  bag, a container or a note is open **closes it** (instead of "over nothing nothing runs"); the radial menu opens over
  them, and its half-ring switches the book's pages. The casting stance's press (A) **first closes everything open** -
  the radial menus, the book, the bag, a container, a note; not the game menu - then the stance starts. In VR the book
  no longer switches the game to the engine's cursor mode (it kept the bag open with the book and stayed after it
  closed), and a click on the panel never picks the world behind it. Next, not in #456: the grip alone as use /
  interact (the chord goes); the book as a depth-tested panel the hands cover; the runes recognised from the hands' strokes (#44).
  Casting with a weapon in the hand: while the stance is held the weapon hand's trigger is the pen - no charge, no
  strike. **A variant for the owner to pick** (open): where the ring opens - `vr_radial_place = hand` (the default, at
  the hand) or `gaze_when_off` (before the eyes at the hand's height when the hand is more than `vr_radial_gaze_deg`,
  45, off the gaze: with a panel open his hand was beside his head and the ring opened out of view).
- **The owner's layout before the headset test of #456 (2026-10-14), as built - replaces "a radial menu per hand":**
  the radial menu opens on the **right B only** (`radial_left` is unbound; a player may bind it, it opens the same
  ring). **One ring of 6 segments**: the right half is the right hand's three quick slots - R1 on top, R2 upper right,
  R3 lower right - the left half the left hand's: L1 upper left, L2 lower left, L3 at the bottom. The inventory
  segment is gone. The half-ring over the ring is unchanged ("it's ideal"). A slot acts for **its side's hand**,
  whichever hand holds the menu: a consumable (potion, food, scroll) is used, a torch lights in that hand; equipment
  is equipped by the engine's use - armour and clothes as usual, a shield to the shield slot, and a weapon into the
  slot's hand, which until #380 (a weapon per hand) is always the right one: a weapon from an L slot goes to the right
  hand, noted in the log (`vr.radial.use ... note=weapon_right_hand_until_380`). Slots are filled by dragging an item
  onto a segment, as before. The **left B toggles the map** (the book on its map page, the engine's map key: pressed
  again, closed) - a new `map` event; the **left A toggles the bag** (`inventory`); the right A stays the casting
  stance. 6 slots for now; a setting for more or fewer comes later. A save made with the first layout (7 slots per
  hand in `$vr_quickslots_left` / `_right`) keeps each hand's first three; the rest is dropped.
- **Fixes after the owner's headset run of that layout (2026-10-14), as built:** every slot shows its side's letter,
  **R or L**, on the plus and on a filled slot (at the icon's upper left; the count stays at its lower right). **The
  ring and the open bag coexist:** B released over nothing closes the book, a note and a container but **never the
  bag** - only the left A closes it (or the casting stance's press, which still closes everything). The flow: the
  left A opens the bag, hold the right B, drag an item out of the bag with the left hand (its trigger press takes the
  panel's cursor from the hand holding the ring, the owner rule 13g), let B go over a segment - the slot holds it,
  the ring closes, the bag stays open. A container opens with the engine's bag: B closes the container and leaves
  that bag open. No game menu key on the left B (the owner: it is the map); the ring opens at the hand
  (`vr_radial_place = hand` stays the default).

---

## D-043: Turning settings are sliders the player drags; the smooth speed is named presets

- **Date:** 2026-10-10
- **Status:** Accepted
- **Decided by:** the owner, in the headset run of PR #281 (build ab495ac35), relayed by the coordinator; the
  player-experience call is the owner's (D-017)

### Decision
- **In the owner's words:** the turning settings as a **slider** — "a bar with a dot the player drags between
  notches, not ◀▶ clicks"; the snap angle **30°…90° in 5° steps**; the smooth turn speed as **named presets**
  "very slow / slow / medium / fast / very fast" — "no °/s shown, players don't get it"; the presets map to
  degrees per second inside, **medium ≈ today's 120** (#32).
- **The widget** is reusable (`src/gui/widget/NotchSliderWidget`): a label, a bar with one notch per value, a dot
  that snaps to the nearest notch, the value's text right of the bar. Pressing on the bar (or the dot overhanging
  its ends) puts the dot there and keeps dragging while the button is held — the mouse in flat mode, a hand's
  reticle and trigger in VR (D-016); the arrow keys and the wheel step one notch. The page of #178 may use it later.
- **The values** (`src/vr/VRSettingsRows`): snap angle 30, 35 … 90 (13 notches, default 45, D-028); smooth speed
  very slow 60, slow 90, medium 120, fast 180, very fast 240 °/s; the repeat of a held snap off, 0.25, 0.4, 0.6,
  0.8, 1 s (default 0.4) — a slider too. cfg.ini keeps the numbers (#32's keys); a hand-edited value shows on its
  nearest notch.
- **Not sliders:** snap / smooth (two values, a ◀ ▶ row) and the character height (−20 … +20 cm in 2 cm steps,
  #260: kept as it is, the owner judges it in a later run).

### Why
The owner tried the ◀ ▶ rows in the headset: stepping through values one click at a time with a hand's reticle is
slow, and a number of degrees per second means nothing to a player. A dot on a bar shows the range at a glance and
is one motion to set.

### Rules out
Showing °/s to the player; ◀ ▶ clicking for the ranged turning values.

---

## D-041: The model's fingers follow the real fingers — hand joints first, the controls as the fallback, closed on a held weapon

- **Date:** 2026-10-10
- **Status:** Accepted (the owner's pick from #298's table, row F-218; implemented by #305)
- **Decided by:** the owner (fingers in the MVP, real finger tracking, a held weapon closes the hand), the worker of #305 for the internals

### Decision
- **Real finger tracking in the MVP.** The player model's five finger groups per hand — the thumb (one segment), the
  index finger (two) and the middle, ring and little finger as one block (two) — bend with the player's real fingers:
  the 26 hand joints of `XR_EXT_hand_tracking`, located every frame in the same space and for the same time as the
  grip poses (F-21). The curl of a group is read from the **angles between the joints** (the knuckle's flexion bends
  the root segment, the two outer joints together the tip, the thumb's two joints the thumb), scaled so the model's
  segment reaches its limit when the real finger reaches a fist; the limits per segment are named constants
  (`vr/FingerCurl.h`, `FingerCurlLimits`: thumb 60°, roots 90°, tips 95°) tuned from the headset in one place.
- **Fallback without joints.** A runtime that lists no extension, a headset that reports no support, or a hand whose
  joints are not active this frame falls back to the controller's values: the trigger is the index finger, the grip the
  three-finger block, the thumb resting on a stick, trackpad or face button (a new boolean action `thumb_touch`, bound
  to the touch paths) a closed thumb. The simple controller has no touch: its thumb stays open.
- **A hand holding a weapon is closed on the grip** — not tracked while it holds (F-190's one owner, the coordinator's
  advice the owner accepted): the right hand with an equipped dagger, one- or two-handed weapon (#75's rule).
- **The engine's animation no longer moves the finger groups of a posed arm**; the bend approaches what the tracking
  says at a bounded speed (720°/s, a fist in 0.15 s) so a source change never snaps the fingers.
- **What the simulator gives:** the simulated runtime has the extension; its trackers return a synthetic hand closed as
  the script says (`hand right fingers <thumb> <index> <rest>`, `fingers off` for a runtime without joints), so the
  joint-to-curl path runs end to end without a headset.

### Why
The owner asked for Valheim VR's hands (2026-10-10): the Index's finger sensors are what players use to point, wave and
grab, and SteamVR exposes them through `XR_EXT_hand_tracking` v4 on the owner's PC (`supportsHandTracking = 1`,
measured in #298). Arx's own hand mesh has the finger groups, so no new hand model is needed; three curls are what it can
show. Angles between joints are independent of the runtime's joint orientation conventions and of the space the joints
are located in, so one mapping serves every runtime. The fallback keeps the hands alive on a Quest through SteamVR or
Virtual Desktop, which was not verified to give joints.

### Consequences
- `status.json` → `vr.hands` (extension, support, trackers, active joints per hand, locate counts) and
  `vr.body.<hand>.fingers` (`source` = joints / controls / held / none, the five segment angles and their target);
  `vr.body.<hand>.tipAlong` / `tipOut` show the fingertip vertex coming back along the hand and out towards the palm as
  the fingers close. Game events `vr.hands.joints` (per hand, active / inactive) and `vr.body.fingers` (source change).
- One finger per bone (the ring or little finger alone), thumb opposition tuned per pose, gestures as input,
  `XR_EXT_hand_joints_motion_range` (a fist shaped to the controller) and the left hand holding anything stay post-MVP.

---

## D-040: The rune trail — the engine's flares at the drawing hand, the engine's hold / release model

- **Date:** 2026-10-10
- **Status:** Accepted as the MVP default; the owner judges the look in the headset (#43). The stance on the left trackpad and "the first trigger wins" are superseded by D-042 (control scheme v2, 2026-10-13); the trail's look stays
- **Decided by:** coordinator (default bindings are the coordinator's call, the owner's D-017 exception of
  2026-10-10; proposed by the worker of #43), within D-010 and D-019

### Decision
- **Control model = the engine's** (Ctrl + mouse in flat mode): the **magic button held** is the casting
  stance (D-019: the Index's **left trackpad pressed**, the Touch's **left X**; none on the simple
  controller); while it is held, **the trigger of a hand draws one stroke** — one rune — with that hand;
  **the trigger's release ends the rune**, **the magic button's release casts** what was drawn. A trigger
  already held when the stance starts draws at once (as the engine's held mouse button does). In the
  stance the trigger is the engine's mouse button as before: the engine draws with it and does not
  interact with the world.
- **One hand draws at a time:** the first trigger wins (both in the same frame: the right hand); the other
  trigger is ignored until it is released. A stroke also ends when its hand is lost by the tracking.
- **The trail is the engine's own magic flares** — textures, additive glow, the colour cycling purple →
  yellow → red per rune, the lifetime (faster fade after the stance) — put at a point **8 cm ahead of the
  grip along the controller** (about the index fingertip, a wand's tip), not at the palm. The path between
  two updates is filled with the engine's line flares every 1.2 cm.
- **Size:** half the engine's flare (`vr::RuneFlareScale` 0.5) — the cursor's flares are drawn at 75 units
  in front of the camera, the hand draws nearer.
- **Depth test off** as in flat mode: the trail is visible through the player's own hand and body.
- **No cursor, no ray, no plane** while drawing (D-010): the magic cursor is not drawn when the hands draw,
  and the cursor's 2D trail is not drawn in VR.

### Why
D-010 says the glow is where the hand moves; the engine already has the look, the colours and the
lifetime players know, so only the place changes. The engine's hold / release model keeps multi-rune
spells (Magic Missile = AAM + TAAR: several strokes in one stance) and the original recognition flow.

### Consequences
`vr/RuneTrail.h` (pure, host-tested), `vr/VRRunes.h` (`vr.magic` in `status.json`, `magic.stance` /
`magic.stroke` game events), `AddFlareAt` in `MagicFlare.h`, the `rune-trail.xrs` sequence. What the
engine recognises still comes from the cursor until #44 feeds the stroke into the recogniser. The look
(size, tip distance, depth test) is tuned after the owner's headset test.
## D-039: Remapping in the VR settings — captured on release, conflicts shown, saved as changes per controller type

- **Date:** 2026-10-10
- **Status:** Accepted
- **Decided by:** worker of #178 (technical internals, D-017), within D-020 and D-029; the split of #178 and the
  "VR" entry after "Input" in Options approved by the coordinator; research rows F-171, F-172, F-194

### Decision
- **The VR page** is an entry "VR" after "Input" in the game's Options list (the pause menu is the same menu, so both
  reach it), rows like the game's Customize Controls pages: the event left, its binding right; a click captures.
- **A capture ends when the player lets go** (`src/vr/BindingCapture.h`): released → single; held ≥ 0.5 s (the
  control table's own long-press time) → long press; a second control of the same hand while the first is held →
  chord (held together ≥ 0.5 s → long chord). Ending on release, not at 0.5 s, keeps "close the grip, then pull the
  trigger" a chord however slowly it is done; releases are debounced like the table's (150 ms, the Index grip).
  Controls held when the capture starts (the trigger that clicked) are ignored until released; nothing pressed for
  8 s cancels; while capturing, the controls reach nothing else in the game.
- **Not remappable:** the left stick's directions (walking, D-008) and the right stick's left / right (turning,
  #32) — refused with the reason. The right stick's down / up and every button are.
- **Conflicts are shown, not refused** (D-020: "conflicts are shown"): two events with the same binding both fire;
  both rows turn red and the page names them. Unlike OpenMW VR, which refuses the rebind (F-171): refusing would
  make swapping two buttons impossible without a free button in between. The table's own rules already let a
  control lead a chord, have a long press and a single binding at once, so only identical bindings conflict.
- **Saved per controller type** as `vr-controls-<type>.ini` next to `cfg.ini`, holding **only the events that
  differ from the default** (`formatLayoutChanges`): a later change of #35's defaults reaches every event the
  player did not touch. An unknown controller uses (and saves) the Index layout, as its default already is the
  Index's. A broken file is ignored with a warning (the default keeps the game playable); "reset to default"
  removes it. The test channel's `bind` / `reset` stay unsaved.
- **Way out of a bad layout (F-171):** the page's "reset to default" works with the mouse and keyboard too, and
  remapping never unbinds an event — the game menu and the clicks can be moved, never removed.

### Why
D-020 asks for single / chord / long press, shown conflicts and layouts per controller type; ending the capture on
release is the one rule that makes all three distinguishable without a mode switch on the page.

### Rules out
A mode toggle on the page for "chord" / "long press", refusing conflicting binds, saving the whole layout (it would
freeze today's defaults into every player's file), remapping the walk and turn sticks before #261 (left-handed mode
swaps them as a whole).
## D-038: Physical melee — the swing is the strike, the trigger charges it with the game's own charge

- **Date:** 2026-10-10
- **Status:** Accepted (the owner's design; the numbers are MVP defaults the owner tunes in the headset)
- **Decided by:** the owner (after a headset fight with #75's sword, relayed by the coordinator on #39); the
  mechanism by the worker of #39; builds on D-009, D-016, D-019, D-032 (the body) and D-033 (the weapon in the IK hand, #75)

### Decision
- **A physical swing hits; the trigger does not strike.** While the IK hand holds a melee weapon, a swing of the
  right controller is the strike. During the swing the weapon is placed where the hand holds it this frame and
  the engine's own strike check runs on it every frame (`ARX_EQUIPMENT_Strike_Check`: the weapon's hit
  spheres, the to-hit roll, damage, armour, knockback, blood, sounds, durability — F-58: the engine keeps the
  rules). The engine's own strike (combat mode: press, charge, release — in VR it hit without any animation)
  no longer starts from the attack button while the IK hand holds the weapon.
- **Plain swing = weak; trigger held = the game's charge; a charged hit spends it.** The right trigger held
  charges with the engine's own numbers — the time held against the player's `Full_AimTime`, the ratio 0.1..1 of
  `StrikeAimtime`, shown on the HUD's hit strength gauge. A hit's aim ratio (the engine's damage = attack ×
  ratio) is the charge (never below a plain swing's) or **0.25** without one, times a **speed factor**
  0.5..1 (F-63: 2.5 → 6 m/s). The first contact of a charged swing spends the charge; it stays empty until the
  trigger is released and pressed again. A charged swing that touches nothing keeps the charge (the owner's
  words: "after a charged hit"); letting go of the trigger before a swing drops it.
- **The swing detector** (`vr/MeleeSwing.h`, pure, host-tested): the speed of a point 0.125 m along the blade
  (F-61's lever: wrist flicks do not swing), relative to the head (stepping or leaning with a still arm is no
  swing; the stick's walk and turn never move the controller in the room), from the median of three positions
  (F-59); a swing starts at **2.5 m/s** and ends below 1.5 m/s or after 1 s; a thrust of 0.2 m along the blade
  within 0.25 s at 2 m/s is a stab (F-61); a time step outside 4–100 ms or a speed above 20 m/s starts it over.
  One hit per target per swing and a **0.25 s** per-target cooldown (F-62, PLANCK's per-target 0.25 s of F-74).
- **Gates** (F-66): a weapon in the IK hand, the right controller tracked, no menu / inventory / book, nothing
  dragged, the controls not blocked, alive. A swing start does what the engine's strike start does: the player
  becomes visible, the `strike` script event, the strike speech of a nearly full charge.
- **The numbers are cfg.ini keys:** `vr_melee_swing_speed` (2.5), `vr_melee_full_speed` (6), `vr_melee_weak_aim`
  (0.25), `vr_melee_target_cooldown` (0.25).

### Why
The owner fought an orc with #75's sword: swinging did nothing, and the trigger aimed at the enemy struck with
no animation. The owner asked for swings that hit, spam-able but weak, and the game's charge for the strong ones — so
the charge mechanic and the damage rules stay Arx's and only the trigger's release is replaced by the swing.
2.5 m/s sits between F-178's 1 m/s (the game's roll filters) and PLANCK's 4.5–5 m/s (F-195), measured at a
short lever; 0.25 s instead of "the weapon's attack duration" (F-62) because the owner wants plain swings fast
and spam-able. Contact through the engine's check (not a ray or a cone) keeps hits where the blade really is.

### Rules out
The engine's click attack while a weapon is in the VR hand; damage from swing speed alone without the charge
(VHVR's curve is only the speed factor here); hits from the trigger, from wrist flicks, or from moving the body.

### Consequences
- `vr.melee` in status.json (detector, charge, hits, the last hit's aim and damage, the thresholds); game events
  `melee.swing` (one per swing: kind, result, peak, factor, charge, aim, hits), `melee.charge` (start, full,
  spent, released), `melee.nearMiss` (F-60: motions that peaked within 20 % below the swing speed — the census to
  tune the swing speed on the owner's arm), `melee.state`; the engine's `melee.hit` per hit.
- Haptics come from #42 (PR #264) inside the strike check; bare hands, bows, shields, the left hand, a swish
  sound by strength (F-178) and the per-target outline (F-62) are later tasks.
- `weapon-hold.xrs` no longer sees a strike clip in combat mode; `melee-swing.xrs` proves the design on
  `mvp-goblin-fight`.

---

## D-037: The wrist bars — size, order, fade, and when they replace the orbs

- **Date:** 2026-10-10
- **Status:** Accepted (refines D-029's "two vertical bars, red and blue, on the inner side of the left wrist")
- **Decided by:** worker of #116, proposed to and approved by the coordinator as the MVP default (D-017)

### Decision
- **Geometry:** two bars, each **1 cm wide × 5 cm long**, **0.6 cm apart**, flat on the **palm side** of the
  full body's left wrist (#36): 3 cm out from the wrist joint, 4 cm back from it towards the elbow. Each bar
  runs **across the wrist** and fills towards the thumb, so both stand upright when the wrist is raised to the
  face like a watch. **Health (red) towards the elbow, mana (blue) towards the hand** — red left, blue right
  for a left forearm held across the face, as on the flat HUD. The empty part is dark and a little see-through.
- **Placement source:** the model's posed elbow and wrist (where the player sees the arm) and the controller's
  grip axes as #36 gives them to the model's hand (palm normal = grip +X, thumb side = grip -Z, knuckles = -Y;
  `HandFrame` in `src/vr/ArmIK.h`), not the raw controller.
- **Fade:** fully visible while the inner wrist faces the eye within **50°**, fading linearly to invisible at
  **80°**; drawn in the world, against the scene's depth (walls and the other hand cover them), without fog.
- **Who shows health and mana:** the bars while the **left arm is posed from a controller** (a located one, or
  the test channel's `handpose`); the HUD's orbs then are neither drawn nor clickable. No posed left arm
  (flat mode, the left controller off, a model without arm chains) → the orbs as before. Everything else of
  the HUD stays on the head-locked quad.
- Sizes and angles are named constants (`WristVitalsParams` in `src/vr/WristVitals.h`), not settings.

### Why
D-029 fixed the place and the colours; the details were proposed by the worker and approved by the
coordinator for the MVP. Thin bars across the wrist read like a watch face; keeping the flat HUD's left/right
order means nothing to relearn; the orbs staying whenever the bars cannot be shown means health and mana are
never invisible.

### Consequences
`status.json` → `vr.wrist` (`shown`, `hidden`, `health`, `mana`, `facingDeg`, `alpha`, `drawnViews`,
`center`); game event `vr.wrist state=wrist|orbs reason=…`. Left-handed mode (#178) and a body-zone registry
(F-198) may move or extend this later.

---

## D-036: The hand reticles pick the world in 3D; one mouse arbiter for the panel and the world

- **Date:** 2026-10-10
- **Status:** Accepted
- **Decided by:** worker of #37 (technical, within D-016, D-019, D-029; plan approved by the coordinator)

### Decision
- **The world is picked by each hand's hidden ray in 3D**, not by the engine's 2D pick: the aim pose, rigid
  with the head camera like the body's hands (#36, F-185: the controller where it is, not the posed hand),
  is cast against the level (`raycastScene`, transparent polygons included — plank floors and gratings stop
  it, water and non-colliding ones do not) and the entities' faces; the nearest interactive entity within the
  engine's reach (350 units from the player, 850 with telekinesis) is the hand's hover, with the hysteresis of
  F-57 (2 frames in, 150 ms out). Reason: the engine's pick is a 2D test along rays through the eye, and a
  hand's ray is another ray (before #72 the screen boxes were partly the last eye's too, TRAPS.md).
- **The engine still decides everything else.** The owning hand's hit becomes the engine's mouse position:
  its projection with the head camera moved **16384 px right of the interface** (the "world band"), where no
  2D widget is — so the inventory drawn at the bottom of the screen never answers for an item seen there,
  distances between such positions stay the projection's (the 16 px click dead zone, the engine's 5 px drag
  threshold), and `GetFirstInterAtPos` asks the reticle module what a band position stands for (a short
  history of the positions given to the engine, and each hand's press). While the reticles are on, an
  interface position never picks the world. Highlight, click, use, drag, stash and drop are the engine's own.
- **One arbiter for the panel and the world:** #34's menu pointer takes world targets too (`PointerRay.onWorld`):
  the panel wins where both are hit; the hand that pressed last owns the mouse until its trigger is up; while
  idle, a hand whose reticle moved onto something new, or whose use button fired, takes it (D-019: the name of
  the hand that moved last); a press on no target is muted.
- **A drag puts the item where the ring is, along the line from the head:** the engine steps the item along a
  ray until it collides; from the hand it would collide at once whenever the hand is in or behind a table edge
  or a wall, the head is always in free space. The engine's 300-unit limit for putting down stays.
- **The engine's modes stay:** the exploration mode (mouse look) is kept — leaving it would open the inventory
  — and the hand's position is given to the engine in the relative mouse mode too; the exploration mode's
  crosshair is the owning hand. The cursor-mode edge turning is off while a VR session renders.
- **The rings** (D-029, numbers approved by the coordinator for the owner): a ring with a dot, white, 1.5°
  wide at any distance, alpha 0.45, **1.0 over an item** (`IO_ITEM`) in reach; a compositor quad layer per
  hand on the first hit, facing the eyes, on top of everything. No ring for a ray on the panel (the panel has
  the engine's cursor and #34's beam). The engine's own cursor is not drawn while the mouse is in the world.
- **Off** (the game's own clicks, as before): outside a session, in menus other than the inventory and
  containers, cinematics, blocked controls, a camera that is not first person, the book, a note, magic, and
  **combat mode** — the weapon drawn keeps the engine's attack on the trigger until the follow-up of #37
  (after #75) changes it.

### Why
D-016 asks for both hands as the mouse with the engine's own logic; the engine's pick cannot be fed from a
3D hand in stereo, everything after the pick can. One arbiter avoids two hands and two pointers fighting over
one engine mouse (F-175: one mapping from hit to pixel for highlight and click; F-204: one owner per press).

### Rules out
Grab by reach (F-57 before D-016); a visible ray in the world (D-010); the engine's screen-space pick in VR;
leaving the exploration mode to get an absolute mouse.

### Consequences
`vr.reticle` in `status.json`, `vr-point <hand> entity|at`, `watch` (TESTING.md); `hand-reticle.xrs` and
`HandReticleTest`; `menu-pointer.xrs` points the hands behind the head where it needs "no target". #38 uses
the same reticles for doors, levers, chests and bodies; the follow-up task makes them work with the weapon
drawn; #271 draws the quad layers into the desktop mirror.

---

## D-035: The eyes are multisampled in a buffer of ours, resolved into single-sample swapchains; 4x by default

- **Date:** 2026-10-10
- **Status:** Accepted (refines D-025: "MSAA stays with the window")
- **Decided by:** worker of #226 (technical; the coordinator asked for a cfg key with a measured default)

### Decision
- Each eye swapchain gets one **multisampled framebuffer object** (color in the swapchain's format +
  depth); the game renders the eye into it and `endEyeRender` **resolves** it into the acquired
  single-sample image with `glBlitFramebuffer` before the desktop mirror's copy and the release.
  The swapchains stay `sampleCount = 1`, as the runtime recommends (the Index reports 1).
- The renderer learns the target's multisampling with its size (`setRenderTargetSize(size, samples)`):
  on a multisampled target `SetAntialiasing(true)` works again, so alpha cutouts (fences, foliage,
  hair) use alpha to coverage / sample shading in the eyes like in the window; a single-sample target
  keeps the alpha test. The window's `antialiasing` setting is not the eyes' switch.
- **`vr_msaa`** in `cfg.ini` (samples per pixel, 0 = off, at most 8), `--vr-msaa N` overrides it;
  **default 4**.

### Why
Multisampled swapchains are what OpenXR offers, but SteamVR recommends 1 sample and a runtime may
refuse more; a buffer of our own works with any runtime and leaves the compositor's images untouched.
4x: on the owner's PC (RX 6800 XT) at the Index's recommended 2468×2740 per eye, unpaced on the
simulated headset, 4x costs less than the run-to-run noise (0x 650–765 frames/s, 4x 650–980 frames/s:
under ~0.5 ms against the 11.1 ms budget at 90 Hz) and removes ~70 % of the hard stair steps on edges;
8x dropped to 59–95 frames/s in the same run — below the Index's 90 Hz.

### Consequences
- Every eye frame pays one resolve blit per eye; the shared buffers take
  2 × 4 × 2468 × 2740 × 8 bytes ≈ 430 MB of video memory at 4x.
- The Video settings after the MVP (#181) get an MSAA row backed by `vr_msaa`.

---

## D-034: Physical crouch — hysteresis on the calibrated eye; the character's crouch never lowers the view twice

- **Date:** 2026-10-10
- **Status:** Accepted (implements the crouch part of D-028 / D-018)
- **Decided by:** owner (thresholds, relayed by the coordinator on #100); worker of #100 (the camera rule, technical within D-018)

### Decision
- The head **30 cm** below the calibrated reference (recenter + auto-raise, D-031) crouches the character
  (`PLAYER_CROUCH`, next to the crouch button and toggle); it stands again once the head is back above
  **20 cm** below. A recenter measures from where the head is, so it stands; a headset that is not worn
  (taken off, put on the desk, D-031) never crouches, nor does one while a settled reference is still
  pending (put on again seated: the old reference is the standing head). A seated player leaning forward
  that far crouches too — accepted. `cfg.ini` [video]: `vr_physical_crouch`, `vr_crouch_enter`,
  `vr_crouch_exit` (settings screen #178). Getting up under a low ceiling stays the game's own rule.
- **Camera:** the engine's crouch lowers the character's eye by its animation (the drop from the last
  standing eye, `vr.head.characterDrop`); the head's drop moves the camera only below that:
  `offset.y = headDrop − clamp(characterDrop, 0, headDrop)` — the view is the head, but never above the
  character's eye. Applies to every crouch (the button's too), so getting up has no jump.

### Why
Adding both drops puts the view of a player who crouched 35 cm about a meter down, inside the character's
knees. Following only the head would leave the view above the crouched cylinder, through the ceiling of
a passage the crouch lets the character into. The rule is continuous while the animation runs.

### Consequences
A seated player who crouched with the button and leans down sees no lower until the head is below the
crouched eye. `status.json`: `vr.head.crouch` / `crouchDepth` / `crouches` / `characterDrop`,
`player.crouched`; event `vr.head.crouch` per episode. Sequence `physical-crouch.xrs`.

---

## D-033: The weapon in VR is held by the IK hand — always in the hand, the controller's handle is the hilt, no engine arm clips

- **Date:** 2026-10-10
- **Status:** Accepted (MVP defaults; the owner judges them in the headset)
- **Decided by:** coordinator for the owner (player-facing, D-017), proposed by the worker of #75; builds on D-009, D-019, D-032

### Decision
- **Always in the hand.** While the IK drives the right arm, an equipped dagger, one- or two-handed
  weapon is linked to the model's right hand (`primary_attach`), **outside the engine's combat mode too**:
  D-019 has no draw or sheathe control ("the weapon stays in the hand"), so the trigger stays a click
  and #39's swings do the hits. When the right arm goes back to the engine (no tracked controller, flat
  mode) the weapon goes back to where the engine keeps it — the back, or the hand in combat mode. Bows are
  the engine's (not in the MVP); a two-handed weapon is held by the right hand alone (F-73 is later).
- **The controller's handle is the hilt** (F-48). The engine draws a linked weapon in the hand bone's
  frame, so its angle in the fist is the asset's. The blade's axis is read from the weapon's mesh (its
  principal axis, pointing at the side that reaches further from the attach vertex) and turned by the
  shortest arc onto the hand's thumb axis — which the IK puts on the grip's **-Z**, the OpenXR "little
  finger to thumb" ray — about the attach point, so the hilt stays on the palm. Daggers the same way (no
  reverse grip in the MVP). No per-class trims yet (F-49, from zero). Measured on the short sword
  (`short_sword_0030`): the asset's blade is **9.1°** off the handle axis; drawn, the blade is 0.1° from
  grip -Z.
- **No engine arm clips while the IK drives the arms** (F-67). The player's arm animation layer (layer 1:
  ready, unready, strike, cast) is not applied to the player's own skeleton while the body was posed in the
  last frame; its timing and game logic still run (the engine's combat mode, reachable only by a key,
  still strikes — #39 owns attacks in VR). The combat mode's fight stance is layer 0, the body's, and stays
  (D-009: the torso keeps the engine's animations).

### Why
The owner's scenario (D-019: sword in the right hand, open the inventory, drag with the left) has the sword in the right hand the whole time; a draw button
would be a control nobody asked for, and the engine's combat mode turns the trigger into an attack. The
handle-is-the-hilt rule is what VHVR and the other reference mods converge on (F-48), and it is right by
construction for every weapon mesh instead of a table of angles tuned by eye. Suppressing the arm layer
keeps the engine's clips from pulling the shoulders the IK starts from.

### Rules out
A draw / sheathe gesture or button in the MVP; per-weapon angle constants before the owner felt the default;
the engine's strike clip on the VR hand (F-67's "trigger attack plays the clip" variant).

### Consequences
- `vr.weapon` in status.json (`place`, `assetAngle`, `bladeAngle`, `bladeGrip`, `armAnimSuppressed`) and
  the `weapon-hold` sequence are the acceptance checks; the channel's `combat on|off` drives the combat mode.
- #39 reads the blade from `vr.weapon` (`hilt`, `tip`) for its hit volume; F-49's trims go on top of the
  correction rotation (`vr::heldWeaponRotation`).

---

## D-031: The head reference comes from a worn headset; one world scale of 100 units per meter

- **Date:** 2026-10-09
- **Status:** Accepted (implements the height model of D-018 / D-028 together with #28)
- **Decided by:** worker of #27 (technical, within D-018; agreed with the coordinator)

### Decision
- **Worn = the session is `visible` or `focused`.** An idle headset on the desk keeps the session
  `synchronized` (measured, TESTING.md), so a reference taken then is only **provisional**. When the
  headset is **put on**, the **settled** reference is taken once the head has stayed within 3 cm for 0.5 s
  (at most 3 s after putting it on); taking it off and putting it on again takes a new one — the
  standing or seated head of whoever wears it now. A reset while it is worn (level load, recenter) is
  settled at once (one reference, so a head that turns meanwhile never turns the view twice).
- **Auto-raise** (D-028) is part of the head math: a head above the reference lifts the reference's height.
- **World scale: 1 m = 100 game units**, one constant (`src/vr/WorldScale.h`) for the head offset, the
  eye offsets and the near plane. The engine is in centimeters: its audio listener uses 0.01 m per unit,
  the player's collision cylinder is 170 units, and the character's eye is 170 units above its feet.

### Why
The owner's #24 headset run: "the camera is much higher than the body". The owner's logs show the save
loading with the headset idle (`synchronized`) and the headset put on 50–80 s later; the reference came
from the desk, ~0.8 m below the worn head.

### Consequences
`status.json` → `vr.head`: `referenceKind`, `pending`, `worn`, `refY`, `raised`, `refAboveFloor` /
`aboveFloor` (meters above the STAGE floor, -1 without one), `eyeAboveFeet`; `camera.eyeAboveFeet`. The
simulated headset gets `session idle` (headset off). Real-height calibration and a height offset stay
post-MVP (#101).

---

## D-032: The VR body is the engine's player model on its own skeleton — arms by IK after the animation, the head and neck hidden, the model drawn under the head

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker session (#36), within D-009; the owner judges the result in the headset

### Decision
How D-009's full body is built (#36):
- **One skeleton, the engine's.** The body is `human_base` on the skeleton the engine animates; legs, torso
  and head keep the engine's animations. The two arms are re-posed once per frame *after* the engine's
  animation and concatenation (`Cedric_ConcatenateTM`) and *before* the vertices are transformed, for the
  player's own skeleton only — the copies animated for the camera's stable position and for the book stay
  the engine's (`vr::posePlayerBody`, called from `animateSkeleton`).
- **The arm chains come from the model, not from names.** The group holding the palm attach vertex
  (`primary_attach` / `left_attach`), the wrist joint it sits on (climbing zero-length bones: `human_base`
  keeps the hand as a zero-length child of `right_wrist`), its father (the elbow) and its grandfather
  (the shoulder joint): `right_arm > right_elbow > right_wrist > right_hand`. A model without such a
  chain keeps the engine's arms and says why in the log. The zero-length hand group is rigid on the
  wrist, so the hand mesh takes the controller's orientation and not the animation's bend of the hand.
- **Hands rigid with the head camera** (`handToWorld`, HeadPose.h): a hand is placed from the head the
  way the head is placed from its reference — the same turn, the same units per metre, relative to the head
  (so the body walking after it, #26, does not matter) — so a hand 30 cm right of the head is 30 units right of the camera, and #27's scale and #28's
  recenter apply to hands and head alike. The model's palm attach point lands on the controller's grip.
- **The hand frame is read from the mesh**, no calibration constants: the long axis from the wrist to the
  palm attach vertex (OpenXR grip −Y), the lateral axis towards the model's thumb group (grip −Z: the
  spec's "ray through the tube of the non-thumb fingers from the little finger to the thumb"), the palm
  normal their cross product (+X); the wrist bone's rotation is the grip's rotation through that frame.
  Per-hand trims are F-49, later, and start from zero.
- **Out of reach** (F-52): the shoulder slides towards the target by up to 20 units, then the arm points
  at it stretched; the elbow bends down, a little outward and back.
- **Hidden under a tracked head: the head and the neck only.** The engine's first-person selection
  (`1st`) hides the chest and the belt too; while a head drives the camera the first-person hiding uses
  the head's bone subtree plus the neck, and goes back to `1st` in flat mode.
- **The model is drawn two-sided under a tracked head.** The eyes sit inside the model, above the
  chest, so looking down meets the torso's inner side; culled, the body reads hollow and see-through
  (the owner's #27 run). The player model skips the engine's face cull while a head drives the camera
  (`Cedric_RenderObject`; `render3D()` has no GPU cull), so the chest reads solid from above — and the
  opening the hidden head and neck leave in the torso is **closed by a cap**: the boundary between the
  hidden faces and the visible ones is walked as loops at bind time and each loop is fanned with the
  material and texture coordinates of the visible faces on it (5 faces over one 7-edge loop on
  `human_base`); the cap faces are appended to the model and drawn only in that view.
- **The model is drawn under the tracked head** — the head's horizontal movement since the reference
  shifts every bone, the camera takes the shift off again — until #26 moves the real player there; and it
  faces #25's body yaw (the heading plus the body's offset after the head), turned about the vertical
  through the model's origin.
- **Two poses per hand are kept** (F-185) and shown in `status.json`: the controller (where the player's
  hand is) and the posed wrist (where the model's hand is), with the error between them.

### Why
Arx has a full model with arms, so the body is the model itself rather than a rig of floating hands
(F-174's lesson: hands "quite a lot off" with offset sliders as the repair). Reading the chains and the
hand frame from the model's own landmarks means no name guessing and no constant calibrated by eye; the
owner's first headset run judges the result, and trims (F-49) come on top of a frame that is right by
construction. Posing after the engine's animation keeps every other animation (walk, crouch, jump) and
lets F-53's hand-back later be a blend of the same bones.

### Rules out
Placeholder hands or a wrist-cut model (F-51); an IK on a separate rig; the engine's `1st` selection in
VR; a hand frame tuned by constants before the owner saw the body.

### Consequences
- #75 attaches the weapon through the same wrist bone and hand frame (the frame's +Z is the blade's
  direction, F-49); #116 anchors the wrist bars on the left wrist bone; #43 / #44 take the hand's position
  from `vr.body.<hand>.controller` or `palm`.
- #26 removes the drawn shift once the player follows the head; F-53's hand-back blends `posed` arms
  back to the animation.
- `scripts/dev/xrs/arms-follow.xrs` (on `mvp-new-game`) and `ArmIKTest` are the acceptance checks; the
  channel's `handpose` lets the flat CI run exercise the IK.

---

## D-030: The interface has its own resolution in VR — the headset's pixel density, not the window's

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker of #73 (technical internals, D-017); refines D-025's "the 2D interface at the window's size"

### Decision
- The interface's virtual screen (`g_size`, `g_sizeRatio`, fonts, HUD scale) is the window in flat
  mode and, while an OpenXR session runs (from its first located eyes), **the quad's own
  resolution**: the eye image's pixels per tangent unit times the quad's tangent width, with the
  window's aspect ratio, 640…4096 px wide (`src/vr/InterfaceSize.h`). On the Index: 1528 px wide,
  about 19 px per degree, like the eyes.
- **Everything 2D in a session is drawn straight into the quad's image** at that size: stereo frames
  (blended, D-025), and also frames without stereo (idle headset, no located eyes, the loading
  screen via `vr::beginInterfaceFrame()`), which are opaque. The window only shows a scaled copy;
  the mouse is mapped from the window to the interface.
- The quad's size, distance and shape stay as they were — only its sharpness changes. Not a look
  decision; the panel's look is D-029 (#49), which will reuse the same density rule for its own
  angular size.

### Why
The owner saw the HUD quad as a little blurry: it had the window's pixels (a 640×480 test window
spread over 69°, about 9 px per degree against the displays' 19). The scene was already decoupled
from the window (#19); the interface was the remaining coupling. Tying the interface to the headset
instead of the window also makes the VR picture independent of how the desktop window is sized.

### Rules out
Laying out the interface at the eye size (the quad covers only part of the eye's field of view: more
pixels than the displays can show at that angle, for nothing), and keeping the window as the
interface in VR.

---

## D-029: The look of block B — the floating panel, wrist bars, hand reticle, settings screen

- **Date:** 2026-10-10
- **Status:** Accepted
- **Decided by:** project owner (D-017; exact wording in the coordinator's comments on #49, #116, #37, #178)

### Decision
- **Panel (#49):** menus, inventory and book open on a **flat panel ~0.8–1 m in front of the player at
  chest height**, placed where the player looks when it opens, then **fixed in the world** (it does not
  follow the head). Fits D-016 (inventory in the centre of the view).
  The numbers and rules of #49 (confirmed by the coordinator within this envelope, 2026-10-10):
  **0.9 m** ahead, its centre **0.3 m below the eyes**, **1 m wide** (the height follows the interface's
  aspect), **turned to face the eyes** (about 18° back); placed on the head's **heading only** (looking down
  when it opens does not put it on the floor); "fixed in the world" = **fixed in the play space** — it does
  not follow the head, while the player's own stick walking and turning carry it along; placed anew when a
  menu opens after **more than 250 ms** closed (inventory → book keeps it) and on a **recenter**. A menu is
  the engine's state (F-82): main / pause menu, character creation, credits, a note, the book, a container,
  the inventory — not loading screens or cinematics. With no menu open the HUD stays on the head-locked
  quad until #116. Distance, drop and width are cfg.ini settings (`vr_panel_distance`, `vr_panel_drop`,
  `vr_panel_width`; F-180). The panel is a compositor quad layer, always on top: never swallowed by walls
  (F-179), but it covers the hands until #36 / #37 decide.
- **Menu pointer (#34)** *(proposed by the worker, approved by the coordinator for the owner as the MVP
  default, 2026-10-10; the owner judges it in the headset)*: only while the panel is shown (D-010), either
  hand points (D-016, D-019). A **thin white beam, 3 mm, from the pointing hand to the panel, fading out
  towards the panel**, drawn as a compositor quad layer above the panel; the **dot is the game's own
  cursor** on the panel, no second one. **Only the hand that owns the cursor shows a beam:** the hand whose
  trigger was pressed last; the other hand's trigger takes it over and clicks under that hand; an owner
  pointing away hands an idle cursor to the other hand on the panel. Nothing when no menu is open or the
  ray misses the panel. The click holds still for **16 px** after a press, then drags (F-81); a press with
  the ray off the panel clicks nothing.
- **Health and mana (#116):** **two vertical bars, red and blue, on the inner side of the left wrist**,
  read by raising the hand to the face like a watch.
- **Hand reticle (#37):** a **small dot / ring where the hand points**, no ray (D-010); it **brightens over
  an item that can be taken**.
- **VR settings screen (#178):** in **the game's own menu style**, on the #49 panel, with the Controls
  section (D-020).

### Why
The owner's answers to the design questions of block B (D-017).

---

## D-028: Comfort defaults — mirror eye, height calibration, turning, body yaw

- **Date:** 2026-10-10
- **Status:** Accepted (amends the turning default of D-019 / D-020 and the seated mode of D-018 / D-020)
- **Decided by:** project owner (relayed by the coordinator, recorded by #231)

### Decision
- **Desktop mirror** shows the **right eye** by default (#21).
- **Height calibration:** recenter makes the headset height the character's eye height; when the head goes
  above it, the calibration **raises itself**; **no seated / standing setting**. Physical crouch triggers
  ~30 cm below the calibrated height and stands up again at ~20 cm below (#27, #28, #100).
- **Turning:** **snap 45°** by default, smooth ~120°/s as the option; both the angle and the speed are
  adjustable in the settings (#32, #178). **A held stick repeats the snap** — "tick tick tick": the first
  repeat 0.4 s after the snap, then every 0.4 s while held; the pause is adjustable, 0 = one snap per push
  (`cfg.ini` `vr_snap_turn_repeat`) *(owner, 2026-10-10, after the #32 headset run: "поворот работает
  отлично, рывок удобный", but holding should keep turning)*.
- **Body yaw** follows the head with a **25° dead zone** and ~1.5 s relaxation by default (#25; judged
  later in the headset).

### Why
The owner's answers to the design questions of these tasks (D-017); auto-raise and the crouch hysteresis
replace a seated mode the player would have to choose.

---

## D-027: Coordinator practice — autopilot without pacing, selective review, models by difficulty

- **Date:** 2026-10-10
- **Status:** Accepted (supersedes the pacing part of D-022's autopilot; refines D-015 and D-021)
- **Decided by:** project owner (recorded by #231)

### Decision
- **No pacing:** every task that can run, runs, until the weekly 90% or the 5-hour 95% cap stops it;
  work is not stretched until a limit resets. The coordinator is blocked only by a headset test or the
  limits, finds more work itself when few tasks run, and asks UX questions (D-017) ahead of time.
  Owner: "ты не должен растягивать задачи до сброса лимитов… сделать максимальное количество работы,
  потратив минимум токенов, с максимальным качеством".
- **Models:** Sonnet medium for build / docs / tests / tooling / research and reviewers; Opus medium for
  moderate engine work; Opus high for hard engine work; Fable xhigh only for the heaviest (stereo, IK).
- **Review:** none for docs-only, test-only and small tooling PRs with good evidence; a reviewer for engine
  core, render, input, the VR frame path and widely used scripts; no second review after GAPS unless the
  fix is large.
- Worker briefs, the headset test procedure and cleanup as written in [COORDINATOR.md](COORDINATOR.md).

### Why
The owner wants the most work for the fewest tokens; idle budget at a reset is lost work, and reviews
of trivial PRs cost more than they find.

---

## D-026: The VR runtime interface is a table of OpenXR function pointers; the simulated headset is a runtime behind it

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker of #112 (technical internals, D-017), from research rows F-04, F-06, F-09, F-165

### Decision
- **The `vr::` backend interface is the OpenXR API itself.** Every OpenXR function the VR module calls
  is listed once in `src/vr/XRFunctions.h`; `vr/OpenXRUtil.h` turns the list into function pointers in
  namespace `vr` named like the functions (`vr::xrWaitFrame`) and hides the loader's global prototypes
  (`XR_NO_PROTOTYPES`). Code in `src/vr` keeps calling `xrWaitFrame(...)`; a call of an unlisted
  function does not compile. The default table is the OpenXR loader (`src/vr/XRDispatch.cpp`) —
  the real path is unchanged.
- **The simulated headset (`--vr-sim <script>`) is a second implementation of that table**
  (`src/vr/VRSimulator.cpp`), installed before the first OpenXR call: session states, a 90 Hz display
  clock, swapchains of real OpenGL textures, eye and hand poses and action values from a script
  (`src/vr/SimRig.h`), and a compositor that pairs the eye images per frame and, on `shot`, composes
  and judges what each eye sees (`src/vr/EyeImage.h`). It never contacts a runtime, so it wins over
  `--no-vr`; a broken script means flat mode, never the real runtime.
- **The fixed custom stereo view of F-165 is a mode of the simulator**, not a separate render path:
  a script with set-up lines only (eye size, IPD, the four angles per eye) and no motion.

### Why
A higher-level backend class (begin frame / render eye / submit) would put a second implementation
of `VRSystem.cpp`'s frame loop, layers and mirror next to the real one — the simulator would then
test itself, and every VR task (#19, #24, #74 …) would have to write its OpenXR use twice. Behind the
OpenXR calls, the game's real VR code runs in the simulator unchanged, including code written after
it. A full simulated runtime DLL loaded through `XR_RUNTIME_JSON` (F-09) would do the same from the
outside, at the cost of a second binary and an inter-process way to ask for shots; it stays for
runtime-side faults (session loss, swapchain failure, pacing modes) when one needs reproducing.

### Consequences
- A new OpenXR function in `src/vr` is added to `XRFunctions.h` and implemented by the simulator
  (`sim_<name>`, or the build fails at link time).
- Every VR task climbs to tier 2 (WORKFLOW.md, *Verification tiers*) with
  `smoke-test.ps1 -VrSim <script>` and `eye-check.ps1` before the owner's headset; the two-eyes check
  is the acceptance of #19 and #72. The `.xrs` sequence runner with assertions (#113) builds on the
  same scripts and channel command (`vr-sim`).

---

## D-025: Stereo by drawing the scene once per eye; one cull for both eyes; the 2D interface on the alpha-blended quad until the floating panel

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker of #19 (technical internals, D-017), from research rows F-06, F-39, F-115, F-116, F-162, F-163

### Decision
- **Brute-force stereo.** The 3D scene is drawn once per eye into the eye swapchains, each eye with
  its own view matrix and its own **off-axis projection built straight from the four `XrFovf`
  angles** (F-115) — no symmetric cover, canted displays allowed (the eye's orientation relative to
  the head is part of the view). No multiview, no geometry-shader stereo (F-162: the engine is
  fixed-function OpenGL; the one-pass tricks are where OpenMW VR's support cost went).
- **One cull for both eyes** (F-163): the head camera's projection in the update is the union of
  both eyes' frusta (cant included); rooms, portals and entities are culled once with it, each eye
  only draws. The VR near plane is 0.09 m (F-39).
- **The eyes relative to the head camera, whatever the camera is.** `vr/EyeProjection.h` turns the
  located eyes into a rig relative to the head between them (the same head #24's camera takes:
  midpoint, orientation halfway) and applies it to the engine's camera: with #24 the camera is the
  head and the eyes land exactly where they were located; before it, the body camera gets correct
  stereo without head tracking. Pure math with host tests, like the frame pacing (D-024).
- **The 2D interface stays on the head-locked quad, blended by its alpha.** In a stereo frame
  everything 2D is drawn into the flat frame quad's swapchain image (cleared to transparent) and the
  compositor blends it over the eyes; menus, loading screens and cinematics keep working untouched
  at the window's size. The renderer accumulates coverage in the alpha channel
  (`glBlendFuncSeparate`), so the quad is premultiplied as the compositor expects. The floating
  panel (#49) and the UI size split (#73) replace this; it is not the MVP's UI design.
- **Render targets are the VR module's business.** The renderer only learns the size of the bound
  target (`Renderer::setRenderTargetSize`), so viewports, scissors and snapshots are measured
  against it; MSAA stays with the window (alpha cutouts use the alpha test off the window).
- **Both eyes or none** (F-116): a frame whose scene did not reach both eyes clears both to the
  background; a stale eye against a live one is never submitted.

### Why
The queue separates the matrices (#19) from the per-eye re-projection of CPU-projected geometry
(#72) and the UI size (#73), and the headset test of #18 showed the 640×480 quad as blurry. Drawing
the scene at the eye size with the runtime's own angles answers "stereo and sharp" first; the
alpha quad keeps the game playable (menus, saves, the HUD the owner reads for sharpness) without
touching the UI code, which #49 rewrites anyway. OpenMW VR's three generations of stereo (F-162)
and Titanfall 2 VR's symmetric cover with a shear (F-115) are the cautionary tales: per-eye draw
with exact angles first, optimisation later.

### Consequences
- #72 does the rest of the CPU-projected geometry per eye (particles, sparks, flares, the
  update-phase 2D data); animated entities are already re-projected per eye here because one call
  does it. #73 decides the UI size against the eye size; #49 moves the interface off the quad.
- The desktop mirror (#21) copies the mirrored eye and the quad while they are ours (before the
  release) and blends the quad by its alpha; with the mirror off the window shows the flat frame.
- The draw check (#71) is refused in a stereo frame — its two views would be the two eyes; the
  two-eyes check of the simulator (#112) is the acceptance of per-eye rendering without a headset.
  The simulated compositor blends a quad flagged `BLEND_TEXTURE_SOURCE_ALPHA` by its alpha plane
  like the real one, and a blended quad with nothing drawn in its middle is judged `transparent`.
- MSAA on the eye targets is a new task (the runtime recommends 1 sample; the window's MSAA does
  not apply to framebuffer objects).
---

## D-024: One frame clock in VR — `xrWaitFrame` paces the game; pose-matched submission; an orderly exit

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker of #20 (technical internals, D-017), from research rows F-07, F-21, F-26, F-116, F-117

### Decision
- **While an OpenXR session exists, the headset's `xrWaitFrame` is the only thing that paces the
  game.** The frame starts with it (`vr::beginFrame()` at the top of `ArxGame::doFrame`), so the
  simulation runs on the runtime's predicted display time; the window's vsync and the engine's own
  frame limiter (`fps_limit`) are switched off for the session and restored when it ends — `cfg.ini`
  is not modified, the menu's vsync choice is kept for flat mode.
- **Pose-matched submission.** The eyes are located once per frame, right after `xrWaitFrame`, for
  that frame's predicted display time in the world-locked `LOCAL` space; the renderer (#19, #24) and
  the projection layer both use exactly that sample (`vr::frameViews()`), never a fresh locate at
  submission. Both eyes or none in the layer. A frame that must be ended without a new picture (a
  level load, a session stop) re-submits the last shown layers with their render poses — zero layers
  would show nothing, not hold the picture.
- **Even cadence over raw rate.** `vr_frame_divisor` in `cfg.ini` (`--vr-frame-divisor N` overrides
  it; default 1) makes the game take every n-th display slot by sleeping past the slots to skip and
  letting `xrWaitFrame` snap to the next one — never a timer of our own as the clock.
- **The rate / cadence line is the instrument.** One `[vr] rate:` line per 3 s window (frames
  against display slots, `xrWaitFrame` + `xrEndFrame` as one budget, slots per frame with the
  jitter, the worst frame and the worst run, MATCHED / OVER-SUBMITTING / UNDER-SUBMITTING and EVEN /
  UNEVEN), the same numbers in `status.json` → `vr.pacing`, frame gaps as they happen. Its decision
  core is pure logic with host tests (`src/vr/FramePacing.h`, `tests/vr/FramePacingTest.cpp`).
- **An orderly exit.** On quit the session is asked to exit (`xrRequestExitSession`) and given up to
  2 s to reach `STOPPING` (`xrEndSession`) and `EXITING` before it is destroyed.

### Why
Two clocks (the monitor's vsync or a sleep-based limiter next to `xrWaitFrame`) pace the headset
twice: the Index's 80–144 Hz against a 60 Hz monitor, or a cap that slips a frame every 50 against
the display period (Titanfall 2 VR, F-117). Dishonored VR's "exceptional improvement in world
stability" came from submitting with the pose each eye was rendered with (F-21); their ghosting at
120 Hz came from frames landing 1.05–1.11 slots apart, fixed by an even cadence at a lower rate
(F-26). A log that says MATCHED / EVEN or names the phase of a gap settles "does it hold the rate"
without a headset session (F-07).

### Consequences
- The MVP's "holds the headset's refresh rate" is judged from the rate line: MATCHED and EVEN at the
  native rate, or EVEN at a clean divisor of it.
- #21 (desktop mirror) and #19 (stereo) keep this loop: render between `beginFrame` and
  `submitFrame`, take the poses from `frameViews()`. The loader frame pump of F-117 (keep the
  compositor fed through a level load) comes after the MVP.
- `vr_frame_divisor` becomes a row of the VR Video settings after the MVP (#181).
---

## D-023: The test save library is generated on each machine, not committed

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** worker (technical, #70); the owner can revisit

### Decision
- The test saves (`mvp-new-game`, `mvp-items`, `mvp-goblin-fight`, …) are **produced by a script**
  (`scripts/dev/make-test-saves.ps1`, scenarios in `scripts/dev/test-saves/scenarios.txt`), in one flat
  launch, into `%LOCALAPPDATA%\ArxFatalisVR\savelib`; they are **not committed**.
- A worker uses one with `smoke-test.ps1 -Save <name>` / `run-windows.ps1 -Isolated -Save <name>`: it is
  made the **only** save of the isolated user directory, so slot 0 is it.
- `make-test-saves.ps1 -Verify` checks the whole library in a fresh game; regenerate after a change to
  the engine's save format.
- What the script cannot place (a lever, chest or door in reach) is recorded by the owner in flat mode
  and copied into the library by hand.

### Why
- A save is a snapshot of the original game's level data and of the engine's save format: committing
  ~0.3 MB binaries per scenario would put game-derived data into a repository that may become public
  (the project requires the owner's own copy of Arx Fatalis, CLAUDE.md), and every save-format change
  would silently rot them.
- A script is reviewable, regenerates in about 2.5 minutes, and doubles as a test of `newgame`, `save`
  and `loadslot` themselves (the same game loads each save again and checks it).
- Slot numbers are positions in a list sorted by file time (newest first): only a single-save directory
  makes "this scenario" a stable `--loadslot 0`.

### Rules out
- Saves in git or in release assets; scripts that assume `--loadslot 1` is a given scenario.

---

## D-022: Budget raised to 80% weekly / 95% five-hour; checks kept in balance with time and usage

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the budget of D-013; refines points 1–3 of D-021); pacing part superseded by D-027
- **Decided by:** project owner

### Decision
- **Weekly limit: up to 90%** (was 70%, then 80%; raised to 90% by the owner on 2026-10-10: "повышай недельную планку до 90%", #256; 10% stay for the owner's other project). At 90% no new workers; running ones finish.
- **Five-hour limit: up to 95%.** At 95% the coordinator stops the non-Fable workers, waits for the reset
  and resumes them; the last 5% are for the coordinator to keep watching. Fable workers go on.
- **Going over a limit only with the owner's explicit permission**, given for that occasion; without it the coordinator stops at the limits above and reports.
- **The safety hook never stops a task**: it denies only the dangerous command, says what to do instead,
  and logs the denial for the coordinator; work goes on while there is budget.
- **CI is balanced with time**: GitHub-hosted runners are out (their minutes would run out); the
  self-hosted runner on the owner's PC costs task time. Every PR gets the **fast set** (build, host unit
  tests, one flat smoke test — a few minutes); the heavy checks (`drawcheck`, the spell draw check, longer
  runs) only when the PR touches the code they guard (path filters) or on demand.
- **Independent reviews are selective**: they spend usage, so only risky PRs get a reviewer, it checks
  the Definition of done and the decisions only (Sonnet, effort medium), and the coordinator checks small
  PRs itself.
- **The coordinator watches its own context**: short outputs, the status issue as external memory.
- **Worker count from measured usage (#217):** the coordinator plans the number of workers from data, not guesses -
  `scripts\dev\usage-plan.ps1` (samples of both limits and of every session's cost, the calibration "% of a limit per dollar",
  the cost of finished tasks per model / effort / size, a forecast for the READY tasks). Run every cycle and before starting a worker; it
  names the binding limit and the number of workers that keeps the week at <= 90% by its reset and the 5-hour window < 95%.
  The coordinator starts no more than it recommends; one line of its output goes to the status page (#193).
- **Autopilot (owner, 2026-10-10):** the coordinator runs the work on its own while the limits last. It reads the limits and
  `usage-plan.ps1` on a timer (about every 30 minutes) and before starting a worker, starts every unblocked task the budget
  allows with a sensible model / effort, loads a 5-hour window that has headroom before its reset, but paces the week to <= 90%
  at its reset. The owner's part is only VR tests, opinions and decisions on how things look and work (D-017).
  Owner: "сам определял расходы, сколько есть, кого можем запустить… от меня только тестирование и моё мнение… ты идёшь вперёд, пока есть лимиты".
### Why
The owner wants the work to keep moving while there is budget, with quality checks that pay for
themselves in time and usage.

---

## D-021: Quality and automation package — review before merge, CI gates, a safety hook, automatic order, research radar

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner (all ten proposals of the coordinator)

### Decision
1. **Every PR is reviewed before merge** by an independent reviewer session (`/review-pr`, Sonnet): each
   Definition-of-done item and coordinator addition needs evidence; decisions are checked. Workers wait
   for PASS (three "done" claims with a missing item on 2026-10-09: #141, #151, #71).
2. **CI is the automatic gate**: the flat smoke test, host unit tests, `drawcheck` and the spell draw
   check on every PR (#127).
3. **A safety hook** blocks dangerous commands of every session even with prompts off (#185).
4. **Automatic task order**: `plan-next.ps1` computes the tasks ready to start (#187).
5. **The dev scripts survive the GitHub GraphQL limit** (REST first, a board cache) (#186).
6. **Session control and spend**: a per-session ledger from the status line; stuck or overfull workers are
   caught; spend per task calibrates model choice.
7. **A status page**: the pinned issue "ARXVR status", updated by the coordinator every cycle.
8. **Research radar**: one reference project per block, OpenMW VR first (#188).
9. **A VR game checklist** to audit the plan against (#189).
10. **The owner's impressions** after each headset test are collected and triaged.

### Why
The owner wants development to run on its own with the quality checked automatically and unknown
features found through research, not by chance. Details: [COORDINATOR.md](COORDINATOR.md) §4, §6, §8.

---

## D-020: Our own VR settings — the MVP has the Controls section, with remapping

- **Date:** 2026-10-09
- **Status:** Accepted (moves the left-handed mode of D-019 into the MVP)
- **Decided by:** project owner (agreed by questions, D-017)

### Decision
- The game gets **its own VR settings**, section by section. **The MVP has "Controls"**; sound,
  graphics and the rest stay in the game's own settings for now and get VR sections later.
- **Controls** in the MVP:
  - **remapping:** a list of gameplay events (jump, inventory, book, crouch, use, …); for each event the
    player assigns a **single button, a chord of two buttons on one hand, or a long press**; conflicts are
    shown; "reset to the default layout";
  - **turning:** snap or smooth, the snap angle (30 / 45 / 90, default ~~30, D-019~~ → 45, D-028), the smooth speed;
  - **crouch and height:** physical crouch on / off and its threshold, ~~seated mode~~ (none, D-028), "calibrate height";
  - **left-handed** mode (dominant hand).
- **Default layouts per controller type**: the game detects the controllers (OpenXR interaction profile —
  Index, Touch first) and offers the standard layout for them (the D-019 table); a remapped layout is
  **saved per controller type** and picked automatically.
- The VR settings open from the **main menu and the pause menu** (an entry "VR" next to the game's own
  options), on the centred panel with the menu pointer.

### Why
Players should be able to fit the controls to themselves inside the game, on any supported controllers;
the controls are the base of the VR settings that grow later (sound, graphics).

### Consequences
- **#30** builds the remapping into the action set from the start: one OpenXR action per physical
  control, the game's own table maps controls (single / chord / long press) to gameplay events; the
  interaction profile is detected. SteamVR's binding UI stays usable on top (F-139).
- **#35** provides the default layouts per controller type as those tables.
- A new task builds the **VR settings menu with the Controls section** (on #49's panel and #34's pointer):
  #178.
- **Later sections, agreed for after the MVP:** **Video** (#181) — render resolution %, MSAA, anisotropic
  filtering, draw distance, VR brightness and gamma, desktop mirror mode, an FPS counter; **Comfort**
  (#182) — a movement / turn vignette and world scale %. Not settings: FOV, foveated rendering, refresh
  rate and motion smoothing (the headset's / SteamVR's).

---

## D-019: The MVP control scheme

- **Date:** 2026-10-09 (game menu, recenter and the grip rule added 2026-10-10; other controllers, #35)
- **Status:** Superseded by D-042 (control scheme v2, 2026-10-13) for the button layout; its locomotion and turning rules stay
- **Decided by:** project owner (agreed by questions, D-017; builds on D-008, D-010, D-016, D-018)

### Decision
Valve Index first; Quest 3 Touch the same (A/B on the right, X/Y on the left).

| Action | Control |
|---|---|
| Walk | left stick, forward = where you look (D-008); **speed by deflection** — slightly = sneak / walk, fully = run; no run button |
| Turn | right stick left / right; **snap ~~30°~~ → 45° by default (D-028)**, repeated every 0.4 s while held (D-028, owner 2026-10-10), smooth turn selectable in the settings (MVP: `cfg.ini` `vr_smooth_turn`, `vr_snap_turn_angle`, `vr_snap_turn_repeat`, `vr_smooth_turn_speed`, #32) |
| Jump | A (right) |
| Crouch | physically (D-018) or a toggle on the right stick down |
| Reticle per hand | a hidden ray from each hand; the reticle turns yellow with the item's name on an interactive object (D-016) |
| Click / drag | the trigger of either hand: press = click; hold and move = drag; release = drop where the game says (inventory, floor, surface, throw) |
| Use an item (eat, equip) | grip on the item (the original's right click). **The grip is a closed hand:** the Index is strapped on, so a **light squeeze = grab, an open hand = nothing** (no hard squeeze needed); **"use" fires when the hand opens again** if no trigger came while it was closed, however long it was closed *(owner, 2026-10-10, after the #30 headset run)* |
| Straight into the inventory | **grip held + trigger** on a world item (the original's stealth-click), one hand, without opening the inventory; the hand closed + the trigger = **stash only, never a use** *(owner, 2026-10-10)* |
| Doors, levers, chests, bodies, talking | **one trigger press acts at once** (sent to the game as its double click) |
| Inventory | **B on the right**; the panel opens in the centre of the view and stays where it appeared while open; **the weapon stays in the hand** (no sheathing in VR) |
| Character book (stats, map, diary, spells) | **B on the left** (Y on Quest), on the same centred panel |
| Menus | a pointer only while a menu is open (D-010); both hands can use it |
| Game menu (open / close) | **A on the left** on the Index (free in this table; the Index has no menu button), the **left menu button** on the Touch *(owner, 2026-10-10, after the #30 headset run)* |
| Two reticles on different items | the name shown is the one of the **hand that moved last** |
| Recenter | ~~hold a button chord (F-34)~~ → **long press of the left stick click** *(owner, 2026-10-10, after the #30 headset run)* |
| Runes | drawn in the air with the hand (D-010); ~~the button that starts casting is decided with the magic block (#43–#45)~~ → **hold the left trackpad** (Index) / **left X** (Touch) = the casting stance; **the trigger of a hand draws a rune with that hand**, its release ends the rune, releasing the stance casts (D-040, #43; coordinator's default, remappable in #178) |
| Left-handed mode | ~~after the MVP~~ → in the MVP, in the VR settings (D-020) |

**Other controllers (#35, 2026-10-10).** Default layouts for controllers other than the Index are the
coordinator's call, because players remap them in the game (#178) — the owner's exception to D-017
(2026-10-10):
- **Quest 3 Touch Plus** (SteamVR reports `oculus/touch_controller`): the table above with **X / Y in
  the places of left A / B**; the game menu on the **left menu button**, so **X stays free** (→ magic mode, #43); recenter is
  the same long press of the left stick click. The grip is a squeeze (`squeeze/value` 0.70 / 0.55, F-41) —
  the Touch is held, not strapped on.
- **Simple controller** (`khr/simple_controller`, select and menu per hand): select = click / drag of
  that hand; **left menu = game menu, long = recenter; right menu = inventory, long = book**; no jump,
  crouch toggle, use or stash on this controller.
- **Any other controller** gets the Index layout: SteamVR's automatic rebinding presents it as one of the
  three profiles above.

### Why
The original's mouse model stays the engine's (each hand produces the cursor), so the controls add little
new mechanics; the owner's key case — sword in the right hand, open the inventory, drag an item from the
floor into it with the left hand — works with these buttons.

### Consequences
#30 action set and #35 bindings follow this table; #32 snap 45° default + smooth option (was 30°); #33 jump, crouch
toggle, speed by deflection; #34 menu pointer for both hands; #37 / #38 the reticle rules (the proposal in
RESEARCH-reference-mods.md 13g with these answers); #49 the centred panel for the inventory and the book.

---

## D-018: The camera is where the head is — full head tracking in the MVP

- **Date:** 2026-10-09
- **Status:** Accepted (replaces D-006); seated play and height calibration refined by D-028; leaning and real steps: defaults in the amendment below (#26)
- **Decided by:** project owner (design agreed by questions, D-017)

### Decision
- The VR camera follows the **headset's position as well as its orientation** — "the camera flies where
  the head is", as in other VR games. A fixed eye height kills VR.
- **Height:** the camera moves with the head continuously (head 20 cm lower → camera 20 cm lower). When
  the head drops below a threshold relative to the standing height, the character **also crouches in the
  game** (stealth, low passages, hitbox); standing up makes it stand (#100).
- **Leaning and steps:** horizontal head movement moves the camera — peek around a corner, take a few real
  steps; the character's body follows the head with the game's collision. A head pushed into geometry
  **fades the screen** (the usual VR rule) instead of seeing through walls.
- **Player height:** at recenter the player's **standing head is mapped to the character's eye height**,
  so tall and short players see the world from the character's eyes and doors and tables keep their size.
  Seated play: recentering while seated does the same (#28).
- **Calibration, auto-raise and crouch — the height model** *(owner, 2026-10-10, for #28; like other VR
  games)*:
  - **Recenter is the calibration** (a long press of the left stick click, D-019; in menus too): the
    headset's current height becomes the character's eye height and where the player looks becomes the
    character's facing. It works the same seated or standing — **there is no seated / standing setting**.
  - **Auto-raise:** a headset that rises above the calibrated eye height (calibrated seated, then stood
    up) takes the calibration up with it at once — the camera is **never above the character's eyes**
    (#27).
  - **Going down is not followed:** after sitting down again the player recalibrates. Below the eye height
    the physical crouch applies (#100): the character crouches when the head is **~30 cm below** the
    calibrated eye height and stands up again at **~20 cm** (hysteresis); leaning forward far enough while
    seated also crouches (accepted). The crouch toggle on the right stick (D-019) works either way.

### Why
Immersion: real crouching, leaning and stepping are what make VR feel physical; the owner tried the
fixed height in thought and rejected it.

### Consequences
- #24 becomes "HMD pose (orientation and position) → camera"; #27 maps the standing head to the
  character's eye height; #100 crouch threshold on top of the continuous height; #26 (leaning, room-scale,
  wall fade) moves from M2 into the MVP; #25 makes the body follow the head with collision; #28 captures
  the standing height at recenter and covers seated play.

### Amendment (2026-10-10, #26): leaning and real steps — the defaults
Agreed by the coordinator with the owner as defaults to judge in the headset (D-017); each is a `cfg.ini` key
(`[video]`), so the owner's verdict tunes it without a rebuild:
- **Body lag** — `vr_body_follow_radius = 0.1` m: the body (the collision cylinder) stays put while the head
  is within 10 cm of it horizontally; beyond that it walks after the head through the game's collision, so
  the head stays at most 10 cm off. Held by a wall, a table or a corner, the body stops and the head goes on
  (a lean). The interim 0.6 m lean clamp of #24 is gone.
- **Wall fade** — `vr_wall_fade_out = 0.15` s to black, `vr_wall_fade_in = 0.25` s back; black; the world
  only (the interface panel stays readable). When the head's near plane touches geometry the game's own eye
  does not, or a wall is between the character's eye and the head.
- **Head too far** from a body that cannot follow (blocked controls, a conversation camera, a ledge) — fade
  from `vr_head_far_start = 0.7` m to black at `vr_head_far_full = 1.0` m.
- **Real steps make footstep noise** like walking — `vr_real_step_noise = 1` (stealth stays honest). **The owner decided: keep** (2026-10-10, before the headset test).

---

## D-017: Player-experience design is agreed with the owner before any work starts

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- Anything the player experiences — controls and bindings, world interaction, locomotion and comfort,
  where UI sits and how it looks (position, colour, shape, size, behaviour), feedback (haptics, sounds,
  highlights) — is **discussed with the owner before** its issue is written or changed and before a
  worker starts: the coordinator sends a short design summary with concrete questions, plays the design
  back in its own words, and waits for a yes. The agreed design is recorded here as a decision.
- **Technical internals** — where a value comes from, how it is updated and rendered, architecture,
  tooling — the coordinator and the workers decide on their own.
- Example: the HP bar — its place, colour and shape are the owner's; reading HP from the engine and
  redrawing the bar are technical.

### Why
D-016 was first recorded as "left hand drags, right hand takes to the inventory" when the owner meant
"both hands work as the mouse". Agreeing the design first avoids building the wrong thing.

---

## D-016: World interaction — both hands work as the mouse, each with its own reticle; the inventory opens in the centre of the view

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the "world interaction by reach" point of D-010). Corrected the same day:
  the first wording split the hands ("left drags, right takes to the inventory") — a misreading of the
  owner; the decision below is the owner's intent.
- **Decided by:** project owner

### Decision
- **Both hands emulate the mouse fully.** Each hand has its own **reticle** — a small crosshair where the
  hand points, with **no visible beam** — within the engine's own interaction distance; the engine's own
  "what is under the cursor" test runs at the reticle (the hidden hand ray of F-57 replaces the mouse).
- With either hand the player does what the mouse does in the original: pick an item up and **drag** it,
  move it over surfaces and set it down on any plane, drag it **into the open inventory**, use doors,
  levers, chests, bodies and characters (the trigger is the game's click).
- **The inventory opens as a panel in the centre of the player's view** (for now; the panel itself is #49).
- **Design case:** the right hand holds a sword in a fight → the player opens the inventory, drags an
  item from the floor into it with the left hand, closes it and fights on.
- Unchanged from D-010: no rays or lasers from the hands, runes drawn in the air with the hand, spell
  direction from the hand, a pointer only while a menu or the inventory is open (#34).

### Why
In the original the player walks up to an item, aims the mouse at it and drags it over surfaces or into
the inventory. Grabbing with the hand and placing by hand would need new carry-and-place mechanics and
much more work; a reticle keeps the game's own logic and is what VR ports commonly do. Two hands keep it
natural. The reference to learn from is **Valheim VR (VHVR)** — its world interaction is studied in detail
before #37 / #38.

### Consequences
- #37 and #38 become "a reticle per hand + the engine's drag / use" instead of reach-and-grab; #49 opens
  the inventory in the centre of the view; #36 (full body with IK arms) no longer has to hold items in
  the palm.
- Catalog: F-57 changed (reticle, no grab by reach); F-138 (distance-grab gesture) not taken.

---

## D-015: Model and effort by task size — Opus by default, Fable only for really hard tasks

- **Date:** 2026-10-09
- **Status:** Accepted (replaces the model part of D-013)
- **Decided by:** project owner

### Decision
- **Opus** is the default model for engine work, **Sonnet** for scripts, CI, docs and research summaries.
- **Fable** only for really large and hard tasks (the OpenXR frame loop and stereo rendering, the IK body,
  rune recognition, crashes nobody could explain) — chosen by the coordinator per task, never by default.
- **Effort** is set per task by the coordinator (`start-worker.ps1 -Effort`): lower for routine work,
  higher for hard design or debugging; defaults Sonnet medium, Opus high, Fable xhigh.
- Context is spent carefully but fully: one task per worker with a precise Definition of done, short
  coordinator messages, big tasks split.

### Why
Half of Fable's weekly limit went in the first hours with Fable on every engine task (D-013); most of
those tasks did not need it. The playbook table: [COORDINATOR.md §B5](COORDINATOR.md#b5-models-d-015-d-027).

---

## D-014: Launch control for parallel workers — no SteamVR start, one game at a time, own fresh build, ledger, budget

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner (relayed during #93, enforced by #147)

### Decision
Workers launch the game often and in parallel, on the owner's desktop. The rules, enforced by the
launch scripts (`scripts/launch-lib.ps1`, used by `run-windows.ps1`, `smoke-test.ps1`, `test-pr.ps1`
and the desktop shortcut), not by the workers' discipline:

1. **No SteamVR start without the owner.** A launch without `--no-vr` would let the OpenXR loader
   start SteamVR, which turns on the owner's headset and base stations. The scripts make such a
   launch flat (`--no-vr` added, said in the output) when `vrserver.exe` is not already running;
   `smoke-test.ps1 -Vr` is then *skipped* (exit 2). Only the owner's switch `-StartSteamVR`
   (`run-windows.ps1`, `test-pr.ps1`) starts SteamVR. Workers never start or close SteamVR by hand
   either (no `vrstartup.exe`, no `Stop-Process vrmonitor`).
2. **One game at a time on this PC.** Every script launch, flat or VR, holds a machine-wide game lock
   slot (`%LOCALAPPDATA%\ArxFatalisVR\game-<n>.lock`; since #205 `%ProgramData%\ArxFatalisVR\locks`, shared by every Windows account including the CI runner service; a VR launch also `vr.lock`) while the game runs,
   waits in line with a timeout and names the holder (working copy, branch, pid). A raw `arx.exe`
   outside the lock counts as a holder. `settings.json` (`maxGames`) may allow 2 flat games later.
3. **Only the working copy's own fresh build.** A launch refuses an `arx.exe` from another copy's
   build directory, or whose `built-from.txt` does not name the copy's `HEAD` (`dirty` is allowed),
   and says to build first (`-Rebuild` does it).
4. **Launch ledger.** Every launch is one line in `%LOCALAPPDATA%\ArxFatalisVR\launches.log` (time,
   copy, branch, build, flat or VR, pid, exit code, duration); `cycle-report.ps1` shows launches per
   working copy, so the coordinator sees a worker that launches 20 times.
5. **Launch budget:** at most **10 game launches per task**. Repeated-run evidence loops (`-Repeat`)
   only after asking the coordinator, sequential and flat. Launches are announced in the narration.
6. **Self-test windows do not take the focus** from the owner's work (SDL shows them without
   activation).

Replaces the #123 rule that isolated flat runs may run side by side: isolation of *files* stays
(own user, config and channel directories per copy), but game *runs* are one at a time.

### Why
On 2026-10-08/09 unannounced loops of launches confused the owner, and a worker of #93 started
SteamVR for 15 VR runs, waking the headset; two games at once also fight for the GPU and the desktop.
The owner wants the desktop quiet while workers run, and the headset untouched unless they put it on.

### Consequences
- `scripts/vr-lock.ps1` became `scripts/launch-lib.ps1` (the VR lock is part of the game lock).
- The VR self-test exists only while the owner has SteamVR running; otherwise the PR says so and
  the simulated VR backend (#112) is the long-term answer for VR evidence without the headset.
- TESTING.md, WORKFLOW.md "Parallel work" and `.claude/commands/next-task.md` carry the rules.

---

## D-013: The coordinator runs development on its own, in blocks, within a usage budget

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- **The owner talks only to the coordinator.** The coordinator picks the tasks of the current block
  (a wave of [MVP.md](MVP.md#waves)), writes or sharpens their issues, starts the workers
  (`start-worker.ps1`), follows them (idle notices, `claude logs`, cross-session messages), triages
  what they find (new bugs and follow-ups → issues, into the queue if they block the MVP), merges and
  cleans up, and keeps the slots full — without the owner opening worker sessions.
- **The coordinator does not investigate or fix things itself**: a problem becomes a task for a worker.
- **It stops and reports** only when (1) the block is done, (2) a PR needs the owner's headset test —
  work that needs no headset goes on meanwhile — or (3) the budget is reached. The owner gives VR
  verdicts to the coordinator ("#18 ok" / what is wrong); the coordinator merges or passes the feedback
  to the worker.
- **Budget:** the common weekly limit up to **70%** — from there no new workers start, running ones finish
  their PRs; the 5-hour limit may be used up (sessions wait for its reset and continue,
  `autoContinueAtUsageLimit`); Fable's separate weekly limit may be used up.
- *(Superseded by D-015: Opus by default, Fable only for really hard tasks.)* **Models** (replaced D-012's table): **Fable** for every engine task (any area except build and docs)
  while its limit lasts, **Sonnet** for build, CI, scripts, docs and research, **Opus** when Fable's limit
  runs out (`start-worker.ps1 -Model opus`).

### Why
The owner wants continuous progress without babysitting sessions or prompts, and a hard ceiling on the
week's usage; Fable's separate limit is spent first to spare the common one.

---

## D-012: Worker model by task difficulty — Fable, Opus, Sonnet

- **Date:** 2026-10-09
- **Status:** Superseded by D-013 — the owner gave Fable every engine task, not only VR runtime and rendering
- **Decided by:** project owner

### Decision
`scripts/dev/start-worker.ps1` picks the worker's model from the issue's area label; the coordinator
overrides it with `-Model` when a task is harder or easier than its area suggests.

| Model | Areas | Typical tasks |
|---|---|---|
| **Fable** | `area:vr-runtime`, `area:render` | swapchains, frame loop, stereo rendering, IK body; hard crashes (override) |
| **Opus** | everything else (`core`, `camera`, `input`, `ui`, `combat`, `magic`) | engine work: camera, input, UI, combat |
| **Sonnet** | only `area:build` / `area:docs` | scripts, CI, docs, research |

Usage is watched in `/new-cycle`: the plan's limits (the session, the week, and Fable's separate weekly
limit) are read from the status line's last input (`~/.claude/statusline/last.json`, if Claude Code
passes them) or from the owner's screenshot of claude.ai → Settings → Usage. When the weekly use runs
ahead of the week, the coordinator runs fewer workers at once (2 instead of 3) or moves tasks to the
model with room left. The one-time free full reset is kept for a week that runs out mid-wave.

### Why
Fable has its own weekly limit, so giving it the hardest tasks spends a separate budget and keeps the
common one for the rest; three workers in parallel spend about three times one.

---

## D-011: The coordinator starts worker sessions itself; workers run without permission prompts

- **Date:** 2026-10-09
- **Status:** Accepted
- **Decided by:** project owner

### Decision
- In parallel mode the **coordinator starts the worker sessions itself** instead of handing the owner
  prompts to paste: `scripts/dev/start-worker.ps1 <n>` creates the task's worktree and runs
  `claude --bg` there with `/next-task <n>`, named "ARXVR-<n> <issue title>".
- Worker sessions run with **`--permission-mode bypassPermissions`** (the owner's choice over "auto mode
  plus an allow-list" and over normal prompts), so they never stop to wait for a permission click.
- The owner follows them with `claude agents`, opens one with `claude attach <id>` (questions,
  `/accept-task`, `/reject-task`); the coordinator can message them and stop them.
- Prerequisite: the owner accepts the bypass-mode disclaimer once (`claude --dangerously-skip-permissions`,
  interactively, choosing "Yes, I accept" — the default is "No, exit"); nobody can do that for them.
- Workers are launched in the owner's copy (a trusted folder) and enter their worktree as their first
  step (`/next-task <n> worktree=<path>`): Claude Code's workspace trust is per folder and does not
  extend to new worktrees, and asking the owner to trust every worktree would defeat the point.

### Why
The owner wants the loop to run without copying prompts and without babysitting permission dialogs;
the coordinator already knows which tasks may run together.

### Trade-offs accepted
- Nothing stops a worker's mistaken destructive command on the owner's PC. Mitigations that stay:
  each worker has its own worktree outside OneDrive and its own game folders (#123), the project rules
  (never push to `main`/`master`, no force-push, never discard uncommitted work) and GitHub's history.
- Each worker spends tokens on its own; three in parallel cost about three times one.

---

## D-010: No rays from the hands; runes drawn in the air with the hand; grab by reach

- **Date:** 2026-10-08
- **Status:** Accepted, except "world interaction by reach" — superseded by D-016 (a hand-aimed reticle from either hand)
- **Decided by:** project owner (while deciding on [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md))

### Decision
- **No rays or lasers from the hands in the game world.**
- **Runes are drawn in the air with the hand:** the glowing trail is where the hand moves. No laser
  cursor; the projection onto a plane that the 2D recognizer needs is internal and invisible (#43–#45).
- **Spell and projectile direction comes from the hand**, without a visible ray (F-75, F-76, #44).
- **World interaction by reach:** items, doors, levers, chests and bodies are used by reaching and
  grabbing with the hand (F-57, #37, #38); the engine still decides what is under the hand.
- **Menus and the inventory:** a short pointer appears **only while a menu or the inventory is open**
  (VHVR-like, F-81, #34) — chosen over touch UI for precision on the original 2D interface.

### Why
Immersion: in a hand-driven VR game a beam from the hand reads as a tool, not as the character's hand.
The old 2D menus are small, so a pointer stays — but only where there is a menu.

---

## D-009: Full body in VR — the player model with arms driven by IK, in the MVP

- **Date:** 2026-10-08
- **Status:** Accepted; amended by D-047 (the arms drawn are a choice of four, the full arm one of them)
- **Decided by:** project owner

### Decision
Arx Fatalis already has a full player model with legs, torso, arms and animations. In VR the player
sees **their whole body**, not floating hands:
- legs and torso keep the engine's animations (walk, crouch, jump); the head is hidden; the camera sits
  at the eyes (D-006);
- the **arms are driven by the controllers via IK** (F-52, Dishonored VR `docs/dishonored/ARM_IK.md`,
  MIT): shoulders relative to the head frame, body yaw following the head with a dead zone, the
  shoulder sliding when a hand is out of reach;
- the weapon is held by the IK hand (#75); while IK drives the arms the engine's arm attack animation
  is suppressed (F-67);
- health and mana are **two bars on the left wrist** (F-83, #116).

**In the MVP, before combat** (checkpoint B). Replaces the earlier plan of placeholder hands and
hidden first-person arms (#36, #75). Hand-back to engine animations (death, ladders — F-53) is after
the MVP.

**How the body follows the head (#25, 2026-10-09, the coordinator for the owner — the F-52 defaults;
the owner judges them in the headset once #36 shows the body).** Yaw only; the body following the head's
*position* with collision is #26 (D-018).
- The body's yaw is kept relative to the **heading** — the yaw the stick and the mouse turn
  (`player.angle`) — so a snap or smooth turn turns head and body together.
- A head more than **25°** from the body drags the body along at once, so the gap never exceeds 25°.
- Inside 25° the body relaxes toward the head and **settles in ~1.5 s** (exponential, 5% of the gap left):
  a glance barely moves it, looking somewhere for a while turns it there.
- A new head reference (recenter, level load, a new session) faces the body forward at once.
- The player model faces the body; the camera stays on the head — the body turning never moves it.
- The values are named constants (`BodyFollowParams`, `src/vr/BodyYaw.h`), so the VR settings (#178)
  can expose them. F-168 (OpenMW VR locks the body to the head) is the warning against the simpler way.

### Why
The owner wants the game to feel like VHVR, which shows the full body; Arx has the model and
animations already. Doing it before combat means the weapon is held by the model's hand from the
start instead of being redone later. Cost: roughly 1–2 weeks more for the MVP.

---

## D-008: Stick forward = where you look

- **Date:** 2026-10-08
- **Status:** Accepted
- **Decided by:** project owner (catalog row F-33)

### Decision
Thumbstick locomotion moves relative to the **head (look) direction**. Controller-relative movement is
an option after the MVP. The body yaw is still decoupled from the head (#25) — for the full-body model
and for where the hands and the UI anchor live — but it does not steer movement.

### Why
Both reference mods default to it (Dishonored VR `HeadBasedMovement`, VHVR
`JoyStickForwardDirection=LookDirection`, which the owner plays); Dishonored VR's body-relative mode
kept drifting.

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
- The **Windows icon** is the owner's prebuilt `data/vr/ARXVR.ico`, compiled into `arx.exe` (and the
  crash reporter / profiler) as a resource; the window takes it from the exe (#103). The upstream
  icon generated from SVG stays off (`-DICON_TYPE=none`), so the build needs no ImageMagick or Inkscape.
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
- **Status:** Superseded by D-018 — the owner wants full head tracking: a fixed eye height kills VR
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
