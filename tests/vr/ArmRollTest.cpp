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

#include "vr/ArmRollTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/ArmIK.h"
#include "vr/ArmRoll.h"

CppUnit::AutoRegisterSuite<ArmRollTest> g_registerArmRollTest;

namespace {

void rollCheckFloat(const char * what, float expected, float actual, float tolerance = 0.01f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual)
		             + " (tolerance " + std::to_string(tolerance) + ")");
	}
}

void rollCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

//! Two rotations are the same when they turn three independent vectors the same way
void rollCheckRotation(const char * what, const glm::quat & expected, const glm::quat & actual, float tolerance = 0.002f) {
	const Vec3f axes[] = { Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f), Vec3f(0.f, 0.f, 1.f) };
	for(const Vec3f & axis : axes) {
		rollCheckVector(what, expected * axis, actual * axis, tolerance);
	}
}

glm::quat rollOf(const Vec3f & axis, float degrees) {
	return glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
}

//! A bent rest arm: the upper arm hangs down (engine y is down), the forearm goes down and forward
vr::ArmRest bentRest() {
	vr::ArmRest rest;
	rest.upper = Vec3f(0.f, 28.f, 0.f);
	rest.forearm = Vec3f(0.f, 20.f, 22.f);
	rest.thumb = Vec3f(0.f, 0.f, 1.f); // the thumb ahead: not used while the triangle bends
	return rest;
}

//! The angle (degrees) between the parts of two vectors across \a axis
float angleAcross(const Vec3f & a, const Vec3f & b, const Vec3f & axis) {
	const Vec3f n = glm::normalize(axis);
	const Vec3f pa = glm::normalize(a - glm::dot(a, n) * n);
	const Vec3f pb = glm::normalize(b - glm::dot(b, n) * n);
	return glm::degrees(std::acos(glm::clamp(glm::dot(pa, pb), -1.f, 1.f)));
}

} // anonymous namespace

void ArmRollTest::testTwistAboutAxis() {

	const Vec3f axis = glm::normalize(Vec3f(1.f, 2.f, -0.5f));
	const Vec3f perpendicular = glm::normalize(glm::cross(axis, Vec3f(0.f, 0.f, 1.f)));

	// A roll about the axis is its own twist, whichever sign of the quaternion represents it
	const float rolls[] = { 0.f, 30.f, -75.f, 120.f, 179.f, -179.f };
	for(float roll : rolls) {
		const glm::quat q = rollOf(axis, roll);
		rollCheckFloat("twist of a roll", roll, vr::twistAboutAxis(q, axis), 0.01f);
		const glm::quat negated(-q.w, -q.x, -q.y, -q.z);
		rollCheckFloat("twist of the negated quaternion", roll, vr::twistAboutAxis(negated, axis), 0.01f);
	}

	// A swing about a perpendicular axis has no twist
	rollCheckFloat("swing has no twist", 0.f, vr::twistAboutAxis(rollOf(perpendicular, 60.f), axis), 0.01f);

	// A roll after a swing keeps its twist; the swing first, then the roll, read in the frame of the roll
	const glm::quat composed = rollOf(axis, 40.f) * rollOf(perpendicular, 60.f);
	rollCheckFloat("roll after swing", 40.f, vr::twistAboutAxis(composed, axis), 0.01f);

	// The identity has none
	rollCheckFloat("identity", 0.f, vr::twistAboutAxis(quat_identity(), axis), 0.0001f);
}

