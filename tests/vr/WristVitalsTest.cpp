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

#include "vr/WristVitalsTest.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>

#include <glm/glm.hpp>

#include "vr/WristVitals.h"

CppUnit::AutoRegisterSuite<WristVitalsTest> g_registerWristVitalsTest;

namespace {

void wristCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void wristCheckFloat(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

Vec3f middle(const std::array<Vec3f, 4> & quad) {
	return (quad[0] + quad[1] + quad[2] + quad[3]) * 0.25f;
}

//! A left forearm along +x, the palm facing +z, the thumb towards +y (palm = knuckles x thumb), 100 units per meter
vr::WristFrame flatArm() {
	vr::WristFrame frame;
	frame.elbow = Vec3f(0.f, 0.f, 0.f);
	frame.wrist = Vec3f(25.f, 0.f, 0.f);
	frame.palmNormal = Vec3f(0.f, 0.f, 1.f);
	frame.thumb = Vec3f(0.f, 1.f, 0.f);
	return frame;
}

} // anonymous namespace

void WristVitalsTest::testBarsOnTheInnerWrist() {

	const vr::WristVitalsParams params;
	const vr::WristFrame frame = flatArm();
	const vr::WristVitalsLayout layout = vr::layoutWristVitals(frame, Vec3f(21.f, 0.f, 40.f), 1.f, 1.f, params);

	CPPUNIT_ASSERT(layout.valid);
	// 4 cm back from the wrist joint, 3 cm out on the palm side
	wristCheckVector("center", Vec3f(21.f, 0.f, 3.f), layout.center);
	wristCheckVector("normal", Vec3f(0.f, 0.f, 1.f), layout.normal);
	wristCheckVector("across", Vec3f(0.f, 1.f, 0.f), layout.across);

	// Health towards the elbow, mana towards the hand, 0.6 cm apart; 1 cm wide, 5 cm long, flat on the skin
	wristCheckVector("health bar", Vec3f(21.f - 0.8f, 0.f, 3.f), middle(layout.health.back));
	wristCheckVector("mana bar", Vec3f(21.f + 0.8f, 0.f, 3.f), middle(layout.mana.back));
	wristCheckFloat("bar width", 1.f, glm::distance(layout.health.back[0], layout.health.back[1]));
	wristCheckFloat("bar length", 5.f, glm::distance(layout.health.back[1], layout.health.back[2]));
	for(const Vec3f & corner : layout.mana.back) {
		wristCheckFloat("on the skin", 3.f, corner.z);
	}

	// Seen straight from the palm side: fully visible
	wristCheckFloat("facing", 0.f, layout.facingDeg, 0.01f);
	wristCheckFloat("alpha", 1.f, layout.alpha);
}

void WristVitalsTest::testFillFollowsTheValue() {

	const vr::WristVitalsLayout layout = vr::layoutWristVitals(flatArm(), Vec3f(21.f, 0.f, 40.f), 0.5f, 0.2f);

	// The empty end is away from the thumb (-y), the fill grows towards the thumb (+y)
	wristCheckFloat("health empty end", -2.5f, layout.health.fill[0].y);
	wristCheckFloat("health at half", 0.f, layout.health.fill[2].y);
	wristCheckFloat("mana at a fifth", -2.5f + 1.f, layout.mana.fill[2].y);
	wristCheckFloat("health backing full end", 2.5f, layout.health.back[2].y);

	// Values beyond the range are clamped, NaN is empty
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const vr::WristVitalsLayout clamped = vr::layoutWristVitals(flatArm(), Vec3f(21.f, 0.f, 40.f), 1.7f, nan);
	wristCheckFloat("over full", 1.f, clamped.health.fraction);
	wristCheckFloat("nan", 0.f, clamped.mana.fraction);
	const vr::WristVitalsLayout negative = vr::layoutWristVitals(flatArm(), Vec3f(21.f, 0.f, 40.f), -0.3f, 0.f);
	wristCheckFloat("negative", 0.f, negative.health.fraction);
}

void WristVitalsTest::testWatchPoseReadsLikeTheFlatHud() {

	// The eye at the origin looking forward (+z), right +x, up -y. The left forearm across the face,
	// elbow on the left, the inner wrist turned to the eyes, the thumb up
	vr::WristFrame frame;
	frame.elbow = Vec3f(-30.f, 0.f, 35.f);
	frame.wrist = Vec3f(-5.f, 0.f, 35.f);
	frame.palmNormal = Vec3f(0.f, 0.f, -1.f);
	frame.thumb = Vec3f(0.f, -1.f, 0.f);
	const vr::WristVitalsLayout layout = vr::layoutWristVitals(frame, Vec3f(0.f), 0.5f, 0.5f);

	CPPUNIT_ASSERT(layout.valid);
	// Red on the left, blue on the right, as on the flat HUD
	CPPUNIT_ASSERT(middle(layout.health.back).x < middle(layout.mana.back).x);
	// Both stand up: they fill upwards on the screen (towards -y)
	CPPUNIT_ASSERT(layout.health.fill[2].y < layout.health.fill[1].y);
	wristCheckFloat("upright", 0.f, layout.health.fill[2].x - layout.health.fill[1].x);
	// Between the eyes and the wrist joint, facing the eyes
	CPPUNIT_ASSERT(layout.center.z < frame.wrist.z);
	CPPUNIT_ASSERT(layout.facingDeg < 20.f);
	wristCheckFloat("alpha", 1.f, layout.alpha);
}

void WristVitalsTest::testFadesAsTheWristTurnsAway() {

	const vr::WristVitalsParams params;
	wristCheckFloat("0", 1.f, vr::wristVitalsAlpha(0.f, params));
	wristCheckFloat("50", 1.f, vr::wristVitalsAlpha(50.f, params));
	wristCheckFloat("65", 0.5f, vr::wristVitalsAlpha(65.f, params));
	wristCheckFloat("80", 0.f, vr::wristVitalsAlpha(80.f, params));
	wristCheckFloat("180", 0.f, vr::wristVitalsAlpha(180.f, params));
	wristCheckFloat("nan", 0.f, vr::wristVitalsAlpha(std::numeric_limits<float>::quiet_NaN(), params));

	// The eye 65 degrees off the inner wrist's normal (about the forearm), then behind the wrist
	const vr::WristFrame frame = flatArm();
	const Vec3f center(21.f, 0.f, 3.f);
	const float a = glm::radians(65.f);
	const Vec3f tiltedEye = center + Vec3f(0.f, -std::sin(a), std::cos(a)) * 40.f;
	const vr::WristVitalsLayout tilted = vr::layoutWristVitals(frame, tiltedEye, 1.f, 1.f);
	wristCheckFloat("tilted facing", 65.f, tilted.facingDeg, 0.01f);
	wristCheckFloat("tilted alpha", 0.5f, tilted.alpha);
	const vr::WristVitalsLayout behind = vr::layoutWristVitals(frame, Vec3f(21.f, 0.f, -40.f), 1.f, 1.f);
	wristCheckFloat("behind facing", 180.f, behind.facingDeg, 0.01f);
	wristCheckFloat("behind alpha", 0.f, behind.alpha);
}

void WristVitalsTest::testDegenerateArms() {

	vr::WristFrame folded = flatArm();
	folded.elbow = folded.wrist;
	CPPUNIT_ASSERT(!vr::layoutWristVitals(folded, Vec3f(0.f, 0.f, 40.f), 1.f, 1.f).valid);

	vr::WristFrame palmAlong = flatArm();
	palmAlong.palmNormal = Vec3f(1.f, 0.f, 0.f);
	CPPUNIT_ASSERT(!vr::layoutWristVitals(palmAlong, Vec3f(0.f, 0.f, 40.f), 1.f, 1.f).valid);

	// A palm normal not square to the forearm is squared; a thumb along the normal falls back to where
	// the thumb of a hand in line with the forearm is (palm = knuckles x thumb)
	vr::WristFrame skewed = flatArm();
	skewed.palmNormal = glm::normalize(Vec3f(0.5f, 0.f, 1.f));
	skewed.thumb = Vec3f(0.f, 0.f, 1.f);
	const vr::WristVitalsLayout layout = vr::layoutWristVitals(skewed, Vec3f(21.f, 0.f, 40.f), 1.f, 1.f);
	CPPUNIT_ASSERT(layout.valid);
	wristCheckVector("squared normal", Vec3f(0.f, 0.f, 1.f), layout.normal);
	wristCheckVector("fallback thumb side", Vec3f(0.f, 1.f, 0.f), layout.across);
	wristCheckFloat("across square to the forearm", 0.f, glm::dot(layout.across, layout.along));
	wristCheckFloat("across square to the normal", 0.f, glm::dot(layout.across, layout.normal));
	wristCheckFloat("across unit", 1.f, glm::length(layout.across));
}
