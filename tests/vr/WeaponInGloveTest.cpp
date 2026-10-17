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

#include "vr/WeaponInGloveTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/WeaponGrip.h"

CppUnit::AutoRegisterSuite<WeaponInGloveTest> g_registerWeaponInGloveTest;

namespace {

// Names unique to this file (prefix wig): the unity build sees every test file's helpers (TRAPS.md)

void wigCheck(const char * what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! The rolled edge against the knuckles' projection onto the plane perpendicular to the blade, degrees
float wigEdgeToKnuckles(const Vec3f & blade, const Vec3f & edge, const Vec3f & knuckles, const glm::quat & roll) {
	const Vec3f rolled = roll * edge;
	const Vec3f projected = knuckles - blade * glm::dot(knuckles, blade);
	return vr::angleDegrees(rolled, projected);
}

} // anonymous namespace

void WeaponInGloveTest::testSwordAndDaggerAtHandleCentre() {
	for(float length : { 10.f, 14.f, 22.f }) {
		wigCheck("sword", 0.5f * length, vr::weaponAnchorOffset(vr::WeaponClass::Sword, length, 8.f), 0.01f);
		wigCheck("dagger", 0.5f * length, vr::weaponAnchorOffset(vr::WeaponClass::Dagger, length, 8.f), 0.01f);
	}
}

void WeaponInGloveTest::testAxeAndMaceNearTheHandleEnd() {
	for(float length : { 20.f, 30.f, 45.f }) {
		for(vr::WeaponClass weaponClass : { vr::WeaponClass::Axe, vr::WeaponClass::Mace }) {
			const float fraction = vr::weaponAnchorOffset(weaponClass, length, 8.f) / length;
			CPPUNIT_ASSERT_MESSAGE("axe / mace anchor at least 15% from the handle end", fraction >= 0.15f - 0.001f);
			CPPUNIT_ASSERT_MESSAGE("axe / mace anchor at most 25% from the handle end", fraction <= 0.25f + 0.001f);
		}
	}
	// One table value for both classes
	wigCheck("axe = mace", vr::weaponAnchorOffset(vr::WeaponClass::Axe, 30.f, 8.f),
	         vr::weaponAnchorOffset(vr::WeaponClass::Mace, 30.f, 8.f), 0.001f);
}

void WeaponInGloveTest::testTwoHandedAHandWidthAbovePommel() {
	for(float width : { 7.f, 9.f, 11.f }) {
		wigCheck("two-handed", width, vr::weaponAnchorOffset(vr::WeaponClass::TwoHanded, 40.f, width), 0.01f);
	}
	// A handle shorter than the hand stays on the handle
	const float shortHandle = vr::weaponAnchorOffset(vr::WeaponClass::TwoHanded, 6.f, 9.f);
	CPPUNIT_ASSERT(shortHandle > 0.f && shortHandle <= 6.f);
}

void WeaponInGloveTest::testEveryAnchorInsideTheHandle() {
	const vr::WeaponClass classes[] = {
		vr::WeaponClass::Sword, vr::WeaponClass::Dagger, vr::WeaponClass::Axe, vr::WeaponClass::Mace,
		vr::WeaponClass::TwoHanded,
	};
	for(vr::WeaponClass weaponClass : classes) {
		for(float length : { 5.f, 12.f, 40.f }) {
			const float offset = vr::weaponAnchorOffset(weaponClass, length, 9.f);
			CPPUNIT_ASSERT(offset > 0.f);
			CPPUNIT_ASSERT(offset <= length);
		}
	}
}

void WeaponInGloveTest::testEdgeTowardKnucklesBothHands() {
	// The glove's frame, the right hand: blade along the handle, the knuckles on one side; the left hand is its
	// mirror (x flipped). The edge starts at several rolls about the blade: every one ends under 5 degrees.
	const Vec3f blades[] = { Vec3f(0.f, 0.f, -1.f), Vec3f(0.3f, -0.2f, -0.9f), Vec3f(0.f, 1.f, 0.f) };
	const Vec3f knuckleDirs[] = { Vec3f(0.f, 1.f, 0.f), Vec3f(0.5f, 0.8f, 0.1f), Vec3f(1.f, 0.f, 0.f) };
	for(int hand = 0; hand < 2; hand++) {
		const float mirror = hand == 0 ? 1.f : -1.f;
		for(const Vec3f & rawBlade : blades) {
			const Vec3f blade = glm::normalize(Vec3f(rawBlade.x * mirror, rawBlade.y, rawBlade.z));
			for(const Vec3f & rawKnuckles : knuckleDirs) {
				const Vec3f knuckles(rawKnuckles.x * mirror, rawKnuckles.y, rawKnuckles.z);
				if(std::abs(glm::dot(glm::normalize(knuckles), blade)) > 0.95f) {
					continue;
				}
				const Vec3f side = glm::normalize(glm::cross(blade, Vec3f(0.37f, 0.61f, 0.7f)));
				for(int roll = 0; roll < 360; roll += 45) {
					const Vec3f edge = glm::angleAxis(glm::radians(float(roll)), blade) * side;
					const glm::quat q = vr::bladeRollToKnuckles(blade, edge, knuckles);
					CPPUNIT_ASSERT_MESSAGE("the blade stays on its axis", vr::angleDegrees(q * blade, blade) < 0.1f);
					const float angle = wigEdgeToKnuckles(blade, edge, knuckles, q);
					CPPUNIT_ASSERT_MESSAGE("edge axis vs knuckle direction under 5 degrees (hand "
					                       + std::to_string(hand) + ", roll " + std::to_string(roll) + "): "
					                       + std::to_string(angle), angle < 5.f);
				}
			}
		}
	}
}

void WeaponInGloveTest::testEdgeAlreadyAwayFromKnuckles() {
	// The negative case: an edge pointing exactly away from the knuckles (180 degrees) is turned, not left
	const Vec3f blade(0.f, 0.f, -1.f);
	const Vec3f knuckles(0.f, 1.f, 0.f);
	const Vec3f edgeAway(0.f, -1.f, 0.f);
	CPPUNIT_ASSERT(vr::angleDegrees(edgeAway, knuckles) > 170.f);
	const glm::quat q = vr::bladeRollToKnuckles(blade, edgeAway, knuckles);
	CPPUNIT_ASSERT(wigEdgeToKnuckles(blade, edgeAway, knuckles, q) < 5.f);
	// ... and an edge already on the knuckles is kept
	const glm::quat kept = vr::bladeRollToKnuckles(blade, knuckles, knuckles);
	CPPUNIT_ASSERT(wigEdgeToKnuckles(blade, knuckles, knuckles, kept) < 1.f);
}

void WeaponInGloveTest::testRollWithKnucklesAlongTheBlade() {
	const Vec3f blade(0.f, 0.f, -1.f);
	const glm::quat q = vr::bladeRollToKnuckles(blade, Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, -1.f));
	const Vec3f turned = q * Vec3f(1.f, 0.f, 0.f);
	CPPUNIT_ASSERT(std::isfinite(turned.x) && std::isfinite(turned.y) && std::isfinite(turned.z));
	CPPUNIT_ASSERT(vr::angleDegrees(q * blade, blade) < 0.1f);
}

