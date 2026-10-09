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

#include "vr/BodyYawTest.h"

#include <cmath>
#include <string>

#include "vr/BodyYaw.h"

CppUnit::AutoRegisterSuite<BodyYawTest> g_registerBodyYawTest;

namespace {

void checkYaw(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(std::abs(vr::wrapSignedDegrees(expected - actual)) > tolerance) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! The body after \a seconds of the head held at \a head, in steps of \a step
float hold(float body, float head, float seconds, float step = 1.f / 90.f) {
	for(float t = 0.f; t < seconds - step * 0.5f; t += step) {
		body = vr::followHead(body, head, step).yaw;
	}
	return body;
}

} // anonymous namespace

void BodyYawTest::testWrapSignedDegrees() {
	checkYaw("0", 0.f, vr::wrapSignedDegrees(0.f));
	CPPUNIT_ASSERT_EQUAL(180.f, vr::wrapSignedDegrees(180.f));
	CPPUNIT_ASSERT_EQUAL(180.f, vr::wrapSignedDegrees(-180.f));
	CPPUNIT_ASSERT_EQUAL(-170.f, vr::wrapSignedDegrees(190.f));
	CPPUNIT_ASSERT_EQUAL(10.f, vr::wrapSignedDegrees(-350.f));
	CPPUNIT_ASSERT_EQUAL(-90.f, vr::wrapSignedDegrees(270.f));
	CPPUNIT_ASSERT_EQUAL(30.f, vr::wrapSignedDegrees(750.f));
}

void BodyYawTest::testStillHeadKeepsTheBody() {
	vr::BodyFollow step = vr::followHead(12.f, 12.f, 0.5f);
	checkYaw("body on the head", 12.f, step.yaw);
	CPPUNIT_ASSERT(!step.dragged);
	// No time, no relaxation: a head inside the dead zone leaves the body where it is
	step = vr::followHead(0.f, 20.f, 0.f);
	checkYaw("no time", 0.f, step.yaw);
	CPPUNIT_ASSERT(!step.dragged);
	step = vr::followHead(0.f, 20.f, -1.f);
	checkYaw("negative time", 0.f, step.yaw);
}

void BodyYawTest::testDeadZoneDragsAtOnce() {
	// Head 40° left, 40° right of the body: the body is pulled to 25° from it in the same step
	vr::BodyFollow left = vr::followHead(0.f, 40.f, 0.f);
	checkYaw("left", 15.f, left.yaw);
	CPPUNIT_ASSERT(left.dragged);
	vr::BodyFollow right = vr::followHead(0.f, -40.f, 0.f);
	checkYaw("right", -15.f, right.yaw);
	CPPUNIT_ASSERT(right.dragged);
	// Looking over the shoulder: the body keeps 25° behind the head all the way
	float body = 0.f;
	for(float head = 0.f; head <= 150.f; head += 5.f) {
		body = vr::followHead(body, head, 0.f).yaw;
		CPPUNIT_ASSERT(std::abs(vr::wrapSignedDegrees(head - body)) <= 25.001f);
	}
	checkYaw("over the shoulder", 125.f, body);
}

void BodyYawTest::testSettlesInsideTheDeadZone() {
	// 20° inside the dead zone: after the settle time 5% of the gap is left, after twice that 0.25%
	checkYaw("1.5 s", 20.f * (1.f - std::exp(-3.f)), hold(0.f, 20.f, 1.5f), 0.01f);
	CPPUNIT_ASSERT(std::abs(20.f - hold(0.f, 20.f, 1.5f)) <= 1.f + 0.01f);
	CPPUNIT_ASSERT(std::abs(20.f - hold(0.f, 20.f, 3.f)) < 0.06f);
	// After a drag the body goes on turning until it faces the head
	float body = vr::followHead(0.f, 60.f, 0.f).yaw;
	checkYaw("dragged", 35.f, body);
	CPPUNIT_ASSERT(std::abs(60.f - hold(body, 60.f, 3.f)) < 0.1f);
}

void BodyYawTest::testGlanceBarelyTurnsTheBody() {
	// A glance of 20° for 0.2 s and back: the body moves a few degrees and returns
	float body = hold(0.f, 20.f, 0.2f);
	CPPUNIT_ASSERT(body > 5.f && body < 8.f);
	body = hold(body, 0.f, 3.f);
	CPPUNIT_ASSERT(std::abs(body) < 0.05f);
}

void BodyYawTest::testShortWayAcrossTheSeam() {
	// Body at 170°, head at -170° (190°): 20° to the left, not 340° to the right
	float body = hold(170.f, -170.f, 3.f);
	checkYaw("across", -170.f, body, 0.05f);
	vr::BodyFollow drag = vr::followHead(170.f, -150.f, 0.f);
	checkYaw("drag across", -175.f, drag.yaw);
	CPPUNIT_ASSERT(drag.dragged);
	CPPUNIT_ASSERT(drag.yaw > -180.f && drag.yaw <= 180.f);
}

void BodyYawTest::testStepSizeDoesNotMatter() {
	// 90 Hz, 144 Hz and one long step land in the same place: the relaxation is in time, not frames
	float a = hold(0.f, 20.f, 1.f, 1.f / 90.f);
	float b = hold(0.f, 20.f, 1.f, 1.f / 144.f);
	float c = vr::followHead(0.f, 20.f, 1.f).yaw;
	checkYaw("90 vs 144", a, b, 0.01f);
	checkYaw("90 vs one step", a, c, 0.01f);
}

void BodyYawTest::testDegenerateParams() {
	vr::BodyFollowParams locked;
	locked.deadZoneDegrees = 0.f;
	vr::BodyFollow step = vr::followHead(0.f, 30.f, 0.f, locked);
	checkYaw("no dead zone", 30.f, step.yaw);
	vr::BodyFollowParams instant;
	instant.settleSeconds = 0.f;
	checkYaw("no settle time", 20.f, vr::followHead(0.f, 20.f, 0.01f, instant).yaw);
	vr::BodyFollowParams negative;
	negative.deadZoneDegrees = -5.f;
	checkYaw("negative dead zone", 30.f, vr::followHead(0.f, 30.f, 0.f, negative).yaw);
}
