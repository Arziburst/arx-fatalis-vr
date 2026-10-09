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

#include "vr/VRTurnTest.h"

#include <string>

#include "vr/VRTurn.h"

CppUnit::AutoRegisterSuite<VRTurnTest> g_registerVRTurnTest;

using namespace vr;

namespace {

const float TtFrame = 1.f / 90.f;

//! The yaw of \a frames frames with the axis at \a axis
float hold(TurnController & turn, const TurnSettings & settings, float axis, int frames, bool allowed = true) {
	float yaw = 0.f;
	for(int i = 0; i < frames; i++) {
		yaw += turn.update(settings, axis, TtFrame, allowed);
	}
	return yaw;
}

} // anonymous namespace

void VRTurnTest::snapOncePerPushTest() {

	TurnController turn;
	TurnSettings settings;
	settings.snapRepeat = 0.f;

	// Resting stick: nothing
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 0.f, 10));

	// Pushed right and held for a second with the repeat off: one step of 45 degrees
	CPPUNIT_ASSERT_EQUAL(45.f, turn.update(settings, 1.f, TtFrame, true));
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 90));
	CPPUNIT_ASSERT_EQUAL(u64(1), turn.snaps());
	CPPUNIT_ASSERT_EQUAL(1, turn.lastSnap());

	// Let go and pushed again: the second step
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 0.f, 2));
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 30));
	CPPUNIT_ASSERT_EQUAL(u64(2), turn.snaps());
	CPPUNIT_ASSERT_EQUAL(90.f, turn.turned());
}

void VRTurnTest::snapRepeatTest() {

	TurnController turn;
	TurnSettings settings;
	CPPUNIT_ASSERT_EQUAL(0.4f, settings.snapRepeat);

	// Held for 2 s: the snap, then a repeat 0.4 s later and every 0.4 s after it - 1 + 4 steps
	CPPUNIT_ASSERT_EQUAL(45.f, turn.update(settings, 1.f, TtFrame, true));
	CPPUNIT_ASSERT(!turn.lastSnapRepeated());
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 35)); // 0.39 s: not yet
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 2)); // 0.41 s: the first repeat
	CPPUNIT_ASSERT(turn.lastSnapRepeated());
	CPPUNIT_ASSERT_EQUAL(135.f, hold(turn, settings, 1.f, 142)); // to 2 s
	CPPUNIT_ASSERT_EQUAL(u64(5), turn.snaps());

	// Let go and pushed again: a new snap at once, its repeat counted from it
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(45.f, turn.update(settings, 1.f, TtFrame, true));
	CPPUNIT_ASSERT(!turn.lastSnapRepeated());
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 35));

	// Eased back between the thresholds, the repeat waits and starts over when pushed again
	hold(turn, settings, SnapRearm + 0.1f, 50);
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 35));
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 2));

	// Held to the left: the repeats go left
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(-90.f, hold(turn, settings, -1.f, 38));
	CPPUNIT_ASSERT_EQUAL(-1, turn.lastSnap());

	// Another pause
	settings.snapRepeat = 1.f;
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 89));
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 3));
}

void VRTurnTest::snapHysteresisTest() {

	TurnController turn;
	TurnSettings settings;

	// Below the fire threshold: nothing
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, SnapFire - 0.01f, 10));
	CPPUNIT_ASSERT_EQUAL(45.f, turn.update(settings, SnapFire, TtFrame, true));

	// Falling back between the thresholds and pushing again does not repeat
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, SnapRearm + 0.05f, 5));
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 5));
	CPPUNIT_ASSERT_EQUAL(u64(1), turn.snaps());

	// Below the re-arm threshold it fires again
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, SnapRearm - 0.01f, 1));
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 5));
}

void VRTurnTest::snapLeftTest() {

	TurnController turn;
	TurnSettings settings;

	CPPUNIT_ASSERT_EQUAL(-45.f, hold(turn, settings, -1.f, 20));
	CPPUNIT_ASSERT_EQUAL(-1, turn.lastSnap());

	// Straight from left to right: the stick passes the middle, so it re-arms on the way
	CPPUNIT_ASSERT_EQUAL(0.f, turn.update(settings, 0.f, TtFrame, true));
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 20));
	CPPUNIT_ASSERT_EQUAL(0.f, turn.turned());
}

void VRTurnTest::snapAngleTest() {

	TurnController turn;
	TurnSettings settings;
	settings.snapAngle = 30.f;

	CPPUNIT_ASSERT_EQUAL(30.f, hold(turn, settings, 1.f, 5));
	settings.snapAngle = 90.f;
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(-90.f, hold(turn, settings, -1.f, 5));
}

void VRTurnTest::smoothTest() {

	TurnController turn;
	TurnSettings settings;
	settings.mode = TurnMode::Smooth;

	// Full deflection for one second: the speed, spread over the frames
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.33333f, turn.update(settings, 1.f, TtFrame, true), 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(120.f, 1.33333f + hold(turn, settings, 1.f, 89), 1e-2f);

	// Half deflection to the left: half the speed
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-30.f, turn.update(settings, -0.5f, 0.5f, true), 1e-4f);

	// No snaps in smooth mode, and a resting stick does not turn
	CPPUNIT_ASSERT_EQUAL(u64(0), turn.snaps());
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 0.f, 10));

	// Another speed
	settings.smoothSpeed = 60.f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(60.f, turn.update(settings, 1.f, 1.f, true), 1e-4f);

	// A negative frame time (clock trouble) does not turn backwards
	CPPUNIT_ASSERT_EQUAL(0.f, turn.update(settings, 1.f, -1.f, true));
}

void VRTurnTest::blockedTest() {

	TurnController turn;
	TurnSettings settings;

	// Pushed while blocked: nothing, and nothing when the block ends with the stick still pushed - not even
	// a repeat after a second
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 10, false));
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 90));

	// A block in the middle of a held snap stops its repeats
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 10));
	hold(turn, settings, 1.f, 1, false);
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 90));
	CPPUNIT_ASSERT_EQUAL(u64(1), turn.snaps());
	hold(turn, settings, 0.f, 1);
	turn = TurnController();
	CPPUNIT_ASSERT_EQUAL(u64(0), turn.snaps());

	// Back to the middle, then a push: one snap
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 10));

	// Smooth turn does not move while blocked either
	settings.mode = TurnMode::Smooth;
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 10, false));
}

void VRTurnTest::modeSwitchTest() {

	TurnController turn;
	TurnSettings settings;
	settings.mode = TurnMode::Smooth;

	// Smooth turning, then switched to snap with the stick still pushed: no snap until it comes back
	hold(turn, settings, 1.f, 10);
	settings.mode = TurnMode::Snap;
	CPPUNIT_ASSERT_EQUAL(0.f, hold(turn, settings, 1.f, 90));
	hold(turn, settings, 0.f, 1);
	CPPUNIT_ASSERT_EQUAL(45.f, hold(turn, settings, 1.f, 10));
}

void VRTurnTest::modeNameTest() {

	CPPUNIT_ASSERT_EQUAL(std::string("snap"), std::string(turnModeName(TurnMode::Snap)));
	CPPUNIT_ASSERT_EQUAL(std::string("smooth"), std::string(turnModeName(TurnMode::Smooth)));
	CPPUNIT_ASSERT(parseTurnMode("smooth") == TurnMode::Smooth);
	CPPUNIT_ASSERT(parseTurnMode("snap") == TurnMode::Snap);
	CPPUNIT_ASSERT(!parseTurnMode("fast"));
}
