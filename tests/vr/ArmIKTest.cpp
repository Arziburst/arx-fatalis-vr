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

#include "vr/ArmIKTest.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "graphics/Math.h"
#include "vr/ArmIK.h"
#include "vr/HeadPose.h"

CppUnit::AutoRegisterSuite<ArmIKTest> g_registerArmIKTest;

namespace {

void armCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void armCheckFloat(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! The bone lengths and the wrist of a pose, checked against what was asked
void checkChain(const char * what, const vr::ArmPose & pose, float l1, float l2) {
	armCheckFloat((std::string(what) + ": upper arm length").c_str(), l1, glm::distance(pose.shoulder, pose.elbow));
	armCheckFloat((std::string(what) + ": forearm length").c_str(), l2, glm::distance(pose.elbow, pose.wrist));
	CPPUNIT_ASSERT_MESSAGE(what, std::isfinite(pose.elbow.x) && std::isfinite(pose.elbow.y) && std::isfinite(pose.elbow.z));
	CPPUNIT_ASSERT_MESSAGE(what, std::isfinite(pose.wrist.x) && std::isfinite(pose.wrist.y) && std::isfinite(pose.wrist.z));
}

vr::HeadSample headSample(float yaw, float pitch, float roll, const Vec3f & position = Vec3f(0.f)) {
	vr::HeadSample sample;
	sample.orientation = vr::headOrientationFromDegrees(yaw, pitch, roll);
	sample.position = position;
	return sample;
}

//! A hand mesh: a slab along +z (wrist at the origin), 8 wide (x), 3 thick (y), with a thumb bump on \a thumbSide x
std::vector<Vec3f> handMesh(float thumbSide) {
	std::vector<Vec3f> vertices;
	for(int z = 0; z <= 20; z += 4) {
		for(int x = -4; x <= 4; x += 4) {
			vertices.push_back(Vec3f(float(x), -1.5f, float(z)));
			vertices.push_back(Vec3f(float(x), 1.5f, float(z)));
		}
	}
	vertices.push_back(Vec3f(thumbSide * 7.f, 0.f, 6.f));
	vertices.push_back(Vec3f(thumbSide * 6.f, 0.f, 9.f));
	return vertices;
}

} // anonymous namespace

void ArmIKTest::testRotationBetween() {

	const Vec3f pairs[][2] = {
		{ Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 1.f, 0.f) },
		{ Vec3f(0.f, 0.f, 3.f), Vec3f(1.f, 1.f, 1.f) },
		{ Vec3f(-2.f, 5.f, 1.f), Vec3f(0.3f, -0.4f, 7.f) },
		{ Vec3f(0.f, 1.f, 0.f), Vec3f(0.f, 1.f, 0.f) },
	};
	for(const auto & pair : pairs) {
		const glm::quat q = vr::rotationBetween(pair[0], pair[1]);
		armCheckVector("rotationBetween turns from onto to", glm::normalize(pair[1]), q * glm::normalize(pair[0]));
		armCheckFloat("a unit quaternion", 1.f, glm::length(q));
	}

	// Opposite directions: a half turn that still lands on the target
	const glm::quat half = vr::rotationBetween(Vec3f(0.f, 0.f, 1.f), Vec3f(0.f, 0.f, -2.f));
	armCheckVector("opposite", Vec3f(0.f, 0.f, -1.f), half * Vec3f(0.f, 0.f, 1.f));

	// Parallel: the identity
	const glm::quat same = vr::rotationBetween(Vec3f(2.f, 0.f, 0.f), Vec3f(5.f, 0.f, 0.f));
	armCheckFloat("parallel is the identity", 1.f, std::abs(same.w));

	// Degenerate input: the identity, no NaN
	const glm::quat zero = vr::rotationBetween(Vec3f(0.f), Vec3f(1.f, 0.f, 0.f));
	armCheckFloat("zero vector gives the identity", 1.f, zero.w);
}

void ArmIKTest::testReachableTargetIsMet() {

	const Vec3f shoulder(100.f, 50.f, 200.f);
	const float l1 = 30.f;
	const float l2 = 28.f;
	const Vec3f targets[] = {
		shoulder + Vec3f(20.f, 30.f, 10.f),
		shoulder + Vec3f(-40.f, 10.f, 25.f),
		shoulder + Vec3f(0.f, 0.f, 57.9f),  // nearly straight
		shoulder + Vec3f(0.f, 5.f, 0.f),    // nearly folded
	};
	for(const Vec3f & target : targets) {
		vr::ArmPose pose = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 0.f));
		armCheckVector("wrist on the target", target, pose.wrist);
		armCheckFloat("no error", 0.f, pose.error);
		CPPUNIT_ASSERT(!pose.stretched);
		armCheckFloat("no slide", 0.f, pose.slide);
		armCheckVector("the shoulder stays", shoulder, pose.shoulder);
		armCheckFloat("reach is the distance to the target", glm::distance(shoulder, target), pose.reach);
		checkChain("reachable", pose, l1, l2);
	}
}

