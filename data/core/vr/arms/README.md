# VR arms: hand models

The hands of the VR arms (#385): `Hand_Glove_low_L.gltf` and `Hand_Glove_low_R.gltf`, the low-poly
glove hands of Godot XR Tools, skinned to the 26 joints of `XR_EXT_hand_tracking`
(Wrist_R, Palm_R, Thumb_Metacarpal_R ... Little_Tip_R; `_L` for the left hand).

- Source: https://github.com/GodotVR/godot-xr-tools, commit
  `6c6189bdfcdf855052ef1aeb9f3e0ec4c7a7eb60` (6c6189b), files `addons/godot-xr-tools/hands/model/Hand_Glove_low_L.gltf`,
  `addons/godot-xr-tools/hands/model/Hand_Glove_low_R.gltf` and `addons/godot-xr-tools/hands/License.md`
- Author: DigitalN8m4r3 aka Miodrag Sejic, 2022
- Licence: CC0 1.0 Universal (public domain dedication), see `License.md` as shipped with the models
- Changes: none; the files are as published (glTF 2.0, the buffer embedded as a data: URI, meters)

The game reads them through its resource system as `vr/arms/Hand_Glove_low_L.gltf` /
`vr/arms/Hand_Glove_low_R.gltf` and loads them with `vr::loadGltfSkinnedMesh` (src/vr/ArmMesh.h).
Every asset in this folder must be under CC0 or a licence compatible with the game's distribution.
