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

#include "vr/ReplayRingTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "platform/Platform.h"
#include "vr/ReplayClip.h"
#include "vr/SimRig.h"

CppUnit::AutoRegisterSuite<ReplayRingTest> g_registerReplayRingTest;

namespace ARX_ANONYMOUS_NAMESPACE {

const u64 RingFrameUs = 11111; // 90 Hz

//! Frame k of a session: everything moves; the right A is held for 7 s every 14 s, the profile changes once
vr::ReplayFrame ringMakeFrame(size_t k) {
	vr::ReplayFrame f;
	f.timeUs = u64(k) * RingFrameUs;
	const float t = float(k) * 0.011111f;
	f.rig.head.position = Vec3f(0.04f * std::sin(t), 0.01f * std::cos(t), -0.02f * std::sin(0.5f * t));
	f.rig.head.angles = Vec3f(30.f * std::sin(0.2f * t), 10.f * std::cos(0.3f * t), 2.f * std::sin(t));
	for(size_t hand = 0; hand < 2; hand++) {
		const float side = hand == 0 ? -1.f : 1.f;
		f.rig.hands[hand].position = Vec3f(side * 0.2f + 0.05f * std::sin(t), -0.4f + 0.05f * std::cos(t), -0.3f);
		f.rig.hands[hand].angles = Vec3f(side * 10.f * std::sin(t), -20.f, 5.f * std::cos(t));
		f.rig.aims[hand].position = Vec3f(0.f, -0.01f, -0.05f);
		f.rig.aims[hand].angles = Vec3f(0.f, -40.f, 0.f);
		f.rig.fingers[hand].curl = Vec3f(0.5f + 0.4f * std::sin(t), 0.5f, 0.2f);
		f.rig.fingers[hand].outer = Vec2f(0.3f, 0.4f + 0.3f * std::cos(t));
	}
	f.rig.profile = k < 1000 ? "/interaction_profiles/valve/index_controller" : "/interaction_profiles/oculus/touch_controller";
	f.rig.actions["trigger"] = { Vec2f(0.f), Vec2f(0.f) };
	f.rig.actions["button_a"] = { Vec2f(0.f), Vec2f(((k / 630) % 2) ? 1.f : 0.f, 0.f) };
	f.rig.actions["thumbstick"] = { Vec2f(0.f, 0.9f), Vec2f(0.f) };
	f.rig.player.position = Vec3f(500.f + float(k) * 0.5f, 100.f, 800.f);
	f.rig.player.yaw = std::fmod(float(k) * 0.1f, 360.f);
	f.rig.player.area = 10;
	return f;
}

vr::ReplayHeader ringHeader() {
	vr::ReplayHeader header;
	header.build = "Arx Fatalis VR 1.3.0-dev";
	header.save = "save0007 replay 2026-10-20 15-00-00";
	header.seed = 77;
	header.rate = 90.f;
	header.ipd = 0.064f;
	header.floor = 1.7f;
	return header;
}

//! A ring in 16-byte aligned memory, with frames [0, count) pushed
struct RingMemory {
	std::vector<u64> words;
	size_t size = 0;
	explicit RingMemory(size_t count, u32 seconds = 60, u32 rate = 90) {
		size = vr::ReplayRing::memorySize(seconds, rate);
		words.assign(size / sizeof(u64) + 2, 0);
		vr::ReplayRing ring(data(), size, seconds, rate);
		ring.setHeader(ringHeader());
		for(size_t k = 0; k < count; k++) {
			ring.push(ringMakeFrame(k));
		}
	}
	void * data() {
		// std::vector<u64> gives 8; the ring wants 16
		u8 * bytes = reinterpret_cast<u8 *>(words.data());
		return bytes + (reinterpret_cast<uintptr_t>(bytes) % 16 == 0 ? 0 : 8);
	}
};

std::vector<std::string> ringLines(const std::string & text) {
	std::vector<std::string> lines;
	size_t pos = 0;
	while(pos < text.size()) {
		size_t end = text.find('\n', pos);
		if(end == std::string::npos) {
			end = text.size();
		}
		lines.push_back(text.substr(pos, end - pos));
		pos = end + 1;
	}
	return lines;
}

s64 ringLineMs(const std::string & line) {
	if(line.size() < 4 || line[0] != '@') {
		return -1;
	}
	size_t pos = 1;
	s64 value = 0;
	while(pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
		value = value * 10 + (line[pos] - '0');
		pos++;
	}
	return (pos > 1 && line.compare(pos, 3, "ms ") == 0) ? value : -1;
}

//! The frame times (ms) of a clip's frame lines; anything else that starts with '@' fails
std::vector<s64> ringFrameTimes(const std::string & clip) {
	std::vector<s64> times;
	for(const std::string & line : ringLines(clip)) {
		if(!line.empty() && line[0] == '@') {
			const s64 ms = ringLineMs(line);
			CPPUNIT_ASSERT_MESSAGE(line, ms >= 0);
			CPPUNIT_ASSERT_MESSAGE(line, times.empty() || ms > times.back());
			times.push_back(ms);
		}
	}
	return times;
}

//! The frame whose line has this time
size_t ringFrameAt(s64 ms) {
	size_t k = size_t(u64(ms) * 1000 / RingFrameUs);
	while(k > 0 && s64((u64(k) * RingFrameUs + 500) / 1000) > ms) {
		k--;
	}
	while(s64((u64(k) * RingFrameUs + 500) / 1000) < ms) {
		k++;
	}
	return k;
}

bool ringHasLine(const std::string & text, const std::string & wanted) {
	for(const std::string & line : ringLines(text)) {
		if(line == wanted) {
			return true;
		}
	}
	return false;
}

//! A clip on its own: its first and last frame as the original frames, and a valid script
void ringAssertSelfContained(const std::string & clip, size_t firstK, size_t lastK) {

	std::vector<std::string> lines = ringLines(clip);
	size_t next = 0;
	vr::ClipPlayer player([&](std::string & line) {
		if(next >= lines.size()) {
			return false;
		}
		line = lines[next++];
		return true;
	});
	vr::Simulation sim;
	vr::ReplayGame game;
	player.advance(0, game, sim);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	vr::ReplayFrame first;
	first.timeUs = player.frameTimeUs();
	first.rig = sim.rig;
	// The first frame carries the whole state, the A held since before the clip and the profile included
	CPPUNIT_ASSERT_EQUAL(vr::formatReplayFrame(ringMakeFrame(firstK)), vr::formatReplayFrame(first));

	const u64 span = ringMakeFrame(lastK).timeUs - ringMakeFrame(firstK).timeUs;
	player.advance(span + 2000, game, sim);
	vr::ReplayFrame last;
	last.timeUs = player.frameTimeUs();
	last.rig = sim.rig;
	CPPUNIT_ASSERT_EQUAL(vr::formatReplayFrame(ringMakeFrame(lastK)), vr::formatReplayFrame(last));

	vr::Simulation script;
	CPPUNIT_ASSERT_EQUAL(std::string(), script.load(clip));
}

} ARX_END_ANONYMOUS_NAMESPACE

