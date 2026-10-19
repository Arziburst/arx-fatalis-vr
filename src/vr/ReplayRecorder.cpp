/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Fatalis VR, based on Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vr/ReplayRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include <zlib.h>

#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/Version.h"
#include "game/Player.h"
#include "io/fs/FilePath.h"
#include "io/fs/FileStream.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "math/Random.h"
#include "platform/DumpHelper.h"
#include "platform/ProgramOptions.h"
#include "scene/ChangeLevel.h"
#include "vr/FingerCurl.h"
#include "vr/PerfWriter.h"
#include "vr/ReplayClip.h"
#include "vr/VRSystem.h"

namespace vr {
namespace replay {

namespace {

bool g_disabled = false;

void disable() {
	g_disabled = true;
}

void setSeed(u64 seed) {
	// After utf8_main's clock seed, on the main thread: the game draws from this sequence from here on
	Random::seed(seed);
}

//! The ring: 60 s at the fastest display the game runs on (an Index at 144 Hz)
const u32 RingSeconds = 60;
const u32 RingMaxRate = 144;

//! The replay save slots the load menu does not list (core/SaveGame.cpp): the newest five are kept
const u32 ReplaySlotFirst = 9000;
const u32 ReplaySlotCount = 5;

//! Older clips kept next to the new one: arx-replay.1.log[.gz] .. arx-replay.9.log[.gz]
const size_t ClipHistory = 10;

struct State {
	bool clockStarted = false;   //!< the clip clock runs: the first playable frame of the game loop was seen
	s64 startNs = 0;             //!< the display time the clock started at
	bool recording = false;
	bool ended = false;          //!< the session's clip was closed: no second record in this run
	u64 lastUs = 0;              //!< the last frame's time on the clip's clock
	u64 frames = 0;
	fs::path file;
	std::unique_ptr<fs::ofstream> stream;
	std::unique_ptr<PerfWriter> writer; //!< destroyed before the stream
	ReplayFrame frame;           //!< this frame, its action names made once (no allocation per frame)
	ReplayFrameText previous;
	ReplayFrameText current;
	bool havePrevious = false;
	std::string line;
	std::vector<unsigned char> ringHeap; //!< the ring's memory when the dump helper has none to share
	void * ringMemory = nullptr;
	size_t ringSize = 0;
	std::unique_ptr<ReplayRing> ring;
	ReplayHeader header;
	s32 lastArea = -1;
	bool areaPending = false;    //!< the level changed: its anchor waits for the first playable frame
	bool savePending = false;    //!< the player saved: an anchor save line is due
	std::array<Vec3f, 2> lastCurl = { };
	std::array<Vec2f, 2> lastOuter = { };
};

State g;

std::string localTime(const char * format) {
	std::time_t now = std::time(nullptr);
	std::tm * tm = std::localtime(&now);
	char buffer[48];
	if(!tm || !std::strftime(buffer, sizeof(buffer), format, tm)) {
		return "unknown";
	}
	return buffer;
}

//! arx-replay.log and arx-replay.log.gz -> .1. -> .2. ... (the two families move together: a run leaves one of them)
void rotateClips(const fs::path & dir) {
	const char * suffixes[] = { ".log", ".log.gz" };
	for(size_t i = ClipHistory - 1; i >= 1; i--) {
		for(const char * suffix : suffixes) {
			fs::path from = dir / ((i == 1 ? std::string("arx-replay") : "arx-replay." + std::to_string(i - 1)) + suffix);
			fs::path to = dir / ("arx-replay." + std::to_string(i) + suffix);
			if(!fs::is_regular_file(from)) {
				continue;
			}
			if(!fs::rename(from, to, true)) {
				LogWarning << "[replay] could not move " << from << " to " << to;
			}
		}
	}
}

//! The replay save slot to use: a free one, else the oldest (the newest ReplaySlotCount stay)
fs::path replaySaveFolder(const fs::path & saveDir, std::string & folder) {
	std::time_t oldestTime = 0;
	std::string oldest;
	for(u32 i = 0; i < ReplaySlotCount; i++) {
		char name[16];
		std::snprintf(name, sizeof(name), "save%04u", unsigned(ReplaySlotFirst + i));
		fs::path file = saveDir / name / "gsave.sav";
		if(!fs::exists(file)) {
			folder = name;
			return saveDir / name;
		}
		std::time_t time = fs::last_write_time(file);
		if(oldest.empty() || time < oldestTime) {
			oldest = name;
			oldestTime = time;
		}
	}
	folder = oldest;
	return saveDir / oldest;
}

//! The save at the record start, in a replay slot: "<folder> <name>", or "none"
std::string makeStartSave() {
	fs::path saveDir = fs::getUserDir() / "save";
	std::string folder;
	fs::path dir = replaySaveFolder(saveDir, folder);
	if(!fs::create_directories(dir)) {
		LogWarning << "[replay] could not create the replay save folder " << dir;
		return "none";
	}
	const std::string name = "VR replay " + localTime("%Y-%m-%d %H-%M-%S");
	if(!ARX_CHANGELEVEL_Save(name, dir / "gsave.sav")) {
		LogWarning << "[replay] the save at the record start failed (see the lines before this)";
		return "none";
	}
	return folder + " " + name;
}

void pushLine(std::string_view line) {
	if(!g.writer) {
		return;
	}
	// A line of the clip, or several: the writer takes one at a time
	size_t pos = 0;
	while(pos < line.size()) {
		size_t end = line.find('\n', pos);
		if(end == std::string_view::npos) {
			end = line.size();
		}
		if(end > pos) {
			g.writer->push(line.substr(pos, end - pos));
		}
		pos = end + 1;
	}
}

void setPose(SimPose & pose, const Vec3f & position, const glm::quat & orientation) {
	pose.position = position;
	pose.angles = simAngles(orientation);
}

//! How far each finger is closed, 0 - 1, from the joints: the mean of its joints' bends against the fist's
void curlsFromJoints(const HandJoints & joints, Vec3f & curl, Vec2f & outer) {
	const FingerPoseLimits limits;
	const FingerPose pose = fingerPoseFromJoints(joints, limits);
	float closed[FingerCount];
	for(size_t finger = 0; finger < FingerCount; finger++) {
		float sum = 0.f;
		for(size_t joint = 0; joint < 3; joint++) {
			const float limit = limits.flex[finger][joint];
			sum += limit > 0.f ? std::clamp(pose.flex[finger][joint] / limit, 0.f, 1.f) : 0.f;
		}
		closed[finger] = sum / 3.f;
	}
	curl = Vec3f(closed[0], closed[1], closed[2]);
	outer = Vec2f(closed[3], closed[4]);
}

void start() {

	const FrameViews & views = frameViews();
	fs::path dir = fs::getUserDir();
	rotateClips(dir);

	g.file = dir / "arx-replay.log";
	g.stream = std::make_unique<fs::ofstream>(g.file, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
	if(!g.stream->is_open()) {
		LogWarning << "[replay] could not open " << g.file << " - this session is not recorded";
		g.stream.reset();
		g.ended = true;
		return;
	}

	// The save first: the hitch is before the first frame, and the header names it
	const std::string save = makeStartSave();

	const s64 period = displayPeriodUs();
	g.header = ReplayHeader();
	g.header.build = arx_name + " " + arx_version;
	g.header.save = save;
	g.header.seed = Random::currentSeed();
	// The period is whole microseconds (90 Hz: 11111): the rate to 0.1 Hz, the display's own number
	g.header.rate = period > 0 ? float(std::round(1e7 / double(period)) / 10.0) : 0.f;
	g.header.ipd = glm::length(views.eyes[1].position - views.eyes[0].position);
	g.header.floor = views.floorValid ? views.localAboveFloor : 0.f;

	fs::ofstream * stream = g.stream.get();
	g.writer = std::make_unique<PerfWriter>([stream](std::string_view batch) {
		stream->write(batch.data(), std::streamsize(batch.size()));
		stream->flush();
	});
	pushLine(formatReplayHeader(g.header));

	// The ring: in the dump helper's shared memory when it has the room, else on the heap
	size_t size = 0;
	void * memory = platform::dumpHelperRegion(size);
	const u32 rate = u32(std::ceil(std::max(g.header.rate, 1.f)));
	const size_t wanted = ReplayRing::memorySize(RingSeconds, rate);
	if(!memory || size < wanted) {
		g.ringHeap.assign(wanted + 16, 0);
		uintptr_t address = reinterpret_cast<uintptr_t>(g.ringHeap.data());
		memory = g.ringHeap.data() + ((16 - address % 16) % 16);
		size = wanted;
	}
	g.ringMemory = memory;
	g.ringSize = size;
	g.ring = std::make_unique<ReplayRing>(memory, size, RingSeconds, rate);
	g.ring->setHeader(g.header);

	// The frame's action names, once
	g.frame = ReplayFrame();
	for(size_t i = 0; i < RawActionValues::Count; i++) {
		g.frame.rig.actions[RawActionValues::name(i)] = { Vec2f(0.f), Vec2f(0.f) };
	}

	g.lastUs = 0;
	g.frames = 0;
	g.havePrevious = false;
	g.lastArea = g_currentArea ? s32(u32(g_currentArea)) : -1;
	g.areaPending = false;
	g.savePending = false;
	g.recording = true;

	std::string folder = "none", name;
	if(save != "none") {
		size_t space = save.find(' ');
		folder = save.substr(0, space);
		name = space == std::string::npos ? std::string() : save.substr(space + 1);
	}
	LogInfo << "[replay] recording arx-replay.log: save " << folder << " \"" << name << "\", seed " << g.header.seed
	        << ", " << g.header.rate << " Hz";
}

//! The plain file into a gzip one, streamed; false (the plain file kept) when anything fails
bool gzipFile(const fs::path & from, const fs::path & to) {

	std::ifstream in(from.string(), std::ios_base::in | std::ios_base::binary);
	fs::ofstream out(to, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
	if(!in.is_open() || !out.is_open()) {
		return false;
	}

	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	if(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
		return false;
	}

	std::vector<char> input(256 * 1024), output(256 * 1024);
	bool ok = true;
	int flush = Z_NO_FLUSH;
	do {
		in.read(input.data(), std::streamsize(input.size()));
		const std::streamsize read = in.gcount();
		flush = in.eof() ? Z_FINISH : Z_NO_FLUSH;
		stream.next_in = reinterpret_cast<Bytef *>(input.data());
		stream.avail_in = uInt(read);
		do {
			stream.next_out = reinterpret_cast<Bytef *>(output.data());
			stream.avail_out = uInt(output.size());
			const int result = deflate(&stream, flush);
			if(result == Z_STREAM_ERROR) {
				ok = false;
				break;
			}
			const size_t have = output.size() - stream.avail_out;
			out.write(output.data(), std::streamsize(have));
			if(!out) {
				ok = false;
				break;
			}
		} while(stream.avail_out == 0);
	} while(ok && flush != Z_FINISH);

	deflateEnd(&stream);
	out.close();
	return ok && out.good();
}

void finish(const char * why) {

	if(!g.recording) {
		return;
	}
	g.recording = false;
	g.ended = true;

	const double seconds = double(g.lastUs) / 1e6;
	pushLine("# end: " + std::to_string(g.frames) + " frames, " + std::to_string(seconds) + " s (" + why + ")");
	g.writer->flush();
	const size_t dropped = g.writer->dropped();
	g.writer.reset();
	g.stream.reset();
	if(dropped) {
		LogWarning << "[replay] " << dropped << " clip lines dropped: the disk fell behind";
	}

	fs::path gz = g.file.parent() / "arx-replay.log.gz";
	if(gzipFile(g.file, gz)) {
		fs::remove(g.file);
		LogInfo << "[replay] " << gz << ": " << g.frames << " frames, " << seconds << " s (" << why << ")";
	} else {
		fs::remove(gz);
		LogWarning << "[replay] could not compress " << g.file << " - the plain file is kept";
	}
}

bool validMarkName(std::string_view name) {
	if(name.empty() || name.size() > 64) {
		return false;
	}
	for(char c : name) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		if(!ok) {
			return false;
		}
	}
	return true;
}

std::string mark(std::string_view name, u32 seconds) {

	if(!g.recording || !g.ring) {
		return "replay: nothing records (a VR session without --no-replay-log, once the game is playable)";
	}

	const u64 markUs = g.lastUs;
	const std::string text = replayRingClip(g.ringMemory, g.ringSize, name, markUs, u64(seconds) * 1000000);
	if(text.empty()) {
		return "replay: the ring holds no clip";
	}

	fs::path dir = fs::getUserDir() / "replay-marks";
	if(!fs::create_directories(dir)) {
		return "replay: could not create " + dir.string();
	}
	const std::string file = localTime("%Y-%m-%d-%H-%M-%S") + "-" + std::string(name) + ".vrsim";
	fs::ofstream out(dir / file, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
	if(!out.is_open()) {
		return "replay: could not write " + (dir / file).string();
	}
	out.write(text.data(), std::streamsize(text.size()));
	out.close();

	// How much the clip spans: its frame lines
	u64 frames = 0;
	s64 firstMs = -1, lastMs = -1;
	for(size_t pos = 0; pos < text.size(); ) {
		size_t end = text.find('\n', pos);
		if(end == std::string::npos) {
			end = text.size();
		}
		if(end > pos && text[pos] == '@') {
			frames++;
			const s64 ms = std::atoll(text.c_str() + pos + 1);
			if(firstMs < 0) {
				firstMs = ms;
			}
			lastMs = ms;
		}
		pos = end + 1;
	}
	const double span = frames ? double(lastMs - firstMs) / 1000.0 : 0.0;
	char spanText[32];
	std::snprintf(spanText, sizeof(spanText), "%.1f", span);
	LogInfo << "[replay] mark " << name << ": replay-marks\\" << file << " (" << frames << " frames, " << spanText << " s)";
	return std::string();
}

} // anonymous namespace

ARX_PROGRAM_OPTION("no-replay-log", nullptr,
                   "Do not record the VR session to arx-replay.log (#550, the replay of a headset session)",
                   &disable)

ARX_PROGRAM_OPTION_ARG("seed", nullptr,
                       "Seed the main thread's random numbers with N (a replay draws the recorded session's, #550)",
                       &setSeed, "N")

size_t ringBytes() {
	return ReplayRing::memorySize(RingSeconds, RingMaxRate);
}

bool recording() {
	return g.recording;
}

void noteSave() {
	if(g.recording) {
		g.savePending = true;
	}
}

s64 clipClockUs(s64 displayTimeNs) {
	if(!g.clockStarted) {
		if(!gameFrame() || !isEnabled() || gameevent::detectGameState() != gameevent::GameState::Gameplay) {
			return -1;
		}
		g.clockStarted = true;
		g.startNs = displayTimeNs;
	}
	return displayTimeNs >= g.startNs ? (displayTimeNs - g.startNs) / 1000 : -1;
}

void frame() {

	const FrameViews & views = frameViews();
	if(!isEnabled() || !views.valid) {
		if(g.recording) {
			finish("the VR session ended");
		}
		return;
	}

	// The clock starts at the first playable frame of the game loop (the simulated runtime may have started it at
	// this frame's xrWaitFrame already, on the same display time)
	const s64 clockUs = clipClockUs(views.displayTime);
	if(clockUs < 0 || g_disabled || g.ended) {
		return;
	}
	const bool playable = gameevent::detectGameState() == gameevent::GameState::Gameplay;
	if(!g.recording) {
		if(!playable) {
			return;
		}
		start();
		if(!g.recording) {
			return;
		}
	}

	ReplayFrame & frame = g.frame;
	frame.timeUs = u64(clockUs);
	SimRig & rig = frame.rig;

	// The head: between the eyes, turned as they are
	setPose(rig.head, (views.eyes[0].position + views.eyes[1].position) * 0.5f,
	        glm::normalize(glm::slerp(views.eyes[0].orientation, views.eyes[1].orientation, 0.5f)));

	const FrameHands & hands = frameHands();
	const std::array<AimSample, HandCount> aims = frameAims();
	for(size_t hand = 0; hand < HandCount; hand++) {
		rig.handTracked[hand] = hands.tracked[hand];
		setPose(rig.hands[hand], hands.hands[hand].position, hands.hands[hand].orientation);
		// The aim in the grip's space: a fixed offset per controller, so it seldom changes
		if(aims[hand].valid && hands.tracked[hand]) {
			Vec3f position = aims[hand].position;
			glm::quat orientation = aims[hand].orientation;
			poseInSpace(hands.hands[hand].position, hands.hands[hand].orientation, position, orientation);
			setPose(rig.aims[hand], position, orientation);
		}
		rig.fingers[hand].joints = hands.joints[hand].valid;
		if(hands.joints[hand].valid) {
			curlsFromJoints(hands.joints[hand], g.lastCurl[hand], g.lastOuter[hand]);
		}
		rig.fingers[hand].curl = g.lastCurl[hand];
		rig.fingers[hand].outer = g.lastOuter[hand];
	}

	const std::array<std::string, HandCount> profiles = controllerProfiles();
	const std::string & profile = !profiles[1].empty() ? profiles[1] : profiles[0];
	if(rig.profile != profile) {
		rig.profile = profile;
	}
	rig.worn = isWorn();
	rig.focused = std::string_view(sessionStateString()) == "focused";

	const RawActionValues & raw = rawControllerValues();
	for(auto & entry : rig.actions) {
		// The map is sorted by name, the raw values by index: nine names, no allocation
		for(size_t k = 0; k < RawActionValues::Count; k++) {
			if(entry.first == RawActionValues::name(k)) {
				entry.second = raw.values[k];
				break;
			}
		}
	}

	rig.player.position = player.pos;
	rig.player.yaw = player.angle.getYaw();
	rig.player.area = g_currentArea ? s32(u32(g_currentArea)) : -1;

	// The anchors: a level change once the new level is playable, a save the player made
	if(rig.player.area != g.lastArea) {
		g.lastArea = rig.player.area;
		g.areaPending = rig.player.area > 0;
	}
	if(g.areaPending && playable) {
		g.areaPending = false;
		pushLine("@" + std::to_string((frame.timeUs + 500) / 1000) + "ms anchor level " + std::to_string(rig.player.area));
	}
	if(g.savePending && playable) {
		g.savePending = false;
		pushLine("@" + std::to_string((frame.timeUs + 500) / 1000) + "ms anchor save");
	}

	g.line = formatReplayFrame(frame, g.havePrevious ? &g.previous : nullptr, g.current);
	std::swap(g.previous, g.current);
	g.havePrevious = true;
	if(!g.line.empty() && g.line.back() == '\n') {
		g.line.pop_back();
	}
	g.writer->push(g.line);
	g.ring->push(frame);
	g.lastUs = frame.timeUs;
	g.frames++;
}

void shutdown() {
	finish("the game quit");
	g.ring.reset();
	g.ringHeap.clear();
}

std::string command(std::string_view rest) {

	// mark <name> [<seconds>]
	std::istringstream iss{std::string(rest)};
	std::string what, name;
	iss >> what >> name;
	if(what != "mark") {
		return "replay: expected mark <name> [<seconds>], got \"" + std::string(rest) + "\"";
	}
	if(!validMarkName(name)) {
		return "replay: a mark's name is letters, digits, - and _ (got \"" + name + "\")";
	}
	long seconds = 30;
	if(iss >> seconds) {
		if(seconds < 1 || seconds > 60) {
			return "replay: the window is 1-60 seconds";
		}
	} else if(!iss.eof()) {
		return "replay: expected the window in seconds after the name";
	}
	return mark(name, u32(seconds));
}

} // namespace replay
} // namespace vr
