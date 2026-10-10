# Arx Fatalis VR — Traps

Things that already cost us time: misleading symptoms, stale state, instruments that lied. Read this
before debugging something that "makes no sense". **Add an entry in the same PR as the work that hit
the trap**; never delete entries (mark them *resolved* if the cause is gone). Newest first.

Format: **symptom** — what is really going on — what to do.

---

## 2026-10-10 — A sequence passes alone and fails in CI with a value it never set (#178)

**Symptom:** in CI `vr-remap-saved` found `remapped` false and `vr-settings-flat` found `jump` = `right.b`, both passing
with `xrs-run.ps1` alone. **Cause:** CI's game checks run every shipped `.xrs`, in alphabetical order, in **one**
user and config directory: `vr-remap-saved` ran before the `vr-remap` it expected, and `vr-remap` left its saved layout
(`vr-controls-index.ini`) for `vr-settings-flat`. **Do:** a sequence sets up what it needs and leaves the user
directory as it found it (here: `vr-input default` at the start and the end; `vr-input reload` instead of a second
start). Anything a sequence saves (cfg, layouts, saves) is shared with the next one.

## 2026-10-10 — A remap through the channel records a long press nobody made (#178)

**Symptom:** `vr-remap.xrs` captured `jump` as `right.b:long` after `vr-input down right.b` … `vr-input up right.b`;
the same sequence had passed an hour earlier. **Cause:** every channel step is a round trip of about 0.6 s, so a
`down` and an `up` in separate steps hold the control longer than the 0.5 s of a long press — the capture (and the
control table) correctly records a long press; on a quiet machine it may just stay under. **Do:** a short press in a
sequence is `vr-input tap` (pressed and released within one update), the second control of a short chord too; a
`down` / `up` pair only for a press that is meant to be long (with a `@wait` well above 0.5 s).

## 2026-10-10 — In VR an item on the floor is not picked where the cursor is, or a far one is (#37)

**Symptom:** with a mouse position projected from a 3D point in a VR run, `FlyingOverObject` finds nothing over
an item lying in the world, or something beside it. **Cause:** the engine's pick is a 2D test against each
entity's `bbox2D` and projected faces (`vertexClipPositions`). Animated entities are projected with the head
camera in the update, but a **static item** (no animation: what lies on floors and tables) is projected in
`DrawEERIEInter` while it is drawn — once per **eye**, into the eye image's pixels — so after a stereo frame
its box is the right eye's, in another pixel space than the interface the mouse lives in. *Resolved for the
screen data by #72* (picking reads the head camera's at the interface's size again). **Do:** in VR the world is
still picked by the hands' rays in 3D (`vr/VRReticle.cpp`) — a hand's ray is not a ray through the eye, so a
projected pixel lands on whatever the head sees there, not on what the hand points at; the engine's
`GetFirstInterAtPos` asks the reticle module what a world reticle's mouse position stands for
(`vr::reticlePickAt`), and an interface position never picks the world while the reticles are on. The test
channel's `pick` checks the engine's own screen pick under `vr::EnginePickOnly`.

## 2026-10-10 — The inventory opens by itself, or the player spins, when a VR hand becomes the mouse (#37)

**Symptom:** forcing the engine out of mouse look so the hand could be the mouse opened the inventory at
once; later, with the inventory open, the player kept turning (`player.yaw` 113 → 56 → 159 → 261 in two
seconds) with nobody touching anything. **Cause:** (1) in Arx the cursor mode *is* the inventory: leaving
mouse look shows it unless the inventory key inverted that (`bInverseInventory`, `ArxGame::manageKeyMouse`).
(2) In the cursor mode a mouse within 50 px of an edge of the interface turns the player (edge scrolling,
`Interface.cpp`); a world reticle's mouse position is right of the interface (`vr/HandReticle.h`), and a
panel pointer near the panel's edge is near the interface's edge. **Do:** keep the engine's mouse-look state
as it is (the hand's position is applied in the relative mode too, `Input::setVirtualMousePosition`); the
edge turning is off while a VR session renders.

## 2026-10-10 — On `mvp-items` an item can be picked from the table but not put back on it (#37)

**Symptom:** a drag started from the table (`item.take`), but the item never landed where the hand pointed:
it stayed in front of the player or was dropped on the floor; the floor in front of the player was not hit
by any ray. **Cause:** the save stands the player on the **wall top above the table's room**: the eye is at
`y` 2765, the room's floor at 3100 (y down), the table top at 3018. The engine picks within 350 units of the
player but puts down only within **300** (`updateDraggedEntity`), and nothing below the player is within 300;
the key on the floor (399 units away) is not even pickable. **Do:** put the player on the room's floor by
the table first — `setpos 10600 3100 11420 72` (as `hand-reticle.xrs` does); the note and the form are then
~1.5 m away, both for picking and for putting down. Mind the goblin in that room.

## 2026-10-10 — A head pressed into a wall is "not in geometry": `CheckBackgroundInSphere` misses the middle of a wall (#26)

**Symptom:** the simulated head is moved into the start cell's side wall — the body is blocked at the wall, the eye
shot shows the wall face filling half the view at a few centimetres — and the near-plane sphere test
(`CheckBackgroundInSphere(Sphere(head, 12.5))`) says nothing is there; once the head is past the wall the ray test
finds it at once.
**Cause:** the engine's `IsPolyInSphere` does not measure the distance to a polygon: it only tests whether the
polygon's corners, edge midpoints and a few points towards its centre lie in the sphere (it was made for the
50-unit cylinders of NPCs). A small sphere in front of the middle of a large wall polygon contains none of them.
**Do:** for anything smaller than a body, measure the distance to the polygon's triangles —
`vr::distanceToTriangle` (HeadLean.h), used by `backgroundCloserThan` in `ArxGame.cpp`; `raycastScene` is exact
for a segment.

## 2026-10-10 — A shipped `.xrs` sequence is red in CI only because of the sequence that ran before it (#296, from #178)