void ReplayRingTest::testKeepsTheLastSixtySeconds() {

	// 90 s pushed into a 60 s ring
	const size_t count = 90 * 90;
	RingMemory memory(count);
	const std::string clip = vr::replayRingClip(memory.data(), memory.size);

	const std::vector<std::string> lines = ringLines(clip);
	CPPUNIT_ASSERT_MESSAGE(clip.substr(0, 400), !lines.empty() && lines[0].rfind("# arx-replay", 0) == 0);
	CPPUNIT_ASSERT(ringHasLine(clip, "# save: save0007 replay 2026-10-20 15-00-00"));
	CPPUNIT_ASSERT(ringHasLine(clip, "# seed: 77"));
	CPPUNIT_ASSERT(ringHasLine(clip, "# build: Arx Fatalis VR 1.3.0-dev"));

	const std::vector<s64> times = ringFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	const s64 newest = s64((u64(count - 1) * RingFrameUs + 500) / 1000);
	CPPUNIT_ASSERT_EQUAL(newest, times.back());
	const std::string span = std::to_string(times.front()) + " - " + std::to_string(times.back()) + " ms";
	// At least the last 60 s (to a frame), and not the whole 90 s: the oldest went
	CPPUNIT_ASSERT_MESSAGE(span, times.front() <= newest - 59900);
	CPPUNIT_ASSERT_MESSAGE(span, times.front() >= newest - 70000);
	// Every frame in between, none lost
	const size_t firstK = ringFrameAt(times.front());
	CPPUNIT_ASSERT_EQUAL(count - firstK, times.size());

	ringAssertSelfContained(clip, firstK, count - 1);
}

