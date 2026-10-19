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

#include "vr/ReplayClipTest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "platform/Platform.h"
#include "vr/ReplayClip.h"
#include "vr/SimRig.h"

CppUnit::AutoRegisterSuite<ReplayClipTest> g_registerReplayClipTest;

namespace ARX_ANONYMOUS_NAMESPACE {

// The contract's tolerances (#550): 1 mm, 0.1 degree, curls 0.01
const double ClipMeters = 0.001;
const double ClipDegrees = 0.1;
const double ClipCurl = 0.01;

const char * const ClipActions[] = {
	"trigger", "grip", "button_a", "button_b", "thumbstick", "thumbstick_click", "trackpad_press", "menu", "thumb_touch",
};

//! A frame of a moving session: every pose moves, and now and then something discrete changes
vr::ReplayFrame clipMakeFrame(size_t k) {

	vr::ReplayFrame f;
	f.timeUs = u64(k) * 11111; // 90 Hz
	const float t = float(k) * 0.011111f;

	f.rig.head.position = Vec3f(0.05f * std::sin(t), -0.02f + 0.01f * std::cos(1.3f * t), -0.03f * std::sin(0.7f * t));
	f.rig.head.angles = Vec3f(40.f * std::sin(0.5f * t), 15.f * std::sin(0.9f * t), 5.f * std::sin(1.7f * t));
	for(size_t hand = 0; hand < 2; hand++) {
		const float side = hand == 0 ? -1.f : 1.f;
		f.rig.hands[hand].position = Vec3f(side * (0.2f + 0.05f * std::sin(t + float(hand))), -0.45f + 0.03f * std::cos(t),
		                                   -0.35f + 0.04f * std::sin(2.f * t));
		f.rig.hands[hand].angles = Vec3f(side * 20.f * std::sin(t), -30.f + 10.f * std::cos(t), 15.f * std::sin(0.5f * t));
		// The aim in the grip's space: the controller's pointing offset, changed once (another profile)
		f.rig.aims[hand].position = Vec3f(0.f, -0.01f, k >= 150 ? -0.06f : -0.05f);
		f.rig.aims[hand].angles = Vec3f(0.f, k >= 150 ? -35.f : -40.f, 0.f);
		f.rig.fingers[hand].curl = Vec3f(0.5f + 0.4f * std::sin(t + float(hand)), 0.5f + 0.45f * std::cos(1.1f * t),
		                                 0.3f + 0.2f * std::sin(0.3f * t));
		f.rig.fingers[hand].outer = Vec2f(0.6f + 0.3f * std::cos(0.8f * t), 0.7f + 0.25f * std::sin(0.6f * t));
	}
	// The right hand loses tracking for a few frames, the right hand's fingers are not tracked for a few others
	f.rig.handTracked[1] = !(k >= 170 && k < 176);
	f.rig.fingers[1].joints = !(k >= 180 && k < 185);
	// No interaction profile for a while (the controllers off)
	f.rig.profile = (k >= 150 && k < 160) ? std::string() : std::string("/interaction_profiles/valve/index_controller");
	// The session: visible (the dashboard up) and idle (the headset on the desk) for a while
	f.rig.focused = !(k >= 100 && k < 110);
	f.rig.worn = !(k >= 120 && k < 130);

	for(const char * name : ClipActions) {
		f.rig.actions[name] = { Vec2f(0.f), Vec2f(0.f) };
	}
	f.rig.actions["trigger"][0].x = std::clamp(0.5f + 0.5f * std::sin(2.f * t), 0.f, 1.f);
	f.rig.actions["grip"][1].x = std::clamp(0.5f + 0.5f * std::cos(1.5f * t), 0.f, 1.f);
	f.rig.actions["button_a"][1].x = ((k / 50) % 2) ? 1.f : 0.f;
	// A 2D action whose y falls to 0 while its x stays: both are always written
	f.rig.actions["thumbstick"][0] = Vec2f(0.3f * std::sin(t), (k % 40 < 20) ? 0.8f : 0.f);
	f.rig.actions["thumb_touch"][0].x = 1.f;

	f.rig.player.position = Vec3f(1000.f + float(k) * 1.5f, 120.f, 2000.f - float(k) * 0.75f);
	f.rig.player.yaw = std::fmod(270.f + float(k) * 0.37f, 360.f);
	f.rig.player.area = k >= 200 ? 11 : 10;

	return f;
}

vr::ReplayHeader clipMakeHeader() {
	vr::ReplayHeader header;
	header.build = "Arx Fatalis VR 1.3.0-dev";
	header.save = "save0003 replay 2026-10-20 14-03-12";
	header.seed = 4242;
	header.rate = 90.f;
	header.ipd = 0.0617f;
	header.floor = 1.8325f;
	return header;
}

std::vector<std::string> clipSplitLines(const std::string & text) {
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

//! The ms of an "@<ms>ms ..." line, or -1
s64 clipLineMs(const std::string & line) {
	if(line.size() < 4 || line[0] != '@') {
		return -1;
	}
	size_t pos = 1;
	s64 value = 0;
	while(pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
		value = value * 10 + (line[pos] - '0');
		pos++;
	}
	if(pos == 1 || line.compare(pos, 3, "ms ") != 0) {
		return -1;
	}
	return value;
}

//! A clip of the frames [0, count): a full first frame, then what changed
std::string clipMakeStream(size_t count, const vr::ReplayHeader & header = clipMakeHeader()) {
	std::string text = vr::formatReplayHeader(header);
	vr::ReplayFrame previous;
	for(size_t k = 0; k < count; k++) {
		vr::ReplayFrame frame = clipMakeFrame(k);
		text += vr::formatReplayFrame(frame, k == 0 ? nullptr : &previous);
		previous = frame;
	}
	return text;
}

//! A line source over lines in memory that counts the lines read
struct ClipSource {
	std::vector<std::string> lines;
	size_t next = 0;
	vr::ClipPlayer::LineSource source() {
		return [this](std::string & line) {
			if(next >= lines.size()) {
				return false;
			}
			line = lines[next++];
			return true;
		};
	}
};

//! What the player made of the clip: the frame the rig now shows
vr::ReplayFrame clipCapture(const vr::ClipPlayer & player, const vr::Simulation & sim) {
	vr::ReplayFrame frame;
	frame.timeUs = player.frameTimeUs();
	frame.rig = sim.rig;
	return frame;
}

vr::ReplayGame clipGame(s32 area = 10, bool playable = true) {
	vr::ReplayGame game;
	game.playable = playable;
	game.player.area = area;
	return game;
}

bool clipStartsWith(const std::string & text, const std::string & prefix) {
	return text.rfind(prefix, 0) == 0;
}

bool clipHasLine(const std::string & text, const std::string & line) {
	for(const std::string & candidate : clipSplitLines(text)) {
		if(candidate == line) {
			return true;
		}
	}
	return false;
}

double clipAngleDiff(float a, float b) {
	double d = std::fmod(double(a) - double(b), 360.0);
	if(d > 180.0) {
		d -= 360.0;
	} else if(d < -180.0) {
		d += 360.0;
	}
	return std::abs(d);
}

void clipAssertPoseNear(const std::string & what, const vr::SimPose & expected, const vr::SimPose & actual) {
	CPPUNIT_ASSERT_MESSAGE(what + " position", glm::length(expected.position - actual.position) <= ClipMeters);
	for(int i = 0; i < 3; i++) {
		CPPUNIT_ASSERT_MESSAGE(what + " angle " + std::to_string(i),
		                       clipAngleDiff(expected.angles[i], actual.angles[i]) <= ClipDegrees);
	}
}

} // namespace ARX_ANONYMOUS_NAMESPACE

void ReplayClipTest::testDialectCommands() {

	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("head 0.1 1.2 -0.3 10 20 30"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, sim.rig.head.position.x, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.2, sim.rig.head.position.y, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.3, sim.rig.head.position.z, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, sim.rig.head.angles.x, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, sim.rig.head.angles.y, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(30.0, sim.rig.head.angles.z, 1e-4);

	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand left -0.25 -0.4 -0.3 15 -25 5"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.25, sim.rig.hands[0].position.x, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.3, sim.rig.hands[0].position.z, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-25.0, sim.rig.hands[0].angles.y, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, sim.rig.hands[0].angles.z, 1e-4);

	// The aim pose lives in the grip's space; by default it is the grip itself
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, glm::length(sim.rig.aims[1].position), 1e-6);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, glm::length(sim.rig.aims[1].angles), 1e-6);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand right aim 0 -0.01 -0.05 0 -40 0"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.01, sim.rig.aims[1].position.y, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.05, sim.rig.aims[1].position.z, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-40.0, sim.rig.aims[1].angles.y, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, glm::length(sim.rig.aims[0].position), 1e-6); // the other hand's stays

