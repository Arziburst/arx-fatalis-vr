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

#include "vr/MarkHoldTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "platform/Platform.h"
#include "vr/MarkMoment.h"
#include "vr/ReplayClip.h"
#include "vr/SimRig.h"
#include "vr/VoiceNote.h"

CppUnit::AutoRegisterSuite<MarkHoldTest> g_registerMarkHoldTest;

using namespace vr;

namespace {

//! One frame of the Index at 144 Hz, in microseconds
const u64 MhFrameUs = 6944;

//! Frame k of a 144 Hz session: everything moves, the right A is held now and then, the profile is the Index's
ReplayFrame mhMakeFrame(size_t k) {
	ReplayFrame f;
	f.timeUs = u64(k) * MhFrameUs;
	const float t = float(k) * 0.006944f;
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
	f.rig.profile = "/interaction_profiles/valve/index_controller";
	f.rig.actions["trigger"] = { Vec2f(0.f), Vec2f(0.f) };
	f.rig.actions["button_a"] = { Vec2f(0.f), Vec2f(((k / 1000) % 2) ? 1.f : 0.f, 0.f) };
	f.rig.actions["thumbstick"] = { Vec2f(0.f, 0.9f), Vec2f(0.f) };
	f.rig.player.position = Vec3f(500.f + float(k) * 0.25f, 100.f, 800.f);
	f.rig.player.yaw = std::fmod(float(k) * 0.05f, 360.f);
	f.rig.player.area = 10;
	return f;
}

ReplayHeader mhHeader() {
	ReplayHeader header;
	header.build = "Arx Fatalis VR 1.3.0-dev";
	header.save = "save0007 replay 2026-10-21 10-00-00";
	header.seed = 574;
	header.rate = 144.f;
	header.ipd = 0.064f;
	header.floor = 1.7f;
	return header;
}

//! One frame of the Index at 80 Hz, in microseconds
const u64 MhFrame80Us = 12500;

//! The display times of \a count frames \a frameUs apart from 0
std::vector<u64> mhTimes(size_t count, u64 frameUs) {
	std::vector<u64> times;
	for(size_t k = 0; k < count; k++) {
		times.push_back(u64(k) * frameUs);
	}
	return times;
}

//! Frame k of the session at the display time \a times[k] (the rate the times say, whatever the header's)
ReplayFrame mhFrameAt(const std::vector<u64> & times, size_t k) {
	ReplayFrame frame = mhMakeFrame(k);
	frame.timeUs = times[k];
	return frame;
}

//! The last frame at or before \a us
size_t mhFrameBefore(const std::vector<u64> & times, u64 us) {
	size_t k = 0;
	while(k + 1 < times.size() && times[k + 1] <= us) {
		k++;
	}
	return k;
}

//! The game's ring (ReplayRingSeconds at ReplayRingMaxRate) in 16-byte aligned memory, a frame pushed for each of
//! \a times; its header is the session's at \a rate Hz (the display rate is fixed for a session: it changes only with
//! a SteamVR restart - the owner, 2026-10-21)
struct MhRing {
	std::vector<u64> words;
	size_t size = 0;
	explicit MhRing(size_t count) : MhRing(mhTimes(count, MhFrameUs)) { }
	explicit MhRing(const std::vector<u64> & times, float rate = 144.f) {
		size = ReplayRing::memorySize(ReplayRingSeconds, ReplayRingMaxRate);
		words.assign(size / sizeof(u64) + 2, 0);
		ReplayRing ring(data(), size, ReplayRingSeconds, ReplayRingMaxRate);
		ReplayHeader header = mhHeader();
		header.rate = rate;
		ring.setHeader(header);
		for(size_t k = 0; k < times.size(); k++) {
			ring.push(mhFrameAt(times, k));
		}
	}
	void * data() {
		u8 * bytes = reinterpret_cast<u8 *>(words.data());
		return bytes + (reinterpret_cast<uintptr_t>(bytes) % 16 == 0 ? 0 : 8);
	}
};

std::vector<std::string> mhLines(const std::string & text) {
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

s64 mhLineMs(const std::string & line) {
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

//! The frame times (ms) of a clip's frame lines, ascending
std::vector<s64> mhFrameTimes(const std::string & clip) {
	std::vector<s64> times;
	for(const std::string & line : mhLines(clip)) {
		if(!line.empty() && line[0] == '@') {
			const s64 ms = mhLineMs(line);
			CPPUNIT_ASSERT_MESSAGE(line, ms >= 0);
			CPPUNIT_ASSERT_MESSAGE(line, times.empty() || ms > times.back());
			times.push_back(ms);
		}
	}
	return times;
}

//! The header lines of a clip: everything before its first frame line
std::vector<std::string> mhHeaderLines(const std::string & clip) {
	std::vector<std::string> header;
	for(const std::string & line : mhLines(clip)) {
		if(!line.empty() && line[0] == '@') {
			break;
		}
		header.push_back(line);
	}
	return header;
}

//! The clip plays on its own: its first and last frames are frames firstK and lastK of the session, the first in
//! full; the "# hold:" line reads back; it loads as a --vr-sim script
void mhAssertPlays(const std::string & clip, const std::vector<u64> & times, size_t firstK, size_t lastK, s64 pressMs,
                   s64 releaseMs) {

	std::vector<std::string> lines = mhLines(clip);
	size_t next = 0;
	ClipPlayer player([&](std::string & line) {
		if(next >= lines.size()) {
			return false;
		}
		line = lines[next++];
		return true;
	});
	CPPUNIT_ASSERT_EQUAL_MESSAGE("ClipPlayer reads the hold's press back", pressMs, player.header().holdPressMs);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("ClipPlayer reads the hold's release back", releaseMs, player.header().holdReleaseMs);

	Simulation sim;
	ReplayGame game;
	player.advance(0, game, sim);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	ReplayFrame first;
	first.timeUs = player.frameTimeUs();
	first.rig = sim.rig;
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the clip's first frame carries the whole state",
	                             formatReplayFrame(mhFrameAt(times, firstK)), formatReplayFrame(first));

	const u64 span = times[lastK] - times[firstK];
	player.advance(span + 2000, game, sim);
	CPPUNIT_ASSERT_EQUAL(std::string(), player.error());
	ReplayFrame last;
	last.timeUs = player.frameTimeUs();
	last.rig = sim.rig;
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the clip ends at the release's frame", formatReplayFrame(mhFrameAt(times, lastK)),
	                             formatReplayFrame(last));

	Simulation script;
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the hold's clip loads as a --vr-sim script", std::string(), script.load(clip));
}

//! The "# hold:" line right after the "# mark:" line, before the first frame
void mhAssertHoldHeader(const std::string & clip, const std::string & mark, s64 pressMs, s64 releaseMs) {
	const std::vector<std::string> header = mhHeaderLines(clip);
	const std::string markLine = "# mark: " + mark + " " + std::to_string(releaseMs);
	const std::string holdLine = "# hold: " + std::to_string(pressMs) + " " + std::to_string(releaseMs);
	size_t markAt = header.size();
	size_t holdAt = header.size();
	for(size_t i = 0; i < header.size(); i++) {
		if(header[i] == markLine) {
			markAt = i;
		}
		if(header[i] == holdLine) {
			holdAt = i;
		}
	}
	CPPUNIT_ASSERT_MESSAGE("the clip is the mark's, at the release: " + markLine, markAt < header.size());
	CPPUNIT_ASSERT_MESSAGE("the header has " + holdLine, holdAt < header.size());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("\"# hold:\" is the line right after \"# mark:\"", markAt + 1, holdAt);
}

//! The clip's "# rate:" line is \a rate (the session's display rate), and reads back
void mhAssertRate(const std::string & clip, const std::string & rate) {
	const std::vector<std::string> header = mhHeaderLines(clip);
	size_t found = 0;
	std::string seen;
	for(const std::string & line : header) {
		if(line.compare(0, 8, "# rate: ") == 0) {
			found++;
			seen = line;
		}
	}
	CPPUNIT_ASSERT_EQUAL_MESSAGE("one \"# rate:\" line", size_t(1), found);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the hold's rate in the clip's header", "# rate: " + rate, seen);
	std::vector<std::string> lines = mhLines(clip);
	size_t next = 0;
	ClipPlayer player([&](std::string & line) {
		if(next >= lines.size()) {
			return false;
		}
		line = lines[next++];
		return true;
	});
	CPPUNIT_ASSERT_EQUAL_MESSAGE("ClipPlayer reads the hold's rate back", std::stof(rate), player.header().rate);
}

//! What snapshotOffsets must give for a hold length, built from the rule itself
std::vector<s64> mhExpectedSnaps(s64 heldMs) {
	std::vector<s64> expected;
	if(heldMs < HoldThresholdMs) {
		return expected;
	}
	const s64 length = std::min(heldMs, HoldMaxMs);
	for(s64 at = SnapshotIntervalMs; at < length; at += SnapshotIntervalMs) {
		expected.push_back(at);
	}
	// The release, unless it falls in the same tenth of a second as the last periodic one (the same name)
	if(expected.empty() || length / 100 != expected.back() / 100) {
		expected.push_back(length);
	}
	return expected;
}

std::string mhList(const std::vector<s64> & values) {
	std::string text;
	for(s64 value : values) {
		text += (text.empty() ? "" : " ") + std::to_string(value);
	}
	return text;
}

} // anonymous namespace

