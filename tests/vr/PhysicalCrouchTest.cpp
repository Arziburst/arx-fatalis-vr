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

#include "vr/PhysicalCrouchTest.h"

#include <algorithm>

#include "vr/PhysicalCrouch.h"

CppUnit::AutoRegisterSuite<PhysicalCrouchTest> g_registerPhysicalCrouchTest;

void PhysicalCrouchTest::testThresholds() {
	// The owner's numbers: crouch 30 cm below the calibrated eye, stand again at 20 cm
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(false, 0.f));
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(false, 0.29f));
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(false, 0.30f));
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(false, 0.35f));
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(true, 0.25f));
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(true, 0.20f));
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(true, 0.15f));
	// A head above the reference (before auto-raise catches up) stands
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(true, -0.05f));
}

void PhysicalCrouchTest::testHysteresis() {
	// A head going down and up again, wobbling around each threshold: one crouch, one stand - no flicker
	bool crouched = false;
	int changes = 0;
	const float path[] = { 0.f, 0.1f, 0.28f, 0.31f, 0.27f, 0.31f, 0.35f, 0.26f, 0.22f, 0.18f, 0.22f, 0.25f,
	                       0.18f, 0.f };
	for(float depth : path) {
		const bool now = vr::updatePhysicalCrouch(crouched, depth);
		if(now != crouched) {
			changes++;
		}
		crouched = now;
		if(depth >= 0.30f) {
			CPPUNIT_ASSERT(crouched);
		}
		if(depth < 0.20f) {
			CPPUNIT_ASSERT(!crouched);
		}
	}
	// Down at 0.31, up at 0.18; the head back at 0.22 and 0.25 does not crouch again
	CPPUNIT_ASSERT_EQUAL(2, changes);
}

void PhysicalCrouchTest::testSwitchedOff() {
	vr::CrouchParams off;
	off.enabled = false;
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(false, 0.8f, off));
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(true, 0.8f, off));
}

void PhysicalCrouchTest::testMisconfiguredExit() {
	// An exit deeper than the entry would stand up and crouch again every frame: the entry is the exit then
	vr::CrouchParams params;
	params.enterMeters = 0.25f;
	params.exitMeters = 0.40f;
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(false, 0.30f, params));
	CPPUNIT_ASSERT(vr::updatePhysicalCrouch(true, 0.30f, params));
	CPPUNIT_ASSERT(!vr::updatePhysicalCrouch(true, 0.20f, params));
}

void PhysicalCrouchTest::testCameraStanding() {
	// The character stands: the camera is the head (D-018) - 20 cm lower is 20 units lower
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::headDropOnCamera(0.f, 0.f)), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, double(vr::headDropOnCamera(20.f, 0.f)), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-5.0, double(vr::headDropOnCamera(-5.f, 0.f)), 1e-4);
}

void PhysicalCrouchTest::testCameraCrouched() {
	// The character's eye 60 units lower: the head 35 cm down sees from the crouched eye, not 35 below it
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::headDropOnCamera(35.f, 60.f)), 1e-4);
	// Lower than the character's crouch: the rest moves the camera
	CPPUNIT_ASSERT_DOUBLES_EQUAL(15.0, double(vr::headDropOnCamera(75.f, 60.f)), 1e-4);
	// The crouch button with the head up: the crouched eye, as the game has it
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::headDropOnCamera(0.f, 60.f)), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-3.0, double(vr::headDropOnCamera(-3.f, 60.f)), 1e-4);
}

void PhysicalCrouchTest::testCameraContinuousThroughTheCrouch() {
	// The crouch animation lowers the character's eye over a few frames while the head is still: the view
	// (the eye's drop plus the camera offset) stays with the head until the eye passes below it
	const float head = 35.f;
	float last = -1.f;
	for(float character = 0.f; character <= 60.f; character += 5.f) {
		const float view = character + vr::headDropOnCamera(head, character);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(double(std::max(head, character)), double(view), 1e-4);
		CPPUNIT_ASSERT(view >= last);
		last = view;
	}
}