	// The recorded player: engine units, degrees, the area; no level before
	CPPUNIT_ASSERT_EQUAL(s32(-1), sim.rig.player.area);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("player 1234.5 -170 5678.25 271.5 11"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1234.5, sim.rig.player.position.x, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-170.0, sim.rig.player.position.y, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5678.25, sim.rig.player.position.z, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(271.5, sim.rig.player.yaw, 1e-3);
	CPPUNIT_ASSERT_EQUAL(s32(11), sim.rig.player.area);

	// The floor under the LOCAL origin (the STAGE space): 1.7 m as before, or what a clip says
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.7, sim.view.floor, 1e-5);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("floor 1.8325"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.8325, sim.view.floor, 1e-5);

	// Anchors are accepted (a clip loads as a script); they change nothing of the rig there
	const vr::SimPose head = sim.rig.head;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("anchor level 11"));
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("anchor save"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, glm::length(head.position - sim.rig.head.position), 1e-6);
}

void ReplayClipTest::testDialectRefusals() {

	vr::Simulation sim;
	const char * const bad[] = {
		"head 1 2 3 4 5",            // five numbers: neither pos, rot nor the whole pose
		"head 1 2 3 4 5 6 7",
		"hand left 1 2 3",
		"hand left 1 2 3 4 5 x",
		"hand left aim 1 2 3",
		"hand both aim 0 0 0 0 0 0", // aims are per hand
		"player 1 2 3",
		"player 1 2 3 4 x",
		"player 1 2 3 4 1.5",        // an area is a whole number
		"floor",
		"floor x",
		"anchor",
		"anchor level",
		"anchor level x",
		"anchor teleport 3",
	};
	for(const char * line : bad) {
		CPPUNIT_ASSERT_MESSAGE(std::string("refused: ") + line, !sim.execute(line).empty());
	}

	// A bad command anywhere on a line refuses the whole script and names the line
	std::string error = sim.load("head 0 0 0 0 0 0\n@0ms head 1 2 3 4 5 6; hand left 1 2\n");
	CPPUNIT_ASSERT_MESSAGE(error, clipStartsWith(error, "line 2:"));
}