void MarkHoldTest::classifierTest() {

	// About 500 ms (the owner tunes it in the headset): past a natural force-press, well below a sentence
	CPPUNIT_ASSERT_MESSAGE("HoldThresholdMs is about 500 ms", HoldThresholdMs >= 400 && HoldThresholdMs <= 800);
	CPPUNIT_ASSERT_MESSAGE("... longer than the shortest note kept", HoldThresholdMs > VoiceNote::minMs);

	CPPUNIT_ASSERT(markKind(0) == MarkKind::Press);
	CPPUNIT_ASSERT_MESSAGE("a 0.2 s press is short", markKind(200) == MarkKind::Press);
	CPPUNIT_ASSERT_MESSAGE("the shortest note's length is still short", markKind(VoiceNote::minMs) == MarkKind::Press);
	CPPUNIT_ASSERT_MESSAGE("one ms before the threshold: short", markKind(HoldThresholdMs - 1) == MarkKind::Press);
	CPPUNIT_ASSERT_MESSAGE("at the threshold: a hold", markKind(HoldThresholdMs) == MarkKind::Hold);
	CPPUNIT_ASSERT(markKind(HoldThresholdMs + 1) == MarkKind::Hold);
	CPPUNIT_ASSERT_MESSAGE("a 7 s hold", markKind(7000) == MarkKind::Hold);
	CPPUNIT_ASSERT(markKind(HoldMaxMs) == MarkKind::Hold);
	CPPUNIT_ASSERT_MESSAGE("past the cap: still a hold", markKind(HoldMaxMs + 15000) == MarkKind::Hold);
	CPPUNIT_ASSERT_MESSAGE("no time (a clock step back): short", markKind(-40) == MarkKind::Press);

	CPPUNIT_ASSERT_EQUAL(std::string("press"), std::string(markKindName(MarkKind::Press)));
	CPPUNIT_ASSERT_EQUAL(std::string("hold"), std::string(markKindName(MarkKind::Hold)));
}

