# Research: Reference VR Mods — Dishonored VR, VHVR (Valheim VR) and Titanfall 2 VR

- **Date:** 2026-10-08
- **Question:** Two released VR mods are close to what we build. What do they do that we can use — from
  testing without a headset to combat, hands, UI and process — and how do we adopt each piece:
  copy it so it works as theirs, copy and adapt one dependency, port the logic function by
  function, or only reimplement from the description?
- **Basis:** a read-through of both repositories as of 2026-10-08 — Dishonored VR at commit
  `b15e4d32a` (branch `VR-Main`, release v1.0.4) and VHVR at commit `d4d7b9523` (branch `master`,
  release v0.11.0) — plus a web check of the alternatives for headless OpenXR testing (Monado, the
  SteamVR null driver, the Meta XR Simulator). File links point at the branches; line numbers were
  taken at the commits above and may drift.
- **Output:** the [feature catalog](#feature-catalog) below. **The project owner decides from the
  catalog what we build** (the *Decision* column is theirs); the coordinator then creates the tasks and
  orders the queue. This document creates no tasks.
- **Continued (#118, 2026-10-09):** a third project, TinyBlackDog's **Titanfall 2 VR**, read at
  commit `8c50a7d` (branch `main`, release v0.1.1, the repository's only commit). Its summary is
  below, its licence is in the table, its rows are **F-111 to F-134**, its notes are in
  [section 11](#11-titanfall-2-vr-118), and the `TF2VR` sources appended to older rows mark what
  it adds to them.
- **Continued (#120, 2026-10-09):** a fourth project, CircuitLord's **Titanfall 2 VR** (released
  2026-09-30), whose mod is **closed source** — researched by observation: the MIT installer read
  in full at commit `b493821a6` (branch `main`, v1.2.9), its manifest and release notes, the
  community add-ons, and the press and guides. Its summary is below, its licence line is in the
  table, its rows are **F-135 to F-152**, its notes are in
  [section 12](#12-circuitlords-titanfall-2-vr-120), and the `CLVR` sources appended to older rows
  mark what it adds to them. Do not confuse the two Titanfall 2 projects: `TF2VR` is
  TinyBlackDog's (#118), `CLVR` is CircuitLord's.

## The projects in short

### Dishonored VR

[VR-Stereo-Hub/Dishonored-VR](https://github.com/VR-Stereo-Hub/Dishonored-VR) — C++, 32-bit, a
`d3d9.dll` proxy for Dishonored (2012, Unreal Engine 3, Direct3D 9). It hooks the engine at fixed
addresses, captures the game's frame into its own Direct3D 11 device and submits it to an OpenXR
runtime; the scene is drawn twice per tick (once per eye) by calling the engine's viewport draw a
second time with the other eye's camera. Head tracking is written into the engine's view-rotation
event; the VR controllers act as an XInput gamepad with motion controls on top (motion sword,
hand-aimed powers, physical crouch, wrist HUD). Runtimes: Virtual Desktop's VDXR (Quest), any
native 32-bit OpenXR runtime, or a bundled OpenXR-on-OpenVR shim for SteamVR (SteamVR has no
32-bit OpenXR). Built largely by AI agents (Claude Code, Codex) driven by two maintainers; the
`docs/` folder (about 4 MB) records every architecture decision, every verification and every
trap, and `tools/` holds about 90 host-test suites, a simulated OpenXR runtime and the scripted
test harness. Released on GitHub with a native launcher/installer; v1.0.4 at the time of reading.

What it is **not**: it does not render in-engine (the frame is a readback of the game's window),
so its rendering and performance story — readbacks, the D3D9Ex device, draw re-entry, DLSS in a
64-bit helper — is mostly irrelevant to us; we render the scene ourselves in OpenGL.

### VHVR — Valheim VR

[brandonmousseau/vhvr-mod](https://github.com/brandonmousseau/vhvr-mod) — C# (BepInEx + Harmony
patches, Unity 2019), SteamVR/OpenVR through Unity XR. It swaps Valheim's camera for a SteamVR
player rig, puts the game's Unity UI on world-space panels, and replaces the combat with
physics-driven motion controls: weapon colliders on the controllers with a speed estimator,
blocking and parry by holding the weapon or shield in the attack's path, a two-handed bow with
string drawing, crossbows, spears, throwing, fists, magic staffs, gesture locomotion, full-body
tracking with VRIK, bHaptics. Mature (since 2021, v0.11.0, 193 stars), the project owner plays it.
No automated tests and no CI in the repository; releases are packaged by a `.cmd` script and
published on Nexus/Thunderstore.

### Titanfall 2 VR (TinyBlackDog)

[kyus2001/titanfall2vr](https://github.com/kyus2001/titanfall2vr) — C++20, 64-bit, a **Northstar
client plugin** (one DLL and one ini) for Titanfall 2's single-player campaign. It hooks the game's
Direct3D 11 renderer and camera through byte-verified detours and vtable slot swaps, drives them
from an OpenXR session (`XR_KHR_D3D11_enable`, API 1.0 requested on purpose, the loader fetched by
CMake at `release-1.1.62`), and presents the controllers to the game as an XInput gamepad at the
game's own input seam. Stereo is **alternate-frame**: one eye per game frame, each eye submitted
with the pose it was rendered with; same-frame stereo by re-entering the scene draw is compiled in
and off because it hangs the engine after about 129 doubled frames. On top: 6DoF head tracking
with a neck model, an eye height applied at the engine's own eye getter, the weapon on the right
controller (grip pose for position, aim pose for direction) with live grip calibration, ADS
behaviours, snap and smooth turn with a response curve and axis dominance, haptics from the game's
own rumble, a 72-setting ImGui panel on a world-fixed quad driven by the sticks, and per-element
HUD placement. Alpha v0.1.1 (2026-09-12), developed and tested by one person on a Meta Quest 3 and
a Play For Dream MR through Virtual Desktop's VDXR; other runtimes are "untested rather than
unsupported" — the suggested bindings cover only `oculus/touch_controller` and
`khr/simple_controller`, so a Valve Index on SteamVR would depend on the runtime's own remapping.
Written almost entirely by an AI agent with the author as "the wearer": the comments carry dated
measurements from 2026-08-14 to 2026-09-11, about 75,000 lines of C++ in four weeks, and the git
history is one squashed commit. Roughly a third of the tree is instrumentation — a crash recorder,
a stall watchdog, a frame-spike window, censuses and probes — and the comments are the project's
memory, Dishonored VR's `docs/` folder written inline.

What it is **not**: like Dishonored VR it does not render in-engine (the frame is the game's
backbuffer copied into a swapchain), so its render-target censuses, scene re-entry rungs,
temporal-history levers and D3D11 pacing-thread designs answer a problem we do not have; and its
controller model — a synthetic XInput pad, engine key latches, a patched `CreateMove` — is specific
to a Source-derived engine with no input API. What carries over is the OpenXR session code, the
input and comfort maths, the hand composition rules, the settings and diagnostics design, and a
list of traps paid for in headset runs.

### Titanfall 2 VR (CircuitLord)

[CircuitLord/CircuitLordVRModInstaller](https://github.com/CircuitLord/CircuitLordVRModInstaller) —
the only open part of a full-conversion mod for the Titanfall 2 **campaign**, released free on
2026-09-30 by a solo developer (per MoguraVR the founder of a VR studio) and covered by UploadVR,
Road to VR and others. The mod itself is a native OpenXR plugin (`Titanfall2VR.dll`) loaded by
**Northstar** (the community mod loader, pinned to v1.31.13) in a separate `TF2VR` profile, with
Northstar script mods (`.gnut` files, e.g. `Titanfall2VR.Cockpit`) for the cockpit and pickups. The announcement
lists "stereo rendering, **full body IK**, manual reloads, Titan controls, and more"; reports add
6DoF head tracking, hand-aimed weapons with working scopes, a reload for every gun, physical
punching, a cockpit for the Titans, snap turn, and cutscenes on a 2D panel by default. It runs on
any OpenXR runtime (SteamVR, Meta Horizon Link, VDXR); the author's claim is "any OpenXR compatible
headset", the shipped bindings are for the **Valve Index and Oculus Touch** only (per the Linux
port's README). The installer is a 350 KB single-file C# / WPF exe: it finds the game, checks the
EA app, installs Northstar and the mod, offers **Launch in VR**, and keeps every normal launch
unmodded — UploadVR calls it "the best selling point". Around it, within two weeks: a Linux / Proton
port, a bHaptics add-on, a force-grab add-on, an auto-reload add-on and a bindings add-on, all by
other people.

What it is **not** for us: there is no mod source, so everything about the IK, the weapon
handling and the rendering is *observation* — the rows say what is verified (installer code,
manifest, release notes, add-on READMEs), what is reported (press and guides) and what is not known
(every IK parameter). The engine, the injection and the Titan mechanics are Titanfall's. What
carries over is the **distribution and launch discipline** — a section no other reference covers
in code — and a few design facts.

### The license answer

| Project | License | What we may do |
|---|---|---|
| Dishonored VR | **zlib/libpng** for the whole repository ([LICENSE](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/LICENSE): DXVK's license, the VR modifications "provided under the same zlib/libpng license"). Parts come from the maintainer's BioShock trilogy VR mod under **MIT** ([THIRD_PARTY_NOTICES.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/THIRD_PARTY_NOTICES.md)): the OpenXR runtime layer `src/core/vr`, the simulated runtime `src/tools/xrsim`, the smoke client `src/tools/xr_hello32`, the harness scripts `tools/*.ps1`, the documentation templates. The arm IK is MIT from `bioshock-trilogy-vr-lefthand` ([docs/licenses/bioshock-arm-ik.txt](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/licenses/bioshock-arm-ik.txt)). | Both zlib and MIT are compatible with our GPLv3+ engine: **code may be copied and modified** as long as the original notice stays in the copied files and we do not misrepresent the origin (zlib: altered versions must be marked as such). Attribution: keep the license header in each copied file and add a line to our third-party notices. **Exception:** `src/tools/ovrshim` (the SteamVR shim) is adapted from BioVRDev's Bioshock-Remastered-VR, which has **no license file** and was copied with a personal permission given to the BioShock project — do not reuse it (we do not need it: SteamVR has a 64-bit OpenXR runtime). |
| VHVR | **GPL-3.0** ([LICENSE](https://github.com/brandonmousseau/vhvr-mod/blob/master/LICENSE)). | Same license family as our engine (GPLv3+): **algorithms and logic may be ported** and code fragments translated from C# to C++ with attribution (file header naming the source file and the project, a line in our third-party notices, and the GPL obligations we already carry). The Unity assets in `Unity/` (prefabs, shaders, Amplify Occlusion) are not ours to take: Amplify Occlusion is a third-party package and the rest is Unity-specific. |
| Titanfall 2 VR (TinyBlackDog) | **MIT** for the whole repository ([LICENSE](https://github.com/kyus2001/titanfall2vr/blob/main/LICENSE)), except the two controller diagrams `docs/images/controls-*.svg`, which embed Meta's Touch Plus artwork under Meta's own terms ([THIRD_PARTY_NOTICES.md](https://github.com/kyus2001/titanfall2vr/blob/main/THIRD_PARTY_NOTICES.md)). Dear ImGui is vendored (MIT, v1.92.8); the OpenXR loader is Apache-2.0 and its licence text ships in the zip; the ImGui overlay is adapted from bioshock-trilogy-vr (MIT) and the plugin ABI header from NorthstarLauncher (MIT); byte patterns from InjectableGenericCameraSystem (MIT) are cited, not copied. | MIT is compatible with our GPLv3+ engine: **code may be copied and modified** with the copyright line and permission notice kept in each copied file and a line in our third-party notices. Do not reuse the two SVGs (Meta's artwork; our tester README needs Index art anyway, F-46). Nothing in the tree is game data or game code. |
| Titanfall 2 VR (CircuitLord) | **Installer: MIT** ([LICENSE](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/LICENSE); Northstar MIT and BepInEx LGPL-2.1 are downloaded, not bundled — [THIRD-PARTY-NOTICES.md](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/THIRD-PARTY-NOTICES.md)). **The mod: closed source, "all rights reserved. Do not redistribute them without permission"** ([README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md)). Community add-ons: Better Bindings **MIT**, tf2vr-linux **MIT**; `djnewage/tf2vr-mods` and the bHaptics add-on **state no licence**. | The installer's C# could be copied under MIT, but it is .NET Framework 4.8 / WPF and we have no use for the code as written (see the table below): treat it as **reference**. **Nothing of the mod may be taken**, and its behaviour is reference only. **Trap:** MoguraVR's summary table lists the mod as "MIT"; the author's own README says the opposite — a press page is not licence evidence ([TRAPS.md](TRAPS.md)). The add-ons without a licence file are reference only. |

All four projects are references for *mechanics* more than for code: Dishonored VR's C++ lives
inside a D3D9 proxy with its own frame path, VHVR's C# inside Unity's component model, TinyBlackDog's
Titanfall 2 VR C++ inside a D3D11 proxy over a Source engine, CircuitLord's installer C# over a
mod that is not ours to read. The rows below
say per feature whether the code itself is worth taking (the simulator, the harness scripts, pure
decision cores) or only the design.

## Copy, port or reimplement — what works immediately

The owner's question for every row is "can we drop this in and have it work like theirs, or do we
have to rebuild it?". Four answers, used in the catalog's *How to adopt* column:

| Value | Meaning | Typical cost |
|---|---|---|
| **copy** | a file or block copied into our tree compiles and behaves as theirs; same language, no engine coupling; keep the license header, add a line to our third-party notices | hours |
| **copy + adapt** | copied, then one named dependency swapped (their log macro → ours, D3D11 → OpenGL, a path default, their handles → ours) | hours to a day |
| **port** | the logic translated function by function (C# → C++, or lifted out of their engine adapter); the notes name the functions and line ranges, so it behaves the same once our types are substituted | a day per block |
| **reference** | only the decision, the constants and the rationale; written from scratch for Arx | — |

### Dishonored VR: blocks that can be copied

| Block | License, size, dependencies | What to change | Rows |
|---|---|---|---|
| [`src/game/dishonored/swing_core.h`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/swing_core.h) + [`tools/swing-core-tests.cpp`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/swing-core-tests.cpp) — the whole swing / stab decision core with its 75 checks | zlib; 462 + 323 lines; **`<cmath>` and `<cstdint>` only**, namespace `dvr::swing` (`Config`, `Sample` = hand and head positions + timestamp + valid flags + "stab armed", `Core::feed(Sample) → Verdict` = fired slash / stab, block reason, hump end / cut, near-miss data) | the namespace; our adapter around it (gates, the attack call, haptic, log — their `melee.cpp` is the model, not copyable: it reads UE3 state) | F-59, F-60, F-08 |
| The harness scripts: [`xrsim-run.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-run.ps1), `xrsim-cmd.ps1`, `xrsim-state.ps1`, `xrsim-shot.ps1`, the [`tools/xrsim/*.xrs`](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tools/xrsim) sequences as templates, [`eye-check.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/eye-check.ps1), [`img-diff.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/img-diff.ps1), [`soak.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/soak.ps1), `game-key.ps1`, `game-cmd.ps1`, `log-parse.ps1`, `tail-log.ps1` | MIT; PowerShell; they depend only on a `$Dir` default (`%LOCALAPPDATA%\DishonoredVR\xrsim`), `tools/lib/game-path.ps1` (process name `Dishonored`, the log path, the command file path) and the **file protocol** below | three path lines per script; and our simulated backend (F-04) plus command channel (F-01) must speak their protocol: `<dir>\command.txt` (one command per line, consumed on read, a file older than the process start discarded), `<dir>\ack.txt` (`seq=N frame=F error=… / last=…`), `<dir>\state.json` written atomically (`.tmp` then rename) with at least `frame`, `sessionState`, `cmdSeq`, `captureSeq`, `errors`, `lastCmdError`, `projectionViews`, `quadLayers`, `capNonBlackL/R`, `eyeAgeL/R` and the rig (`head`, `gripL/R`, `controls`), per shot `capture\<name>_left.png`, `_right.png`, `<name>.json` with `derived.eyeSeparationM`, `stats.nonBlackPctL/R`, `stats.bboxL/R`, and the game's own `status.json` for `@modassert`. Then the runner, the sequences, the eye check and the soak run **unchanged** | F-01, F-04, F-05, F-06, F-13, F-14 |
| [`src/tools/xr_hello32/main.cpp`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xr_hello32/main.cpp) — the runtime probe | MIT; 150 lines; D3D11 | `XR_USE_GRAPHICS_API_D3D11` → `XR_USE_GRAPHICS_API_OPENGL` with the `HGLRC` binding we already have in `src/vr/VRSystem.cpp`; build 64-bit | F-02 |
| [`src/tools/xrsim/*`](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/src/tools/xrsim) — the simulated runtime | MIT; 11 files, about 5,000 lines; `xrsim_control.cpp` (commands, `state.json`, `ack.txt`), `xrsim_actions.cpp`, `xrsim_frame.cpp` (pacing), `xrsim_session.cpp`, `xrsim_instance.cpp`, `xrsim_math.h`, `xrsim_log.cpp` are graphics-agnostic; `xrsim_compositor.cpp` (51 KB) and the swapchain part of `xrsim_session.cpp` are D3D11 | copy the agnostic files as-is (64-bit is a compile switch); rewrite swapchain images as GL textures in a context shared with the app's `HGLRC`, and the capture as a GL readback | F-09 |
| [`src/core/vr/openxr_input.cpp`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/vr/openxr_input.cpp) + `.h` — the action set, the **suggested bindings for `valve/index_controller`, `oculus/touch_controller`, `htc/vive_controller`, `khr/simple_controller`**, the `InputSnapshot`, the grip hysteresis (0.70 / 0.55), the haptic queue applied inside the sync | MIT; 723 + 91 lines; depends on their `XRLOG` macro (15 calls), `openxr_runtime.h` for the instance and session handles, `imgui.h` (the panel's pointer), `<windows.h>` | replace the log macro with `LogInfo`, pass our `XrInstance` / `XrSession`, drop the ImGui block, rename the actions to ours; the binding tables are data and copy verbatim | F-41, F-42, F-44; #30, #35, #74 |
| [`src/core/util/xr_math.h`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/xr_math.h) — pose and quaternion helpers the above use | MIT; 66 lines | nothing (or map onto glm) | with the two rows above |
| [`tools/collect-support.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/collect-support.ps1) + `release/Collect VR Support.cmd`; [`tools/watch-crashes.ps1`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/watch-crashes.ps1) | MIT; 255 + 3 lines; PowerShell 5.1 | the allowlist paths (`arx.log` history, `cfg.ini`, the `arxcrashreporter` folder, SteamVR's `vrserver.txt`), the exe name | F-91, F-94 |
| [`.github/ISSUE_TEMPLATE/bug_report.yml`](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/.github/ISSUE_TEMPLATE/bug_report.yml), the PR template sections | — | our fields | F-100, F-102 |

**Not copyable from Dishonored VR**, even though the files exist: `melee.cpp`, `crouch.cpp`,
`snap_turn.cpp`, `head_track.cpp`, the HUD anchors, the arm split, the hand-back, the command
channel `command.cpp` (it includes their perf, desktop-eye, GPU-memory and ETW modules) — every
adapter reads UE3 memory through their reflection layer and the pad bridge. Their *rules* are in
the notes; the code is a few dozen lines each to write against Arx's own state.

### VHVR: functions to translate one to one (C# → C++, GPL-3.0, with attribution)

Nothing from VHVR compiles in our tree, but these functions are self-contained maths over
positions, velocities and directions; translated line by line they behave as in Valheim.

| Function(s) | File and lines | Size | In → out | Rows |
|---|---|---|---|---|
| `PhysicsEstimator` (8-snapshot ring, `GetVelocity`, `GetAngularVelocity`, `GetVelocityOfPoint`, `GetAverageVelocityInSnapshots`, `GetLongestLocomotion`) | [Utilities/PhysicsEstimator.cs:10-200](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/PhysicsEstimator.cs) | ~200 | positions per physics step → velocity, angular velocity, travel over a window | F-58, F-61, F-72, F-77 |
| `WeaponCollision.hasMomentum`, `isStab`, `GetMinSpeed` | [Scripts/WeaponCollision.cs:662-770](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponCollision.cs#L662-L770) | ~110 | hand / weapon velocities, blade direction → "this contact is a hit" | F-58, F-61 |
| `WeaponUtils.EstimateWeaponDirectionAndLength`, `EstimateWeaponCollider`, `GetWeaponVelocity` | [Utilities/WeaponUtils.cs:398-433, 627-685](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/WeaponUtils.cs) | ~100 | mesh bounds → blade axis, length, hit volume | F-48, F-58 |
| `AttackTargetMeshCooldown` + `MeshCooldown` (`tryTriggerPrimaryAttack`, `GetSpeedScaledDamageFactor`, `calcDamageMultiplier`) | [Scripts/AttackTargetMeshCooldown.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/AttackTargetMeshCooldown.cs), [MeshCooldown.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/MeshCooldown.cs) | ~170 | per-target timer, speed → damage factor, multi-target decay | F-62, F-63 |
| `WeaponUtils.GetThrowLaunchSpeed`, `GetThrowAimDir`; `ThrowableManager.CalculateThrowAndDistance` and the direction modes | [WeaponUtils.cs:688-731](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/WeaponUtils.cs), [ThrowableManager.cs:127-297](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/ThrowableManager.cs) | ~160 | hand speed and travel at release → direction, launch speed | F-72 |
| `ShieldBlock.setBlocking`, `CheckParryMotion`; `WeaponBlock.setBlocking`; `Block.GetBlockTolerance`, `hitIntersectsBlockBox` | [Block/ShieldBlock.cs:95-135](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/ShieldBlock.cs), [Block/WeaponBlock.cs:32-89](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/WeaponBlock.cs), [Block/Block.cs:193-294](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/Block.cs) | ~200 | hit direction, shield / blade pose and velocity → blocked, parry, block timer | F-68, F-69 |
| `BowLocalManager.checkHandNearString`, `handlePulling`, `pullPercentage`, `releaseString`; `BowManager.rotateBowOnPulling`, `pullString` | [BowLocalManager.cs:141-389](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowLocalManager.cs), [BowManager.cs:444-548](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowManager.cs) | ~250 | two hand poses → bow orientation, draw percentage, aim, release | F-70 |
| `BowManager` string / limb extraction and bending | [BowManager.cs:145-241, 492-526](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowManager.cs) | ~150 | a bow mesh → string ends, bone weights, bend angle | F-71 |
| `InterhandItemTransfer` | [Scripts/InterhandItemTransfer.cs:12-132](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/InterhandItemTransfer.cs) | ~120 | two hand poses and grips → transfer event | F-54 |
| `Pose` holster / draw zones | [Utilities/Pose.cs:139-340, 656-797](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/Pose.cs) | ~350 | head and hand poses → zone, holster / draw events | F-55 |
| `TwoHandedGeometry` providers + `LocalWeaponWield.GetDesiredTwoHandedState` | [TwoHandedGeometry.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/TwoHandedGeometry.cs), [LocalWeaponWield.cs:218-340](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/LocalWeaponWield.cs) | ~400 | two hand poses → weapon pose, rear / front hand | F-73 |
| `SummonerManager` rising-hand gesture | [SummonerManager.cs:65-124](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SummonerManager.cs) | ~60 | hand height over time → progress, event | F-78 |
| `VRGUI.updateUiPanelScaleAndPosition`, the recenter rules, `UpdateCursorPosition`, the click dead zone | [VRGUI.cs:433-565, 714-786, 1474-1496](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRGUI.cs) | ~250 | head pose, movement state → panel pose; ray → cursor pixel | F-80, F-81 |
| `VRPlayer` sneak-height check; `ControlPatches` snap turn stepping and re-arm; `FadingManager` | [VRPlayer.cs:2362-2387](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRPlayer.cs), [ControlPatches.cs:1124-1263](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/ControlPatches.cs), [FadingManager.cs:21-130](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/FadingManager.cs) | ~280 | eye height, stick, game events → crouch, yaw step, fade | F-29, F-32, F-35 |
| `QuickAbstract` radial menu (anchor, sector hover, selection) | [QuickAbstract.cs:146-410](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/QuickAbstract.cs) | ~260 | hand pose → hovered sector | F-86 |
| `ApplicationManifestHelper` | [Utilities/ApplicationManifestHelper.cs:16-70](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/ApplicationManifestHelper.cs) | ~55 | writes and registers the `.vrmanifest`; needs `IVRApplications` from `openvr_api.dll` (BSD-3) — one OpenVR call in an OpenXR game, or keep `register-steamvr.ps1` | F-90 |
| The haptics event table (area 9) and the attack-duration table | `WeaponCollision.cs`, `ShieldPatches.cs`, `BowLocalManager.cs`; `WeaponUtils.cs:297-323` | data | events → `(duration, Hz, amplitude)`; weapon → cooldown | F-44, F-62 |

Everything else in VHVR (the Harmony patches, VRIK, the Unity cameras and canvases, the SteamVR
Interaction System) is Unity plumbing: **reference** only.

### Titanfall 2 VR: blocks that can be copied

MIT, so the same terms as the Dishonored VR blocks. The plugin's engine adapters (`camera/`,
`render/`, `hud/`, most of `input/` and `hands/`) read Titanfall's memory and are not copyable;
the blocks below are the ones with no engine inside them.

| Block | License, size, dependencies | What to change | Rows |
|---|---|---|---|
| [`plugin/src/xr/asym_projection.h`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/asym_projection.h) + [`tools/asym-projection-check.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/asym-projection-check.cpp) — tangent-space frustum extents (`TanExtents`, `IsSymmetric`, `ShearForCentre`, `ShiftedExtents`, `ClampToRendered`, `WindowToRect`) and the offline check with a **negative control** (the same conversion without the Y flip must fail) | MIT; 140 + ~250 lines; `<cmath>` only; namespace `tf2vr` | our #19 builds an off-axis matrix straight from `XrFovf`, so only `TanExtents`, `IsSymmetric` and the *shape* of the test are needed; feed it the Index's four angles from our banner | F-115, F-08 |
| [`plugin/src/xr/xr_input.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp) + [`.h`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.h) — the action set, the Touch and simple-controller suggestions with the atomic-rejection fallback, the per-hand seqlock publication, the bound-profile poll, the `changedSinceLastSync` press latch, the handedness check, `TriggerHaptic` | MIT; 933 + 172 lines; depends on `Tf2VrLog` and the OpenXR → Source axis helpers in the header | the log call; the axis conversion (ours is in `src/vr`); add the Index profile to the suggestion lists (Dishonored VR's `openxr_input.cpp` has it, F-41); the published globals become our action snapshot (#74) | F-122, F-123, F-41, F-44 |
| [`plugin/src/input/dpad_gesture.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/dpad_gesture.cpp) — the hand-beside-the-head modifier | MIT; 249 lines; reads the published grip pose and head position | the two reads and the haptic call | F-124 |
| The right-stick arithmetic in [`input/vr_input.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/vr_input.cpp) (`Latch`, the axis-dominance gate with its release factor) and the turn curve in [`input/xinput_pad.cpp` `ComposeGamepad`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp) | MIT; ~80 lines inside engine-specific functions | lift the arithmetic into a header; it is the model for the #32 / #33 decision core (F-08) | F-121 |
| [`hands/hand_pose.cpp` `ApplyAdsBrace`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.cpp) — the aim brace | MIT; ~140 lines; `GetTickCount64`, the log | the clock and the log; takes pitch/yaw in and out | F-127 |
| [`core/settings_registry.h`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/settings_registry.h) and, in [`core/config.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp), the `kSettings[]` table, `RunSettingsRegistrySelfCheck` and `PersistSettingToIni` | MIT; a design plus ~300 lines | Arx has its own `Config` / `ConfigWriter`; take the design (one table, one write path, a self-check with a positive control, staged edits, a surgical rewrite) | F-113 |
| [`core/crash_recorder.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/crash_recorder.cpp), [`diag/xr_breadcrumb.h`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/xr_breadcrumb.h), [`engine/hook_registry.h`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/engine/hook_registry.h), [`diag/stall_watchdog.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/stall_watchdog.cpp) | MIT; 605 + ~100 + ~80 + 337 lines; Win32 only | Arx has an SEH crash handler and reporter; add the arm-state bitmask, the XR breadcrumb and the watchdog thread to it | F-131, F-132 |
| [`core/diagnostics.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/diagnostics.cpp) — rotation with the rotation report as line 1, the quiet-mode allowlist, the 250 ms flusher | MIT; 461 lines | Arx's logger already rotates (#129); the allowlist and the "rotation incomplete" line | F-92, F-105 |
| [`tools/logdig`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/logdig), [`tools/defaults-vs-ini.py`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/defaults-vs-ini.py), [`tools/orphan-statements.py`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/orphan-statements.py), [`tools/bmpdiff/bmpdiff.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/bmpdiff/bmpdiff.cpp), [`tools/bmpcrop/bmpcrop.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/bmpcrop/bmpcrop.cpp), [`tools/bmpshift/bmpshift.cpp`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/bmpshift/bmpshift.cpp) | MIT; sh / Python / C++; the Python ones parse `config.cpp` by regex | `logdig` → a PowerShell digest over `arx.log` (#131); `defaults-vs-ini` → `cfg.ini` and our defaults; `bmpdiff` → our PNG shots (F-14) | F-112, F-114, F-15, F-14 |
| [`plugin/cmake/version_stamp.cmake`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/cmake/version_stamp.cmake), [`tools/build.ps1`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/build.ps1), [`tools/install.ps1`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/install.ps1), [`tools/package.ps1`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/package.ps1), [`tools/cleanprofile/enter-clean-profile.ps1`](https://github.com/kyus2001/titanfall2vr/blob/main/tools/cleanprofile/enter-clean-profile.ps1) | MIT; CMake / PowerShell | the paths; `build-windows.ps1` already stamps `built-from.txt` (D-014); the package gates — placeholders, the documented hash, nothing personal, every licence — for #88 | F-96, F-18 |

### CircuitLord's Titanfall 2 VR: blocks that could be ported

The mod is closed, so there is nothing to copy from it. The MIT installer is C# on .NET Framework 4.8
(WPF); our tooling is PowerShell and our game is C++. Nothing below compiles in our tree — these are
the pieces whose **logic** is short and exact enough to restate:

| Block | License, size | What it decides | Rows |
|---|---|---|---|
| [`Services/LaunchPermissions.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/LaunchPermissions.cs) — `IsElevated(pid)` and the Steam / EA app / installer check | MIT; 57 lines; `OpenProcess` + `OpenProcessToken` + `GetTokenInformation(TokenElevation)` | whether the process that will start the game runs as administrator | F-146 |
| [`Installers/Titanfall2Installer.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) `Play()`, `VrProcess()`, `CanWriteGameFolder()`, `GrantGameFolderAccess()` | MIT; about 80 lines of the 310 | the pre-launch checklist: proxy DLLs beside the exe, inherited VR variables, a write probe, one UAC prompt that grants Modify | F-146 |
| [`Services/CrashReportService.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/CrashReportService.cs) `CreateTitanfall` | MIT; 123 lines | which files go into a shareable report, `report.json` with the build hash | F-147, F-91 |
| [`Services/RepoClient.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/RepoClient.cs) `Download`, `CachedDownload` and [`PackageInstaller.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/PackageInstaller.cs) | MIT; 123 + 130 lines | the HTTPS + SHA-256 contract, the zip-slip guard, `preserve` / `tokenize`, stale-file cleanup | F-142, F-143 |
| [`tests/InstallerValidation/Program.cs`](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/tests/InstallerValidation/Program.cs) | MIT; 602 lines | a console test per refusal and per "this must not change" (see F-152) | F-152 |

## Feature catalog

One table per category; IDs are stable across tables, so an answer like "take F-03, F-07" is
enough. Columns: **Source** (DVR = Dishonored VR, VHVR = Valheim VR, TF2VR = TinyBlackDog's Titanfall 2 VR,
CLVR = CircuitLord's Titanfall 2 VR, with the
file or doc the row comes from; the [detailed notes](#detailed-notes-per-area) hold the full references),
**How to adopt** (*copy* / *copy + adapt* / *port* / *reference*, as defined
[above](#copy-port-or-reimplement--what-works-immediately)), **Effort** (S ≤ 1 day, M a few days,
L a week or more), **Depends on** (our issues or other IDs), **Fits** (MVP now / after MVP /
later / probably not, with why), **Decision** — empty, for the owner. Rows **F-111 to F-134**
come from Titanfall 2 VR (#118, [section 11](#11-titanfall-2-vr-118)), rows **F-135 to F-152** from
CircuitLord's (#120, [section 12](#12-circuitlords-titanfall-2-vr-120)); a `TF2VR` or `CLVR` source
appended to an older row marks a duplicate, and what it adds is in that section. Rows built on a
closed mod say what kind of **evidence** they rest on: *code* (the installer), *docs* (the author's
README and release notes), *add-on* (a community add-on's README), *report* (press and guides) —
and the IK rows say plainly what is not known.

### Testing

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-01 | **Flat smoke test with a command channel and `status.json`** — a file the game polls (feeding the existing script console plus `status`, `shot`, `recenter`), a `status.json` with game state, frame counters, VR session state, and a `[game] state:` log line | DVR [tools/game-cmd.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/game-cmd.ps1), [VERIFICATION.md §1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md) | Every worker self-test and every later script asserts on numbers instead of reading a log by eye | reference | S | #99 | **MVP now** — it is #99's shape | **Take** → #99 |
| F-02 | **`xr_hello` runtime probe**, 64-bit OpenGL: instance, system, session, 60 empty frames; exit 0 / 1 / 2 | DVR [xr_hello32/main.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xr_hello32/main.cpp) (MIT); CLVR ships the same idea as `TF2VR\tools\xr_probe.exe` (F-146) | "Is the runtime the problem or the game?" in one command; measures what SteamVR does with an idle Index before #99 automates anything | copy + adapt (D3D11 → OpenGL binding) | S | — | **MVP now** — #99 step 1 | **Take** → #99 (step 1) |
| F-03 | **SteamVR null driver recipe** for a VR launch with no headset (`driver_null` enabled, `forcedDriver: null`, `activateMultipleDrivers`) | web (section 1e) | The real runtime path (OpenGL, FOCUSED session) with no headset; zero code | reference (config) | S | F-01 | **MVP now** — if #99 finds an idle Index never reaches FOCUSED; needs a SteamVR restart to switch | **Take** → #99 (only if an idle Index never reaches FOCUSED) |
| F-04 | **In-engine simulated VR backend** (`--vr-sim <script>`): a `vr::` interface with an OpenXR and a scripted implementation; head/hand poses and action states from a script, offscreen per-eye captures, a status file | DVR xrsim's rig and commands ([xrsim_common.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xrsim/xrsim_common.h)) as the model | Every VR task (camera, locomotion, swing, runes) tested without a headset or a runtime, in CI even as a service | reference | S–M | #18, F-05 | **MVP now** — from #18 on, the worker's main self-test | **Take** → #112 |
| F-05 | **`.xrs` scripted sequence runner** with `@assert` on `status.json`, `@log` / `@nolog` on `arx.log`, `@shot`, `@frames`, `@key` | DVR [tools/xrsim-run.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-run.ps1), [tools/xrsim/*.xrs](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tools/xrsim) (MIT) | Repeatable sequences per feature (`swing-edge`, `snap-turn`, `world-6dof`) a worker runs before handing over | copy (script) | S | F-01, F-04 | **MVP now** | **Take** → #113 |
| F-06 | **"Two eyes are two pictures" instrument**: per-frame per-eye image ids, left-vs-right difference against a same-eye floor, parallax sign, plus the five-leg eye check (pairing rate, eye separation, L/R diff band, a 25° yaw moves both eyes) | DVR `stereo: frameid`, [tools/eye-check.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/eye-check.ps1) | Catches swapped, stale or identical eyes that no flat screenshot shows — the bug class that cost them weeks | reference | S | #19, F-04 | **MVP now** — checkpoint A acceptance without the headset | **Take** → #112 (acceptance of #19) |
| F-07 | **Rate / cadence verdict line** every 3 s (display period, presents/s, `xrEndFrame` mean/max, OVER / MATCHED / UNDER-SUBMITTING, EVEN / UNEVEN CADENCE in display slots) plus `frame gap` and `mark` | DVR `stereo: rate`, [VERIFICATION.md §1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md); TF2VR [xr_context.cpp spike window](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (read WAIT+END as one budget, the split is phase; the worst frame and the worst *run* per 10 s window, never the mean; a SPIKE MARK key dumps the last 360 frames) | The MVP's "holds the refresh rate" judged from a log; ghosting on head turns explained by uneven cadence | reference | S | #20 | **MVP now** | **Take** → #20 |
| F-08 | **Host tests of pure decision cores** in `arxtest` (CppUnit): swing detector, rune projection, snap turn, crouch threshold, each as a header taking positions and timestamps | DVR [tools/*-host.ps1 + *-tests.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tools) | VR logic verified in CI with no game; "a suite that does not compile is not a passing suite" | reference | S per core | #39, #44, #32, #100 | **MVP now** — a rule for every VR core | **Take** → #114 + rule in WORKFLOW.md |
| F-09 | **Port `dvr_xrsim32` to 64-bit OpenGL**: a full simulated OpenXR runtime with scripted rig, pacing modes, fault injection (session lost, swapchain fail), focus policies, compositor captures | DVR [src/tools/xrsim](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/src/tools/xrsim) (MIT) | The only option that scripts the *runtime* side; reproduces session-state and pacing bugs | copy + adapt (GL swapchains) | M minimal, L parity | F-02 | **later** — when a runtime-level bug needs reproducing; F-04 covers the engine side first | Later |
| F-10 | **OpenVR sample driver** with scripted HMD and controllers through the real SteamVR | Valve [samples/driver_sample](https://github.com/ValveSoftware/openvr/tree/master/samples/driver_sample) (BSD-3) | Scripted poses on the real runtime | reference | M | F-03 | **probably not** — F-09 does the same without SteamVR | No |
| F-11 | **Monado on Windows** as a simulated runtime | [winbuild.md](https://gitlab.freedesktop.org/monado/monado/-/blob/main/doc/winbuild.md) | An open-source runtime with simulated devices | — | M | — | **probably not** — "work in progress", simulated drivers only, OpenGL on Windows unverified | No |
| F-12 | **Meta XR Simulator** | [native docs](https://developers.meta.com/horizon/documentation/native/xrsim-intro/) | Session record/replay, CLI | — | — | — | **not applicable** — no OpenGL support | No |
| F-13 | **Soak test** (`soak.ps1 -Minutes N`: exit 2 = log stalled, 4 = process died) | DVR [tools/soak.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/soak.ps1) (MIT) | Wedges and leaks found before a tester does | copy (script) | S | F-01 | **after MVP** — before the tester build #88 | Later (with #88) |
| F-14 | **`img-diff` screenshot comparison** (mean absolute difference, per-cent changed, noise floor ~0.4) | DVR [tools/img-diff.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/img-diff.ps1) (MIT); TF2VR [tools/bmpdiff](https://github.com/kyus2001/titanfall2vr/blob/main/tools/bmpdiff/bmpdiff.cpp) (an 8×6 per-cell brightness table and a difference bitmap; `bmpcrop` never resamples — "a downscale destroys exactly the evidence being judged"; `bmpshift` refuses to report a disparity until it has found a known one) | Flat-visual assertions for F-05 shots (head look moved the picture, stereo differs) | copy (script) | S | F-05 | **MVP now** | **Take** → #113 |
| F-15 | **Golden default-config check** (the default config generated from code and compared in CI) | DVR [tests/golden](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tests/golden), `tools/ini-golden.py`; TF2VR [tools/defaults-vs-ini.py](https://github.com/kyus2001/titanfall2vr/blob/main/tools/defaults-vs-ini.py) (DIVERGENCE / DISCARDED / NO CODE DEFAULT against the wearer's own ini; exit 1 gates the release) | A changed default never ships unnoticed | reference | S | — | **later** — once VR settings exist (post-MVP) | Later |
| F-111 | **Synthetic head pose with motion and a tilted reference**: a constant pose (static), a sweep (yaw ±25° at 1.2 rad/s on the wall clock, so two runs compare), and a pitched or rolled pose *at the recentre* — a composition that bakes the reference's pitch into the camera is invisible while the reference is level; "arm head tracking at yaw 0, then step the yaw, or the correction is never under load" | TF2VR [core/flat_harness.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/flat_harness.cpp), [camera_hook.cpp "HEADING IS REFERENCED"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp) | Three `.xrs` cases for #112 / #113: a still head gives identical view matrices every frame; a sweep shows no velocity-proportional error; a recentre while pitched 18° leaves no permanent tilt (#24, #28) | reference | S | #112, #113 | **MVP now** — test cases, no new code | |

### Tooling

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-16 | **Script-console command seam**: the file channel of F-01 feeds Arx's script commands (teleport, spawn, give item) so test saves become scripts | DVR command seam + Arx `src/gui/Console.cpp` | Scenario setup without hand-made saves (#70 partly) | reference | S | F-01 | **MVP now** — alongside #70 | **Take** → #99 |
| F-17 | **Subsystem bisect switches** (`DVR_SKIP=hands,blink,overlay`-style: disable a VR feature by environment variable without a rebuild) | DVR [TESTING.md "Crash triage"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/TESTING.md) | Crash triage on the owner's or a tester's machine | reference | S | — | **after MVP** — with #88 | Later (with #88) |
| F-18 | **Log banner and conventions**: first line = build id, git hash, config (Debug perf lines tagged "not comparable"), `.prev.log` rotation, one `VR:` category | DVR [VERIFICATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md) rows VR-160 / VR-180; TF2VR [plugin.cpp `HostOnInit`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/host/plugin.cpp), [cmake/version_stamp.cmake](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/cmake/version_stamp.cmake) (the sha stamped at build time, `+` when the tree is dirty, the DLL's own write time beside `__DATE__`; the line forced past the quiet filter — F-133) | Every log a tester sends says which build wrote it | reference | S | — | **MVP now** — cheap, pays off at #88 | **Take** → #115 |
| F-19 | **In-headset settings and debug overlay** (ImGui, pointed at with the laser; Basic / Advanced / Debug tiers; every lever a live A/B that fails soft) | DVR [F10_MOTION_CONTROLS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/F10_MOTION_CONTROLS.md); TF2VR [ui/menu_overlay.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ui/menu_overlay.cpp) (driven by the sticks on a world-fixed quad, edits staged and saved on close — F-128, F-113) | Tuning thresholds (swing speed, panel distance) in the headset without a relaunch | reference | M | #49, #34 | **after MVP** — the MVP has no VR settings menu by decision | Later |
| F-112 | **`logdig`, a log digest for agents**: lines with the timestamp, hex and numbers replaced by `N` become families; families seen fewer than 6 times are printed in full and in order, the rest as `count × last sample`; `id` prints the run identity, `f <pattern> [n]` the last n matches; "never cat the log — reading it whole costs 25–40 % of an agent's context in a single call" | TF2VR [tools/logdig](https://github.com/kyus2001/titanfall2vr/blob/main/tools/logdig) (MIT, 60 lines of sh / awk) | #131's digest in one script: a 9,000-line `arx.log` read in a screenful, and the rare lines are where the answers are | copy + adapt (PowerShell, `arx.log` paths) | S | #129 | **MVP now** — #131 is in the queue | |
| F-113 | **Settings registry**: one table row per setting (key, label, tooltip, lock-free getter, min, max, default, widget, tier *live / restart / careful*, category); the ini and the panel take the **same** write path; a startup self-check feeds every key through it at its current value and round-trips the getter, after a **deliberately bogus key proves the check can fail**; panel edits are staged and applied on close; the ini is rewritten line by line (comments kept, a duplicate key commented out as "superseded"), temp file then rename; a per-headset cache for the one setting that is per headset | TF2VR [core/settings_registry.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/settings_registry.h), [config.cpp `RunSettingsRegistrySelfCheck`, `PersistSettingToIni`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp), [render_resolution.cpp (headset cache)](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/render_resolution.cpp) | The design for F-19's panel and for every VR setting we add to `cfg.ini` (Arx's `Config` already has defaults and a writer) | reference | M | F-19, a VR config | **after MVP** — with F-19 | |
| F-114 | **Orphan-statement lint**: a statement indented deeper than the complete statement above it, with no brace between them, is unconditional yet reads as controlled — three settings toggled themselves every frame after an `if` line was deleted; the lint exits 1 | TF2VR [tools/orphan-statements.py](https://github.com/kyus2001/titanfall2vr/blob/main/tools/orphan-statements.py) (MIT) | One CI step over `src/vr/` | copy (script) | S | — | **after MVP** — cheap, but noisy on the upstream tree unless scoped | |

### Rendering and performance

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-20 | **Rung 1 first: the flat frame on a head-locked quad in both eyes** (`XrCompositionLayerQuad`, 2.4 m wide at 1.75 m) before any stereo exists | DVR [ARCHITECTURE.md "The stereo ladder"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/ARCHITECTURE.md) | Swapchains, frame loop, mirror and head tracking verified with one readback; reused later for menus and loading screens | reference | S | #18, #20 | **MVP now** — the first thing the owner can see in checkpoint A | **Take** → #18 |
| F-21 | **Pose-matched submission**: render and submit each eye with that frame's `xrLocateViews` pose; place hands and weapon from the same head sample | DVR [RESEARCH.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/RESEARCH.md), [HEAD_MOTION_120HZ.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HEAD_MOTION_120HZ.md); TF2VR [xr_context.h `renderedView_`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.h) (the pose each eye was rendered with, kept apart from the current locate — F-116) | No judder or ghosting on head turns; their "exceptional improvement in world stability" | reference (a rule) | S | #20 | **MVP now** — design rule for #20 / #24 | **Take** → #20, #24 (rule) |
| F-22 | **Per-eye draw = re-enter the scene draw with the other eye's camera** (simulate once, draw twice) | DVR `reentry` method | Confirms #71's shape; nothing to copy | reference | — | #71 | **MVP now** (already planned) | **Take** → #71 (already planned) |
| F-23 | **Screen-space effects at a comfortable depth** (damage flash, fades, magic tint, fog and flares on a quad at a fixed depth or per eye with the eye's projection) | DVR README, `[Lens] Distance=18` | Removes the "effects look wrong in stereo" accepted problem | reference | S–M | #72, #73 | **after MVP** — accepted problem for the MVP | Later |
| F-24 | **Desktop mirror = a copy of the left eye**, optional and cheap, never a third render | DVR [DESKTOP_MIRROR.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/DESKTOP_MIRROR.md); VHVR `MirrorMode` | #21 done right the first time (their window alternated eyes for months) | reference | S | #21 | **MVP now** | **Take** → #21 |
| F-25 | **Render scale 50–200 %** with quality presets, changed live | DVR README "The launcher"; TF2VR [xr/render_resolution.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/render_resolution.h) (one `render.scale`, both axes together so the aspect cannot drift; ships at 0.75 because nothing measures the GPU; a per-headset geometry cache keyed by runtime, system name, recommended size and tangents, which wins over the ini) | Performance headroom per PC (the tester's Quest 3 PC) | reference | S | #18 | **after MVP** — MVP renders at native size | Later |
| F-26 | **Even cadence**: a frame cap that divides the refresh rate, slots-per-frame measured; "a higher refresh rate can be worse" | DVR [KNOWN_ISSUES.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/KNOWN_ISSUES.md) ghosting entry | Judder-free head turns on the Index at 90 / 120 / 144 Hz | reference | S | #20, F-07 | **MVP now** — definition of done "holds the refresh rate" | **Take** → #20 |
| F-27 | **Performance instruments**: per-frame phase split every 3 s, PACE-BOUND vs STARVED verdict, GPU timestamp ring, `perf-tick-stats` over marked windows | DVR [VERIFICATION.md §1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md) perf rows; TF2VR [diag/plugin_cost.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/plugin_cost.h) (rdtsc scopes per seam with the probe's own cost calibrated and subtracted — "a probe that does not state its own cost is asking to be believed"; a HITCH line when one frame spends over 4 ms in the mod survives quiet mode) | Epic #8's "no dropped frames" judged from logs | reference | M | F-07 | **after MVP** — epic #8 | Later |
| F-28 | AFW / "mod spacewarp", DLSS in a 64-bit helper, custom TAA | DVR `PLAN-mod-spacewarp.md`, `PLAN-motion-vectors-dlss.md` | — | — | — | — | **probably not** — answers to their readback pipeline and a wireless link; SteamVR has motion smoothing | No |
| F-115 | **Off-axis per-eye projection from `XrFovf`, canted eyes allowed, verified offline**: each eye's projection from its own four tangents (a Quest 3 reports L −54° R 40° U 44° D −55°, mirrored between the eyes; the Index is asymmetric too) and its own view orientation. TF2VR could not (a symmetric engine frustum): it renders the symmetric cover and *shears* the frustum to the lens centre, with the Y flip "verified by construction" — an offline check fed a symmetric headset's numbers (must be an identity) and an asymmetric one's (the top edge must move *down* in image space), plus the no-flip variant as a negative control; the banner says when the two eyes' orientations differ (canted panels) | TF2VR [xr/asym_projection.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/asym_projection.h), [tools/asym-projection-check.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/tools/asym-projection-check.cpp), [xr_context.cpp `LogHeadsetBanner`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) | #19 done right the first time: no symmetric cover, no cropped lens on an asymmetric headset; a host test with the Index's angles and a wrong-sign control | reference + copy the test's shape (MIT) | S | #19, #112, F-08 | **MVP now** — #19 and its host test | |
| F-116 | **Alternate-frame stereo — the two rules it taught, not the method**: (1) a projection layer is submitted with the pose **each eye was rendered with**, never the current locate (they keep `renderedView_[2]` beside `locatedView_[2]`); (2) **nothing is submitted until both eyes hold an image** (`eyeHasImage_`) — one stale or blank eye against a live one is the bug class F-06 catches. Their same-frame alternative, re-entering the engine's scene draw with the other eye's camera, hangs after about 129 doubled frames because the engine's per-frame latches, job group and temporal history are shared by both passes: the #71 split must move every per-frame side effect out of the draw before #19 draws twice | TF2VR [xr/xr_context.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.h), [render/scene_reentry.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/render/scene_reentry.h), [render/stereo_targets.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/render/stereo_targets.h), README "In development" | Half-rate eyes are judder by construction (not for us); the rules go to #20 / #19 and the warning to #71 | reference (rules) | — | #19, #20, #71 | **MVP now** (rules) | |
| F-117 | **Keep the compositor fed while the game stalls** (level loads): a gap-filling pacing thread re-submits the last image with a fresh pose when the game has not presented for a display period. Two designs failed first and left rules: **one thread owns `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`** (a mutex around two callers broke the runtime's pacing); **never acquire a swapchain image you will not write** (the compositor shows the most recently *released* image, so an untouched acquire jumps back in time); **zero layers means "show nothing", not "hold the last picture"**; a loop paced by our own timer slips a frame every 50 (12.75 ± 0.52 ms against 12.50) — only `xrWaitFrame` is the display clock; it "does not shorten the load and must never be sold as if it does" | TF2VR [xr/xr_context.h "F3"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.h), [xr_context.cpp `SubmitFrameCore`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp), [KNOWN-ISSUES 5](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) | A loading screen that stays up instead of SteamVR's grid; the rules for #20 now, a frame pump from Arx's load-progress callback after | reference | S (rules), M (the pump) | #20, #18 | **MVP now** (rules) → #20; the loader pump **after MVP** | |
| F-118 | **Session events as evidence**: every `XrSessionState` transition logged by name with its meaning (SYNCHRONIZED = not displayed = "the runtime is entitled to pace us as slowly as it likes"); on leaving FOCUSED the Windows foreground window, pid, class and title are logged (another pid → the game lost desktop focus and the runtime followed; our pid → the runtime took focus for its own overlay); `XR_SESSION_NOT_FOCUSED` from `xrSyncActions` named as "every action reads inactive — nothing is wrong with the bindings"; `XR_ERROR_FORM_FACTOR_UNAVAILABLE` named as "no headset: the game runs at full speed and looks identical to a good run"; `REFERENCE_SPACE_CHANGE_PENDING` re-seeds the mod's own recentre reference | TF2VR [xr/xr_context.cpp `PollEvents`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp), [xr_input.cpp `XrInputSync`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp) | One line each in `arx.log` (#130 D2) and `status.json`; the runtime's own recentre works through #28 | reference | S | #17, #28, #130 | **MVP now** — #130 and #28 | |

### Camera and comfort

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-29 | **Physical crouch**: a head drop of ~20 cm held 250 ms sets the engine's crouch; VHVR's 70 % / 75 % of calibrated height hysteresis; stance read back from the engine with a settle time | DVR [crouch.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/crouch.cpp); VHVR [VRPlayer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRPlayer.cs) `RoomScaleSneakHeight` | #100 with tested constants | port the rule | S | #100 | **MVP now** — #100 is queued | **Take** → #100 |
| F-30 | **Eye-height calibration**: clamp the measured height to [1.2, 1.9] m, move the character's eye point to match within [−0.75, +0.25] m; tune height trim and world scale together | VHVR [VRPlayer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRPlayer.cs); DVR `[Tracking] HeightOffsetM` + `[PosTrack] Scale`; TF2VR [xr_context.cpp `LocateViews`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (eye height from `XR_REFERENCE_SPACE_TYPE_STAGE`, settled over 5 samples within 1 cm inside [0.8, 2.0] m — and two runtimes read the same seated wearer 0.3 m apart, so it drives nothing: F-119) | #101 (seated play, real height) | reference | S | #27, #101 | **after MVP** — D-006 | Later (#101) |
| F-31 | **Shake, bob and pop-smoothing removed at the source per kind**; the HMD pose applied *after* every engine camera smoothing (or the smoothing disabled) | DVR `[CameraShake]`, [PLAN-VR-165](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-VR-165-camera-displacement.md); TF2VR [hands/weapon_settings.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/weapon_settings.h) (weapon sway zeroed in its per-weapon source data, not cancelled at the camera — "no camera-matrix correction can cancel an animation") | #76 without the "camera leaves the body" class of bug | reference | S | #76 | **MVP now** | **Take** → #76, #24 |
| F-32 | **Snap turn as a body-yaw step**: 45° at 0.6 stick, re-arm at 0.3, once per push, blocked while a menu owns the stick; optional "smooth snap" at 10° per 10 ms; the step logged FIRED / APPLIED | DVR [snap_turn.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/snap_turn.cpp); VHVR [ControlPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/ControlPatches.cs); TF2VR [input/aim_cmd.h `RequestSnapYaw`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/aim_cmd.h) (their first snap held the turn button for degrees ÷ yaw speed seconds — a fast *sweep*, the thing snap exists to avoid; now one angle write between two frames, requests replace rather than queue, re-armed at centre; 30° by default) | #32 with tested constants; both ship smooth turn as the default | reference | S | #32 | **MVP now** | **Take** → #32 |
| F-33 | **Decision: the stick's frame is the head** ("stick forward = where you look") in both mods; the decoupled body only decides where hands and the UI anchor live | DVR `HeadBasedMovement`; VHVR `JoyStickForwardDirection=LookDirection` | Settles #25 / #31 before they are built (Dishonored's body-relative mode kept drifting) | decision | — | #25, #31 | **MVP now** — decide before #31 | **Decided:** look direction — D-008 (#25, #31) |
| F-34 | **Recenter as a hold on a chord** (tap = panel), also re-seeding the UI anchor; VHVR's "hands in front of the face for 3 s" pose as an alternative | DVR [F10_MOTION_CONTROLS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/F10_MOTION_CONTROLS.md); VHVR [VRControls.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRControls.cs); TF2VR [xr_context.cpp `PollEvents`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (the runtime's own recentre arrives as `REFERENCE_SPACE_CHANGE_PENDING` and re-seeds the mod's reference; an automatic recentre 300 ticks after the world is up — F-120) | #28 | reference | S | #28 | **MVP now** | **Take** → #28 |
| F-35 | **Fades**: 0.2 s to black on loads, teleports and death, 0.15 s back, room offset re-centred on the way back; a low-health pulse instead of a full-screen flash | VHVR [FadingManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/FadingManager.cs) | Comfortable level loads and death | reference | S | — | **after MVP** — MVP accepts death and fades behaving badly | Later |
| F-36 | **Cinematics**: HMD rotation composed over the authored camera (one sample for both eyes), forced pitch / roll / FOV locked, controls parked with a 300 ms hold-to-skip | DVR [CINEMATIC_HEAD_TRACKING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CINEMATIC_HEAD_TRACKING.md), `[Cine]` keys; TF2VR [KNOWN-ISSUES.md 15](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) (ships with cutscene cameras pinned to the head — "the one thing in this alpha that can make you feel ill"); CLVR (cutscenes on a 2D panel by default, "force 3D cutscenes" an off-by-default option — F-140) | Cutscenes and scripted cameras without nausea | reference | M | #24 | **after MVP** — accepted problem for the MVP | Later |
| F-37 | **Camera = the tracked eye, no neck model**; pitch clamped away from the pole; never cancel an arc the current camera owner did not generate | DVR `[Neck]`, [STANDING_PITCH_ROLL_ARC.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/STANDING_PITCH_ROLL_ARC.md); TF2VR [camera_hook.cpp neck model](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp) (a 0.10 m forward / 0.10 m up neck model, needed only because its positional tracking is a lean *offset* on a camera that does not otherwise translate; with the HMD pose applied in full there is nothing to cancel) | A rule for #24 (Arx has no neck to cancel — do not add one) | reference | S | #24 | **MVP now** | **Take** → #24 (rule) |
| F-38 | **Room-scale**: the character follows the headset (activation 0.125 m, 4 m/s clamp), a drift fix when it creeps with no input, optional fade when movement is blocked | VHVR [VRPlayer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRPlayer.cs), [RoomscalePatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/RoomscalePatches.cs) | #26 | reference | M | #26 | **after MVP** | Later (#26) |
| F-39 | **Near clip 0.09 m** so hands and a raised weapon do not clip at the face | VHVR `NearClipPlane` | A check for #75 | reference | S | #75 | **MVP now** (check) | **Take** → #19, #75 |
| F-40 | **No vignette / tunnelling** in either mod; VHVR's comfort options (immersive ship camera, dodge roll) ship off | VHVR config, DVR README | Supports leaving the vignette out of the MVP | — | — | — | **MVP now** (confirms the plan) | Confirmed — no vignette in the MVP |
| F-119 | **Eye height is one engine quantity**: raise it where the engine defines the eye (their `EyePosition` getter, a vtable slot swap that refuses on a prologue mismatch) so the camera, the weapon, the shot origin and the reticle move together; raising the render camera alone put impacts below the reticle by atan(raise / distance), and writing the view offset per tick fought prediction and "vibrated the gun"; the raise is a **constant** (8 units — the game's eye sits at 83 % of the hull, a human's at 93 %), never a measured height: two runtimes read the same seated wearer 0.3 m apart | TF2VR [camera/player_eye_hook.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/player_eye_hook.h), [camera/player_eye.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/player_eye.h), [camera_hook.cpp "THE MEASUREMENT DRIVES NOTHING"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp) | A rule for #27 and #101: a height trim goes into the player's eye point (what spells, reach and the view all derive from), never into the view matrix alone; D-006's "eye height = the character's camera" is this rule already | reference | S | #27, #101 | **MVP now** (rule) → #27 | |
| F-120 | **Recentre = heading only; three recentre paths; the mid-load trap**: the head pose comes from the gravity-aligned LOCAL space, so pitch and roll are absolute and only the **yaw** needs a reference — subtracting the reference's pitch and roll baked an 18° tilt into every frame that the world never showed (the compositor reprojects with the true pose) but every screen-anchored panel did, "a boat riding a wave" as the body turned; the reference is re-seeded by the runtime's own recentre event, by a key, and **automatically 300 ticks after the world is up** ("a height sampled during the loading screen is wrong for the whole session"); the engine's view pitch is **always** the head's absolute pitch and nothing else writes it (a stick axis that could ratcheted the body pitch to the ±89° clamp and broke aiming) | TF2VR [camera/camera_hook.cpp `AdvanceHeadTracking`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp), [host/plugin.cpp `recentre_on_spawn`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/host/plugin.cpp), [KNOWN-ISSUES 6](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) | #28 (re-seed on the runtime's event and after a load) and #24 (a yaw-only reference, pitch owned by the head) with their traps named | reference | S | #24, #28 | **MVP now** → #24, #28 | |

### Controls

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-41 | **Index binding facts**: menu on a firm trackpad press (`trackpad/force`), grips on `squeeze/value` with 0.70 / 0.55 hysteresis, A/B on both hands, never bind `system/click`, no thumbrest (stick click as the modifier); VHVR: trackpad force 0.3 for menu / inventory, trackpad scroll for the panel | DVR [openxr_input.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/vr/openxr_input.cpp); VHVR [bindings_knuckles.json](https://github.com/brandonmousseau/vhvr-mod/blob/master/Unity/ValheimVR/Assets/StreamingAssets/SteamVR/bindings_knuckles.json); TF2VR [xr/xr_input.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp), [input/xinput_pad.cpp grips](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp) (grips 0.60 / 0.45 on `squeeze/value`; suggestions for `oculus/touch_controller` and `khr/simple_controller` only — no Index profile, so no third Index binding to compare); CLVR (bindings for the Index and Touch only — F-139) | #35 with two shipped Index bindings to compare against | reference | S | #35 | **MVP now** | **Take** → #35 |
| F-42 | **Physical → logical action snapshot** with chords resolved below the remap, motion gestures OR-ed into the same logical actions, one "motion off" switch | DVR [CONTROLLER_BINDS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_BINDS.md), [CONTROLLER_EMULATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_EMULATION.md) | #74's structure; the trigger always still works when a gesture misfires | reference | S–M | #30, #74 | **MVP now** — design for #74 | **Take** → #74 |
| F-43 | **Tap / hold splits on shared buttons** (350 ms tap vs 600 ms hold; 0.3 s hold when a button doubles as a UI click) and "stop gestures on any touch" | DVR F10 chord; VHVR quick menus; TF2VR [input/xinput_pad.cpp Y long-press](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp) (the tap is emitted on *release* — nothing can know a press will stay short; held ≥ 350 ms is the other action and never the tap; the tap is held a few frames so the game's own poll sees it) | Fewer buttons on the Index without misfires | reference | S | #74 | **MVP now** | **Take** → #74 |
| F-44 | **Haptics**: one queue applied on the frame thread, the game's hit/damage events mapped to VHVR's table (hit 0.2 s / 100 Hz / 0.5, parry 0.7 / 100 / 0.3, block 0.2 / 100 / 0.2, stagger 0.1 / 20 / 0.1 for 1.2 s, draw, release, grab, menu tick) | VHVR §6 of the combat notes; DVR [melee.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/melee.cpp); TF2VR [input/xinput_pad.cpp `ApplyMotorsToHaptics`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp), [xr/xr_input.cpp `TriggerHaptic`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp) (a change applied the instant it arrives — the game animates envelopes faster than 40 ms — and a 40 ms sustain tick re-firing 60 ms pulses so they overlap; frequency 160–260 Hz from the heavy / light motor balance; the slider stops at 100 % because the spec clamps amplitude at 1.0 — "a range that cannot act is a lie about what the control does") | #42 with a tested starting table | port the table | S | #42 | **MVP now** — #42 is queued | **Take** → #42 |
| F-45 | **Missing-bindings prompt**: open SteamVR's binding UI when a required action is still unbound 5 s after the controllers appear | VHVR [MissingBindingsPrompt.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/MissingBindingsPrompt.cs) | Testers with other controllers (Quest 3) are not stuck | port | S | #35 | **after MVP** — with #88 | Later (with #88) |
| F-46 | **Controller layout diagrams** (PNG + editable source) shipped with the game and shown in the launcher / panel | VHVR [diagrams](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/diagrams); DVR launcher "Bindings" page | The tester README | reference | S | #35 | **after MVP** — #88 | Later (with #88) |
| F-47 | **Crouch on the right stick's south touch, run toggle on north** (VHVR) versus a toggle button (DVR B) | VHVR bindings; DVR README; TF2VR [CONTROLS.md](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md) (jump on A and crouch on B after the right stick's vertical was found to clip turns; three crouch modes — hold, the engine's own toggle, the mod's toggle — kept "to save a headset run: one ini digit rather than a rebuild") | An alternative for #33 that frees A/B | decision | — | #33 | **MVP now** — decide in #35 | **Decided:** VHVR layout — jump on A, crouch toggle on right stick down (#33, #35) |
| F-121 | **Right-stick maths**: the deadzone is subtracted and the remainder **rescaled** to full travel, then a power curve (`past^2.0`, the maximum unchanged — "linear after a large deadzone spends the travel near the top, which reads as an abrupt ramp"); a turn-speed scale where **70 = the game's own rate** and 0 the slowest usable; **axis dominance** so one diagonal cannot turn *and* crouch — an axis counts only if ≥ 0.6 × the other, with hysteresis on the ratio as well as the magnitude, and a deliberate band where both still fire; separate deadzones per axis (turn 0.45, vertical 0.60) | TF2VR [input/vr_input.cpp "THE RIGHT STICK'S TWO AXES"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/vr_input.cpp), [input/xinput_pad.cpp `ComposeGamepad`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp), [CONTROLS.md](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md) | #31 / #32 smooth-turn feel, and #33's crouch on right-stick-down (F-47) without clipping a turn | port (~80 lines, MIT) | S | #32, #33, F-08 | **MVP now** → #32, #33 | |
| F-122 | **Press and chord rules**: a press that starts and ends between two `xrSyncActions` is caught with `changedSinceLastSync` and delivered once (measured: one of three A presses lost on a 40 fps loading screen); a chord puts **no timing gate on the first press** ("a quarter second of latency on melee is unacceptable, and no menu convenience buys it") — the second press and both releases are consumed until both are up, and *lead with the harmless button*; a button still held when a menu opens or closes is consumed until released (B closed the menu *and* reloaded); a signal read on both sides of a mode switch has **one** edge detector; **one owner per action** (B reached the game by two routes and did two things) | TF2VR [xr/xr_input.cpp (the latch)](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp), [vr_input.cpp "THE L3+R3 SUMMON CHORD"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/vr_input.cpp), [input/xinput_pad.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp) | #74's snapshot and #28's recentre chord without lost or doubled presses | reference; copy the latch | S | #74, #28, #34 | **MVP now** → #74 | |
| F-123 | **Binding liveness**: `xrSuggestInteractionProfileBindings` is atomic — one path the runtime does not define rejects the whole profile, silently — so suggest the richer list first and fall back to a list without the optional path, logging "N bindings suggested → accepted / REJECTED" per profile (the count is the falsifier: 19 without the thumbrest row, 21 with); poll `xrGetCurrentInteractionProfile` per hand every 30 frames and log on change, with `XR_NULL_PATH` explained ("no device for that hand — switch the controller on"); "bound" is `isActive`, not "suggested"; **handedness measured**: the right hand's offset from the left, projected on the head's right axis, must be positive | TF2VR [xr/xr_input.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp) | #30 / #35 on the Index and on a Quest 3 (#87) without a run spent on "the controllers do nothing" | copy + adapt (MIT) | S | #30, #35 | **MVP now** → #30 | |
| F-124 | **Hand-beside-the-head modifier**: the left controller raised to chin height (grip y ≥ head y − 0.15 m) within 0.45 m of the head turns the left stick into a d-pad while held; held means held (a dwell timer was tried and rejected — a deliberate, hesitant use must not expire); a haptic pulse on engage; movement *released*, not frozen; the status line counts "evaluated / skipped: no grip pose / no head pose" so a gesture that never fires says which half is missing | TF2VR [input/dpad_gesture.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/dpad_gesture.cpp) | A shift key on the Index for when the MVP's buttons run out (quick items, a spell wheel) | port (MIT, 250 lines) | S | #74 | **later** — the MVP has buttons to spare | |
| F-139 | **Every gameplay concept is its own action, remapped in SteamVR; posture instead of a button where it fits.** The Better Bindings add-on adds actions to the mod — reload grab, sprint, tactical ability, crouch (toggle *or* hold; a jump while crouched stands up and jumps), grenade toggle — and the player binds them in SteamVR's binding UI; the add-on's own README says "bind the actions in SteamVR" and that it was tested on Index controllers. The mod itself ships full bindings only for the **Valve Index and Oculus / Meta Touch** (per the Linux port's README: other controllers on Monado or WiVRn "may get no input"). **Sprint by posture:** lowering the gun below the eyes while moving sprints, raising it to aim stops — no button. Evidence: add-on, report | CLVR [TF2VRBetterBindings](https://github.com/Fdrph/TF2VRBetterBindings) (MIT), [tf2vr-linux](https://github.com/Monkellie/tf2vr-linux) (MIT) | Two things for #30 / #35: (1) one OpenXR action per concept lets the owner and testers rebind in SteamVR with no rebinding UI of ours — [RESEARCH-controllers.md](RESEARCH-controllers.md) design rules 4 and 6 already say so; this is a released mod where a third party then **added** actions on top, which is the evidence the approach holds (keep crouch and jump separate actions even though the defaults are fixed, F-47); (2) the shipped-bindings priority of a successful release was Index + Touch, i.e. our own order (Index, then Quest 3). The posture idea is optional for run / sneak in Arx (the engine has both) | reference | S | #30, #35, F-41, F-47 | **MVP now** (a rule for #30 / #35); posture sprint is an idea | |

### Hands

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-48 | **Weapon placement from the controller**: per-weapon-class grip offset along the blade, hand centre 0.08 m behind the controller, blade axis and length from the mesh bounds, 180° "ulnar" flip for daggers | VHVR [WeaponWield.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponWield.cs), [WeaponUtils.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/WeaponUtils.cs); TF2VR [ads/ads_probe.h per-weapon grip](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ads/ads_probe.h) (the per-weapon term read from the model's own hand attachment, latched at idle, refused when implausible — F-126) | #75 with the numbers to start from | port | S | #75 | **MVP now** | **Take** → #75 |
| F-49 | **Hand + weapon as one rigid assembly**; per-eye corrections tagged with their eye; per-hand trims stored in the palm frame, edited in the view frame; a second trim for the casting hand | DVR [VR-33-HANDS-AND-WEAPONS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-33-HANDS-AND-WEAPONS.md); TF2VR [core/config.h `NudgePivotOffset`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.h) (the pivot nudge: move what the weapon rotates about without moving the weapon — F-126) | Rules that avoid their first-design bugs (bolt pulled out of the crossbow, hand a whole IPD off) | reference | S | #75, #36 | **MVP now** (rules); trims **after MVP** | **Take** (rules) → #75, #36; trims later |
| F-50 | **Index finger tracking** copied onto a hand mesh's finger bones when the hand is free | VHVR [HandGesture.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/HandGesture.cs) | Real hands after the placeholders | reference | M | #36, a hand mesh | **later** — needs our own hand model | Later |
| F-51 | **Wrist-cut recipe** for the first-person arm mesh (plane square to the bone axis, clip straddling triangles, two-sided flat-colour cap, forearm weights pinned to the hand bone) | DVR [ARM_HAND_SPLIT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_HAND_SPLIT.md); TF2VR [KNOWN-ISSUES.md 3](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) (its hands-only mode still shows the forearm — the gauntlet is one mesh and "hands-only would need a custom model"; the full body avoids the cut) | Floating hands from Arx's existing arm model instead of a new mesh | reference | M | #36 | **after MVP** | Not needed — full body, D-009 |
| F-52 | **Full-arm IK** (shoulders 16 cm back, 25 cm down, 38 cm apart from the head frame; body yaw with a 25° dead zone and 1.5 s relaxation; shoulder slides when out of reach) | DVR [ARM_IK.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_IK.md) (MIT); CLVR (full-body IK as the headline feature of a closed mod — no parameters known, F-135) | Arms, if ever wanted | reference | L | F-51 | **later** | **Take** → #36 — full body with IK arms in the MVP, D-009 |
| F-53 | **"Who owns the body" hand-back**: a read-only ownership flag from the animation state, immediate in, 250 ms out, hands blended 250 ms in / 350 ms out with smootherstep and a straight palm path | DVR [ANIM-HANDOFF-PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ANIM-HANDOFF-PLAN.md) | Knockdowns, death, ladders and scripted moments without hands stuck to the controllers | reference | M | #75 | **after MVP** | Later |
| F-54 | **Inter-hand item transfer** (hands within 0.2 m, forwards aligned, both grips, release one) | VHVR [InterhandItemTransfer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/InterhandItemTransfer.cs) | Left-handed play and torch/weapon swaps without the panel | port | S | #37 | **after MVP** | Later |
| F-55 | **Holster / draw zones** at the shoulders and waist (head-relative bands, release grip to holster, press at the shoulder to draw, no body tracking) | VHVR [Utilities/Pose.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/Pose.cs); CLVR (release the grip to holster — F-137) | Sheathing and bow draw without a belt UI | port | S | #75, #40 | **after MVP** | Later |
| F-56 | **Open empty hand pose** with a 250 ms equipment-state debounce | DVR [HAND_POSE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HAND_POSE.md) | No fist with nothing in it | reference | S | #36 | **after MVP** | Later |
| F-57 | **Physical pickup by reach**: the engine's own "what is under the cursor" test aimed along the hand ray at the nearest item within 60 cm, the grip = the click, time hysteresis (2 frames in, 150 ms out), worst-frame bound on the sweep | DVR [PLAN-physical-interaction.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-physical-interaction.md); TF2VR [hands/use_target.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/use_target.h) (the engine's own use-search wrapped to take the hand's angles — rung 1 — then the hand's origin too — rung 2; every rung counts its calls so a dead stage is named) | Grabbing instead of pointing, with the engine still deciding | reference | M | #37, #38 | **after MVP** — the MVP picks up by ray | ~~**Take** → #37, #38 — grab by reach, no rays (D-010)~~ → **Changed** (D-016, 2026-10-09): aim with a reticle from either hand, the engine's cursor test at the reticle; no grab by reach |
| F-125 | **Hand-to-world composition rules**: position from the **grip** pose, direction from the **aim** pose (a grip-forward barrel sat 45° up); the hand as an offset from a **fixed room reference** (the recentre position), not from the live head — the head pivots about the neck, so a turn translates the eyes and "the gun slid the opposite way"; rotated by the **body's** yaw, never the applied view yaw; the same reference yaw the view uses (else the hand is off the view by the reference after every recentre); composed and consumed **in one frame** — a composition one frame stale in body yaw lagged by exactly the turn rate (8° per frame at full stick) and differed per eye, which was a flicker | TF2VR [hands/hand_pose.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.h), [hand_pose.cpp `ComposeWorld`, `PublishHandPoseForFrame`, "THE YAW RE-BASE"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.cpp) | The IK targets for #36 and the held weapon for #75 placed without the four bugs they paid for; F-21's "one head sample for eyes and hands" extended to the body yaw | reference | S | #36, #75, #24 | **MVP now** → #36, #75 | |
| F-126 | **Live grip calibration**: the grip offset is expressed in the **weapon's own frame** and rotated with it, so it is also the pivot a wrist rotation turns about ("the model origin sits wherever it must"); a separate position offset in the player's frame and angle offsets applied last; a *pivot nudge* moves the pivot without moving the gun (nudge the grip, subtract `R · delta` from the player-frame offset in the same press); the three grip rows move the weapon live in the panel while it is held and save on change; a per-weapon term from the model's own hand attachment, latched at idle and **refused** when further than 9 / 4 / 5 units from the reference | TF2VR [core/config.h `NudgePivotOffset`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.h), [hand_pose.cpp `NudgePivotOffset`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.cpp), [ads/ads_probe.h "PER-WEAPON GRIP"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ads/ads_probe.h), [KNOWN-ISSUES 13](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) | For #75: Arx's weapons already carry their grip vertex (`primary_attach`), so the per-weapon term is data; the one constant to dial is the controller's grip pose → the model's palm (F-49's trims), once, live in the headset | reference | S | #75, F-49 | **after MVP** — the MVP holds the weapon at the model's attachment | |
| F-135 | **Full-body IK — what is actually known, and what is not.** Known (docs, report): it is the headline feature — the author's announcement and the installer README both lead with "full body IK, manual reloads"; the pilot's body is visible when you look down (arvrtips); MoguraVR: it "estimates the whole posture from head and hand movement". **Not known: every parameter** — no source, in code or prose, gives shoulder offsets, body-yaw rule, what the legs do while walking or crouching, arm reach limits or how the body hands back to scripted animations; whether the *legs* are tracked or animated is **unverified** (one search for it found nothing). Evidence: the author's text and two secondary reports; no code | CLVR [README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md), [MoguraVR](https://www.moguravr.com/?p=378357), [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/); the author's announcement ("Fullbody IK, manual reloading, and so much more", seen as a search snippet of [x.com/CircuitLord/status/2105099559391416752](https://x.com/CircuitLord/status/2105099559391416752); the page itself would not load) | Nothing to adopt: it confirms that a **solo-built** mod put full-body IK first and that players noticed it — the same bet as D-009 — and it confirms that the numbers must come from Dishonored VR's `ARM_IK.md` (F-52) and VHVR's VRIK, not from here. Do not wait for more: the mod's behaviour can only be learnt by playing it | reference (none) | — | #36 | **MVP now** — confirms #36; no new input | |
| F-136 | **Manual reload as a body-anchored grab, with a highlight.** B starts a manual reload; the magazine is taken from a zone on the body, inserted, and the slide or bolt racked; guns that need a reload are **highlighted**; the Wingman reloads by a gesture; every weapon has one. The plugin does the steps; the game's scripts only own the clip and reserve counts (the auto-reload add-on moves ammo between them and cancels if a manual reload starts). **Day-one feedback (issue #18, 2026-09-30):** the grab zone "feels somewhat unintuitive to locate" and resembles Half-Life: Alyx's abstract body inventory — the reporter asks for the zone to be shown in a tutorial, a temporary indicator, a bigger zone and optional calibration. Evidence: report, docs, add-on | CLVR [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/), [Road to VR](https://roadtovr.com/titanfall-2-vr-mod-circuitlord/), [issue #18](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/18), [tf2vr-mods AutoReload](https://github.com/djnewage/tf2vr-mods) | Arx has no ammo, so the reload itself is not ours. What transfers is the **rule for any body-anchored zone** (quiver and arrow for the bow F-70, potion and quick-item slots, holster F-55): show it (highlight), teach it once, make it a little larger than feels right, and expect it to be the first complaint | reference (a rule) | S | F-55, F-70 | **after MVP** — with the bow and holster zones | |
| F-137 | **Grip semantics for weapons**: the grip holds the weapon and **releasing it holsters**; the off hand on the front of a rifle **steadies** it (two-handed); raising the gun to the eye aims down its sights; a **closed fist punches** (physical melee punching); trigger fires. The Better Bindings add-on moves "reload grab" to the trigger so the grip only ever holds. Evidence: report (arvrtips), add-on | CLVR [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/), [TF2VRBetterBindings](https://github.com/Fdrph/TF2VRBetterBindings) (MIT) | A second shipped example of release-to-holster (F-55) and off-hand support (F-73). The new bit is the **unarmed punch**: Arx has an unarmed attack, so #39's swing detector could drive it with no weapon in hand — a decision for #39, not a feature to add | reference | S | #39, F-55, F-73 | **after MVP** (holster); the unarmed case is a question for #39 | |
| F-138 | **Distance grab ("force pull") add-on**: an open hand pointed at a weapon or grenade, then a flick toward yourself, and the item flies to the palm. The numbers from its README: targeting cone **3.0 m** long, **0.6 m** radius at the far end (the mod's own stock cone, in `tf2vr_pickups.gnut`, is **1.0 m**); a flick is a hand speed ≥ **2.0 m/s**; the item flies at **8 m/s** — moved by the server with `SetOrigin` to a point before the palm; it hovers **1.5 s** then drops; a second mode holds the point for 0.6 s instead of a flick; line of sight is required; only weapon entities. **Traps it recorded (2026-10-01):** pulls from over 1 m stopped 28–37 cm short of the palm until the flying item was made non-solid; a single-player checkpoint keeps the old server script, so a change takes effect only from a fresh mission. Evidence: add-on README (no licence stated — reference only) | CLVR [djnewage/tf2vr-mods](https://github.com/djnewage/tf2vr-mods) (`TF2VR.DistanceGrab`) | Starting numbers for #37 beyond arm reach (items on a high shelf or a low floor, seated play) and a number for the stock reach to compare with DVR's 60 cm (F-57). **It conflicts with D-010 in spirit:** you *point* at the item — a ray in everything but name — so it needs the owner's call before it is a task | reference | S | #37, D-010 | **after MVP** — and only if the owner accepts it against D-010 | **No** (D-016, 2026-10-09) — items are taken with the hand-aimed reticle and the engine's own drag, not pulled by a gesture |
| F-151 | **Titan cockpit, embark and rodeo**: embark by holding the right trigger, abilities on four grip directions inside a cockpit, boarding the Titan by touch; "touch and board BT up close" is what players praise. Evidence: report | CLVR [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/), [Inven](https://www.invenglobal.com/articles/26827/i-am-jack-cooper-titanfall-2-vr-mod-causes-a-stir) | Nothing: Arx has no mounts or vehicles. Recorded so the catalog does not read as if it were missed | — | — | — | **not applicable** | n/a |

### Combat

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-58 | **Collision-driven hits**: a volume on the blade plus a speed gate call `ARX_EQUIPMENT_Strike_Check` with the volume position as the hit point and the swing velocity as the direction; terrain hits need the grip | VHVR [WeaponCollision.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponCollision.cs), [CollisionPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/CollisionPatches.cs) | #39 — the engine keeps damage, armour and knockback | port | M | #75, F-48 | **MVP now** — #39 | **Take** → #39 |
| F-59 | **Swing decision core** (`edge`: head-relative speed, median of three, fire on the crossing, two-sample re-arm, 4–100 ms dt window, > 20 m/s = tracking jump, one sample per hand pose) | DVR [swing_core.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/swing_core.h) (zlib) | The gate that rejects reaches, body turns and sample lies — tuned over 60 host checks and a headset census | copy | S | #39, F-08 | **MVP now** | **Take** → #39 (copy, zlib) |
| F-60 | **Census and honour instruments**: humps binned by peak speed, attacked / not, NEAR MISS within 20 % of the threshold; "the engine was seen starting the attack" within 600 ms; a `swing sim` that synthesises positions | DVR [PHYSICAL_SWING.md §2](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PHYSICAL_SWING.md) | A threshold chosen from the owner's arm and Index tracking, not guessed | port | S | F-59, F-05 | **MVP now** — before the owner tunes 3 m/s | **Take** → #39 |
| F-61 | **Stab rule and angular lever**: ≥ 0.2 m of thrust in 0.25 s at ≥ 2 m/s along the blade bypasses the speed threshold; the blade's angular velocity counted with a fixed 0.125 m lever so wrist flicks do not trigger | VHVR [WeaponCollision.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponCollision.cs) `isStab`, `hasMomentum` | Daggers in the goblin prison; no false hits from flicks | port | S | F-58 | **MVP now** | **Take** → #39 |
| F-62 | **Per-target cooldown** = the weapon's attack duration, a red outline fading over its second half; horizontal sweeps hit several targets with damage 1, 1/3, 1/6 … | VHVR [AttackTargetMeshCooldown.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/AttackTargetMeshCooldown.cs) | One swing = one hit per target (Arx has the animation lengths) | port | S | F-58 | **MVP now** (cooldown); outline **after MVP** | **Take** → #39 (cooldown); outline later |
| F-63 | **Swing speed → Arx's charge factor**: Arx scales melee damage by hold time; replace it with VHVR's curve (full damage at `cooldown + 5` m/s, a cooldown hit scaled by `timeSinceLastHit / cooldown`) | VHVR `GetSpeedScaledDamageFactor` (off by default there) | A physical reason for the charged attack the MVP removes | port | S | F-58 | **MVP now** — decide with #39 | **Take** → #39 |
| F-64 | **Heavy attack by gesture at contact** (overhead swing from above the head, long horizontal swing, strong stab) | VHVR [RoomscaleSecondaryAttackUtils.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/RoomscaleSecondaryAttackUtils.cs) | — | port | M | F-58 | **probably not** — Arx has no secondary attacks | No |
| F-65 | **Stab stick** (a stabbed target pinned on the blade until withdrawn 0.5 m) | VHVR [StabStick.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/StabStick.cs) | — | port | M | F-61 | **probably not** | No |
| F-66 | **Swing gates**: weapon in hand (an unknown equipment state is "not a weapon"), no menu / panel, the game does not own the body; **the trigger still attacks** | DVR [PHYSICAL_SWING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PHYSICAL_SWING.md) gates | No attacks from reaching into the panel or during a cutscene | reference | S | #39 | **MVP now** | **Take** → #39 |
| F-67 | **Attack source decides the animation**: a trigger attack plays the engine's swing clip on the hand, a physical swing keeps the arm | DVR VR-220 (`ARCHITECTURE.md`) | The first-person animation does not yank a moving arm | reference | S | #75, #39 | **after MVP** — the MVP hides the arms | **Take** → #75, #39 (now, with the full body) |
| F-68 | **Shield block per incoming hit**: shield facing vs strike direction (`dot < −0.5`), parry = the shield was moving > 1.5 m/s, the hit direction rewritten so the engine's own block path runs; modes gesture / grab button / realistic | VHVR [Block/ShieldBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/ShieldBlock.cs), [ShieldPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/ShieldPatches.cs) | #41 | port | M | #41 | **after MVP** — #41 is not MVP | Later — first after the MVP |
| F-69 | **Weapon and fist block** (blade 60–120° to the hit with the grip held; parry when the blade moves > 1.5 m/s across the hit) | VHVR [Block/WeaponBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/WeaponBlock.cs), [Block/FistBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/FistBlock.cs) | Blocking with a sword | port | S | F-68 | **after MVP** | Later |
| F-70 | **Bow**: arrow hand within 0.2 m of the nock starts the draw, 0.6 m full draw, pull² → the engine's draw percentage, the string resists beyond the engine's charge, aim = rest − nock, prediction arc, release on trigger up | VHVR [BowLocalManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowLocalManager.cs) | #40 | port | M | #40 | **after MVP** — #40 is not MVP | Later |
| F-71 | **Bow anatomy from the mesh** (string found by edge length, limbs weighted by height, bend angle from the pull) | VHVR [BowManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowManager.cs), [BowAnatomy.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/BowAnatomy.cs) | A bending bow | port | M | F-70 | **later** — polish | Later |
| F-72 | **Throwing**: trigger release, direction = the 8-sample mean hand velocity, speed curve with 5 m/s full throw, 0.0625 m minimum travel | VHVR [ThrowableManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/ThrowableManager.cs), `WeaponUtils.GetThrowLaunchSpeed` | Throwing items (#37) one to one onto `ARX_THROWN_OBJECT` | port | S | #37 | **after MVP** | **Take** → #37 |
| F-73 | **Two-handed wield geometry** (both grips, rear / front hand from the hand-to-hand angle, per-type offsets, sticky polearms) | VHVR [TwoHandedGeometry.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/TwoHandedGeometry.cs), [LocalWeaponWield.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/LocalWeaponWield.cs) | Two-handed swords and axes | port | M | F-48 | **later** | Later |
| F-74 | **Prior-art thresholds** collected for contact-timed melee (Skyrim VR 2 → ~4 m/s, PLANCK 4.5 arm / 5.0 slash / 2.0 stab, per-target 0.25 s, swing 0.55 s, HL2 VR speed *and* distance) | DVR [PLAN-contact-sword.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-contact-sword.md) | Context for #39's numbers | reference | — | F-58 | **MVP now** (knowledge) | **Take** → #39 (reference) |

### Magic

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-75 | **One `AimRay` function** from the OpenXR aim pose; the laser, the stroke sampler and every spell read it, nothing else builds a ray | DVR [CROSSHAIR_PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CROSSHAIR_PLAN.md), [VR-57-AIM-PIPELINE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-57-AIM-PIPELINE.md) | The laser and the spell never disagree; their grip + 40° path "fought its own coordinate convention" | reference | S | #34, #43 | **MVP now** | **Take** → #43, #44 — invisible (D-010) |
| F-76 | **Spell targeting at the engine's input**: the hand ray replaces the view direction where the engine computes a spell's or projectile's target, never its result; per-spell head / hand switch; one global pitch trim (about −48°) for comfortable pointing | DVR [VR-36-BLINK-RAY.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-36-BLINK-RAY.md), `[Crosshair] OtherItemsY` | Magic Missile goes where the hand points, range and collision stay the engine's | reference | S | #44 | **MVP now** | **Take** → #44 — invisible (D-010) |
| F-77 | **Rune stroke sampling**: arm on press, sample while held, finish on release; a stroke counts after 0.2 m / 400 ms, straightness ratio and forward dot as stroke-quality measures, the plane in a head-yaw-only frame, no stroke while a UI pointer is up | DVR stab core (`swing_core.h`), VHVR `MagicStaffUtils.IsCastTriggerHeld` | #43 / #44 with tested gesture measures | reference | S | #43, #44 | **MVP now** | **Take** → #43, #44 — stroke at the hand (D-010) |
| F-78 | **Haptic progress ticks** while a gesture is being recognised (VHVR's summon: a tick every physics step while the hand keeps rising) and a distinct pulse on recognition | VHVR [SummonerManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SummonerManager.cs) | Feedback while drawing a rune | reference | S | #45, #42 | **after MVP** | Later |
| F-79 | **Staff aim-and-shoot and swing-launch** (fire on release when the tip's peak speed reached 3.5 m/s) | VHVR [SwingableStaffManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SwingableStaffManager.cs), [SwingLaunchManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SwingLaunchManager.cs) | — | port | M | — | **probably not** — Arx has no staff casting | No |
| F-127 | **Aim brace**: `aim = anchor + (hand − anchor) · gain` inside a cone (4°), ramping to 1 at a release angle (12°) beyond which the anchor is dragged, all in the same frame — a gain, never a filter, so nothing is ever delayed; its own falsifier counts "hand moved, gun did not" frames (must be 0) and anchor drift while still | TF2VR [hands/hand_pose.cpp `ApplyAdsBrace`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.cpp) | Steadier Magic Missile aim from the hand (#44, #45), or a bow later, without latency | port (MIT, 140 lines) | S | #44 | **later** — only if #45 finds hand aim too jittery | |

### UI

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-80 | **The one UI panel**: a 1920×1080 texture on a quad (VHVR 3 m tall at 3 m, 1 m up; DVR 1.25 m at 1.3 m for in-game screens), re-facing only when movement starts or the head turns > 75° (50° while moving) at 180°/s, frozen while a menu is open, re-seeded at the head yaw when a menu reopens after > 250 ms | VHVR [VRGUI.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRGUI.cs); DVR [MONO_ANCHOR_UI_STATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/MONO_ANCHOR_UI_STATE.md), [HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md); TF2VR [xr_context.cpp `CaptureMenuPose`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (a quad captured once at summon from the head's yaw only, 1.7 m wide at 1.5 m, facing *back* along the view — the `atan2` sign that read as "angled slightly to my left"; placed at the origin, and said so, when the head pose is invalid at summon — F-128) | #49 with tested placement rules | port the rules | M | #49, #73 | **MVP now** | **Take** → #49 |
| F-81 | **Laser as mouse**: the analytic ray / plane hit fed into the engine's mouse coordinates (the 2D UI untouched); one laser, only while the game wants a cursor; click dead zone 5 mm and 16 px; trigger > 0.5 clicks; the stick's dominant axis scrolls with a 0.25 dead zone per second; those inputs muted for the game | VHVR [VRGUI.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRGUI.cs), [SoftwareCursor.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/SoftwareCursor.cs); DVR [F10_MOTION_CONTROLS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/F10_MOTION_CONTROLS.md) | #34 | port the rules | S | #34, F-75 | **MVP now** | **Take** → #34 — pointer only while a menu is open (D-010) |
| F-82 | **UI state from the engine**: "a menu is open" read from the menu system, loading released by the engine's own completion, never a timer; the panel armed on game state, never on a per-eye tag | DVR [MONO_ANCHOR_UI_STATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/MONO_ANCHOR_UI_STATE.md) | No flicker between panel and world | reference | S | #49 | **MVP now** (rule) | **Take** → #49 (rule) |
| F-83 | **Wrist vitals** (health and mana on the backs of the hands, drawn in-engine at the hand node, fading when not facing the eyes) | DVR [HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md) vitals; VHVR [VRHud.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRHud.cs) | A HUD without a panel | reference | M | epic #7 | **after MVP** | **Take** → #116 — two bars on the left wrist, in the MVP |
| F-84 | **Per-element HUD anchors** (`off / frame / window / world / handL / handR`, with alpha repair and a world window that re-parks per menu) | DVR [HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md); TF2VR [CONTROLS.md "HUD"](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md) (72 settings, about 30 of them HUD placement — a slider per element is what this design costs — F-129) | The full diegetic HUD design | reference | L | F-83 | **later** | Later |
| F-85 | **Hand-held book and notes** with a fixed grip-relative pose, one tilt slider, pages turned by the stick | DVR [HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md) notes; VHVR inventory on the off hand (0.375 m) | The Arx book in the hand | reference | M | epic #7 | **after MVP** | Later |
| F-86 | **Radial hand menu** (seeded at the grip, 15 mm dead zone, 120 mm full travel or a 0.1 m ring, depth does not select, haptic tick on sector change, release selects) | DVR weapon dial; VHVR [QuickAbstract.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/QuickAbstract.cs) | Quick spells and items without the panel | reference | M | epic #7 | **after MVP** | Later |
| F-87 | **World-space crosshair, hover names and enemy health** at the hit distance, scaled by distance; NPC speech bubbles above the speaker | VHVR [CrosshairManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/CrosshairManager.cs), [EnemyHudManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/EnemyHudManager.cs), [NpcTextBubble.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/NpcTextBubble.cs) | Interaction prompts and dialogue off the panel | reference | M | epic #7 | **after MVP** | Later |
| F-88 | **SteamVR keyboard** for text input (save names) in minimal mode with the game keeping its own text buffer | VHVR [TextInputPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/TextInputPatches.cs) | Naming a save without the keyboard | reference | S | #49 | **after MVP** | Later (save names are pre-filled) |
| F-89 | **Cinematics on a flat screen** (video on a world-space quad with letterbox and subtitles) | VHVR [VRCinematicScreen.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRCinematicScreen.cs); CLVR (the same default — F-140) | The intro and in-game cinematics | reference | S | F-80 | **after MVP** | Later (cheap after F-20) |
| F-128 | **Menus driven by the sticks, no pointer at all**: the settings panel is ImGui on a private 3072×2304 texture in its own swapchain, shown on a **world-fixed quad** (1.7 m wide at 1.5 m) whose pose is captured once at summon from the head's *yaw only*, facing back along the view; the sticks feed ImGui's gamepad nav as **levels** (ImGui repeats), ignored until both sticks have returned to centre once after opening; A activates, B backs out, the grips change category, the right stick scrolls; one category at a time, tabs across the top; the footer shows the focused row's help or "N changes pending"; the panel build runs inside an SEH guard that names the row and disables the overlay instead of crashing; the D3D state is saved and restored around ImGui; gameplay input is suppressed while open and the game is **not** paused (opening the game's pause menu for that was measured wrong) | TF2VR [ui/menu_overlay.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ui/menu_overlay.cpp), [xr_context.cpp `CaptureMenuPose`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp), [CONTROLS.md](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md) | The alternative to F-81's laser for #34: a 2D menu that needs no ray at all; Arx's menus are mouse-built, so it costs a keyboard-navigation layer in `src/gui/` | decision | M | #49, #34 | **MVP now** — decide in #34: laser as mouse (F-81) or stick navigation | |
| F-129 | **The flat HUD pass in a headset**: scale it about the view centre to a comfortable angular size (TF2VR ships the pass at **0.28** of the frame, the lower-left group at 0.55, name labels at 0.5) and move the edge elements **inward separately** — a scale about the centre pulls corners in but shrinks them, so size and position need two controls; prompts that were readable on a monitor need enlarging (1.82×, 2.1×); fades and full-screen overlays belong to the head, never to the aim (theirs draw as a rectangle in front of the right hand) | TF2VR [core/config.cpp `kSettings[]` HUD rows](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp), [hud/world_marker.h "THE A/B THAT SHOULD HAVE COME FIRST"](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hud/world_marker.h), [KNOWN-ISSUES 11](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md) | #73's starting numbers: the UI virtual screen smaller than the eye image and centred, with Arx's edge elements (gauges, hotbar, icons) pulled in | reference | S | #73 | **MVP now** → #73 | |
| F-140 | **The smallest set of VR settings a well-received release shipped.** Reported: snap turning in the options; a "VR menu" in the options with **HUD, Titan HUD and gun angle**; a **cutscene mode toggle** — cutscenes on a 2D panel by default, "force 3D cutscenes" off by default because it "can cause odd camera moments" (UploadVR: the full-VR option can cause jerkiness; Inven: cutscenes play in 2D "because of mod limitations"). Nothing else is reported. A guide adds a first-run trap: the **texture streaming budget defaults to the lowest** setting after install and is worth raising, and the per-eye resolution should be lowered in the *runtime* before the in-game graphics. Compare TinyBlackDog's 72 settings (F-19). Evidence: report | CLVR [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/), [UploadVR](https://www.uploadvr.com/titanfall-2s-vr-mod-is-stellar-and-its-installer-is-the-best-thing-about-it/), [Inven](https://www.invenglobal.com/articles/26827/i-am-jack-cooper-titanfall-2-vr-mod-causes-a-stir) | The scope for our post-MVP settings menu is **four things**: turn mode (#32), HUD / UI placement and size (#73, F-129), a hand-to-weapon offset (the "gun angle", F-126), cutscene mode (F-36, F-89). And a rule for the first run: a default that is too low costs more than a missing option — pick the shipped defaults deliberately, on the owner's PC and a lower-end one. Cutscenes: the flat panel as the default is now the choice of two released mods (F-89) | reference | S | #32, #73, F-19 | **after MVP** — the MVP has no VR settings menu; the *defaults* list is MVP | |
| F-150 | **First-use gesture prompts, a controls reference page and VR button glyphs.** Issue #18 (a tester after several hours): "some abilities use VR gestures that aren't obvious" — it asks for a prompt on first use with hand / gesture visuals and a controls / gestures reference page. The mod already replaces gamepad glyphs ("LB", "R3") with VR controller prompts (UploadVR) and highlights guns that need a reload (F-136). Evidence: report, issue | CLVR [issue #18](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/18), [UploadVR](https://www.uploadvr.com/titanfall-2s-vr-mod-is-stellar-and-its-installer-is-the-best-thing-about-it/) | **Runes drawn in the air (#43–#45) are the least discoverable thing we ship.** Plan a first-cast prompt that animates the hand path of the rune, a reference page in the tester README (F-148), and, wherever Arx's own hints name a key or a mouse button, name the controller's instead | reference | S–M | #43, #88, #116 | **README: MVP now** (checkpoint B tester); in-game prompts **after MVP** | |

### Distribution

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-90 | **SteamVR application manifest registered by the game at every launch** (`app_key steam.app.<appid>`, `steam://launch/<appid>/VR`, `AddApplicationManifest` + `IdentifyApplication`, errors only logged) | VHVR [ApplicationManifestHelper.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/ApplicationManifestHelper.cs) | #80 from inside `arx.exe` instead of `register-steamvr.ps1` by hand; the name and art on the loading card | port | S | #89 (done), #80 | **MVP now** — #80 is in the queue | No — keep `register-steamvr.ps1`; Steam does it for a Steam app |
| F-91 | **Support bundle collector** (`Collect-Support.ps1` + `.cmd`): allowlist of `arx.log` history, config, crash reports, SteamVR logs, the OpenXR `ActiveRuntime` value, system facts, the exe hash; a 24 MB cap; `manifest.json`; no upload | DVR [tools/collect-support.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/collect-support.ps1) (MIT); CLVR (a crash-report button with the build hash inside — F-147) | "What to report" for the tester README becomes one double-click | copy (script) | S–M | #88 | **after MVP** — with #88 | **Take** → #88 |
| F-92 | **Log rotation ten deep** with append-and-say-why on failure, a per-run header in the crash file, the banner of F-18 | DVR [log.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/log.cpp), [crash.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/crash.cpp); TF2VR [core/diagnostics.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/diagnostics.cpp) (eight sessions kept, the rotation reported as line 1 of the new file, "ROTATION INCOMPLETE" when a second game instance holds the file; `%LOCALAPPDATA%` rather than `%TEMP%`, which Storage Sense sweeps) | A tester's second crash does not overwrite the first (`arx.log` is written once per run today) | reference | S | #88 | **after MVP** | Later |
| F-93 | **VEH crash fallback** re-armed from the frame loop, named threads, a fault budget our own probes cannot spend | DVR [crash.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/crash.cpp); TF2VR [core/crash_recorder.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/crash_recorder.cpp) (vectored handler first, UEF chained, at most four records so a caught first-chance exception cannot spend the only slot — F-131) | Crash reports that survive the Steam overlay and SteamVR injecting into the process | reference | S–M | — | **after MVP** — first check whether Arx's SEH handler fires under SteamVR | Later |
| F-94 | **External dump capture** with ProcDump attached by PID for freezes (`-e -t -mm`), then the collector | DVR [tools/watch-crashes.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/watch-crashes.ps1) (MIT) | A freeze during the owner's session becomes a dump | copy (script) | S | F-91 | **after MVP** | Later |
| F-95 | **Symbol archive** per tester build keyed by the exe's hash (`arx.pdb` kept with every zip) | DVR `archive-symbols.ps1`, [CRASH_SUPPORT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/CRASH_SUPPORT.md) | Readable crash dumps from a tester | reference | S | #88 | **after MVP** | Later |
| F-96 | **Tester package discipline**: version from CMake so the zip name, log banner and git tag agree; built from a tag; `HOW-TO-USE.txt`; a `.cmd` wrapper for every `.ps1`; a kill switch (`--no-vr`) documented | DVR [tools/package.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/package.ps1), `release/HOW-TO-USE.txt`; TF2VR [tools/package.ps1](https://github.com/kyus2001/titanfall2vr/blob/main/tools/package.ps1), [tools/install.ps1](https://github.com/kyus2001/titanfall2vr/blob/main/tools/install.ps1), [tools/cleanprofile/enter-clean-profile.ps1](https://github.com/kyus2001/titanfall2vr/blob/main/tools/cleanprofile/enter-clean-profile.ps1) (refuses placeholders in the docs, a documented SHA-256 that does not match the DLL, and `.pdb` / `.log` / `.bak` / `.bmp` / `.cache` in the box; `-TestBuild` names the zip TEST; install refuses while the game runs and hashes both sides; a clean-profile run installs exactly the zip before a release); CLVR (no game data in the package — F-145; the hash in the README — F-142) | "Which build did the tester run" answerable from git | reference | S | #88 | **after MVP** — #88 | **Take** → #88 |
| F-97 | **Steam discovery details**: `HKCU` then `HKLM`, both registry views, `libraryfolders.vdf` without a regex, `appmanifest_1700.acf` → `installdir`, a `why` string on failure, a hand-picked folder normalised | DVR [steam.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/installer/sys/steam.cpp), [discovery.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/installer/sys/discovery.cpp); CLVR [SteamGameLocator.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/SteamGameLocator.cs) (the same chain with the game's own registry key first and a regex over the vdf; it never reads `appmanifest` — F-144) | #79 in C++ with the edge cases our PowerShell already handles | reference | S | #79 | **MVP now** — #79 is in the queue | **Take** → #79 |
| F-98 | **Runtime selection logged**: the runtime that answered, where `XR_RUNTIME_JSON` came from, a warning when another runtime answered than expected; no pinned manifest path ever ships | DVR `tools/vr-runtime.ps1`, [TROUBLESHOOTING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TROUBLESHOOTING.md); TF2VR [xr_context.cpp `Initialize`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (runtime name and version, the system name "as the runtime's claim" — VDXR reports a Quest 3 for a Play For Dream — vendorId / systemId, STAGE availability, and the per-eye geometry and IPD in one forced HEADSET line); CLVR (the launcher scrubs an inherited `XR_RUNTIME_JSON` before every launch — F-146) | One less class of "it started flat" support question | reference | S | #17 | **MVP now** (one log line) | **Take** → #115 |
| F-99 | **Installer behaviours** for a non-Steam tester build: refuse while the game runs, atomic writes with backups, idempotent baseline, an install record JSON, self-update under a `sha256` asset contract | DVR [INSTALLER.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/INSTALLER.md); CLVR (a smaller installer with an ownership record and a rename-swap self-update — F-143) | — | reference | M | #88 | **later** — a Steam app needs none of it; a zip does | Later |
| F-100 | **Tester bug-report template**: build id, headset and runtime, refresh rate, "in flat mode?" and "fresh config?" dropdowns, the log attached, no footage; blank issues off | DVR [bug_report.yml](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/.github/ISSUE_TEMPLATE/bug_report.yml) | Reports a worker can act on | port | S | #88 | **after MVP** | **Take** → #88 |
| F-130 | **`KNOWN-ISSUES.md` for testers**: per issue a severity, *what you see*, *workaround*, *where it stands* with a status word (OPEN / FIXED, wearer-confirmed / FIX SHIPPED, UNCONFIRMED / BY DESIGN / NO LONGER OBSERVED — "a defect that stopped appearing rather than one that was repaired"), the measurement so far, and what a report should contain; a table of the five worst in the README | TF2VR [KNOWN-ISSUES.md](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md), [README](https://github.com/kyus2001/titanfall2vr/blob/main/README.md) | The tester README for #88 and the friend's Quest 3 (#87); the honest status words keep a tester's report useful | reference | S | #88 | **after MVP** → #88 | |
| F-131 | **A crash record that attributes**: a vectored handler registered *last* (the front of the chain, ahead of the host's and the overlay's) plus the chained filter; one line with the exception, module + RVA, thread, frames published, and the **arm state as a bitmask the tick publishes every frame** (no getters inside the handler — one took a mutex the crashing thread could hold); the access violation's read / write address; the **XR breadcrumb** — the last XR call we were inside, the textures handed to the runtime this frame, a swapchain epoch — so a crash inside the runtime's DLL with none of our frames on the stack can still be attributed to a stale resource we submitted; a hook-site registry names "PC is in bytes we patched"; `VirtualQuery` describes memory in no module; a linker map ships; a self-test key behind an ini flag faults on purpose | TF2VR [core/crash_recorder.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/crash_recorder.cpp), [diag/xr_breadcrumb.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/xr_breadcrumb.h), [engine/hook_registry.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/engine/hook_registry.h), [CMakeLists.txt `/MAP`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/CMakeLists.txt) | Arx's crash reporter plus the VR state and the XR breadcrumb in the report (#132 D4); SteamVR injects into our process too | reference | S–M | #132, #131 | **after MVP** → #132 | |
| F-132 | **Stall watchdog**: a separate thread that, when a bracketed section has been open for 2 s, suspends the stuck thread, reads its instruction pointer, resolves module + RVA, walks the stack for return addresses inside loaded modules, resumes it and logs — "a probe driven by the thread that stalls cannot report the stall" | TF2VR [diag/stall_watchdog.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/stall_watchdog.h), [stall_watchdog.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/stall_watchdog.cpp) | The hang class our `[E]`-line and crash checks cannot see (D3 / D4): a frozen `arx.exe` names the instruction | copy + adapt (MIT) | S | #131, #132 | **after MVP** → #132 | |
| F-141 | **A separate profile and separate VR saves; an install never touches the base game.** Normal launches stay unmodded: Northstar's launcher is copied beside the original as `Titanfall2VRLauncher.exe`, the mod lives in a `TF2VR/` profile folder, and the VR user data (profile, saves, settings) lives in `%LOCALAPPDATA%\Respawn\Titanfall2_VR` — moved there from Documents because **Controlled Folder Access blocks the launcher in Documents** (code comment, mod 1.0.4+). The installer tests assert the negatives: "install created a VR save" must fail, "default checkpoint changed" must fail, "untracked user file changed" must fail, "uninstall changed VR progress" must fail. **No import of base-game saves**: the current tests assert the "copy save" button is *absent* ("base game save import remains") and v1.2.4 notes "the VR campaign always starts a new save" — two press pages (MoguraVR, PCVR Central) still say progress can be copied; the code and release notes disagree with them. Evidence: code, docs | CLVR [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) (`SaveDirectory`, `Install`), [tests/InstallerValidation/Program.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/tests/InstallerValidation/Program.cs), [releases](https://github.com/CircuitLord/CircuitLordVRModInstaller/releases) v1.2.4 / v1.2.5 | A decision point and a trap for #88 and epic #9. **D-007 keeps Arx Libertatis' user directory**, so flat and VR share `Saved Games\Arx Libertatis` — the owner's flat-mode saves (`--loadslot`) work in VR, which the MVP relies on; Titanfall had a reason to split (its VR campaign flow differs), we have found none. The trap: never write logs, config or saves under Documents / Desktop / Pictures, which Controlled Folder Access protects by default (Saved Games is, as far as we know, not on the default list — check on the owner's PC before relying on it) | reference | S | D-007, #88, epic #9 | **MVP now** — a one-line confirmation of D-007 for VR, no code | |
| F-142 | **A manifest with a SHA-256 per release, release channels, and a test seam.** `manifest-v3.json` (`schemaVersion` 3): the installer `{version, url, sha256, commit}`, the loader, and per mod `{id, version, url, sha256, size, preserve[], tokenize[], channels[]}`. Rules in `RepoClient`: HTTPS only; a 64-hex SHA-256 is required **before** any transfer; the whole payload is hashed in memory and **discarded on mismatch**; the manifest is fetched with a `t=<unix time>` query because raw.githubusercontent caches for minutes; a package is cached on disk as `<sha256>.zip` and a corrupt cache is not reused (tested); the env var `CIRCUITLORD_LOCAL_MANIFEST` points at a local file (and only then may a `file://` URL be used). Older manifests stay frozen so old installers can still self-update. A channel is listed only while newer than stable — in the manifest as read, stable is 1.0.15 and beta 1.0.12, so the beta is hidden. Northstar is pinned in code with URL, hash and size. Evidence: code | CLVR [manifest-v3.json](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/manifest-v3.json), [RepoClient.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/RepoClient.cs), [README "How it works"](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md) | For #88 (one zip for one friend) only the hash discipline carries over: print the zip's SHA-256 in the README and check it in the package gate (F-96). A Steam app gets update channels from Steam **branches** (a beta branch), so the rest is not needed. The environment override as a test seam is worth copying as a habit (F-152) | reference | S | #88, F-96 | **after MVP** — #88 only; Steam branches replace the rest | |
| F-143 | **An ownership record, stale-file cleanup and a zip-slip guard.** Every install writes `.circuitlord-vr-mods/<id>.json` in the game folder with `{id, version, northstarVersion, runtime, channel, custom, files[]}`; an update deletes the files the previous release shipped and this one does not (and prunes emptied directories); uninstall deletes exactly the recorded files and the record; `ResolveInside` throws "package tried to write outside the game folder"; `preserve` entries are recorded as owned but not overwritten if present. The order is the good part: the game-derived assets are built **before** anything in the game folder changes, and a test proves a failed patch leaves the launcher and plugin as they were. **Weak spots in the code, so do not copy it as it is:** files are written in place with no staging or rollback; delete errors are swallowed; a corrupt record reads as "none" (stale cleanup silently does nothing) in the Big Walk path; the record stores no hashes, so a file the user edited is deleted anyway. Evidence: code | CLVR [OwnedFileStore.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/OwnedFileStore.cs), [InstallerFileSystem.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/InstallerFileSystem.cs), [PackageInstaller.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/PackageInstaller.cs), [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) `Install` | If #88 ever becomes "unzip over the game folder", a file list for uninstall is the cheap half; DVR's installer (F-99) is the stricter model (atomic writes, backups). A Steam app needs none of it | reference | S | #88, F-99 | **later** — Steam installs and removes the files | |
| F-144 | **Prerequisites as a checklist with one button each.** The installer shows three steps: (1) *game found* — the game's own registry key `HKLM\SOFTWARE\Respawn\Titanfall2 "Install Dir"` (and its `WOW6432Node` view), then every Steam library from `libraryfolders.vdf` (regex `"path"\s+"(.+?)"`) plus `steamapps\common\Titanfall2\Titanfall2.exe`, canonical casing, else a hand-picked folder that must contain the exe (the Steam root falls back to `C:\Program Files (x86)\Steam`; `appmanifest` files are **not** read); (2) *EA app installed and signed in* — `HKLM\SOFTWARE\WOW6432Node\Origin ClientPath`, opened **visibly** so the sign-in happens on the desktop, and if it is missing `steam.exe -applaunch 1237970` makes Steam install it and link the account; (3) *Install*, then **Launch in VR**, which expects the runtime to be running already. UploadVR calls it "the best selling point" and describes the flow as "simply press the play button with SteamVR open". Evidence: code, report | CLVR [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs), [SteamGameLocator.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/SteamGameLocator.cs), [UploadVR](https://www.uploadvr.com/titanfall-2s-vr-mod-is-stellar-and-its-installer-is-the-best-thing-about-it/) | Epic #9's definition of done — "without Arx Fatalis installed, the app shows a clear message and a link to the store page" — is this checklist with one step: say *what* is missing and give a button for it (the store page, or Steam's install dialog: `steam://install/1700` — an untested assumption), plus "start SteamVR first" when no runtime answers (D-004's flat fallback already runs in that case). Discovery itself is F-97 (#79); this is the message around it | reference | S | #79, #80, epic #9, D-004 | **with #79** (the message); the buttons **after MVP** | |
| F-145 | **No game data in the package: patches applied to the player's own install.** The mod zip (73.9 MB) carries `asset_patcher.exe` and a `patches/` folder; the installer extracts them to a temp folder, runs `asset_patcher apply "<game dir>" "<patches>" "<out>"` (it prints `progress <read> <total>`; the build is 80 % of the installer's progress bar), copies the result into `TF2VR/`, and deletes the temp folder. The code comment: "game derived files ship as patches, the patcher builds them from the installed game and writes them only once every file verifies"; a test asserts the patcher is **not** left installed. Whether the patches are binary deltas or scripts is not visible — the patcher is closed. Evidence: code | CLVR [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) `BuildGameAssets`, [tests](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/tests/InstallerValidation/Program.cs) | The legal shape for anything we might ever need to change in Arx's data (an arm animation, a texture): **ship deltas, build them on the owner's machine from their copy of app 1700, never ship originals** — a pattern epic #9's talks with the rights holders can point to. Today the engine reads the original `.pak` files in place, so we need nothing; the rule belongs in #88's package gate (F-96): the zip contains no game data | reference (a rule) | S | #88, epic #9 | **MVP now** — one line in the #88 gate | |
| F-146 | **Pre-launch checks that name the cause.** Before launching, the installer: (a) **refuses** if `dxgi.dll` or `d3d11.dll` sits in the game root or `bin\x64_retail` — "Another mod replaced DirectX files, which breaks VR rendering. Remove these files and launch again" followed by the paths (v1.2.6); (b) **warns** when Steam, the EA app or the installer itself runs *elevated*, by reading each process token (`OpenProcessToken`, `TokenElevation`; v1.2.9, test seam `CIRCUITLORD_TEST_ELEVATED_APPS`) — the source gives no reason, a plausible one is that an elevated parent cannot hand the game to a non-elevated runtime; (c) probes write access with a file and, if denied (Program Files), raises **one UAC prompt** that grants the user Modify on the game folder, inherited, so installs, launches and the game's own logs work without admin; (d) **scrubs inherited variables** — `TF2VR_*`, `VR_PATHREG_OVERRIDE`, `VR_CONFIG_PATH`, `VR_LOG_PATH`, `XR_RUNTIME_JSON` — and sets its own `TF2VR_OPENXR=1`; (e) writes `launcher.txt` first: UTC time, installer version, OS, each key binary's existence and size, then `monitor_started pid=…` or `launch_error=… win32_error=…`; (f) the headset check lives in the mod (`xr_probe.exe`, v1.2.6 "mods check headset startup themselves"), and v1.2.3 "explains OpenXR launch failures". Evidence: code, release notes | CLVR [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) `Play`, `VrProcess`, [LaunchPermissions.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/LaunchPermissions.cs), [releases](https://github.com/CircuitLord/CircuitLordVRModInstaller/releases) | The OpenGL twin of (a) is **`opengl32.dll`** (ReShade's OpenGL mode, overlay injectors) beside `arx.exe` — not checked against our build. All six are one-line facts for the log banner (#115) before they are a dialog: *proxy DLL found*, *elevated: yes/no*, *XR_RUNTIME_JSON set to …* (F-98), *launched by …*. A user-facing refusal comes with the tester build (#88) | reference; port `IsElevated` (57 lines) | S | #115, #88, F-98 | **MVP now** — banner lines in #115 / #130; dialogs **after MVP** | |
| F-147 | **A parent process as the crash witness, and a one-click crash report.** The game is started *by* `crash_monitor.exe` (arguments: profile folder, launcher path, launch arguments), which writes a capture folder per session — `session.json`, `monitor.txt`, `stderr.txt`, `exceptions.txt`, `stacks.txt`, `memory.txt`, `diagnostics.txt`, `frames.csv`, and `incident.txt` as the "something happened" marker — beside the plugin's own `engine.txt`, `events.txt`, `runtime.txt`, `frames.csv`. The installer's **crash report** button zips the newest session with an `incident.txt`, the latest session, the newest Northstar log, `launcher.txt` and a `report.json` (installer, mod and Northstar versions, **SHA-256 of the plugin DLL**, OS, which session was captured and the DLL hash recorded then) to the Desktop. **Memory dumps are left out so the zip can be shared publicly**; nothing is redacted, so the Windows user name in paths stays (the guide says to review it). The Big Walk side detects a crash with a `GameSession.lock` written at start and deleted on clean exit. Release-week timeline: v1.2.4 game exit reasons, v1.2.7 launcher errors, v1.2.8 session diagnostics were added to the report. Evidence: code, release notes | CLVR [CrashReportService.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Services/CrashReportService.cs), [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs) `CreateLaunchInfo`, [releases](https://github.com/CircuitLord/CircuitLordVRModInstaller/releases) | Complements F-91 (the collector), F-94 (ProcDump) and F-132 (in-process watchdog): a **parent that outlives the crash** works when the in-process handler dies under SteamVR's injection (the doubt in F-93), and the **build hash inside the report** ties a report to the symbols kept by #132 (F-95). Our `launch-lib.ps1` already is a parent for our own launches — the shipped build has none | reference | M | #131, #132, F-91, F-95 | **after MVP** → #132 | |
| F-148 | **What a tester page has to say, from the field.** Collected from the guides and the author's pages: start the runtime *first* and launch **only** through the VR button — any other launch is flat; Quest 3 via Link, Air Link or Virtual Desktop, and for Link set Meta Horizon Link as the active OpenXR runtime; Index, Pimax, Beyond and PSVR2 through SteamVR; lower the per-eye resolution in the runtime before the in-game graphics; the common errors with the fix each — *EA App Not Found*, *can't write to the game folder*, *another mod replaced DirectX files*, *headset not detected → it started flat*, *game can't be found → point to the folder with the exe*, *crashes → crash report, review it before sharing*; start with 15–20 minute sessions (a guide's advice). **What is not said is as telling:** the creator publishes no PC specs and no frame-rate target (VR.org), PCVR Central lists no tested headset, and the claim "any OpenXR compatible headset" sits beside bindings for two controller families. Evidence: report | CLVR [arvrtips](https://arvrtips.com/titanfall-2-vr-mod/), [VR.org](https://vr.org/articles/titanfall-2-vr-mod-how-to-install-circuitlord-northstar-2026), [PCVR Central](https://pcvrcentral.com/mods/titanfall-2-circuitlord-vr), [MoguraVR](https://www.moguravr.com/?p=378357) | The content list for #88's tester README (the friend has a Quest 3 — one line per connection path, one line on the active runtime), and a reminder to state *what we tested* (Index) and what we did not, instead of a blanket claim | reference | S | #88, #87 | **after MVP** — #88 (checkpoint B tester) | |

### Process

| ID | Feature | Source | What it gives us | How to adopt | Effort | Depends on | Fits | Decision |
|---|---|---|---|---|---|---|---|---|
| F-101 | **Non-goals and ordered pass criteria in every task**: flat / simulator checks first, the headset check last with the A/B that would disprove it; "a ticket with no pass criteria cannot be finished, only abandoned" | DVR [LINEAR_AND_GITHUB.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/LINEAR_AND_GITHUB.md) | Our Definition of done, made measurable | reference | S | WORKFLOW.md | **MVP now** | **Take** → WORKFLOW.md, task template |
| F-102 | **PR template sections**: *Evidence* (a measurement; "a falsified hypothesis is a result"), *Levers and defaults* (off unless there is a reason, with a live A/B), *What is deliberately not here* (each fault found and not fixed gets a ticket), "the installed build is the built build" | DVR [PULL_REQUEST_TEMPLATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/.github/PULL_REQUEST_TEMPLATE.md) | *How verified* with numbers; new bugs always filed | reference | S | WORKFLOW.md | **MVP now** | **Take** → PR template |
| F-103 | **One question per headset launch**: a `needs-vr-test` PR states one clearly defined question, the expected result and what each outcome means; the verdict and the build id go on the issue | DVR [AGENTS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/AGENTS.md), review rules | The owner's headset time spent on one judgement, not a tour | reference | S | WORKFLOW.md | **MVP now** | **Take** → WORKFLOW.md |
| F-104 | **`docs/vr/TRAPS.md`**: a dated, never-deleted record of stale-setting faults, instruments that could not fail their own hypothesis, plans tried and failed — written in the same PR as the work | DVR [TRAPS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TRAPS.md); TF2VR (the same traps as dated comments beside the fix and in KNOWN-ISSUES.md — F-130, F-134) | Each worker session starts with what already cost a headset run (our memory notes do this ad hoc) | reference | S | — | **MVP now** | **Take** → TRAPS.md |
| F-105 | **Logging rules for VR lines**: log generously, nothing unbounded per frame (`ONCE` / `EVERY_MS` / `FIRST_N`), log state changes, name the owner before the result, every refused guard says why with the values, an expected zero says so | DVR [CLAUDE.md "Logging"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/CLAUDE.md); TF2VR [core/diagnostics.cpp `KeepWhenQuiet`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/diagnostics.cpp) (quiet by default with an allowlist measured against a real log — 56 of 9,559 lines kept, zero during steady play; identity lines forced; `diag.verbose = 1` for a dev run) | `arx.log` as the instrument every test and every tester sends back | reference | S | #17 onwards | **MVP now** | **Take** → WORKFLOW.md |
| F-106 | **Verification tiers as a working rule**: numeric → flat visual → headset; "asking the user to put the headset on for a question the simulator could answer is a wasted session"; prove each gate can fail once | DVR [VERIFICATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md); TF2VR (every self-check runs a positive control first — a bogus key, a no-flip conversion; "read the counts, not the word PASS": a gate once passed with three empty lists) | The self-test rule in WORKFLOW.md, sharpened | reference | — | F-01…F-08 | **MVP now** | **Take** → WORKFLOW.md |
| F-107 | **Levers and defaults**: every tunable ships off with a live A/B and fails soft; headset-tested values become the shipped defaults as a byte copy; a changed default migrates installed configs once per key instead of resetting them | DVR `CLAUDE.md` hard rules, [TRAPS.md §1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TRAPS.md); TF2VR [plugin/titanfall2vr.ini.example](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/titanfall2vr.ini.example) (the shipped ini documents the wearer's tested values with every line commented out, and `defaults-vs-ini.py` proves the code's defaults equal them — F-15) | Tuning without losing the owner's settings | reference | S | a VR config | **after MVP** — when VR settings exist | Later |
| F-108 | **Batch project update** on the MVP epic (Shipped with the number that proves it, Open, Risks, Blocked, health) | DVR [LINEAR_AND_GITHUB.md "Project updates"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/LINEAR_AND_GITHUB.md) | A 30-second status for the owner from the coordinator | reference | S | `/new-cycle` | **after MVP** | Later |
| F-109 | **Research tasks close with a measurement and a written verdict**, not code; the board is the work, `docs/` is the memory | DVR ticket types | Confirms this task's shape and DECISIONS.md | reference | — | — | **MVP now** (already so) | Confirmed |
| F-110 | **VHVR's process**: none — a maintainer and a community, no tests, no CI, manual headset checks, post-build copy into the game | VHVR repo | Nothing to take; confirms that the harness side has to come from Dishonored VR | — | — | — | **not applicable** | n/a |
| F-133 | **The run's identity in the log**: the config's path, the directive count, an **FNV-1a content hash** and the applied `key=value` list on one forced line ("a run of an unchanged config looks exactly like a successful fix"); every ignored config line named with its line number; the banner forced past the quiet filter with the version tag, the git sha stamped at *build* time (`+` when the tree was dirty — a configure-time sha "announced a commit from hours earlier"), `__DATE__ __TIME__` and the DLL's own write time ("the installed build is the built build") | TF2VR [config.cpp `ParseConfigFile`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp), [host/plugin.cpp `HostOnInit`](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/host/plugin.cpp), [cmake/version_stamp.cmake](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/cmake/version_stamp.cmake) | #115's banner plus the `cfg.ini` hash: a tester's log says which config ran | reference | S | #115, #129 | **MVP now** — one line in #129 | |
| F-134 | **TF2VR's process**: one maintainer in the headset ("the wearer") and an AI agent writing the code; **headset runs are the unit of cost** and are counted ("fourteen headset runs went into this"); every instrument prints a line *either way* and proves it can fail (a bogus key, a no-flip control; "an instrument that reads exactly zero everywhere is broken, not lucky"); "a fix never ships in the same run as the measurement that justifies it"; levers default to the wearer's tested values; the traps are written beside the fix in the code and in KNOWN-ISSUES; the pre-commit gates (`registry-crosscheck.sh`, `scrub.sh`) and `uninstall.ps1` named in BUILDING.md are **absent from the repository**; no CI, no unit tests, offline checks only; a single squashed commit | TF2VR [BUILDING.md](https://github.com/kyus2001/titanfall2vr/blob/main/BUILDING.md), [README](https://github.com/kyus2001/titanfall2vr/blob/main/README.md), the code's comments | Confirms D-014's launch budget and F-101 … F-106; the process shape is Dishonored VR's again, with the same kind of agent | — | — | — | **not applicable** (confirms the plan) | n/a |
| F-149 | **What breaks in the first week of a popular release** — read off the installer's versions v1.2.0 to v1.2.9 in eight days (2026-09-29 to 10-06; the mod itself went from v1.0.8 on one directory page to 1.0.15 in the manifest, within the same week) and the first tester issue. By theme: **launch** (OpenXR launch failures explained, the headset check moved into the mod, DirectX proxies refused, admin warnings, updates run before launch and then made optional), **permissions** (Program Files → one UAC prompt), **saves** (the location moved twice, "always a new save"), **diagnostics** (crash reports extended three times), **packaging** (licence files installed, Northstar cached). Issue #18, opened 2026-09-30 after several hours of play: a **scripted-sequence softlock** (a Data Knife interaction on certain robots returns to the main menu), **stuck after a scripted step** (cannot dismount the friendly Titan), **gesture discoverability**, **grab-zone discoverability** (F-136), and **unexplained crashes in long sessions**. Evidence: docs, issue | CLVR [releases](https://github.com/CircuitLord/CircuitLordVRModInstaller/releases), [issue #18](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/18), [manifest-v3.json](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/manifest-v3.json), [PCVR Central](https://pcvrcentral.com/mods/titanfall-2-circuitlord-vr) | A checklist of where our tester build will bleed, in order: install and launch (F-144, F-146), permissions and save location (F-141), diagnostics (F-147), then **interactions gated by scripts** — dialogue- and cutscene-locked doors and levers, ladders and any state where the engine owns the body (D-009's hand-back is after the MVP) — and **long sessions** (a soak run, F-13). Feeds #70 (which saves to record: one before a scripted interaction, one on a ladder) and #88 | reference | S | #70, #88, F-13 | **MVP now** — the list for #70 / #88 | |
| F-152 | **CircuitLord's process**: a solo developer; closed-source mod, MIT installer; bugs arrive as GitHub issues and on a Discord linked from the README; support through Ko-fi. The installer repo has `tests/InstallerValidation/Program.cs` (602 lines, a console program with `Assert(condition, message)`): every refusal and every "this must not change" has a case — a failed patch must leave the launcher unchanged, an uninstall must not touch VR progress, a stale owned file must go, a corrupt cached download must not be reused — and **environment overrides are the test seams** (`CIRCUITLORD_LOCAL_MANIFEST`, `CIRCUITLORD_TEST_ELEVATED_APPS`) instead of mocks; `repotools/` holds the publish scripts. No CI workflow is visible in the repository listing. Evidence: code | CLVR [tests/InstallerValidation](https://github.com/CircuitLord/CircuitLordVRModInstaller/tree/main/tests/InstallerValidation), [repotools](https://github.com/CircuitLord/CircuitLordVRModInstaller/tree/main/repotools), [README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md) | Nothing new for WORKFLOW.md: it is F-106 again ("prove the gate can fail") and the same habit D-014 chose for the launch scripts — a refusal without a test that makes it fire is a claim. The one transferable habit: a guard's test setup is an environment variable, so the production code path is the one tested | — | — | — | **not applicable** (confirms the plan) | n/a |

## Detailed notes per area

### 1. Testing without a headset

Dishonored VR is built around the rule in
[docs/VERIFICATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md):
three tiers of evidence — **numeric** (a number from a log or a file, asserted by a script), **flat
visual** (a screenshot or compositor capture, compared by `img-diff`), **headset** (a human) — and
"push every question as far up this list as it will go; asking the user to put the headset on for a
question the simulator could answer is a wasted session". That is exactly our situation (one owner,
one headset), so this area got the most attention.

#### 1a. The simulated OpenXR runtime `dvr_xrsim32.dll`

Source: [src/tools/xrsim](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/src/tools/xrsim)
(11 files, about 250 KB of C++, MIT via the BioShock trilogy mod). A complete OpenXR runtime with no
hardware behind it: it presents as a Meta Quest 3 (2064×2208 per eye, measured VDXR FOVs, IPD 63 mm),
implements instance/system/session/spaces/swapchains/frame loop/actions/haptics, composites the
submitted layers per eye into PNG captures, and is driven from a file.

- **Selection without touching the machine:** the OpenXR loader reads `XR_RUNTIME_JSON` before the
  registry, so the simulator is selected per process by setting that variable to a manifest
  `{"file_format_version":"1.0.0","runtime":{"name":"dvr-xrsim","library_path":"<absolute path>"}}`
  ([tools/xrsim-install.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-install.ps1)).
  The real runtime stays active for headset sessions. Traps they hit: an elevated shell (the loader
  ignores the variable there), a wrong-bitness DLL (silently skipped, the registry runtime answers
  and "every later measurement is taken against the wrong runtime"), a BOM in the manifest, a relative
  `library_path`. Their launcher therefore **asserts the runtime name** from the game log
  (`xr: runtime "dvr-xrsim"`) before anything else — "the single most valuable check".
  Our `src/vr/VRSystem.cpp` already resolves the manifest the same way (`XR_RUNTIME_JSON`, then
  the registry) and logs it.
- **The rig** ([xrsim_common.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xrsim/xrsim_common.h)):
  one flat struct — head pose, grip and aim pose per hand (optionally following the head with an
  offset), sticks, triggers, squeezes, A/B/X/Y/menu, stick clicks, thumbrests, IPD, world scale, FOV
  per eye. The control thread stages a copy and the frame path swaps it in at one point inside
  `xrWaitFrame`, so a multi-line command lands as one change and never tears across a frame
  (`FrameSnapshot` in [xrsim_internal.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xrsim/xrsim_internal.h):
  everything a frame is built from is published once and read unchanged by `xrLocateSpace`,
  `xrLocateViews`, `xrSyncActions` and `xrEndFrame`).
- **Commands** ([xrsim_control.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xrsim/xrsim_control.cpp),
  written to `command.txt`, acknowledged through `cmdSeq` in `state.json`): `head pos|rot|pose|move|
  movelocal|turn|to|orbit|height|valid`, `recenter`, `hand l|r grip|aim pose|pos|rot|point|to`,
  `hand l|r follow head|offset|aimtrim|valid`, `btn a|b|x|y|menu down|up|press [ms]`, `click`,
  `thumbrest`, `trigger|grip l|r pull [ms]`, `stick l|r x y|center`, `pace free [hz]|step|turbo`,
  `step on|off`, `refresh <hz>`, `idle`, `state <session state>`, `focus lose|regain|policy|norender|
  throttle`, `hazard nosystem|waitfail|beginfail|endfail|swapchainfail|attachfail|clear`,
  `instanceloss`, `ipd`, `worldscale`, `fov`, `shot [name]`, `capture next|every|size`, `reset`,
  `status`. The timed forms (`head to`, `hand r to grip x y z … <ms>`) move over N milliseconds —
  the only way to produce a swing, because a teleported hand is discarded by the swing detector as a
  tracking jump.
- **Pacing and fault injection:** `free` (a fixed Hz gate in `xrWaitFrame`), `step` (the game
  advances one frame per granted credit), `turbo`. The invariant "NO WAIT IN THE SIM IS EVER
  UNBOUNDED" (30 s starve grant, 20 s idle max) so an abandoned step mode leaves the game slow, not
  hung. `Hazards` make known runtime faults repeatable: no system, the next N `xrWaitFrame` /
  `xrBeginFrame` / `xrEndFrame` fail with `XR_ERROR_SESSION_LOST`, swapchain creation fails, action
  set attach fails, instance loss. `FocusPolicy` reproduces measured runtime behaviour around
  `FOCUSED`: VDXR will not re-grant FOCUSED to an app that submits nothing, and once parked a session
  at VISIBLE for minutes while the app submitted empty frames — the class of problem #99 will meet
  with SteamVR and an idle Index.
- **What it writes:** `state.json` at 20 Hz (frame counters, session state, pace mode, layers last
  frame, head/hand poses, controls, errors, `quadLayers`, `capNonBlackL/R`, per-eye release age
  `eyeAgeL/R`, `eyeSameSwapchain`, `projSubmits/projMonoSubmits/projStaleSubmits`, `endPhaseMs`)
  and per shot `capture\<name>_left.png`, `_right.png`, `_sbs.png` plus `<name>.json` (layers with
  type/space/size/pose, `derived.eyeSeparationM`, `derived.claimRatioH`, `stats.meanLuma`,
  `stats.nonBlackPct`, `stats.bboxL/R`, and per layer the *source* image statistics). `xrEndFrame`
  validates every projection view (unit quaternion, ordered FOV edges under 178°) and logs a bad one;
  an eye that composites black while its source is not is logged as `COMPOSITOR fault`, a black
  source as `APP fault` — added after a real session where the simulator itself had a bug.
- **Self-test:** [tools/xrsim-selftest.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-selftest.ps1)
  runs [xr_hello32](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xr_hello32/main.cpp)
  — a 150-line OpenXR client with no mod in it (extensions, instance, system, device, session, 60
  empty frames; exit 0 full pass / 1 loader or runtime failure / 2 no headset) — against the
  simulator in a scratch directory: "a failure here means the sim is broken and the mod is not the
  suspect".
- **Handles carry a generation counter** so a stale handle returns `XR_ERROR_HANDLE_INVALID` instead
  of dereferencing freed memory; every entry point is an SEH shim, so a fault inside the simulator
  degrades to an error code instead of taking the game down. Not modelled: lens distortion,
  timewarp, real display cadence, Wi-Fi encode, guardian — "a pacing bug that reproduces in the sim
  is real; one that does not may still exist on VDXR".

**Fit for us:** the simulator is D3D11-only (`XR_USE_GRAPHICS_API_D3D11`) and 32-bit; our engine is
64-bit OpenGL. 64-bit is a compile switch. OpenGL means implementing `xrGetOpenGLGraphicsRequirementsKHR`,
swapchain images as `XrSwapchainImageOpenGLKHR` (GL texture names created in a context shared with
the application's `HGLRC` from `XrGraphicsBindingOpenGLWin32KHR`), and a GL readback for the
captures instead of the D3D11 compositor. The control channel, actions, pacing, hazards, `state.json`
and the script runner are graphics-agnostic. A minimal port (per-eye captures, no quad-layer
compositing) is **M**; parity with their compositor is **L**. MIT, so the port may keep their code.

#### 1b. The scripted harness

- [tools/xrsim-run.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-run.ps1)
  runs a `.xrs` file: one simulator command per line, `;` groups commands into one atomic batch (a
  head and a hand that must move together cannot be two writes), and directives `@wait <ms>`,
  `@frames <n>`, `@shot <name>`, `@mod <cmd>` (to the game's own command channel), `@assert <key>
  <eq|ne|gt|ge|lt|le> <value>` on the simulator's `state.json`, `@modassert <a.b.c> <op> <v>` on the
  game's `status.json`, `@fps <min> [secs]`, `@key <name> [n] [ms]` (scancode injection into the
  game window), `@mark`, `@log <regex> [<n>s]` (wait for a game log line written since the mark;
  consecutive `@log` lines assert an *order*), `@nolog <regex>` (fail if a line matched — "a swing
  that must NOT fire"). The game log is read as Latin-1 so a text offset is a file offset.
- 26 sequences in [tools/xrsim](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tools/xrsim):
  `smoke` (session FOCUSED, a layer submitted, a non-black capture), `mono` (both eyes ≥ 10 %
  non-black with equal bounding boxes), `stereo` (two projection views; left vs right `img-diff`
  well above the 0.4 standing-still noise floor), `headlook` (yaw 0 vs 35 moves the picture),
  `world-6dof` (yaw ±30°, pitch ±20°, 0.4 m translations, a crouch to 1.25 m: `claimRatioH` ≈ 1.0 at
  every pose, eye separation constant, parallax concentrated on near geometry), `coupling-hand`
  (the weapon's bounding box moves with the hand, not the head), `unfocused-pacing` (`@fps 60`
  across FOCUSED → VISIBLE → FOCUSED), `stale-eye`, `pause-resume`, `swing-edge/soft/gates/stab/
  plunge/anim`, `snap-turn`, `camshake`, `camspring`, `hud-*`. An example from
  [snap-turn.xrs](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim/snap-turn.xrs):
  four stick pushes at 30° must read 120° on the view *and* the body and about 0° on the head, a held
  stick fires once, a 0.4 push does nothing, the pause menu blocks it, and `snapturn off` returns
  the axis to the game's smooth turn.
- The game side: a **command seam** — [tools/game-cmd.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/game-cmd.ps1)
  writes `command.txt`, the mod polls it from its frame hook at 1 Hz (a trap: back-to-back writes
  overwrite each other, so `@mod` groups with `;` and waits a poll period) and answers in
  `status.json`; a `[game] state: GAMEPLAY|MENU|LOADING|CINEMATIC` log line that
  [tools/boot.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/boot.ps1) waits
  for while pressing Enter every 3.5 s through the title and the menu (we do not need that:
  `--loadlevel` / `--loadslot` start in the level); `tools/game-key.ps1` (scancodes),
  `tools/game-shot.ps1` (`PrintWindow`), `tools/img-diff.ps1` (mean absolute difference and
  per-cent changed), `tools/soak.ps1 -Minutes N` (exit 2 = log stalled, 4 = process died),
  `tools/tail-log.ps1`, `tools/log-parse.ps1`, `tools/watch-crashes.ps1`.
- Confounds they record: an unfocused game window stops rendering the world (captures show the HUD
  on black — foreground before every shot); the agent's shell virtualised writes under the user
  profile so a Steam-launched game could not see the files the harness wrote (fixed by a data
  directory outside the profile); `XR_RUNTIME_JSON` is per process, so the game must be started
  directly, not through Steam.

#### 1c. "The two eyes are really two pictures" and the pacing verdicts

Both live in the mod, not the simulator, and both are cheap log instruments we can reproduce:

- **`stereo: frameid`** (once a second, one left/right pair sampled every 8 ticks): per pair, how
  different the two eyes are at each stage of the pipeline (for them: game backbuffer, shared slot,
  eye texture, swapchain image) against a *same-eye floor* (on the simulator 4.1 / 4.1 / 4.1 / 4.8
  against a floor of 1.5), the camera step between the two draws (`c5 |d| 6.17 uu along right: side
  ok`) and the picture's own parallax sign (the right eye's content must sit *left* of the left
  eye's — a positive shift means the eye tags rode the wrong draw). Found their worst bug: eye
  tags paired to presents by order broke whenever the game thread ran ahead, swapping the eyes
  "spontaneously every ~2 s", invisible to every flat check.
- [tools/eye-check.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/eye-check.ps1):
  five legs with pass bands — the pairing beat (`stereo: beat L/s R/s` both flowing within 80 % of
  each other), a projection layer with eye separation ≈ 0.063 m, left-vs-right `img-diff` inside a
  calibrated band, a 25° yaw moving *both* eyes, back inside the band. Their negative control showed
  that a mono picture presented at two layer poses reads *inside* the image band — only the
  pairing-rate leg catches "both eyes render the same world".
- **`stereo: rate`** every 3 s: `hmd=8.33 ms (120 Hz) slots/s | presents/s submits/s | endFrame
  mean/max` and a verdict — **OVER-SUBMITTING** (more submits than display slots: the runtime
  blocks), **MATCHED** (the `xrEndFrame` mean *is* the pacing wait, only its max is a hitch),
  **UNDER-SUBMITTING** (slots go unfilled: the stalls are upstream) — plus a cadence verdict:
  `EVEN CADENCE: 2.00 display slots per frame` or `UNEVEN CADENCE: 1.13 slots, sd 1.83 ms`, which
  is seen as doubled edges on head turns. Lesson from their known issues: at 120 Hz their frames
  landed 1.05–1.11 slots apart and ghosted; 90 Hz fixed it — "a higher refresh rate is WORSE here".
  Also `perf: frame gap` (fires on its own at 80 ms or 2.5× the mean interval and names the phase
  the gap sat in) and `mark <text>` (stamps a felt freeze in the log with the ring of presents
  around it).

#### 1d. Host tests of pure decision cores

About 90 pairs `tools/<name>-host.ps1` + `tools/<name>-tests.cpp`: a pure decision core (the
motion sword's [swing_core.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/swing_core.h)
takes *positions and timestamps*, not speeds, and has 60 checks: the threshold, one swing one
attack, re-arm and cooldown, a body turn, repeated poses, doubled samples through the median, the
tracking-jump and dt hygiene, the thrust) is compiled standalone with a tiny harness, no game
needed. Their 2026-10-05 audit found six suites that "had rotted silently — they did not compile,
so they guarded nothing"; the rule since: "a suite that does not compile is not a passing suite:
run it, read the last line". We have the same shape available for free — `tests/` with CppUnit
(`arxtest`, `BUILD_TESTS`) — so every VR decision core (swing detection, rune projection, snap
turn, crouch threshold) can get its unit test there and run in CI.

#### 1e. Alternatives for an OpenXR runtime without a headset

| Option | What it is | OpenGL | Scripted head/hands | Verdict for us |
|---|---|---|---|---|
| **Port `dvr_xrsim32`** to 64-bit + OpenGL | Their simulator with GL swapchains | after the port | yes, full (poses, inputs, pacing, hazards, focus policies) | the only option that scripts the *runtime* side; **M** minimal, **L** full |
| **In-engine simulated VR backend** (our own; inspired by their rig and `.xrs`) | `vr::` split into an interface; `--vr-sim <script>` feeds scripted poses and action states into the same engine code, renders the per-eye FBOs offscreen, writes captures and a status file | yes (it is our renderer) | yes, for everything the engine does with poses | cheapest, **S–M**, runs in CI even as a service (no runtime, no window focus); does not exercise the OpenXR plumbing — cover that with an `xr_hello` probe and the headset |
| **SteamVR null driver** | The real SteamVR runtime with a virtual HMD: `drivers\null\resources\settings\default.vrsettings` → `"enable": true`, and in `Steam\config\steamvr.vrsettings` `"steamvr": {"activateMultipleDrivers": true, "forcedDriver": "null"}` (`requireHmd: false` is disputed — reported to break initialisation) | yes (the same path as the Index) | no (a static HMD, no controllers) | **S**, zero code: "does the game start in VR and shut down cleanly on the real runtime" for #99; SteamVR must run on an interactive desktop and be restarted to switch (`vrpathreg setconfig <dir>` can point SteamVR at a test config directory) |
| **An OpenVR driver from Valve's `samples/driver_sample`** (BSD-3) | A virtual HMD plus two controllers whose poses and buttons are fed from a file, through the real SteamVR | yes | yes | **M**; scripted testing on the real runtime, but SteamVR-specific and still needs an interactive desktop |
| **Monado on Windows** (BSL-1.0) | Open-source OpenXR runtime; the Windows port "is not ready for widespread usage due to some rough edges and lack of drivers" ([doc/winbuild.md](https://gitlab.freedesktop.org/monado/monado/-/blob/main/doc/winbuild.md)); only simulated drivers, a windowed Vulkan compositor, `XR_MND_headless`; selected via `XR_RUNTIME_JSON` like xrsim | listed for the project in general; unverified on Windows | needs a driver written in C | **probably not**: the same work as the xrsim port on a less controllable base |
| **Meta XR Simulator** | Meta's Windows OpenXR runtime simulating Quest headsets, with session capture/replay for CI and a CLI | **no** — "OpenGL and OpenGL ES are not supported" (official docs; Vulkan, D3D11, D3D12 only) | replay of recorded sessions | **not applicable** to our OpenGL renderer |

Two facts about our own setup that decide the shape: the CI runner is a Windows *service* (session 0,
no interactive desktop), so anything that needs a game window, OpenGL on the GPU or SteamVR cannot
run from CI as installed today — it runs on the owner's desktop session (a worker session, or the
runner re-registered as an interactive process); and SteamVR with an Index nobody wears goes to
standby, so the session may never reach `FOCUSED` — the exact class of behaviour xrsim's
`FocusPolicy` models, and what #99 step 1 has to measure first.

#### 1f. What VHVR does for testing

Nothing automated: the repository has no `.github/` directory, no test project and no CI; every
build is copied into the game directory by a post-build script and verified by hand in the
headset. Two things are still useful: the **flat-screen companion mode** — `flatScreenMode=auto`
runs in VR only if SteamVR is already running, every patch starts with `if (NonVrPlayer()) return`
and any initialisation failure falls back to the flat game with the same DLL (our D-004 and
`--no-vr` are the same policy, which keeps the flat smoke test meaningful for VR code paths) —
and the command-line overrides (`-flatScreenMode=`, `-UseVRControls=`) that make a scripted
launch deterministic.

#### Recommendation (also catalog rows F-01 to F-12)

1. **Now, for #99 (S):** the flat smoke test as planned, plus two cheap seams borrowed from their
   harness: a file-polled **command channel** in the engine (feeding the existing script console
   `src/gui/Console.cpp` and a small VR vocabulary: `status`, `recenter`, `shot`) that answers in a
   `status.json` (game state, frame counters, VR session state, per-eye frame ids), and a 64-bit
   OpenGL **`xr_hello` probe** built from our tree that checks the runtime without the game (exit
   code 0 / 1 / 2 as theirs). The SteamVR null driver is the no-code way to run the VR smoke with
   no headset; measure first whether an idle Index already reaches `FOCUSED`.
2. **With the stereo work (#18–#19, S–M):** an **in-engine simulated VR backend** (`--vr-sim`)
   driven by a `.xrs`-style script (port
   [xrsim-run.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/xrsim-run.ps1),
   MIT, to `scripts/dev/`), with offscreen per-eye captures, `@assert` on `status.json` and
   `@log`/`@nolog` on `arx.log`; and the two log instruments — the **frame-id / parallax-sign line**
   and the **rate / cadence verdict** — because they catch the two bugs a flat screenshot cannot.
   Every VR decision core (swing, rune projection, snap turn, crouch) gets a CppUnit test in
   `arxtest`.
3. **Later, when a runtime-level bug needs reproducing (M–L):** port `dvr_xrsim32` to 64-bit
   OpenGL for the hazards, focus policies and pacing modes; or the OpenVR sample driver if we want
   scripted poses through SteamVR itself.
4. **Not:** Meta XR Simulator (no OpenGL), Monado on Windows (work in progress, no advantage over
   the port).

### 2. Melee

Two opposite designs. **Dishonored VR** keeps the game's attack system and only decides *when*
to press the attack from the controller's motion ("the swing decides WHEN, the game decides
WHERE"); **VHVR** removes the attack button entirely and lets a trigger volume on the weapon hit
whatever it touches fast enough, then calls the game's damage code itself with the contact
position and the swing direction. Our #39 ("hits driven by controller speed and trajectory
instead of charged attacks") is the VHVR shape; Dishonored VR's detector is the better *gate*
(what counts as a swing) and its verification is the better *method*.

#### 2a. VHVR: collision-driven hits

Sources: [WeaponCollision.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponCollision.cs),
[PhysicsEstimator.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/PhysicsEstimator.cs),
[WeaponUtils.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/WeaponUtils.cs),
[AttackTargetMeshCooldown.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/AttackTargetMeshCooldown.cs),
[RoomscaleSecondaryAttackUtils.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/RoomscaleSecondaryAttackUtils.cs),
[Patches/CollisionPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/CollisionPatches.cs),
[FistCollision.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/FistCollision.cs),
[StabStick.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/StabStick.cs).

- **No attack button.** The game's `Attack` / `SecondAttack` inputs are ignored
  (`VRControls.cs:1066-1067`); the attack animation is skipped by running the animator at
  1000× speed for the local player (`FixPatches.cs:216-255`). The weapon stays on the hand bone
  (VRIK drives the hands to the controllers) and is re-posed every render frame by per-weapon
  offsets.
- **The weapon volume:** a box trigger on the blade, hand-tuned per item in mesh-local units
  (`WeaponUtils.cs:17-207`) with a bounds-based estimator as fallback (`EstimateWeaponCollider`,
  `WeaponUtils.cs:627-657`: tip = bounds centre + pointing × half length; collider length by type —
  sword `max(0.125, tipDistance − 0.15)`, default 75 % of the tip distance). The pointing axis and
  the weapon length come from the mesh bounds (`EstimateWeaponDirectionAndLength`,
  `WeaponUtils.cs:398-433`): the local axis with the largest extent from the hand is the blade.
- **Speed:** hand velocity is the runtime's own tracked velocity (`Hand.GetTrackedObjectVelocity`);
  the weapon volume's velocity is an 8-snapshot finite difference in *rig space* (room-scale
  motion only, character locomotion excluded), with a 16-entry sparse ring every 4th physics step
  for "how far did the hand travel in the last 0.25–1 s" (`PhysicsEstimator.cs:10-12, 58-89,
  184-200`). The angular contribution uses a **fixed 0.125 m lever** instead of the real blade
  length (`WEAPON_ANGULAR_WEIGHT_OFFSET`, `WeaponCollision.cs:23`) — "so wrist flicks with long
  weapons do not trigger".
- **The gate** (`hasMomentum`, `WeaponCollision.cs:662-712`): `speed > SwingSpeedRequirement`
  (config, **3 m/s** default, 0–8; two-handed weapons × 1.5) **or a stab** (`isStab`,
  `WeaponCollision.cs:737-770`): thrust of ≥ 0.2 m within 0.25 s, within 30° of the blade (40°
  two-handed), ≥ 2 m/s along the blade — a stab bypasses the speed threshold. Two-handed:
  direction = normalised sum of both hand velocities, speed = the larger projection. Fists: 45 %
  of the threshold (1.35 m/s), jabs within 30° of the fist at > 3 m/s.
- **Contact → hit** (`MaybeAttackCollider`, `WeaponCollision.cs:343-377`): on trigger enter (and
  on trigger stay only for stabs, so a thrust can start while already overlapping), with a hostile
  target not in cooldown: the hit point is the **volume's centre** (not the contact point), the
  hit direction the velocity, then the game's own `Attack.Start` is called; its prefix
  (`CollisionPatches.cs:30-160`) replaces the melee entirely — zero attack range/offset, stamina
  once per attack window, `doMeleeAttack` builds the engine's `HitData` from the weapon's damage ×
  skill × level and applies it. Knockback direction defaults to "away from the player";
  swing-direction knockback is an option.
- **Per-target cooldown** (`AttackTargetMeshCooldown.cs`): a hit target cannot be hit again for
  the weapon's *attack animation duration* from a hard-coded table (`swing_longsword 0.63 s`,
  `swing_axe 0.64`, `knife_stab 0.49`, `spear_poke 0.63`, `greatsword 1.13`, `swing_sledge 2.15`,
  `unarmed_attack 0.54`, default 0.63; `WeaponUtils.cs:297-323`), shown as a red outline that
  fades over the second half. Non-horizontal attacks hit one target at a time; horizontal sweeps
  hit several with damage decaying 1, 1/3, 1/6, 1/12 … (`calcDamageMultiplier`). Speed-scaled
  damage is **off by default** (`MomentumScalesAttackDamage`: full damage at `cooldown + 5` m/s;
  a hit inside the cooldown at > 0.25 s since the last gets `timeSinceLastHit / cooldown`).
- **Heavy attacks by gesture at contact** (`RoomscaleSecondaryAttackUtils.cs`): axe = overhead
  swing (sagittal speed ≥ `clamp(1.5 × threshold, 3, 9)`, 1 m wind-up in 0.5 s starting above
  the head); club = long horizontal swing (1.25 m in 0.5 s within 45° of the strike direction);
  sword = strong stab (> max(threshold, 3) m/s with 1 m of thrust in 1 s); fists = hook; dual
  axes = both hands closing at ≥ 1 m/s each, sum ≥ 4.
- **Stab stick** (`StabStick.cs`): a successful stab on a non-boss target of mass ≤ 500 pins it
  on the blade; released when withdrawn 0.5 m or moved 0.75 m sideways. Feedback: weapon outline
  red when the attack is unavailable (no stamina, target in cooldown), yellow during the
  post-heavy countdown; haptic `(0.2 s, 100 Hz, 0.5)` on every applied hit.
- **Terrain hits need the grip held** (one-handed) or the dominant hand behind (two-handed) to
  stop accidental ground strikes.

**Fit for us:** Arx's `ARX_EQUIPMENT_Strike_Check` is VHVR's `doMeleeAttack`: keep its damage,
armour and knockback code, but call it from a weapon volume + speed gate instead of the
animation timeline, with the volume position as the hit point and the swing velocity as the
direction. First numbers to copy: 3 m/s, the 0.125 m lever, a per-target cooldown equal to the
weapon's attack duration (Arx has the animation lengths), the stab rule for daggers.

#### 2b. Dishonored VR: the motion sword

Sources: [docs/dishonored/PHYSICAL_SWING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PHYSICAL_SWING.md),
[src/game/dishonored/swing_core.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/swing_core.h),
[src/game/dishonored/melee.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/melee.cpp),
[docs/dishonored/PLAN-contact-sword.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-contact-sword.md),
[docs/dishonored/PLAN-physical-interaction.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-physical-interaction.md),
[docs/dishonored/CHOKE_GESTURE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CHOKE_GESTURE.md).

- **The swing adds a press, it never replaces the trigger.** A pure decision core (`swing_core.h`:
  hand and head positions plus a timestamp in, a verdict out, 75 host checks) decides *when*; the
  attack input is then pressed for `PulseMs` (120 ms) and the game plays its own attack where its
  view and melee assist point. Confirmed by a haptic tick (`0.7`, 80 ms) on fire and a softer one
  (`0.35`, 40 ms) when the game is seen starting the attack (`melee.cpp:263, 406`).
- **The `edge` detector** (shipped since 2026-09-20): hand speed from two successive hand samples
  with the **head's own movement subtracted** (`HeadRel=1` — a body turn with `rel off` fires),
  passed through a **median of three** (`Median=1`, one sample of latency, ~11 ms at 90 Hz — a
  repeated pose reads 0 and the next carries two frames of travel), firing **the instant the speed
  crosses `EdgeSpeed`** so the game's wind-up lands where the arm is going (not at the peak). A fire
  clears an arm latch that re-arms only after **two consecutive readings** under `RearmSpeed`;
  `CooldownMs` bounds a shake. Guards in the core: sample pairs closer than 4 ms or further than
  100 ms produce no speed; a speed over 20 m/s is a tracking jump (controller re-acquired from
  behind the body) and re-seeds. The feed is **one sample per hand-sample generation, not per
  frame**: "on a real runtime two presents in three carry no new hand pose", and feeding per
  present read three scripted swings as ten 4–39 ms flicks of 8–14 m/s that fired nothing.

  | `[Melee]` key | Shipped | Meaning |
  |---|---|---|
  | `EdgeSpeed` | **3.0 m/s** (3.6 until a softer-swinging player reported misses) | hand speed that is a swing |
  | `RearmSpeed` | 1.0 m/s (≤ 0.9 × EdgeSpeed) | how slow the hand must get to re-arm |
  | `CooldownMs` | 300 | between attacks |
  | `PulseMs` / `PulseMinPolls` | 120 / 2 | the press, held until the game polled the pad twice |
  | `EdgeTravelM` | 0 (off) | a travel guard "decides nothing": a real swing has travelled only 0.15 m when it crosses 3 m/s |
  | `HonourMs` | 600 | window to see the game start the attack |

  "3.6, 1.0, 300 and 120 are the sibling BioShock mod's headset-tuned numbers. They describe a
  player's arm, not an engine, which is why they may cross games at all." Headset run of
  2026-09-20: 47 fires at 3.70–6.35 m/s (median 4.18), honoured 15–109 ms after the fire.
- **The hump census** (the tuning instrument): every excursion of decision speed above the re-arm
  level is a *hump*, binned by peak in 0.5 m/s steps and split attacked / did-not-attack, printed
  once a minute; "`EdgeSpeed` belongs in the gap between the two lists". A hump within 20 % under
  the threshold with the gates open is a **NEAR MISS** and counted; a hump cut short by tracking
  loss is reported `CUT SHORT`, never called a near miss. The F10 panel shows the player's peak,
  near misses and the slider.
- **Gates** (fail closed; the first closed gate is named in the log): gesture on; a gameplay view
  (pause, loads, cutscenes and the power wheel count as UI); the sword is in the hand (the attack
  input with an empty hand would *draw* it; an equipment read older than 1 s is "unknown, and
  unknown is not a sword"); no grip held (block); the overlay down; the game does not own the body
  (takedown, climb).
- **The honour check:** after every fire the mod watches the game's animation state for `HonourMs`
  and logs `HONOURED slash` / `HONOURED kill` / `NOT HONOURED … BLOCKED instead` (the install's
  binding put block on that input) — "a verified write is not an honoured one".
- **The animation belongs to the trigger, not the swing** (VR-220): a trigger attack hands the
  right hand to the game's swing clip; a physical swing keeps the arm — "the player's arm IS that
  animation, and pinning a moving arm to the clip would yank it". The game's sword trail is hidden
  because it follows the animation, not the blade.
- **The stealth stab** (`Stab=1`, armed only while crouched): a thrust is 1.5–2.5 m/s over
  20–35 cm and never crosses a slash threshold, so it is judged on four bars at once — start speed
  1.5 m/s, 0.20 m of extension within 400 ms, straightness ratio 0.75 (extension gained / path
  travelled), forward dot 0.5 — with the shoulder modelled from the head **yaw only** at 0.17 m
  right, 0.22 m down, 0.04 m back (used for direction only: "measuring hand-to-shoulder distance
  would read a head turn as extension"). Default style `plunge` (a fist raised to the shoulder and
  driven down, 20° forward tilt) because the sword sits in a reverse grip in the headset.
- **Contact-timed sword** (VR-173, research only, not built): run the engine's own trace along
  the *physical* blade and press the attack when a fast blade reaches an enemy; the game still
  owns damage, parry, combos and kills; an air swing does nothing so the threshold can come down.
  Prior art they collected: Skyrim VR 2.0 m/s hand speed (community raises it to ~4 because
  walking triggers it), PLANCK arms at 4.5 m/s and classifies contact 5.0 slash / 2.0 stab with
  direction dot 0.8, tip speed `|v_hand + ω × r_tip|`, per-target cooldown 0.25 s, swing cooldown
  0.55 s, velocity smoothed ~55 ms; Jedi Knight VR 2.0 m/s; HL2 VR crowbar: minimum speed *and*
  distance plus cooldown.
- **Physical pickup** (`PLAN-physical-interaction.md`): reach a hand to a lootable item and squeeze
  the grip; **the engine decides** — the mod aims the engine's own interaction trace at the nearest
  item within reach (60 cm, books 74 cm, doors 75 cm), the engine focuses, highlights and prompts,
  and the grip becomes a 130 ms Interact press only while the engine's focused actor is the target;
  eligibility has *time* hysteresis (2 ready frames in, 150 ms out). Carried objects are held at the
  hand and thrown along the hand's aim with the game's own release speed. Bound the *worst* frame
  of the sweep, not the mean (0.32 ms/frame with occasional slow frames starved an eye).
- **Choke gesture** (a model for any "hand at a body landmark" gesture, shipped off): the right hand
  within 0.14 m of the left shoulder (head-yaw-only landmark 0.17 m left, 0.22 m down, 0.04 m
  back) having moved at ≥ 0.9 m/s within the last 400 ms; held while within 0.22 m (hysteresis).

#### 2c. What to take for #39 / #75

1. **The hit decision:** VHVR's shape — a weapon volume + a speed gate calling the engine's strike
   code with the contact position and the swing direction — because we own the engine and Arx
   already resolves a strike at one call. Dishonored's contact-timed plan says the same thing from
   the other side.
2. **The gate:** Dishonored's `edge` core — head-relative speed, median of three, crossing not
   peak, two-sample re-arm, dt and tracking-jump hygiene, one sample per hand pose — plus VHVR's
   stab rule and the 0.125 m lever. Start at 3 m/s and *measure* with a hump census before tuning.
3. **The instruments before the numbers:** the census / near-miss line, the honour check
   ("the engine was seen starting the attack"), a `swing sim` that synthesises *positions*, and a
   host test of the core (section 1d).
4. **Per-target cooldown** = the weapon's attack duration (VHVR); haptic on fire and on hit.
5. **Keep the trigger working** as a plain attack (Dishonored), gate swings on "weapon in hand,
   no menu, game does not own the body".

### 3. Shield, blocking and parry

VHVR only (Dishonored's block is the right grip held, a plain button). Sources:
[Block/Block.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/Block.cs),
[Block/ShieldBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/ShieldBlock.cs),
[Block/WeaponBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/WeaponBlock.cs),
[Block/FistBlock.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/Block/FistBlock.cs),
[Patches/ShieldPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/ShieldPatches.cs).

- **No block stance.** Blocking is decided *per incoming hit*, when the damage call reaches the
  player (`Character.RPC_Damage` prefix, `ShieldPatches.cs:198-231`), from where the shield /
  weapon / fists are and how fast they move. The trick that reuses the engine: the prefix of the
  game's `BlockAttack` **overwrites the hit direction** with `−player.forward` when VHVR says
  "blocked" and `+player.forward` otherwise, so the game's own facing test always agrees
  (`ShieldPatches.cs:25-87`); `IsBlocking` is replaced by the OR of the three detectors.
- **Shield** (`ShieldBlock.cs:95-119`): shield facing = `−leftHand.right`; blocked when
  `hitDirection · shieldFacing < −0.5` (within about 60° of facing the hit; `−0.25` in Realistic
  mode) — or, if the shield is moving faster than 1.5 m/s *across* the hit, `< 0.5`. Three modes:
  `Gesture` (default), `GrabButton` (grip held = block), `Realistic` (the hit's path, a sphere cast
  with a radius from the damage type — blunt and push widen it, pierce narrows it, `Block.cs:
  254-294` — must intersect the shield's box).
- **Parry** = the shield (or weapon) was *already moving* > 1.5 m/s when the hit landed
  (`CheckParryMotion`, `ShieldBlock.cs:121-135`): the first sample over 1.5 m/s resets the game's
  block timer to 0 and it runs on from there (× 0.5 in Realistic, × 0.75 in Gesture, so the window
  is stretched); the attempt ends when the speed drops under 0.2 m/s. A parry is therefore the
  game's own "perfect block" window measured from the start of the motion, not a timed press.
  Gesture-mode blocks are followed by a 1 s cooldown (`Block.cs:12-15`).
- **Weapon block** (`WeaponBlock.cs:38-65`): the hit direction 60–120° from the blade and the
  grip held; **in Gesture mode a one-handed weapon blocks only when held with both hands**
  (`allowBlocking`, `LocalWeaponWield.cs:408-421`); parry when the blade's velocity
  perpendicular to the hit exceeds 1.5 m/s. **Fists** (`FistBlock.cs:42-87`): both grips held,
  both hands raised in front within the sagittal band; parry at > 1 m/s.
- **Shield size** is rescaled (`ShieldScale` 1.0, capped by `MaxShieldWidth` 1.5 m, "about 0.3 m
  gives a realistic size") and shrinks to 40 % while two-handing or aiming a throw.
- **Haptics** (`ShieldPatches.cs:94-122`): parry `(0.7 s, 100 Hz, 0.3)`, block
  `(0.2 s, 100 Hz, 0.2)`; stagger = both hands `(0.1 s, 20 Hz, 0.1)` every frame for 1.2 s.

**Fit for us (#41, after the MVP):** Arx resolves an NPC strike on the player in one damage
call too, so the same shape works: test the shield's facing against the strike direction
(`dot < −0.5`), count it as a parry when the shield hand moved > 1.5 m/s in the last quarter
second, and feed the result into Arx's existing block/armour code. The rewritten-hit-direction
trick is the cheap way to reuse the engine's own block path.

### 4. Bow and arrows, crossbow, throwing, spears

VHVR only (Dishonored's crossbow is a one-handed, hand-aimed gun; see area 5 for the aim ray).
Sources: [BowManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowManager.cs),
[BowLocalManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/BowLocalManager.cs),
[Utilities/BowAnatomy.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/BowAnatomy.cs),
[CrossbowManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/CrossbowManager.cs),
[ThrowableManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/ThrowableManager.cs),
[SwingLaunchManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SwingLaunchManager.cs),
[SpearWield.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SpearWield.cs),
[TwoHandedGeometry.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/TwoHandedGeometry.cs),
[Patches/BowAndFishingPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/BowAndFishingPatches.cs).

- **Bow anatomy from the mesh** (`BowManager.cs:145-241`, `BowAnatomy.cs:32-124`): the string is
  found as the mesh triangles with an edge ≥ 0.965 mesh units; their extreme vertices along the
  bow's up axis are the string ends; vertices are weighted to three bones (upper limb, handle,
  lower limb) by height. Per-bow fallback tables exist (handle height 0.624, string radius 0.008,
  string ends at `(0, ±0.75, −0.325)`), and a bend shader when the mesh is unreadable. Bending per
  frame: `bendAngle = asin(min(1, pullDelta × 1.25 / stringLength))`, limbs rotated ± that angle;
  the string is a 3-point line renderer (top, pull point, bottom).
- **Nocking and drawing** (`BowLocalManager.cs:141-154, 272-389`): the arrow is equipped by a
  back-reach gesture (or automatically in one-handed mode) and attached to the arrow hand; the
  draw starts when the arrow hand is within **0.2 m** of the nock point while trigger or grip is
  held; the bow rotates so the handle-to-hand line is the shooting axis (`rotateBowOnPulling`);
  pull = the hand's distance behind the rest along that axis, clamped to `[braceHeight,
  fullDrawLength]` with **`BowFullDrawLength` 0.6 m** (0.2–0.8); `realLifePullPercentage = (pull
  / (fullDraw − brace))²`. Default mode `Full` makes the string physically refuse to go beyond
  the game's own charge progress (max pull = `lerp(brace, full, √vanillaCharge)`), so a fast
  pull still waits for the engine; `None` lets the player over-draw at extra stamina. Aim =
  `arrowRest − nockPoint`; a 20-point prediction arc at `v = lerp(vMin, vMax, pull)` with 0.1 s
  steps. Full draw → one haptic on the bow hand.
- **Release** (`BowLocalManager.cs:326-364`): trigger/grip up on the arrow hand → the draw
  percentage is handed to the game (`GetAttackDrawPercentage` prefix/postfix) and the game fires;
  `Attack.GetProjectileSpawnPoint` is redirected to the arrow rest and the aim direction;
  `FireProjectileBurst` drops character facing and launch angle; accuracy stays tied to *time*
  charged, not pull length (`BowAccuracyIgnoresDrawLength`). Haptics: arrow hand
  `(0.1 s, 75 Hz, 0.9)`, bow hand `(0.2 s, 100 Hz, 0.3)`.
- **Crossbow** (`CrossbowManager.cs`): a two-handed wield with a fixed inter-grip distance
  (0.35 m), aim = weapon forward, fires on the rear hand's trigger, manual reload by pulling the
  string with hard-coded anatomy (`CrossbowAnatomy.cs`).
- **Throwing** (`ThrowableManager.cs:127-297`): grip held, trigger pressed to "prepare", trigger
  released to throw. Direction modes: `Classic` = the 8-sample mean hand velocity (default),
  `DartType` = hand position now minus at trigger press, `TwoStagedThrowing` = the hand's aim
  at the press, `SecondHandAiming` = headset → other hand. Speed along the throw =
  `max(instant, averaged)` hand velocity incl. a 0.125 m hand-tip lever; a release that moved
  < 0.0625 m in the last 0.4 s is ignored. **Launch speed curve** (`WeaponUtils.cs:688-731`):
  1:1 with the hand up to `FullThrowSpeed` (5 m/s), flattening into the weapon's max by a cubic
  (`s³ × (max − full) + handSpeed`, `s = hand / full`); the game multiplies the aim vector by its
  own max speed, so VHVR hands over `direction × (launch / max)`. Spears: the same manager, with
  the spear turned toward the aim direction while preparing (`TwoHandedGeometry.cs:401-437`).
- **Swing-launch** (`SwingLaunchManager.cs`): staffs that fire from the tip are launched by
  swinging with the trigger held — peak speed ≥ 3.5 m/s at trigger-up, draw percentage =
  `speed / 5`.
- **Two-handed geometry** (`WeaponWield.cs:164-191`, `LocalWeaponWield.cs:218-340`): both grips
  held and the hands within half the weapon length along the blade enter the two-handed state;
  which hand is behind comes from the angle between the hand-to-hand vector and the blade
  (< 60° / > 120°); the weapon then points from the rear hand centre to the front hand centre
  with a per-type preferred offset (default: the rear end in the rear hand when the hands are
  far apart, the grip in the front hand otherwise), the hand centre being 0.08 m behind the
  controller's forward (`HAND_CENTER_OFFSET`). "Sticky" two-handing (default for polearms) keeps
  the state until both grips are pressed again with the hands within 0.2 m.
- **Sheathing by zones** (`Utilities/Pose.cs:656-797`): head-relative bands — shoulder band
  −0.25…+0.125 m, waist band −0.75…−0.375 m below the headset, behind the body by the sagittal
  offset; *release* the grip in a zone to holster, *press* the grip at the shoulder to draw, with
  a haptic `(0.2 s, 100 Hz, 0.3)`. No body tracking needed.

**Fit for us (#40, after the MVP):** Arx's bow is already a charged attack, so VHVR's "real-life
pull percentage → the engine's draw percentage" port is natural (0.2 m nock radius, 0.6 m full
draw, aim = rest − nock); mesh-derived bending is optional polish. Arx's throw takes a position,
direction and velocity, so the trigger-release model with the 5 m/s curve maps one to one.
Sheathing zones are a cheap way to holster weapons and bows without a belt UI.

### 5. Magic

Neither mod draws anything in the air: Dishonored's powers are *aimed* with the hand, VHVR's staffs
are *aimed and swung*. What carries over to rune casting (#43–#45) is the ray, the gesture
sampling and the guards.

- **One ray** (Dishonored, [CROSSHAIR_PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CROSSHAIR_PLAN.md),
  [VR-57-AIM-PIPELINE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-57-AIM-PIPELINE.md)):
  "anything that claims to point where shots go derives from the identical ray. The crosshair, the
  projectile, Blink, any assist and any debug draw read one source. Two derivations disagree by a
  small amount that is invisible until a player misses a shot they were told they would hit." One
  function owns `struct AimRay { ok, hand, origin, dir, gen, why }` from the OpenXR **aim pose**
  (`/input/aim/pose`, where the controller points), a second one converts it to game space, and
  nothing else may build either. Measured: the grip pose points 74–84° up when the arm is
  extended; **aim-vs-grip is a constant 60°** on Touch; their legacy "grip pose + 40° + flip
  flags" path "was fighting its own coordinate convention" and sent shots up and behind.
- **Inject at the input of the engine's own aim computation, never at its output**
  ([VR-36-BLINK-RAY.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-36-BLINK-RAY.md)):
  Blink's hook swaps the pointer the engine reads its aim vector from, so range, trace, collision
  pull-back and destination stay the engine's work along our ray — "the marker and the landing
  point cannot disagree, because there is only one trace and the engine performed it". Writing the
  destination after validation was their teleport-through-walls bug. A reach limit learned from the
  mod's own output ratcheted 1100 → 5606 units in minutes; a setting "can only shorten what the
  engine offered, never lengthen it".
- **Per-item head/hand aim** with hand as default, and one global "comfortable pointing angle" on
  the shared ray for non-gun items: they needed about **−48° to −53° of pitch** so that a naturally
  held controller feels pointed where the player means (`[Crosshair] OtherItemsY=-48.00`).
- **VHVR staffs** ([MagicStaffUtils.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/MagicStaffUtils.cs),
  [SwingableStaffManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SwingableStaffManager.cs),
  [SummonerManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/SummonerManager.cs)):
  aim-and-shoot = hold the trigger, the projectile leaves the staff tip along the staff (two-handed)
  or the hand's ray (one-handed; by default also needs the grip, `AllowSimpleMagicAttack=false`);
  swing-launch = arm on trigger press, sample the tip's velocity while held (8-snapshot estimator),
  fire on release if the peak reached 3.5 m/s, draw percentage = `speed / 5`; the summon gesture =
  the free hand raised steadily (vertical speed > 0.25 m/s, never dropping below its running
  maximum) for 1 s cumulative, with a haptic tick `(0.1 s, 50 Hz, 0.3)` every physics step while
  rising, one summon per motion. **Every cast is guarded by "no laser pointer up"**, so a UI click
  never casts. The projectile spawn point is `rayStart + dir × |attack offset|` (× 0.6 for
  non-summons).

**What to take for rune drawing (#43–#45):**

1. One `AimRay` function in the VR module (aim pose, not grip), consumed by the laser on the UI
   panel, the stroke sampler and every projectile spell; the rune's 2D plane and Arx's existing
   recognizer (`src/game/magic/`) take the *projection* of the controller position onto a plane in
   front of the player, as #44 plans — Dishonored's head-yaw-only shoulder/landmark frame is the
   right frame for that plane (a head turn must not become a stroke).
2. "Arm on press, sample while held, fire on release" (both mods), with a stroke that starts only
   after a minimum travel (Dishonored's 0.20 m in 400 ms and the straightness ratio are ready-made
   stroke-quality measures) and the "no UI pointer up, no menu" guard.
3. Spell targeting through the engine's own targeting input, never a mod-side destination; a
   per-spell head/hand switch and one global pitch trim for comfortable pointing.
4. Haptic progress ticks while a gesture is being recognised (VHVR's summon), a distinct pulse on
   recognition.

### 6. Rendering and camera

#### 6a. Per-eye drawing (Dishonored VR) versus our #71 / #72

Dishonored VR cannot render in-engine, so it climbs a **stereo ladder**
([docs/ARCHITECTURE.md, "The stereo ladder"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/ARCHITECTURE.md)):
rung 1 `mono` — the game's frame on a head-locked quad in both eyes, which proves the whole path
(capture, swapchain, compositor, pacing, head tracking) before any stereo exists; rung 2 `aer` —
alternate-eye rendering (each tick renders one eye, the compositor holds the other eye's last
frame; half temporal rate per eye, never shipped); rung 3 `reentry` — the engine's viewport draw
root is called a *second time per tick* with the other eye's camera, the shipped method. That is
the same shape as our plan: simulate once, draw per eye (#71). Their lessons that carry over:

- **Pose-matched submission.** Each eye image reaches the headset with the head pose it was
  drawn for (the `XrView` from `xrLocateViews` at that frame's `predictedDisplayTime`), and the
  runtime reprojects by the delta to the display-time pose; a stamp that disagrees with the pose
  the render used cancels the motion ([docs/RESEARCH.md, "OpenXR from a 32-bit process"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/RESEARCH.md)).
  Their worst bug was eye images paired to the wrong draw; the fix was to pair by a measured
  camera step, not by submission order ([KNOWN_ISSUES](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/KNOWN_ISSUES.md)).
  For us, drawing both eyes inside one frame from one `xrLocateViews` call avoids the class
  entirely — but the *instrument* (section 1c) is still worth having.
- **Rung 1 first.** Before #18/#19 produce stereo, a head-locked quad with the flat frame in both
  eyes (a `XrCompositionLayerQuad`, 2.4 m wide at 1.75 m in their default) validates swapchains,
  the frame loop and the desktop mirror with one readback; it is also what they show for menus
  and loading screens later (area 8).
- **Cadence over rate.** One render per display slot; a frame that runs long misses its slot and
  waits a whole period. Their shipped size was chosen to fit a 90 Hz slot, and a 120 Hz headset
  *ghosted* because frames landed 1.05–1.11 slots apart. The rate/cadence verdict line (1c) and
  the `[VR] FpsCap` even-cadence limiter (a cap that divides the refresh rate) are the tools; our
  epic #2 asks for "the headset's native rate", which should read "an even cadence at the native
  rate or a clean divisor of it".
- **The neck.** The engine pitched its camera about a pivot 32 cm below and 6 cm behind the eyes;
  they cancel it with the measured pivot (`[Neck] Mode=cancel`, headset-judged right) so looking
  down does not swing the camera forward. Arx's camera is a point with no neck model, so we only
  have to *not add* one (D-006: HMD orientation on the character's camera position).
- **World scale** is a measured constant (`[PosTrack] Scale`, 100 uu/m for UE3). Ours is #27.
- **Effects at a comfortable depth.** Rain, blood splashes and the low-health vignette are drawn
  at depth instead of flat on the eyes (README "Seeing the world"). The analogue in Arx is the
  screen-space layer: damage flashes, the fade to black, the magic-mode tint, fog and flares
  (MVP "accepted problems") — put them on the UI quad at panel depth or draw them per eye with the
  eye's projection, never as a flat full-screen quad per eye at different offsets.
- **Not relevant to us:** the readback pipeline (`GetRenderTargetData`, 16 ms of GPU copy per
  present at Quest 3 size), the D3D9Ex device with managed-pool shadows, the frame capture modes,
  AFW/"mod spacewarp" (filling missed slots on the PC with the mod's own reprojection from depth
  and motion vectors — a response to Virtual Desktop's SSW smearing on a wireless link) and DLSS in
  a 64-bit helper process. We render natively; our performance work is the engine's own
  (#8 epic).

#### 6b. Camera and comfort

Sources: Dishonored [README](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/README.md),
[crouch.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/crouch.cpp),
[snap_turn.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/snap_turn.cpp),
[release/dishonored_vr.ini](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/release/dishonored_vr.ini),
[STANDING_PITCH_ROLL_ARC.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/STANDING_PITCH_ROLL_ARC.md),
[CINEMATIC_HEAD_TRACKING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CINEMATIC_HEAD_TRACKING.md),
[PLAN-VR-165-camera-displacement.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-VR-165-camera-displacement.md),
[HEAD_MOTION_120HZ.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HEAD_MOTION_120HZ.md);
VHVR [VRPlayer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRPlayer.cs),
[EyeRotationPatch.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/EyeRotationPatch.cs),
[ControlPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/ControlPatches.cs),
[FadingManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/FadingManager.cs),
[VHVRConfig.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/VHVRConfig.cs).

- **Physical crouch.** Dishonored: the camera follows the real eye height (positional tracking),
  and a head drop of `CrouchDropUU=20` (~20 cm) held `CrouchHoldMs=250` *presses the game's crouch
  button*; the stance is then **read back** from the collision capsule ("the cylinder is the
  referee": 65 crouched / 87.5 standing, sampled every 250 ms, silent for 800 ms after a press
  because the capsule resizes a beat late, overriding only after three agreeing readings — "truth
  that is a moment late beats truth that argues mid-motion"); whether the button toggles or holds
  is *measured* from whether the view stays down after release. VHVR: sneak when the eye height
  falls under **70 %** of the calibrated height (`RoomScaleSneakHeight`), released above 75 %
  (5 % hysteresis); a stick crouch lowers the camera by 0.4 m, a physical one does not. For us
  (#100, D-006): the HMD drop sets the engine's crouch flag directly; take the 250 ms hold and the
  hysteresis, and keep "eye height trim and world scale are judged together" (Dishonored tuned
  `HeightOffsetM=0.060` and `Scale=108` in one session).
- **Eye height.** VHVR clamps the measured eye height to **[1.2, 1.9] m** against a wrong SteamVR
  floor and moves the character's eye point to match within [−0.75, +0.25] m; the character model
  is scaled 0.9. Dishonored keeps the engine-bounds clamp off (`EyeClamp=0`) and lets a ceiling
  (low clearance) remove real vertical motion. For the MVP the eye sits on the character's camera
  height (D-006), so the clamp matters only for #101.
- **No neck, no bob, no shake.** Dishonored's engine pitches its camera about a pivot 0.321 m
  below and 0.062 m behind the eye (17 cm of backward travel at +30° pitch); they cancel it with
  the measured pivot, computed in the yaw frame so a rolled head at steep pitch does not sweep an
  arc, and "never cancel an arc that the current camera owner did not generate" (an animated
  cutscene camera has no arc). Camera shake is removed **at its source per kind** (`[CameraShake]
  Suppress=1`: walk bob and roll off, landing dips and hit jolts off, the weapon kick and the
  stair smoother kept) rather than cancelled at the seam; the "camera that leaves the body" bug
  was the engine's collision-pop smoother reading back the camera position the mod had already
  offset and never converging — any engine smoothing that reads the camera back will fight the VR
  offset forever: apply the HMD pose *after* all engine camera smoothing, or disable the
  smoothing (#76).
- **Head-based movement.** Both mods ship "stick forward = where you look": Dishonored's
  `HeadBasedMovement=1` (the native facing follows the view heading; its decoupled "character"
  mode kept a 5–45° drift after scripted scenes and stayed open), VHVR's
  `JoyStickForwardDirection=LookDirection` with the HMD yaw delta added to the body yaw and the rig
  counter-rotated (options: left/right controller, body). Our #25 decouples head and body; the
  decision to make early is which frame the *stick* uses — both references say head.
- **Turning.** Both ship smooth turn by default and snap turn off. Dishonored: a snap step is
  written as **body yaw** where the head writer writes the view (not a synthetic stick pulse, which
  depends on sensitivity and frame timing; not a pawn rotation write, measured futile), 45° per
  push at 0.6 stick, re-arm at 0.3, exactly once, eaten from the stick only while the writer owns
  the view (menus keep the game's smooth turn); logged FIRED / APPLIED / HONOURED. VHVR: 45°,
  optionally "smooth snap" at 10° per 10 ms step, re-armed when the stick returns, the X axis
  ignored when |x| < |y|, turn inversion option. For #32: snap = a one-shot body yaw delta on the
  player entity (the thing locomotion, hands and the UI anchor derive from).
- **Room-scale and positional tracking.** VHVR moves the character with the headset
  (`CharaterMovesWithHeadset=true`; activation 0.125 m, speed clamp 4 m/s; an optional fade to
  black when the body moved less than 60 % of the intended) and a drift fix snaps the character back
  when it creeps with no input. Dishonored applies lean/crouch/room-scale head displacement along
  the camera's basis, up to `MaxMeters=0.80`. For us: post-MVP (#26).
- **Recenter.** Dishonored: both stick clicks held 0.6 s (tap = settings panel; "recenter stops
  being instant" was an accepted trade); it also resets the UI screen anchor. VHVR: a wrist
  button, the Home key, or a pose (both hands within 0.2 m of the face for 3 s); `RecenterOnStart`;
  the recentre recalibrates the eye height. For #28.
- **Fades and vignette.** VHVR fades to black in 0.2 s on sleep, death, bed, teleport and loading,
  back in 0.15 s, re-centring the room offset on the way back; a low-health red pulse under
  min(50 %, 64 HP) with alpha ≤ 0.25. **Neither mod has a vignette or tunnelling option** (155
  config keys in VHVR, none). Comfort options VHVR ships off: immersive ship camera, immersive
  dodge roll, view turning with a mount.
- **Cinematics.** Dishonored composes the physical head rotation over the authored camera in a
  draw-scoped overlay (one coherent sample for both eyes, the reference not reset on every draw —
  33 re-anchors in one run caused "repeated return to the initial gaze"), locks forced pitch, roll
  and FOV during recognised cinematic states, keeps dialogue scenes in stereo, and **parks the
  controls** during a scene (sticks and triggers zero, buttons dropped) while a button *held*
  300 ms passes through so hold-to-skip works. VHVR plays its cinematics as a video on a
  world-space screen. For the MVP's "cutscenes may behave badly": keep HMD rotation on top of the
  authored camera and never force pitch/roll.
- **Pose lag.** Dishonored renders from the head sample two locate generations back and places
  the hands from the *same* head sample ("hands normalised against the freshest head judder by two
  generations of head rotation"); with native OpenXR we get this for free by rendering both eyes
  and the hands from one `xrLocateViews` call per frame.
- **Near clip.** VHVR sets the VR camera's near plane to 0.09 m (hands and held items would clip at
  the default). Arx's near plane is worth checking with a weapon raised to the face.

### 7. Hands

Sources: Dishonored [ARM_HAND_SPLIT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_HAND_SPLIT.md),
[VR-33-HANDS-AND-WEAPONS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-33-HANDS-AND-WEAPONS.md),
[HAND_POSE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HAND_POSE.md),
[ARM_IK.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_IK.md),
[ANIM-HANDOFF-PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ANIM-HANDOFF-PLAN.md),
[WEAPON_MIRROR_PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/WEAPON_MIRROR_PLAN.md);
VHVR [WeaponWield.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/WeaponWield.cs),
[HandGesture.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/HandGesture.cs),
[InterhandItemTransfer.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/InterhandItemTransfer.cs),
[VrikCreator.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/VrikCreator.cs),
[Utilities/Pose.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/Pose.cs).

- **What is shown.** Dishonored: the game's first-person arms **cut at the wrist** so floating
  hands follow the controllers (they have no mesh of their own: the arm/hand triangle list is split
  per bone, triangles straddling the cut plane are clipped, each cut end is capped two-sided with a
  flat disc in the ring's dominant texture colour, the ring position is "a measured number where
  the tester left it in the headset"); since 1.0.4 full-arm IK is on by default (shoulders from a
  head-centred body frame: 16 cm back, 25 cm down, 38.1 cm apart, arm length × 1.27, body yaw with
  a 25° head-yaw dead zone and 1.5 s relaxation; the shoulder slides along the shoulder–wrist line
  when the wrist is out of reach). VHVR: the full body through VRIK (FinalIK) with the SteamVR hand
  models hidden, and **Index finger tracking copied onto the model's finger bones** whenever the
  hand is free (`HandGesture.cs`). Our MVP shows placeholder hands (#36); both show what the next
  step costs.
- **The weapon in the hand** (#75). Dishonored moves hand + weapon + bolt as **one rigid assembly**
  ("two members each solved onto the same palm do NOT stay attached to each other — that was the
  first design and it would have pulled the bolt out of the crossbow"), tags every per-eye
  correction with the eye it was computed for ("a correction from the previous eye is a whole IPD
  wrong"), stores per-hand trims in the palm frame but edits them in the *view* frame ("forward
  moved the hand diagonally" in the palm frame), keeps a second trim for the casting hand, and
  mirrors one-sided first-person meshes with a reflection found by a symmetry search (check whether
  Arx's first-person weapon meshes are single-sided). VHVR re-poses the weapon every render frame
  from the controller with a per-type offset along the pointing axis (sword at the grip, polearm
  −0.875 of the handle, spear +0.5625, battleaxe −0.4, sledge −0.3), the hand centre 0.08 m behind
  the controller's forward (`HAND_CENTER_OFFSET`), an "ulnar" flip of 180° for knives and spears,
  and the blade axis estimated from the mesh bounds (section 2a). Arx has its own first-person
  arm/weapon model: place the weapon node at the controller pose plus a per-weapon-class grip
  offset, with the blade axis from the mesh bounds.
- **An open empty hand** (Dishonored `HAND_POSE.md`): the game curled the empty right hand into a
  fist; the fix mirrors the left hand's finger pose onto the right, applied only after the
  equipment state has held 250 ms (sockets flip mid-holster) — the debounce is the rule to keep.
- **The game takes the hands back** for full-body moves (takedowns, chokes, body pickup; ledge
  climbing was judged *better* with the controller hands): a read-only "who owns the body" flag
  from the animation state machine's class name, entered immediately, released only after 250 ms
  out of every listed state; hands blended to the animation over 250 ms in and 350 ms out with
  smootherstep easing, a straight palm path and the mask held through the return — the first
  version snapped because "the mask dropped on the same tick the return blend began". Arx has
  scripted full-body moments too (being knocked down, death, ladders if animated).
- **VHVR hand gestures:** inter-hand transfer (hands within 0.2 m, forwards aligned within 0.6,
  both grips down, release *one* grip → the item moves to the other hand, haptic `(0.3 s, 100 Hz,
  0.5)`); holster/draw zones at the shoulders and waist (area 4); a recentre pose (hands in front
  of the face 3 s); arm-swing locomotion (run above 2 m/s hand speed, palms down stops it). The
  radial **hand quick menus** are in area 8.

### 8. UI

The MVP puts the whole 2D layer on one floating panel with a laser pointer as mouse (#49, #34).
VHVR is exactly that design (the whole Unity GUI on one quad with one laser); Dishonored shows
where it goes next (per-element anchors, wrist vitals, hand-held notes, a radial dial).

Sources: VHVR [VRGUI.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRGUI.cs),
[SoftwareCursor.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/SoftwareCursor.cs),
[LaserPointerChords.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/LaserPointerChords.cs),
[TextInputPatches.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Patches/TextInputPatches.cs),
[CrosshairManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/CrosshairManager.cs),
[EnemyHudManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/EnemyHudManager.cs),
[VRHud.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRHud.cs),
[QuickAbstract.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Scripts/QuickAbstract.cs),
[VRCinematicScreen.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRCinematicScreen.cs);
Dishonored [HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md),
[MONO_ANCHOR_UI_STATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/MONO_ANCHOR_UI_STATE.md),
[F10_MOTION_CONTROLS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/F10_MOTION_CONTROLS.md),
[DESKTOP_MIRROR.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/DESKTOP_MIRROR.md).

- **VHVR's panel.** The game's UI canvases are rendered by an orthographic camera into a
  **1920×1080** render texture and shown on a quad **3 m tall at 3 m, 1 m above the player's
  origin** (`UIPanelSize`, `UIPanelDistance`, `UIPanelVerticalOffset`), drawn by its own camera on
  its own layer with a third camera drawing the hands over it (an OpenVR *overlay* was abandoned
  for motion controls: "overlays have no depth", so no laser interaction). Placement: the panel
  **re-faces the head only when movement starts or when the head has turned more than 75° (50°
  while moving)**, slewing at 180°/s; it is **frozen while a menu, the inventory or the map is
  open** (`LockGuiPositionWhenMenuOpen`); with the inventory or build menu open it can shrink to
  0.375 m and sit on the off hand; death re-centres it.
- **The laser as mouse.** One laser only, from the dominant hand, **enabled only while the game
  shows a mouse cursor** (or in build mode); the cursor position is the analytic ray/plane hit
  mapped to pixels and written into the game through a one-line hook on `Input.mousePosition`
  (the 2D UI code is untouched); trigger = left click, B = right click, chords for middle click,
  split stack and discard (grip + trigger); a **click dead zone** freezes the cursor at the press
  until it drifts more than 5 mm *and* 16 px, so a trigger pull does not become a drag; scrolling
  from the laser hand's stick axis (`ContextScroll`) with a 0.4 s repeat delay. Text input opens
  the SteamVR keyboard in minimal mode with the mod keeping its own text and caret.
- **Dishonored's screen.** Menus, loading screens and books are on a head-locked quad 2.4 m wide
  at 1.75 m, or (anchored mode) an upright panel seeded at the current head yaw and position when
  a menu opens and kept there while it is open; a window absent more than 250 ms and back is "a
  new menu" and re-seeds at the current head yaw (before that fix "the pause menu appeared in the
  session's starting direction all session"). In-game screens ride the **HUD window** at
  **1.30 m, 1.25 m wide, 0.10 m below the eye line**, billboarded. "A menu is open" is *read* from
  the game's UI manager, loading is a lease released by the game's own movie completion, never a
  timer; the HUD redirect arms on game state, "never the per-present eye tag" (that gave a 10 Hz
  flicker).
- **The pointer rules** from Dishonored's F10 panel (the same problem as our laser on the panel):
  compute the cursor by projecting the aim ray onto the panel plane, not by a gain ("the cursor
  can only sit on the ray at one resolution and one FOV"); hold the last good position so both
  eyes draw the cursor at the same pixel; click on trigger > 0.5; scroll on the stick's dominant
  axis with a 0.25 dead zone *per second*, not per frame; mute the pointing trigger and the stick
  for the game while the panel is up; the pointer expires 500 ms after the last ray so the real
  mouse returns. ImGui-specific: event-API input, the eye texture as the viewport, input trickling
  off.
- **HUD elements.** Dishonored: every element has an anchor — `off | frame | window | world |
  handL | handR` — with health and mana split across the two wrists (a compositor quad "cannot be
  locked to an animated hand", so the wrist vitals are drawn *inside the hand's own draw*); notes
  and the journal attach to the hand that opened them with a fixed grip-relative pose and one tilt
  slider (0.66 m wide); objective markers are claimed by the screen point their native parent
  publishes. VHVR: health on the left wrist and the minimap on the right by default (fading from
  full within 5° of facing the camera to none beyond 90°), stamina and the rest camera-locked at
  about 1 m; the crosshair, hover names and enemy health bars are world-space canvases at the hit
  distance, scaled by distance; NPC speech bubbles above the speaker; cinematics as a video on a
  world-space screen.
- **Radial menus from the hand.** Dishonored's weapon wheel: hold the left grip, the dial is seeded
  at the grip position, selection by hand displacement projected onto the dial's plane with a
  **15 mm neutral radius and 120 mm full travel**, depth does not select, the sticks do not select,
  tracking loss neutralises; their pad bridge had been quantising stick axes in menus and
  "destroying radial directions". VHVR's quick menus: hold B, a ring of up to 11 items at 0.1 m
  radius anchored at the hand (then rig-locked), hover when the hand is more than 7 cm from the
  centre, haptic tick on sector change, release selects; with a laser up the button must be held
  0.3 s because it doubles as right click.
- **Desktop mirror** (Dishonored, our #21): one eye (left), a copy of a resolved draw, **off by
  default to save GPU time**; their window alternated eyes for months because "the desktop simply
  had no eye policy at all". VHVR: one undistorted eye at full window width (cropped), or
  side-by-side, or a separate smoothed flat camera (`FlatscreenSmoothing`) for recording.

**What to take for #49 / #34:** VHVR's numbers as the first panel (1920×1080 texture, about 3 m
tall at 3 m or Dishonored's 1.25 m at 1.3 m for in-game screens — try both), re-facing only on
movement start or a > 75° head turn, frozen while a menu is open, re-seeded when a menu reopens;
one laser, only when the game wants a cursor, the analytic hit fed into the engine's mouse
coordinates so `src/gui/` stays untouched; the click dead zone; stick scrolling; mute the pointing
trigger for the game. Wrist HUD, hand-held book, radial spell dial and per-element anchors are
the post-MVP design (epic #7).

### 9. Controls

Sources: Dishonored [README "Controls"](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/README.md),
[CONTROLLER_BINDS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_BINDS.md),
[CONTROLLER_EMULATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_EMULATION.md),
[src/core/vr/openxr_input.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/vr/openxr_input.cpp);
VHVR [actions.json](https://github.com/brandonmousseau/vhvr-mod/blob/master/Unity/ValheimVR/Assets/StreamingAssets/SteamVR/actions.json),
[bindings_knuckles.json](https://github.com/brandonmousseau/vhvr-mod/blob/master/Unity/ValheimVR/Assets/StreamingAssets/SteamVR/bindings_knuckles.json),
[VRControls.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/UI/VRControls.cs),
the controller diagrams in [ValheimVRMod/diagrams](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/diagrams).

| Action | Dishonored VR (Touch; Index via SteamVR) | VHVR (Index) | Ours, proposed in [RESEARCH-controllers.md](RESEARCH-controllers.md) |
|---|---|---|---|
| Move | left stick | left stick | left stick |
| Turn | right stick (smooth; snap option) | right stick X (`PitchAndYaw`) | right stick X |
| Jump | A | right A | right stick up, or A |
| Crouch | B (toggle) | right stick **south touch** (toggle); north = run toggle | right stick down, or B |
| Attack / use | right trigger (sword); left trigger = power | triggers = `Use` and `LeftClick` on both hands | right trigger |
| Grab / block | right grip = block (+ choke); left grip = weapon wheel | grip force 0.35 = `Grab` (shield block, bow nock, two-handing) | right grip = grab / use |
| Interact | X | (collision + `Use`) | — |
| Menu | left Menu (tap = pause, hold 0.5 s = journal); X+Y chord for controllers without Menu | **left trackpad force click 0.3** = menu; right trackpad force click = inventory | left B |
| Inventory / map | item shortcuts via thumbrest + stick | right trackpad force = inventory; left A = map | right B / left A |
| Quick menus | left grip hold = wheel | right B / left B hold = radial menus (also right click) | — |
| Scroll (panel) | right stick while the panel is up | trackpad scroll (`ContextScroll`) | — |
| Recenter | both stick clicks held 0.6 s | wrist button, Home key, or hands in front of the face 3 s | long-press left stick click |
| Settings panel | both stick clicks tap, or F10 | in-game "VHVR" settings tab | — |

- **Index facts from Dishonored's binding block** (`openxr_input.cpp`, the Index profile):
  "A/B on BOTH hands (left has no x/y paths), analog squeeze, no menu button (`system/click` is
  reserved — never bind), no thumbrest"; so the menu goes to a **firm left-trackpad press**
  (`/input/trackpad/force`, a boolean action on the force component — the runtime thresholds it),
  grips bind `squeeze/value` with a **0.70 / 0.55 press / release hysteresis** in code (the doc
  says 0.9 / 0.7), and the D-pad-style modifier goes to a stick click because there is no
  thumbrest. VHVR binds the trackpad *force click at 0.3* for menu and inventory and the trackpad
  *scroll* for context scrolling. Both agree with our rule "core actions on components both
  controllers have".
- **Layering** (Dishonored): a physical snapshot → a logical action snapshot (remappable, identity
  by default — checked over all 16384 combinations) → per-feature consumers; **chords are resolved
  below the remap** so they can never be lost; motion gestures OR their presses into the same
  logical actions so the trigger always still works; one `GamepadOnly` switch turns every motion
  feature off at once for testing; a "press to set" rebinding waits for all inputs released, takes
  the first input past 0.6 within 6 s. SteamVR's own binding UI applies first. VHVR: one SteamVR
  action set (`/actions/Valheim`: walk, pitch-and-yaw, use, grab, jump, toggle inventory/menu/map,
  quick switch/actions, left/right/middle click, context scroll, toggle run/crouch, dodge, haptic,
  two poses) and a `MissingBindingsPrompt` that opens SteamVR's binding UI when a required action
  is unbound after 5 s.
- **Guards both found necessary:** a world action and a UI click on the same trigger, gated by
  "no laser up" (VHVR); a tap/hold split on a shared button (VHVR 0.3 s hold for the quick menu
  when it doubles as right click; Dishonored 350 ms tap vs 600 ms hold for the stick-click chord);
  "stop gestures on any touch" (VHVR binds every touch to `StopGesturedLocomotion`).
- **Haptics.** Dishonored translates the game's XInput rumble into a pulse on the hand of the
  stronger motor (`XInputSetState` hook) and adds its own: swing fire 0.7 / 80 ms, honoured 0.35 /
  40 ms, potion 0.6 / 120 ms left. VHVR fires `(duration, frequency, amplitude)` pulses per event:
  melee hit `(0.2 s, 100 Hz, 0.5)`, parry `(0.7, 100, 0.3)`, block `(0.2, 100, 0.2)`, stagger
  `(0.1, 20, 0.1)` per frame for 1.2 s, bow full draw `(0.2, 100, 0.3)`, arrow release `(0.1, 75,
  0.9)`, grab `(0.25, 100, 0.5)`, holster `(0.2, 100, 0.3)`, menu sector `(0.1, 40, 0.1)`. For #42:
  route Arx's existing hit/damage events through one `haptic(hand, amplitude, duration)` queue
  applied on the frame thread (Dishonored's rule after a threading crash: every OpenXR call from
  one thread) and start from VHVR's table.

### 10. Process and distribution

Sources: Dishonored [CLAUDE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/CLAUDE.md),
[AGENTS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/AGENTS.md),
[docs/LINEAR_AND_GITHUB.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/LINEAR_AND_GITHUB.md),
[.github/PULL_REQUEST_TEMPLATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/.github/PULL_REQUEST_TEMPLATE.md),
[.github/ISSUE_TEMPLATE/bug_report.yml](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/.github/ISSUE_TEMPLATE/bug_report.yml),
[docs/TRAPS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TRAPS.md),
[docs/CRASH_SUPPORT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/CRASH_SUPPORT.md),
[docs/INSTALLER.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/INSTALLER.md),
[tools/collect-support.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/collect-support.ps1),
[tools/watch-crashes.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/watch-crashes.ps1),
[tools/package.ps1](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/tools/package.ps1),
[src/core/util/crash.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/crash.cpp),
[src/core/util/log.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/log.cpp),
[src/tools/installer/sys/steam.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/installer/sys/steam.cpp);
VHVR [README.development.md](https://github.com/brandonmousseau/vhvr-mod/blob/master/README.development.md),
[make-release.cmd](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/make-release.cmd),
[ApplicationManifestHelper.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/Utilities/ApplicationManifestHelper.cs),
[VRManager.cs](https://github.com/brandonmousseau/vhvr-mod/blob/master/ValheimVRMod/VRCore/VRManager.cs).

#### 10a. Process (Dishonored VR; VHVR has none to speak of)

Dishonored VR is two maintainers plus Claude Code and Codex sessions on a Linear board and GitHub,
with a 43 KB `CLAUDE.md`. The parts that fit a one-owner, one-headset project:

- **Tickets.** Sections *Summary*, *Why now* (a measurement, a log line, a headset verdict),
  *Technical detail*, *Blast radius*, **Non-goals** ("the most valuable section for an agent and
  the one most often left out"), **Pass criteria** — falsifiable, measured, *in run order*:
  `simulator: <command> -> <expected>` first, `headset: <what a human must judge, and the A/B that
  would disprove it>` last — "a ticket with no pass criteria cannot be finished, only abandoned";
  "ticket descriptions are also agent prompts". A `Research` type closes with a measurement and a
  written verdict, not code. Their statuses have **no "needs headset test"** column: the headset
  run happens before review ends and its result goes in the PR body — the opposite of our Testing
  column, chosen because they have two people; ours fits one owner who tests in batches.
- **Pull requests.** *What this is* (what a player would notice), *What changed*, **Evidence** ("a
  PR that changes behaviour and shows no measurement is not ready to read. If a hypothesis was
  falsified on the way, say so: that is a result"), **Levers and defaults** ("default OFF unless
  there is a stated reason"; a lever with no settings control is not shipped), *Blast radius and
  fail-soft*, **What is deliberately not here** (every fault found and not fixed gets a ticket named
  here), a testing checklist with **"the installed build is the built build (log banner and file
  hash agree)"** — after a fix "was measured on a binary that had been compiled and never
  installed". Review asks four questions: does the evidence support the claim, was the build that
  produced the verdict installed, does every lever have an A/B and every refused guard log why
  with the values, are the docs in the same PR. Verdicts go on the ticket "because the ticket
  outlives the branch".
- **Headset time.** "Simulator first, headset last — a headset run costs a person their evening";
  the Codex guide: the tester launches the game, the agent never does, **"one clearly defined
  question per launch, with expected results and what each outcome means"**. Merging needs explicit
  permission every time ("looks great" is not permission).
- **Memory.** `docs/` is the memory, the board is the work: `STATUS.md` (784 KB, newest entry first,
  *Current state* and *Next* naming the headset question and the log line to read — "a session
  that ends without pushing STATUS is a failed handoff"), `ROADMAP.md` with a one-sentence "Done
  when …" per milestone, and **`TRAPS.md`** (117 KB, "everything here cost a session, a headset run
  or both … never deleted"): the stale-setting class (an ini key that exists beats every compiled
  default; a setting with two homes has no owner; a default changed in installed configs needs a
  one-time per-key migration), instruments that could not fail their own hypothesis (the golden
  check that compared the golden against itself; a refusal whose number can only read zero; six
  test suites that did not compile for weeks while cited as guards), plans tried and failed, and
  per-topic graveyards — "new traps and failed plans go in that file in the same commit as the
  work". Rules repeated in almost every doc: *a verified write is not an honoured one*; *an
  instrument that cannot print the unwelcome answer is not evidence*; *a counter is not evidence
  until you know its population*; *never copy a number between games*; *a performance number
  carries its machine and build config*; *prove a gate can fail before trusting that it passed*.
- **Logging rules** (`CLAUDE.md` "Logging"): "Log generously. The log is the only instrument a
  remote tester can send back"; nothing unbounded per frame (`LOG_ONCE`, `LOG_EVERY_MS`,
  `LOG_FIRST_N`); log state changes; an expected zero must say so on the line; name the owner
  before the result; every refused guard says why, with the values; log the derived number, not
  only the input. Ten log sessions kept (`.prev.log` … `.prev9.log`), append-and-say-why when the
  rotation fails, the first line a banner with build id, git describe and config (a Debug build
  tags every perf line "not comparable").
- **Releases.** "Only a human declares a release"; the version lives in CMake so the zip name, the
  banner and the tag cannot disagree; the package script refuses a build that carries the legacy
  marker; a one-time migration keyed on a marker instead of a config-version bump "which would
  drop the machine's tuning"; the packaged config is a byte copy of the tested machine's.

#### 10b. Crash reports and support logs

- Arx Libertatis already has an SEH crash handler with a minidump and `arxcrashreporter`
  (`src/platform/crashhandler/CrashHandlerWindows.cpp`), so the base exists. Dishonored adds:
  a **per-run header** in an append-forever crash file (`run <time> <version> build <id> config
  <cfg> pid <n>`), **named threads** in the fingerprint, a **VEH fallback** because
  `SetUnhandledExceptionFilter` "never ran in three measured crashes" (the Steam overlay displaces
  filters; SteamVR injects into our process too) with a **re-arm from the frame loop**, a
  three-fault budget that their own watchdog once spent at startup ("a recorder with a budget must
  not let our own probes spend it"), and a targeted first-chance dump at a byte-verified address
  after a 44 MB minidump "held none of the memory the crash was about".
- **Support collector** (`collect-support.ps1` + `Collect VR Support.cmd`, "local-only, no
  uploads"): an explicit allowlist (the config, the crash file, the ten-deep log history, the
  runtime's logs, SteamVR's input JSON, the OpenXR `ActiveRuntime` registry value, OS / RAM / CPU /
  GPU + driver facts, the DLL and exe SHA-256 and versions, `status.json`, a bounded tail of the
  trace), a hard **24,000,000-byte cap** with per-file compression, newest logs first and an
  oversized log reduced to head + tail with a marker line, a `manifest.json` (hashes, sizes,
  truncated, omitted) and a `READ-ME.txt`, written to `Desktop\<Mod> Support\`. Dumps are opt-in
  and private. Lesson: "a shipped `.ps1` with no `.cmd` wrapper does not run for the user".
- **Freezes:** `watch-crashes.ps1` attaches Microsoft ProcDump to the game by PID (`-e -t -mm`
  for crashes, `-ma -m <MB>` for memory) and then runs the collector — opt-in, never launches or
  kills the game. **Symbols:** DLL + PDB archived per build under the DLL's SHA-256.
- **Bug report template** for testers: build tag, headset and connection, refresh rate and render
  size, "fresh config?" and "motion controls off?" dropdowns, the log as an attachment ("the useful
  line is often not the one that looks useful"), no game footage. Blank issues disabled.

#### 10c. Distribution

- **Dishonored ships** a zip (DLLs, docs as `.txt`, `HOW-TO-USE.txt`, the tested config, the
  scripts with `.cmd` wrappers) plus a native launcher: a 32-bit Win32 + ImGui exe with the mod
  embedded as resources, which finds the game in the Steam library, installs, updates, changes
  settings, disables VR (a `disable_vr.txt` kill switch), collects logs and uninstalls; it
  **refuses while the game runs** ("a process list that cannot be read counts as running"), writes
  atomically with backups, keeps an install record JSON (version, build id, config, DLL hash,
  choices), changes exactly **four** game settings ("a mod that silently rewrites a player's
  graphics settings is indistinguishable from a mod that broke them"), and self-updates from GitHub
  releases under a strict contract (exact asset name, `sha256:` digest, size, never replace a
  same-version asset). Every screen renders headless to a PNG; a scratch-folder smoke test installs
  against a stand-in exe. A Steam app needs none of the install half; the *Manage* ideas (build id
  in the log, runtime name, kill switch, collect logs) and the tester-build discipline carry over.
- **Steam discovery** (`steam.cpp`): `HKCU` then `HKLM`, both registry views, `SteamPath` then
  `InstallPath`, Program Files fallbacks; `libraryfolders.vdf` `"path"` lines parsed without a
  regex; `appmanifest_<appid>.acf` → `installdir`; a `why` string on every failure; a hand-picked
  folder normalised (root, `Binaries`, `Win32` all accepted); a 64-bit exe rejected by
  `GetBinaryTypeW`. Our `scripts/build-paths.ps1` (`Find-ArxFatalisSteamInstall`) already walks the
  same registry → vdf → manifest chain; #79 moves it into the game.
- **Runtime selection:** `XR_RUNTIME_JSON` wins over the registry silently — "a path left in the
  ini silently wins … every launch. That cost a session" — so the log names the runtime that
  answered and where the override came from, and no pinned path ever ships.
- **VHVR ships** a Nexus / Thunderstore package made by a post-build `make-release.cmd` that copies
  the plugin, the SteamVR / XR / FinalIK DLLs, the action manifest and bindings and the asset
  bundles into the game directory; settings through BepInEx's config file, an in-game "VHVR" tab
  and the optional ConfigurationManager. Its one distribution idea we need: the **SteamVR
  application manifest written and registered by the game at every launch**
  (`ApplicationManifestHelper`: `"source": "builtin"`, `app_key steam.app.<appid>`, `image_path`
  from the Steam CDN header, `"launch_type": "url", "url": "steam://launch/<appid>/VR"`, then
  `OpenVR.Applications.AddApplicationManifest(path, false)` and `IdentifyApplication(pid, key)`,
  errors only logged) — what `scripts/register-steamvr.ps1` does by hand today (#89) and what #80
  "launch from SteamVR" needs from inside `arx.exe`. Also the **`flatScreenMode=auto`** policy (VR
  only if SteamVR is already running, the same DLL runs flat on any failure) — our D-004.

### 11. Titanfall 2 VR (#118)

Sources: [README](https://github.com/kyus2001/titanfall2vr/blob/main/README.md),
[CONTROLS.md](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md),
[INSTALL.md](https://github.com/kyus2001/titanfall2vr/blob/main/INSTALL.md),
[BUILDING.md](https://github.com/kyus2001/titanfall2vr/blob/main/BUILDING.md),
[KNOWN-ISSUES.md](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md),
[CHANGELOG.md](https://github.com/kyus2001/titanfall2vr/blob/main/CHANGELOG.md),
[THIRD_PARTY_NOTICES.md](https://github.com/kyus2001/titanfall2vr/blob/main/THIRD_PARTY_NOTICES.md),
[plugin/titanfall2vr.ini.example](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/titanfall2vr.ini.example),
and the files named per area below. Read at commit `8c50a7d` (v0.1.1, the repository's only
commit); no line numbers, because a squashed history may be replaced. Quotes are from the code's
own comments, which are dated and carry the measurements.

The seven questions #118 asked, in its order. Numbers in the rows F-111 … F-134 are theirs; the
decisions are the owner's.

#### 11a. OpenXR integration in C++ (vs `src/vr/VRSystem.cpp`; #18, #20, #30, #74)

Sources: [xr/xr_context.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp),
[xr/xr_context.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.h),
[xr/xr_input.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp),
[xr/render_resolution.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/render_resolution.cpp),
[render/present_hook.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/render/present_hook.h),
[host/plugin.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/host/plugin.cpp).

What they do, in the order a session runs, with what ours does today in brackets:

- **Instance.** `XR_KHR_D3D11_enable`; the API version requested is **1.0, deliberately** — a
  1.1.x loader against VDXR's 1.0 runtime failed `xrCreateInstance` before the graphics extension
  could be negotiated [ours asks for 1.0 first as well, `VRSystem.cpp`]. `XR_ERROR_FORM_FACTOR_
  UNAVAILABLE` is named in the log as "NO HEADSET — the game will run at full speed with VR doing
  nothing, which looks identical to a good run"; one headset run was spent on that [ours names it,
  #17]. Runtime name and version, the system name ("the runtime's claim, not a detection": VDXR
  calls a Play For Dream a Quest 3), vendorId and systemId go in the banner (F-98 note); the view
  configuration's recommended and maximum sizes are logged once.
- **Actions before the session, attached once after it** (F-123): one action set `gameplay`, one
  action per concept with two subaction paths (aim pose, grip pose, thumbstick click, thumbstick,
  trigger, primary and secondary button *by position* — X/A, Y/B — squeeze value, menu on the left
  hand only because the right Touch button belongs to the dashboard, thumbrest touch, haptic
  output). Suggestions for the Touch profile with the thumbrest, then without it if rejected, then
  `khr/simple_controller` with the two poses; each result logged with its count. No Index profile —
  Dishonored VR's block (F-41) has it. Action spaces are session-scoped and remade with the session.
- **Spaces.** LOCAL (the pose source), VIEW (the head, for the handedness check and the menu quad),
  and **STAGE**, enumerated first and created only when offered — the floor, "the quantity that was
  never in the program" until then (F-30 note).
- **Session events** (F-118): READY → `xrBeginSession`, STOPPING → `xrEndSession`, EXITING or
  LOSS_PENDING → disable; every transition logged by name with its meaning; on leaving FOCUSED the
  Windows foreground window, pid, class and title are logged — a different pid means the game lost
  desktop focus and the runtime followed, our pid means the runtime took it for an overlay (the
  player died, the session dropped to VISIBLE and stayed there for 26 s of zero-layer frames, "a
  black rectangle that follows the head"); `REFERENCE_SPACE_CHANGE_PENDING` re-seeds the mod's own
  recentre reference (F-120) [ours polls and names the states, #17; the rest is #28 and #130].
- **The frame** (`SubmitFrameCore`): `xrWaitFrame` → `xrBeginFrame` → on real game frames publish
  the head pose and `xrSyncActions` with the frame's `predictedDisplayTime` → `xrLocateViews` for
  both eyes (the midpoint of the two positions, each eye's four FOV angles, the IPD from the two
  positions, the eye height in STAGE) → acquire, wait, copy, release this eye's image → `xrEndFrame`
  with a projection layer whose two views carry the pose **each eye was rendered with** (F-116)
  plus a quad layer for the menu; `shouldRender == false` → zero layers. The trap stated loudest:
  `xrWaitFrame` and `xrEndFrame` are **one pacing budget** — across every archived run WAIT + END is
  a per-run constant (11.9 ms healthy, 98 ms stalled) while the split between them wanders, so "an
  88 ms xrEndFrame next to a 10 ms xrWaitFrame is the app being paced at 10 Hz with the wait landing
  in END, not a stall inside xrEndFrame"; three sessions went into that premise. Our #20 rate line
  (F-07 note) should print the sum against `predictedDisplayPeriod`, and the worst frame and the
  worst *run* per window rather than a mean — "a pause the wearer can feel is a RUN, which no single
  worst-frame number can show".
- **Swapchains.** Created from the game's backbuffer description; a runtime routinely returns a
  TYPELESS image for a typed request and `CopyResource` is happy across the family — a format check
  "rejected every copy on a healthy session and turned the headset black"; a resize of the game's
  buffer is detected by size and the swapchains rebuilt (a `CopyResource` between mismatched sizes
  is a silent no-op: "the headset shows the last good frame forever while the desktop carries on");
  every create and destroy bumps an epoch the crash record reads (F-131).
- **Loading screens** (F-117): the gap-filling pacing thread is the third design; the first two
  replaced the working loop and failed (`ID3D11Multithread` swapped a vtable the byte matcher
  relied on, so stereo silently never armed for a whole run; an image acquired by one thread and
  released by another blocked the runtime 79 ms inside `xrEndFrame` and the headset went black).
  The rules it left are in the row. Arx's loading is synchronous on the main thread with a progress
  callback (`LoadLevelScreen`), which is where an `xrWaitFrame` / `xrEndFrame` pump could live after
  the MVP.
- **Haptics.** `xrApplyHapticFeedback` with an explicit duration and frequency,
  `xrStopHapticFeedback` on zero (F-44 note).
- **Resolution.** One `render.scale` (0.5–2.0, default 0.75) multiplies the runtime's recommended
  per-eye height; the width follows the headset's tangent ratio so the aspect cannot drift; the
  learned geometry is cached per headset (runtime name, system name, recommended size, tangents to
  three decimals) so the next launch starts right; applied live when windowed — a fullscreen mode
  change replaces the swapchain. Our #18 renders at the recommended size (F-25 note).
- **Eye height in STAGE** (F-30 note): averaged over both eyes, settled when five consecutive
  samples agree within 1 cm inside [0.8, 2.0] m; measured, logged, and **not used** — the Quest 3
  and the Play For Dream read the same seated wearer at 1.22–1.25 m and 1.54–1.60 m. The height fix
  is a game constant instead (F-119).
- **Asymmetric and canted optics** (F-115): the Quest 3 reports L −54° R 40° U 44° D −55° per eye
  with the horizontal asymmetry mirrored between the eyes; a symmetric engine frustum had to render
  the cover (the larger half of each axis) and *shear* the frustum to the lens centre — a
  translation in NDC that costs no draw calls — with the Y flip proven offline. We build the
  projection from the four tangents directly, so the only thing to carry is the test with a negative
  control and the banner's cant check (the two eyes' orientations differ on canted panels).

#### 11b. Weapon aim and grip calibration (#75, #36)

Sources: [hands/hand_pose.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.h),
[hands/hand_pose.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/hand_pose.cpp),
[core/config.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.h) (`NudgePivotOffset`, `CalibrationTarget`),
[ads/ads_probe.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ads/ads_probe.h),
[ads/ads_lock.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ads/ads_lock.h),
[hands/use_target.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/use_target.h),
[hands/weapon_settings.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/weapon_settings.h),
[hands/arms_collapse.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hands/arms_collapse.h),
KNOWN-ISSUES 2, 3, 13.

- **Composition** (F-125). Position from the **grip** pose, orientation from the **aim** pose (the
  grip's forward runs along the handle: a rifle sat pitched 45° up on the first headset run). The
  hand is an offset *head reference → hand* in room metres, mapped to engine units through one axis
  convention reused from the head path "so a sign error here cannot disagree with a sign convention
  that is already right", rotated by the **body's** yaw (the engine's view before head tracking adds
  the headset's rotation) minus the **same yaw reference the view uses** — without that the gun sat
  off the view by exactly the reference after every recentre, "aim that drifts as you play and needs
  a headset reset to fix". The anchor is the origin the view is built from, not the camera base
  (the base leaps a whole stair step in one frame while the view eases into it). The offset is
  taken from the **recentre reference position**, not the live head: turning the head translates
  the eyes about the neck and "the gun slid the opposite way". The composition is formed on the
  plugin frame and consumed on the render frame; measured, that left the hand one frame stale in
  body yaw — `lag == turn` to three decimals, 8° per frame at full stick, different in each eye
  under alternate-frame stereo, "which is the flicker" — and the fix adds the base movement since
  composition at the point of consumption, at *both* consumers (the drawn gun and the aim write),
  "or a turn-rate-proportional gap opens between where the gun is drawn and where it shoots". For
  us the rule is simpler and stricter: compose the IK targets (#36) and the held weapon (#75) from
  the same frame's head and hand samples and the same body yaw (F-21), on the thread that renders
  them.
- **Grip = pivot** (F-126). Three offsets in the **weapon's** frame (`hand.grip_*`, rotated by the
  final weapon orientation and applied last, so the weapon pivots about the hand instead of
  swinging on a lever from a model origin authored near the eye), three in the **player's** frame
  (`hand.off_fwd / left / up`, applied before the body-yaw rotation so "forward" means forward
  whichever way the player faces), three angles applied after the composition (so they mean
  "rotate the model relative to the hand", which is what someone dialling them in a headset
  expects), two axis gates (`use_pitch`, `use_roll` — "not free to toggle once a grip is dialled
  in: the offset is rotated by the gun's orientation, so turning roll off moves the gun"). The
  **pivot nudge**: nudging the grip moves the pivot *and* translates the gun by `R · delta`, which
  is all the wearer can see ("the tooling moved the GUN; I want to move the PIVOT"), so the nudge
  subtracts `R · delta` from the player-frame offset in the same press — the gun stays, only what it
  swings about changes. Keyboard calibration keys were replaced by panel rows that move the weapon
  live while it is held and save on change; the ini's "START HERE ON NEW HARDWARE" block is the grip.
- **Per weapon.** A census at the gun range measured where each weapon model puts its own `R_HAND`
  attachment; the grip becomes one user calibration plus *(the reference weapon's R_HAND − this
  weapon's R_HAND)*, latched from the first idle second the weapon is held (an animated hand bone is
  only the grip point at idle), refused outright when further than 9 / 4 / 5 units from the
  reference, never applied in a Titan (a different rig). The first campaign weapon ever to run the
  measurement in anger put the rifle "two feet above and a foot left of the hand" for fifteen
  seconds (KNOWN-ISSUES 13): the latch had accepted the first *steady* second, which in a firefight
  was a combat pose, and a latch is first-wins for the session. Arx's weapon meshes carry their own
  grip vertex (`primary_attach`, used by the engine's equip code), so the per-weapon term is data
  for us; what remains to dial is the constant from the Index controller's grip pose to the model's
  palm (F-49), once, live.
- **ADS, kept as a design principle only** (Arx has no guns): "the mechanism is a removal, not an
  addition" — in ADS the overrides stand down and the engine's own canonical pose returns, "no
  canonical offset to calibrate, no sight-line solve, nothing new that can be wrong per weapon";
  the brace (F-127) is a gain inside a cone, never a filter, so nothing is ever late; one predicate
  with hysteresis answers "are we in ADS" for all three consumers "so the gun, the aim and the turn
  can never disagree about it for a frame".
- **Use targeting** (F-57 note): the engine's own "which item am I pointing at" search is wrapped to
  take the hand's angles (rung 1) or the hand's origin too (rung 2), with per-stage counters and a
  5 s heartbeat that names the dead stage — the same shape as D-010's "the engine's own test aimed
  along the hand".
- **The body.** `body.show`: weapon only (every arm bone's 3×3 zeroed so the mesh rasterises
  nothing), weapon + hands (the two hand chains kept — but the gauntlet mesh carries the forearm,
  KNOWN-ISSUES 3: "hands-only would need a custom model"), full body. The weapon's own sway is
  zeroed in its per-weapon settings data, "the engine's own animation, which no camera-matrix
  correction can cancel" (F-31 note). The gun trailing the hand by a frame in hip fire
  (KNOWN-ISSUES 2) is the "sample the hand at the display's predicted time" rule we get from one
  `xrLocateViews` per frame.

#### 11c. Eye height (D-006, #27, #101)

Sources: [camera/player_eye_hook.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/player_eye_hook.h),
[camera/player_eye_hook.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/player_eye_hook.cpp),
[camera/player_eye.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/player_eye.h),
[camera/camera_hook.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/camera/camera_hook.cpp) (`AdvanceHeadTracking`),
[host/plugin.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/host/plugin.cpp) (`recentre_on_spawn`),
CONTROLS.md "View", CHANGELOG v0.1.1 "World scale".

- **The defect and the fix** (F-119). The game seats the camera at the neck: the view offset is 60
  of a 72-unit hull (83 %) where a human eye is at 93 %, "the same on every headset". Three fixes
  were tried in order, and the first two are the traps: raising the **render camera** fixed the
  height and broke the aim (the round still leaves the game's eye, so impacts sit below the reticle
  by `atan(raise / distance)` — "parallax, it varies with range, and no reticle nudge can cancel
  it"); writing the player's **view offset per tick** fought prediction and "vibrated the gun"; the
  fix that shipped swaps the vtable slot of the engine's own `EyePosition` getter on both the client
  and the server and adds a **constant** 8 units — the camera, the gun, the shot origin and the
  reticle all read that one getter. The install refuses unless the slot and the function's prologue
  match the bytes read offline, so a game patch degrades to "no height change", never to a wrong
  write; the hook counts calls per side and prints its first callers so the log names the camera
  path and the fire path.
- **Why not the measured height.** STAGE gives the wearer's real eye height (F-30 note) and it
  drives nothing: the two runtimes disagree by a third of a metre about one unmoved seated wearer,
  and "what needs correcting is the GAME seating its camera at the neck, a constant, not how tall
  the wearer is". Our D-006 ("eye height follows the character") is the same conclusion; #101's
  real-height option would scale the world (F-30), not move the eye.
- **Recentre** (F-120). The yaw reference is captured once and re-seeded three ways: the runtime's
  own recentre (the event), a key, and automatically 300 ticks after a world is up — "a height
  sampled during the loading screen is wrong for the whole session, and the moment a level finishes
  loading is the moment the wearer is least likely to be sitting the way they mean to play". Pitch
  and roll are never referenced (LOCAL is gravity-aligned); subtracting the reference's 18.48° pitch
  and 5.21° roll had baked a constant tilt into the engine camera that the world never showed (the
  compositor reprojects with the true pose) but every screen-anchored panel did. The engine's pitch
  is the head's absolute pitch, clamped at ±89° as a safety only; a stick axis that once also wrote
  it ratcheted the body pitch to the clamp across crouch cycles. For Arx: the player's pitch comes
  from the HMD each frame and nothing else may write it; the recentre is yaw only; #28 listens to
  the runtime's event.
- **World scale** is a separate knob from height (`view.world_scale` 0.5–2.0): the world is sized
  from the runtime's IPD at one game unit per inch; "lower it if hands and weapons look oversized —
  the depth strengthens as you do, because both come from the same number". Its first
  implementation scaled the eye separation and the units-per-metre by the same factor, which cancel
  exactly, so it did nothing for a release (CHANGELOG v0.1.1, "World scale actually does
  something") — the kind of lever F-106's "prove it can fail" catches.

#### 11d. HUD per-element placement (#73, #49, #116)

Sources: [core/config.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp) (`kSettings[]`, the HUD rows),
CONTROLS.md "HUD",
[hud/world_marker.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hud/world_marker.h),
[hud/lock_hud.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hud/lock_hud.h),
[hud/crosshair_scale.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hud/crosshair_scale.h),
[hud/hudwarp.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/hud/hudwarp.h),
KNOWN-ISSUES 7, 11, 12, 16.

- **What they ended up with** (F-129). The engine's flat HUD pass (reticle, fades, overlays) is
  scaled about its centre by `hud2d.zoom` = **0.28** and shifted in render pixels; the lower-left
  weapon and ammo group at **0.55** with its own sideways shift, "because the size control scales
  about the screen centre, so it cannot bring a group in from the edge without also shrinking it —
  this separates the two"; name labels at 0.5; the top-left cluster pulled in through the engine's
  own safe-area inset; prompts that were readable on a monitor enlarged (the dialogue prompt 1.82×,
  the pickup highlight 2.1×); Titan-only widgets (cockpit surround, dash bars, lock rings) each with
  a size and a shift. Grouped in the panel "by which mechanism moves them, not by where they sit
  on screen, because that is the thing a wearer has to know to pick the right control". The cost
  is visible in the count: about 30 of the 72 settings are HUD placement (F-84 note).
- **Anchors.** The flat pass follows the **aim** angles the mod writes for the hand, so fades,
  loading overlays and the damage border draw as a rectangle in front of the right controller
  (KNOWN-ISSUES 11); an ini switch moves the pass to the head at the cost of the reticle leaving the
  gun. World markers (waypoints, pickups) are projected by the engine in the body-yaw frame while
  the eye renders in the head's, so they ride the head by the delta; the fix rotates the marker's
  world position by −(head yaw delta) about the view origin before the engine projects it, yaw only
  — the first version also corrected pitch and the wearer saw a "flattened U". A Titan HUD
  correction gated on the camera sitting within 2.5 cm of the origin lost itself on 40 % of frames,
  because a head turn swings the eyes ten centimetres about the neck (KNOWN-ISSUES 7). A reticle
  scale hook was "measured closed": the float it scaled reached the draw and was not the size, and
  the row is kept in the panel labelled as doing nothing "because it is real and reversible".
- **For #73.** Arx draws its 2D layer into a virtual screen; the numbers to start from are TF2VR's:
  the whole pass at roughly a quarter to half of the eye image, centred, with the corner elements
  (gauges, the hotbar, the book icon, the cursor) moved inward separately rather than only scaled;
  fades and full-screen effects on the head, never on a hand; anything meant to be read (notes,
  dialogue, the "halo" messages) larger than on a monitor. The wrist vitals (#116) and the panel
  (#49) are the diegetic answer the catalog already has (F-83, F-80); TF2VR's screen-space HUD shows
  what the fallback costs.

#### 11e. Controls (#30, #31, #32, #33, #35, #74; [RESEARCH-controllers.md](RESEARCH-controllers.md))

Sources: CONTROLS.md,
[input/vr_input.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/vr_input.h),
[input/vr_input.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/vr_input.cpp),
[input/xinput_pad.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.h),
[input/xinput_pad.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/xinput_pad.cpp),
[input/dpad_gesture.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/dpad_gesture.cpp),
[input/aim_cmd.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/input/aim_cmd.h),
[xr/xr_input.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_input.cpp).

| Action | Titanfall 2 VR (Touch) | Ours (D-008, F-47, RESEARCH-controllers.md) |
|---|---|---|
| Move | left stick (digital key latches on the engine route, analogue through the synthetic pad) | left stick, look-relative |
| Turn | right stick X: smooth with a response curve, or snap 30° | right stick X, smooth and snap (#32) |
| Jump / crouch | A / B (the right stick's vertical answers two-option prompts and nothing else: "in no scenario should moving the right stick up/down alter the direction we are facing") | A / right stick down (#33) |
| Fire / ADS | right trigger / left trigger | right trigger = attack |
| Use / reload | X tap and hold; Y tap = weapon, Y held 350 ms = objectives | grip = grab (D-010) |
| Abilities | left grip = tactical, right grip = ordnance, 0.60 / 0.45 hysteresis ("which is which is not configurable — rebind the shoulders in the game") | — |
| Sprint / melee | L3 / R3 | — |
| Menu | left menu button = the game's pause menu | left B |
| Settings panel | L3 + R3 within 400 ms | — (F-19, later) |
| Recentre | the headset's own recentre (the event re-seeds the mod) plus a keyboard chord | a held chord (#28) |
| Modifier | thumbrest touch, or the left controller raised beside the head | — |

- **Input route.** Two closed routes are written at the top of `vr_input.h` so nobody re-derives
  them (the game's input system does not import XInput at all; the server's usercmd is
  delta-compressed and unsound to write); the route that shipped drives the engine's own key latches
  and, later, one synthetic gamepad resolved at the game's own `GetProcAddress` seam, because every
  reference mod (Halo MCC, BioShock, Cyberpunk) "presents ONE synthetic pad and lets the game's own
  binding and menu code do the rest". **One owner per action**: when both routes delivered B it
  reloaded *and* crouched; when both delivered the grips, one squeeze fired both abilities; when
  both delivered the stick clicks the summon chord fired sprint and melee "about half the time". For
  #74 the equivalent is one snapshot and one consumer per logical action, with the engine's own
  bindings untouched.
- **The right stick** (F-121): deadzone subtracted and the remainder rescaled, a power curve on the
  normalised travel (2.0 by default), a turn-speed scale where **70 = the game's own rate** because
  "the old scale spent its bottom half on speeds nobody would choose", and axis dominance with
  hysteresis on the ratio (0.6) and the magnitudes (turn 0.45, vertical 0.60) — the wearer reported
  the diagonal problem from both sides, and the fix deliberately keeps a band where both fire.
- **Snap turn** (F-32 note): the first version held the engine's turn button for
  `degrees / yawspeed` seconds — a 17-frame sweep at 80 Hz, "exactly what snap turning exists to
  avoid: a quick smooth turn can make a motion-sick player worse"; the shipped one writes the view
  angle once between two frames and the engine keeps it; requests replace rather than queue so a
  double flick turns once; re-armed only when the stick returns to centre; in snap mode the analogue
  turn is sent as zero so a stick underneath cannot make it a sweep again.
- **Presses and chords** (F-122): the OpenXR boolean's `changedSinceLastSync` latches a press that
  started and ended between two syncs (one of three A presses was lost on a 40 fps loading screen,
  "indistinguishable from a screen that ignored one"); the summon chord has **no timing gate** on
  the first click — the second click and both releases are consumed, and the order matters: "lead
  with L3; a leaked L3 is a brief sprint, a leaked R3 is a melee swing"; a button still held when
  the panel closes is consumed until released; the menu button has one edge detector above the
  mode branch, because two (one per branch) double-fired across the switch.
- **Tap versus hold** (F-43 note): a tap of Y is emitted on *release* and held for a few frames so
  the game's own poll cannot miss it; a hold past 350 ms sends the other button and never the tap —
  the wearer's constraint ("if that is going to fire a Y normal press as well, DON'T DO IT"), with
  the cost written beside it and the ini key that turns it off.
- **The modifier** (F-124): a mode over the left stick entered with an input nothing else owns — the
  capacitive thumbrest on Touch (a runtime without it reports the action inactive, which reads
  "never fires" rather than a fault) or the left controller raised to chin height within 0.45 m of
  the head. "Held means held": an earlier draft armed the modifier for a window and let it expire,
  "which would have made a correct, deliberate use fail — touch, hesitate, push, and find the mode
  gone". Movement is released, not frozen, while engaged; a haptic pulse announces the mode; the
  status line counts "evaluated / skipped: no grip pose / no head pose" because a gesture that is
  never *evaluated* reads exactly like one that is evaluated and rejected — "`head gesture seen=0`
  was read for four sessions as the wearer never tried it".
- **Haptics** (F-44 note): the game's `XInputSetState` is the source; changes go to the controllers
  the instant they arrive because the game animates its own envelopes (65535 → 29490 → 28418 … and
  a low tail) faster than a 40 ms timer, and the timer only *sustains* with 60 ms pulses that
  overlap; the frequency carries the heavy / light motor balance (160–260 Hz); the strength slider
  stops at 100 % because `XrHapticVibration::amplitude` is clamped to 1.0 by the specification and
  "the wearer reported the top two thirds of the range as indistinguishable".
- **Binding liveness** (F-123) is in 11a. Crouch has three modes kept for the headset-run economy
  (F-47 note).

#### 11f. The settings panel in the headset (F-19)

Sources: [ui/menu_overlay.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ui/menu_overlay.cpp),
[ui/menu_overlay.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/ui/menu_overlay.h),
[core/settings_registry.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/settings_registry.h),
[core/config.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/config.cpp),
[xr/xr_context.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/xr/xr_context.cpp) (`EnsureMenuSwapchain`, `CaptureMenuPose`, `PrepareMenuLayer`),
CONTROLS.md.

- **Presentation** (F-128). One ImGui context, two adapters: flat (the game's backbuffer, for a desk
  run) and VR (a private 3072×2304 texture copied into its own `XrSwapchain` and shown as an
  `XrCompositionLayerQuad`, 1.7 m wide at 1.5 m, posed once at summon in LOCAL space from the head's
  yaw only — "I really want this fixed in front of me and not pinned to my head"). ImGui's DX11
  backend does not restore pipeline state, so the game image corrupted until a full state backup
  wrapped the draw; the render-target view is keyed on the backbuffer pointer so a resolution change
  rebuilds it; the font is re-rasterised at the panel's scale (`FontScaleMain`, ImGui 1.92's dynamic
  fonts) rather than magnified — "the REALLY blurry the wearer reported" was a 9 px atlas scaled up.
- **Navigation.** The sticks feed ImGui's gamepad nav as **levels** (ImGui does its own repeat;
  synthesised edges would fight it); `ImGuiBackendFlags_HasGamepad` is set every frame, because
  without it ImGui discards every gamepad key — "the whole reason the panel could be summoned, drawn
  and focused and still not be driven"; directions are ignored until both sticks have returned to
  centre once after opening; A activates, B backs out, the grips step the category (the tab row is
  not focusable, so the stick never has to cross windows), the right stick scrolls; the parent
  window's scroll is pinned to zero every frame or navigation scrolled the tab row off the top. A
  once-a-second readout prints `hasgamepad / navactive / navvisible / armed`, because three failures
  look identical from inside a headset.
- **Content.** Every control is a registry row (F-113) — "a control with no setting behind it is a
  second store by another name"; one category at a time with tabs across the top (a sidebar "was
  eating a quarter of the width to show eight short words"); rows grouped by mechanism with units in
  the label, honest precision (`%.0f`, not `%.2f` on everything), one note under the last row of a
  group instead of a tooltip per row, rows hidden when the mode that uses them is off ("a control
  that provably cannot act is worse than a hidden one" — Halo MCC VR's convention), a `reset` button
  only beside the focused row, the tier printed on the control for anything not live. Edits are
  **staged** and applied on close ("the game is live behind the panel; a slider that applied on
  every frame of a drag would be changing the world while the wearer was still deciding"), then
  written to the ini line by line and, for the one per-headset setting, to the cache that wins over
  the ini — the first panel edit of the resolution "applied, persisted to the ini, and was then
  silently overridden on the next launch by a cache nothing had updated".
- **Safety.** The panel build runs inside an SEH guard that names the registry row being drawn and
  disables the overlay for the session instead of crashing — "a crash costs the wearer a whole run
  to learn one bit". Gameplay input is suppressed while the panel is up by the panel's own flag; the
  first design opened the game's pause menu to get that for free, and the run proved the predicate
  it relied on was a cursor-visibility test that never fired in VR, so the game is **not** paused
  ("the wearer is stationary and cannot act, but is not invulnerable").
- **For us.** F-19 stays post-MVP; when it comes, this is the design, and the stick navigation
  without a laser is the alternative the owner can pick for #34 now (F-128).

#### 11g. Docs, testing and process

Sources: BUILDING.md, INSTALL.md, KNOWN-ISSUES.md, CHANGELOG.md, README.md,
[tools/](https://github.com/kyus2001/titanfall2vr/tree/main/tools),
[core/flat_harness.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/flat_harness.cpp),
[core/diagnostics.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/diagnostics.cpp),
[core/crash_recorder.cpp](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/core/crash_recorder.cpp),
[diag/stall_watchdog.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/stall_watchdog.h),
[diag/plugin_cost.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/plugin_cost.h),
[diag/xr_breadcrumb.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/xr_breadcrumb.h),
[diag/resource_watch.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/diag/resource_watch.h),
[engine/hook_registry.h](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/src/engine/hook_registry.h),
[plugin/CMakeLists.txt](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/CMakeLists.txt),
[plugin/cmake/version_stamp.cmake](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/cmake/version_stamp.cmake).

- **Testing without a headset.** `profile = 1` is the flat gate: the plugin comes up, the hooks
  install, everything that needs no headset runs on the monitor; a synthetic head pose (static,
  sweeping or tilted) is written into the same globals the OpenXR path publishes, "so nothing
  downstream knows or cares where a pose came from, which is what makes this a fair test rather
  than a parallel one" (F-111); a synthetic fixed hand offset proves the hand composition flat ("the
  gun must sit at one place relative to the player and stay there while they walk and turn");
  scripted flat sessions drive the weapon pin and capture named backbuffer shots pinned to one eye,
  because under alternate-frame stereo two captures a second apart are usually different eyes and
  "every pixel comparison was meaningless". Pure geometry (`asym_projection.h`,
  `world_rect_gate.h`) is compiled by offline checks from the same headers with known headset
  numbers and a negative control (F-115). There is no simulated runtime (Dishonored's `xrsim`,
  F-09), no CI and no unit-test suite; the self-checks run at startup in the shipped build (the
  registry self-check with its bogus key, the hook registry's positive and negative halves).
- **Instruments that cannot lie quietly.** The rules, stated in the code and paid for: *every
  instrument prints a line either way* ("silence never means the instrument stopped"); *a zero
  everywhere is a broken instrument, not luck* (three spike columns read 0.00 across fourteen
  windows because a patch never wired them); *a null needs the instrument to have reached its
  target* (the d-pad's "evaluated / skipped" counters); *read the counts, not the word PASS* (a
  cross-check passed with three empty lists after a file move); *a write that reads back proves
  nothing* (the engine's `VerifyUserCmd` reverted every late write before the wire); *a run of an
  unchanged config looks exactly like a successful fix* (hence the content hash, F-133); *the
  installed build is the built build* (hence the DLL's write time in the banner and an installer
  that refuses while the game runs). Our F-101 … F-106 say the same; TF2VR adds the built-in
  positive controls.
- **Logs** (F-92, F-105, F-112, F-133 notes). Quiet by default with an allowlist tightened against a
  real log; `diag.verbose` for a dev run; a 250 ms flusher thread so a hang loses at most a quarter
  second; rotation `prev-1 … prev-8` with the rotation report as line 1 and an explicit "APPENDED to
  the previous session" when a second game instance holds the file; `%LOCALAPPDATA%`, not `%TEMP%`,
  "because Storage Sense sweeps `%TEMP%` and a log a scheduled task can throw away is not a record";
  `logdig` for reading it. The banner: the version tag, the git sha stamped at **build** time with a
  `+` for a dirty tree (a configure-time sha "announced a commit from hours earlier"),
  `__DATE__ __TIME__` and the DLL's own write time, forced past the filter — "the one line that says
  WHICH BUILD THIS IS was absent from every log a user could ever send" until it was.
- **Crashes and hangs** (F-131, F-132). A vectored handler at the front of the chain plus the
  chained filter, four records at most, the arm state as a bitmask the tick publishes (no getters in
  the handler — one took a mutex the crashing thread could hold), the XR breadcrumb, the hook-site
  registry, `VirtualQuery` on an address in no module (and the PE export name read from the mapped
  image, for an unloaded DLL), a linker map, a self-test fault behind an ini flag; a stall watchdog
  thread that suspends the stuck thread after 2 s and names the instruction; a resource watch that
  logs failed D3D creations and the video-memory budget. KNOWN-ISSUES 10 is the model of an honest
  hang report: six sightings, the fault lands in the runtime's own D3D11 path 39–43 s *after* the
  loop stops, the mod's share of a frame 0.6 %, and "two honest caveats, because 'not ours' without
  proof is how this kind of bug survives a release".
- **Release discipline** (F-96 note, F-130, F-134). The version in one CMake line read by the
  header, the banner, the panel and the packager; `build.ps1` never deploys and `install.ps1`
  refuses while the game runs and prints the hash on both sides; `package.ps1` refuses placeholders,
  a documented hash that does not match the DLL, and anything personal in the box, generates
  `README-FIRST.txt` from a template, ships every licence, and prints the inventory; a clean-profile
  pair of scripts parks the development folder and installs exactly the zip; the shipped ini
  documents the wearer's tested defaults with every line commented out and a script proves the code
  agrees (F-15 note). KNOWN-ISSUES carries severity, what you see, workaround and where it stands per
  issue. Documentation drift exists too: BUILDING.md names two pre-commit gates and an uninstall
  script that are not in the repository, and INSTALL.md says sixty settings where CONTROLS.md says
  seventy-two.
- **Process shape.** One wearer, one agent, headset runs as the currency ("fourteen headset runs
  went into this", "a whole headset run was spent on that once"), measurement before fix, a lever
  for every change with its A/B, the history squashed to one commit so the comments are the only
  record — Dishonored VR's process (F-101 … F-109) without its docs folder, board or CI. Nothing
  here changes our WORKFLOW.md; it confirms D-014's launch budget and the "prove the gate can fail"
  rule.

**What to take for the queue, by ID:** F-115, F-116, F-117 and F-118 into #18 / #19 / #20 and
#130; F-119 and F-120 into #24, #27 and #28; F-121, F-122 and F-123 into #30, #32, #33 and #74;
F-125 into #36 and #75; F-129 into #73; F-111 into #112 / #113; F-112 and F-133 into #129 / #131;
F-128 is a decision for #34. After the MVP: F-113 and F-126 (with F-19 and F-49), F-130, F-131 and
F-132 (with #88 and #132), F-114; later: F-124, F-127.

### 12. CircuitLord's Titanfall 2 VR (#120)

Read on 2026-10-09 for #120. **The mod is closed source**, so this section is research by
observation, and every row says what its evidence is. What was read in full: the installer repository
at commit `b493821a6` (branch `main`, installer v1.2.9) — the README, `manifest-v3.json`, `LICENSE`,
`THIRD-PARTY-NOTICES.md`, `Titanfall2Installer.cs` (310 lines), `PackageInstaller.cs`,
`SteamGameLocator.cs`, `RepoClient.cs`, `CrashReportService.cs`, `LaunchPermissions.cs`,
`SelfUpdater.cs`, `OwnedFileStore.cs`, `InstallerFileSystem.cs` and `tests/InstallerValidation/Program.cs`;
the release notes (v1.1.1–v1.2.9); issue #18; the READMEs of the four add-ons; and the press and
guides listed in [Sources](#sources). **What could not be read:** the mod and its zip (73.9 MB;
closed), the author's X posts (the page answers HTTP 402; the announcement text was seen only as
a search snippet), his Discord and videos. Line numbers are omitted because the installer changes
every few days (v1.2.0 to v1.2.9 in eight days).

#### 12a. Full-body IK — the question that cannot be answered from outside (#36, D-009)

| Question | Answer | Evidence |
|---|---|---|
| Does the mod have full-body IK? | Yes; it is the first feature in the README and the announcement | docs |
| Is the body visible? | Yes, "you see your pilot's body when you look down" | report (arvrtips) |
| What drives it? | Head and both hands ("estimates the whole posture from head and hand movement") | report (MoguraVR) |
| Shoulder offsets, body-yaw rule, arm reach, elbow pole | **not known** | — |
| Legs while walking, crouching, wall-running, sliding | **not known**; whether they are tracked or animated is unverified | — |
| Hand-back to scripted animation (cutscenes, Titan embark) | **not known**; cutscenes play on a 2D panel by default | report |
| Calibration / body-size settings | not mentioned anywhere; the options menu is reported as HUD, Titan HUD, gun angle | report |

The practical answer for #36: nothing in this row set changes D-009; the numbers come from
Dishonored VR's `ARM_IK.md` (F-52) and VHVR's VRIK, and the only way to learn what CircuitLord's body
does is to play the mod (it is free, the base game is cheap in sales) — a question for the owner, not a
task this research creates.

#### 12b. Weapons, manual reload and the add-ons (#75, #39, #37; the bow later)

- **What the mod does** (reports): hands aim the guns, scopes work when the gun is raised to the eye,
  a manual reload exists for every gun (B starts it; the magazine is grabbed from the body, inserted,
  the slide or bolt racked; the Wingman reloads by a gesture), a closed fist punches, the grip holds
  and releasing it holsters, the off hand on the front of a rifle steadies it (F-136, F-137).
- **What the add-ons reveal about the architecture** (add-on READMEs): the native plugin passes the
  hand positions to the game's script **every frame**; scripts own clip and reserve counts and the
  pickup cone (`tf2vr_pickups.gnut` in `Titanfall2VR.Cockpit`, load priority 2; the add-on overrides it
  at 3); a flick is detected in script from hand speed, the pull is a server-side `SetOrigin`; the
  auto-reload add-on works because "the engine's clip and reserve counts are plain script values".
  The reload *steps* stay in the plugin. This is the opposite of our situation (we own the engine),
  but it confirms the shape of #37 / #75: a thin native layer that publishes hand poses each frame,
  and game rules that read them.
- **The one weapon-handling gap for Arx** is the unarmed punch (F-137); nothing here helps with the
  swing detector — that stays Dishonored VR's `swing_core.h` (F-59).

#### 12c. Installer and distribution (#88, #79, #80, #132, epic #9) — the open half, in code

The launch sequence, as the code does it (F-144 … F-147):

1. **Find the game**: registry key → Steam libraries → hand-picked folder (must contain the exe).
2. **Check prerequisites** (the EA app, signed in) with a visible fix for each.
3. **Install** into a `TF2VR` profile (F-141): the mod zip and Northstar are downloaded over HTTPS,
   hashed (F-142), the **game-derived assets are built from the player's own install** by a patcher
   *before* the game folder changes (F-145), then files are extracted, stale files of the previous
   release removed, and an ownership record written (F-143).
4. **Launch in VR**: refuse on proxy DLLs, warn on elevated launchers, scrub inherited VR variables,
   write `launcher.txt`, start `crash_monitor.exe`, which starts the game (F-146, F-147).
5. **After a crash**: one button zips the evidence, without dumps (F-147).

What this tells us about the shape of #88 / #80 (nothing here is a decision):

| Our task | What the installer shows | Difference for us |
|---|---|---|
| #79 find the data | registry-first discovery plus a vdf regex, no `appmanifest` | we read `appmanifest_1700.acf` (F-97) and have it in PowerShell already |
| #80 launch from SteamVR | Titanfall's flow is the reverse: the installer launches the game, the runtime must already run | ours is a Steam app: SteamVR starts it (D-004, `register-steamvr.ps1`) |
| #88 tester build | a 350 KB exe + a 74 MB zip, hash-checked | a zip for one friend: README with the hash (F-142), the gate "no game data" (F-145), the checklist (F-148, F-149) |
| #132 D4 crash reports | a parent monitor + report with the build hash | in-process handler first; the parent is an option (F-147) |
| epic #9 legal | the mod ships patches, never the game's files | our engine reads the player's `.pak` files in place — the strongest possible form of the same rule |

Release notes, by theme (installer; v1.1.1 / v1.1.2 in August are Big Walk only):

| Version | Date (2026) | What changed |
|---|---|---|
| v1.2.0 | 09-29 | multi-game support, Titanfall 2 VR first |
| v1.2.3 | 09-30 | OpenXR launch failures explained; admin request when the game is in Program Files |
| v1.2.4 | 09-30 | updates before launch; install progress bar; game exit reasons in crash reports; the VR campaign always starts a new save |
| v1.2.5 | 09-30 | campaign saves open from the new LocalAppData location |
| v1.2.6 | 10-01 | the mods check the headset startup themselves; launch stops when another mod replaced `dxgi.dll` / `d3d11.dll`; Northstar download cached |
| v1.2.7 | 10-02 | launcher errors in crash reports; ZIP installs return to the version menu; mod licence files installed |
| v1.2.8 | 10-03 | release channels come from the download list; crash reports include the latest session diagnostics |
| v1.2.9 | 10-06 | warns before launch if Steam, the EA app or the installer runs as admin; mod updates optional at launch |

**Smaller facts worth keeping.** The installer is one 350 KB exe that updates itself by renaming the
running exe to `.old.exe`, writing the new one, rolling back the rename on failure, and retrying the
delete 12 × 500 ms at the next start (`SelfUpdater.cs`) — irrelevant to a Steam app. The Big Walk side
of the same installer waits for BepInEx's log line `Chainloader startup complete` to know the mod is up
and treats a leftover `GameSession.lock` as a crash — the cheap version of our "Clean shutdown" line.

#### 12d. Controls — against [RESEARCH-controllers.md](RESEARCH-controllers.md)

Reported layout (arvrtips; the author publishes none): right trigger fires, **B** starts a manual
reload, grips hold and holster, thumbsticks move and turn, a closed fist punches, abilities on four
grip directions in the Titan cockpit; snap turn is an option. Nothing contradicts the proposed default
layout in RESEARCH-controllers.md. Two facts help #30 / #35: (1) a release that bound **Index and
Touch only** (per the Linux port's README) was well received and a third party filled the rest with
SteamVR actions (F-139) — the order Index → Quest 3 is the order of the successful release; (2) the
bindings add-on keeps the **grip for holding** and puts its new "reload grab" on the trigger — one
more example that the grip means "hold", as in our design rule 3 and the proposed layout (grab on the
right grip). The one collision is semantic: B is "reload" there and "inventory" for us; irrelevant across games.

#### 12e. The ecosystem around a closed mod

Within two weeks other people wrote a Linux / Proton port (MIT), a bHaptics add-on, a force-grab
add-on, an auto-reload add-on and a bindings add-on. What made that possible: an MIT installer with
published hashes, a data-driven launch (`launch.json` next to `crash_monitor.exe`, with placeholders
for profile, width, height and sound), a script layer (Northstar mods) and a documented place to drop
packages (`TF2VR\packages\`; the bindings add-on says the first launch only registers it and it works
from the second). The Linux README documents the launch chain end to end — `crash_monitor.exe` →
`Titanfall2VRLauncher.exe`, `SteamGameId=1237970` to enable Proton's OpenXR bridge, DXVK required,
EA sign-in timing, a patched `mmdevapi.dll` for an audio-capture request — which is also a map of
everything Windows-specific in the launch. Arx Libertatis is cross-platform, but our development and test setup is
Windows with SteamVR (CLAUDE.md); a Linux / Steam Deck VR build is not on the list, and this is the
evidence of what it would take if it is ever asked for.

#### 12f. Trust: press pages are not licence evidence

MoguraVR's summary table lists the mod as "MIT"; the author's README says "the mods are closed
source and not covered by the MIT license. All rights reserved." Two press pages (MoguraVR, PCVR
Central) also say a player can copy existing saves; the installer's own test asserts that the copy
button does **not** exist (F-141). When primary and secondary sources disagree, the code and the
author's README win; the entry is in [TRAPS.md](TRAPS.md).

**What to take for the queue, by ID** (the owner decides — this research creates no tasks):
- **Before checkpoint B / #88:** F-145 (no game data in the zip — a line in the gate), F-146 (the
  proxy-DLL / elevation / environment facts as log banner lines in #115 and the launch scripts),
  F-148 (the tester README's content), F-149 (the checklist of what breaks first, for #70 and #88),
  F-141 (confirm D-007 for VR saves; never write under Documents).
- **With #79 / epic #9:** F-144 (the message and the button for "app 1700 is missing").
- **With #30 / #35:** F-139 (a released example of the per-concept action approach).
- **With #43–#45 / #34:** F-150 (first-cast prompt, reference page).
- **After the MVP:** F-147 (a crash witness), F-140 (the four-setting menu), F-136 (zones need a
  highlight), F-142 / F-143 (only if an unzip-over-game package ever ships).
- **Decide against D-010 first:** F-138 (pointing at an item to pull it).
- **Nothing to take:** F-135 (no IK parameters exist outside the mod), F-151, F-152.

## Sources

Dishonored VR (branch `VR-Main`, commit `b15e4d32a`):
[README](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/README.md),
[LICENSE](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/LICENSE),
[THIRD_PARTY_NOTICES.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/THIRD_PARTY_NOTICES.md),
[docs/ARCHITECTURE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/ARCHITECTURE.md),
[docs/VERIFICATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/VERIFICATION.md),
[docs/TRAPS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TRAPS.md),
[docs/KNOWN_ISSUES.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/KNOWN_ISSUES.md),
[docs/RESEARCH.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/RESEARCH.md),
[docs/MODEL_WORKFLOW.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/MODEL_WORKFLOW.md),
[docs/LINEAR_AND_GITHUB.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/LINEAR_AND_GITHUB.md),
[docs/dishonored/TESTING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/TESTING.md),
[src/tools/xrsim](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/src/tools/xrsim),
[src/tools/xr_hello32/main.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/tools/xr_hello32/main.cpp),
[src/core/vr/openxr_input.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/vr/openxr_input.cpp),
[src/game/dishonored/melee.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/melee.cpp),
[tools/](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/tools) (`xrsim-*.ps1`, `xrsim/*.xrs`, `boot.ps1`, `game-cmd.ps1`, `eye-check.ps1`, `soak.ps1`, `*-host.ps1` + `*-tests.cpp`).
Gameplay, hands, camera and UI docs under `docs/dishonored/`:
[PHYSICAL_SWING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PHYSICAL_SWING.md),
[PLAN-contact-sword.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-contact-sword.md),
[PLAN-physical-interaction.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-physical-interaction.md),
[CHOKE_GESTURE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CHOKE_GESTURE.md),
[VR-33-HANDS-AND-WEAPONS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-33-HANDS-AND-WEAPONS.md),
[ARM_HAND_SPLIT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_HAND_SPLIT.md),
[ARM_IK.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ARM_IK.md),
[HAND_POSE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HAND_POSE.md),
[ANIM-HANDOFF-PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/ANIM-HANDOFF-PLAN.md),
[WEAPON_MIRROR_PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/WEAPON_MIRROR_PLAN.md),
[CINEMATIC_FOV_AND_HANDS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CINEMATIC_FOV_AND_HANDS.md),
[VR-36-BLINK-RAY.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-36-BLINK-RAY.md),
[VR-57-AIM-PIPELINE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-57-AIM-PIPELINE.md),
[VR-57-MODEL-RAY.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/VR-57-MODEL-RAY.md),
[CROSSHAIR_PLAN.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CROSSHAIR_PLAN.md),
[CINEMATIC_HEAD_TRACKING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CINEMATIC_HEAD_TRACKING.md),
[STANDING_PITCH_ROLL_ARC.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/STANDING_PITCH_ROLL_ARC.md),
[DOWNWARD_CLAMP_REVIEW.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/DOWNWARD_CLAMP_REVIEW.md),
[HEAD_MOTION_120HZ.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HEAD_MOTION_120HZ.md),
[PLAN-VR-165-camera-displacement.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-VR-165-camera-displacement.md),
[STEREO_STATE_TRANSITIONS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/STEREO_STATE_TRANSITIONS.md),
[GAMEPLAY_STATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/GAMEPLAY_STATE.md),
[HUD_ANCHORS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ANCHORS.md),
[HUD_ELEMENTS_HOWTO.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/HUD_ELEMENTS_HOWTO.md),
[MONO_ANCHOR_UI_STATE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/MONO_ANCHOR_UI_STATE.md),
[F10_MOTION_CONTROLS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/F10_MOTION_CONTROLS.md),
[DESKTOP_MIRROR.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/DESKTOP_MIRROR.md),
[CONTROLLER_BINDS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_BINDS.md),
[CONTROLLER_EMULATION.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/CONTROLLER_EMULATION.md),
[PLAN-mod-spacewarp.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-mod-spacewarp.md),
[PLAN-motion-vectors-dlss.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/dishonored/PLAN-motion-vectors-dlss.md);
process and distribution:
[CLAUDE.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/CLAUDE.md),
[AGENTS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/AGENTS.md),
[CONTRIBUTING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/CONTRIBUTING.md),
[docs/CODE_REVIEW.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/CODE_REVIEW.md),
[docs/CRASH_SUPPORT.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/CRASH_SUPPORT.md),
[docs/TROUBLESHOOTING.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/TROUBLESHOOTING.md),
[docs/INSTALLER.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/INSTALLER.md),
[docs/BRVR_LAUNCHER_RESEARCH.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/BRVR_LAUNCHER_RESEARCH.md),
[docs/STATUS.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/STATUS.md),
[docs/ROADMAP.md](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/docs/ROADMAP.md),
[.github/](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/.github),
[release/](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/release),
[src/core/util/crash.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/crash.cpp),
[src/core/util/log.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/util/log.cpp),
[src/core/framework/command.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/core/framework/command.cpp),
[src/tools/installer/](https://github.com/VR-Stereo-Hub/Dishonored-VR/tree/VR-Main/src/tools/installer),
[src/game/dishonored/crouch.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/crouch.cpp),
[src/game/dishonored/snap_turn.cpp](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/snap_turn.cpp),
[src/game/dishonored/swing_core.h](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/src/game/dishonored/swing_core.h),
[release/dishonored_vr.ini](https://github.com/VR-Stereo-Hub/Dishonored-VR/blob/VR-Main/release/dishonored_vr.ini).

VHVR (branch `master`, commit `d4d7b9523`):
[README](https://github.com/brandonmousseau/vhvr-mod/blob/master/README.md),
[README.development.md](https://github.com/brandonmousseau/vhvr-mod/blob/master/README.development.md),
[LICENSE](https://github.com/brandonmousseau/vhvr-mod/blob/master/LICENSE),
[ValheimVRMod/Scripts](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/Scripts),
[ValheimVRMod/Patches](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/Patches),
[ValheimVRMod/VRCore](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/VRCore),
[ValheimVRMod/Utilities](https://github.com/brandonmousseau/vhvr-mod/tree/master/ValheimVRMod/Utilities).
Files read in full: `Scripts/` WeaponCollision, WeaponWield, LocalWeaponWield, TwoHandedGeometry,
SpearWield, StabStick, AttackTargetMeshCooldown, MeshCooldown, SwingLaunchManager, ThrowableManager,
Block/Block, Block/ShieldBlock, Block/WeaponBlock, Block/FistBlock, FistCollision, BowManager,
BowLocalManager, CrossbowManager, MagicStaffUtils, SwingableStaffManager, ShootingStaffManager,
SummonerManager, HandGesture, InterhandItemTransfer, LeftHandQuickMenu, RightHandQuickMenu,
QuickAbstract, WristStatusEffects, VrikCreator, GesturedLocomotionManager, FadingManager;
`Utilities/` PhysicsEstimator, WeaponUtils, WeaponColData, RoomscaleSecondaryAttackUtils,
BowAnatomy, CrossbowAnatomy, Pose, EquipScript, PlayerDriftFix, StabilizedCameraUpdater,
CameraUtils, FullWidthMirror, VRScreenshot, LogUtils, ApplicationManifestHelper, VHVRConfig;
`VRCore/` VRManager, VRPlayer, `VRCore/UI/` VRGUI, VRHud, HudElements/*, SoftwareCursor,
LaserPointerChords, CrosshairManager, EnemyHudManager, NpcTextBubble, VRCinematicScreen,
ConfigSettings, MissingBindingsPrompt, VRControls; `Patches/` CollisionPatches, ShieldPatches,
ProjectilePatches, BowAndFishingPatches, BHapticsPatches, ControlPatches, EquipPatches,
EyeRotationPatch, RoomscalePatches, FixPatches, InteractionPatches, UIPatches, TextInputPatches;
`Unity/ValheimVR/Assets/StreamingAssets/SteamVR/` actions.json, bindings_knuckles.json,
bindings_oculus_touch.json; `ValheimVRMod.cs`, `ValheimVRMod.csproj`, `make-release.cmd`.

Alternatives for headless testing:
[Monado](https://monado.freedesktop.org/) and its
[Windows build guide](https://gitlab.freedesktop.org/monado/monado/-/blob/main/doc/winbuild.md);
[Meta XR Simulator — native intro](https://developers.meta.com/horizon/documentation/native/xrsim-intro/)
(graphics API list; OpenGL unsupported);
SteamVR null driver: [Valve wiki — steamvr.vrsettings](https://developer.valvesoftware.com/wiki/SteamVR/steamvr.vrsettings),
[SteamVRNoHeadset](https://github.com/username223/SteamVRNoHeadset);
[OpenVR SDK `samples/driver_sample`](https://github.com/ValveSoftware/openvr/tree/master/samples/driver_sample);
OpenXR loader runtime discovery (`XR_RUNTIME_JSON`): [OpenXR-SDK loader docs](https://github.com/KhronosGroup/OpenXR-SDK-Source/blob/main/specification/loader/runtime.adoc).

Titanfall 2 VR (branch `main`, commit `8c50a7d`, v0.1.1; read on 2026-10-09 for #118):
[README](https://github.com/kyus2001/titanfall2vr/blob/main/README.md),
[LICENSE](https://github.com/kyus2001/titanfall2vr/blob/main/LICENSE),
[THIRD_PARTY_NOTICES.md](https://github.com/kyus2001/titanfall2vr/blob/main/THIRD_PARTY_NOTICES.md),
[BUILDING.md](https://github.com/kyus2001/titanfall2vr/blob/main/BUILDING.md),
[INSTALL.md](https://github.com/kyus2001/titanfall2vr/blob/main/INSTALL.md),
[CONTROLS.md](https://github.com/kyus2001/titanfall2vr/blob/main/CONTROLS.md),
[KNOWN-ISSUES.md](https://github.com/kyus2001/titanfall2vr/blob/main/KNOWN-ISSUES.md),
[CHANGELOG.md](https://github.com/kyus2001/titanfall2vr/blob/main/CHANGELOG.md),
[plugin/CMakeLists.txt](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/CMakeLists.txt),
[plugin/cmake/version_stamp.cmake](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/cmake/version_stamp.cmake),
[plugin/titanfall2vr.ini.example](https://github.com/kyus2001/titanfall2vr/blob/main/plugin/titanfall2vr.ini.example),
[tools/](https://github.com/kyus2001/titanfall2vr/tree/main/tools) (`build.ps1`, `install.ps1`, `package.ps1`, `logdig`, `defaults-vs-ini.py`, `orphan-statements.py`, `asym-projection-check.cpp`, `bmpdiff/`, `bmpcrop/`, `bmpshift/`, `cleanprofile/`, `pescan/`, `package/`).
Files read in full under [plugin/src/](https://github.com/kyus2001/titanfall2vr/tree/main/plugin/src):
`host/host.h`, `host/plugin.cpp` (the arm ladder, init and the frame body); `xr/xr_context.h`,
`xr/xr_context.cpp` (init, events, head pose, banner, views, the frame core, pacing, the menu quad,
the spike ring), `xr/xr_input.h`, `xr/xr_input.cpp`, `xr/asym_projection.h`,
`xr/render_resolution.h`, `xr/render_resolution.cpp` (scale, cache, derived setup);
`input/vr_input.h`, `input/vr_input.cpp` (the frame body and the turn settings),
`input/xinput_pad.h`, `input/xinput_pad.cpp` (the pad composition and haptics),
`input/dpad_gesture.h`, `input/dpad_gesture.cpp`, `input/aim_cmd.h`; `hands/hand_pose.h`,
`hands/hand_pose.cpp` (brace, composition, publish, pivot nudge), `hands/weapon_settings.h`,
`hands/placement_pin.h`, `hands/use_target.h`, `hands/viewmodel_hide.h`, `hands/arms_collapse.h`,
`hands/viewmodel_visibility.h`, `hands/viewmodel_bones.h`, `hands/titan_state.h`;
`camera/player_eye.h`, `camera/player_eye_hook.h`, `camera/player_eye_hook.cpp`,
`camera/camera_hook.h`, `camera/camera_hook.cpp` (recentre, head tracking, neck model),
`camera/view_build_hook.h`, `camera/placement_watchpoint.h`; `ads/ads_lock.h`, `ads/ads_zoom.h`,
`ads/ads_probe.h`; `core/config.h`, `core/config.cpp` (parser, settings table, self-check,
staging, persistence), `core/settings_registry.h`, `core/flat_harness.h`, `core/flat_harness.cpp`,
`core/diagnostics.h`, `core/diagnostics.cpp`, `core/crash_recorder.h`, `core/crash_recorder.cpp`;
`ui/menu_overlay.h`, `ui/menu_overlay.cpp`; `render/stereo_targets.h`, `render/scene_reentry.h`,
`render/temporal_lever.h`, `render/present_hook.h`, `render/stereo_experiment.h`,
`render/world_rect_gate.h`; `hud/hudwarp.h`, `hud/crosshair_scale.h`, `hud/world_marker.h`,
`hud/lock_hud.h`, `hud/rui_immediate.h`; `diag/stall_watchdog.h`, `diag/plugin_cost.h`,
`diag/xr_breadcrumb.h`, `diag/resource_watch.h`; `engine/hook_registry.h`.

CircuitLord's Titanfall 2 VR (installer: branch `main`, commit `b493821a6`, v1.2.9; read on 2026-10-09 for #120).
The installer, in full or in the parts the rows name:
[README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/README.md),
[LICENSE](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/LICENSE),
[THIRD-PARTY-NOTICES.md](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/THIRD-PARTY-NOTICES.md),
[manifest-v3.json](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/manifest-v3.json),
[releases](https://github.com/CircuitLord/CircuitLordVRModInstaller/releases),
[issue #18](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/18),
[src/Installer/Installers/Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/src/Installer/Installers/Titanfall2Installer.cs),
[src/Installer/Services/](https://github.com/CircuitLord/CircuitLordVRModInstaller/tree/main/src/Installer/Services)
(`PackageInstaller`, `SteamGameLocator`, `RepoClient`, `CrashReportService`, `LaunchPermissions`,
`SelfUpdater`, `OwnedFileStore`, `InstallerFileSystem`, `GameStartupWatcher`),
[tests/InstallerValidation/Program.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/main/tests/InstallerValidation/Program.cs),
[repotools/](https://github.com/CircuitLord/CircuitLordVRModInstaller/tree/main/repotools).
Community add-ons:
[djnewage/tf2vr-mods](https://github.com/djnewage/tf2vr-mods) (`TF2VR.DistanceGrab`, `TF2VR.AutoReload`; no licence stated),
[Fdrph/TF2VRBetterBindings](https://github.com/Fdrph/TF2VRBetterBindings) (MIT),
[floh-bhaptics/Titanfall2VR_bhaptics](https://github.com/floh-bhaptics/Titanfall2VR_bhaptics) (no licence stated),
[Monkellie/tf2vr-linux](https://github.com/Monkellie/tf2vr-linux) (MIT).
Press and guides (secondary; checked against the code where the code could answer):
[UploadVR](https://www.uploadvr.com/titanfall-2s-vr-mod-is-stellar-and-its-installer-is-the-best-thing-about-it/),
[Road to VR](https://roadtovr.com/titanfall-2-vr-mod-circuitlord/),
[MoguraVR](https://www.moguravr.com/?p=378357),
[Inven Global](https://www.invenglobal.com/articles/26827/i-am-jack-cooper-titanfall-2-vr-mod-causes-a-stir),
[VR.org](https://vr.org/articles/titanfall-2-vr-mod-how-to-install-circuitlord-northstar-2026),
[arvrtips](https://arvrtips.com/titanfall-2-vr-mod/),
[PCVR Central](https://pcvrcentral.com/mods/titanfall-2-circuitlord-vr).
Not readable: the mod itself (closed), the author's X posts
([announcement](https://x.com/CircuitLord/status/2105099559391416752) — HTTP 402; the text was seen only as a search snippet), his Discord and videos.