void ReplayClipTest::testSeveralCommandsOnOneLine() {

	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("@100ms head 0.1 0.2 0.3 10 20 30; hand right off; "
	                                              "action trigger right 0.5; player 1 2 3 4 10\n"));
	sim.advance(0, 0);
	CPPUNIT_ASSERT(sim.rig.handTracked[1]);
	CPPUNIT_ASSERT_EQUAL(s32(-1), sim.rig.player.area);
	sim.advance(9, 100000);
	sim.advance(10, 111111);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, sim.rig.head.position.z, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, sim.rig.head.angles.y, 1e-4);
	CPPUNIT_ASSERT(!sim.rig.handTracked[1]);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.actions["trigger"][1].x, 1e-5);
	CPPUNIT_ASSERT_EQUAL(s32(10), sim.rig.player.area);

	// The channel's form: now
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand right on; head 0 0 0 5 0 0"));
	CPPUNIT_ASSERT(sim.rig.handTracked[1]);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, sim.rig.head.angles.x, 1e-4);
}

void ReplayClipTest::testHeaderRoundTrip() {

	vr::ReplayHeader header = clipMakeHeader();
	header.seed = 18446744073709551557ull; // the largest 64-bit prime: every digit survives
	const std::string text = vr::formatReplayHeader(header);
	const std::vector<std::string> lines = clipSplitLines(text);

	CPPUNIT_ASSERT_MESSAGE(text, !lines.empty() && clipStartsWith(lines[0], "# arx-replay"));
	CPPUNIT_ASSERT_MESSAGE(text, clipHasLine(text, "# build: Arx Fatalis VR 1.3.0-dev"));
	CPPUNIT_ASSERT_MESSAGE(text, clipHasLine(text, "# save: save0003 replay 2026-10-20 14-03-12"));
	CPPUNIT_ASSERT_MESSAGE(text, clipHasLine(text, "# seed: 18446744073709551557"));
	CPPUNIT_ASSERT_MESSAGE(text, clipHasLine(text, "# rate: 90") || clipHasLine(text, "# rate: 90.0")
	                             || clipHasLine(text, "# rate: 90.00"));
	bool ipd = false, floor = false;
	for(const std::string & line : lines) {
		CPPUNIT_ASSERT_MESSAGE(line, line.empty() || line[0] != '@'); // a header has no frame
		ipd = ipd || clipStartsWith(line, "ipd 0.0617");
		floor = floor || clipStartsWith(line, "floor 1.8325");
		CPPUNIT_ASSERT_MESSAGE(line, !clipStartsWith(line, "# mark")); // no mark in a session's clip
	}
	CPPUNIT_ASSERT_MESSAGE(text, ipd);
	CPPUNIT_ASSERT_MESSAGE(text, floor);

	// Read back by the player, and its set-up lines applied with the first frame
	ClipSource clip;
	clip.lines = clipSplitLines(text + vr::formatReplayFrame(clipMakeFrame(0)));
	vr::ClipPlayer player(clip.source());
	CPPUNIT_ASSERT_EQUAL(header.build, player.header().build);
	CPPUNIT_ASSERT_EQUAL(header.save, player.header().save);
	CPPUNIT_ASSERT_EQUAL(header.seed, player.header().seed);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(90.0, player.header().rate, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0617, player.header().ipd, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.8325, player.header().floor, 1e-5);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.header().mark);
	CPPUNIT_ASSERT_EQUAL(s64(-1), player.header().markMs);
	vr::Simulation sim;
	player.advance(0, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0617, sim.view.ipd, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.8325, sim.view.floor, 1e-5);

	// A mark's clip names the mark and its display time
	header.mark = "chest-did-not-open";
	header.markMs = 61234;
	const std::string marked = vr::formatReplayHeader(header);
	CPPUNIT_ASSERT_MESSAGE(marked, clipHasLine(marked, "# mark: chest-did-not-open 61234"));
	ClipSource markClip;
	markClip.lines = clipSplitLines(marked);
	vr::ClipPlayer markPlayer(markClip.source());
	CPPUNIT_ASSERT_EQUAL(std::string("chest-did-not-open"), markPlayer.header().mark);
	CPPUNIT_ASSERT_EQUAL(s64(61234), markPlayer.header().markMs);
}