void MarkHoldTest::capTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("a hold is at most 1 minute (the owner)", s64(60000), HoldMaxMs);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the voice note never outlasts the hold", HoldMaxMs, VoiceNote::maxMs);

	CPPUNIT_ASSERT(!holdCapped(0));
	CPPUNIT_ASSERT(!holdCapped(7000));
	CPPUNIT_ASSERT_MESSAGE("59.999 s: still held", !holdCapped(HoldMaxMs - 1));
	CPPUNIT_ASSERT_MESSAGE("1:00: the hold ends by itself", holdCapped(HoldMaxMs));
	CPPUNIT_ASSERT_MESSAGE("a frame late: ended", holdCapped(HoldMaxMs + 11));

	CPPUNIT_ASSERT_EQUAL(s64(0), holdLengthMs(-5));
	CPPUNIT_ASSERT_EQUAL(s64(700), holdLengthMs(700));
	CPPUNIT_ASSERT_EQUAL(s64(7000), holdLengthMs(7000));
	CPPUNIT_ASSERT_EQUAL(s64(59999), holdLengthMs(59999));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a hold is never longer than the cap", HoldMaxMs, holdLengthMs(HoldMaxMs + 11));
	CPPUNIT_ASSERT_EQUAL(HoldMaxMs, holdLengthMs(300000));
}

