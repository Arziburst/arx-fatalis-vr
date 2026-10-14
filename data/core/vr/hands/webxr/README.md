# VR hands: the WebXR generic hand

The library hand of the VR arms (#467.1, the arms model `library`): `left.glb` and `right.glb`, the generic hand of
the WebXR input profiles. A binary glTF (GLB) each, about 1360 vertices and 2314 triangles, skinned to 25 joints
named as WebXR names them (`wrist`, `thumb-metacarpal`, `thumb-phalanx-proximal`, `thumb-phalanx-distal`,
`thumb-tip`, `index-finger-metacarpal`, `index-finger-phalanx-proximal`, `-phalanx-intermediate`,
`-phalanx-distal`, `index-finger-tip`, and `middle-finger-`, `ring-finger-`, `pinky-finger-` alike): the
`XR_EXT_hand_tracking` joints without the palm. No texture: one material with a grey base colour.

- Source: https://github.com/immersive-web/webxr-input-profiles, commit
  `f4992299601614adbfefd398dc8e281556bb7444` (f499229), files
  `packages/assets/profiles/generic-hand/left.glb` and `packages/assets/profiles/generic-hand/right.glb`
  (https://github.com/immersive-web/webxr-input-profiles/tree/f4992299601614adbfefd398dc8e281556bb7444/packages/assets/profiles/generic-hand)
- Licence: MIT, see `LICENSE.md` (the `packages/assets` package's licence as published at that commit,
  https://github.com/immersive-web/webxr-input-profiles/blob/f4992299601614adbfefd398dc8e281556bb7444/packages/assets/LICENSE.md;
  its README: "Assets are available under MIT license")
- Changes: none; the files are as published (SHA-256 `left.glb`
  `BC67783144944EA1CDA54D9247885825EA5FB9D4651469FE7D00BE517A5C2B87`, `right.glb`
  `291790C14F7F88A7F9BD35330C47392ED8E8D395AE6728F4BB7089F1BC1F2B96`)

How the game reads them (`vr::loadGltfSkinnedMesh`, `vr::findHandJoint`, src/vr/ArmMesh.h):

- The files keep every joint as a child of one `Armature` node, so the loader chains them as
  `XR_EXT_hand_tracking` does (the wrist, each finger's metacarpal to its tip) to bend a finger as one chain.
- The WebXR names map to the `XrHandJointEXT` joints; the hand has no palm joint, so the palm is its middle
  finger's metacarpal joint.

The game reads them through its resource system as `vr/hands/webxr/left.glb` / `vr/hands/webxr/right.glb`
(resource names are lower case, TRAPS.md).