void ReplayClipTest::testFullFrameRoundTrip() {

	// Frames with every kind of state: moving, the dashboard up, the headset idle, no profile, a hand lost, no joints
	for(size_t k : { size_t(0), size_t(37), size_t(105), size_t(125), size_t(155), size_t(172), size_t(182), size_t(250) }) {

		const vr::ReplayFrame original = clipMakeFrame(k);
		const std::string formatted = vr::formatReplayFrame(original);
		const std::string what = "frame " + std::to_string(k) + ": " + formatted;

		// One line, "@<ms>ms ", absolute, the time rounded to whole milliseconds
		CPPUNIT_ASSERT_MESSAGE(what, !formatted.empty() && formatted.back() == '\n');
		CPPUNIT_ASSERT_MESSAGE(what, formatted.find('\n') == formatted.size() - 1);
		const s64 ms = clipLineMs(formatted);
		CPPUNIT_ASSERT_MESSAGE(what, ms >= 0);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, s64((original.timeUs + 500) / 1000), ms);

		// Through the dialect into a fresh simulator, and formatted again: the same bytes
		ClipSource clip;
		clip.lines = clipSplitLines(vr::formatReplayHeader(clipMakeHeader()) + formatted);
		vr::ClipPlayer player(clip.source());
		vr::Simulation sim;
		player.advance(0, clipGame(), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, std::string(), player.error());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms) * 1000, player.frameTimeUs());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, formatted, vr::formatReplayFrame(clipCapture(player, sim)));
	}
}

void ReplayClipTest::testDeltaStreamRoundTrip() {

	const size_t count = 300;
	const std::string text = clipMakeStream(count);
	ClipSource clip;
	clip.lines = clipSplitLines(text);

	// One line per frame after the header, each "@<ms>ms ...", the times in order
	std::vector<s64> times;
	for(const std::string & line : clip.lines) {
		if(!line.empty() && line[0] == '@') {
			const s64 ms = clipLineMs(line);
			CPPUNIT_ASSERT_MESSAGE(line, ms >= 0);
			CPPUNIT_ASSERT_MESSAGE(line, times.empty() || ms > times.back());
			times.push_back(ms);
		}
	}
	CPPUNIT_ASSERT_EQUAL(count, times.size());

	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;
	for(size_t k = 0; k < count; k++) {
		player.advance(u64(times[k]) * 1000, clipGame(), sim);
		CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
		const std::string expected = vr::formatReplayFrame(clipMakeFrame(k));
		CPPUNIT_ASSERT_EQUAL_MESSAGE("frame " + std::to_string(k), expected, vr::formatReplayFrame(clipCapture(player, sim)));
	}
	CPPUNIT_ASSERT(player.status().done);
	CPPUNIT_ASSERT_EQUAL(u64(count), player.status().framesServed);
}

void ReplayClipTest::testValuesWithinTolerance() {

	const size_t count = 260;
	ClipSource clip;
	clip.lines = clipSplitLines(clipMakeStream(count));
	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;
	for(size_t k = 0; k < count; k++) {
		const vr::ReplayFrame original = clipMakeFrame(k);
		player.advance((original.timeUs + 500) / 1000 * 1000, clipGame(), sim);
		const std::string what = "frame " + std::to_string(k);
		CPPUNIT_ASSERT_MESSAGE(what + " time", player.frameTimeUs() + 1000 > original.timeUs
		                                       && player.frameTimeUs() < original.timeUs + 1000);
		const vr::SimRig & rig = sim.rig;
		clipAssertPoseNear(what + " head", original.rig.head, rig.head);
		for(size_t hand = 0; hand < 2; hand++) {
			const std::string h = what + (hand ? " right" : " left");
			clipAssertPoseNear(h + " grip", original.rig.hands[hand], rig.hands[hand]);
			clipAssertPoseNear(h + " aim", original.rig.aims[hand], rig.aims[hand]);
			CPPUNIT_ASSERT_EQUAL_MESSAGE(h + " tracked", original.rig.handTracked[hand], rig.handTracked[hand]);
			CPPUNIT_ASSERT_EQUAL_MESSAGE(h + " joints", original.rig.fingers[hand].joints, rig.fingers[hand].joints);
			for(int i = 0; i < 3; i++) {
				CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(h + " curl", original.rig.fingers[hand].curl[i], rig.fingers[hand].curl[i], ClipCurl);
			}
			for(int i = 0; i < 2; i++) {
				CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(h + " outer curl", original.rig.fingers[hand].outer[i], rig.fingers[hand].outer[i], ClipCurl);
			}
		}
		for(const auto & [name, values] : original.rig.actions) {
			auto it = rig.actions.find(name);
			CPPUNIT_ASSERT_MESSAGE(what + " action " + name, it != rig.actions.end());
			for(size_t hand = 0; hand < 2; hand++) {
				CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(what + " action " + name, values[hand].x, it->second[hand].x, 0.01);
				CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(what + " action " + name + " y", values[hand].y, it->second[hand].y, 0.01);
			}
		}
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what + " profile", original.rig.profile, rig.profile);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what + " worn", original.rig.worn, rig.worn);
		if(original.rig.worn) {
			CPPUNIT_ASSERT_EQUAL_MESSAGE(what + " focused", original.rig.focused, rig.focused);
		}
		// The player: 1 mm is 0.1 engine units
		CPPUNIT_ASSERT_MESSAGE(what + " player", glm::length(original.rig.player.position - rig.player.position) <= 0.1f);
		CPPUNIT_ASSERT_MESSAGE(what + " player yaw", clipAngleDiff(original.rig.player.yaw, rig.player.yaw) <= ClipDegrees);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what + " area", original.rig.player.area, rig.player.area);
	}
}

