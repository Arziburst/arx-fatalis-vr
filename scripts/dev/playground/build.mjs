// Generates the Arx Fatalis VR test playground level (#321) with arx-level-generator.
//
//   node build.mjs [outputDir]
//
// outputDir defaults to %LOCALAPPDATA%\ArxFatalisVR\playground\level (playground-lib.ps1 passes a per-working-copy folder): a data dir for the game
// (--data-dir), holding graph/levels/level30/*.dlf, *.llf and game/graph/levels/level30/fast.fts.
// The textures are the original game's own (referenced by name, never copied), so the generated
// folder holds no game data and is built on the player's own machine. The layout is in layout.mjs.
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import zlib from 'node:zlib'
import { fileURLToPath } from 'node:url'
import { $, ArxMap, Color, Entity, Material, Player, Rotation, Settings, Texture, Vector3 } from 'arx-level-generator'
import { createLight } from 'arx-level-generator/tools'
import { createBox as createBoxMesh } from 'arx-level-generator/prefabs/mesh'
import { Lever, Marker } from 'arx-level-generator/prefabs/entity'
import { createRoom } from 'arx-level-generator/prefabs/rooms'
import {
	CENTRE, CONSUMABLES, FIRST_ID, HERO_START, LEVEL_INDEX, LEVER_HEIGHT, LEVER_WALL, LIGHTS, MAX_LEVEL_XP, NPC_SPOT, ROOM, TABLE,
	WEAPON_COLUMNS, WEAPON_TABLE, WEAPONS,
} from './layout.mjs'

// A solid-colour RGB PNG (8 bit, no filter), so the generator needs no image library
const crcTable = Array.from({ length: 256 }, (_, n) => {
	let c = n
	for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
	return c >>> 0
})
const crc32 = (buf) => {
	let c = 0xffffffff
	for (const byte of buf) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8)
	return (c ^ 0xffffffff) >>> 0
}
const pngChunk = (type, data) => {
	const body = Buffer.concat([Buffer.from(type, 'ascii'), data])
	const out = Buffer.alloc(body.length + 8)
	out.writeUInt32BE(data.length, 0)
	body.copy(out, 4)
	out.writeUInt32BE(crc32(body), body.length + 4)
	return out
}
const solidPng = (width, height, [r, g, b]) => {
	const header = Buffer.alloc(13)
	header.writeUInt32BE(width, 0)
	header.writeUInt32BE(height, 4)
	header.set([8, 2, 0, 0, 0], 8)
	const row = Buffer.alloc(1 + width * 3)
	for (let x = 0; x < width; x++) row.set([r, g, b], 1 + x * 3)
	const raw = Buffer.concat(Array.from({ length: height }, () => row))
	return Buffer.concat([
		Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
		pngChunk('IHDR', header),
		pngChunk('IDAT', zlib.deflateSync(raw)),
		pngChunk('IEND', Buffer.alloc(0)),
	])
}

const here = path.dirname(fileURLToPath(import.meta.url))
const localAppData = process.env.LOCALAPPDATA ?? path.join(os.homedir(), 'AppData', 'Local')
const outputDir = path.resolve(process.argv[2] ?? path.join(localAppData, 'ArxFatalisVR', 'playground', 'level'))

// Fred's lighting calculator targets .NET 6; a newer runtime runs it with roll-forward
process.env.DOTNET_ROLL_FORWARD ??= 'Major'

const settings = new Settings({
	outputDir,
	cacheFolder: path.join(outputDir, '..', 'cache'),
	assetsDir: path.join(here, 'assets'),
	levelIdx: LEVEL_INDEX,
	calculateLighting: true,
	lightingCalculatorMode: 'DistanceAngleShadowNoTransparency',
	seed: '321',
	mode: 'production',
})

const texture = (t) => ({ texture: t, fitX: false, fitY: false, isRemoved: false })

const map = createRoom(
	new Vector3(ROOM.width, ROOM.height, ROOM.depth),
	{
		textures: {
			floor: texture(Texture.stoneHumanPaving),
			wall: texture(Texture.stoneHumanStoneWall),
			ceiling: texture(Texture.stoneHumanStoneWall1),
		},
	},
)