void MarkHoldTest::scheduleTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("every 2 s", s64(2000), SnapshotIntervalMs);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("at most 31 a hold", size_t(31), SnapshotMax);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the clip starts 10 s before the press", s64(10000), HoldContextMs);

	// A short press: the press's own pictures only, no snapshot
	CPPUNIT_ASSERT_EQUAL(std::string(), mhList(snapshotOffsets(0)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a 0.2 s press has no snapshot", std::string(), mhList(snapshotOffsets(200)));
	CPPUNIT_ASSERT_EQUAL(std::string(), mhList(snapshotOffsets(HoldThresholdMs - 1)));

	// A hold: the periodic ones before the release, then the release
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the shortest hold: the release only", std::to_string(HoldThresholdMs),
	                             mhList(snapshotOffsets(HoldThresholdMs)));
	CPPUNIT_ASSERT_EQUAL(std::string("1999"), mhList(snapshotOffsets(1999)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("released at 2 s: one picture", std::string("2000"), mhList(snapshotOffsets(2000)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("2.001 s: the release is the 2 s one (the same name)", std::string("2000"),
	                             mhList(snapshotOffsets(2001)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("2.05 s: the release is the 2 s one (the same name)", std::string("2000"),
	                             mhList(snapshotOffsets(2050)));
	CPPUNIT_ASSERT_EQUAL(std::string("2000 2100"), mhList(snapshotOffsets(2100)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the sim's 7 s hold: about 2/4/6 s plus the release", std::string("2000 4000 6000 7000"),
	                             mhList(snapshotOffsets(7000)));
	CPPUNIT_ASSERT_EQUAL(std::string("2000 4000 6000 7049"), mhList(snapshotOffsets(7049)));
	CPPUNIT_ASSERT_EQUAL(std::string("2000 4000 6000"), mhList(snapshotOffsets(6000)));
	CPPUNIT_ASSERT_EQUAL(std::string("2000 4000 6000"), mhList(snapshotOffsets(6050)));
	CPPUNIT_ASSERT_EQUAL(std::string("2000 4000 6000 6100"), mhList(snapshotOffsets(6100)));

	// A full minute: 2 .. 58 s and the release at 1:00 - and a longer hold is cut at the cap
	std::vector<s64> minute;
	for(s64 at = 2000; at <= 58000; at += 2000) {
		minute.push_back(at);
	}
	minute.push_back(60000);
	CPPUNIT_ASSERT_EQUAL(size_t(30), minute.size());
	CPPUNIT_ASSERT_EQUAL(mhList(minute), mhList(snapshotOffsets(60000)));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("past the cap: the minute's snapshots", mhList(minute), mhList(snapshotOffsets(61000)));
	CPPUNIT_ASSERT_EQUAL(mhList(minute), mhList(snapshotOffsets(600000)));
	std::vector<s64> almost(minute.begin(), minute.end() - 1);
	almost.push_back(59999);
	CPPUNIT_ASSERT_EQUAL(mhList(almost), mhList(snapshotOffsets(59999)));
}

void MarkHoldTest::scheduleBoundsTest() {

	for(s64 held = -100; held <= 62000; held += 7) {
		const std::vector<s64> offsets = snapshotOffsets(held);
		const std::string at = "held " + std::to_string(held) + " ms";
		CPPUNIT_ASSERT_EQUAL_MESSAGE(at, mhList(mhExpectedSnaps(held)), mhList(offsets));
		CPPUNIT_ASSERT_MESSAGE(at + ": at most SnapshotMax", offsets.size() <= SnapshotMax);
		if(markKind(held) == MarkKind::Press) {
			CPPUNIT_ASSERT_MESSAGE(at + ": a short press has none", offsets.empty());
			continue;
		}
		CPPUNIT_ASSERT_MESSAGE(at + ": a hold has at least the release", !offsets.empty());
		std::set<std::string> names;
		for(size_t i = 0; i < offsets.size(); i++) {
			CPPUNIT_ASSERT_MESSAGE(at + ": after the press", offsets[i] > 0);
			CPPUNIT_ASSERT_MESSAGE(at + ": within the hold", offsets[i] <= holdLengthMs(held));
			CPPUNIT_ASSERT_MESSAGE(at + ": ascending", i == 0 || offsets[i] > offsets[i - 1]);
			names.insert(snapshotName(offsets[i]));
		}
		CPPUNIT_ASSERT_EQUAL_MESSAGE(at + ": every snapshot has its own name", offsets.size(), names.size());
		CPPUNIT_ASSERT_MESSAGE(at + ": the last one is the release's (within its tenth of a second)",
		                       holdLengthMs(held) - offsets.back() < 100);
		// The names sort as the times do (the listing and lastFiles sort by name)
		CPPUNIT_ASSERT_EQUAL_MESSAGE(at + ": names sort by time", snapshotName(offsets.front()), *names.begin());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(at + ": names sort by time", snapshotName(offsets.back()), *names.rbegin());
	}
}

void MarkHoldTest::snapshotNameTest() {

	CPPUNIT_ASSERT_EQUAL(std::string("snap-+00.0s.jpg"), snapshotName(0));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+00.5s.jpg"), snapshotName(500));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+02.0s.jpg"), snapshotName(2000));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+04.0s.jpg"), snapshotName(4000));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+06.0s.jpg"), snapshotName(6000));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+07.0s.jpg"), snapshotName(7000));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("rounded down to 0.1 s", std::string("snap-+07.0s.jpg"), snapshotName(7099));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+07.1s.jpg"), snapshotName(7100));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("rounded down, never up", std::string("snap-+06.9s.jpg"), snapshotName(6999));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+12.0s.jpg"), snapshotName(12000));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+42.3s.jpg"), snapshotName(42345));
	CPPUNIT_ASSERT_EQUAL(std::string("snap-+59.9s.jpg"), snapshotName(59999));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the cap's release", std::string("snap-+60.0s.jpg"), snapshotName(60000));

	// Valid on every file system, no separator
	for(s64 ms : { s64(0), s64(2000), s64(7049), s64(60000) }) {
		const std::string name = snapshotName(ms);
		CPPUNIT_ASSERT_MESSAGE(name, name.find_first_of("\\/:*?\"<>| ") == std::string::npos);
	}
}