void ArmIKTest::testElbowBendsTowardsTheHint() {

	const Vec3f shoulder(0.f);
	const Vec3f target(0.f, 0.f, 40.f);
	const float l1 = 30.f;
	const float l2 = 28.f;

	// Hint down (engine y is down): the elbow is below the shoulder-target line
	vr::ArmPose down = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 0.f));
	CPPUNIT_ASSERT(down.elbow.y > 5.f);
	armCheckFloat("in the plane of the hint", 0.f, down.elbow.x);

	// The opposite hint flips the side; any length of hint will do
	vr::ArmPose up = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, -7.f, 0.f));
	CPPUNIT_ASSERT(up.elbow.y < -5.f);
	armCheckVector("mirror image", Vec3f(down.elbow.x, -down.elbow.y, down.elbow.z), up.elbow);

	// A hint with a component along the arm only uses the part across it
	vr::ArmPose skew = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 5.f));
	armCheckVector("along-arm part ignored", down.elbow, skew.elbow);

	// A hint along the arm: some perpendicular, still a valid chain
	vr::ArmPose along = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 0.f, 1.f));
	checkChain("hint along the arm", along, l1, l2);
	armCheckVector("still on the target", target, along.wrist);
}

void ArmIKTest::testOutOfReachSlidesThenStretches() {

	const Vec3f shoulder(10.f, 20.f, 30.f);
	const Vec3f target = shoulder + Vec3f(0.f, 0.f, 100.f);
	const float l1 = 30.f;
	const float l2 = 28.f;

	// No slide allowed: the arm points at the target, the wrist stops at the arm's length
	vr::ArmPose stiff = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 0.f));
	CPPUNIT_ASSERT(stiff.stretched);
	armCheckFloat("no slide", 0.f, stiff.slide);
	armCheckVector("wrist at the arm's length", shoulder + Vec3f(0.f, 0.f, 58.f), stiff.wrist);
	armCheckFloat("error is the shortfall", 42.f, stiff.error);
	armCheckFloat("reach", 100.f, stiff.reach);
	checkChain("stiff", stiff, l1, l2);
	armCheckVector("a straight arm", shoulder + Vec3f(0.f, 0.f, 30.f), stiff.elbow);

	// A limited slide: the shoulder moves by the limit, the rest is still short
	vr::ArmIKParams limited;
	limited.maxShoulderSlide = 20.f;
	vr::ArmPose slid = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 0.f), limited);
	CPPUNIT_ASSERT(slid.stretched);
	armCheckFloat("slid by the limit", 20.f, slid.slide);
	armCheckVector("shoulder moved towards the target", shoulder + Vec3f(0.f, 0.f, 20.f), slid.shoulder);
	armCheckFloat("error after the slide", 22.f, slid.error);
	checkChain("slid", slid, l1, l2);

	// Enough slide: the target is reached, the arm is straight but not stretched
	vr::ArmIKParams ample;
	ample.maxShoulderSlide = 100.f;
	vr::ArmPose reached = vr::solveArm(shoulder, target, l1, l2, Vec3f(0.f, 1.f, 0.f), ample);
	CPPUNIT_ASSERT(!reached.stretched);
	armCheckFloat("slid just enough", 42.f, reached.slide);
	armCheckVector("wrist on the target", target, reached.wrist);
	armCheckFloat("no error", 0.f, reached.error);
	checkChain("reached", reached, l1, l2);

	// Within reach the slide limit changes nothing
	// ("near" and "far" are macros from windows.h on MSVC, TRAPS.md)
	vr::ArmPose within = vr::solveArm(shoulder, shoulder + Vec3f(10.f, 10.f, 10.f), l1, l2, Vec3f(0.f, 1.f, 0.f), ample);
	armCheckFloat("no slide within reach", 0.f, within.slide);
	CPPUNIT_ASSERT(!within.stretched);
}