void ArmRollTest::testUnwrapRollKeepsTheSide() {

	vr::ArmRollState state;

	// Well inside a half turn nothing happens
	rollCheckFloat("0", 0.f, vr::unwrapRoll(0.f, 45.f, state));
	rollCheckFloat("100", 100.f, vr::unwrapRoll(100.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(0, state.overflow);

	// Approaching +180 from below arms the positive side; crossing it continues past 180
	rollCheckFloat("170", 170.f, vr::unwrapRoll(170.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(1, state.overflow);
	rollCheckFloat("-170 continues as 190", 190.f, vr::unwrapRoll(-170.f, 45.f, state));
	rollCheckFloat("-140 continues as 220", 220.f, vr::unwrapRoll(-140.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(1, state.overflow);

	// Past the overflow the roll flips to the complement and the side is forgotten
	rollCheckFloat("-130 flips", -130.f, vr::unwrapRoll(-130.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(0, state.overflow);

	// Coming back into the zone from the negative side arms that side: no jump back to 220
	rollCheckFloat("-140 stays", -140.f, vr::unwrapRoll(-140.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(-1, state.overflow);
	rollCheckFloat("170 continues as -190", -190.f, vr::unwrapRoll(170.f, 45.f, state));
	rollCheckFloat("130 is back inside", 130.f, vr::unwrapRoll(130.f, 45.f, state));
	CPPUNIT_ASSERT_EQUAL(0, state.overflow);

	// No overflow: no hysteresis, the measured roll is taken as is
	vr::ArmRollState plain;
	rollCheckFloat("170 plain", 170.f, vr::unwrapRoll(170.f, 0.f, plain));
	rollCheckFloat("-170 plain", -170.f, vr::unwrapRoll(-170.f, 0.f, plain));
	CPPUNIT_ASSERT_EQUAL(0, plain.overflow);
}

void ArmRollTest::testRestElbowDirection() {

	// A bent rest arm with no back known: the elbow's side across the shoulder-wrist line, a unit vector
	const char * source = "";
	const vr::ArmRest bent = bentRest();
	const Vec3f e = vr::restElbowDirection(bent, &source);
	CPPUNIT_ASSERT_EQUAL(std::string("the rest triangle"), std::string(source));
	rollCheckFloat("unit", 1.f, glm::length(e));
	rollCheckFloat("across the line", 0.f, glm::dot(e, glm::normalize(bent.upper + bent.forearm)), 0.001f);
	// the elbow (the upper arm's end) lies on that side of the line
	CPPUNIT_ASSERT(glm::dot(e, bent.upper) > 1.f);

	// human_base's right arm as the model has it (the right arm along -x, the model faces -z): a 10 degree
	// bend with the elbow on +z - the model's back - and the thumb forward, as a relaxed T-pose has them
	vr::ArmRest human;
	human.upper = Vec3f(-27.5f, 0.f, 2.5f);
	human.forearm = Vec3f(-30.4f, -0.8f, -2.4f);
	human.thumb = Vec3f(-0.11f, 0.11f, -0.99f);
	const Vec3f back = vr::restElbowDirection(human, &source);
	CPPUNIT_ASSERT_EQUAL(std::string("the rest triangle"), std::string(source));
	rollCheckFloat("unit", 1.f, glm::length(back));
	CPPUNIT_ASSERT(back.z > 0.9f);
	CPPUNIT_ASSERT_MESSAGE("the elbow is opposite the thumb here too", glm::dot(back, human.thumb) < -0.9f);
	rollCheckFloat("across the line", 0.f, glm::dot(back, glm::normalize(human.upper + human.forearm)), 0.001f);

	// A straight rest arm along z with a thumb: opposite the thumb
	vr::ArmRest along;
	along.upper = Vec3f(0.f, 0.f, -28.f);
	along.forearm = Vec3f(0.f, 0.f, -30.f);
	along.thumb = Vec3f(1.f, 0.f, 0.f);
	rollCheckVector("opposite the thumb", Vec3f(-1.f, 0.f, 0.f), vr::restElbowDirection(along, &source));
	CPPUNIT_ASSERT_EQUAL(std::string("opposite the thumb"), std::string(source));

	// A straight rest arm, nothing else known: the elbow is opposite the thumb
	vr::ArmRest tpose;
	tpose.upper = Vec3f(28.f, 0.f, 0.f);
	tpose.forearm = Vec3f(30.f, 0.f, 0.f);
	tpose.thumb = Vec3f(0.f, 0.f, 1.f);
	const Vec3f opposite = vr::restElbowDirection(tpose, &source);
	CPPUNIT_ASSERT_EQUAL(std::string("opposite the thumb"), std::string(source));
	rollCheckVector("opposite the thumb", Vec3f(0.f, 0.f, -1.f), opposite);

	// Straight and no thumb: still a unit vector across the arm, no NaN
	tpose.thumb = Vec3f(0.f);
	const Vec3f any = vr::restElbowDirection(tpose, &source);
	CPPUNIT_ASSERT_EQUAL(std::string("any"), std::string(source));
	rollCheckFloat("unit", 1.f, glm::length(any));
	rollCheckFloat("across", 0.f, any.x, 0.001f);
}

void ArmRollTest::testRestRelationHasNoRoll() {

	const vr::ArmRest rest = bentRest();
	const Vec3f e0 = vr::restElbowDirection(rest);
	const float l1 = glm::length(rest.upper);
	const float l2 = glm::length(rest.forearm);
	vr::ArmIKParams ik;
	vr::ArmRollParams params;

	// The chain posed exactly as modelled: every rotation is the identity, every roll zero
	{
		vr::ArmRollState state;
		const Vec3f shoulder(10.f, 20.f, 30.f);
		const vr::ArmRollPose out = vr::solveArmRoll(shoulder, shoulder + rest.upper + rest.forearm, quat_identity(),
		                                             l1, l2, e0, rest, ik, params, state);
		rollCheckVector("elbow as at rest", shoulder + rest.upper, out.pose.elbow, 0.01f);
		rollCheckVector("wrist on the target", shoulder + rest.upper + rest.forearm, out.pose.wrist, 0.01f);
		rollCheckRotation("upper identity", quat_identity(), out.upper);
		rollCheckRotation("forearm identity", quat_identity(), out.forearm);
		rollCheckRotation("hand as given", quat_identity(), out.hand);
		rollCheckFloat("no hand roll", 0.f, out.handRoll, 0.05f);
		rollCheckFloat("no swing", 0.f, out.elbowSwing, 0.05f);
		rollCheckFloat("no forearm roll", 0.f, out.forearmRoll, 0.05f);
		rollCheckFloat("no upper roll", 0.f, out.upperRoll, 0.05f);
		rollCheckFloat("no wrist twist", 0.f, out.wristTwist, 0.05f);
		CPPUNIT_ASSERT_EQUAL(std::string("the rest triangle"), std::string(out.restElbowSource));
	}

	// The whole chain moved rigidly: every bone takes that rotation and nothing rolls
	{
		const glm::quat turns[] = {
			rollOf(Vec3f(0.f, 1.f, 0.f), 70.f),
			rollOf(Vec3f(1.f, 0.f, 0.f), -50.f) * rollOf(Vec3f(0.f, 1.f, 0.f), 20.f),
			rollOf(Vec3f(0.f, 0.f, 1.f), 160.f) * rollOf(Vec3f(1.f, 0.f, 0.f), 80.f),
		};
		for(const glm::quat & turn : turns) {
			vr::ArmRollState state;
			const Vec3f shoulder(-5.f, 0.f, 12.f);
			const vr::ArmRollPose out = vr::solveArmRoll(shoulder, shoulder + turn * (rest.upper + rest.forearm), turn,
			                                             l1, l2, turn * e0, rest, ik, params, state);
			rollCheckVector("elbow turned with the chain", shoulder + turn * rest.upper, out.pose.elbow, 0.02f);
			rollCheckRotation("upper is the turn", turn, out.upper);
			rollCheckRotation("forearm is the turn", turn, out.forearm);
			rollCheckFloat("no hand roll", 0.f, out.handRoll, 0.05f);
			rollCheckFloat("no forearm roll", 0.f, out.forearmRoll, 0.05f);
			rollCheckFloat("no upper roll", 0.f, out.upperRoll, 0.05f);
		}
	}

	// A different bend (the target nearer the shoulder) with the hand following the forearm's neutral
	// frame is still roll-free: the bend is a swing, not a twist
	{
		vr::ArmRollState state;
		const Vec3f shoulder(0.f);
		const Vec3f target = shoulder + glm::normalize(rest.upper + rest.forearm) * 30.f;
		const vr::ArmRollPose neutral = vr::solveArmRoll(shoulder, target, quat_identity(), l1, l2, e0, rest, ik, params, state);
		// take the forearm's neutral rotation as the hand and solve again: nothing may roll
		const vr::ArmRollPose again = vr::solveArmRoll(shoulder, target, neutral.forearm, l1, l2, e0, rest, ik, params, state);
		rollCheckFloat("no hand roll on a bend", 0.f, again.handRoll, 0.05f);
		rollCheckFloat("no forearm roll on a bend", 0.f, again.forearmRoll, 0.05f);
		rollCheckRotation("the forearm is where the hand follows", neutral.forearm, again.forearm);
		rollCheckVector("wrist on the target", target, again.pose.wrist, 0.01f);
	}
}

void ArmRollTest::testRollIsSharedAlongTheArm() {

	const vr::ArmRest rest = bentRest();
	const Vec3f e0 = vr::restElbowDirection(rest);
	const Vec3f f0 = glm::normalize(rest.forearm);
	const float l1 = glm::length(rest.upper);
	const float l2 = glm::length(rest.forearm);
	vr::ArmIKParams ik;
	vr::ArmRollParams params;
	params.forearmShare = 0.667f;
	params.upperShare = 0.5f;
	params.elbowShare = 0.33f;

	const Vec3f shoulder(3.f, 4.f, 5.f);
	const Vec3f target = shoulder + rest.upper + rest.forearm;
	const vr::ArmPose base = vr::solveArm(shoulder, target, l1, l2, e0, ik);

	const float rolls[] = { 30.f, -45.f, 90.f, -90.f, 150.f };
	for(float roll : rolls) {
		vr::ArmRollState state;
		// The hand rolled about the forearm, in the rest relation
		const glm::quat hand = rollOf(f0, roll);
		const vr::ArmRollPose out = vr::solveArmRoll(shoulder, target, hand, l1, l2, e0, rest, ik, params, state);

		// The roll is measured as given and the joints are still met
		rollCheckFloat("hand roll", roll, out.handRoll, 0.05f);
		rollCheckVector("wrist on the target", target, out.pose.wrist, 0.01f);
		rollCheckFloat("no error", 0.f, out.pose.error, 0.01f);
		rollCheckFloat("upper length", l1, glm::distance(out.pose.shoulder, out.pose.elbow), 0.01f);
		rollCheckFloat("forearm length", l2, glm::distance(out.pose.elbow, out.pose.wrist), 0.01f);

		// The elbow swung about the shoulder-target line by a third of the roll
		rollCheckFloat("elbow swing", 0.33f * roll, out.elbowSwing, 0.01f);
		const Vec3f line = target - shoulder;
		rollCheckFloat("elbow moved by the swing", std::abs(out.elbowSwing),
		               angleAcross(base.elbow - shoulder, out.pose.elbow - shoulder, line), 0.5f);

		// What is left is less than the roll, roughly the roll minus the swing
		CPPUNIT_ASSERT_MESSAGE("the swing reduces the roll", std::abs(out.residualRoll) < std::abs(roll));
		rollCheckFloat("residual near roll minus swing", roll - out.elbowSwing, out.residualRoll, 0.4f * std::abs(out.elbowSwing));

		// The shares, exactly
		rollCheckFloat("forearm share", 0.667f * out.residualRoll, out.forearmRoll, 0.01f);
		rollCheckFloat("upper share", 0.5f * out.forearmRoll, out.upperRoll, 0.01f);
		rollCheckFloat("wrist band", out.residualRoll - out.forearmRoll, out.wristTwist, 0.01f);
		rollCheckFloat("elbow band", out.forearmRoll - out.upperRoll, out.elbowTwist, 0.01f);

		// The rotations show it: the twist between the forearm bone and the hand about the forearm is the
		// wrist band, the forearm's own roll off the neutral plane is the forearm's share
		rollCheckFloat("wrist band in the rotations", out.wristTwist,
		               vr::twistAboutAxis(glm::inverse(out.forearm) * out.hand, f0), 0.05f);
		const Vec3f forearmAxis = glm::normalize(out.pose.wrist - out.pose.elbow);
		rollCheckFloat("forearm bone along its joints", 1.f, glm::dot(out.forearm * f0, forearmAxis), 0.001f);
		const Vec3f upperAxis = glm::normalize(out.pose.elbow - out.pose.shoulder);
		rollCheckFloat("upper bone along its joints", 1.f, glm::dot(out.upper * glm::normalize(rest.upper), upperAxis), 0.001f);
		// the upper arm's roll off the bend plane: its rest inside-of-the-bend reference, turned by the
		// bone, leans out of the bend plane by the sine of its roll
		const Vec3f bendNormal = glm::normalize(glm::cross(upperAxis, forearmAxis));
		const Vec3f restInside = glm::normalize(-e0 - glm::dot(-e0, glm::normalize(rest.upper)) * glm::normalize(rest.upper));
		rollCheckFloat("upper roll off the bend plane", std::sin(glm::radians(out.upperRoll)),
		               glm::dot(out.upper * restInside, bendNormal), 0.02f);
	}
}

void ArmRollTest::testLargeRollSwingsTheElbow() {

	const vr::ArmRest rest = bentRest();
	const Vec3f e0 = vr::restElbowDirection(rest);
	const Vec3f f0 = glm::normalize(rest.forearm);
	const float l1 = glm::length(rest.upper);
	const float l2 = glm::length(rest.forearm);
	vr::ArmIKParams ik;
	vr::ArmRollParams params;

	const Vec3f shoulder(0.f);
	const Vec3f target = rest.upper + rest.forearm;
	const vr::ArmPose base = vr::solveArm(shoulder, target, l1, l2, e0, ik);

	// A hand rolled by 170 degrees: the elbow swings by a third of it, every band carries at most its
	// share - nothing like the whole 170 on the wrist band the old rule left there
	vr::ArmRollState state;
	const vr::ArmRollPose out = vr::solveArmRoll(shoulder, target, rollOf(f0, 170.f), l1, l2, e0, rest, ik, params, state);
	rollCheckFloat("hand roll", 170.f, out.handRoll, 0.05f);
	rollCheckFloat("elbow swing", 0.33f * 170.f, out.elbowSwing, 0.01f);
	rollCheckFloat("elbow moved", 0.33f * 170.f, angleAcross(base.elbow, out.pose.elbow, target), 0.5f);
	rollCheckVector("wrist on the target", target, out.pose.wrist, 0.01f);
	CPPUNIT_ASSERT(std::abs(out.wristTwist) < 60.f);
	CPPUNIT_ASSERT(std::abs(out.elbowTwist) < 60.f);
	CPPUNIT_ASSERT(std::abs(out.upperRoll) < 60.f);
	CPPUNIT_ASSERT(std::abs(out.wristTwist) + std::abs(out.elbowTwist) + std::abs(out.upperRoll) < 170.f);

	// With no elbow share the elbow stays and the forearm and the hand share the whole roll
	vr::ArmRollParams stiff;
	stiff.elbowShare = 0.f;
	vr::ArmRollState stiffState;
	const vr::ArmRollPose kept = vr::solveArmRoll(shoulder, target, rollOf(f0, 170.f), l1, l2, e0, rest, ik, stiff, stiffState);
	rollCheckFloat("no swing", 0.f, kept.elbowSwing, 0.001f);
	rollCheckVector("elbow kept", base.elbow, kept.pose.elbow, 0.01f);
	rollCheckFloat("residual is the roll", 170.f, kept.residualRoll, 0.05f);
}

void ArmRollTest::testOverflowThroughTheSolver() {

	const vr::ArmRest rest = bentRest();
	const Vec3f e0 = vr::restElbowDirection(rest);
	const Vec3f f0 = glm::normalize(rest.forearm);
	const float l1 = glm::length(rest.upper);
	const float l2 = glm::length(rest.forearm);
	vr::ArmIKParams ik;
	vr::ArmRollParams params;
	params.overflowDegrees = 45.f;

	const Vec3f shoulder(0.f);
	const Vec3f target = rest.upper + rest.forearm;

	// The hand rolls on through the half turn: the roll continues past 180 and the elbow keeps
	// swinging the same way, until the overflow, where both flip to the complement
	vr::ArmRollState state;
	const float measured[] = { 150.f, 170.f, -170.f, -140.f, -130.f };
	const float expected[] = { 150.f, 170.f, 190.f, 220.f, -130.f };
	float previousSwing = 0.f;
	for(size_t i = 0; i < 5; i++) {
		const vr::ArmRollPose out = vr::solveArmRoll(shoulder, target, rollOf(f0, measured[i]), l1, l2, e0, rest, ik, params, state);
		rollCheckFloat("unwrapped roll", expected[i], out.handRoll, 0.1f);
		rollCheckFloat("swing follows", 0.33f * expected[i], out.elbowSwing, 0.05f);
		rollCheckVector("wrist on the target", target, out.pose.wrist, 0.01f);
		if(i > 0 && i < 4) {
			CPPUNIT_ASSERT_MESSAGE("the elbow keeps swinging the same way", out.elbowSwing > previousSwing);
		}
		// the residual stays on the same turn as the roll, the bands within their shares
		rollCheckFloat("residual on the same turn", expected[i] - out.elbowSwing, out.residualRoll, 0.4f * std::abs(out.elbowSwing) + 0.1f);
		previousSwing = out.elbowSwing;
	}
	CPPUNIT_ASSERT_EQUAL(0, state.overflow);
}

void ArmRollTest::testStraightRestArm() {

	// A T-posed rest arm: the bend reference comes from the thumb, and a posed bend still gives a
	// consistent, roll-free chain for a hand that follows the forearm
	vr::ArmRest rest;
	rest.upper = Vec3f(28.f, 0.f, 0.f);
	rest.forearm = Vec3f(30.f, 0.f, 0.f);
	rest.thumb = Vec3f(0.f, 0.f, 1.f);
	const Vec3f e0 = vr::restElbowDirection(rest);
	rollCheckVector("elbow opposite the thumb", Vec3f(0.f, 0.f, -1.f), e0);
	vr::ArmIKParams ik;
	vr::ArmRollParams params;

	const Vec3f shoulder(0.f);
	const Vec3f target(40.f, 10.f, 0.f);
	// The neutral forearm of the base bend plane: no share swings the elbow or rolls the forearm
	vr::ArmRollParams stiff;
	stiff.elbowShare = stiff.forearmShare = stiff.upperShare = 0.f;
	vr::ArmRollState state;
	const vr::ArmRollPose neutral = vr::solveArmRoll(shoulder, target, quat_identity(), 28.f, 30.f, e0, rest, ik, stiff, state);
	CPPUNIT_ASSERT_EQUAL(std::string("opposite the thumb"), std::string(neutral.restElbowSource));
	CPPUNIT_ASSERT(std::isfinite(neutral.handRoll) && std::isfinite(neutral.forearmRoll));
	rollCheckVector("wrist on the target", target, neutral.pose.wrist, 0.01f);
	// the elbow went opposite the thumb's side (back, -z)
	CPPUNIT_ASSERT(neutral.pose.elbow.z < -1.f);

	// A hand that sits on that neutral forearm has no roll, so with the full shares nothing moves
	vr::ArmRollState fresh;
	const vr::ArmRollPose follows = vr::solveArmRoll(shoulder, target, neutral.forearm, 28.f, 30.f, e0, rest, ik, params, fresh);
	rollCheckFloat("no roll when the hand follows the forearm", 0.f, follows.handRoll, 0.05f);
	rollCheckFloat("no swing", 0.f, follows.elbowSwing, 0.05f);
	rollCheckRotation("forearm kept", neutral.forearm, follows.forearm);
	rollCheckVector("elbow kept", neutral.pose.elbow, follows.pose.elbow, 0.01f);

	// A straight posed arm (the target beyond reach) still has a bend plane from the hint: no NaN,
	// the bones along the arm
	vr::ArmRollState beyond;
	const vr::ArmRollPose stretched = vr::solveArmRoll(shoulder, Vec3f(100.f, 0.f, 0.f), rollOf(Vec3f(1.f, 0.f, 0.f), 60.f),
	                                                   28.f, 30.f, e0, rest, ik, params, beyond);
	CPPUNIT_ASSERT(stretched.pose.stretched);
	CPPUNIT_ASSERT(std::isfinite(stretched.handRoll) && std::isfinite(stretched.upperRoll));
	rollCheckVector("upper along the arm", Vec3f(1.f, 0.f, 0.f), stretched.upper * Vec3f(1.f, 0.f, 0.f));
	rollCheckVector("forearm along the arm", Vec3f(1.f, 0.f, 0.f), stretched.forearm * Vec3f(1.f, 0.f, 0.f));
	rollCheckFloat("hand roll measured", 60.f, stretched.handRoll, 0.1f);
}

void ArmRollTest::testOldRuleWringsTheWrist() {

	// The rule of #36 in these terms: the forearm keeps no roll (share 0) and the elbow does not move
	// (share 0): the whole roll lands on the wrist band - the candy wrapper of research section 16a
	const vr::ArmRest rest = bentRest();
	const Vec3f e0 = vr::restElbowDirection(rest);
	const Vec3f f0 = glm::normalize(rest.forearm);
	vr::ArmIKParams ik;
	vr::ArmRollParams old;
	old.forearmShare = 0.f;
	old.upperShare = 0.f;
	old.elbowShare = 0.f;
	vr::ArmRollState state;
	const Vec3f target = rest.upper + rest.forearm;
	const vr::ArmRollPose out = vr::solveArmRoll(Vec3f(0.f), target, rollOf(f0, 90.f), glm::length(rest.upper),
	                                             glm::length(rest.forearm), e0, rest, ik, old, state);
	rollCheckFloat("the wrist band takes it all", 90.f, out.wristTwist, 0.05f);
	rollCheckFloat("the forearm nothing", 0.f, out.forearmRoll, 0.001f);
	rollCheckFloat("the upper arm nothing", 0.f, out.upperRoll, 0.001f);
	rollCheckRotation("the forearm stays neutral", quat_identity(), out.forearm);

	// With the defaults the same hand leaves the wrist band a third of what is left
	vr::ArmRollParams params;
	vr::ArmRollState fresh;
	const vr::ArmRollPose shared = vr::solveArmRoll(Vec3f(0.f), target, rollOf(f0, 90.f), glm::length(rest.upper),
	                                                glm::length(rest.forearm), e0, rest, ik, params, fresh);
	CPPUNIT_ASSERT_MESSAGE("the wrist band carries less than a third of the roll", std::abs(shared.wristTwist) < 30.f);
	CPPUNIT_ASSERT_MESSAGE("the forearm carries more than the wrist band", std::abs(shared.forearmRoll) > std::abs(shared.wristTwist));
}