void MarkHoldTest::indicatorFromThresholdTest() {

	const std::string mic = "\xF0\x9F\x8E\xA4";
	std::string text;
	MarkNoticeState state;
	state.markedNumber = 2;
	state.recording = true;

	// Still a short press: the confirmation, never the microphone (it showed from VoiceNote::minMs before #574)
	for(s64 ms : { s64(0), s64(200), s64(VoiceNote::minMs), s64(HoldThresholdMs - 1) }) {
		state.recordingMs = ms;
		state.sinceMarkedMs = ms;
		CPPUNIT_ASSERT_MESSAGE("recording " + std::to_string(ms) + " ms: still a short press - no indicator",
		                       markNotice(state, text) == MarkNotice::Marked);
		CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#2)"), text);
	}

	// From the threshold: a hold, the indicator counting up
	state.recordingMs = HoldThresholdMs;
	state.sinceMarkedMs = HoldThresholdMs;
	CPPUNIT_ASSERT_MESSAGE("at the threshold: the indicator", markNotice(state, text) == MarkNotice::Recording);
	CPPUNIT_ASSERT_EQUAL(mic + " 0:00", text);
	state.recordingMs = 7000;
	state.sinceMarkedMs = 7000;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Recording);
	CPPUNIT_ASSERT_EQUAL(mic + " 0:07", text);
}

void MarkHoldTest::metaKindTest() {

	MarkMeta meta;
	CPPUNIT_ASSERT_MESSAGE("a new mark is a press until its release says otherwise", meta.kind == MarkKind::Press);
	CPPUNIT_ASSERT_EQUAL(s64(-1), meta.pressMs);
	CPPUNIT_ASSERT_EQUAL(s64(-1), meta.releaseMs);

	meta.number = 4;
	meta.unixSeconds = 1792510212;
	meta.area = 10;
	meta.position = Vec3f(1234.5f, -150.f, 4321.3f);
	meta.yaw = 90.f;
	meta.build = "Arx Fatalis VR 1.3-dev abc1234 Release";
	meta.state = "GAMEPLAY";
	meta.session = "focused";
	meta.kind = MarkKind::Hold;
	meta.pressMs = 43000;
	meta.releaseMs = 50021;

	const std::string expected =
		"# moment mark (#551)\n"
		"mark=4\n"
		"time=2026-10-20T15:30:12Z\n"
		"area=10\n"
		"position=1234.5 -150.0 4321.3\n"
		"yaw=90.0\n"
		"build=Arx Fatalis VR 1.3-dev abc1234 Release\n"
		"state=GAMEPLAY\n"
		"session=focused\n"
		"kind=hold\n"
		"pressMs=43000\n"
		"releaseMs=50021\n";
	CPPUNIT_ASSERT_EQUAL(expected, formatMarkMeta(meta));

	// A short press
	meta.kind = MarkKind::Press;
	meta.pressMs = 12000;
	meta.releaseMs = 12200;
	const std::string press = formatMarkMeta(meta);
	CPPUNIT_ASSERT_MESSAGE(press, press.find("\nsession=focused\nkind=press\npressMs=12000\nreleaseMs=12200\n") != std::string::npos);

	// Before the clip's clock started (a mark in the menu before any gameplay)
	MarkMeta early;
	early.number = 1;
	const std::string text = formatMarkMeta(early);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("\nkind=press\npressMs=-1\nreleaseMs=-1\n") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE("the last line ends the text", text.back() == '\n');
}

