// The playground room's layout (#321): every spot other tasks hang something on lives here.
// Units are Arx units (about 1 cm); the room is centred on CENTRE, the floor is y = 0 and heights are given above the floor
// (build.mjs turns them into Arx coordinates, where up is negative y). X is east, Z is north (docs/rooms.md of the generator).

// The area the level loads as: PlaygroundArea in src/game/Levels.h
export const LEVEL_INDEX = 30

// A cell grid of 160 x 160 cells of 100 units: the room goes in the middle
export const CENTRE = { x: 8000, y: 0, z: 8000 }

export const ROOM = { width: 1000, height: 400, depth: 800 }

// The hero stands near the south wall, facing north (into the room); angle in degrees (Arx "b" = yaw)
export const HERO_START = { x: 0, y: 0, z: -ROOM.depth / 2 + 150, yaw: 0 }

// A table along the north wall: 321.2 puts items on it
export const TABLE = { x: 0, z: ROOM.depth / 2 - 60, width: 300, depth: 90, height: 90, top: 8 }

// Room for an NPC, in front of the table, a few steps from the hero (321.2)
export const NPC_SPOT = { x: 200, y: 0, z: 100 }

// The lever wall: the north wall, right of the table, so that both levers are in front of the hero from his start
// (the hero faces north, they are 20 to 30 degrees right of straight ahead). Levers hang at LEVER_HEIGHT above
// the floor, LEVER_GAP apart; the mode lever is the nearer to the table (321.2); the third, the arms lever, switches
// the VR arms through their models (#385.3, #385.8)
export const LEVER_WALL = { z: ROOM.depth / 2 - 5, xMode: 280, xRespawn: 380, xArms: 470 }
export const LEVER_HEIGHT = 130

// Things worked by hand (#38), all within a few steps of the hero's start: a door standing free west of the middle (the
// game's light_door, closed and unlocked, facing the hero), a metal chest by the east wall holding CHEST_ITEMS, and a dead
// goblin ahead of the hero holding CORPSE_ITEMS. Yaw in degrees as for the levers (180 faces south, into the room)
export const DOOR = { x: -150, z: 150, yaw: 180 }
export const CHEST = { x: 420, z: 60, yaw: 90 }
export const CORPSE = { x: 100, z: -50, yaw: 180 }
// Class paths under graph/obj3d/interactive/items, as the script command `inventory add` takes them
export const CHEST_ITEMS = ['magic/potion_life/potion_life', 'provisions/bread/bread']
export const CORPSE_ITEMS = ['provisions/cheese/cheese', 'provisions/food_apple/food_apple']

// A long table along the west wall for the weapons: WEAPON_COLUMNS columns, rows along the wall (321.2)
export const WEAPON_TABLE = { x: -400, z: 0, width: 180, depth: 620, height: 90, top: 8 }
export const WEAPON_COLUMNS = 4

// Every weapon class of the game: the folders of graph/obj3d/interactive/items/weapons that hold a class script,
// taken from the game's own archives with `arxunpak --list` (the _backup and _common folders are not weapons;
// arrows_2 holds only an icon). Paths are relative to graph/obj3d/interactive (321.2)
export const WEAPONS = [
	'arrow', 'arrows', 'axe', 'axe_2handed', 'bone_weap', 'bow', 'bow_mx', 'club', 'dagger', 'dagger_assassin',
	'dagger_jeweld', 'giant_slayer', 'hammer', 'hammer2', 'hammer_club', 'long_sword', 'long_sword_ciprian',
	'long_sword_feather', 'long_sword_jeweld', 'long_sword_recovery', 'sabre', 'sabre_meteor', 'sabre_meteor_enchant',
	'sabre_mithril', 'sabre_ylside', 'short_sword', 'sword_2handed', 'sword_2handed_forged', 'sword_2handed_meteor',
	'sword_2handed_meteor_enchant', 'sword_2handed_mithril', 'sword_mx',
].map((name) => `items/weapons/${name}`)

// Potions, food and a scroll on the main table, one of each kind (321.2)
export const CONSUMABLES = [
	'items/magic/potion_life', 'items/magic/potion_mana', 'items/magic/potion_green', 'items/magic/potion_purple',
	'items/magic/potion_invisible', 'items/magic/scroll_generic', 'items/provisions/applepie', 'items/provisions/bread',
	'items/provisions/cheese', 'items/provisions/food_apple', 'items/provisions/food_chicken',
	'items/provisions/food_fish_cooked',
]

// Instances get ids from here up: the original levels ship scripts for the low numbers of every class
// (goblin_base_0001 ...), and the engine would run those for an item that has none of its own (321.2)
export const FIRST_ID = 9001

// The hero is level 10, the game's cap (ARX_PLAYER_Modify_XP levels up while i < 11, GetXPforLevel(10) = 178000 xp)
export const MAX_LEVEL_XP = 178000

// One light in the middle of the ceiling and one over the table
export const LIGHTS = [
	{ x: 0, y: ROOM.height - 40, z: 0, radius: 700 },
	{ x: TABLE.x, y: ROOM.height - 80, z: TABLE.z - 60, radius: 400 },
]
