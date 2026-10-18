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

#include "vr/ArxArmFixTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "math/Angle.h"
#include "vr/ArxArmFix.h"

CppUnit::AutoRegisterSuite<ArxArmFixTest> g_registerArxArmFixTest;

namespace {

using vr::ArmPart;

void armFixCheckFloat(const char * what, float expected, float actual, float tolerance = 0.01f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)
		             + " (tolerance " + std::to_string(tolerance) + ")");
	}
}

glm::quat armFixTurn(const Vec3f & axis, float degrees) {
	return glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
}

//! The first vertex of each ring of the synthetic arm, by the ring's place in it
struct SyntheticArm {
	vr::ArxArmRest rest;
	size_t elbowUpper = 0;  //!< the elbow ring's upper-arm vertices
	size_t elbowForearm = 0;
	size_t forearm35 = 0;
	size_t forearm395 = 0;
	size_t wristGroup44 = 0; //!< the wrist group's vertices in the forearm
	size_t wristGroup48 = 0;
	size_t wristRing = 0;
	size_t handRing = 0;
	size_t body = 0;
};

//! Vertices around the arm's axis (+X) at \a along: \a count of them, the first at \a startDegrees
size_t addRing(vr::ArxArmRest & rest, float along, ArmPart part, size_t count = 6, float startDegrees = 0.f,
               float stepDegrees = 60.f) {
	const size_t first = rest.positions.size();
	for(size_t i = 0; i < count; i++) {
		const float angle = glm::radians(startDegrees + stepDegrees * float(i));
		rest.positions.emplace_back(along, 4.f * std::cos(angle), 4.f * std::sin(angle));
		rest.parts.push_back(part);
	}
	return first;
}

/*!
 * A straight arm along +X built like human_base's (docs/vr/TRAPS.md 2026-10-14): the shoulder at 0, the elbow at 28,
 * the wrist at 58; upper-arm rings at 2 .. 20, the elbow ring mixing three upper-arm vertices just before the joint
 * with three of the forearm just past it, forearm rings at 35 and 39.5, the wrist group's rings in the forearm at 44
 * and 48, the wrist ring at 57.5 and a hand ring at 66; a web face from the upper arm to the wrist group
 */
SyntheticArm syntheticArm() {
	SyntheticArm arm;
	vr::ArxArmRest & rest = arm.rest;
	rest.shoulder = Vec3f(0.f);
	rest.elbow = Vec3f(28.f, 0.f, 0.f);
	rest.wrist = Vec3f(58.f, 0.f, 0.f);
	arm.body = rest.positions.size();
	rest.positions.emplace_back(-5.f, 0.f, 0.f);
	rest.parts.push_back(ArmPart::Body);
	const size_t upper2 = addRing(rest, 2.f, ArmPart::Upper);
	addRing(rest, 8.f, ArmPart::Upper);
	addRing(rest, 14.f, ArmPart::Upper);
	addRing(rest, 20.f, ArmPart::Upper);
	arm.elbowUpper = addRing(rest, 27.8f, ArmPart::Upper, 3, 0.f, 120.f);
	arm.elbowForearm = addRing(rest, 29.f, ArmPart::Forearm, 3, 60.f, 120.f);
	arm.forearm35 = addRing(rest, 35.f, ArmPart::Forearm);
	arm.forearm395 = addRing(rest, 39.5f, ArmPart::Forearm);
	arm.wristGroup44 = addRing(rest, 44.f, ArmPart::Hand);
	arm.wristGroup48 = addRing(rest, 48.f, ArmPart::Hand);
	arm.wristRing = addRing(rest, 57.5f, ArmPart::Hand);
	arm.handRing = addRing(rest, 66.f, ArmPart::Hand);
	rest.faces.push_back({ arm.body, upper2, upper2 + 1 });                            // the shoulder
	rest.faces.push_back({ arm.elbowUpper, arm.wristGroup44, arm.forearm35 });         // the web
	rest.faces.push_back({ arm.forearm395, arm.wristGroup44, arm.wristGroup44 + 1 });  // the band in the forearm
	rest.faces.push_back({ arm.elbowUpper, arm.elbowForearm, arm.elbowUpper + 1 });    // the elbow band
	return arm;
}

//! The engine's skin of the synthetic arm and the classic one
std::vector<ArmPart> engineParts(const SyntheticArm & arm) {
	return arm.rest.parts;
}

} // anonymous namespace

