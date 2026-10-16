# VR hands: hand shapes

The hand shapes of the custom VR hand (#385.7): the controls fallback closes each finger of the glove from the
open hand toward MRTK3's Grab (the index finger by the trigger, the middle, ring and little finger by the grip,
the thumb by its rest on a control) when the runtime gives no hand joints.

- `ArticulatedHandPose_Grab.json`: MRTK3's articulated hand pose "Grab", 26 joints in the OpenXR joint layout
  (`XR_EXT_hand_tracking`), each with a position (meters) and a rotation, recorded for the right hand in Unity's
  left-handed space (y up, z forward). `vr::loadHandShape` (src/vr/HandRig.h) converts it to OpenXR's
  right-handed space; the left hand is its mirror.
  - Source: https://github.com/MixedRealityToolkit/MixedRealityToolkit-Unity, commit
    `acdec2d5a8e290892be4d6ceceaed51fa5563700` (acdec2d), file
    `org.mixedrealitytoolkit.input/Subsystems/SyntheticHands/HandPoses/SimulatedArticulatedHandshapes.cs`
    (https://github.com/MixedRealityToolkit/MixedRealityToolkit-Unity/blob/acdec2d5a8e290892be4d6ceceaed51fa5563700/org.mixedrealitytoolkit.input/Subsystems/SyntheticHands/HandPoses/SimulatedArticulatedHandshapes.cs)
  - What was extracted: the JSON of the C# string constant `ArticulatedHandPose_Grab`, with the C# quote escapes
    (`""`) turned into `"`, the source's indentation of 8 spaces removed and the empty lines dropped. Every value,
    joint name and the order are as published.
  - Author: Mixed Reality Toolkit Contributors
  - Licence: BSD 3-Clause, see `LICENSE.md` (the `org.mixedrealitytoolkit.input` package's licence as published at
    that commit, https://github.com/MixedRealityToolkit/MixedRealityToolkit-Unity/blob/acdec2d5a8e290892be4d6ceceaed51fa5563700/org.mixedrealitytoolkit.input/LICENSE.md)
- MRTK3 has no "Fist" hand shape (its shapes at that commit: Flat, Grab, Open, OpenSteadyGrabPoint, Pinch,
  PinchSteadyWrist, Poke, ThumbsUp, Victory, TeleportStart, TeleportEnd). A hand holding a weapon closes on the
  hilt pose instead (`vr::hiltPose`, built in code from the `vr_hilt_*` angles), or on a pose captured in the
  headset with the channel command `grip capture` (stored in the user directory, not here).

The game reads the file through its resource system as `vr/hands/articulatedhandpose_grab.json` (resource names
are lower case, TRAPS.md). Every asset in this folder must be under a licence compatible with the game's
distribution, with its notice shipped next to it.