void ArmIKTest::testFoldedAndDegenerateTargets() {

	const Vec3f shoulder(5.f, 5.f, 5.f);

	// Target at the shoulder, equal bones: folded flat, the elbow out along the hint, no NaN
	vr::ArmPose flat = vr::solveArm(shoulder, shoulder, 30.f, 30.f, Vec3f(0.f, 1.f, 0.f));
	checkChain("flat", flat, 30.f, 30.f);
	armCheckVector("wrist back at the shoulder", shoulder, flat.wrist);
	armCheckVector("elbow along the hint", shoulder + Vec3f(0.f, 30.f, 0.f), flat.elbow);

	// Unequal bones: the chain cannot fold past the difference of the lengths
	vr::ArmPose fold = vr::solveArm(shoulder, shoulder + Vec3f(1.f, 0.f, 0.f), 30.f, 20.f, Vec3f(0.f, 1.f, 0.f));
	checkChain("fold", fold, 30.f, 20.f);
	armCheckFloat("wrist at the fold", 10.f, glm::distance(shoulder, fold.wrist));
	armCheckFloat("error is the rest", 9.f, fold.error);
	CPPUNIT_ASSERT(!fold.stretched);

	// Zero-length bones do not divide by zero
	vr::ArmPose zero = vr::solveArm(shoulder, shoulder + Vec3f(0.f, 0.f, 10.f), 0.f, 0.f, Vec3f(0.f, 1.f, 0.f));
	CPPUNIT_ASSERT(std::isfinite(zero.elbow.x) && std::isfinite(zero.wrist.z));
	CPPUNIT_ASSERT(zero.stretched);
}

void ArmIKTest::testHandMovesLikeTheHead() {

	// Whatever the turn: a hand where the head is sits at the camera; a hand moved from the head by
	// v sits where the camera would be if the head had moved by v (the body not walking after it)
	vr::HeadCameraParams params;
	const float turns[][2] = { { 0.f, 0.f }, { 30.f, 90.f }, { 200.f, 45.f }, { -20.f, 350.f } };
	const Vec3f moves[] = { Vec3f(0.3f, 0.f, 0.f), Vec3f(0.f, -0.4f, -0.5f), Vec3f(-0.25f, 0.1f, 0.2f) };
	for(const auto & turn : turns) {
		const float referenceYaw = turn[0];
		const float bodyYaw = turn[1];
		vr::HeadReference reference;
		reference.yaw = referenceYaw;
		reference.position = Vec3f(1.f, 1.6f, -2.f);
		const vr::HeadSample head = headSample(referenceYaw + 10.f, -20.f, 5.f, Vec3f(1.2f, 1.5f, -2.3f));
		const vr::HeadCamera camera = vr::headToCamera(head, reference, bodyYaw, Vec2f(0.f), params);

		vr::HandSample atHead;
		atHead.position = head.position;
		armCheckVector("a hand at the head is at the camera", Vec3f(0.f),
		               vr::handToWorld(atHead, head, reference, bodyYaw, params).offset);

		for(const Vec3f & move : moves) {
			vr::HandSample hand;
			hand.position = head.position + move;
			vr::HeadSample movedHead = head;
			movedHead.position = head.position + move;
			const vr::HeadCamera movedCamera = vr::headToCamera(movedHead, reference, bodyYaw, Vec2f(0.f), params);
			armCheckVector("the hand's offset is the camera's movement", movedCamera.offset - camera.offset,
			               vr::handToWorld(hand, head, reference, bodyYaw, params).offset);
		}
	}

	// A hand 30 cm right of a head that looks the reference way is 30 units right of the camera
	vr::HeadReference reference;
	reference.yaw = 0.f;
	const vr::HeadSample head = headSample(0.f, 0.f, 0.f);
	vr::HandSample right;
	right.position = Vec3f(0.3f, 0.f, 0.f);
	armCheckVector("30 cm right", Vec3f(30.f, 0.f, 0.f), vr::handToWorld(right, head, reference, 0.f).offset);
	// 40 cm in front of it (OpenXR -z) is 40 units along the engine's +z
	vr::HandSample front;
	front.position = Vec3f(0.f, 0.f, -0.4f);
	armCheckVector("40 cm in front", Vec3f(0.f, 0.f, 40.f), vr::handToWorld(front, head, reference, 0.f).offset);
	// 20 cm below (OpenXR -y) is 20 units down (engine +y)
	vr::HandSample below;
	below.position = Vec3f(0.f, -0.2f, 0.f);
	armCheckVector("20 cm below", Vec3f(0.f, 20.f, 0.f), vr::handToWorld(below, head, reference, 0.f).offset);
}