void ReplayRingTest::testMarkCutsTheLastThirtySeconds() {

	const size_t count = 65 * 90;
	RingMemory memory(count);
	const u64 markUs = ringMakeFrame(count - 1).timeUs;
	const s64 markMs = s64((markUs + 500) / 1000);
	const std::string clip = vr::replayRingClip(memory.data(), memory.size, "chest-did-not-open", markUs, 30000000);

	CPPUNIT_ASSERT_MESSAGE(clip.substr(0, 400), ringHasLine(clip, "# mark: chest-did-not-open " + std::to_string(markMs)));
	CPPUNIT_ASSERT(ringHasLine(clip, "# seed: 77"));
	const std::vector<s64> times = ringFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	const std::string span = std::to_string(times.front()) + " - " + std::to_string(times.back()) + " ms, mark " + std::to_string(markMs);
	// The first frame >= 29.5 s before the mark (and not much more than 30 s), the last within 100 ms of it
	CPPUNIT_ASSERT_MESSAGE(span, times.front() <= markMs - 29500);
	CPPUNIT_ASSERT_MESSAGE(span, times.front() >= markMs - 31000);
	CPPUNIT_ASSERT_MESSAGE(span, times.back() <= markMs);
	CPPUNIT_ASSERT_MESSAGE(span, times.back() >= markMs - 100);

	ringAssertSelfContained(clip, ringFrameAt(times.front()), ringFrameAt(times.back()));
}

void ReplayRingTest::testMarkBeforeTheNewestFrame() {

	// The mark names a moment the ring has passed already: the clip ends there, not at the newest frame
	const size_t count = 70 * 90;
	RingMemory memory(count);
	const u64 markUs = 50000000;
	const std::string clip = vr::replayRingClip(memory.data(), memory.size, "earlier", markUs, 30000000);
	const std::vector<s64> times = ringFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	const std::string span = std::to_string(times.front()) + " - " + std::to_string(times.back()) + " ms";
	CPPUNIT_ASSERT_MESSAGE(span, times.back() <= 50000);
	CPPUNIT_ASSERT_MESSAGE(span, times.back() >= 49900);
	CPPUNIT_ASSERT_MESSAGE(span, times.front() <= 20500);
	CPPUNIT_ASSERT_MESSAGE(span, times.front() >= 19000);
}

void ReplayRingTest::testWindowIsConfigurable() {

	const size_t count = 40 * 90;
	RingMemory memory(count);
	const u64 markUs = ringMakeFrame(count - 1).timeUs;
	const s64 markMs = s64((markUs + 500) / 1000);
	const std::string clip = vr::replayRingClip(memory.data(), memory.size, "short", markUs, 10000000);
	const std::vector<s64> times = ringFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	const std::string span = std::to_string(times.front()) + " - " + std::to_string(times.back()) + " ms";
	CPPUNIT_ASSERT_MESSAGE(span, times.front() <= markMs - 9500);
	CPPUNIT_ASSERT_MESSAGE(span, times.front() >= markMs - 11000);
	CPPUNIT_ASSERT_MESSAGE(span, times.back() >= markMs - 100);
	ringAssertSelfContained(clip, ringFrameAt(times.front()), ringFrameAt(times.back()));
}

