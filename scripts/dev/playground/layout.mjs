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

// The lever wall: the east wall, levers go along it at LEVER_HEIGHT (321.2)
export const LEVER_WALL = { x: ROOM.width / 2, zFrom: -250, zTo: 250 }
export const LEVER_HEIGHT = 130

// One light in the middle of the ceiling and one over the table
export const LIGHTS = [
	{ x: 0, y: ROOM.height - 40, z: 0, radius: 700 },
	{ x: TABLE.x, y: ROOM.height - 80, z: TABLE.z - 60, radius: 400 },
]
