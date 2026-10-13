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

#include "vr/MeleeChargeCueTest.h"

#include <cmath>
#include <limits>
#include <string>

#include <glm/glm.hpp>

#include "vr/MeleeChargeCue.h"
#include "vr/MeleeSwing.h"

CppUnit::AutoRegisterSuite<MeleeChargeCueTest> g_registerMeleeChargeCueTest;

namespace {

void cueCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void cueCheckFloat(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

/*!
 * A right forearm along +x, the palm facing the floor (+y is down in the engine), so the back of the hand is
 * up; 100 units per meter. The ring sits 2 cm towards the elbow and 5 cm above the wrist: at (23, -5, 0)
 */
vr::WristFrame rightArm() {
	vr::WristFrame frame;
	frame.elbow = Vec3f(0.f, 0.f, 0.f);
	frame.wrist = Vec3f(25.f, 0.f, 0.f);
	frame.palmNormal = Vec3f(0.f, 1.f, 0.f);
	frame.thumb = Vec3f(0.f, 0.f, 1.f);
	return frame;
}

const Vec3f RingCenter = Vec3f(23.f, -5.f, 0.f);
//! In front of the ring, looking along +z: the eye's right is +x, its up -y
const Vec3f FrontEye = Vec3f(23.f, -5.f, -40.f);

} // anonymous namespace

void MeleeChargeCueTest::testNames() {

	const vr::ChargeCue all[] = { vr::ChargeCue::Off, vr::ChargeCue::Glow, vr::ChargeCue::Wrist, vr::ChargeCue::Hud,
	                              vr::ChargeCue::Blade, vr::ChargeCue::All };
	for(vr::ChargeCue cue : all) {
		vr::ChargeCue parsed = vr::ChargeCue::Off;
		CPPUNIT_ASSERT(vr::parseChargeCue(vr::chargeCueName(cue), parsed));
		CPPUNIT_ASSERT(parsed == cue);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("wrist"), std::string(vr::chargeCueName(vr::ChargeCue::Wrist)));

	vr::ChargeCue untouched = vr::ChargeCue::Blade;
	CPPUNIT_ASSERT(!vr::parseChargeCue("ring", untouched));
	CPPUNIT_ASSERT(!vr::parseChargeCue("", untouched));
	CPPUNIT_ASSERT(!vr::parseChargeCue("Glow", untouched));
	CPPUNIT_ASSERT(untouched == vr::ChargeCue::Blade);
}

void MeleeChargeCueTest::testWhichVariantsShow() {

	const vr::ChargeCue variants[] = { vr::ChargeCue::Glow, vr::ChargeCue::Wrist, vr::ChargeCue::Hud,
	                                   vr::ChargeCue::Blade };
	for(vr::ChargeCue mode : variants) {
		for(vr::ChargeCue variant : variants) {
			CPPUNIT_ASSERT_EQUAL(mode == variant, vr::chargeCueShows(mode, variant));
		}
		CPPUNIT_ASSERT(vr::chargeCueShows(vr::ChargeCue::All, mode));
		CPPUNIT_ASSERT(!vr::chargeCueShows(vr::ChargeCue::Off, mode));
	}
	// Off and All are settings, not variants
	CPPUNIT_ASSERT(!vr::chargeCueShows(vr::ChargeCue::All, vr::ChargeCue::All));
	CPPUNIT_ASSERT(!vr::chargeCueShows(vr::ChargeCue::All, vr::ChargeCue::Off));
}

void MeleeChargeCueTest::testFillFollowsTheEngineCharge() {

	const float full = 1.5f;
	cueCheckFloat("not charging", 0.f, vr::chargeFill(0.f, full));
	cueCheckFloat("half", 0.5f, vr::chargeFill(0.75f, full));
	cueCheckFloat("full", 1.f, vr::chargeFill(1.5f, full));
	cueCheckFloat("held past full", 1.f, vr::chargeFill(4.f, full));
	cueCheckFloat("starts empty, below the engine's floor of 0.1", 0.02f, vr::chargeFill(0.03f, full));

	// The engine's charge (StrikeCharge, the trigger held against Full_AimTime): the fill is its ratio above
	// its floor, grows with it, and is empty again when the strike spends it and after the release
	vr::StrikeCharge charge;
	float before = 0.f;
	for(int frame = 0; frame < 120; frame++) {
		charge.update(true, 1.f / 60.f);
		const float fill = vr::chargeFill(charge.seconds(), full);
		CPPUNIT_ASSERT(fill >= before);
		if(charge.ratio(full) > 0.1f) {
			cueCheckFloat("the engine's ratio", charge.ratio(full), fill);
		}
		before = fill;
	}
	cueCheckFloat("full after 2 s", 1.f, vr::chargeFill(charge.seconds(), full));
	charge.spend();
	cueCheckFloat("spent by the strike", 0.f, vr::chargeFill(charge.seconds(), full));
	charge.update(true, 1.f / 60.f);
	cueCheckFloat("still held after the strike", 0.f, vr::chargeFill(charge.seconds(), full));
	charge.update(false, 1.f / 60.f);
	charge.update(true, 0.3f);
	cueCheckFloat("pressed again", 0.2f, vr::chargeFill(charge.seconds(), full));
	charge.update(false, 1.f / 60.f);
	cueCheckFloat("released", 0.f, vr::chargeFill(charge.seconds(), full));

	cueCheckFloat("no full time", 0.f, vr::chargeFill(1.f, 0.f));
	cueCheckFloat("NaN", 0.f, vr::chargeFill(std::numeric_limits<float>::quiet_NaN(), full));
}

void MeleeChargeCueTest::testRingAtTheWristFacingTheEye() {

	const vr::ChargeCueParams params;
	const vr::ChargeRingLayout ring = vr::layoutChargeRing(rightArm(), FrontEye, 0.f, params);

	CPPUNIT_ASSERT(ring.valid);
	cueCheckVector("center: above the back of the wrist", RingCenter, ring.center);
	cueCheckVector("turned to the eye", Vec3f(0.f, 0.f, -1.f), ring.normal);
	cueCheckVector("12 o'clock is up", Vec3f(0.f, -1.f, 0.f), ring.up);
	cueCheckVector("3 o'clock is the eye's right", Vec3f(1.f, 0.f, 0.f), ring.right);

	// The whole ring as the backing, 1.6 to 2.4 cm from the center, flat towards the eye; nothing filled
	CPPUNIT_ASSERT_EQUAL(size_t(36), ring.track.size());
	CPPUNIT_ASSERT(ring.filled.empty());
	for(const vr::CueQuad & quad : ring.track) {
		cueCheckFloat("inner", 1.6f, glm::distance(RingCenter, quad[0]));
		cueCheckFloat("outer", 2.4f, glm::distance(RingCenter, quad[1]));
		for(const Vec3f & corner : quad) {
			cueCheckFloat("flat", 0.f, corner.z);
		}
	}

	// Seen from above the ring still faces the eye; 12 o'clock is then towards the hand
	const vr::ChargeRingLayout above = vr::layoutChargeRing(rightArm(), RingCenter + Vec3f(0.f, -40.f, 0.f), 0.5f,
	                                                        params);
	CPPUNIT_ASSERT(above.valid);
	cueCheckVector("from above: turned up", Vec3f(0.f, -1.f, 0.f), above.normal);
	cueCheckVector("from above: 12 o'clock towards the hand", Vec3f(1.f, 0.f, 0.f), above.up);
}

void MeleeChargeCueTest::testRingFillsClockwise() {

	const vr::ChargeCueParams params;

	// A quarter: 12 to 3 o'clock, in 10 degree segments
	const vr::ChargeRingLayout quarter = vr::layoutChargeRing(rightArm(), FrontEye, 0.25f, params);
	CPPUNIT_ASSERT(quarter.valid);
	cueCheckFloat("quarter", 90.f, quarter.filledDeg);
	CPPUNIT_ASSERT_EQUAL(size_t(9), quarter.filled.size());
	cueCheckVector("starts at 12 o'clock", RingCenter + Vec3f(0.f, -1.6f, 0.f), quarter.filled.front()[0]);
	cueCheckVector("ends at 3 o'clock", RingCenter + Vec3f(2.4f, 0.f, 0.f), quarter.filled.back()[2]);

	// 55 %: the last segment is cut where the fill ends (198 degrees)
	const vr::ChargeRingLayout part = vr::layoutChargeRing(rightArm(), FrontEye, 0.55f, params);
	CPPUNIT_ASSERT_EQUAL(size_t(20), part.filled.size());
	const float end = glm::radians(198.f);
	cueCheckVector("cut at 198 degrees", RingCenter + Vec3f(std::sin(end), -std::cos(end), 0.f) * 2.4f,
	               part.filled.back()[2]);

	// Full: all the way round, back at 12 o'clock; past full is full
	const vr::ChargeRingLayout full = vr::layoutChargeRing(rightArm(), FrontEye, 1.7f, params);
	cueCheckFloat("full", 360.f, full.filledDeg);
	cueCheckFloat("clamped", 1.f, full.fill);
	CPPUNIT_ASSERT_EQUAL(size_t(36), full.filled.size());
	cueCheckVector("back at 12 o'clock", RingCenter + Vec3f(0.f, -2.4f, 0.f), full.filled.back()[2]);
}

void MeleeChargeCueTest::testBladeStrip() {

	const vr::ChargeCueParams params;
	// A blade 80 units along +z, seen from above its middle
	const Vec3f hilt(0.f, 0.f, 0.f);
	const Vec3f tip(0.f, 0.f, 80.f);
	const Vec3f eye(0.f, -50.f, 40.f);

	const vr::BladeStripLayout half = vr::layoutBladeStrip(hilt, tip, eye, 0.5f, params);
	CPPUNIT_ASSERT(half.valid);
	cueCheckVector("ends half way", Vec3f(0.f, 0.f, 40.f), half.end);
	// 1.2 cm wide across the blade as the eye sees it, 1.5 cm in front of it
	cueCheckVector("hilt, one side", Vec3f(-0.6f, -1.5f, 0.f), half.strip[0]);
	cueCheckVector("hilt, other side", Vec3f(0.6f, -1.5f, 0.f), half.strip[1]);
	cueCheckVector("end, other side", Vec3f(0.6f, -1.5f, 40.f), half.strip[2]);
	cueCheckVector("end, one side", Vec3f(-0.6f, -1.5f, 40.f), half.strip[3]);

	const vr::BladeStripLayout full = vr::layoutBladeStrip(hilt, tip, eye, 1.f, params);
	cueCheckVector("full: to the tip", tip, full.end);
	const vr::BladeStripLayout empty = vr::layoutBladeStrip(hilt, tip, eye, 0.f, params);
	cueCheckVector("empty: at the hilt", hilt, empty.end);
	cueCheckFloat("empty strip has no length", 0.f, glm::distance(empty.strip[0], empty.strip[3]));
}

void MeleeChargeCueTest::testGlow() {

	const vr::ChargeCueParams params;
	cueCheckVector("none uncharged", Vec3f(0.f), vr::chargeGlow(0.f, params));
	cueCheckVector("half", params.glowFull * 0.5f, vr::chargeGlow(0.5f, params));
	cueCheckVector("full", params.glowFull, vr::chargeGlow(1.f, params));
	cueCheckVector("past full", params.glowFull, vr::chargeGlow(3.f, params));
	// Warm: more red than green, more green than blue, and visible (a light term of 0 .. 255)
	CPPUNIT_ASSERT(params.glowFull.x > params.glowFull.y && params.glowFull.y > params.glowFull.z);
	CPPUNIT_ASSERT(params.glowFull.x > 100.f && params.glowFull.x <= 255.f);
}

void MeleeChargeCueTest::testDegenerateInput() {

	const vr::ChargeCueParams params;

	vr::WristFrame noForearm = rightArm();
	noForearm.elbow = noForearm.wrist;
	CPPUNIT_ASSERT(!vr::layoutChargeRing(noForearm, FrontEye, 0.5f, params).valid);

	vr::WristFrame palmAlongArm = rightArm();
	palmAlongArm.palmNormal = Vec3f(1.f, 0.f, 0.f);
	CPPUNIT_ASSERT(!vr::layoutChargeRing(palmAlongArm, FrontEye, 0.5f, params).valid);

	CPPUNIT_ASSERT(!vr::layoutChargeRing(rightArm(), RingCenter, 0.5f, params).valid);

	const Vec3f hilt(0.f, 0.f, 0.f);
	const Vec3f tip(0.f, 0.f, 80.f);
	CPPUNIT_ASSERT(!vr::layoutBladeStrip(hilt, hilt, Vec3f(0.f, -50.f, 0.f), 0.5f, params).valid);
	CPPUNIT_ASSERT(!vr::layoutBladeStrip(hilt, tip, Vec3f(0.f, 0.f, 200.f), 0.5f, params).valid);
}
