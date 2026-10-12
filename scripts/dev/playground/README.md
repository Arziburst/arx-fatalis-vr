# Test playground generator (#321)

Generates the test playground level (area 30, `PlaygroundArea` in `src/game/Levels.h`) with
[arx-level-generator](https://github.com/arx-tools/arx-level-generator) (MIT, pinned in `package-lock.json`):
one stone room in the game's style with a table along the north wall, light, and free spots for the NPC and
levers (321.2). The repository holds only the generator's source; the level is written on the player's own machine
to `%LOCALAPPDATA%\ArxFatalisVR\playground\level` (a `--data-dir` for the game) and uses the original game's
textures by name — no game data is copied.

- `layout.mjs` — the room's layout in one place: room size, hero start, table, NPC spot, lever wall, lights.
- `build.mjs` — the generator (`npm ci && npm run build [-- <outputDir>]`).
- `playground-lib.ps1` — what `-Playground` of `run-windows.ps1`, `dev\test-pr.ps1` and `dev\smoke-test.ps1` uses:
  generates the level when it is missing or older than the generator, returns `--data-dir <level> --loadlevel 30`.

Needs node 20+ (and the .NET runtime for the generator's lighting calculator, which is bundled with the package).
Textures: floor `stone_human_paving`, walls `stone_human_stone_wall`, ceiling `stone_human_stone_wall1`,
table `l4_dwarf_wood_board02` (all native to the original game).
The tester zip (`package-tester.ps1`) does not carry the playground yet.