void ArmIKTest::testHandTurnsLikeTheHead() {

	// A hand oriented like the head has the camera's rotation: its orientation turns the engine's
	// view axes onto the world the way the camera's angles do (toRotationMatrix is world-to-view)
	const float cases[][5] = {
		{ 0.f, 0.f, 0.f, 0.f, 0.f },
		{ 30.f, -20.f, 10.f, 0.f, 0.f },
		{ 30.f, -20.f, 10.f, 50.f, 120.f },
		{ -100.f, 45.f, -30.f, 200.f, 10.f },
		{ 170.f, 80.f, 0.f, 10.f, 300.f },
	};
	for(const auto & c : cases) {
		const vr::HeadSample head = headSample(c[0], c[1], c[2]);
		vr::HeadReference reference;
		reference.yaw = c[3];
		const float bodyYaw = c[4];
		const vr::HeadCamera camera = vr::headToCamera(head, reference, bodyYaw);
		vr::HandSample hand;
		hand.orientation = head.orientation;
		const vr::HandPlacement placement = vr::handToWorld(hand, head, reference, bodyYaw);
		const glm::mat3 expected = glm::transpose(glm::mat3(toRotationMatrix(camera.angle)));
		const glm::mat3 actual = glm::mat3_cast(placement.orientation);
		for(int column = 0; column < 3; column++) {
			armCheckVector("hand rotation is the camera's", expected[column], actual[column], 0.002f);
		}
	}

	// The grip's own axes, head and reference straight: OpenXR +X stays right, +Y (up) is engine -y,
	// +Z (back) is engine -z - the same half turn about x as toEngineAxes
	vr::HeadReference reference;
	const vr::HeadSample head = headSample(0.f, 0.f, 0.f);
	vr::HandSample hand;
	const glm::quat grip = vr::handToWorld(hand, head, reference, 0.f).orientation;
	armCheckVector("grip +X", Vec3f(1.f, 0.f, 0.f), grip * Vec3f(1.f, 0.f, 0.f));
	armCheckVector("grip +Y", Vec3f(0.f, -1.f, 0.f), grip * Vec3f(0.f, -1.f, 0.f));
	armCheckVector("grip +Z", Vec3f(0.f, 0.f, -1.f), grip * Vec3f(0.f, 0.f, -1.f));

	// A hand turned 90 degrees left about the room's vertical in a body that faces the reference
	// way: its grip +X (right) now points the engine's +z (forward)
	hand.orientation = vr::headOrientationFromDegrees(90.f, 0.f, 0.f);
	const glm::quat turned = vr::handToWorld(hand, head, reference, 0.f).orientation;
	armCheckVector("turned left: right is forward", Vec3f(0.f, 0.f, 1.f), turned * Vec3f(1.f, 0.f, 0.f), 0.002f);

	// The body's yaw turns the hand with it, like the head
	reference.yaw = 0.f;
	hand.orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	const glm::quat withBody = vr::handToWorld(hand, head, reference, 90.f).orientation;
	const Vec3f viaVector = vr::turnAboutVertical(Vec3f(1.f, 0.f, 0.f), 90.f);
	armCheckVector("body yaw turns the hand", viaVector, withBody * Vec3f(1.f, 0.f, 0.f), 0.002f);
}