**Symptom:** the CI game check fails a sequence that passes alone: `vr-remap-saved` expected the file `vr-remap` saved
(it ran *after* it alphabetically), `vr-settings-flat` found the `vr-controls-index.ini` that `vr-remap` left. Rename or add
a sequence and the order — and the verdict — changes.
**Cause:** `ci-smoke.ps1` ran every sequence in the runner account's single user dir, so config, saves and ini files leaked
from one sequence into the next.
**Do:** every sequence now runs in its own fresh user dir (`New-ArxSequenceUserDir` in `xrs-lib.ps1`, passed to
`smoke-test.ps1 -UserDir`; its `@save` is copied in as before; the dir is removed after the run and its logs are in the
phase's evidence). A sequence that needs a file from another one is wrong: set the state up in its own `@save` / commands.
The first fresh-dir CI run found it at once: `walk-forward`, `walk-look` and `sneak-jump-crouch` had `@args --loadslot 0` but no
`@save`, and had only ever passed on the save an earlier sequence left in the shared dir ("no such save, 0 save(s)"). They
declare `@save mvp-new-game` now, and `test-xrs.ps1` fails a shipped sequence with `--loadslot` and no `@save`.
Checked by `test-xrs.ps1` ("fresh user dir per sequence"). Locally, `smoke-test.ps1 -Sequence` already used the isolated dir of
the working copy: run two sequences by hand with `-UserDir` pointing at different folders for the same effect.



## 2026-10-10 — The arm clips are "suppressed", yet the shoulder moves 10 units when the combat mode starts (#75)

**Symptom:** `vr.weapon.armAnimSuppressed` is `true` and every `vr.body.armAnim` event says `suppressed`, but
`vr.body.right.shoulder` jumps by ~10 units between before `combat on` and after the strike.
**Cause:** the combat mode also switches the **body's** layer 0 to the fight stance (`ANIM_FIGHT_WAIT`,
`ARX_PLAYER_Manage_Visual`), which turns the torso; only layer 1 (the arm clips) is suppressed (D-009: the body
keeps the engine's animations). **Do:** measure what the arm layer does from a settled stance — take the
shoulder after `combat on` + 60 frames (300 ms of blend), then strike, as `weapon-hold.xrs` does. Also: never
ask `entities.player()` while writing `status.json` before a level exists (the first `weapon-hold` run crashed
at start-up on it, #286) — check this module's own state first.

## 2026-10-10 — .xrs: a count of what a held input did passes locally and fails on the CI runner (#285)

**Symptom:** `snap-turn.xrs` held the stick until the 5th snap, let go and allowed one more snap; locally it
passed, on the CI runner it counted 10 to 12 (`ASSERT FAILED: vr.input.turn.snaps (12) le 9`).
**Cause:** a channel command reaches the game a poll after the runner sends it, and the runner's round trip on a
loaded machine is longer than a repeat (0.4 s), so the release came 2–4 repeats late. An upper bound on a count
after a release is a bound on the runner's speed.
**Do:** let the game measure the timing by its own clock and assert on that (`vr.input.turn.repeatInterval`);
in a sequence, bound only what the game decides, never how many times something happened before a later command.

## 2026-10-10 — The arms twist outward and wrap into spirals from the shoulder and the elbow when the wrist turns (#36, owner's headset run of PR #254)

**Symptom:** the body, the solid torso and the hands following the controllers are right in the headset
("руки работают"), but as soon as a controller rolls, the upper arm and the forearm of the model twist into
flat spirals between the shoulder and the hand (`C:\Users\arzib\arx-fatalis-vr-wt\arms-twist-owner.png`:
both arms reach in from the sides as twisted ribbons while the hands themselves look fine). Every number in
`status.json` is still perfect — `error` 0, the wrist on its target — because the numbers describe joints,
not the mesh between them.
**Cause:** the engine's skeleton has no twist model: a group's vertices turn rigidly with the bone's quaternion,
and the IK of #36 only *swings* each bone onto its joints (`rotationBetween` on top of the animated quaternion)
while the wrist takes the controller's full orientation. The roll about the arm's axis is therefore whatever
the animation plus the swing leave — nothing distributes the hand's roll along the forearm or keeps the
upper arm's roll tied to the elbow — and the low-poly arm, a flat-ish slab, shows every stray roll as a spiral.
A joint-position check cannot see it.
**Do:** accepted as the interim body (D-032 / #36); the arm rework is a research task (how Valheim VR builds
its arms, what Arx's model and textures allow: twist bones, roll limits, a separate VR arm rig). Until then,
judge arms in the headset or on a capture with the controllers *rolled*, not from `vr.body` alone; the
`arms-follow` sequence rolls nothing.

## 2026-10-09 — The body is nowhere in a `--vr-sim` shot that looks down, although `vr.body` says both arms are posed with `error` 0 (#36)

**Symptom:** the first two runs of `arms-follow` on the default `--loadlevel 10` had every status number right
(chains found, wrists on their targets) and both eyes' pictures showed a bare floor from a standing height —
no torso, no arms, and the picture even drifted between two "still" shots.
**Cause:** level 10 is the scripted intro: its conversation camera (`speechControlledCinematic`, see the #24
entry below) is a camera somewhere in the cell, not the model's eyes, and it moves with the speech; the head
pose is applied on top of it. The model stands elsewhere, correctly posed.
**Do:** anything that must *see* the body runs on a first-person save: `smoke-test.ps1 -VrSim idle -Sequence
arms-follow -Save mvp-new-game`. Status numbers prove the pose; only a first-person save proves the picture.

## 2026-10-09 — The arm chain found from the hand is one joint short, and the hand ignores the controller's pitch (#36)

**Symptoms:** (1) walking up from the group that holds `primary_attach` gave `right_elbow > right_wrist >
right_hand` with bone lengths `30.5 + 0.0`, so the "arm" reached only 30 units and the shoulder slid 17.6 units
towards a controller held comfortably in front. (2) With the wrist bone set to the controller's orientation the
fingertips stayed at the same height whether the grip was upright or pitched 90° forward.
**Cause:** `human_base` keeps the hand as a **zero-length child group** of the wrist (`right_hand < right_wrist`,
same origin), and the engine's animations bend that hand group on the wrist (the wait animation curls it). The
wrist joint is therefore one group up, and the hand mesh belongs to the child group, whose rotation the
concatenation rebuilds from the animation after the wrist was set.
**Do:** the wrist joint is the first ancestor with a non-zero `transinit_global` (climb the zero-length bones);
the zero-length groups between it and the hand's group are made rigid with the wrist (`ArmChain::rigid`,
`vr/VRBody.cpp`). Check a chain by its lengths in the `[vr] body:` bind line (`27.6 + 30.5` on `human_base`) and
a hand by `vr.body.<hand>.tip` against `wrist` / `controller`, not by the picture alone.
## 2026-10-10 — The sneak covered more ground than the run (#33)

**Symptom:** on `mvp-new-game` a slight stick push (the stealth walk) moved the player 355 units in 1.5 s and the
full push right after it only 201; with `@frames 40` instead the sneak covered 136-151 and the run 163-197 — the
ratio changed from run to run.
**Cause:** a distance measures the room and the clock, not the walk: the second walk ran into the cell's far end,
and `@frames n` is n frames, whose length swings with the frame rate of the moment (36-90 fps in these runs).
**Do:** compare walk speeds with `player.animSpeed` (the current animation's own units per second, what the engine
moves the player by: run 192, stealth 135.6); use positions only for "it moved", from a known start.

## 2026-10-10 — `test-xrs.ps1` fails once in a while: "timed out waiting for the game to acknowledge" (#275)

**Symptom:** one check of 73 fails on the CI runner (a `@shot`/`@diff` step), the rerun of the same commit passes.
**Cause (most likely, not reproduced in 20 local runs):** the fake game read `command.txt` with `Get-Content` and
wrote `ack.txt` with `Set-Content`, whose errors are non-terminating — a sharing violation with the test's own
50 ms polling (or a scanner on a loaded runner) silently lost the batch or its ack, so nothing was acknowledged.
**Do:** the fake game removes `command.txt` only after reading it and retries both the read and the ack write.
When a channel check times out with the game alive, suspect a dropped file operation before a slow game.

## 2026-10-09 — The body turned 45° by the numbers, the picture did not turn (#32)

**Symptom:** on the simulated headset with `--loadlevel 10` (+ `setplayercontrols on`) a snap turn changed
`player.yaw` and `camera.yaw` in `status.json` by exactly 45°, the `vr.turn.snap` game event was logged, and the
eye shots before and after still looked down the same corridor (picture change ~4, barely over the floor);
a head turn of the simulator did turn the picture.
**Cause:** the trap of #24 again — level 10 is the intro conversation, whose camera is placed by
`speechControlledCinematic()`, not from the player's yaw, so nothing the **body** does shows in the view.
`status.json` → `camera.yaw` is the engine's `g_playerCamera`, not what the eyes render with.
**Do:** check turning, walking, anything the body does, on a first-person save: `@save mvp-new-game` +
`@args --loadslot 0` in the `.xrs` (snap turn there: picture change 16.9). Judge by the shots, not by
`camera.yaw`.

## 2026-10-10 — A channel-driven check times out with `ack.txt` holding only `quit`, though the game ran (#258)

**Symptom:** `body-check.ps1` fails with "timed out after 180 s waiting for gameplay"; no command of the check
reached the game. **Cause:** `smoke-test.ps1` first waits up to 300 s for the game lock (D-014); the check's
own timeout counted that wait as game start-up. **Do:** a check that drives the game from outside calls
`Clear-ArxLaunchMarker` before it starts the smoke test job and `Wait-ArxGameLaunched $job $dir` before any
timeout of its own — `smoke-test.ps1` writes `launched.txt` into the test folder right after the game started.
`false` means the smoke test ended without a launch (lock skip = exit 2). Covered by `test-wait-launch.ps1`.

## 2026-10-09 — In the headset the camera floats far above the body (#24, #27)

**Symptom:** looking down in the headset, the character's body is far below the eyes, although the camera
math is right and every flat or simulated check passes.
**Cause:** the head's reference (the head there = the character's eye) was taken from the first tracked
sample after the level load — and the owner starts a test at the desk: the session runs `synchronized`
with the headset lying there (the runtime still locates it), and it is put on 50–80 s later
(`vr.session state=visible` in arx.log). The worn head was ~0.8 m above the desk reference, so the camera
was 80 units above the eye.
**Do:** read `vr.head.reference` in arx.log: `kind`, `wornS` and `headAboveFloorM` say when and from what
the reference came; `status.json` → `vr.head.worn` / `referenceKind`. Since #27 the settled reference
only comes from a worn headset (D-031). To reproduce without a headset: `session idle` in a `--vr-sim`
script (`scripts/dev/vrsim/eye-height.vrsim`).

## 2026-10-10 — The 2D interface is empty in `--vr-sim` shots, or the inventory "never closes", on level 10 (#49)

**Symptom:** on `--loadlevel 10` every shot's flat frame is `transparent` and `quadPixels` is **0** — no HUD,
no inventory, even after `ui inventory open`. After `setplayercontrols on` / `cinemascope off` the
inventory opens by itself, and a `ui inventory close` is followed by `ui.inventory state=closed` and,
one frame later, `state=open` again: `player.inventoryOpen` never becomes false.
**Cause:** (1) while the level 10 start holds the player the HUD is not drawn at all (TESTING.md, the
`ui` command). (2) Released, the level's start shows the bag and opens it again after every close (seen
flat and in VR, #49; the cause in the level's script was not chased). (3) `quadPixels` of a shot counts
the eye pixels the **drawn** interface covers (alpha > 0), not the quad's area: an empty blended quad is 0.
**Do:** for anything about the HUD or the menus use the test save `mvp-new-game` (#70: controls free, the
bag closed) — `smoke-test.ps1 -VrSim idle -Sequence <name> -Save mvp-new-game`, as `ui-panel.xrs` does.

---

## 2026-10-09 — A check of "what the button did" counts one event too many, or finds it before the press (#74)

**Symptom:** a flat check of the VR buttons on level 10 sees `ui.inventory=3` after two presses of the
inventory button, and a `game.state to=MENU` that is not after the `[mark] game menu` sent right after the
menu button — "the menu never opened", although it did.
**Cause:** (1) the level 10 start shows the inventory bag: once `setplayercontrols on` / `cinemascope off`
give the controls back, a `ui.inventory state=open` is logged with no button pressed — the first press
*closes* it. (2) A `vr-input` tap is resolved at the start of the next frame and the game reacts in that
frame, so its game event can be logged before the ack of the command and before the next `mark` line.
**Do:** anchor on the button's own `[vr] input: <event> (…)` log line (it comes first in the frame, before the
game events) and check the order of the state changes after it, not their total — as
`scripts/dev/game-input-check.ps1` does.

## 2026-10-09 — A shot in the first second after a level load is black (#112)