void MarkHoldTest::metaPoseTest() {

	// The pose of the press frame (the owner, 2026-10-21): the head, both grips with their tracked state, both hands'
	// five curls - in the units and order of the .vrsim "head" / "hand" / "hand ... fingers" commands
	SimRig rig;
	rig.head.position = Vec3f(0.0123f, 1.6512f, -0.0456f);
	rig.head.angles = Vec3f(12.5f, -3.25f, -0.001f);
	rig.hands[0].position = Vec3f(-0.2f, 1.2f, -0.35f);
	rig.hands[0].angles = Vec3f(10.f, -20.f, 5.f);
	rig.hands[1].position = Vec3f(0.25f, 1.1f, -0.3f);
	rig.hands[1].angles = Vec3f(-15.75f, 30.f, 0.f);
	rig.handTracked = { true, false };
	rig.fingers[0].curl = Vec3f(0.1f, 0.2f, 0.3f);
	rig.fingers[0].outer = Vec2f(0.4f, 0.5f);
	rig.fingers[1].curl = Vec3f(1.f, 1.f, 1.f);
	rig.fingers[1].outer = Vec2f(0.f, 0.f);

	MarkMeta meta;
	CPPUNIT_ASSERT_MESSAGE("no pose until the game gives one", !meta.posed);
	meta.number = 5;
	meta.unixSeconds = 1792510212;
	meta.area = 10;
	meta.state = "GAMEPLAY";
	meta.session = "focused";
	meta.kind = MarkKind::Press;
	meta.pressMs = 12000;
	meta.releaseMs = 12200;
	meta.posed = true;
	meta.pose = rig;
	const std::string text = formatMarkMeta(meta);

	const std::string pose =
		"\nreleaseMs=12200\n"
		"head=0.0123 1.6512 -0.0456 12.50 -3.25 0.00\n"
		"left=-0.2000 1.2000 -0.3500 10.00 -20.00 5.00 on\n"
		"right=0.2500 1.1000 -0.3000 -15.75 30.00 0.00 off\n"
		"curlsLeft=0.10 0.20 0.30 0.40 0.50\n"
		"curlsRight=1.00 1.00 1.00 0.00 0.00\n";
	CPPUNIT_ASSERT_MESSAGE(text, text.size() >= pose.size() && text.compare(text.size() - pose.size(), pose.size(), pose) == 0);

	// The same numbers as the frame's clip commands (a worker pastes them into a .vrsim as they are)
	ReplayFrame frame;
	frame.rig = rig;
	ReplayFrameText commands;
	formatReplayFrameText(frame, commands);
	const auto command = [&commands](const std::string & key) {
		for(size_t i = 0; i < commands.keys.size(); i++) {
			if(commands.keys[i] == key) {
				return commands.commands[i];
			}
		}
		return std::string();
	};
	const auto has = [&text](const std::string & line) { return text.find("\n" + line + "\n") != std::string::npos; };
	CPPUNIT_ASSERT_MESSAGE(command("head"), has("head=" + command("head").substr(5)));
	CPPUNIT_ASSERT_MESSAGE(command("hand left"), has("left=" + command("hand left").substr(10) + " on"));
	CPPUNIT_ASSERT_MESSAGE(command("hand right"), has("right=" + command("hand right").substr(11) + " off"));
	CPPUNIT_ASSERT_MESSAGE(command("hand left fingers"), has("curlsLeft=" + command("hand left fingers").substr(18)));
	CPPUNIT_ASSERT_MESSAGE(command("hand right fingers"), has("curlsRight=" + command("hand right fingers").substr(19)));

	// A hold's meta.txt carries the press frame's pose the same way
	meta.kind = MarkKind::Hold;
	meta.releaseMs = 19200;
	const std::string hold = formatMarkMeta(meta);
	CPPUNIT_ASSERT(hold.find("\nkind=hold\npressMs=12000\nreleaseMs=19200\nhead=0.0123 1.6512 -0.0456 12.50 -3.25 0.00\n")
	               != std::string::npos);

	// No pose (no eye poses that frame): no pose lines
	meta.posed = false;
	const std::string bare = formatMarkMeta(meta);
	CPPUNIT_ASSERT_MESSAGE(bare, bare.find("head=") == std::string::npos && bare.find("curlsLeft=") == std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(bare, bare.size() > 15 && bare.compare(bare.size() - 16, 16, "releaseMs=19200\n") == 0);
}

void MarkHoldTest::ringSizeTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the ring keeps 75 s (it was 60)", u32(75), ReplayRingSeconds);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("... sized for the Index at 144 Hz", u32(144), ReplayRingMaxRate);
	CPPUNIT_ASSERT_MESSAGE("a minute's hold and its 10 s before fit in the ring",
	                       s64(ReplayRingSeconds) * 1000 >= HoldMaxMs + HoldContextMs);

	// 80 s at 144 Hz pushed: the last 75 s kept
	const size_t count = 80 * 144;
	MhRing ring(count);
	const std::vector<s64> times = mhFrameTimes(replayRingClip(ring.data(), ring.size));
	CPPUNIT_ASSERT(!times.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the newest frame is kept", s64((u64(count - 1) * MhFrameUs + 500) / 1000), times.back());
	CPPUNIT_ASSERT_MESSAGE("75 s at 144 Hz are kept (" + std::to_string(times.back() - times.front()) + " ms)",
	                       times.back() - times.front() >= 74990);
}

void MarkHoldTest::holdClipMinuteTest() {

	// 95 s of a 144 Hz session; held from 35 s to 95 s - the full minute - then cut at the release
	const size_t count = 95 * 144 + 1;
	MhRing ring(count);
	const u64 pressUs = 35000000;
	const u64 releaseUs = 95000000;
	const std::string mark = "20261021T101500Z-4";
	const std::string clip = replayRingHoldClip(ring.data(), ring.size, mark, pressUs, releaseUs,
	                                            u64(HoldContextMs) * 1000);

	mhAssertHoldHeader(clip, mark, 35000, 95000);
	const std::vector<s64> times = mhFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	CPPUNIT_ASSERT_MESSAGE("the clip starts 10 s before the press (" + std::to_string(times.front()) + " ms)",
	                       times.front() <= 25000 && times.front() >= 25000 - 7);
	CPPUNIT_ASSERT_MESSAGE("the clip ends at the release (" + std::to_string(times.back()) + " ms)",
	                       times.back() <= 95000 && times.back() >= 95000 - 7);
	CPPUNIT_ASSERT_MESSAGE("a minute's hold plus its context: at least 69.9 s ("
	                       + std::to_string(times.back() - times.front()) + " ms)", times.back() - times.front() >= 69900);
	CPPUNIT_ASSERT_MESSAGE("every frame of the 70 s at 144 Hz", times.size() >= size_t(70 * 144));
	mhAssertRate(clip, "144");

	mhAssertPlays(clip, mhTimes(count, MhFrameUs), size_t((pressUs - u64(HoldContextMs) * 1000) / MhFrameUs),
	              size_t(releaseUs / MhFrameUs), 35000, 95000);
}

void MarkHoldTest::holdClipEightyHzTest() {

	// A session at 80 Hz (the Index set to 80 Hz in SteamVR; the rate is fixed for a session): 140 s of it - more than
	// the 75 s the ring is sized for at 144 Hz, and at 80 Hz the ring holds more (10801 frames: 135 s). Held a minute
	// from 70 s to 130 s: the window by display time, the header at 80 Hz
	const std::vector<u64> session = mhTimes(140 * 80 + 1, MhFrame80Us);
	MhRing ring(session, 80.f);
	const std::vector<s64> all = mhFrameTimes(replayRingClip(ring.data(), ring.size));
	CPPUNIT_ASSERT(!all.empty());
	CPPUNIT_ASSERT_MESSAGE("at 80 Hz the ring holds more than 75 s (" + std::to_string(all.back() - all.front()) + " ms)",
	                       all.back() - all.front() >= 134900);

	const u64 pressUs = 70000000;
	const u64 releaseUs = 130000000;
	const std::string clip = replayRingHoldClip(ring.data(), ring.size, "20261021T120000Z-1", pressUs, releaseUs,
	                                            u64(HoldContextMs) * 1000);
	mhAssertHoldHeader(clip, "20261021T120000Z-1", 70000, 130000);
	const std::vector<s64> times = mhFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("from 10 s before the press", s64(60000), times.front());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("to the release", s64(130000), times.back());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("every frame of the 70 s at 80 Hz", size_t(70 * 80 + 1), times.size());
	mhAssertRate(clip, "80");
	mhAssertPlays(clip, session, mhFrameBefore(session, pressUs - u64(HoldContextMs) * 1000),
	              mhFrameBefore(session, releaseUs), 70000, 130000);
}

void MarkHoldTest::holdClipShortHoldTest() {

	// 60 s recorded; a 7 s hold from 43 s to 50 s (the newest frames after it are not the hold's)
	MhRing ring(60 * 144 + 1);
	const u64 pressUs = 43000000;
	const u64 releaseUs = 50000000;
	const std::string clip = replayRingHoldClip(ring.data(), ring.size, "20261021T101500Z-2", pressUs, releaseUs,
	                                            u64(HoldContextMs) * 1000);

	mhAssertHoldHeader(clip, "20261021T101500Z-2", 43000, 50000);
	const std::vector<s64> times = mhFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	CPPUNIT_ASSERT_MESSAGE("from 33 s", times.front() <= 33000 && times.front() >= 33000 - 7);
	CPPUNIT_ASSERT_MESSAGE("to 50 s, nothing after the release", times.back() <= 50000 && times.back() >= 50000 - 7);
	const s64 span = times.back() - times.front();
	CPPUNIT_ASSERT_MESSAGE("7 s held + 10 s context = 17 s (" + std::to_string(span) + " ms)", span >= 16990 && span <= 17000);
	mhAssertRate(clip, "144");

	mhAssertPlays(clip, mhTimes(60 * 144 + 1, MhFrameUs), size_t((pressUs - u64(HoldContextMs) * 1000) / MhFrameUs),
	              size_t(releaseUs / MhFrameUs), 43000, 50000);
}

void MarkHoldTest::holdClipLittleHistoryTest() {

	// The hold starts 4 s after the record start: the clip starts at the oldest frame
	const size_t count = 9 * 144 + 1;
	MhRing ring(count);
	const u64 pressUs = 4000600; // rounded like the mark's ms: 4001
	const u64 releaseUs = u64(count - 1) * MhFrameUs;
	const std::string clip = replayRingHoldClip(ring.data(), ring.size, "20261021T101500Z-1", pressUs, releaseUs,
	                                            u64(HoldContextMs) * 1000);
	const s64 releaseMs = s64((releaseUs + 500) / 1000);

	mhAssertHoldHeader(clip, "20261021T101500Z-1", 4001, releaseMs);
	const std::vector<s64> times = mhFrameTimes(clip);
	CPPUNIT_ASSERT(!times.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("from the oldest frame kept", s64(0), times.front());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("to the release", releaseMs, times.back());
	CPPUNIT_ASSERT_EQUAL(count, times.size());
	mhAssertPlays(clip, mhTimes(count, MhFrameUs), 0, count - 1, 4001, releaseMs);
}

void MarkHoldTest::holdHeaderTest() {

	// Written: right after "# mark: "
	ReplayHeader header = mhHeader();
	header.mark = "20261021T101500Z-4";
	header.markMs = 95000;
	header.holdPressMs = 35000;
	header.holdReleaseMs = 95000;
	const std::string text = formatReplayHeader(header);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("\n# mark: 20261021T101500Z-4 95000\n# hold: 35000 95000\n") != std::string::npos);

	// No hold: no line (the #550 clips, the crash reports' arx-replay.vrsim, a short press has no clip at all)
	ReplayHeader plain = mhHeader();
	plain.mark = "bug";
	plain.markMs = 1234;
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a clip that is no hold's has no \"# hold:\" line", std::string::npos,
	                             formatReplayHeader(plain).find("# hold:"));
	CPPUNIT_ASSERT_EQUAL(std::string::npos, formatReplayHeader(mhHeader()).find("# hold:"));
	MhRing ring(20 * 144);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("`replay mark` clips stay as they were", std::string::npos,
	                             replayRingClip(ring.data(), ring.size, "bug", 15000000, 30000000).find("# hold:"));

	// Read back; a clip without the line reads -1
	std::vector<std::string> lines = mhLines(text + "@1000ms head 0.0000 0.0000 0.0000 0.00 0.00 0.00\n");
	size_t next = 0;
	ClipPlayer player([&](std::string & line) {
		if(next >= lines.size()) {
			return false;
		}
		line = lines[next++];
		return true;
	});
	CPPUNIT_ASSERT_EQUAL(s64(35000), player.header().holdPressMs);
	CPPUNIT_ASSERT_EQUAL(s64(95000), player.header().holdReleaseMs);
	CPPUNIT_ASSERT_EQUAL(std::string("20261021T101500Z-4"), player.header().mark);

	std::vector<std::string> plainLines = mhLines(formatReplayHeader(plain) + "@1000ms head 0.0000 0.0000 0.0000 0.00 0.00 0.00\n");
	next = 0;
	ClipPlayer plainPlayer([&](std::string & line) {
		if(next >= plainLines.size()) {
			return false;
		}
		line = plainLines[next++];
		return true;
	});
	CPPUNIT_ASSERT_EQUAL(s64(-1), plainPlayer.header().holdPressMs);
	CPPUNIT_ASSERT_EQUAL(s64(-1), plainPlayer.header().holdReleaseMs);
}