const wood = Material.fromTexture(Texture.l4DwarfWoodBoard02, {})
// A table: a top plate and four legs
const addTable = (t) => {
	map.add(
		ArxMap.fromThreeJsMesh(
			createBoxMesh({
				position: new Vector3(t.x, -(t.height - t.top / 2), t.z),
				size: new Vector3(t.width, t.top, t.depth),
				materials: wood,
			}),
			{},
		),
	)
	for (const dx of [-1, 1]) {
		for (const dz of [-1, 1]) {
			map.add(
				ArxMap.fromThreeJsMesh(
					createBoxMesh({
						position: new Vector3(t.x + dx * (t.width / 2 - 12), -(t.height - t.top) / 2, t.z + dz * (t.depth / 2 - 12)),
						size: new Vector3(14, t.height - t.top, 14),
						materials: wood,
					}),
					{},
				),
			)
		}
	}
}
addTable(TABLE)
addTable(WEAPON_TABLE)

for (const l of LIGHTS) {
	map.lights.push(createLight({ position: new Vector3(l.x, -l.y, l.z), radius: l.radius, color: Color.fromCSS('#ffe2b0'), intensity: 1.3 }))
}

// ArxMap.move() fails on lights (arx-level-generator 20.1.0 Light has no move()): move the polygons and the lights ourselves
$(map.polygons).selectAll().move(new Vector3(CENTRE.x, CENTRE.y, CENTRE.z))
map.lights.forEach((light) => light.position.add(new Vector3(CENTRE.x, CENTRE.y, CENTRE.z)))

// --- The content (321.2) ---------------------------------------------------------------------------------------------

const at = (x, y, z) => new Vector3(CENTRE.x + x, CENTRE.y + y, CENTRE.z + z)
const yawDeg = (deg) => new Rotation(0, (deg * Math.PI) / 180, 0)

// Weapons on the west table, in rows along the wall; potions, food and the scroll on the main table in two rows.
// Items without a script of their own run their class script: the originals are placed as they are
WEAPONS.forEach((src, i) => {
	const row = Math.floor(i / WEAPON_COLUMNS)
	const col = i % WEAPON_COLUMNS
	const x = WEAPON_TABLE.x + (col - (WEAPON_COLUMNS - 1) / 2) * 45
	const z = WEAPON_TABLE.z - WEAPON_TABLE.depth / 2 + 40 + row * 75
	map.entities.push(new Entity({ id: FIRST_ID, src, position: at(x, -WEAPON_TABLE.height, z), orientation: yawDeg(90) }))
})
CONSUMABLES.forEach((src, i) => {
	const x = TABLE.x + ((i % 6) - 2.5) * 45
	const z = TABLE.z + (i < 6 ? -20 : 20)
	const item = new Entity({ id: FIRST_ID, src, position: at(x, -TABLE.height, z) })
	if (src.endsWith('scroll_generic')) {
		// A blank scroll would do nothing: this one holds the 3rd circle spell magic missile
		item.withScript().script.on('init', ['set £spellname "magic_missile"', 'set §circle 3'])
	}
	map.entities.push(item)
})

// The NPC: a goblin, the MVP enemy. The class script is the game's own; the instance script below makes it
// peaceful (the original goblin is peaceful until something angers it, and a hit would anger it: ouch and
// aggression are refused while the mode is peaceful) and takes the two orders of the levers:
//   custom "aggressive"  the goblin attacks the hero at once
//   custom "peaceful"    revive -i: the game's own reset - full life, back at its spot, its script initialised
//                        again (peaceful, not hostile), the corpse's death state undone
//   custom "respawn"     the same, then back into the mode it was in
// §aggro is the mode; the test channel reads it (CommandChannel.cpp, status.json playground.npcMode) and finds
// the entity by £pg_role. A dead goblin runs no events at all, so the respawn lever revives it from outside first.
// `reset` revives and puts the goblin back into its peaceful stance: revive does not undo a fight's behaviour, target,
// drawn weapon or death animation
const reset = ['revive -i', 'behavior none', 'settarget none', 'weapon off', 'forceanim wait'].join('\n\t\t')
const goblin = new Entity({ id: FIRST_ID, src: 'npc/goblin_base', position: at(NPC_SPOT.x, NPC_SPOT.y, NPC_SPOT.z), orientation: yawDeg(180) })
goblin.withScript()
goblin.script
	.on('init', ['set £pg_role "npc"', 'set §aggro 0', 'set §totaldead 0'])
	.on('ouch', 'if (§aggro == 0) {\n refuse\n}')
	.on('aggression', 'if (§aggro == 0) {\n refuse\n}')
	.on(
		'custom',
		`
	if (^$param1 == "aggressive") {
		set §aggro 1
		sendevent attack_player self ""
		accept
	}
	if (^$param1 == "peaceful") {
		${reset}
		accept
	}
	if (^$param1 == "respawn") {
		set §was §aggro
		${reset}
		if (§was == 1) {
			set §aggro 1
			sendevent attack_player self ""
		}
		accept
	}`,
	)