**Symptom:** a `--vr-sim` shot (or a channel `shot`) taken right after `status.json` says `GAMEPLAY` judges
`black` on `--loadlevel 10` — the same verdict as the real black-world bug of #215 — and a shot a second later
on the same level says `world`. An eye-check run with early shots "passes" its yaw leg because the first shot
is black and the later one is not.
**Cause:** the level fades in from black for about a second after it is playable; nothing is wrong with the
render. The game state is `GAMEPLAY` from the first frame of the fade.
**Do:** shoot 3 s in: the `scripts\dev\vrsim\*.vrsim` scripts start at `@270` (90 Hz). A real black world
stays black: compare two shots seconds apart before calling it a bug.

## 2026-10-09 — A unity build sees one file's macros and helper names in every file after it (#112)

**Symptom:** a build fails with `'::xrWaitFrame': undeclared` in a file that includes `<openxr/openxr.h>`
itself, or `C2374 'anonymous-namespace'::Epsilon': redefinition` / `fov ... already has a body` in a test
file that compiled fine alone.
**Cause:** `UNITY_BUILD` (on by default, off only with `DEVELOPER`) compiles all sources of a target as one
file (`ub_*.cpp`, sorted by path). An include guard opened by an earlier file keeps its macros: once
`vr/OpenXRUtil.h` defined `XR_NO_PROTOTYPES`, a later `#include <openxr/openxr.h>` adds nothing. And every
file's anonymous namespace is the same one, so two test files with a helper `Epsilon` or `fov` collide —
`arxtest` is a unity build too.
**Do:** a file that needs the OpenXR loader's functions declares them itself from the `PFN_` types
(`src/vr/XRDispatch.cpp`); give file-local helpers file-specific names (`ImageEpsilon`, `imageFov`). `Near` and
`Far` are macros from `windows.h` on MSVC too (hit in #19's test): not identifiers in any file that sees it.

## 2026-10-09 — A scene drawn into a framebuffer object lands at the wrong place, is clipped, or loses its alpha cutouts (#19)

**Symptom:** rendering through the engine's renderer into an OpenGL framebuffer object of another
size than the window (a VR eye image, 2468×2740) puts the picture in a corner or off the image;
scissored draws (the cinematic border) cut the wrong band; `getSnapshot()` reads the window's size;
fences and foliage with alpha cutouts come out as solid black quads on the eye image although they are
fine in the window.
**Cause:** `OpenGLRenderer::SetViewport` / `SetScissor` turn the engine's top-left rectangles into
OpenGL's bottom-left ones with the **window's** height, and `getSnapshot` reads the window's size; and
with MSAA on the window the renderer draws alpha cutouts with alpha-to-coverage or sample shading
(`alpha_cutout_antialiasing`), which do nothing on a single-sample framebuffer object - the alpha test
that would discard the transparent texels is off.
**Do:** tell the renderer the bound target's size (`Renderer::setRenderTargetSize`, 0 = the window): it
re-applies the viewport and the scissor against that height, snapshots read that size, and
`SetAntialiasing(true)` is refused on a single-sample offscreen target so the alpha cutout path falls
back to the alpha test (a multisampled one names its samples too, `setRenderTargetSize(size, samples)`, #226).
`vr::beginEyeRender()` / `endEyeRender()` and the flat frame target do this; a new offscreen target must too.
With eye MSAA (`vr_msaa`, D-035) the eye is drawn into a multisampled buffer, not the swapchain image:
the image holds the picture only after `Swapchain::resolve()` in `endEyeRender` - read eye pixels after it.

## 2026-10-10 — "use" fires from just holding the Index controller (a closed hand flickers, #30)

**Symptom:** the log shows `[vr] input: use_right (right.grip)` several times (×6 in the owner's headset run)
although the player only held the controller and made one grip + trigger chord; with a game reaction (#74)
that is a right click on whatever the reticle is on.
**Cause:** the Index's `input/squeeze/value` measures how far the hand is closed around the handle. A relaxed
closed hand sits around 0.6 – 0.7, right on the 0.70 / 0.55 threshold borrowed from Dishonored VR (F-41), so
it crossed it back and forth; each dip was a release of the grip — which leads the stash chord, so its own
binding fired as a tap on release. A "firm" threshold is the wrong model too: the Index is strapped on, a
light close is the grip and an open hand is none (D-019, owner 2026-10-10) — `squeeze/force` (a hard
squeeze) was tried in round 2 and dropped before a headset run for that reason.
**Do:** the Index grip stays on `squeeze/value` with a low press and a wide gap (`IndexGripThreshold`
0.50 / 0.30); a chord leader's release is debounced (`ControlResolver::releaseDebounceMs`), so a flicker is
one hold; "use" fires when the hand opens without a trigger (D-019). Every grip crossing is logged with its
value (`[vr] input: right grip down at 0.55 (threshold 0.5 / 0.3)`), and `status.json` → `vr.input.grip`
shows it live — tune the threshold from those numbers, not by feel.

## 2026-10-09 — A non-Steam shortcut written into `shortcuts.vdf` disappears

**Symptom:** a script (or a hand edit) adds a shortcut to `<Steam>\userdata\<account>\config\shortcuts.vdf`,
the file has it, yet the Steam library does not show it — and after the next Steam exit the entry is gone.
**Cause:** Steam reads `shortcuts.vdf` once at start, keeps the shortcuts in memory and writes the
whole file back when they change or when it exits; an edit made while Steam runs is overwritten.
**Do:** exit Steam first (Steam > Exit, not just closing the window), write, then start Steam.
`scripts/add-steam-shortcut.ps1` refuses while `steam.exe` runs (#80).

## 2026-10-09 — A hex literal like `0x80000000` is negative in PowerShell

**Symptom:** `[uint32]0x80000000` throws "Value was either too large or too small for a UInt32";
a CRC32 written in PowerShell gives wrong values or the same error mid-loop.
**Cause:** PowerShell parses a hex literal as `int` when it fits in 32 bits as a bit pattern, so
`0x80000000` is `-2147483648` and `0xEDB88320` is negative; `-bxor`/`-shr` then work on signed values.
**Do:** suffix 32-bit constants with `L` (`0x80000000L`, `0xEDB88320L`), compute in `[long]`, and cast
to `[uint32]` only at the end (`scripts/steam-shortcuts-lib.ps1`, #80).

## 2026-10-09 — The headset test cannot start: "could not read PR", or the game starts flat with SteamVR off (#171)

**Symptom:** `test-pr.ps1 <pr> -StartSteamVR` stops at once with `API rate limit already exceeded` /
`could not read PR #<n>` although the PR's worktree and fresh build are on disk; a hand-made fallback launch
(`run-windows.ps1` in that worktree) starts the game **flat** and no SteamVR process appears.
**Cause:** the station read the PR through the GitHub API, whose quota every worker session shares; and nothing
starts SteamVR on its own — the game's OpenXR call is not a reliable way to bring it up (and D-014 makes the
scripts add `--no-vr` while `vrserver` is off anyway).
**Do:** `test-pr.ps1` now falls back to `git ls-remote origin refs/pull/<n>/head` + the local worktree (`-Branch`
when even git cannot reach GitHub), and `-StartSteamVR` starts SteamVR explicitly (`steam://rungameid/250820`,
then `vrstartup.exe`) and waits for `vrserver` + `vrcompositor`. Never test the start logic by starting SteamVR as
a worker: `scripts/dev/test-start-steamvr.ps1` proves it with stubs. A `Get-Process`-style stub for a cmdlet in a
PowerShell self-test must be a function with `[CmdletBinding()]`, or `-ErrorAction` breaks it.

**Stopping SteamVR (same task):** `CloseMainWindow` on `vrmonitor` plus `Stop-Process` on the rest took the software
away while the Valve Index stayed on — the drivers never got their own shutdown. SteamVR's status window takes URL
commands (`vrmonitor.exe` has the `quit` handler of the `vrmonitor://` protocol), so `stop-steamvr.ps1` /
`test-pr.ps1 -StopSteamVR` send `vrmonitor://quit` — the request behind "Exit SteamVR" — and only wait for the
processes to leave (60 s); they never kill. Found by reading the strings of `vrmonitor.exe` and the registry entry
`HKCU\Software\Classes\vrmonitor`, not from documentation (there is none): the headset powering down is confirmed
by the owner's run only.

---

## 2026-10-09 — The head turns, the picture does not: the camera of a conversation owns the view (#24)

**Symptom:** on the simulated headset `eye-check.ps1` failed its yaw leg on the #24 branch although the head
drove the camera — the `yaw25` shot's flat frame was **identical** to `still-a` (picture change 1.34, floor 0.81),
while the flat `head-check.ps1` on `mvp-new-game` passed.
**Cause:** `--loadlevel 10` (the default of the smoke test and of `eye-check.ps1`) plays the intro conversation:
`speechControlledCinematic()` places `g_playerCamera` itself **after** `updateFirstPersonCamera()`, so a head pose
applied there was overwritten every frame. In the headset that glues the world to the face during every
conversation, death and external view.
**Fixed:** the head is applied in `updateActiveCamera()` to whatever camera the frame renders with, and taken off
again (`restoreEngineCamera()`) before the engine updates its cameras next frame, so the engine's interpolations
never read it back. Yaw leg after the fix: picture change 4.25.
**Do:** a head-tracking check needs both a first-person save (`mvp-new-game`) and level 10 (a scripted camera);
"PASS in one" proves only that path.

## 2026-10-09 — The world is black in the headset, only the HUD shows: no level was loaded (#24)

**Symptom:** the owner's headset run of #215 (`test-pr.ps1 215`, `--skiplogo --loadslot 0`, the shared user dir)
showed a black world with the inventory bar, the health and mana orbs and the icons on top, in the headset and in
the desktop window. It looked like the new head-tracked camera was inside a wall or had broken the projection.
**Cause:** the owner's `Saved Games\Arx Libertatis\save` was **empty**. `--loadslot N` switches the game flow to
"in game" at once and loads slot N only if it exists; without it nothing was loaded and nothing was said — the
game ran with **no level at all**: the HUD is drawn, the world is not. The log shows it plainly: `game.state` never
leaves `STARTUP` (`game.state to=EXITED was=STARTUP`), no `level.loading` / `level.loaded`, and the frame loop's rate
line says `game 0.1 mean` ms (a level costs several ms). The #21 run minutes before had the same log — no level either.
**Fixed:** `arx.exe` now logs `[E] --loadslot N: no such save, M save(s) in the save game dir - starting at the main
menu instead of an empty level` and opens the main menu; `run-windows.ps1` (so `test-pr.ps1` too) refuses to start a
`--loadslot` the user dir has no save for, before the headset is involved, and names the test save to use.
**Do:** for a headset test without saves of your own, load a test save: `test-pr.ps1 <pr> -Saves mvp-new-game` (or
`run-windows.ps1 -Isolated -Save mvp-new-game`). Before suspecting rendering, check `game.state` / `level.loaded` in
the log or `state` / `area` in `status.json`: "black world" with `area` 0 is not a rendering bug.

## 2026-10-09 — "Blocked [rule]: ..." on a command that looks harmless (the safety hook)

**Symptom:** a Bash / PowerShell / Edit call is refused with `Blocked [push-main|force-push|delete-outside|
owner-write|owner-discard]: ...`, or a command is slow (~0.5 s) before it runs.
**Cause:** the `PreToolUse` safety hook (#185, [WORKFLOW.md](WORKFLOW.md#the-safety-hook-185-d-021)) denies the
five rules named in the message; the reason says what to do instead. It reads the command line, so a
command that *names* the owner's copy or a path outside the worktree is judged by what it writes or deletes
there. `cd <owner's copy>` followed by a write is `owner-write` even with relative paths. A call that
contains `push`, `reset`, `clean`, `checkout`, `rm`/`del`/`rd`, `cd` or `arx-fatalis-vr` takes the slow path
through PowerShell; that is normal. Hook errors never block (fail open) and are logged.
**Do:** follow the "instead" in the message. Read `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log` for the exact
command and rule. For a real false positive tell the coordinator (do not rewrite the command to dodge the
rule): the fix is a rule change plus a row in `scripts/dev/hooks/test-safety-guard.ps1`. A new session picks
up a changed `.claude/settings.json` only on start; a hook that "does nothing" in a session started from
a copy without the file (the owner's copy before the merge) is not loaded at all.

## 2026-10-09 — PowerShell rules code that silently matches nothing: int keys, doubles, Write-Host (#187)

**Symptom:** a lookup by issue number or wave finds nothing (a reason prints `wave 1 still has #` with the list empty), a
dictionary hands out the wrong entry, or a test harness that captures a script's output sees an empty string and "0 failing".
**Cause:** (1) `[ordered]@{}` is an `OrderedDictionary`: `$d[1]` with an **int** key is read as *position* 1, not key 1 —
use a plain `@{}` and sort afterwards. (2) `Measure-Object -Minimum` answers a **double**; `$hashtable[1.0]` does not find
the int key `1` — cast with `[int]`. (3) `Write-Host` goes to the information stream: `& script 2>&1` does not capture
it, `*>&1` does.
**Do:** cast numbers to `[int]` before using them as keys; no int keys in `[ordered]` tables; capture a script's whole output
with `*>&1`; and break the rule once on purpose to see the test fail (test-plan-next.ps1 was checked that way).

## 2026-10-09 — The CI game is another Windows account: its log, lock and build are not in your profile (#127)

**Symptom:** a CI game check failed (or passed) but `%LOCALAPPDATA%\ArxFatalisVR\userdirs\…\arx.log` and `launches.log`
show nothing about it; or a smoke test of yours and the CI game ran together although "one game at a time" holds.
**Cause:** the runner service is `NT AUTHORITY\NETWORK SERVICE` (session 0). Its `%LOCALAPPDATA%` is
`C:\Windows\ServiceProfiles\NetworkService\AppData\Local`, so CI has its own build, user directory, channel, game lock and
ledger. (Until #205 the game lock was per account too and did not span accounts; it now lives in `%ProgramData%\ArxFatalisVR\locks`,
shared by both. A working copy that has not merged `main` since still has the old launch-lib: its launches do not see a CI game
(they look in their own account only), while CI sees its `arx.exe` through WMI and waits - merge `main` into such a copy.) It also
has no Steam registry entry of the owner, hence `ARXVR_DATA_DIR`.
The game itself runs fine there: no desktop, window and OpenGL context are created anyway (measured: `GAMEPLAY` after ~1 s).
**Do:** read the evidence from the workflow artifact `game-checks` (logs, `status.json`, crash reports, digest), not from
your profile. Do not "fix" a CI failure by starting an interactive runner before the artifact says the window was the problem.

## 2026-10-09 — Backslashes vanish from Windows paths written through sed or a shell string (#186)

**Symptom:** a doc or script line reads `.scriptsdevpr-checks.ps1` instead of `.\scripts\dev\pr-checks.ps1`.
**Cause:** `sed 's#...#.\scripts\dev\x#'` in Git Bash, or any unquoted/double-quoted shell string, treats `\s`, `\d`, `\p`
as escapes and drops the backslash; the edit still "succeeds".
**Do:** edit files with the Edit/Write tools, not `sed` with Windows paths; after any scripted edit grep the changed
lines for the path (`git diff -U0 | grep pr-checks`) before committing.

## 2026-10-09 — "GraphQL limit" errors from commands that look like plain REST (#186)

**Symptom:** `API rate limit already exceeded` / `unknown owner type` from `gh issue view`, `gh pr view`, `gh pr list`,
`gh pr checks --watch`, `gh issue list` or a dev script, while `gh api repos/...` calls work and
`gh api rate_limit` shows `core` untouched; `gh api rate_limit` may even show thousands of `graphql` points left.
**Cause:** every high-level `gh` command except `gh api` with a `repos/...` path is GraphQL (`gh project ...` the
heaviest), and the GraphQL quota (hourly points plus a secondary, short-term limit) is shared by every session of the
account. A board query that fails is easy to read as "nothing there" (`next-task.ps1` said the queue was empty).
**Do:** use `gh api repos/Arziburst/arx-fatalis-vr/...` (REST) or the helpers in `scripts/dev/github-lib.ps1`
(`Get-GhIssue`, `Get-GhPull`, `Get-GhCommitChecks`, ...) and `scripts/dev/pr-checks.ps1 <pr> -Watch`; the board comes from
the 5-minute cache (`board-cache.json`) and a spent limit queues the move (`board-queue.json`, `set-status.ps1 -Flush`).
`ARXVR_FORCE_GRAPHQL_ERROR=1` makes every GraphQL call fail like a spent limit, to try the degraded paths. Also:
PowerShell 7 turns ISO date strings of `ConvertFrom-Json` into `DateTime` — the cache stores epoch seconds instead.

## 2026-10-09 — A reference project's manual and settings screen describe a feature its code no longer has

**Symptom:** research on OpenMW VR finds two accounts of the same facts. The manual says controllers "vibrate in
response to successful hits, and to being hit", and the game's VR page has a *haptics enabled* setting; it also says
the bindings screen "does NOT do any sanity checking". The code: the hit haptics are commented out (`TODO: Port to
lua`), the setting is read by nothing, no shipped script calls the haptics function; and the rebinding code refuses
a conflicting binding with a message. A config comment says the hand-offset sliders run "0 = -1 meters, 1 = 1
meters"; the code computes `(v - 0.5)` m. The `player height` setting is read into a scale that nothing applies.
**Cause:** the manual, the settings window and the config comments were written before a port or refactor (combat
code moving to Lua, the bindings screen becoming a script page) and not updated; a screen that lists a setting
proves nothing about what the code does with it.
**Do:** for every catalog row that rests on "the project does X", search the code for the *use* of the setting or the
call, not its declaration, and say which of manual, screen and code the row rests on; when they disagree the row
follows the code and names the disagreement (RESEARCH-reference-mods.md, F-44, F-167, F-171, F-174).

## 2026-10-09 — On `--loadlevel 10` the inventory never closes, `dodamage player` does nothing, and the game goes to the menu after a minute

**Symptom:** a smoke test on the default `--loadlevel 10` opens the inventory through the channel
(`ui inventory open`), but `ui inventory close` never produces `ui.inventory state=closed`;
`dodamage player 1000` leaves the player alive; a `shot` shows a letterboxed picture; and a run
longer than about a minute ends with `game.state to=MENU` at t=58 s although nobody touched the menu.
**Cause:** `--loadlevel 10` starts a **new game**, and level 10 is the scripted intro, not a playable
start: the script blocks the player's controls (`BLOCK_PLAYER_CONTROLS`, so damage to the player is
not applied) and shows cinema borders (`cinemascope`, so `renderInterface()` skips the HUD whose
update finishes the inventory's closing animation — `player.Interface` keeps `INTER_INVENTORY`). The
intro (`cinematic.skip` at t=29.55, NPC spells at t=52–56) ends in the main menu at t=58.4. Damage
from the player's own `dodamage` is also ignored: the source must be another entity.
**Do:** for anything that needs the player in control, send `setplayercontrols on` and
`cinemascope off` first, damage the player from an NPC (`human_base_0033.dodamage player 1000`), and
keep level 10 runs under 50 s — or start from a playable save (`-Saves`, the test save library #70).
The game event log (#130) shows all of it with `t=` times; read it before suspecting the feature.

## 2026-10-09 — The game crashes in `ScriptEventName::toString()` for a console command

**Symptom:** a new log line that names the script event of a failing command crashed the game
(`ACCESS_VIOLATION`, stack `Script.cpp:180` ← `ScriptEvent.cpp` ← `Console.cpp`) the moment a
console / channel command failed; the same line was fine for commands failing inside real scripts.
**Cause:** console lines run as the pseudo-event `SM_EXECUTELINE` (and jumps as `SM_DUMMY`), which
have no `"on <name>"` entry; `toString()` asserts on that (a no-op in Release) and takes
`substr(3)` of whatever came back. The engine itself only prints events through `operator<<`,
which special-cases both.
**Do:** never call `ScriptEventName::toString()` without handling `SM_EXECUTELINE` / `SM_DUMMY`
first (see `script.error` in `ScriptEvent.cpp`, #130), or print through `operator<<`.

## 2026-10-09 — A lever, a chest, a door cannot be found by coordinates (#70)

**Symptoms:** `setpos` next to `lever_0012` / `chest_metal_0052` and a screenshot shows a bare wall, a
black screen, the player's own feet, or a goblin; the entity is nowhere in the picture. About 6 of
the 13 launches of #70 went into it before this was written down.
**Causes, all measured:** (1) an entity's `pos` — from `entities` — is its **floor** position, so
looking at "the entity" from 2 m with pitch 0 aims 1.7 m above it; `lookat` now aims 40 units above the
floor and sets the pitch. (2) `setpos` / `player.pos` is the **eye**: `y` = floor − 170; a floor-level
`y` puts the eye in the floor (black or a wall's back) until gravity pulls the player up. (3) The
level is narrow corridors and cells: many points 1.5-2.5 m away on a ring around an object are
inside a wall or in another room (black or wall-only screenshots in the contact sheets of the 4
compass points around each of 7 objects); which ones are valid cannot be told without a picture. (4) The pick that decides what a click acts on is not exposed: `FlyingOverIO` is cleared in
mouse look (`ArxGame::render`: `PLAYER_MOUSELOOK_ON`), and `FlyingOverObject(screen centre)` read from
the channel found nothing from any of the 20 level markers within 700 units of the levers, chests and
portcullises either (3 hits for `note_0004` with `FlyingOverIO`, on the rings). Why is not known.
**Do:** place the player for **items and NPCs** by coordinates and a screenshot (what `mvp-items` and
`mvp-goblin-fight` do, `docs/vr/TESTING.md`); for a lever, chest or door the owner records the save by
hand in flat mode (`--loadlevel 1`, walk there, F5/save), or a task adds a way to see what the engine
picks (log `InterClick` at the centre with the distances it tests: `InterClick` needs the entity within
360 units of the **eye**, i.e. about 3.2 m along the floor, and its bounding box under the cursor). Do not
spend launches on ring or marker searches again.

## 2026-10-09 — The awakening of a new game blocks the controls for 30-40 s; a save made then is no use (#70)

**Symptom:** a save made right after `newgame` loads into a hero lying on the floor with the letterbox
on, the camera at the floor, and nothing responds (`player.y` 2837 instead of 2935).
**Cause:** the level 1 script plays the awakening; `BLOCK_PLAYER_CONTROLS` stays `true` (status.json
`player.controlsBlocked`) for 30-42 s (3 runs). It is not a `.cin`: the cinematic skip of #124 does
not apply, and no `Skipping cinematic` line is logged. A save keeps the blocked state.
**Do:** wait for `player.controlsBlocked` to be `false` before the first `save` (`@until
player.controlsBlocked False` in a scenario); never infer "playable" from `state: GAMEPLAY`.

## 2026-10-09 — `--loadslot N` is the Nth newest save, not a folder; and a console `equip` does nothing (#70)

**Symptoms:** (1) after another save was copied or made, `--loadslot 1` loads something else; (2)
`equip short_sword_0030` is acked `errors=0` and the sword is not equipped.
**Causes:** (1) the list of saves is sorted by the save file's time, **newest first**, at one-second
resolution (`SaveGameList::update`): a slot number is a position, and two saves made in the same second
swap. (2) `equip <target>` is the **item's** command (`ARX_EQUIPMENT_Equip(target, context entity)`):
typed for the player it asks to equip the player on the sword, which does nothing and says nothing.
**Do:** (1) to load a given save, make it the only one — `smoke-test.ps1 -Save <name>` /
`run-windows.ps1 -Isolated -Save <name>` do — and keep a second between saves of a scenario script;
(2) `short_sword_0030.equip player`, and check `entities short_sword_0030` says `show=6`.

## 2026-10-09 — Scripting a launch from PowerShell: three tools that quietly do the wrong thing

**Symptoms:** (1) `pwsh -File run-windows.ps1 ... -- --no-vr` fails with "parameter name '' is ambiguous";
(2) a script waiting for `status.json` to say `MENU` times out although the game just started — the
first thing it reads is `"state":"EXITED"` of the **previous** run; (3) a date written to a JSON store
with `ToString('o')` comes back from `ConvertFrom-Json` as a `DateTime` and `[DateTime]::Parse(...)` of it
throws or loses the precision.
**Cause:** (1) `pwsh -File` consumes the `--` itself; (2) `run-windows.ps1` does not clear the test
channel directory (the smoke test does); (3) PowerShell 7 turns ISO date strings into objects.
**Do:** (1) start such a launch with `pwsh -Command "& .\scripts\run-windows.ps1 ... -- --no-vr"`;
(2) delete `status.json` before launching and wait for a state *and* the new pid; (3) store epoch seconds
(`log-digest-lib.ps1` does), never date strings, in JSON read back with `ConvertFrom-Json`.

## 2026-10-09 — `vr.shownFrames` stays at 1 while `vr.frames` grows: the frame loop looks broken

**Symptom:** a VR self-test with nobody in the headset reports `frames=834 shown=1`; the log has
one `First VR frame shown` line and nothing after it, as if the layers stopped being submitted.
**Cause:** with an idle headset the session reaches `synchronized` but never `visible`, and SteamVR
sets `XrFrameState::shouldRender` only for the very first frame; the game then submits frames
without layers (as the spec asks) to keep the timing. The one shown frame proves that the whole
layer path (acquire, clear, copy, release, two layers in `xrEndFrame`) ran without an error.
**Do:** judge the frame loop by `vr.frames` (must grow) and by the absence of `[E]` lines, not by
`vr.shownFrames`; whether the picture is right is the simulated backend's (#112) or the owner's
question. A `shownFrames` that grows with an idle headset would mean the runtime changed its
behaviour, not that the game got better.

## 2026-10-09 — A press page says a closed-source mod is "MIT", or that a feature exists the code removed

**Symptom:** research on a reference mod finds two accounts of the same fact: MoguraVR's summary table
lists CircuitLord's Titanfall 2 VR as "MIT" and two pages say a player can copy their flat-game saves
into the VR profile, while the author's README says "the mods are closed source … all rights reserved"
and the installer's own test asserts that the "copy save" button does not exist.
**Cause:** summaries repeat each other and the tool's earlier versions; the installer's MIT licence is
read as the mod's, and a feature removed in v1.2.4 lives on in pages written before it. A licence
line or a feature list copied from a press page into the reference catalog would put a wrong
"copy this" or "this exists" into a decision.
**Do:** for a licence, read `LICENSE` and the README of the repository that ships the thing, and the
installer's third-party notices; for a feature of a closed mod, prefer the author's text and the
installer's code and tests over articles, and label every catalog row with its evidence (code, docs,
add-on, report). When primary and secondary sources disagree, say so in the row.

## 2026-10-09 — `next-task.ps1` says the MVP queue is empty; `set-status.ps1` fails to move a task

*Resolved for the dev scripts by #186: a board that cannot be read is the cache (with a warning) or an error, never
"empty"; `set-status.ps1` queues the move. The cause below stays true for any hand-typed `gh project ...`.*

**Symptom:** `.\scripts\dev\next-task.ps1` prints `NEXT none — the MVP queue is empty` although
`docs/vr/MVP.md` and the board are full of Todo tasks, and `.\scripts\dev\set-status.ps1` throws
`Failed to move #N to <status>`; both outputs start with `GraphQL: API rate limit exceeded for
user ID …`.
**Cause:** the board scripts read the project through `gh`'s GraphQL API, and that hourly quota is
shared by every session of the owner's account (the coordinator, every worker, `cycle-report.ps1`).
When it is spent the project query returns nothing and the scripts read "nothing" as "no tasks".
`set-status.ps1` may still have landed the move: its `#N -> <status>` line is printed before a
trailing query fails.
**Do:** read the reset time with `gh api rate_limit --jq '.resources.graphql'` and retry after it;
never conclude "queue empty" or "move failed" from an output that carries the rate-limit line; check
the board in the browser when in doubt.

## 2026-10-09 — A new PR gets no CI run: `gh pr checks` says "no checks reported" for minutes

**Symptom:** the PR is open, other PRs got their `Build (Windows)` run within a minute, but this one
has none after 6 minutes (`gh run list --branch <branch>` is empty); closing and reopening the PR
changes nothing, `gh workflow run build-windows.yml --ref <branch>` runs green but is not attached
to the PR as a check.
**Cause:** the PR **conflicts with `main`** (`gh pr view <pr> --json mergeStateStatus,mergeable` →
`DIRTY` / `CONFLICTING`). A `pull_request` workflow runs on the merge ref (`refs/pull/N/merge`), and
GitHub cannot create it for a conflicting PR, so no run is created at all — silently, nothing on
the PR page says why. Here two PRs merged into `main` while the task ran (#153, #154) and both
touched `TESTING.md` / `TRAPS.md` next to this task's lines.
**Do:** when a PR shows no checks, look at `mergeStateStatus` first. `git fetch origin`,
`git merge origin/main`, resolve, push — the run starts on the push. Check `mergeable` again before
asking why CI is quiet; `gh pr merge` would also refuse a `CONFLICTING` PR.

## 2026-10-09 — `set-status.ps1` fails with "API rate limit exceeded" although `gh api rate_limit` shows thousands left

*Resolved for the dev scripts by #186 (cached board, one mutation, queue, "secondary limit" recognised and waited for or
queued). Still true for hand-typed `gh project ...`.*

**Symptom:** `set-status.ps1 -Issue <n> -Status Backlog` prints `Issue #<n> is not on the board yet,
adding it` and then `Failed to move #<n>`; run by hand, `gh project item-list` / `field-list` answer
`GraphQL: API rate limit exceeded for user ID …`, while `gh api rate_limit` reports
`graphql.remaining` in the thousands. Repeats for a quarter of an hour, then works.
**Cause:** the Projects commands are heavy GraphQL queries (`item-list` with the whole board), and
several sessions of the same account poll the board (`cycle-report.ps1`, `next-task.ps1`, workers'
`set-status.ps1`); GitHub's secondary / points limit for the account trips before the hourly
counter does. `item-add` went through each time, so the issue **is** on the board — without a status.
**Do:** retry later (minutes, not seconds); do not add the item again by hand (`item-add` is
idempotent, but every retry is another query). Mention the un-set status in the report if the limit
does not lift before the task ends. A board script should treat this error as "try again later",
not as "not on the board".

## 2026-10-09 — A `spellcast` on the player from the console casts nothing

**Symptom:** `spellcast -fs 5 magic_sight self` through the test channel is acknowledged with
`errors=0`, no `[W]`, yet nothing happens: no spell sound, no effect, `camera.fov` in `status.json`
stays at 75.00 although Magic Sight should widen it; a second `spellcast` on the player does nothing
either. The #71 "26 spells, 0 of 240 frames differ" rows were accepted on exactly this command.
**Cause:** `TryToCastSpell` launches a script-cast spell at once only with `NOANIM` **and** `NODRAW`
(`-s` and `-m`). Otherwise it is queued in `spellcast_data` for the rune-drawing loop
(`RuneDraw.cpp`), which skips the player entity — the spell never starts, and the queued
`castingspell != SPELL_NONE` makes every later `spellcast` on the player return at the first line.
There is no error anywhere: the script command succeeded, the launch simply never comes.
**Do:** cast on the player with `-fsm` (`spellcast -fsm 5 <spell> self`) and prove the spell is active
with a number — `spells.launchedByType` / `spells.activeByType` in `status.json` (#155), or the
spell's own effect (`camera.fov` 80.65 for Magic Sight, life / mana, a `shot`) — never with the ack
alone. `scripts/dev/spell-drawcheck.ps1` re-ran the #71 spells check that way (#155).
**Even `-fsm` can launch nothing:** every spell has its own `CanLaunch`, and a refusal is silent too
(at most a fizzle sound). Found in #155: `fake_summon` refuses the player as caster, and the player's
`summon_creature` / `raise_dead` find no valid spot 3 m ahead anywhere tried on level 10. Cast those
from an NPC (`human_base_0033.spellcast -fsm 5 <spell> player`) — and check `launchedByType`.

## 2026-10-09 — A worktree build keeps the commit of its first build in the log and the window title

**Symptom:** `arx.log` says `Starting Arx Fatalis VR 0.1-dev + a7e4255` (so does `status.json`
`version` and the window title) although the worktree is several commits further; `built-from.txt`
next to `arx.exe` names the right commit.
**Cause:** `Version.cpp` is generated by a custom command whose dependencies are git's `HEAD`,
`logs/HEAD` and `packed-refs` — but in a worktree `.git` is a *file* (`gitdir: <path>`), those files
live under the main checkout's `.git/worktrees/<name>/`, and the rule depended on nothing that changes
with a commit. The owner's copy (a real `.git` directory) never showed it, every worker's copy did.
**Do:** fixed in #115 (`cmake/VersionString.cmake` resolves the gitfile and the common dir, so the
reflog is a dependency again). The banner's `[W] built-from.txt names commit … but the executable
carries version information from …` says when a build id lies: rebuild, and if it persists,
reconfigure (`build-windows.ps1 -Clean`).

## 2026-10-09 — `status.json` says `window.focused: true` for a window that never got the focus

**Symptom:** a self-test started without activation (`SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1`, #147)
reports `window.focused: true` in `status.json`, although the foreground window (user32
`GetForegroundWindow`) is still the terminal and the game never took the keyboard.
**Cause:** `SDL2Window::initialize()` called `onFocus(true)` unconditionally after `SDL_ShowWindow`
— an assumption from the days when a shown window was always the active one. The flag was never
read from SDL; `SDL_WINDOWEVENT_FOCUS_GAINED` fixes it only when the focus really changes later.
**Do:** fixed in #147 (`onFocus(SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS)`), so
`window.focused` is now evidence. In general: when `status.json` and an OS-level probe disagree,
suspect the engine's bookkeeping before the probe, and check where the value is *set*, not where it
is reported.

## 2026-10-09 — SteamVR starts, and the headset wakes up, from "just a test launch"

**Symptom:** the owner's Index and base stations turn on, SteamVR's window appears, although nobody
asked for VR — only a worker's test ran.
**Cause:** the OpenXR loader starts the active runtime (SteamVR) inside `xrCreateInstance`. The
engine's guard (#17) refuses when the runtime manifest names SteamVR and `vrserver.exe` is not
running, but anything that bypasses the guard starts SteamVR anyway: `xr_hello --start-runtime`,
another active runtime, a raw `arx.exe` of a build without the guard, and, worst, a worker starting
`vrstartup.exe` by hand to get VR evidence (it happened in #93 — 15 VR runs, someone closed SteamVR
two minutes later).
**Do:** launch only through the scripts: `run-windows.ps1` and `smoke-test.ps1` add `--no-vr` while
`vrserver.exe` is not running and say so; `smoke-test.ps1 -Vr` is skipped (exit 2). Never run
`vrstartup.exe`, never stop `vrmonitor` / `vrserver`; VR evidence without the owner's SteamVR is the
simulator's job (#112). Only the owner passes `-StartSteamVR` (D-014).
## 2026-10-09 — A `shot` taken right after `GAMEPLAY` is all black

**Symptom:** `smoke-test.ps1 -Commands 'shot level10'` with `--loadlevel 10` saves a 640×480 capture
in which every pixel is black (frame 15, 0.15 s after `state GAMEPLAY`), while `status.json` reports
`GAMEPLAY`, 0 errors and a normal frame rate — as if nothing were rendered.
**Cause:** the level starts behind a fade from black. A capture from frame 86 on (0.9 s) shows the
scene (58 % of sampled pixels non-black in the draw-check captures and in a `shot` at frame 517,
#71). The only code path that starts a fade is the `worldfade` script command (`fadeRequestStart`),
so this is the level script's own fade-in, not the renderer; the first `-Commands` line runs the
moment the state flips, i.e. inside that fade.
**Do:** take `shot` captures at least a second after `GAMEPLAY` — put a few `status` lines in front
of the `shot` (each ack takes ≈ 0.25 s) or wait first; judge "is anything rendered" by a non-black
pixel count over several frames, never by one early capture. An early black shot is no evidence
against the renderer. (The save verification of #70 waits 4 s after a `loadslot` before its `shot`;
the dark room of `mvp-items` stays dark at that time too — it is the level, not the fade.)

## 2026-10-09 — The obvious owner fix for #93 changed nothing: the sword was linked to a mesh the NPC no longer used

**Symptom:** the exit crash #93 reads a freed owner through `m_owner`; releasing the NPC weapon
(`_npcdata->weapon`) before the NPC is deleted looked like the fix, yet a detector in `~Entity` still
reported `human_base_0033` deleted while it owned `long_sword_0021` on every exit from level 10 —
an entity that is not the NPC's weapon, not in its inventory and, by `isEntityLinked()`, not linked.
**Cause:** the level 10 script links the sword to the NPC and then applies a full mesh tweak
(`HEAD|TORSO|LEGS`): `EERIE_MESH_TWEAK_Do()` swaps in a new mesh **without** the links and keeps the
old one as `tweaky` (restored by `TWEAK_REMOVE`). The sword keeps `show = LINKED` and its owner, but
the link exists only in `tweaky`, which `destroy()`, `UnlinkAllLinkedObjects()` and `isEntityLinked()`
never look at.
**Do:** do not trust a guess about *which* relation owns an entity — log what the owned entity looks
like (`show`, `scriptload`, linked / in inventory / weapon) and trace `setOwner()` before fixing. The
fix releases every entity whose `owner()` is the dying one, whatever the relation (#93).

## 2026-10-09 — `smoke-test.ps1 -Commands 'loadlevel N'` fails with "null-valued expression" and kills the game

**Symptom:** `=> FAIL You cannot call a method on a null-valued expression.`; the game disappears in
the middle of loading the new level, with no crash report — it looks like the level change crashed.
**Cause:** a test-tool bug, not the game: the channel's `loadlevel` runs synchronously, the level
change writes `status.json` with the new `cmdSeq` before `ack.txt` exists, `Send-ArxCommand` reads a
missing ack, throws, and the smoke test kills the game in its error handler (#142).
**Do:** until #142 is fixed, change levels with `-- --loadlevel N` (one launch per level), not with
the channel command.

## 2026-10-09 — A crash report folder is dated "yesterday" and the VR self-test crashes where the flat one is clean

**Symptom:** a run at 01:52 local time leaves `crashes\2026-10-08-22-52-43-00`; looking for a report
from "today" finds none, and an isolated user directory (`--user-dir`, #123) seems to have a
different crash than the shared one.
**Cause:** crash report folders are named in **UTC** (the `crash time` inside the report too); the
isolated directory simply has its own `crashes\` folder, so reports of a worker's runs are under
`%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>\crashes\`, not in `Saved Games\Arx Libertatis`. And the
crash that showed up only in VR (`smoke-test.ps1 -Vr`, 2 of 3 exits) is #93 again: the OpenXR session
changes the heap layout enough for the dangling `m_owner` read to fault without any cinematic loaded.
**Do:** match reports by the smoke test's `new crash report: <name>` line, not by the date; look in the
user directory the run used (the smoke test prints it). Treat a `-Vr` exit crash with the
`Entity::cleanReferences` ← `DanaeClearLevel` stack as #93, not as a VR bug — and remember `-Vr` is
not a gate until #93 is fixed.
*Resolved for the exit crash: #93 is fixed (no exit crash in 13 VR exits after the fix); `-Vr` is
still not a reliable gate because of #146 (the game exits with code -1 mid-run in 2 of 15 VR runs).*

## 2026-10-09 — The exit crash #93 needs no playing cinematic — a loaded one is enough

**Symptom:** closing the window after `--loadlevel 10` crashes in `Entity::cleanReferences`
(`DanaeClearLevel`) in 2 of 5 runs although no cinematic ever played and the log shows only
`Loading cinematic "…/introduction.cin"`; quitting through the channel is clean 5 of 5.
**Cause:** the level 10 script only *preloads* the intro (`cine -p introduction`); `cine play` comes
from the real new-game flow, never from `--loadlevel 10`. A loaded `.cin` (its bitmaps) is enough to
change the heap so that the dangling `m_owner` read of #93 faults; the window-close path is more
exposed than the channel quit. The first version of #124 skipped cinematics by loading them and
ending them at once — and brought the crash back.
**Do:** a skipped cinematic must never be loaded (#124 ends it through `cinematicEnd()` without
loading the file). Test exits with `-CloseWindow` as well as with the channel quit: they fail
differently. `--intro` / `--cinematics` remain exposed to #93 until it is fixed.
*Resolved: #93 is fixed — the loaded intro with `-CloseWindow` was 10 of 10 clean after the fix. The
cinematic only changed the heap layout; the dangling owner was there on every exit.*

## 2026-10-09 — An issue body edited from PowerShell lost all its line breaks

**Symptom:** after `gh issue edit --body-file`, the issue body is one long line, and the `-replace`
that should have inserted a section into it did nothing.
**Cause:** PowerShell captures `gh issue view --json body -q .body` as an **array of lines**, not a
string: a multi-line regex never matches, `-eq` between two arrays is not a "did it change" check,
and `Set-Content -NoNewline` with an array joins the lines **without** separators.
**Do:** write issue bodies from a here-string or with `[IO.File]::WriteAllText`; to get a body as one
string use `gh … | Out-String` (or `-join "`n"`); read the issue back and check its line count before
moving on. (Hit on #124, 2026-10-09; the body was restored from the original text.)

## 2026-10-08 — Closing the game 30 s after `--loadlevel 10` crashes 4 times out of 5

**Symptom:** a scripted flat launch (`--no-vr --loadlevel 10`, close the window after 30 s) ends with
"Illegal storage access" (`Entity::cleanReferences` ← `DanaeClearLevel` ← `ArxGame::shutdownGame`)
in 4 of 5 runs, and the crash dialog stays on the screen.
**Cause:** `--loadlevel 10` starts a new game, the level 10 script plays the intro cinematic, and
closing during the cinematic is bug #93 (an entity owner is destroyed before its dependants).
**Do:** development launches now skip the intro cinematic (`--intro` plays it; #92 restores it for
players), so `--loadlevel 10` lands in GAMEPLAY after ~1 s and closing is clean (10/10 with the
channel's `quit`, measured in #99). #93 itself is still open for the `--intro` / player path.
*Resolved: #93 is fixed (an entity linked to a swapped-out tweak mesh kept a dangling owner).*

## 2026-10-08 — `arx.log` is empty or stale while the game runs

**Symptom:** a test that reads `arx.log` while the game is running sees nothing of the current run
(or only the first few KB).
**Cause:** the file logger writes through an `ofstream` that is never flushed until shutdown; a crash
closes it (so a crashed run *does* leave a log, without `Clean shutdown`).
**Do:** start the game with `--test-dir <dir>`: the test channel flushes the log every 250 ms together
with `status.json`, and `[game] state:` / `[mark] <text>` lines give a script fixed anchors. Without
the channel, read the log only after the process exited.
*Resolved: since #129 the file logger flushes a warning or error at once and everything else at
least once a second from its own flush thread (so a stuck main thread does not stop it). A
`Stop-Process` 15 s into a flat run without the channel left a complete log ending with `Done
loading level`, where the old logger left 4137 bytes cut mid-line. The channel's 250 ms flush stays
for tests that want the log fresher than a second.*

## 2026-10-08 — The crash dialog outlives the game and opens Explorer when dismissed

**Symptom:** after a crash a "Fatal Error - Arx Fatalis VR" window stays open although `arx.exe` has
exited with code 1, and closing it opens the crash report folder in Explorer.
**Cause:** the crash processor is a second `arx.exe --crashinfo=…` that shows a message box and then
*always* launches the report directory; without `arxcrashreporter` nothing closes it for a script.
**Do:** automated runs set `ARX_UNATTENDED=1` (the test channel does it for the game, `smoke-test.ps1`
for the process): the report is still written, the dialog is skipped. A leftover dialog is best
stopped with `Stop-Process` (closing it would open Explorer).

## 2026-10-08 — A rebuild fails with `LNK1104: cannot open file 'arx.exe'`

**Symptom:** the build fails at the final link although nothing in the code is wrong.
**Cause:** the game from that build directory is still running (a smoke test, the owner's window, a
forgotten process) and Windows keeps the executable locked. A CMake reconfigure can relink `arx.exe`
even when no source changed.
**Do:** `Get-Process arx` before building; wait for the smoke test to finish (it refuses to start
while another `arx.exe` runs, for the same reason).

## 2026-10-08 — `@(... | Measure-Object).Count` is always 1

**Symptom:** a PowerShell probe reported one `[E]` line in every log, including logs without any.
**Cause:** `Measure-Object` returns a single object; wrapping *that* in `@()` and taking `.Count`
counts the wrapper, not the matches.
**Do:** `@(Select-String …).Count` or `(… | Measure-Object).Count` — and verify a check can fail
(a log with no errors must give 0) before trusting it.

## 2026-10-08 — A board script "succeeded" but changed the wrong issues

**Symptom:** a PowerShell script printed "retitled #36" / "noted #18" for every issue, but titles were
unchanged and comments were empty or belonged to another issue.
**Cause:** `$ordered[36]` on an `[ordered]` dictionary indexes by **position**, not by key, when the
index is an integer (out of range → `$null`, in range → some other entry). Native commands (`gh`)
failing with a non-zero exit code do not stop a script even with `$ErrorActionPreference = 'Stop'`.
**Do:** iterate `[ordered]` dictionaries with `.GetEnumerator()` (`$_.Key` / `$_.Value`) or use a plain
hashtable for lookups; check `$LASTEXITCODE` after every `gh` call; verify the result afterwards
(read the issues back) instead of trusting the script's own output.

## 2026-10-08 — CI downloads hang or time out

**Symptom:** a CI run sits in "vcpkg install" for half an hour or fails with
`error: Download timed out` from `github.com`.
**Cause:** the network to GitHub from the owner's PC is flaky at times, and a vcpkg without a cache
downloads every port source again.
**Do:** CI and local builds share `C:\vcpkg-shared` (vcpkg checkout, downloads, binary cache — see
WORKFLOW.md); a one-off timeout is re-run once (`gh run rerun <id>`), not debugged.

## 2026-10-08 — A freshly built `arx.exe` does not start

**Symptom:** a build that ran minutes ago is suddenly blocked, or the game "crashes" before writing
anything.
**Cause:** Windows **Smart App Control** is enforced on the owner's PC
(`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState = 1`) and blocks
unsigned files by reputation ("An Application Control policy has blocked this file").
**Do:** before debugging the engine, check
`Get-WinEvent -LogName Microsoft-Windows-CodeIntegrity/Operational` for event 3077 naming `arx.exe`.
Report it to the owner; never try to bypass it (turning Smart App Control off is irreversible and the
owner's call). Relevant for tester builds too (#88): unsigned builds may be blocked or warned about.

## 2026-10-08 — `arx.log` shows an old run

**Symptom:** after a launch the log looks unchanged, as if the fix did nothing.
**Cause:** `%USERPROFILE%\Saved Games\Arx Libertatis\arx.log` is written only on a **clean
shutdown**; a launch that failed (or was killed) leaves the previous run's log in place.
**Do:** compare the log's `LastWriteTime` with the launch time (or delete it first), and close the game
with `Process.CloseMainWindow()`, never `Stop-Process`.
*Resolved for the log: since #129 the previous run's log is rotated to `arx.1.log` at startup and the
current run's lines reach the disk within a second, so a killed run leaves its own log (the `Run:`
banner line names the pid and the start time; the smoke test no longer deletes `arx.log` before a run,
the history would go with it). `Stop-Process` still skips `Clean shutdown` and the channel's final
status: close the game properly whenever those are the evidence you need.*

## 2026-10-08 — `--data-dir` with the Steam path breaks the command line

**Symptom:** `arx.exe` prints `[CRITICAL] CommandLine:68 Error parsing command-line argument` to
stdout and exits with code 1 without a log.
**Cause:** `Start-Process -ArgumentList` does not quote arguments; `...\common\Arx Fatalis` has a space.
**Do:** quote it (`"--data-dir", "`"$dir`""`) and capture `-RedirectStandardOutput` /
`-RedirectStandardError`; `scripts/run-windows.ps1` already does the right thing.

## 2026-10-08 — A worktree session's branch has none of our work

**Symptom:** a task branch has no `docs/vr/`, no VR code; its PR cannot be merged into `main`.
**Cause:** `EnterWorktree` bases new branches on `origin/master` — the untouched upstream mirror —
because git sees `master` as the default branch, while our development branch is `main`.
**Do:** right after entering the worktree: `git branch -m ARXVR-<n>-<type>-<name>`,
`git reset --hard origin/main`, verify with `git merge-base HEAD origin/main`. The worktree guard
also refuses `git -C <owner copy>` and commands with runtime-computed paths: use literal paths.

## 2026-10-08 — The build pulls in Visual Studio's own vcpkg

**Symptom:** dependencies are rebuilt from source although they were built before.
**Cause:** `VsDevCmd.bat` sets `VCPKG_ROOT` to the vcpkg bundled with Visual Studio.
**Do:** `scripts/build-windows.ps1` reads `VCPKG_ROOT` before loading the MSVC environment — keep it that way.

## 2026-10-08 — CMake refuses a build directory

**Symptom:** `CMake Error: The source "…" does not match the source "…" used to generate cache`.
**Cause:** one build directory was configured from another working copy (the main clone vs. a worktree).
**Do:** build directories are per working copy (`scripts/build-paths.ps1`); do not share them by hand.

## 2026-10-08 — `rc.exe` fails with RC1203

**Symptom:** the build stops at `arx-version.rc` with `RC1203: invalid language ID`.
**Cause:** C/C++-only MSVC flags added with `add_definitions()` reach the resource compiler.
**Do:** add compiler-only flags with `add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:...>)` (fixed in #12).

## 2026-10-09 — `claude --bg` refuses a worker's worktree: "Workspace not trusted"

**Symptom:** `start-worker.ps1` fails with `Workspace not trusted. Run claude in <worktree> once and accept
the trust prompt`, even after the owner trusted the parent folder `C:\Users\arzib\arx-fatalis-vr-wt`.
**Cause:** Claude Code's workspace trust is checked for the launch folder itself; trusting a parent does
not cover a new subfolder. Every worktree is a new folder.
**Do:** launch workers in the owner's copy (trusted) and let them enter the worktree first —
`start-worker.ps1` passes `/next-task <n> worktree=<path>` (D-011). Never write trust flags into
`~/.claude.json` by hand: the owner gives trust.

## 2026-10-09 — PowerShell: `[math]::Max(0, 4.5)` is 4, and `ConvertFrom-Json` turns date strings into DateTime

**Symptom:** a rate computed with `[math]::Max(0, $double)` is rounded down to an integer (a worker count came out one too
high); `[datetime]::Parse($obj.updated)` on a JSON field gave a date a month off.
**Cause:** with an `int` literal first, PowerShell picks `Max(int,int)` and converts the double; PowerShell 7's
`ConvertFrom-Json` already returns ISO-8601 strings as `DateTime`, and parsing its culture-formatted string swaps day and month.
**Do:** write `[math]::Max(0.0, $x)`; take a field that may be a `DateTime` or a string through a helper
(`ConvertTo-Epoch` in `scripts/dev/usage-lib.ps1`).

## 2026-10-09 - A deliberate crash check files a bug issue

**Symptom:** the `crash` / `crash steal` channel check (#132) opened an `auto-reported` issue (#235).
**Cause:** the run digest files every new crash signature (#131).
**Do:** the digest skips filing for a run whose log has `[crash] deliberate crash requested`; if another deliberate fault is added, log the same marker line first.

## 2026-10-09 — .xrs: a timeout without its `s` was part of the value; `$g` and `$G` are one variable

**Symptom:** `@until state eq GAMEPLAY 60` waited 30 s and failed with "never became eq GAMEPLAY 60" while the game was in GAMEPLAY;
a PowerShell test helper with parameter `$G` and a local `$g` failed with "cannot convert Graphics to Int32".
**Cause:** the runner took everything after the operator as the value; PowerShell variable names are case-insensitive, so `$g` and `$G` are one variable.
**Do:** write timeouts as `60s` (the parser now rejects a value with a trailing bare number); never name two variables in one scope by letters that differ only in case.

## 2026-10-09 — VR: picking finds nothing although the item is in the middle of the view

**Symptom:** in a stereo frame (`--vr-sim` or the headset) the mouse hovers nothing — no highlight, `pick` finds nothing on
the whole window — while the item is plainly on screen; the item's `bbox2D` is in the thousands (`597 1066 657 1113` in a
640x480 window).
**Cause:** the scene is drawn once per eye, and drawing an entity stores its screen data (clip positions, items'
`bbox2D`) for the camera of the draw — the last eye, at the eye image's size. Mouse picking reads that data next frame
against window coordinates (#72).
**Do:** anything that keeps screen positions from a draw must be projected again with the head camera after the eyes
(`ProjectEntitiesForPicking()` after `renderStereoViews()`); check with `pick <entity>` (`targetSeenInBox`).

## 2026-10-09 — `specialfx newspell` logs `[W] unexpected newline`

**Symptom:** the script command `specialfx newspell` (the book icon's FX) works, but adds a `[W] ScriptUtils … [player:N] :
unexpected newline`, which the run digest reports as a new warning.
**Cause:** the command skips one word after `newspell`; with nothing after it, it reads the end of the line.
**Do:** send `specialfx newspell 1`.

## 2026-10-09 - Send-ArxCommand returned the previous batch's ack

**Symptom:** `test-xrs.ps1` failed only on the slow CI runner: the ack of a check's first command read `last=refuse this`, the command of the check before (#250).
**Cause:** the game writes `ack.txt` first and `status.json` later; `Send-ArxCommand` took its baseline `cmdSeq` from `status.json` and waited for it to advance, so the late status update of the *previous* batch ended the wait and the old ack was returned.
**Do:** the baseline is the newest seq of ack.txt and status.json, and the answer is `ack.txt` itself advancing (`Read-ArxAck` in `scripts/dev/test-lib.ps1`). After a send, `status.json` may still lag behind the ack - wait for the state you assert on (`@until`, `@frames`).

## 2026-10-09 — The stick walk is applied on level 10, but the player does not move

**Symptom:** `walk-forward.xrs` on `--loadlevel 10` with `setplayercontrols on`: `vr.walk state=start` / `end` are logged
and `vr.input.walk.walking` is true, yet `player.x` / `player.z` stay exactly the same.
**Cause:** level 10 is the scripted intro (see "On `--loadlevel 10` the inventory never closes" above): the movement
the controls ask for is not applied to the player there - not a locomotion bug.
**Do:** check walking on a playable save: `smoke-test.ps1 -VrSim idle -Save mvp-new-game -Sequence walk-forward` (#31).

## 2026-10-10 - Crouched in the start cell, the character never stood up again
**Symptom:** on the test save `mvp-new-game` the character crouched (button, toggle or the head, #100) and never stood up: the toggle went off (`player.crouch state=off`), `PLAYER_CROUCH` stayed set for good; on `mvp-items` the same sequence stood up (#263).
**Cause:** the engine's own stand-up check, not VR code - flat Arx Libertatis did the same. `ARX_PLAYER_Manage_Visual` checked the **whole** standing cylinder with `CheckAnythingInCylinder`, and at the awakening spot that cylinder grazes a sloped floor polygon 21 units above the feet (`background=-21.0 ... normY=-0.3`). Standing there is fine - the check runs only to stand up - but the crouched character could never pass it.
**Do:** the stand-up (and the jump from a crouch) now checks only the part of the standing cylinder above the crouched one (`standUpCylinder()` in `src/game/Player.cpp`). A refused stand-up logs one `player.standup.blocked` event with what is in the way (`describeCylinderBlocker`); read it before blaming crouch code. `stand-up.xrs` checks both directions.