void ArmIKTest::testHandFrameFromMesh() {

	// The thumb on +x: knuckles along +z, thumb +x, palm = knuckles x thumb = +y
	vr::HandFrame frame = vr::handFrameFromMesh(handMesh(1.f));
	CPPUNIT_ASSERT(frame.valid);
	armCheckVector("knuckles", Vec3f(0.f, 0.f, 1.f), frame.knuckles, 0.05f);
	armCheckVector("thumb", Vec3f(1.f, 0.f, 0.f), frame.thumb, 0.05f);
	armCheckVector("palm", Vec3f(0.f, 1.f, 0.f), frame.palm, 0.05f);
	CPPUNIT_ASSERT(frame.length > 19.f && frame.length < 21.f);
	CPPUNIT_ASSERT(frame.thumbExtent > frame.otherExtent);

	// The mirrored hand: the thumb on -x, the palm flips with it
	vr::HandFrame mirror = vr::handFrameFromMesh(handMesh(-1.f));
	CPPUNIT_ASSERT(mirror.valid);
	armCheckVector("mirror thumb", Vec3f(-1.f, 0.f, 0.f), mirror.thumb, 0.05f);
	armCheckVector("mirror palm", Vec3f(0.f, -1.f, 0.f), mirror.palm, 0.05f);

	// A palm point names the long axis exactly
	const Vec3f palmPoint(0.f, 0.f, 10.f);
	vr::HandFrame hinted = vr::handFrameFromMesh(handMesh(1.f), &palmPoint);
	armCheckVector("hinted knuckles", Vec3f(0.f, 0.f, 1.f), hinted.knuckles);
	armCheckVector("hinted thumb", Vec3f(1.f, 0.f, 0.f), hinted.thumb, 0.05f);

	// A thumb point names the lateral axis - even against the extents (the bump on +x, the thumb said -x)
	const Vec3f thumbPoint(-5.f, 0.f, 4.f);
	vr::HandFrame named = vr::handFrameFromMesh(handMesh(1.f), &palmPoint, &thumbPoint);
	CPPUNIT_ASSERT(named.valid);
	armCheckVector("named knuckles", Vec3f(0.f, 0.f, 1.f), named.knuckles);
	armCheckVector("named thumb", Vec3f(-1.f, 0.f, 0.f), named.thumb);
	armCheckVector("named palm", Vec3f(0.f, -1.f, 0.f), named.palm);
	CPPUNIT_ASSERT(named.length > 19.f && named.length < 21.f);

	// Too few vertices, or no thumb side: not valid, no NaN
	CPPUNIT_ASSERT(!vr::handFrameFromMesh(std::vector<Vec3f>(3, Vec3f(1.f))).valid);
	std::vector<Vec3f> round;
	for(int i = 0; i < 16; i++) {
		const float a = float(i) * 0.3927f;
		round.push_back(Vec3f(std::cos(a) * 4.f, std::sin(a) * 4.f, 10.f));
		round.push_back(Vec3f(std::cos(a) * 4.f, std::sin(a) * 4.f, 2.f));
	}
	vr::HandFrame tube = vr::handFrameFromMesh(round);
	CPPUNIT_ASSERT(!tube.valid);
	CPPUNIT_ASSERT(std::isfinite(tube.thumb.x));
}

void ArmIKTest::testHandToGripPutsTheAxesOnTheGrip() {

	// For any (right-handed) hand frame, the rotation puts the palm on the grip's +X, the knuckles on
	// its -Y (engine +y) and the thumb on its -Z (engine +z) - the OpenXR grip pose: -Z runs through
	// the fingers' tube from the little finger to the thumb, +Y follows by the right-hand rule
	const vr::HandFrame frames[] = {
		vr::handFrameFromMesh(handMesh(1.f)),
		vr::handFrameFromMesh(handMesh(-1.f)),
	};
	for(const vr::HandFrame & frame : frames) {
		const glm::quat q = vr::handToGrip(frame);
		armCheckFloat("unit", 1.f, glm::length(q));
		armCheckVector("palm -> +X", Vec3f(1.f, 0.f, 0.f), q * frame.palm);
		armCheckVector("knuckles -> -Y (engine +y)", Vec3f(0.f, 1.f, 0.f), q * frame.knuckles);
		armCheckVector("thumb -> -Z (engine +z)", Vec3f(0.f, 0.f, 1.f), q * frame.thumb);
	}

	// A hand already in the grip's layout (palm +x, knuckles engine +y, thumb engine +z) needs no turn
	vr::HandFrame aligned;
	aligned.palm = Vec3f(1.f, 0.f, 0.f);
	aligned.knuckles = Vec3f(0.f, 1.f, 0.f);
	aligned.thumb = Vec3f(0.f, 0.f, 1.f);
	armCheckFloat("identity", 1.f, std::abs(vr::handToGrip(aligned).w));

	// An identity grip in a room that looks the reference way: the hand hangs with the fingers down
	// (grip -Y is the room's -y) and the thumb forward (grip -Z is the room's forward, engine +z)
	vr::HeadReference reference;
	const vr::HeadSample head = headSample(0.f, 0.f, 0.f);
	vr::HandSample hand;
	const glm::quat world = vr::handToWorld(hand, head, reference, 0.f).orientation * vr::handToGrip(frames[0]);
	armCheckVector("fingers down", Vec3f(0.f, 1.f, 0.f), world * frames[0].knuckles, 0.002f);
	armCheckVector("thumb forward", Vec3f(0.f, 0.f, 1.f), world * frames[0].thumb, 0.002f);
}