map.entities.push(goblin)

// The levers: pulled by hand like any lever of the game (#38). The mode lever hangs down for aggressive and up for
// peaceful, the respawn lever fires on every pull. They send the goblin the orders above
const leverRotation = yawDeg(180) // on the north wall, facing into the room
// `before` runs first: a dead goblin runs no events, so the respawn lever revives it from outside, then the order follows
const addLever = (id, x, role, orders, before = '') => {
	const lever = new Lever({ id, position: at(x, -LEVER_HEIGHT, LEVER_WALL.z), orientation: leverRotation })
	lever.script.on('init', `set £pg_role "${role}"`)
	lever.script.on(
		'custom',
		Object.entries(orders).map(([param, order]) => `if (^$param1 == "${param}") {\n ${before}sendevent custom ${goblin.ref} "${order}"\n}`),
	)
	map.entities.push(lever)
}
addLever(FIRST_ID, LEVER_WALL.xMode, 'lever_mode', { on: 'aggressive', off: 'peaceful' })
addLever(FIRST_ID + 1, LEVER_WALL.xRespawn, 'lever_respawn', { on: 'respawn', off: 'respawn' }, `revive -it ${goblin.ref}\n `)

// The arms lever (#385.3): every pull, up or down, counts in the global #pg_arms_pulls; the game (src/vr/VRArms.cpp)
// steps the VR arms to the next model on each new pull (custom, forearm, hands, arx, library, #385.8, #467), so the
// owner compares them in the headset
const armsLever = new Lever({ id: FIRST_ID + 2, position: at(LEVER_WALL.xArms, -LEVER_HEIGHT, LEVER_WALL.z), orientation: leverRotation })
armsLever.script.on('init', 'set £pg_role "lever_arms"')
armsLever.script.on('custom', ['on', 'off'].map((param) => `if (^$param1 == "${param}") {\n inc #pg_arms_pulls 1\n}`))
map.entities.push(armsLever)

// The hero: level 10 and every rune, set by a marker's script when the level loads (the generator's player script
// would replace the game's own player script, with all its animations)
const hero = new Marker({ id: FIRST_ID, position: at(0, 0, 0) })
hero.withScript().script.on('init', [`addxp ${MAX_LEVEL_XP}`, 'rune all'])
map.entities.push(hero)

map.player = new Player()
map.player.position = new Vector3(CENTRE.x + HERO_START.x, CENTRE.y + HERO_START.y, CENTRE.z + HERO_START.z).adjustToPlayerHeight()
map.player.orientation = new Rotation(0, (HERO_START.yaw * Math.PI) / 180, 0)

map.finalize()
await map.saveToDisk(settings)

// The game also looks for the level's loading screen and mini-map picture (graph/levels/levelN/loading, map);
// the original levels ship them. The playground gets plain dark placeholders (no game data).
const levelDir = path.join(outputDir, 'graph', 'levels', `level${LEVEL_INDEX}`)
await fs.writeFile(path.join(levelDir, 'loading.png'), solidPng(640, 480, [10, 8, 6]))
await fs.writeFile(path.join(levelDir, 'map.png'), solidPng(256, 256, [10, 8, 6]))
console.log(`[playground] level ${LEVEL_INDEX} written to ${outputDir}`)
