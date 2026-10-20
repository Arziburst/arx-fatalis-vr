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

#include "vr/RecordGateTest.h"

#include "vr/ReplayClip.h"

CppUnit::AutoRegisterSuite<RecordGateTest> g_registerRecordGateTest;

using Action = vr::RecordGate::Action;

void RecordGateTest::testStartsAtTheFirstPlayableFrameWithViews() {
	vr::RecordGate gate;
	// No session, a session loading (not playable), a playable frame without eye poses: nothing yet
	CPPUNIT_ASSERT(gate.step(false, false, false) == Action::Skip);
	CPPUNIT_ASSERT(gate.step(true, true, false) == Action::Skip);
	CPPUNIT_ASSERT(gate.step(true, false, true) == Action::Skip);
	CPPUNIT_ASSERT(!gate.recording());
	// The first playable frame with eye poses starts the recording and is recorded
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Start);
	CPPUNIT_ASSERT(gate.recording());
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Record);
	// A menu or a level load in the middle: still recorded (the clip's anchors mark the load)
	CPPUNIT_ASSERT(gate.step(true, true, false) == Action::Record);
}

void RecordGateTest::testLostTrackingSkipsTheFrameOnly() {
	vr::RecordGate gate;
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Start);
	// The headset loses tracking for three frames: skipped, the recording goes on - no Finish, no second Start
	for(int i = 0; i < 3; i++) {
		CPPUNIT_ASSERT(gate.step(true, false, true) == Action::Skip);
		CPPUNIT_ASSERT(gate.recording());
	}
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Record);
}

void RecordGateTest::testSessionEndFinishesOnce() {
	vr::RecordGate gate;
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Start);
	CPPUNIT_ASSERT(gate.step(false, false, true) == Action::Finish);
	CPPUNIT_ASSERT(!gate.recording());
	// Frames without a session after that: nothing, and no second Finish
	CPPUNIT_ASSERT(gate.step(false, false, true) == Action::Skip);
	CPPUNIT_ASSERT(gate.step(false, false, false) == Action::Skip);
}

void RecordGateTest::testNewSessionRecordsAgain() {
	vr::RecordGate gate;
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Start);
	CPPUNIT_ASSERT(gate.step(false, false, true) == Action::Finish);
	// A new session (the runtime came back): its first playable frame with eye poses starts a new recording
	CPPUNIT_ASSERT(gate.step(true, false, true) == Action::Skip);
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Start);
	CPPUNIT_ASSERT(gate.step(true, true, true) == Action::Record);
	CPPUNIT_ASSERT(gate.step(false, false, false) == Action::Finish);
}
