# Arx Fatalis VR — SteamVR app manifest

How SteamVR learns the product name and the tile image of Arx Fatalis VR, and what that gets us.

## Why a manifest

SteamVR knows nothing about an OpenXR application beyond its `applicationName` and the path of the
running executable. While the game starts, the compositor shows a **loading card**; for unknown
applications it is a plain card with the application name. A name, a tile image and a SteamVR library
entry come from a **SteamVR app manifest** (`.vrmanifest`), the same mechanism Steam uses for its own
VR titles and the Valheim VR mod uses for itself. The game itself does not link against OpenVR for
this ([D-001](DECISIONS.md#d-001-use-openxr-not-openvr-as-the-vr-api)): the manifest is a JSON file,
and registering it means adding its path to `<Steam>\config\appconfig.json` under `manifest_paths`.

## Files

| File | What |
|---|---|
| `data/vr/arx-fatalis-vr.vrmanifest` | Manifest template: `app_key` `arxfatalisvr.app.arx`, `launch_type` `binary`, name and description. `@BINARY_PATH_WINDOWS@`, `@IMAGE_PATH@` and `@ARGUMENTS@` are filled in at registration time, because SteamVR wants absolute paths |
| `data/vr/key-art.png` | Key art provided by the project owner (1672×941) |
| `data/vr/steamvr-tile.png` | The 460×215 tile SteamVR and Steam use for application "header" images; derived from the key art by `scripts/dev/make-steamvr-tile.ps1` (ImageMagick) |
| `scripts/register-steamvr.ps1` | Writes the filled-in manifest to `%LOCALAPPDATA%\ArxFatalisVR\steamvr\arx-fatalis-vr.vrmanifest` and registers it; `-Unregister` undoes it |

## Usage

```powershell
.\scripts\build-windows.ps1
.\scripts\register-steamvr.ps1            # or -Config Debug; re-run after switching working copies
# restart SteamVR, then launch the game as usual
.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0
```

SteamVR reads `appconfig.json` and the manifests on startup, so it has to be restarted after the
script ran. The manifest points at one `arx.exe` (this working copy, this configuration): SteamVR
matches the *running executable path* against `binary_path_windows`, so after switching to another
working copy or build type, run the script again.

The manifest also carries `--data-dir <Arx Fatalis install>` in `arguments`, so the entry in the
SteamVR library can start the game until the engine finds the data itself (#79). Launching from
SteamVR proper is task #80.

## What SteamVR shows

To be filled in after the in-headset test of #89: whether the loading card for an OpenXR application
uses the manifest name and `image_path`, and if not, what it shows and what the alternatives are
(e.g. matching by `applicationName`, `steam://` launch type, Steam non-Steam shortcut).

## Sources

- SteamVR app manifest format: https://gist.github.com/krzys-h/98464aa2f4a1ad814358f8f078111366
- Loading card title and icon in SteamVR: https://hub.jmonkeyengine.org/t/solved-vr-launch-title-and-icon-in-steamvr/43706
- Examples on a machine with SteamVR: `<Steam>\config\steamapps.vrmanifest`,
  `<Steam>\steamapps\common\SteamVR\resources\config\runtime.vrmanifest`