void ReplayClipTest::testClipLoadsAsAScript() {

	// A clip is a .vrsim script: --vr-sim takes it as it is (a clip of up to a minute; longer ones are streamed)
	const std::string text = clipMakeStream(60);
	vr::Simulation sim;
	const std::string error = sim.load(text);
	CPPUNIT_ASSERT_EQUAL(std::string(), error);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0617, sim.view.ipd, 1e-5);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.8325, sim.view.floor, 1e-5);

	for(size_t k = 0; k <= 30; k++) {
		sim.advance(k, s64((clipMakeFrame(k).timeUs + 500) / 1000 * 1000));
	}
	const vr::ReplayFrame frame = clipMakeFrame(30);
	clipAssertPoseNear("head", frame.rig.head, sim.rig.head);
	clipAssertPoseNear("left grip", frame.rig.hands[0], sim.rig.hands[0]);
	clipAssertPoseNear("right aim", frame.rig.aims[1], sim.rig.aims[1]);
	CPPUNIT_ASSERT(glm::length(frame.rig.player.position - sim.rig.player.position) <= 0.1f);
}

void ReplayClipTest::testFrameSizeBudget() {

	// <= 2 MB per minute at 90 Hz: 2000000 / 5400 = 370 bytes for a frame where everything that moves moved -
	// the head, both grips, all ten curls and the player - 30 minutes into a session, every number at its longest
	vr::ReplayFrame previous = clipMakeFrame(7);
	previous.timeUs = 1799977778;
	vr::ReplayFrame frame = previous;
	frame.timeUs = 1799988889;
	frame.rig.head.position = Vec3f(-0.123456f, -0.054321f, -0.234567f);
	frame.rig.head.angles = Vec3f(-123.456f, -45.678f, -12.345f);
	for(size_t hand = 0; hand < 2; hand++) {
		frame.rig.hands[hand].position = Vec3f(-0.234567f, -0.456789f, -0.345678f);
		frame.rig.hands[hand].angles = Vec3f(-123.456f, -45.678f, -12.345f);
		frame.rig.fingers[hand].curl = Vec3f(0.123456f, 0.345678f, 0.567891f);
		frame.rig.fingers[hand].outer = Vec2f(0.789123f, 0.912345f);
	}
	frame.rig.player.position = Vec3f(-12345.67f, -170.25f, -23456.78f);
	frame.rig.player.yaw = 271.53f;

	const std::string line = vr::formatReplayFrame(frame, &previous);
	CPPUNIT_ASSERT_MESSAGE(line, line.size() <= 370);
	CPPUNIT_ASSERT_MESSAGE(line, clipLineMs(line) == 1799989);

	// Nothing but the head moved: much less
	vr::ReplayFrame still = frame;
	still.timeUs = 1800000000;
	still.rig.head.angles.x += 0.5f;
	const std::string small = vr::formatReplayFrame(still, &frame);
	CPPUNIT_ASSERT_MESSAGE(small, small.size() <= 100);
}

void ReplayClipTest::testPlayerReadsNoFurtherThanOneLineAhead() {

	ClipSource clip;
	clip.lines = clipSplitLines(clipMakeStream(100));
	size_t headerLines = 0;
	while(headerLines < clip.lines.size() && (clip.lines[headerLines].empty() || clip.lines[headerLines][0] != '@')) {
		headerLines++;
	}

	vr::ClipPlayer player(clip.source());
	CPPUNIT_ASSERT_MESSAGE(std::to_string(clip.next), clip.next <= headerLines + 1);
	vr::Simulation sim;
	for(size_t k = 0; k < 100; k++) {
		player.advance(u64(clipLineMs(clip.lines[headerLines + k])) * 1000, clipGame(), sim);
		CPPUNIT_ASSERT_MESSAGE("frame " + std::to_string(k) + ": " + std::to_string(clip.next) + " lines read",
		                       clip.next <= headerLines + k + 2);
	}
}

