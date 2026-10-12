// Generates the Arx Fatalis VR test playground level (#321) with arx-level-generator.
//
//   node build.mjs [outputDir]
//
// outputDir defaults to %LOCALAPPDATA%\ArxFatalisVR\playground\level: a data dir for the game
// (--data-dir), holding graph/levels/level30/*.dlf, *.llf and game/graph/levels/level30/fast.fts.
// The textures are the original game's own (referenced by name, never copied), so the generated
// folder holds no game data and is built on the player's own machine. The layout is in layout.mjs.
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import zlib from 'node:zlib'
import { fileURLToPath } from 'node:url'
import { $, ArxMap, Color, Material, Player, Rotation, Settings, Texture, Vector3 } from 'arx-level-generator'
import { createLight } from 'arx-level-generator/tools'
import { createBox as createBoxMesh } from 'arx-level-generator/prefabs/mesh'
import { createRoom } from 'arx-level-generator/prefabs/rooms'
import { CENTRE, HERO_START, LEVEL_INDEX, LIGHTS, ROOM, TABLE } from './layout.mjs'

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
const table = ArxMap.fromThreeJsMesh(
	createBoxMesh({
		position: new Vector3(TABLE.x, -(TABLE.height - TABLE.top / 2), TABLE.z),
		size: new Vector3(TABLE.width, TABLE.top, TABLE.depth),
		materials: wood,
	}),
	{},
)
map.add(table)
for (const dx of [-1, 1]) {
	for (const dz of [-1, 1]) {
		map.add(
			ArxMap.fromThreeJsMesh(
				createBoxMesh({
					position: new Vector3(TABLE.x + dx * (TABLE.width / 2 - 12), -(TABLE.height - TABLE.top) / 2, TABLE.z + dz * (TABLE.depth / 2 - 12)),
					size: new Vector3(14, TABLE.height - TABLE.top, 14),
					materials: wood,
				}),
				{},
			),
		)
	}
}

for (const l of LIGHTS) {
	map.lights.push(createLight({ position: new Vector3(l.x, -l.y, l.z), radius: l.radius, color: Color.fromCSS('#ffe2b0'), intensity: 1.3 }))
}

// ArxMap.move() fails on lights (arx-level-generator 20.1.0 Light has no move()): move the polygons and the lights ourselves
$(map.polygons).selectAll().move(new Vector3(CENTRE.x, CENTRE.y, CENTRE.z))
map.lights.forEach((light) => light.position.add(new Vector3(CENTRE.x, CENTRE.y, CENTRE.z)))

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
