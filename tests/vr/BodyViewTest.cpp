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

#include "vr/BodyViewTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "math/Types.h"
#include "vr/BodyView.h"

CppUnit::AutoRegisterSuite<BodyViewTest> g_registerBodyViewTest;

namespace {

void bvCheckFloat(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! The engine facing for a yaw in degrees (angleToVectorXZ): x = -sin, z = cos; y is down
Vec3f bvForward(float yawDegrees) {
	const float t = glm::radians(yawDegrees);
	return Vec3f(-std::sin(t), 0.f, std::cos(t));
}

const float bvYaws[] = { 0.f, 30.f, 90.f, 135.f, 180.f, -90.f, -45.f, 270.f };

const Vec3f bvEye(100.f, -50.f, 200.f);

//! A tiny triangle at a point
bool bvHidden(const Vec3f & p, const Vec3f & eyeLeft, const Vec3f & eyeRight, const vr::BodyViewParams & params) {
	return vr::hideFaceNearEye(p, p + Vec3f(1.f, 0.f, 0.f), p + Vec3f(0.f, 0.f, 1.f), eyeLeft, eyeRight, params);
}

} // anonymous namespace

void BodyViewTest::testPivotIsBelowTheEye() {
	vr::BodyViewParams params;
	for(float yaw : bvYaws) {
		Vec3f pivot = vr::neckPivot(bvEye, yaw, params);
		// Engine y is down: below the eye is a larger y
		bvCheckFloat("pivot below the eye", bvEye.y + params.neckBelowEye, pivot.y);
	}
}

void BodyViewTest::testPivotIsPushedBehindTheFacing() {
	vr::BodyViewParams params;
	for(float yaw : bvYaws) {
		Vec3f pivot = vr::neckPivot(bvEye, yaw, params);
		Vec3f expected = bvEye - bvForward(yaw) * params.push;
		bvCheckFloat("pivot x", expected.x, pivot.x);
		bvCheckFloat("pivot z", expected.z, pivot.z);
		// The horizontal distance is the push, and it is behind: the offset from the eye opposes the facing
		Vec3f flat(pivot.x - bvEye.x, 0.f, pivot.z - bvEye.z);
		bvCheckFloat("horizontal distance", params.push, glm::length(flat));
		CPPUNIT_ASSERT(glm::dot(flat, bvForward(yaw)) < 0.f);
	}
}

void BodyViewTest::testPushFollowsTheParameters() {
	vr::BodyViewParams params;
	params.push = 0.f;
	params.neckBelowEye = 0.f;
	Vec3f onEye = vr::neckPivot(bvEye, 40.f, params);
	bvCheckFloat("no push x", bvEye.x, onEye.x);
	bvCheckFloat("no drop y", bvEye.y, onEye.y);
	bvCheckFloat("no push z", bvEye.z, onEye.z);
	params.push = 30.f;
	params.neckBelowEye = 25.f;
	Vec3f moved = vr::neckPivot(bvEye, 0.f, params);
	// Yaw 0 faces +z: behind is -z
	bvCheckFloat("yaw 0 x", bvEye.x, moved.x);
	bvCheckFloat("yaw 0 z", bvEye.z - 30.f, moved.z);
	bvCheckFloat("yaw 0 y", bvEye.y + 25.f, moved.y);
}

void BodyViewTest::testOffsetPutsTheNeckOnThePivot() {
	vr::BodyViewParams params;
	const Vec3f modelNeck(-12.f, 40.f, 7.f);
	for(float yaw : bvYaws) {
		Vec3f pivot = vr::neckPivot(bvEye, yaw, params);
		Vec3f offset = vr::bodyOffset(bvEye, yaw, modelNeck, params);
		Vec3f moved = modelNeck + offset;
		bvCheckFloat("neck x", pivot.x, moved.x);
		bvCheckFloat("neck y", pivot.y, moved.y);
		bvCheckFloat("neck z", pivot.z, moved.z);
	}
	// A neck already on the pivot needs no shift
	Vec3f pivot = vr::neckPivot(bvEye, 20.f, params);
	Vec3f none = vr::bodyOffset(bvEye, 20.f, pivot, params);
	bvCheckFloat("no shift x", 0.f, none.x);
	bvCheckFloat("no shift y", 0.f, none.y);
	bvCheckFloat("no shift z", 0.f, none.z);
}

void BodyViewTest::testFaceAtTheEyeIsHidden() {
	vr::BodyViewParams params;
	Vec3f eyeLeft = bvEye + Vec3f(-3.f, 0.f, 0.f);
	Vec3f eyeRight = bvEye + Vec3f(3.f, 0.f, 0.f);
	CPPUNIT_ASSERT_MESSAGE("a face at the eye", bvHidden(bvEye, eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("just inside the clearance",
	                       bvHidden(bvEye + Vec3f(0.f, 0.f, params.clearance - 3.f), eyeLeft, eyeRight, params));
}

void BodyViewTest::testFaceFarFromTheEyesIsDrawn() {
	vr::BodyViewParams params;
	Vec3f eyeLeft = bvEye + Vec3f(-3.f, 0.f, 0.f);
	Vec3f eyeRight = bvEye + Vec3f(3.f, 0.f, 0.f);
	const float away = params.clearance * 2.f;
	CPPUNIT_ASSERT_MESSAGE("in front", !bvHidden(bvEye + Vec3f(0.f, 0.f, away), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("beside", !bvHidden(bvEye + Vec3f(away, 0.f, 0.f), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("above", !bvHidden(bvEye + Vec3f(0.f, -away, 0.f), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("below", !bvHidden(bvEye + Vec3f(0.f, away, 0.f), eyeLeft, eyeRight, params));
}

void BodyViewTest::testEitherEyeCounts() {
	vr::BodyViewParams params;
	// A wide gap between the eyes, so that only one eye is within the clearance of a point at the other
	Vec3f eyeLeft = bvEye + Vec3f(-40.f, 0.f, 0.f);
	Vec3f eyeRight = bvEye + Vec3f(40.f, 0.f, 0.f);
	CPPUNIT_ASSERT_MESSAGE("at the left eye", bvHidden(eyeLeft, eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("at the right eye", bvHidden(eyeRight, eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("between them", !bvHidden(bvEye, eyeLeft, eyeRight, params));
}

void BodyViewTest::testAnyCornerCounts() {
	vr::BodyViewParams params;
	Vec3f eyeLeft = bvEye + Vec3f(-3.f, 0.f, 0.f);
	Vec3f eyeRight = bvEye + Vec3f(3.f, 0.f, 0.f);
	Vec3f outA = bvEye + Vec3f(0.f, 0.f, 100.f);
	Vec3f outB = bvEye + Vec3f(100.f, 0.f, 0.f);
	CPPUNIT_ASSERT_MESSAGE("one corner within the clearance",
	                       vr::hideFaceNearEye(outA, outB, bvEye + Vec3f(0.f, 0.f, 5.f), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("no corner within the clearance",
	                       !vr::hideFaceNearEye(outA, outB, bvEye + Vec3f(0.f, 100.f, 0.f), eyeLeft, eyeRight, params));
}

void BodyViewTest::testLegsAreNeverHidden() {
	// A wide clearance so that parts well below the eyes are within it: the legs stay drawn anyway
	vr::BodyViewParams params;
	params.clearance = 100.f;
	params.keepBelow = 60.f;
	Vec3f eyeLeft = bvEye + Vec3f(-3.f, 0.f, 0.f);
	Vec3f eyeRight = bvEye + Vec3f(3.f, 0.f, 0.f);
	CPPUNIT_ASSERT_MESSAGE("the chest 30 below the eyes is hidden",
	                       bvHidden(bvEye + Vec3f(0.f, 30.f, 0.f), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("the legs 80 below the eyes, within the clearance, stay drawn",
	                       !bvHidden(bvEye + Vec3f(0.f, 80.f, 0.f), eyeLeft, eyeRight, params));
	CPPUNIT_ASSERT_MESSAGE("the legs 80 below, a little forward, stay drawn",
	                       !bvHidden(bvEye + Vec3f(10.f, 80.f, 10.f), eyeLeft, eyeRight, params));
	// With the default clearance the feet are drawn too
	vr::BodyViewParams defaults;
	CPPUNIT_ASSERT(!bvHidden(bvEye + Vec3f(0.f, defaults.keepBelow + 20.f, 0.f), eyeLeft, eyeRight, defaults));
}