void ReplayClipTest::testThirtyMinuteClipAtMicrosecondsPerFrame() {

	// 30 minutes at 90 Hz, made line by line as it is read: the player never holds the clip
	const size_t count = 30 * 60 * 90;
	const std::vector<std::string> header = clipSplitLines(vr::formatReplayHeader(clipMakeHeader()));
	size_t next = 0;
	vr::ReplayFrame previous;
	auto source = [&](std::string & line) {
		if(next < header.size()) {
			line = header[next++];
			return true;
		}
		const size_t k = next - header.size();
		if(k >= count) {
			return false;
		}
		vr::ReplayFrame frame = clipMakeFrame(k);
		line = vr::formatReplayFrame(frame, k == 0 ? nullptr : &previous);
		if(!line.empty() && line.back() == '\n') {
			line.pop_back();
		}
		previous = frame;
		next++;
		return true;
	};

	vr::ClipPlayer player(source);
	vr::Simulation sim;
	const size_t window = 10000;
	double firstUs = 0.0, lastUs = 0.0, totalUs = 0.0;
	for(size_t k = 0; k < count; k++) {
		const u64 nowUs = (u64(k) * 11111 + 500) / 1000 * 1000;
		const auto start = std::chrono::steady_clock::now();
		player.advance(nowUs, clipGame(), sim);
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
		totalUs += us;
		if(k < window) {
			firstUs += us;
		} else if(k >= count - window) {
			lastUs += us;
		}
	}
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	CPPUNIT_ASSERT(player.status().done);
	CPPUNIT_ASSERT_EQUAL(u64(count), player.status().framesServed);
	const double meanUs = totalUs / double(count);
	const std::string numbers = "mean " + std::to_string(meanUs) + " us, first " + std::to_string(firstUs / window)
	                            + " us, last " + std::to_string(lastUs / window) + " us per frame";
	// Microseconds per frame, and no slower at the end of the clip than at its start (a cursor, not a search)
	CPPUNIT_ASSERT_MESSAGE(numbers, meanUs < 200.0);
	CPPUNIT_ASSERT_MESSAGE(numbers, lastUs / window <= 3.0 * firstUs / window + 20.0);
}

