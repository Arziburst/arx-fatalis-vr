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
| `scripts/add-steam-shortcut.ps1` | Adds the game to Steam as a non-Steam shortcut in the VR library (see [Launching from SteamVR](#launching-from-steamvr)); `-Remove` undoes it |
| `scripts/steam-shortcuts-lib.ps1` | Reads and writes Steam's binary `shortcuts.vdf`; self-test `scripts/dev/test-steam-shortcut.ps1` (runs in CI) |

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

The manifest carries no `arguments`: the game finds the Arx Fatalis data in the Steam libraries
itself (#79; `-DataDir` on the script adds `--data-dir` for another install). Launching from
SteamVR proper is the next section.

## Launching from SteamVR

Decision [D-004](DECISIONS.md#d-004-zero-setup-launch-like-the-valheim-vr-mod): the player starts
SteamVR and launches the game from inside VR. The SteamVR dashboard library lists the Steam library's
VR games; until Arx Fatalis VR is a Steam app with the VR flag (M3), it gets there as a **non-Steam
shortcut with "Include in VR Library"** — the check box in the shortcut's Steam properties, stored as
`OpenVR = 1` in `<Steam>\userdata\<account>\config\shortcuts.vdf`.

```powershell
.\scripts\build-windows.ps1
# exit Steam first (Steam > Exit): it overwrites shortcuts.vdf when it exits
.\scripts\add-steam-shortcut.ps1                          # or -Config Debug, -LaunchOptions '--loadslot 0'
# start Steam: "Arx Fatalis VR" is in the library; start SteamVR: it is in the dashboard library
.\scripts\add-steam-shortcut.ps1 -Remove                  # undo (Steam exited again)
```

What the script writes (task #80):

- one shortcut **"Arx Fatalis VR"**: `Exe` = this working copy's `arx.exe` (Release unless `-Config`),
  `StartDir` = its folder, icon `data/vr/ARXVR.ico`, `LaunchOptions` from `-LaunchOptions` (none by
  default: the main menu, VR detected by itself), `OpenVR` = 1;
- the library images in `config\grid`, named by the shortcut's app id (CRC32 of the quoted `Exe` and
  the name, top bit set — Steam's own formula): `<id>.png` = the 460×215 tile, `<id>_hero.png` = the
  key art. The script prints the id and the `steam://rungameid/<id << 32 | 0x02000000>` link.

A second run updates the same entry in place (another working copy or configuration gives another
`Exe`, so another app id; the old grid images are removed). Every other shortcut stays byte for byte
as it was; `shortcuts.vdf` is backed up to `shortcuts.vdf.arxvr-<time>.bak` before every change. The
account is the logged-in one (`HKCU\Software\Valve\Steam\ActiveProcess\ActiveUser`), else the most
recent login in `config\loginusers.vdf`; `-AccountId` picks another.

The script **refuses while `steam.exe` runs** (see [TRAPS.md](TRAPS.md): Steam keeps the shortcuts in
memory and writes the file back on exit); `-Force` writes anyway.

A launch from Steam or the SteamVR dashboard runs `arx.exe` directly, not through `run-windows.ps1`:
no game lock, no launch ledger, no fresh-build check (D-014 binds the scripts, not Steam). That is the
player's path, so it is meant for the owner; rebuild the working copy before playing. The vrmanifest
of `register-steamvr.ps1` is independent of the shortcut: it names the loading card.

**Result of the headset test (#80, 2026-10-09): it works.** With the shortcut written by the script
(app id 3841125220, "VR library: yes") and Steam restarted, "Arx Fatalis VR" is in the SteamVR
dashboard library and starts from there in VR. Known wrinkle: the dashboard shows several tile
variants for the game — probably the vrmanifest of `register-steamvr.ps1` and the shortcut as two
entries — and the one that worked had no picture; sorting that out is #213.

If the shortcut ever stops showing up in the dashboard, the fallback flow is: SteamVR running,
start "Arx Fatalis VR" from the Steam library (desktop or Big Picture) — the game detects the
headset by itself (D-004).
## What SteamVR shows

To be filled in after the in-headset test of #89: whether the loading card for an OpenXR application
uses the manifest name and `image_path`, and if not, what it shows and what the alternatives are
(e.g. matching by `applicationName`, `steam://` launch type, Steam non-Steam shortcut).

## Sources

- SteamVR app manifest format: https://gist.github.com/krzys-h/98464aa2f4a1ad814358f8f078111366
- Loading card title and icon in SteamVR: https://hub.jmonkeyengine.org/t/solved-vr-launch-title-and-icon-in-steamvr/43706
- `shortcuts.vdf` binary format and the shortcut app id: https://developer.valvesoftware.com/wiki/Binary_VDF,
  Steam ROM Manager (https://github.com/SteamGridDB/steam-rom-manager)
- Examples on a machine with SteamVR: `<Steam>\config\steamapps.vrmanifest`,
  `<Steam>\steamapps\common\SteamVR\resources\config\runtime.vrmanifest`
