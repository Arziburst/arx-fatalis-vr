# VR game checklist — what players expect, audited against our plan

- **Date:** 2026-10-09
- **Task:** #189 (epic #8, the owner's quality package, D-021 point 9)
- **Question:** what does a PC VR game have to offer so that players do not bounce off it, and which of
  those things does our plan (MVP, M2, M3, [DECISIONS.md](DECISIONS.md), the catalog in
  [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md)) not cover yet?
- **Result:** 94 items in 11 areas below. 18 rows are marked **MISSING** (no task, decision or catalog
  row); with three more existing rows that lack a part (O-01, O-03, S-04) they make **14 proposed
  actions**, plus four rows whose proposal is "skip" or "a line on the store page" (C-06, I-03, S-05,
  R-07). All are listed for the owner's decision in
  [Missing items for the owner's decision](#missing-items-for-the-owners-decision).
  **No tasks have been created** (the coordinator creates them after the owner decides).

## How the list was built and how far to trust it

Sources, in the order of how much weight they carry:

1. **Platform guidance.** Meta's Virtual Reality Checks (VRC) and accessibility checklist — written for
   the Horizon Store, not for Steam, but the only published, itemised checklist, and Steam players
   come from the same headsets and habits. Valve's own Steamworks pages are an index of sub-pages
   and say little about requirements in the parts that were readable; what they do say (store page
   declarations) is in [Store page and requirements](#10-store-page-and-requirements).
2. **Comfort vocabulary** — Road to VR's comfort-settings checklist (turning, movement, posture,
   accessibility terms; the list Steam VR players know).
3. **Player reactions to ports** — thin. The searches found no aggregated Steam-review analysis; the
   evidence is individual cases: Little Nightmares VR (reviews: no smooth turn, a vignette that cannot
   be switched off — "a dealbreaker" for some; fixed in a patch), Roboquest VR (praised for natural
   controls and responsiveness to bugs), Titanfall 2 VR (praised even as a rough mod; its cutscene
   cameras are "the one thing that can make you feel ill"), and the VHVR / Dishonored VR / Titanfall 2 VR
   mods already in our catalog. Treat the "what players expect" column as **well-supported for comfort and
   input, anecdotal for everything else**.
4. **Our own catalog** (F-xx rows) and the engine facts in [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md).

**Not verified:** Steamworks' VR page content beyond the three store-page lines quoted in section 10; any
numeric performance requirement from Valve (none found in primary sources); one secondary article
(vr.org, August 2026) claims Valve now strongly recommends submitting depth / motion vectors for the
compositor — see P-09, marked as unconfirmed.

## Status legend

| Status | Meaning |
|---|---|
| **MVP** | In the MVP queue — task or decision named |
| **M2** | Planned after the MVP — task or catalog row named |
| **Accepted** | The MVP lists it as a known problem we live with ([MVP.md](MVP.md#accepted-for-the-mvp-known-problems-we-live-with)) |
| **MISSING** | No task, no decision, no catalog row — proposal given, owner decides |

A **MISSING** item has a **proposed slot**: *MVP* (needed to ship the demo to others), *tester build*
(before #88 goes to a friend), *M2*, or *skip* (with why). Anything that is player experience is
decided by the owner first (D-017); the proposals are a starting point, not a design.

---

## 1. Comfort: turning and moving

What players expect: a choice of turn style, a choice of movement style, a way to soften motion for
sensitive players, and nothing the game does to the camera that the player did not do.

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| C-01 | **Snap turn** with adjustable angle | Road to VR: comfortable for most; Meta VRC.7 (rotate without turning the head) | **MVP** | #32, 30° default (D-019); angle in the settings (D-020, #178) |
| C-02 | **Smooth turn** with adjustable speed | Little Nightmares VR reviews: missing smooth turn is a reason not to buy | **MVP** | #32 option; speed in #178 |
| C-03 | **Smooth stick movement**, head-relative | The default of every reference mod | **MVP** | #31, D-008 |
| C-04 | **Controller-relative movement** as an option | Road to VR lists head-based / controller-based as the standard pair | **M2** | D-008 ("option after the MVP"); **no task** — add to the Comfort section (#182) |
| C-05 | **Movement vignette / blinders**, strength adjustable, switchable off | Meta locomotion guidance; Little Nightmares VR: a vignette that cannot be disabled is a complaint in itself | **M2** | #56, #182. The MVP has none (F-40, confirmed). Rule when built: **default off or mild, always switchable** |
| C-06 | **Teleport / dash locomotion** | Road to VR: "teleport is the gentlest"; Meta: "support multiple locomotion styles when possible" | **MISSING** | Proposal: **skip for v1**. The game has stairs, ladders and narrow levels built for walking; snap turn + vignette cover most sensitive players. Revisit if testers report sickness. (RESEARCH-playable-v1 already put teleport out of v1.) |
| C-07 | **Speed by stick deflection**, no run button | VHVR / common practice | **MVP** | D-019, #33 |
| C-08 | **No head bob, screen shake, FOV zoom** | Universal; Titanfall 2 VR README | **MVP** | #76, F-31 |
| C-09 | **No camera smoothing on stairs / step-ups** that moves the view independently of the head | Classic sickness trigger in ports of flat games with stair smoothing | **MVP** | D-018: the HMD pose is applied after every engine camera smoothing (F-31, F-37); #24 must verify on the first stairs of level 10 — add as an acceptance line when #24 is reviewed |
| C-10 | **Swappable movement hand** | Road to VR vocabulary | **MVP** | Left-handed mode, D-020 / #178 |
| C-11 | **Fade to black on loads, death, teleports** | Comfort; VHVR F-35 | **M2** | F-35 "Later"; see also S-04 |
| C-12 | **Wall-clip fade** (head pushed into geometry) | Standard room-scale rule | **MVP** | #26, D-018 |
| C-13 | **Frame rate held at the headset rate** — judder is the cheapest way to make people sick | Meta VRC.Performance.1 (required there); our own MVP DoD | **MVP** (owner's PC) | #20, F-07/F-26; generalised to other PCs in #60, #23 (M2). See P-01 |

## 2. Comfort: posture, height, play space

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| H-01 | **Standing, seated, room-scale** all work | Meta VRC.Tracking.1 (required): state the modes and meet them; Road to VR lists seated mode | **MVP** | #28 (recenter; seated), D-018, D-020 "seated mode". **Acceptance line to add to #28 / #178:** the whole demo scenario is playable seated, with nothing on the floor out of reach (Meta VRC.9) |
| H-02 | **Artificial crouch** (button) next to real crouch; real crouch never required | Road to VR "real crouch required" is a store flag players filter on | **MVP** | #100 (physical) + #33 (toggle on right stick down) |
| H-03 | **Adjustable player height** | Road to VR "adjustable player height" | **MVP** / **M2** | Eye height from the standing head (D-018, #27); trim and "calibrate height" in #178; the independent height option is #101 (M2) |
| H-04 | **Recenter** that survives loads | Meta VRC.Functional.9; TF2VR learned the mid-load trap (F-120) | **MVP** | #28 (re-seed after a load: F-120 taken) |
| H-05 | **Respect the runtime's boundary**, never fight it | Basic safety | **MVP** | Nothing to build: the runtime draws the chaperone. Real steps (#26) move the body with the head, never the head against a wall |
| H-06 | **Real steps and the game's collision agree** | D-018 | **MVP** | #26 |

## 3. Input and controllers

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| I-01 | **Valve Index and Quest-family Touch** default layouts | Steam store page lists the devices the game supports (section 10) | **MVP** | #30, #35, D-019, controllers research |
| I-02 | **Fallback for any other controller** | SteamVR rebinds on its own for unknown profiles | **MVP** | `khr/simple_controller` + SteamVR auto-rebinding (#35, F-139) |
| I-03 | **HTC Vive wands, Windows Mixed Reality, Pico** — tested bindings | Players on those headsets read the store page's device list and the reviews | **MISSING** | Proposal: **skip until a tester has the hardware**; the store page honestly lists "Index and Touch tested". Note #87 (Quest 3 check) is the first such test |
| I-04 | **In-game remapping** (button, chord, long press) and a reset | Road to VR / Meta: remappable controls | **MVP** | D-020, #178; SteamVR's binding UI stays usable (F-139) |
| I-05 | **Hand-tracking / finger tracking** | Nice to have; Meta input VRCs apply only to apps that support hand tracking | **M2** | F-50 "Later"; not planned for the MVP |
| I-06 | **One-handed / one-controller play** | Meta accessibility VRC.4 (recommended); Cosmonious High added it by "switch off the second controller" | **MISSING** | Proposal: **M2**. Arx needs both sticks (move + turn) and a weapon hand. Possible route: a "one-controller" preset in #178's remapping (move and turn on one stick with a modifier). Do not promise it before a preset is tested |
| I-07 | **Haptics** on hits, pick-ups, menu ticks | Multimodal feedback, Meta VRC.3 (recommended) | **MVP** | #42, F-44; a haptics on/off or strength setting is **M2** (no task — add to #178 successor) |
| I-08 | **Quick save / quick load on a controller event** | Arx has F5 / F9; players do not want the menu every time | **MISSING** | Proposal: **MVP, no new task** — make "quick save" and "quick load" events in #30's action list so #178 can remap them; default binding the owner's choice (D-017) |
| I-09 | **Out of focus: keep rendering, hide the controllers' effect, ignore input** | Meta VRC.Input.4 (required there) | **MISSING** | See S-03 — the same event. The code logs the session states (F-118) but nothing reacts to them |
| I-10 | **Controller switch / loss while playing** (controller off, battery) | Meta VRC.Input.7 | **MISSING** | Proposal: part of S-03. The engine must not stick on "forward" when an action source goes inactive; OpenXR reports `isActive = false` per action, #74 should treat inactive as zero (check as an acceptance line in #74) |
| I-11 | **Text entry** (save names) | Players need a keyboard for names, SteamVR has one | **M2** | F-88 "Later (save names are pre-filled)" |

## 4. UI, text and readability

The original UI is 2D, drawn for 640×480 and scaled; on a panel it must be legible at the panel's
distance. This is the area where the original's design and VR meet worst.

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| U-01 | **Menus, inventory, book, map on a panel** with a pointer only while open | Skyrim VR reviews praise floating panels, criticise bad menu navigation | **MVP** | #49, #34, D-010, D-016 |
| U-02 | **UI rendered at panel resolution**, not stretched from a small texture | Legibility; Owlchemy: large clear text needed against the screen-door effect | **MVP** | #73 (F-129); verify with a screenshot of the book page at reading distance in #49 |
| U-03 | **Panel size and distance adjustable** | Meta: "larger UI elements" option; players have different headsets and sight | **MISSING** | Proposal: **M2**, into the VR settings next to Video (#181): a UI scale and distance setting. The MVP picks one good default on the owner's Index (D-017 — placement is the owner's design) |
| U-04 | **HUD that is always visible without opening a menu** — what is shown when no menu is open: health and mana, the pick-up / "cannot carry" messages, hints, the name under the reticle | A flat game's HUD does not stop existing in VR | **MVP** for health / mana (wrist, #116) and the reticle name (D-016) | **Other HUD lines are MISSING:** messages and hints, damage direction, spell icons. Proposal: **tester build** — decide which of them appear (a short text line at a fixed place in the view, or on the wrist panel). A player-experience design: ask the owner first |
| U-05 | **Dialogue and subtitles** readable at any head direction | Meta VRC.1/.2; Owlchemy's subtitle system (follows the view, larger when the speaker is far, shows who speaks) | **M2** | #53, F-89 for cutscenes. The MVP shows dialogue on the panel; the demo's goblin prison has little speech. **Rule for #53:** subtitles on by default |
| U-06 | **Contrast and a colour-blind-safe cue** | Meta VRC.6: never colour alone | **MVP** | The interactive reticle is yellow *and* shows the item's name (D-019); no other colour-only cue was found. A UI brightness option is in #181 |
| U-07 | **Controller diagrams and button glyphs** in the controls screen | F-46, F-150: TF2VR testers asked for it on day one | **M2** (README at #88) | F-46 / F-150 "Later"; the in-game page is part of O-01 |
| U-08 | **Loading screens and logos** shown in VR (not a black headset) | Meta VRC.Performance.3: head-tracked graphics within 4 s or a loading indicator | **M2** | F-117 rules in #20, the loader pump is after the MVP; #92 restores logos. See S-04 |
| U-09 | **The desktop window shows what the player sees** | Streamers, owner testing, a second person | **MVP** | #21, F-24 (a copy of the left eye) |

## 5. Rendering and performance

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| P-01 | **Hold the headset's rate** (Index: 80 / 90 / 120 / 144 Hz); no dropped frames in normal play | Meta VRC.Performance.1; Valve's compositor reprojects when missed, players read it as judder | **MVP** (owner's PC) | #20, F-07, F-26; the cost of drawing the scene twice at 120 Hz is the biggest known risk (RESEARCH-playable-v1). **Proposal for the store page:** recommend 90 Hz; leave the rate to SteamVR |
| P-02 | **Render resolution setting (supersampling)** | Every VR game has one; weak GPUs need it | **M2** | #59, #181 (D-020 later sections) |
| P-03 | **Anti-aliasing** | Visible shimmer in stereo | **M2** | #59, #181 (MSAA) |
| P-04 | **Draw distance / texture quality** | Performance headroom | **M2** | #181 |
| P-05 | **An FPS / frame-time readout** | Players report performance in reviews; SteamVR has its own | **M2** | #181 (FPS counter); SteamVR's overlay also works |
| P-06 | **Correct stereo for effects**: fog, flares, particles, sprites, spell glows, full-screen flashes | Wrong depth is an instant "looks broken" in a headset | **Accepted** in the MVP | #22, #58, F-23 (M2). Particle and sprite *re-projection* is MVP (#72) |
| P-07 | **Brightness / gamma in VR** | Meta VRC.5 (in-app brightness and contrast, recommended); the dungeon is dark | **M2** | #181 |
| P-08 | **Runs on weaker PCs and wireless headsets** (Quest 3 via Link / Steam Link) | Half of Steam VR players use Quest | **M2** | #87 (controls on Quest 3), #60 |
| P-09 | **Depth layer submitted to the compositor** (`XR_KHR_composition_layer_depth`) so SteamVR's reprojection / motion smoothing works well | One secondary article claims Valve now strongly recommends depth + motion vectors; **unconfirmed** — no primary source found | **MISSING** | Proposal: **a small research item inside #60** (does SteamVR use the depth layer for us; cost). Not before the MVP is playable |
| P-10 | **Frame pacing independent of refresh rate** | #23 | **M2** | #23 |

## 6. Audio

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| A-01 | **3D audio follows the head** — the listener position and orientation come from the headset | Sound that stays behind the body when the head turns is wrong in VR; our body yaw is decoupled from the head (D-008, #25) | **MISSING** | The word "listener" appears nowhere in the plan. Proposal: **MVP**, one small task after #24: set the OpenAL listener from the head pose (and verify with the existing test channel) |
| A-02 | **Audio goes to the headset's output** | Many VR games switch to the VR audio device; without it sound plays from the PC speakers while the headset is silent | **MISSING** | Proposal: **tester build**. First find out what SteamVR does by default for an OpenAL app (SteamVR's "set default audio device" setting is the player's); document it in the README or add a selector — a bug otherwise |
| A-03 | **Playable without audio / subtitles for all sounds that matter** | Meta VRC.1 (recommended) | **M2** | Covered by U-05; sound cues that matter for danger in Arx were not audited |
| A-04 | **Volume controls** | Basic | **MVP** | The game's own sound options on the panel (the original menu) |

## 7. Accessibility

Meta's accessibility VRCs are recommendations; the nine items are mapped here once, with the rest of
the checklist referring to them.

| ID | Item | Status | Where |
|---|---|---|---|
| X-01 | Playable without audio / subtitles (VRC.1) | **M2** | U-05, A-03 |
| X-02 | Legible text, larger UI option, contrast (VRC.2, .5) | **MVP** legibility, **M2** option | U-02, U-03, P-07 |
| X-03 | Multimodal feedback: visual + audio + haptic (VRC.3) | **MVP** | #42, existing audio and visuals |
| X-04 | One-handed play (VRC.4) | **MISSING** → M2 | I-06 |
| X-05 | Colour-blind support (VRC.6) | **MVP** (no colour-only cue found) | U-06 |
| X-06 | Rotate without turning the head (VRC.7) | **MVP** | C-01, C-02 |
| X-07 | Multiple locomotion styles (VRC.8) | **MVP** smooth + snap, **M2** controller-relative | C-03, C-04, C-06 |
| X-08 | All interactions seated or standing (VRC.9) | **MVP** | H-01 |
| X-09 | An alternative to precise gestures (Meta best practice) | **M2** | Runes drawn in the air have no non-gesture alternative in the MVP. #47 prepared spells and #48 spell book are the alternative; **#45 (tolerances) is what keeps the MVP casting forgiving.** Do not demote #47 below the first public build |

## 8. Gameplay systems that need a VR answer

The scope of the full game is in RESEARCH-playable-v1; this is only about things players of a flat-to-VR
port expect to *just work*. Arx-specific items that nobody has walked through in a headset are listed
as a single audit row.

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| G-01 | **Body visible, hands where the controllers are** | The first thing a player checks (Roboquest VR / VHVR reviews praise natural hands) | **MVP** | #36, D-009 |
| G-02 | **Weapon physics: swing to hit** | The reason to play in VR | **MVP** | #39, #75 |
| G-03 | **Bow, shield, block** | Players try to raise a shield or draw a bow | **Accepted** (M2: #40, #41) | MVP.md |
| G-04 | **Pick up / use / open with the hand-aimed reticle** | D-016 | **MVP** | #37, #38 |
| G-05 | **Torch / light source in the hand** — Arx's `player.torch` is an equipped light the dungeons need | Dark levels are unplayable without a light; flat Arx has it on a key | **MISSING** | The engine has a `torch` on the player; no task or decision says how it is equipped, switched on, or held in VR. Proposal: **MVP-adjacent**: decide where it lives (the off hand's IK hand) in the #75 design |
| G-06 | **Ladders, climbing, knockdowns, death animations** | Engine animations take over the body: hands stuck to controllers or the view changes | **Accepted** | F-53 "Later"; #29 (M2) for the camera |
| G-07 | **Trading, crafting, reading notes, character level-up** | Needed to finish the game | **M2** | #55, #52 |
| G-08 | **Lock picking, stealing, searching bodies, potions, food, scrolls, map** | Arx systems; each uses the mouse in the original | **MISSING (unaudited)** | Proposal: **M2**, a single audit task during the full-playthrough pass (epic #8): walk every mouse-driven interaction of the original, list the ones the reticle does not already cover |
| G-09 | **Combat feedback** (hit effects, damage flash, low health) comfortable and visible | Full-screen flashes are uncomfortable or invisible in VR | **Accepted** / **M2** | #22, F-35 (a pulse instead of a flash) |
| G-10 | **Difficulty setting** | Meta/Road to VR "adjustable difficulty" | **MVP** | The original's own option, reachable on the panel |

## 9. Onboarding, saving, pausing, quitting

| ID | Item | Expected because | Status | Where / proposal |
|---|---|---|---|---|
| O-01 | **First-use help**: a controls card, the rune gesture explained, the reticle explained | Arx has no tutorial; half of our controls (runes in the air, two reticles, grip + trigger) are new to every player. TF2VR testers asked on day one (F-150) | **M2** (README at #88) | F-150 "in-game prompts after MVP". Proposal: **tester build** — one controls card shown at the start of a new game and in the VR settings (the same card). Player-experience design: ask the owner |
| O-02 | **Zero-setup start** (headset detected, data found) | D-004; the Valheim VR mod's reputation | **MVP** | #17 (done), #79, #80 |
| O-03 | **A helpful error when the game data is missing or SteamVR is not running** — shown **on the desktop**, not as a black headset | A silent black headset reads as a broken game, and is a top reason for refunds | **MVP** | #79 / #65 own the detection; **add the message to #79's DoD**: a desktop dialog or console line that says what to do, before VR starts |
| O-04 | **New game / continue / load from the VR menu** | Without it the player needs a keyboard | **MVP** | Checkpoint B (#49, #34) |
| O-05 | **Saves compatible with flat Arx** | Nice to have for players who also play flat | **MVP** | D-007 (same save folder) |
| S-01 | **Save / load from the panel; autosave** | Meta VRC.Functional.4 (do not lose data) | **MVP** (panel); autosave **MISSING** | Arx has quicksave; autosave was not found in the plan. Proposal: **tester build**: check what the original offers (autosave on level change) and keep it on |
| S-02 | **Quit cleanly** from the menu and from the headset | The smoke test already checks "Clean shutdown" | **MVP** | #93 (done), #99 |
| S-03 | **Dashboard / headset off → the game pauses** | Meta VRC.Functional.2 (required): a single-player game must pause when the platform asks; players open the SteamVR dashboard mid-fight | **MISSING** | Verified in `src/vr/VRSystem.cpp`: the session state is logged (F-118) but **nothing pauses the simulation**. Proposal: **MVP** — pause game time and ignore input when the session leaves FOCUSED, resume on FOCUSED (the engine's own pause path), plus a log line and a `status.json` field. This also covers I-09 / I-10 |
| S-04 | **The compositor is fed during long loads** (a frame pump while a level loads) | Meta VRC.Performance.3; a frozen view during a level load is the usual cause of "is it crashed?" | **M2** | F-117: rules in #20, the frame pump after the MVP. **Proposal: bring the pump forward to the tester build** — every level change shows it |
| S-05 | **Health / comfort notice on the store page** | Customary for VR titles | **MISSING** → M3 | #68; one line in the store text, no code |

## 10. Store page and requirements

What Steamworks asks for (read from the Steamworks pages): the store page has a **VR Devices and
Support** block where the game names the SDK (OpenXR / SteamVR first), the **supported headsets and
controllers**, and the **play area** needed; a VR launch option must be defined before VR support is
enabled (and a non-VR launch option can be added as another mode); the input-binding mode is set in
Application > Virtual Reality. With OpenXR, Valve's page says to suggest bindings for Meta Quest 2
controllers and that **Index, Vive wands and WMR controllers need custom bindings** — matches #35.

| ID | Item | Status | Where / proposal |
|---|---|---|---|
| R-01 | Store page: supported headsets and controllers named honestly (Index tested, Touch via Steam Link, others "may work") | **M3** | #68 |
| R-02 | Store page: play area / seated-or-standing statement (Meta VRC.Tracking.1 wording) | **M3** | #68; H-01 decides the wording |
| R-03 | Store page: system requirements for VR, the "requires Arx Fatalis" note | **M3** | #68 |
| R-04 | A non-VR launch option (the game also runs flat) | **M3** | Exists (`--no-vr`); add as a Steam launch option in #69 |
| R-05 | Launch from the SteamVR dashboard | **MVP** | #80 |
| R-06 | Steamworks SDK / GPL question, ownership check, name, rights | **M3** | #62–#66, #67 |
| R-07 | Performance target statement on the store page | **MISSING** → M3 | Proposal: one line "90 Hz recommended"; gather the numbers from #60 first |

## 11. Support and updates

| ID | Item | Status | Where |
|---|---|---|---|
| T-01 | Logs and a way for players to send them | **MVP** | D1–D3 (#129–#131), F-91 bundle at #88 |
| T-02 | Crash reports under SteamVR with symbols | **MVP** | #132 |
| T-03 | A known-issues page for testers | **M2** | F-130, #88 |
| T-04 | Tester build for a friend, then Steam beta branch | **MVP** / **M3** | #88, #69 |
| T-05 | Bug-report template with headset, runtime, refresh rate | **MVP** | F-100 at #88 |

---

## Missing items for the owner's decision

14 proposed actions (some cover several rows). Ordered by how soon each should be decided. **Slot** is
the proposal; D-017 marks those that are player-experience design.

| # | Item | IDs | Why it matters | Proposed slot | Cost | D-017? |
|---|---|---|---|---|---|---|
| 1 | **Pause the game when the session leaves FOCUSED** (dashboard opened, headset off); ignore input then | S-03, I-09, I-10 | Platform-required for single-player games on Meta; with a long fight and an opened dashboard the player dies unseen | **MVP** — before the tester build | S–M, in the VR runtime area | no |
| 2 | **Audio listener = head pose** | A-01 | The decoupled body (D-008) leaves sound at the body, not the ears | **MVP** | S | no |
| 3 | **A desktop error when the data or SteamVR is missing** | O-03 | A black headset reads as a broken game | **MVP** (a line in #79's DoD) | S | no |
| 4 | **Quick save / quick load as remappable events** | I-08 | Everyone who plays Arx uses F5 / F9 | **MVP** (a line in #30) | S | button default is D-017 |
| 5 | **How the torch is held and lit** | G-05 | A dark dungeon with no light is unplayable; the engine has a torch on the player | **MVP-adjacent** (decide with #75) | design first | **yes** |
| 6 | **Which HUD lines show when no menu is open** (messages, hints, damage direction, spell icons) | U-04 | Without a decision the player gets no feedback at all about "cannot carry", level-up, quest updates | **tester build** | design first | **yes** |
| 7 | **Bring the load-time frame pump forward** | S-04, U-08 | Every level change is visible; "is it frozen?" ends sessions | **tester build** (from F-117) | M | no |
| 8 | **Audio output device** | A-02 | Silent headset if sound goes to the PC speakers | **tester build** (find out, document or add a selector) | S | no |
| 9 | **A controls card for the first use** | O-01 | Runes in the air, two reticles, grip + trigger are unexplained | **tester build** | S–M | **yes** |
| 10 | **Autosave** (if the original lacks it) | S-01 | Death + lost progress is a classic review line | **tester build** (check first) | S | maybe |
| 11 | **Panel size and distance setting** | U-03 | Different headsets, different eyes | **M2**, in #181 | S | **yes** |
| 12 | **One-controller preset** | I-06, X-04 | Meta VRC.4 (recommended); accessibility goodwill | **M2** | M | **yes** |
| 13 | **Depth layer for the compositor** | P-09 | Possibly better reprojection under load; unconfirmed | **M2**, inside #60 | research | no |
| 14 | **An audit of every mouse-driven interaction** (lock picking, stealing, bodies, potions, scrolls, map) | G-08 | Nobody has walked these in a headset | **M2**, in the full-playthrough pass | M | no |

**Also noted, not missing (existing tasks that need a line added):**

- **#28 / #178:** "the whole demo is playable seated" as an acceptance line (H-01).
- **#24:** check stair behaviour for camera smoothing (C-09).
- **#74:** an inactive action source reads as zero, never as "stuck" (I-10).
- **#49:** a screenshot of the book page at reading distance shows the text is legible (U-02).
- **#92 (restore cinematics for players) must not ship before #29 / #54:** forced cameras are the
  most reliable nausea source in a port; today the development build skips every `.cin`, and
  restoring them for players without a VR-safe handling (flat virtual screen, F-89) would reverse
  that. Recorded here as a dependency to add; it was not in the plan.
- **C-06 teleport:** the proposal is to skip; if the owner disagrees it is a design task.

## Recommendation

The plan is strong on comfort (turn options, no bob and shake, seated, left-handed, remapping,
crouch alternatives) and on input. The gaps cluster in three places: **what the runtime tells us**
(focus loss, loading) — items 1, 3, 7; **what the player hears and reads** (listener, output device, HUD
messages, subtitles) — items 2, 6, 8; and **the first minute** (controls card, errors, autosave) —
items 3, 9, 10. Items 1–4 are cheap, have no design component and can start at once; items 5, 6, 9
need the owner's design answers first.

## Sources

- [Meta — Virtual Reality Checks (VRC) requirements](https://developers.meta.com/horizon/resources/publish-quest-req)
- [Meta — Accessibility checklist](https://developers.meta.com/vr/design/accessibility-checklist/)
- [Meta — Locomotion best practices](https://developers.meta.com/vr/design/locomotion-best-practices/)
- [Road to VR — VR comfort settings checklist and glossary](https://roadtovr.com/vr-comfort-settings-checklist-glossary-developers-players/)
- [UploadVR — Little Nightmares VR adds comfort settings after player feedback](https://www.uploadvr.com/little-nightmares-vr-altered-echoes-adds-new-comfort-settings-after-player-feedback/)
- [Unity — Accessible VR game design tips from Owlchemy Labs](https://unity.com/blog/games/accessible-vr-game-design-tips-from-owlchemy-labs)
- [Steamworks — Application settings for virtual reality](https://partner.steamgames.com/doc/features/steamvr/settings)
- [vr.org — Steam Frame, 72 fps requirement (secondary, unconfirmed)](https://vr.org/articles/steam-frame-verified-72fps-valve-quietly-cut-requirement-2026)
- [Vaporlens — Roboquest VR review summary](https://vaporlens.app/app/3151760/roboquest_vr/stats)
- Internal: [MVP.md](MVP.md), [DECISIONS.md](DECISIONS.md), [RESEARCH-playable-v1.md](RESEARCH-playable-v1.md),
  [RESEARCH-controllers.md](RESEARCH-controllers.md), [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md)