void WeaponInGloveTest::testSwordTiltVariants() {
	const Vec3f handle(0.f, 0.f, -1.f);
	const Vec3f thumb(0.f, 1.f, 0.f);
	// 0: identity, the blade stays on the handle
	CPPUNIT_ASSERT(vr::angleDegrees(vr::swordTiltRotation(0.f, handle, thumb) * handle, handle) < 0.01f);
	for(float degrees : { -15.f, -30.f }) {
		const Vec3f tilted = vr::swordTiltRotation(degrees, handle, thumb) * handle;
		wigCheck("tilt angle", std::abs(degrees), vr::angleDegrees(tilted, handle), 0.05f);
		// Towards the thumb, inside the handle-thumb plane
		CPPUNIT_ASSERT_MESSAGE("the tip tilts towards the thumb", glm::dot(tilted, thumb) > 0.f);
		wigCheck("in the handle-thumb plane", 0.f, glm::dot(tilted, glm::cross(handle, thumb)), 0.001f);
		// The other hand: the thumb on the other side
		const Vec3f otherThumb(0.f, -1.f, 0.f);
		const Vec3f otherTilted = vr::swordTiltRotation(degrees, handle, otherThumb) * handle;
		CPPUNIT_ASSERT_MESSAGE("towards the other hand's thumb", glm::dot(otherTilted, otherThumb) > 0.f);
		wigCheck("other hand angle", std::abs(degrees), vr::angleDegrees(otherTilted, handle), 0.05f);
	}
	// -30 is twice -15
	wigCheck("30 vs 15", 2.f * vr::angleDegrees(vr::swordTiltRotation(-15.f, handle, thumb) * handle, handle),
	         vr::angleDegrees(vr::swordTiltRotation(-30.f, handle, thumb) * handle, handle), 0.1f);
}

void WeaponInGloveTest::testSwordTiltSnapsToVariants() {
	wigCheck("0", 0.f, vr::swordTiltVariant(0.f), 0.001f);
	wigCheck("-15", -15.f, vr::swordTiltVariant(-15.f), 0.001f);
	wigCheck("-30", -30.f, vr::swordTiltVariant(-30.f), 0.001f);
	wigCheck("-14", -15.f, vr::swordTiltVariant(-14.f), 0.001f);
	wigCheck("-60 (beyond the table)", -30.f, vr::swordTiltVariant(-60.f), 0.001f);
	wigCheck("+20 (wrong side)", 0.f, vr::swordTiltVariant(20.f), 0.001f);
}
