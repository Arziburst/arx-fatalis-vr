# Research: Controllers — Valve Index and Meta Quest 3

- **Date:** 2026-10-08
- **Question:** What do we need to support the Valve Index controllers (primary — the owner's) and the
  Meta Quest 3 Touch Plus controllers (secondary), with full control remapping?
- **Context:** OpenXR with SteamVR as the runtime (D-001). Quest 3 reaches SteamVR via Steam Link
  (or Quest Link with SteamVR set as the OpenXR runtime).

## How OpenXR input works (what it means for us)

- The game never reads buttons directly. It declares **actions** (e.g. `move`, `turn`, `interact`,
  `grab`, `menu`) in an action set, and **suggests bindings** of those actions to input paths for each
  **interaction profile** (controller type) it knows.
- The runtime picks the profile matching the connected controller and delivers action states.
  The game finds out which profile is active via `xrGetCurrentInteractionProfile` /
  `XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED`.
- **Remapping is free for players:** SteamVR lets players rebind any OpenXR app in
  *Settings → Controller Bindings*. SteamVR picks a binding in this order: the player's own binding,
  then one shipped with the app, then one set on the Steamworks partner site, then the driver default,
  then compatibility / automatic rebinding. So "full remapping" means: **define clean, well-named
  actions** — players can rebind them in SteamVR without any in-game UI.
- For controllers we do not suggest bindings for, SteamVR 1.26+ auto-generates a binding by emulating
  a common controller.
- SteamVR activates interaction profiles only after the session has been focused for a few frames —
  do not treat "no profile yet" at startup as an error.

## Valve Index controllers (primary)

Profile: `/interaction_profiles/valve/index_controller` — both hands identical.

| Component | Paths (`/user/hand/{left,right}/…`) | Notes |
|---|---|---|
| A button | `input/a/click`, `input/a/touch` | Lower face button |
| B button | `input/b/click`, `input/b/touch` | Upper face button |
| Trigger | `input/trigger/value`, `input/trigger/click`, `input/trigger/touch` | |
| Thumbstick | `input/thumbstick` (x/y), `input/thumbstick/click`, `input/thumbstick/touch` | |
| Trackpad | `input/trackpad` (x/y), `input/trackpad/force`, `input/trackpad/touch` | Index only — not on Quest |
| Grip | `input/squeeze/value`, `input/squeeze/force` | Bind grab to **`squeeze/force`** with a threshold around 0.5: `squeeze/value` fires just from holding the controller (Blender hit this) |
| System | `input/system/click` | Reserved for SteamVR dashboard — do not bind |
| Poses | `input/grip/pose`, `input/aim/pose` | Grip pose for hands/weapons, aim pose for rays |
| Haptics | `output/haptic` | |

**Finger tracking:** SteamVR exposes `XR_EXT_hand_tracking` and Index controllers are reported to
work with it (finger curl from the capacitive sensors), plus `XR_EXT_hand_joints_motion_range`.
Developer reports are mixed, so treat it as post-MVP polish (epic #5), not a dependency.

*Path list compiled from the OpenXR interaction-profile definitions as recalled and the sources below;
verify against the OpenXR specification appendix (or by enumerating at runtime) when implementing #35.*

## Meta Quest 3 Touch Plus controllers (secondary)

Through SteamVR the Touch Plus controllers appear as **`/interaction_profiles/oculus/touch_controller`**.
Meta's own profile `/interaction_profiles/meta/touch_controller_plus` (extension
`XR_META_touch_controller_plus`) is **not** implemented by SteamVR; it would only matter on Meta's
runtime, which we do not target. Suggest bindings for `oculus/touch_controller`.

| Component | Left hand | Right hand | Notes |
|---|---|---|---|
| Face buttons | `input/x/click`, `input/x/touch`, `input/y/click`, `input/y/touch` | `input/a/click`, `input/a/touch`, `input/b/click`, `input/b/touch` | X/Y on the left, A/B on the right |
| Menu | `input/menu/click` | — | Right "system" (Meta) button is reserved |
| Trigger | `input/trigger/value`, `input/trigger/touch` | same | |
| Grip | `input/squeeze/value` | same | No force sensor — threshold on `value` |
| Thumbstick | `input/thumbstick` (x/y), `…/click`, `…/touch` | same | |
| Thumb rest | `input/thumbrest/touch` | same | |
| Poses | `input/grip/pose`, `input/aim/pose` | same | |
| Haptics | `output/haptic` | same | |

Known issues reported by the community: occasional inverted stick rotation in some games (fixed by a
custom SteamVR binding); Steam Link and the Meta Quest Link app conflict if both run.

## Design rules that follow

1. **Core actions use only components both controllers have:** thumbsticks, trigger, grip, two face
   buttons per hand, poses, haptics. Index trackpad and Quest thumb rest are optional extras.
2. **One menu action, different buttons:** Quest has a dedicated left `menu` button; Index has none,
   so the menu goes on a face button (e.g. left B) for Index.
3. **Grab:** Index → `squeeze/force` with threshold; Quest → `squeeze/value` with threshold.
4. **Actions are named for what they do in the game** (`move`, `turn`, `jump`, `crouch`,
   `interact`, `grab`, `cast`, `inventory`, `book`, `menu`, `recenter`, `hand_pose`, `aim_pose`,
   `haptic`) so they read well in the SteamVR rebinding UI, with localized names.
5. **Also suggest `/interaction_profiles/khr/simple_controller`** as a minimal fallback; everything
   else is covered by SteamVR's automatic rebinding.
6. **Left-handed players** are served by SteamVR rebinding for the MVP; an in-game "swap hands"
   option is post-MVP.

## Proposed default layout (to be finalized in #30 / #35)

> **Superseded by D-019 / D-020 (2026-10-09), implemented in #30:** the OpenXR actions are one per
> physical control (`src/vr/VRActions.cpp`, with the Index, Touch and simple-controller suggestions),
> and the game's own control table maps them to gameplay events — single control, ordered chord or
> long press, remappable in the game (`src/vr/VRControls.h`, `defaultLayout()` holds the D-019
> table). Design rules 4 and 6 above are replaced by that; the table below is the original proposal.

| Action | Index | Quest 3 |
|---|---|---|
| Move | Left thumbstick | Left thumbstick |
| Turn (snap/smooth) | Right thumbstick X | Right thumbstick X |
| Jump / crouch | Right thumbstick up / down (or A / B) | Right thumbstick up / down (or A / B) |
| Interact / attack (ACTION) | Right trigger | Right trigger |
| Grab / use (USE) | Right grip (force) | Right grip |
| Magic mode (hold) + draw | Left grip + right trigger | Left grip + right trigger |
| Inventory panel | Right B | Right B |
| Book / map | Left A | X |
| Menu | Left B | Menu (left) |
| Recenter | Long-press left thumbstick click | Long-press left thumbstick click |

## Impact on the plan

- **#30 (OpenXR action set)** — define the actions above, named for gameplay.
- **#35 (suggested bindings)** — now covers **both** `valve/index_controller` and
  `oculus/touch_controller`, plus `khr/simple_controller`.
- **#87: verify the controls on Quest 3 via Steam Link** — needs a Quest 3 for testing
  (Backlog until one is available for testing).

## Sources

- [OpenXR input in Unreal Engine docs](https://docs.unrealengine.com/4.26/en-US/SharingAndReleasing/XRDevelopment/OpenXR/input_with_openxr)
- [Blender: unintended navigation with Valve Index (squeeze/value vs squeeze/force)](https://developer.blender.org/T93509)
- [Valve Index controllers](https://www.valvesoftware.com/en/index/controllers)
- [Meta: Touch Plus controllers (native PC)](https://developers.meta.com/horizon/documentation/native/pc/native-touch-plus-controllers/)
- [Unity: Meta Quest Touch Plus controller profile](https://docs.unity3d.com/Packages/com.unity.xr.openxr@1.16/manual/features/metaquesttouchpluscontrollerprofile.html)
- [Khronos forum: Quest 3 controllers with SteamVR](https://community.khronos.org/t/quest-3-controllers-with-steamvr/111048)
- [Valve: Driver API documentation (binding selection order)](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md)
- [UploadVR: SteamVR automatic rebinding for less used controllers](https://www.uploadvr.com/steamvr-automatic-controller-rebinding-update/)
- [Khronos forum: xrGetSystem with Quest 3 and Steam Link](https://community.khronos.org/t/xrgetsystem-fails-for-quest-3-with-steamlink/110409)
- [Meta community: Quest 3 controllers rotate inverted on Steam](https://communityforums.atmeta.com/discussions/GamesApps/quest-3-controllers-rotate-inverted-on-steam/1347783)
- [Steam Community: SteamVR OpenXR feature list (XR_EXT_hand_tracking)](https://steamcommunity.com/app/250820/discussions/8/3121550424355682585/)
- [Unity discussions: Index finger tracking with OpenXR](https://discussions.unity.com/t/seeking-assistance-on-integrating-valve-index-controllers-with-unity-openxr-for-finger-tracking/949483)
