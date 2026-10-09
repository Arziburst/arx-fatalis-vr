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

#include "vr/LocomotionTest.h"

#include <cmath>
#include <string>

#include "graphics/Math.h"
#include "vr/Locomotion.h"

CppUnit::AutoRegisterSuite<LocomotionTest> g_registerLocomotionTest;

namespace {

vr::Stick stick(float x, float y) {
	vr::Stick result;
	result.x = x;
	result.y = y;
	return result;
}

void checkDirection(const char * what, Vec3f expected, Vec3f actual) {
	if(glm::length(expected - actual) > 0.001f) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.z)
		             + "), got (" + std::to_string(actual.x) + ", " + std::to_string(actual.z) + ")");
	}
}

void checkFlags(const char * what, const vr::StickWalk & walk, bool forward, bool backward, bool left, bool right) {
	if(walk.forward != forward || walk.backward != backward || walk.left != left || walk.right != right) {
		CPPUNIT_FAIL(std::string(what) + ": flags forward/backward/left/right "
		             + std::to_string(walk.forward) + std::to_string(walk.backward)
		             + std::to_string(walk.left) + std::to_string(walk.right));
	}
}

} // anonymous namespace

void LocomotionTest::testRestingStickDoesNotWalk() {
	vr::StickWalk walk = vr::stickWalk(stick(0.f, 0.f), 37.f);
	CPPUNIT_ASSERT(!walk.moving());
	checkDirection("rest", Vec3f(0.f), walk.direction);
}

void LocomotionTest::testForwardIsTheLook() {
	// D-008: the stick's forward is where the player looks, whatever the yaw - the key's forward vector
	for(float yaw : { 0.f, 90.f, 180.f, 270.f, 33.f, -120.f }) {
		vr::StickWalk walk = vr::stickWalk(stick(0.f, 0.6f), yaw);
		checkDirection("forward", angleToVectorXZ(yaw), walk.direction);
		checkFlags("forward", walk, true, false, false, false);
	}
	// Engine degrees: yaw 0 walks +z, a larger yaw turns left (-x)
	checkDirection("yaw 0", Vec3f(0.f, 0.f, 1.f), vr::stickWalk(stick(0.f, 1.f), 0.f).direction);
	checkDirection("yaw 90", Vec3f(-1.f, 0.f, 0.f), vr::stickWalk(stick(0.f, 1.f), 90.f).direction);
}

void LocomotionTest::testStrafeAndBack() {
	const float yaw = 90.f;
	vr::StickWalk right = vr::stickWalk(stick(1.f, 0.f), yaw);
	checkDirection("right", angleToVectorXZ(yaw - 90.f), right.direction);
	checkFlags("right", right, false, false, false, true);
	vr::StickWalk left = vr::stickWalk(stick(-0.5f, 0.f), yaw);
	checkDirection("left", angleToVectorXZ(yaw + 90.f), left.direction);
	checkFlags("left", left, false, false, true, false);
	vr::StickWalk back = vr::stickWalk(stick(0.f, -1.f), yaw);
	checkDirection("back", angleToVectorXZ_180offset(yaw), back.direction);
	checkFlags("back", back, false, true, false, false);
}

void LocomotionTest::testDiagonal() {
	// The direction keeps the stick's angle, and a weak deflection walks the same way as a full one
	const float c = std::sqrt(0.5f);
	vr::StickWalk walk = vr::stickWalk(stick(0.3f, 0.3f), 0.f);
	checkDirection("forward right", Vec3f(c, 0.f, c), walk.direction);
	checkFlags("forward right", walk, true, false, false, true);
	walk = vr::stickWalk(stick(-1.f, -1.f), 0.f);
	checkDirection("back left", Vec3f(-c, 0.f, -c), walk.direction);
	checkFlags("back left", walk, false, true, true, false);
	// 30 degrees right of forward, turned by the look
	walk = vr::stickWalk(stick(0.5f, std::sqrt(0.75f)), 90.f);
	checkDirection("30 right", angleToVectorXZ(60.f), walk.direction);
}

void LocomotionTest::testSectors() {
	// Within 22.5 degrees of an axis the stick is that axis alone, like a single key
	const float in = glm::radians(20.f);
	const float out = glm::radians(25.f);
	checkFlags("20 off forward", vr::stickWalk(stick(std::sin(in), std::cos(in)), 0.f), true, false, false, false);
	checkFlags("25 off forward", vr::stickWalk(stick(std::sin(out), std::cos(out)), 0.f), true, false, false, true);
	checkFlags("20 off left", vr::stickWalk(stick(-std::cos(in), std::sin(in)), 0.f), false, false, true, false);
	checkFlags("25 off left", vr::stickWalk(stick(-std::cos(out), -std::sin(out)), 0.f), false, true, true, false);
}

void LocomotionTest::testWalkYawFollowsTheHead() {
	// Without a head the look is the heading
	CPPUNIT_ASSERT_DOUBLES_EQUAL(30.f, vr::stickWalkYaw(30.f, 30.f, 30.f, 0.f), 0.001f);
	// The head turned 90 degrees left of the heading 10, the body dragged to 65 (offset 55)
	CPPUNIT_ASSERT_DOUBLES_EQUAL(100.f, vr::stickWalkYaw(10.f, 100.f, 65.f, 55.f), 0.001f);
	// The heading turned 20 more since the look was taken: the walk turns with it in this frame
	CPPUNIT_ASSERT_DOUBLES_EQUAL(120.f, vr::stickWalkYaw(30.f, 100.f, 65.f, 55.f), 0.001f);
	// Across the seam: heading 350, head 90 right (look 260), body 285 with offset -65
	CPPUNIT_ASSERT_DOUBLES_EQUAL(260.f, vr::stickWalkYaw(350.f, 260.f, 285.f, -65.f), 0.001f);
}