void ArxArmFixTest::testDiagnosisReadsTheArm() {

	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);

	CPPUNIT_ASSERT(diagnosis.valid);
	armFixCheckFloat("upper length", 28.f, diagnosis.upperLength);
	armFixCheckFloat("forearm length", 30.f, diagnosis.forearmLength);
	// The rings from the shoulder to the wrist ring; the hand ring past it is the hand's
	CPPUNIT_ASSERT_EQUAL(size_t(10), diagnosis.rings.size());
	CPPUNIT_ASSERT_EQUAL(size_t(4), diagnosis.elbowRing);
	CPPUNIT_ASSERT_EQUAL(size_t(9), diagnosis.wristRing);
	CPPUNIT_ASSERT_EQUAL(size_t(1), diagnosis.mixedRings);
	CPPUNIT_ASSERT_EQUAL(size_t(3), diagnosis.rings[4].parts[size_t(ArmPart::Upper)]);
	CPPUNIT_ASSERT_EQUAL(size_t(3), diagnosis.rings[4].parts[size_t(ArmPart::Forearm)]);
	// The forearm group from 1 past the elbow to 11.5; the wrist group's vertices before the wrist joint (the two
	// forearm rings and the wrist ring), the farthest back 16 units before it
	CPPUNIT_ASSERT_EQUAL(size_t(15), diagnosis.parts[size_t(ArmPart::Forearm)].vertices);
	armFixCheckFloat("forearm start", 1.f, diagnosis.forearmStart);
	armFixCheckFloat("forearm end", 11.5f, diagnosis.forearmEnd);
	CPPUNIT_ASSERT_EQUAL(size_t(18), diagnosis.handBeforeWrist);
	armFixCheckFloat("hand before the wrist", 14.f, diagnosis.handBeforeWristMax);
	CPPUNIT_ASSERT_EQUAL(size_t(0), diagnosis.upperPastElbow);
	// The faces between two parts of the arm
	CPPUNIT_ASSERT_EQUAL(size_t(1), diagnosis.facesUpperHand);
	CPPUNIT_ASSERT_EQUAL(size_t(1), diagnosis.facesForearmHand);
	CPPUNIT_ASSERT_EQUAL(size_t(1), diagnosis.facesUpperForearm);
	CPPUNIT_ASSERT_EQUAL(size_t(0), diagnosis.facesBodyForearm);
	// Round rings: nothing flat
	CPPUNIT_ASSERT_EQUAL(size_t(0), diagnosis.flatUpperRings);
	CPPUNIT_ASSERT(!vr::diagnosisText(diagnosis).empty());
}

void ArxArmFixTest::testClassicPartsByRing() {

	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	const std::vector<ArmPart> parts = vr::classicArmParts(arm.rest, diagnosis);

	CPPUNIT_ASSERT_EQUAL(arm.rest.parts.size(), parts.size());
	CPPUNIT_ASSERT(parts[arm.body] == ArmPart::Body);
	CPPUNIT_ASSERT(parts[arm.elbowUpper - 1] == ArmPart::Upper);       // the ring before the elbow ring
	// The elbow ring is the forearm's, with the upper arm's vertices in it
	for(size_t i = 0; i < 3; i++) {
		CPPUNIT_ASSERT(parts[arm.elbowUpper + i] == ArmPart::Forearm);
		CPPUNIT_ASSERT(parts[arm.elbowForearm + i] == ArmPart::Forearm);
	}
	// The wrist group's rings in the forearm are the forearm's; the wrist ring and the hand stay the hand's
	for(size_t i = 0; i < 6; i++) {
		CPPUNIT_ASSERT(parts[arm.forearm35 + i] == ArmPart::Forearm);
		CPPUNIT_ASSERT(parts[arm.wristGroup44 + i] == ArmPart::Forearm);
		CPPUNIT_ASSERT(parts[arm.wristGroup48 + i] == ArmPart::Forearm);
		CPPUNIT_ASSERT(parts[arm.wristRing + i] == ArmPart::Hand);
		CPPUNIT_ASSERT(parts[arm.handRing + i] == ArmPart::Hand);
	}
}

void ArxArmFixTest::testStraightArmHasNoCrease() {

	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	const std::array<glm::quat, 3> bones = { quat_identity(), quat_identity(), quat_identity() };
	for(const std::vector<ArmPart> & parts : { engineParts(arm), vr::classicArmParts(arm.rest, diagnosis) }) {
		const vr::ArmBend bend = vr::measureArm(arm.rest, diagnosis, parts, bones);
		CPPUNIT_ASSERT_EQUAL(size_t(0), bend.creases);
		armFixCheckFloat("max bend", 0.f, bend.maxBend, 0.05f);
		armFixCheckFloat("elbow bend", 0.f, bend.elbowBend, 0.05f);
		armFixCheckFloat("twist spread", 0.f, bend.forearmTwistSpread, 0.05f);
	}
}