void ReplayRingTest::testWindowLongerThanTheRing() {

	// 20 s recorded, 30 s asked: the clip starts at the first frame
	const size_t count = 20 * 90;
	RingMemory memory(count);
	const u64 markUs = ringMakeFrame(count - 1).timeUs;
	const std::string clip = vr::replayRingClip(memory.data(), memory.size, "early", markUs, 30000000);
	const std::vector<s64> times = ringFrameTimes(clip);
	CPPUNIT_ASSERT_EQUAL(count, times.size());
	CPPUNIT_ASSERT_EQUAL(s64(0), times.front());
	ringAssertSelfContained(clip, 0, count - 1);
}

void ReplayRingTest::testCopyOfTheImage() {

	// What the dump helper has: the bytes, read at another address
	const size_t count = 75 * 90;
	RingMemory memory(count);
	std::vector<u64> copy(memory.size / sizeof(u64) + 4, 0);
	u8 * at = reinterpret_cast<u8 *>(copy.data()) + 8;
	std::memcpy(at, memory.data(), memory.size);

	const std::string original = vr::replayRingClip(memory.data(), memory.size);
	CPPUNIT_ASSERT(!ringFrameTimes(original).empty());
	CPPUNIT_ASSERT_EQUAL(original, vr::replayRingClip(at, memory.size));
	const u64 markUs = ringMakeFrame(count - 1).timeUs;
	CPPUNIT_ASSERT_EQUAL(vr::replayRingClip(memory.data(), memory.size, "copy", markUs, 30000000),
	                     vr::replayRingClip(at, memory.size, "copy", markUs, 30000000));
}

void ReplayRingTest::testBrokenImages() {

	const size_t count = 10 * 90;
	RingMemory memory(count);

	// Zeros and garbage: no clip
	std::vector<u64> zeros(memory.size / sizeof(u64) + 2, 0);
	CPPUNIT_ASSERT(ringFrameTimes(vr::replayRingClip(zeros.data(), memory.size)).empty());
	std::vector<u64> garbage(memory.size / sizeof(u64) + 2, 0);
	std::mt19937_64 random(550);
	for(u64 & word : garbage) {
		word = random();
	}
	CPPUNIT_ASSERT(ringFrameTimes(vr::replayRingClip(garbage.data(), memory.size)).empty());
	CPPUNIT_ASSERT(ringFrameTimes(vr::replayRingClip(garbage.data(), memory.size, "x", 1000000, 30000000)).empty());

	// The ring's own header with garbage behind it, and copies cut short: fewer frames or none, every line whole
	std::vector<u64> mixed(memory.size / sizeof(u64) + 2, 0);
	std::memcpy(mixed.data(), memory.data(), std::min<size_t>(256, memory.size));
	for(size_t i = 256 / sizeof(u64); i < mixed.size(); i++) {
		mixed[i] = random();
	}
	(void)ringFrameTimes(vr::replayRingClip(mixed.data(), memory.size));
	for(size_t size : { size_t(0), size_t(1), size_t(16), size_t(255), memory.size / 2, memory.size - 1 }) {
		std::vector<u64> cut(size / sizeof(u64) + 2, 0);
		std::memcpy(cut.data(), memory.data(), size);
		const std::string clip = vr::replayRingClip(cut.data(), size);
		const std::vector<s64> times = ringFrameTimes(clip);
		CPPUNIT_ASSERT(times.size() <= count);
	}
}

void ReplayRingTest::testEmptyRing() {
	RingMemory memory(0);
	CPPUNIT_ASSERT(ringFrameTimes(vr::replayRingClip(memory.data(), memory.size)).empty());
	CPPUNIT_ASSERT(ringFrameTimes(vr::replayRingClip(memory.data(), memory.size, "none", 1000000, 30000000)).empty());
	// A faster display needs more room for the same 60 s
	CPPUNIT_ASSERT(vr::ReplayRing::memorySize(60, 144) > vr::ReplayRing::memorySize(60, 90));
}