void ReplayClipTest::testWindowFromTo() {

	ClipSource clip;
	clip.lines = clipSplitLines(clipMakeStream(300));
	const s64 fromMs = s64((clipMakeFrame(100).timeUs + 500) / 1000);
	const s64 toMs = s64((clipMakeFrame(200).timeUs + 500) / 1000);

	vr::ClipPlayer player(clip.source(), u64(fromMs) * 1000, u64(toMs) * 1000);
	vr::Simulation sim;

	// The first call brings the rig to the window's start at once
	player.advance(0, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	CPPUNIT_ASSERT_EQUAL(u64(fromMs) * 1000, player.frameTimeUs());
	CPPUNIT_ASSERT_EQUAL(vr::formatReplayFrame(clipMakeFrame(100)), vr::formatReplayFrame(clipCapture(player, sim)));
	CPPUNIT_ASSERT(player.status().framesServed <= 1);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(double(fromMs), player.status().positionMs, 0.5);
	CPPUNIT_ASSERT(!player.status().done);

	// Half way
	player.advance(u64(toMs - fromMs) * 500, clipGame(), sim);
	CPPUNIT_ASSERT(!player.status().done);

	// The window's end: frame 200, done, and nothing after it
	player.advance(u64(toMs - fromMs) * 1000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(u64(toMs) * 1000, player.frameTimeUs());
	CPPUNIT_ASSERT_EQUAL(vr::formatReplayFrame(clipMakeFrame(200)), vr::formatReplayFrame(clipCapture(player, sim)));
	CPPUNIT_ASSERT(player.status().done);
	const u64 served = player.status().framesServed;
	CPPUNIT_ASSERT_MESSAGE(std::to_string(served), served == 100 || served == 101);
	player.advance(u64(toMs - fromMs) * 1000 + 500000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(u64(toMs) * 1000, player.frameTimeUs());
	CPPUNIT_ASSERT_EQUAL(served, player.status().framesServed);
	CPPUNIT_ASSERT_EQUAL(vr::formatReplayFrame(clipMakeFrame(200)), vr::formatReplayFrame(clipCapture(player, sim)));
}

void ReplayClipTest::testLevelAnchorHolds() {

	// Frames 0..199, and the level change of the recording between frames 100 and 101
	std::vector<std::string> lines = clipSplitLines(clipMakeStream(200));
	const s64 ms100 = s64((clipMakeFrame(100).timeUs + 500) / 1000);
	const s64 ms101 = s64((clipMakeFrame(101).timeUs + 500) / 1000);
	const s64 anchorMs = ms100 + 5;
	for(size_t i = 0; i < lines.size(); i++) {
		if(clipLineMs(lines[i]) == ms101) {
			lines.insert(lines.begin() + std::ptrdiff_t(i), "@" + std::to_string(anchorMs) + "ms anchor level 11");
			break;
		}
	}

	for(bool alreadyThere : { false, true }) {

		ClipSource clip;
		clip.lines = lines;
		vr::ClipPlayer player(clip.source());
		vr::Simulation sim;
		const std::string what = alreadyThere ? "the game already in level 11: " : "the game still in level 10: ";

		player.advance(u64(ms100) * 1000 + 2000, clipGame(10), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms100) * 1000, player.frameTimeUs());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(0), player.status().anchorsWaiting);

		if(alreadyThere) {
			// The replayed game changed the level by itself before the clip got there: no hold
			player.advance(u64(anchorMs) * 1000 + 1000, clipGame(11), sim);
			CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(0), player.status().anchorsWaiting);
			player.advance(u64(ms101) * 1000, clipGame(11), sim);
			CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms101) * 1000, player.frameTimeUs());
			continue;
		}

		// The clip reaches the anchor: it holds there
		player.advance(u64(anchorMs) * 1000 + 1000, clipGame(10), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(1), player.status().anchorsWaiting);
		CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(what, double(anchorMs), player.status().positionMs, 0.5);
		const u64 served = player.status().framesServed;

		// The level loads (not playable), then it is level 10 still: the play holds, no frame is served
		player.advance(u64(anchorMs) * 1000 + 300000, clipGame(10, false), sim);
		player.advance(u64(anchorMs) * 1000 + 500000, clipGame(10), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(1), player.status().anchorsWaiting);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, served, player.status().framesServed);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms100) * 1000, player.frameTimeUs());
		CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(what, double(anchorMs), player.status().positionMs, 0.5);
		CPPUNIT_ASSERT(!player.status().done);

		// Level 11 is playable: the play goes on from the anchor
		const u64 resumed = u64(anchorMs) * 1000 + 600000;
		player.advance(resumed, clipGame(11), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(0), player.status().anchorsWaiting);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms100) * 1000, player.frameTimeUs());
		player.advance(resumed + u64(ms101 - anchorMs) * 1000, clipGame(11), sim);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(ms101) * 1000, player.frameTimeUs());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, vr::formatReplayFrame(clipMakeFrame(101)), vr::formatReplayFrame(clipCapture(player, sim)));

		// To the end: done, nothing waiting
		player.advance(resumed + 10000000, clipGame(11), sim);
		CPPUNIT_ASSERT(player.status().done);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u32(0), player.status().anchorsWaiting);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(what, u64(200), player.status().framesServed);
	}
}

