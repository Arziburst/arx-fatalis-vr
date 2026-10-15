# Test playground generator (#321)

Generates the test playground level (area 30, `PlaygroundArea` in `src/game/Levels.h`) with
[arx-level-generator](https://github.com/arx-tools/arx-level-generator) (MIT, pinned in `package-lock.json`):
one stone room in the game's style with a table along the north wall, light, and free spots for the NPC and
levers (321.2). The repository holds only the generator's source; the level is written on the player's own machine
to `%LOCALAPPDATA%\ArxFatalisVR\playground\<folder>-<hash>` (one per working copy, regenerated when a source changes; a `--data-dir` for the game) and uses the original game's
textures by name — no game data is copied.

- `layout.mjs` — the room's layout in one place: room size, hero start, table, NPC spot, lever wall, lights.
- `build.mjs` — the generator (`npm ci && npm run build [-- <outputDir>]`).
- `playground-lib.ps1` — what `-Playground` of `run-windows.ps1`, `dev\test-pr.ps1` and `dev\smoke-test.ps1` uses:
  generates the level when it is missing or was generated from other sources (`sources.sha256` next to it), returns
  `--data-dir <level> --loadlevel 30`.

Needs node 20+ (and the .NET runtime for the generator's lighting calculator, which is bundled with the package).
Textures: floor `stone_human_paving`, walls `stone_human_stone_wall`, ceiling `stone_human_stone_wall1`,
table `l4_dwarf_wood_board02` (all native to the original game).
The tester zip (`package-tester.ps1`) does not carry the playground yet.

## Content (321.2)

- **Goblin** (`npc/goblin_base`, the MVP enemy) at `NPC_SPOT`: peaceful by default — its instance script refuses `ouch`
  and `aggression` while `§aggro` is 0, so even a hit does not anger it. Orders arrive as `custom` events:
  `aggressive` (attacks at once), `peaceful` (`revive -i` + stance reset: full life, back at its spot), `respawn` (the same,
  back into the mode it was in).
- **Two levers** on the north wall right of the table (visible from the start): the mode lever (nearer the table; down =
  aggressive, up = peaceful) and the respawn lever. Both are the game's `fix_inter/lever`, pulled by hand like any lever
  (#38). A dead NPC runs no script events, so the respawn lever revives it from outside (`revive -it <entity>`).
- **The arms lever** (#385.3), third on the wall: each pull steps the VR arms to the next model - custom, forearm, hands,
  arx, then custom again (#385.8; it counts pulls in the global `#pg_arms_pulls`, `src/vr/VRArms.cpp` follows it; the
  setting is cfg.ini `vr_arms_model`).
- **Worked by hand** (#38, `DOOR`, `CHEST`, `CORPSE` in `layout.mjs`), all a few steps from the hero's start, each
  the game's own class with its own state: a **door** (`fix_inter/light_door`, `light_door_9001`) standing free ahead-left,
  closed and unlocked (the class locks every door; the instance script unlocks it); a **metal chest**
  (`fix_inter/chest_metal`, `chest_metal_9001`) by the east wall, unlocked, holding `CHEST_ITEMS` (a life potion and
  bread, added on `initend` as the original levels do); a **dead goblin** (`npc/goblin_base`, `goblin_base_9002`) ahead
  holding `CORPSE_ITEMS` (cheese and an apple), killed by its own script half a second after the start and refusing
  `ouch` / `aggression` before that. Each is opened, pulled or searched with a hand's reticle and grip + trigger.
- **Weapons**: every folder under `items/weapons` that holds a class script (`WEAPONS` in `layout.mjs`, 32), on a table at
  the west wall. **Consumables** (`CONSUMABLES`: 5 potions, 6 foods, a scroll holding magic missile) on the main table.
- **Hero**: a marker's init script runs `addxp 178000` (level 10, the game's cap) and `rune all`. The player script is not
  used: the generator's one would replace the game's own.
- Instance ids start at `FIRST_ID` (9001): the original levels ship instance scripts for the low numbers of every class,
  which the engine would run for an item without a script of its own.
- The test channel finds these entities by the text variable `£pg_role` their scripts set (`npc`, `lever_mode`,
  `lever_respawn`, `lever_arms`, `door`, `chest`, `corpse`); status.json `playground`, `player.level`, `player.allRunes` and the commands
  `playground pull|kill|place|hit` are described in `docs/vr/TESTING.md`. Sequences: `scripts/dev/xrs/playground-levers.xrs`,
  `scripts/dev/xrs/playground-hands.xrs` (the lever, the door, the chest and the body by each hand, #38), `arms-variants-lever.xrs`.
- The mode lever does nothing while the goblin is dead (a dead NPC runs no script events): use the respawn lever.
