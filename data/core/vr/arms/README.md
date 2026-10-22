# VR arms: hand models

The hands of the VR arms (#385): `Hand_Glove_low_L.gltf` and `Hand_Glove_low_R.gltf`, the low-poly
glove hands of Godot XR Tools, skinned to the 26 joints of `XR_EXT_hand_tracking`
(Wrist_R, Palm_R, Thumb_Metacarpal_R ... Little_Tip_R; `_L` for the left hand).

- Source: https://github.com/GodotVR/godot-xr-tools, commit
  `6c6189bdfcdf855052ef1aeb9f3e0ec4c7a7eb60` (6c6189b), files `addons/godot-xr-tools/hands/model/Hand_Glove_low_L.gltf`,
  `addons/godot-xr-tools/hands/model/Hand_Glove_low_R.gltf` and `addons/godot-xr-tools/hands/License.md`
- Author: DigitalN8m4r3 aka Miodrag Sejic, 2022
- Licence: CC0 1.0 Universal (public domain dedication), see `License.md` as shipped with the models
  (https://github.com/GodotVR/godot-xr-tools/blob/6c6189bdfcdf855052ef1aeb9f3e0ec4c7a7eb60/addons/godot-xr-tools/hands/License.md)
- Changes: none; the files are as published (glTF 2.0, the buffer embedded as a data: URI, meters)

The game reads them through its resource system as `vr/arms/Hand_Glove_low_L.gltf` /
`vr/arms/Hand_Glove_low_R.gltf` and loads them with `vr::loadGltfSkinnedMesh` (src/vr/ArmMesh.h).
Every asset in this folder must be under CC0 or a licence compatible with the game's distribution.

## Textures (385.5, a first proposal - the owner judges the look in the headset)

- `glove.png` (512x512): the leather glove, laid out over the UVs of the Godot glove (`TEXCOORD_0` of the two
  `.gltf` files; one colour map for the left and the right glove). Dark worn leather in the game's muted palette,
  no logos or text.
  - Source: `addons/godot-xr-tools/hands/textures/glove_caucasian_dark_camo.png` of
    https://github.com/GodotVR/godot-xr-tools (commit `6c6189b`), CC0 1.0 Universal, the same `License.md` as the models.
  - Changes: downsampled 1024 -> 512; every pixel's brightness mapped onto a dark brown leather ramp (the
    skin-coloured fingertips and back of the hand become mid leather, the dark palm and camo grunge stay dark worn
    leather) plus a little noise. No other source is used.
  - Not used: ambientCG materials (https://ambientcg.com, CC0), named in research F-229 / F-283 as the replacement
    if the owner wants a photographic leather or cloth; none of them is in this folder.