void ReplayClipTest::testSaveAnchorHolds() {

	std::vector<std::string> lines = clipSplitLines(clipMakeStream(50));
	const s64 ms20 = s64((clipMakeFrame(20).timeUs + 500) / 1000);
	const s64 ms21 = s64((clipMakeFrame(21).timeUs + 500) / 1000);
	for(size_t i = 0; i < lines.size(); i++) {
		if(clipLineMs(lines[i]) == ms21) {
			lines.insert(lines.begin() + std::ptrdiff_t(i), "@" + std::to_string(ms20 + 1) + "ms anchor save");
			break;
		}
	}

	ClipSource clip;
	clip.lines = lines;
	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;
	player.advance(u64(ms20) * 1000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(u32(0), player.status().anchorsWaiting);
	// The game is busy (not playable) when the clip reaches the save: hold
	player.advance(u64(ms20 + 2) * 1000, clipGame(10, false), sim);
	CPPUNIT_ASSERT_EQUAL(u32(1), player.status().anchorsWaiting);
	player.advance(u64(ms20 + 200) * 1000, clipGame(10, false), sim);
	CPPUNIT_ASSERT_EQUAL(u32(1), player.status().anchorsWaiting);
	CPPUNIT_ASSERT_EQUAL(u64(ms20) * 1000, player.frameTimeUs());
	// Playable again: on from the anchor
	player.advance(u64(ms20 + 300) * 1000, clipGame(10), sim);
	CPPUNIT_ASSERT_EQUAL(u32(0), player.status().anchorsWaiting);
	player.advance(u64(ms20 + 300) * 1000 + u64(ms21 - ms20 - 1) * 1000, clipGame(10), sim);
	CPPUNIT_ASSERT_EQUAL(u64(ms21) * 1000, player.frameTimeUs());
}

void ReplayClipTest::testEventsReplayed() {

	// Poses move every frame; one button press at frame 20, one session change at frame 30
	std::string text = vr::formatReplayHeader(clipMakeHeader());
	vr::ReplayFrame previous;
	std::vector<s64> times;
	for(size_t k = 0; k < 40; k++) {
		vr::ReplayFrame frame = clipMakeFrame(k);
		for(auto & entry : frame.rig.actions) {
			entry.second = { Vec2f(0.f), Vec2f(0.f) };
		}
		frame.rig.actions["button_a"][1].x = k >= 20 ? 1.f : 0.f;
		frame.rig.focused = k < 30;
		frame.rig.worn = true;
		text += vr::formatReplayFrame(frame, k == 0 ? nullptr : &previous);
		times.push_back(s64((frame.timeUs + 500) / 1000));
		previous = frame;
	}

	ClipSource clip;
	clip.lines = clipSplitLines(text);
	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;
	player.advance(u64(times[19]) * 1000, clipGame(), sim);
	const u64 before = player.status().eventsReplayed;
	player.advance(u64(times[20]) * 1000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(before + 1, player.status().eventsReplayed);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, sim.rig.actions["button_a"][1].x, 1e-6);
	player.advance(u64(times[29]) * 1000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(before + 1, player.status().eventsReplayed); // poses and curls are no events
	player.advance(u64(times[30]) * 1000, clipGame(), sim);
	CPPUNIT_ASSERT_EQUAL(before + 2, player.status().eventsReplayed);
	CPPUNIT_ASSERT(!sim.rig.focused);
}

void ReplayClipTest::testDrift() {

	vr::SimPlayer recorded;
	recorded.position = Vec3f(100.f, 0.f, 200.f);
	recorded.area = 10;
	vr::SimPlayer replayed = recorded;

	vr::ReplayDrift drift;
	drift.update(recorded, replayed);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, drift.current, 1e-6);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, drift.max, 1e-6);
	CPPUNIT_ASSERT_EQUAL(u64(1), drift.frames);

	// 300 and 400 units apart: 5 m
	replayed.position = Vec3f(400.f, 0.f, 600.f);
	drift.update(recorded, replayed);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, drift.current, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, drift.max, 1e-4);

	// Closer again: the current falls, the max stays; height counts too
	replayed.position = Vec3f(100.f, 30.f, 200.f);
	drift.update(recorded, replayed);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, drift.current, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, drift.max, 1e-4);
	CPPUNIT_ASSERT_EQUAL(u64(3), drift.frames);

	// Different levels, or no level: not comparable, nothing changes
	replayed.area = 11;
	replayed.position = Vec3f(9000.f, 0.f, 9000.f);
	drift.update(recorded, replayed);
	vr::SimPlayer menu;
	drift.update(menu, menu);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, drift.current, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, drift.max, 1e-4);
	CPPUNIT_ASSERT_EQUAL(u64(3), drift.frames);
}

void ReplayClipTest::testDriftThroughThePlayer() {

	ClipSource clip;
	clip.lines = clipSplitLines(clipMakeStream(11));
	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;

	// The replayed player stands where the recorded one started; the recorded one walks on
	vr::ReplayGame game = clipGame(10);
	game.player.position = clipMakeFrame(0).rig.player.position;
	player.advance(0, game, sim);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, player.status().drift.current, 1e-4);
	player.advance((clipMakeFrame(10).timeUs + 500) / 1000 * 1000, game, sim);
	const double expected = double(glm::length(clipMakeFrame(10).rig.player.position - game.player.position)) / 100.0;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, player.status().drift.current, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, player.status().drift.max, 1e-3);
	CPPUNIT_ASSERT(player.status().drift.frames >= 2);
}

void ReplayClipTest::testBrokenLineEndsThePlay() {

	std::vector<std::string> lines = clipSplitLines(clipMakeStream(20));
	size_t firstFrame = 0;
	while(firstFrame < lines.size() && (lines[firstFrame].empty() || lines[firstFrame][0] != '@')) {
		firstFrame++;
	}
	// Frame 10's line is cut off
	const size_t broken = firstFrame + 10;
	lines[broken] = "@" + std::to_string(clipLineMs(lines[broken])) + "ms head 1 2";

	ClipSource clip;
	clip.lines = lines;
	vr::ClipPlayer player(clip.source());
	vr::Simulation sim;
	player.advance(1000000, clipGame(), sim);
	const std::string error = player.error();
	CPPUNIT_ASSERT_MESSAGE(error, clipStartsWith(error, "line " + std::to_string(broken + 1) + ":"));
	CPPUNIT_ASSERT(player.status().done);
	CPPUNIT_ASSERT_EQUAL(u64((clipMakeFrame(9).timeUs + 500) / 1000 * 1000), player.frameTimeUs());

	// Not a clip at all: an error, never a crash
	ClipSource garbage;
	garbage.lines = { "@", "@ms", "@12", "@-5ms head 0 0 0 0 0 0", std::string(5000, 'x'), "@99999999999999999999ms head 0 0 0 0 0 0" };
	vr::ClipPlayer bad(garbage.source());
	vr::Simulation other;
	bad.advance(5000000, clipGame(), other);
	CPPUNIT_ASSERT(!bad.error().empty());
	CPPUNIT_ASSERT(bad.status().done);
}