void ArxArmFixTest::testEngineSkinFoldsTheForearm() {

	// The elbow bent 90 degrees, the wrist 40 more: the mixed elbow ring splits the elbow into two creases, and the
	// wrist group's rings in the forearm fold it in its middle - the second elbow
	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	const glm::quat upper = quat_identity();
	const glm::quat forearm = armFixTurn(Vec3f(0.f, 0.f, 1.f), 90.f);
	const glm::quat hand = forearm * armFixTurn(Vec3f(0.f, 0.f, 1.f), 40.f);
	const vr::ArmBend bend = vr::measureArm(arm.rest, diagnosis, engineParts(arm), { upper, forearm, hand });

	CPPUNIT_ASSERT_EQUAL(size_t(3), bend.creases);
	armFixCheckFloat("max bend", 45.f, bend.maxBend, 0.1f);
	armFixCheckFloat("elbow bend", 90.f, bend.elbowBend, 0.1f);
	CPPUNIT_ASSERT_EQUAL(std::string("3>4 45.0, 4>5 45.0, 6>7 40.0"), bend.bands);
}

void ArxArmFixTest::testClassicSkinFoldsOnce() {

	// The same pose on the classic skin: one band takes the whole elbow, the wrist band (the hand's joint) is not
	// the arm's
	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	const glm::quat upper = quat_identity();
	const glm::quat forearm = armFixTurn(Vec3f(0.f, 0.f, 1.f), 90.f);
	const glm::quat hand = forearm * armFixTurn(Vec3f(0.f, 0.f, 1.f), 40.f);
	const vr::ArmBend bend = vr::measureArm(arm.rest, diagnosis, vr::classicArmParts(arm.rest, diagnosis),
	                                        { upper, forearm, hand });

	CPPUNIT_ASSERT_EQUAL(size_t(1), bend.creases);
	armFixCheckFloat("max bend", 90.f, bend.maxBend, 0.1f);
	CPPUNIT_ASSERT_EQUAL(std::string("3>4 90.0"), bend.bands);
	armFixCheckFloat("twist spread", 0.f, bend.forearmTwistSpread, 0.05f);

	// Below the threshold the elbow is no crease; a threshold is the caller's
	const glm::quat slight = armFixTurn(Vec3f(0.f, 0.f, 1.f), 15.f);
	CPPUNIT_ASSERT_EQUAL(size_t(0), vr::measureArm(arm.rest, diagnosis, vr::classicArmParts(arm.rest, diagnosis),
	                                               { upper, slight, slight }).creases);
	CPPUNIT_ASSERT_EQUAL(size_t(1), vr::measureArm(arm.rest, diagnosis, vr::classicArmParts(arm.rest, diagnosis),
	                                               { upper, slight, slight }, 10.f).creases);
}

void ArxArmFixTest::testRollStaysOffTheClassicForearm() {

	// The hand rolled a quarter turn about the forearm: on the engine's skin the wrist group's rings in the forearm
	// roll with it, on the classic skin the forearm's rings are one piece; a roll is no crease
	const SyntheticArm arm = syntheticArm();
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	const glm::quat hand = armFixTurn(Vec3f(1.f, 0.f, 0.f), 90.f);
	const std::array<glm::quat, 3> bones = { quat_identity(), quat_identity(), hand };

	const vr::ArmBend engine = vr::measureArm(arm.rest, diagnosis, engineParts(arm), bones);
	armFixCheckFloat("engine twist spread", 90.f, engine.forearmTwistSpread, 0.1f);
	CPPUNIT_ASSERT_EQUAL(size_t(0), engine.creases);

	const vr::ArmBend classic = vr::measureArm(arm.rest, diagnosis, vr::classicArmParts(arm.rest, diagnosis), bones);
	armFixCheckFloat("classic twist spread", 0.f, classic.forearmTwistSpread, 0.05f);
	CPPUNIT_ASSERT_EQUAL(size_t(0), classic.creases);
}

void ArxArmFixTest::testSkinOnBone() {

	// The engine's order: scale the offset from the joint, turn it, add the joint
	const Vec3f p = vr::skinOnBone(armFixTurn(Vec3f(0.f, 0.f, 1.f), 90.f), Vec3f(1.f, 2.f, 3.f), Vec3f(2.f, 1.f, 1.f),
	                               Vec3f(1.f, 0.f, 0.f));
	armFixCheckFloat("x", 1.f, p.x, 1e-4f);
	armFixCheckFloat("y", 4.f, p.y, 1e-4f);
	armFixCheckFloat("z", 3.f, p.z, 1e-4f);
}

void ArxArmFixTest::testDegenerateChain() {

	SyntheticArm arm = syntheticArm();
	arm.rest.elbow = arm.rest.shoulder;
	const vr::ArxArmDiagnosis diagnosis = vr::diagnoseArxArm(arm.rest);
	CPPUNIT_ASSERT(!diagnosis.valid);
	CPPUNIT_ASSERT(vr::classicArmParts(arm.rest, diagnosis) == arm.rest.parts);
	CPPUNIT_ASSERT_EQUAL(size_t(0), vr::measureArm(arm.rest, diagnosis, arm.rest.parts,
	                                               { quat_identity(), quat_identity(), quat_identity() }).creases);
}
