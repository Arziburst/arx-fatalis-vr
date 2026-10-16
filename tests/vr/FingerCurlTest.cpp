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

#include "vr/FingerCurlTest.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/FingerCurl.h"

CppUnit::AutoRegisterSuite<FingerCurlTest> g_registerFingerCurlTest;

namespace {

const float CurlEpsilon = 0.05f;

void curlCheck(const char * what, float expected, float actual, float tolerance = CurlEpsilon) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void curlCheckAll(const char * what, const vr::FingerCurl & expected, const vr::FingerCurl & actual,
                  float tolerance = CurlEpsilon) {
	curlCheck((std::string(what) + ": thumb").c_str(), expected.thumb, actual.thumb, tolerance);
	curlCheck((std::string(what) + ": indexRoot").c_str(), expected.indexRoot, actual.indexRoot, tolerance);
	curlCheck((std::string(what) + ": indexTip").c_str(), expected.indexTip, actual.indexTip, tolerance);
	curlCheck((std::string(what) + ": fingersRoot").c_str(), expected.fingersRoot, actual.fingersRoot, tolerance);
	curlCheck((std::string(what) + ": fingersTip").c_str(), expected.fingersTip, actual.fingersTip, tolerance);
}

vr::HandShape handShape(float thumb, float index, float fingers) {
	vr::HandShape shape;
	shape.thumb = thumb;
	shape.index = index;
	shape.middle = shape.ring = shape.little = fingers;
	return shape;
}

} // anonymous namespace

void FingerCurlTest::testFlexionDegrees() {
	curlCheck("parallel", 0.f, vr::flexionDegrees(Vec3f(0.f, -1.f, 0.f), Vec3f(0.f, -3.f, 0.f)));
	curlCheck("perpendicular", 90.f, vr::flexionDegrees(Vec3f(0.f, -1.f, 0.f), Vec3f(-2.f, 0.f, 0.f)));
	curlCheck("opposite", 180.f, vr::flexionDegrees(Vec3f(1.f, 0.f, 0.f), Vec3f(-1.f, 0.f, 0.f)));
	curlCheck("45 degrees", 45.f, vr::flexionDegrees(Vec3f(1.f, 0.f, 0.f), Vec3f(1.f, 1.f, 0.f)));
	curlCheck("a zero bone is no bend", 0.f, vr::flexionDegrees(Vec3f(0.f), Vec3f(1.f, 0.f, 0.f)));
}

void FingerCurlTest::testSynthesizedHandLiesInTheGripFrame() {

	// Open: the fingers run along grip -Y from the palm, the thumb lies on the -Z side
	const vr::HandJoints open = vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.f, 0.f, 0.f));
	CPPUNIT_ASSERT(open.valid);
	CPPUNIT_ASSERT(open[vr::HandJoint::IndexTip].y < open[vr::HandJoint::Palm].y - 0.05f);
	CPPUNIT_ASSERT(open[vr::HandJoint::LittleTip].y < open[vr::HandJoint::Palm].y - 0.05f);
	CPPUNIT_ASSERT(open[vr::HandJoint::Wrist].y > open[vr::HandJoint::Palm].y);
	CPPUNIT_ASSERT(open[vr::HandJoint::ThumbTip].z < open[vr::HandJoint::IndexTip].z);
	CPPUNIT_ASSERT(open[vr::HandJoint::IndexTip].z < open[vr::HandJoint::LittleTip].z);
	curlCheck("an open index finger is straight: its tip is on the palm's plane", 0.f, open[vr::HandJoint::IndexTip].x, 0.001f);

	// A fist: the fingertips swing towards the palm's face - grip -X on the right hand, +X on the left
	const vr::HandJoints rightFist = vr::synthesizeHandJoints(vr::Hand::Right, handShape(1.f, 1.f, 1.f));
	const vr::HandJoints leftFist = vr::synthesizeHandJoints(vr::Hand::Left, handShape(1.f, 1.f, 1.f));
	CPPUNIT_ASSERT(rightFist[vr::HandJoint::IndexIntermediate].x < -0.03f);
	CPPUNIT_ASSERT(leftFist[vr::HandJoint::IndexIntermediate].x > 0.03f);
	CPPUNIT_ASSERT(rightFist[vr::HandJoint::IndexTip].y > open[vr::HandJoint::IndexTip].y + 0.03f);

	// Placed: every joint moves with the grip
	const Vec3f at(1.f, 2.f, 3.f);
	const glm::quat turn = glm::angleAxis(glm::radians(90.f), Vec3f(0.f, 1.f, 0.f));
	const vr::HandJoints placed = vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.f, 0.f, 0.f), at, turn);
	for(size_t i = 0; i < vr::HandJointCount; i++) {
		const Vec3f expected = at + turn * open.position[i];
		CPPUNIT_ASSERT(glm::length(expected - placed.position[i]) < 0.0001f);
	}
}

void FingerCurlTest::testOpenHandIsStraight() {
	const vr::FingerCurl curl = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.f, 0.f, 0.f)));
	curlCheckAll("open hand", vr::FingerCurl(), curl);
	// Joints that were not located: an open hand, never garbage
	vr::HandJoints none = vr::synthesizeHandJoints(vr::Hand::Right, handShape(1.f, 1.f, 1.f));
	none.valid = false;
	curlCheckAll("invalid joints", vr::FingerCurl(), vr::fingerCurlFromJoints(none));
}

void FingerCurlTest::testFistReachesTheLimits() {
	const vr::FingerCurlLimits limits;
	const vr::FingerCurl curl = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(1.f, 1.f, 1.f)));
	curlCheckAll("fist", vr::fingerCurlClosed(limits), curl);
	// Other limits scale every segment with them
	vr::FingerCurlLimits half;
	half.thumb = 30.f;
	half.indexRoot = 45.f;
	half.indexTip = 40.f;
	half.fingersRoot = 50.f;
	half.fingersTip = 20.f;
	curlCheckAll("fist with other limits", vr::fingerCurlClosed(half),
	             vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(1.f, 1.f, 1.f)), half));
}

void FingerCurlTest::testPointingHandAndThumbsUp() {
	const vr::FingerCurlLimits limits;
	// Pointing: the index finger straight, the thumb and the block closed
	vr::FingerCurl expected = vr::fingerCurlClosed(limits);
	expected.indexRoot = 0.f;
	expected.indexTip = 0.f;
	curlCheckAll("pointing", expected,
	             vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(1.f, 0.f, 1.f))));
	// Thumbs-up: the thumb straight, everything else closed
	expected = vr::fingerCurlClosed(limits);
	expected.thumb = 0.f;
	curlCheckAll("thumbs-up", expected,
	             vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.f, 1.f, 1.f))));
	// Only the block closed: the index finger and the thumb straight
	expected = vr::FingerCurl();
	expected.fingersRoot = limits.fingersRoot;
	expected.fingersTip = limits.fingersTip;
	curlCheckAll("block only", expected,
	             vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.f, 0.f, 1.f))));
}

void FingerCurlTest::testHalfClosedIsBetween() {
	const vr::FingerCurlLimits limits;
	const vr::FingerCurl quarter = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.25f, 0.25f, 0.25f)));
	const vr::FingerCurl half = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, handShape(0.5f, 0.5f, 0.5f)));
	// The knuckle is linear in the real angle: a half-closed knuckle is half the limit
	curlCheck("half knuckle", limits.indexRoot * 0.5f, half.indexRoot);
	curlCheck("half knuckles", limits.fingersRoot * 0.5f, half.fingersRoot);
	curlCheck("quarter knuckle", limits.indexRoot * 0.25f, quarter.indexRoot);
	// The rest is monotonic and strictly between open and the fist
	CPPUNIT_ASSERT(quarter.thumb > 1.f && quarter.thumb < half.thumb && half.thumb < limits.thumb - 1.f);
	CPPUNIT_ASSERT(quarter.indexTip > 1.f && quarter.indexTip < half.indexTip && half.indexTip < limits.indexTip - 1.f);
	CPPUNIT_ASSERT(quarter.fingersTip > 1.f && quarter.fingersTip < half.fingersTip && half.fingersTip < limits.fingersTip - 1.f);
}

void FingerCurlTest::testCurlIgnoresTheSpaceAndTheHand() {
	const vr::HandShape shape = handShape(0.6f, 0.3f, 0.8f);
	const vr::FingerCurl reference = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, shape));
	const glm::quat turn = glm::normalize(glm::quat(0.7f, 0.3f, -0.5f, 0.4f));
	const vr::FingerCurl placed = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, shape, Vec3f(-2.f, 1.f, 5.f), turn));
	curlCheckAll("placed hand", reference, placed, 0.01f);
	const vr::FingerCurl left = vr::fingerCurlFromJoints(vr::synthesizeHandJoints(vr::Hand::Left, shape));
	curlCheckAll("left hand", reference, left, 0.01f);
}

void FingerCurlTest::testControlsFallback() {
	const vr::FingerCurlLimits limits;
	curlCheckAll("nothing pressed", vr::FingerCurl(), vr::fingerCurlFromControls(0.f, 0.f, false));
	vr::FingerCurl expected;
	expected.indexRoot = limits.indexRoot;
	expected.indexTip = limits.indexTip;
	curlCheckAll("trigger pulled", expected, vr::fingerCurlFromControls(1.f, 0.f, false));
	expected = vr::FingerCurl();
	expected.fingersRoot = limits.fingersRoot * 0.5f;
	expected.fingersTip = limits.fingersTip * 0.5f;
	curlCheckAll("grip half", expected, vr::fingerCurlFromControls(0.f, 0.5f, false));
	expected = vr::FingerCurl();
	expected.thumb = limits.thumb;
	curlCheckAll("thumb resting", expected, vr::fingerCurlFromControls(0.f, 0.f, true));
	curlCheckAll("everything, clamped", vr::fingerCurlClosed(limits), vr::fingerCurlFromControls(1.5f, 2.f, true));
	curlCheckAll("negative values clamp to open", vr::FingerCurl(), vr::fingerCurlFromControls(-1.f, -0.5f, false));
}

void FingerCurlTest::testClosedHand() {
	vr::FingerCurlLimits limits;
	limits.thumb = 55.f;
	limits.fingersTip = 80.f;
	const vr::FingerCurl closed = vr::fingerCurlClosed(limits);
	curlCheck("thumb", 55.f, closed.thumb);
	curlCheck("indexRoot", limits.indexRoot, closed.indexRoot);
	curlCheck("indexTip", limits.indexTip, closed.indexTip);
	curlCheck("fingersRoot", limits.fingersRoot, closed.fingersRoot);
	curlCheck("fingersTip", 80.f, closed.fingersTip);
}

void FingerCurlTest::testApproachStepsTowardsTheTarget() {
	const vr::FingerCurl closed = vr::fingerCurlClosed();
	// One step of 30 degrees towards the fist: every segment moves 30, none past its target
	vr::FingerCurl step = vr::approachCurl(vr::FingerCurl(), closed, 30.f);
	curlCheck("thumb step", 30.f, step.thumb);
	curlCheck("indexRoot step", 30.f, step.indexRoot);
	curlCheck("fingersTip step", 30.f, step.fingersTip);
	// A big step lands on the target exactly
	curlCheckAll("big step", closed, vr::approachCurl(vr::FingerCurl(), closed, 1000.f));
	// Back towards open from the fist
	step = vr::approachCurl(closed, vr::FingerCurl(), 50.f);
	curlCheck("thumb back", 10.f, step.thumb);
	curlCheck("indexTip back", 45.f, step.indexTip);
	// No step: nothing moves; a negative step is no step
	curlCheckAll("zero step", vr::FingerCurl(), vr::approachCurl(vr::FingerCurl(), closed, 0.f));
	curlCheckAll("negative step", vr::FingerCurl(), vr::approachCurl(vr::FingerCurl(), closed, -5.f));
}

// ---- Five-finger pose (#385, 385.2) ----

namespace {

//! Joint angles are exact geometry in float: a couple of degrees is generous
const float PoseEpsilon = 2.f;

const char * const FingerNames[] = { "thumb", "index", "middle", "ring", "little" };

float & shapeOf(vr::HandShape & shape, size_t finger) {
	float * fields[] = { &shape.thumb, &shape.index, &shape.middle, &shape.ring, &shape.little };
	return *fields[finger];
}

//! The synthesized angle at each joint of a fully closed finger (the thumb's metacarpal joint has no synthesized bend)
const float ClosedAngles[5][3] = {
	{ 0.f, 50.f, 70.f }, { 90.f, 100.f, 70.f }, { 90.f, 100.f, 70.f }, { 90.f, 100.f, 70.f }, { 90.f, 100.f, 70.f },
};

float poseFlex(const vr::FingerPose & pose, size_t finger, size_t joint) {
	return pose.flex[finger][joint];
}

//! Compares every joint; the thumb's metacarpal joint is skipped (the synthesized hand does not define it)
void poseCheck(const char * what, const vr::FingerPose & expected, const vr::FingerPose & actual, float tolerance) {
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = (finger == 0) ? 1 : 0; joint < 3; joint++) {
			curlCheck((std::string(what) + ": " + FingerNames[finger] + " joint " + std::to_string(joint)).c_str(),
			          poseFlex(expected, finger, joint), poseFlex(actual, finger, joint), tolerance);
		}
	}
}

} // anonymous namespace

void FingerCurlTest::testPoseEachFingerClosedAlone() {
	for(size_t closed = 0; closed < 5; closed++) {
		vr::HandShape shape;
		shapeOf(shape, closed) = 1.f;
		const vr::HandJoints joints = vr::synthesizeHandJoints(vr::Hand::Right, shape);
		CPPUNIT_ASSERT(joints.valid);
		const vr::FingerPose pose = vr::fingerPoseFromJoints(joints);
		for(size_t finger = 0; finger < 5; finger++) {
			for(size_t joint = (finger == 0) ? 1 : 0; joint < 3; joint++) {
				const float expected = (finger == closed) ? ClosedAngles[finger][joint] : 0.f;
				curlCheck((std::string(FingerNames[closed]) + " closed alone: " + FingerNames[finger] + " joint "
				           + std::to_string(joint)).c_str(), expected, poseFlex(pose, finger, joint), PoseEpsilon);
			}
		}
	}
}

void FingerCurlTest::testPoseOpenAndInvalidHand() {
	vr::HandShape open;
	const vr::FingerPose pose = vr::fingerPoseFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, open));
	poseCheck("open hand", vr::FingerPose(), pose, PoseEpsilon);
	// Joints that were not located: an open hand, never garbage - including the thumb's first joint and the spread
	vr::HandShape fist;
	fist.thumb = fist.index = fist.middle = fist.ring = fist.little = 1.f;
	vr::HandJoints none = vr::synthesizeHandJoints(vr::Hand::Right, fist);
	none.valid = false;
	const vr::FingerPose invalid = vr::fingerPoseFromJoints(none);
	for(size_t finger = 0; finger < 5; finger++) {
		curlCheck("invalid joints: spread", 0.f, invalid.spread[finger], 0.0001f);
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("invalid joints: flex", 0.f, poseFlex(invalid, finger, joint), 0.0001f);
		}
	}
	// A real fist reaches the synthesized angles on every finger
	const vr::FingerPose closed = vr::fingerPoseFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, fist));
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = (finger == 0) ? 1 : 0; joint < 3; joint++) {
			curlCheck("fist", ClosedAngles[finger][joint], poseFlex(closed, finger, joint), PoseEpsilon);
		}
	}
}

void FingerCurlTest::testPoseIgnoresTheSpaceAndTheHand() {
	vr::HandShape shape;
	shape.thumb = 0.6f;
	shape.index = 0.3f;
	shape.middle = 0.8f;
	shape.ring = 0.5f;
	shape.little = 1.f;
	const vr::FingerPose reference = vr::fingerPoseFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, shape));
	// The five fingers really differ from each other (the test would pass on a constant otherwise)
	CPPUNIT_ASSERT(poseFlex(reference, 2, 0) > poseFlex(reference, 3, 0) + 10.f);
	CPPUNIT_ASSERT(poseFlex(reference, 3, 0) > poseFlex(reference, 1, 0) + 10.f);
	const glm::quat turn = glm::normalize(glm::quat(0.7f, 0.3f, -0.5f, 0.4f));
	const vr::FingerPose placed = vr::fingerPoseFromJoints(
		vr::synthesizeHandJoints(vr::Hand::Right, shape, Vec3f(-2.f, 1.f, 5.f), turn));
	poseCheck("placed hand", reference, placed, 0.5f);
	const vr::FingerPose left = vr::fingerPoseFromJoints(vr::synthesizeHandJoints(vr::Hand::Left, shape));
	poseCheck("left hand", reference, left, 0.5f);
	const vr::FingerPose leftPlaced = vr::fingerPoseFromJoints(
		vr::synthesizeHandJoints(vr::Hand::Left, shape, Vec3f(3.f, -1.f, 0.5f), turn));
	poseCheck("left hand placed", reference, leftPlaced, 0.5f);
}

void FingerCurlTest::testPoseControlsFallback() {
	const vr::FingerPose closed = vr::fingerPoseClosed();
	const size_t Thumb = size_t(vr::Finger::Thumb), Index = size_t(vr::Finger::Index);
	const size_t Middle = size_t(vr::Finger::Middle), Little = size_t(vr::Finger::Little);

	// Nothing pressed: open; negative values clamp to open, values past 1 to the limit
	poseCheck("nothing pressed", vr::FingerPose(), vr::fingerPoseFromControls(0.f, 0.f, false), 0.001f);
	poseCheck("negative values", vr::FingerPose(), vr::fingerPoseFromControls(-1.f, -0.5f, false), 0.001f);
	poseCheck("everything, clamped", closed, vr::fingerPoseFromControls(1.5f, 2.f, true), 0.001f);

	// The trigger is the index finger and only it
	const vr::FingerPose trigger = vr::fingerPoseFromControls(1.f, 0.f, false);
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("trigger", (finger == Index) ? poseFlex(closed, finger, joint) : 0.f,
			          poseFlex(trigger, finger, joint), 0.001f);
		}
	}
	CPPUNIT_ASSERT(poseFlex(trigger, Index, 0) > 1.f);

	// The grip closes the middle, ring and little finger and not the index finger or the thumb
	const vr::FingerPose grip = vr::fingerPoseFromControls(0.f, 1.f, false);
	for(size_t finger = 0; finger < 5; finger++) {
		const bool gripped = finger >= Middle && finger <= Little;
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("grip", gripped ? poseFlex(closed, finger, joint) : 0.f, poseFlex(grip, finger, joint), 0.001f);
		}
	}
	// Half a grip is strictly between open and closed
	const vr::FingerPose half = vr::fingerPoseFromControls(0.f, 0.5f, false);
	for(size_t finger = Middle; finger <= Little; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			CPPUNIT_ASSERT(poseFlex(half, finger, joint) > 0.5f);
			CPPUNIT_ASSERT(poseFlex(half, finger, joint) < poseFlex(closed, finger, joint) - 0.5f);
		}
	}

	// A thumb resting on a control is the thumb and only it
	const vr::FingerPose thumb = vr::fingerPoseFromControls(0.f, 0.f, true);
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("thumb resting", (finger == Thumb) ? poseFlex(closed, finger, joint) : 0.f,
			          poseFlex(thumb, finger, joint), 0.001f);
		}
	}
	CPPUNIT_ASSERT(poseFlex(thumb, Thumb, 1) > 1.f);
}

void FingerCurlTest::testPoseClosed() {
	// A held weapon closes all five fingers: every joint of every finger bends to a real limit
	const vr::FingerPose closed = vr::fingerPoseClosed();
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			const float flex = poseFlex(closed, finger, joint);
			CPPUNIT_ASSERT_MESSAGE(std::string(FingerNames[finger]) + " joint " + std::to_string(joint) + " is closed",
			                       flex >= 20.f && flex <= 120.f);
		}
	}
	// The same pose from the joints of a fist and from full controls
	vr::HandShape fist;
	fist.thumb = fist.index = fist.middle = fist.ring = fist.little = 1.f;
	poseCheck("fist joints", closed, vr::fingerPoseFromJoints(vr::synthesizeHandJoints(vr::Hand::Right, fist)), PoseEpsilon);
	poseCheck("full controls", closed, vr::fingerPoseFromControls(1.f, 1.f, true), 0.001f);
}

void FingerCurlTest::testPoseApproach() {
	const vr::FingerPose closed = vr::fingerPoseClosed();
	const vr::FingerPose open;
	// One step of 30 degrees towards the fist: every joint moves 30 or lands on a smaller target
	const vr::FingerPose step = vr::approachPose(open, closed, 30.f);
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("step", std::min(30.f, poseFlex(closed, finger, joint)), poseFlex(step, finger, joint), 0.001f);
		}
	}
	// A big step lands on the target exactly
	poseCheck("big step", closed, vr::approachPose(open, closed, 1000.f), 0.001f);
	// Back towards open from the fist: at most 50 per joint, none past open
	const vr::FingerPose back = vr::approachPose(closed, open, 50.f);
	for(size_t finger = 0; finger < 5; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			curlCheck("back", std::max(0.f, poseFlex(closed, finger, joint) - 50.f), poseFlex(back, finger, joint), 0.001f);
		}
	}
	// No step and a negative step: nothing moves
	poseCheck("zero step", open, vr::approachPose(open, closed, 0.f), 0.001f);
	poseCheck("negative step", open, vr::approachPose(open, closed, -5.f), 0.001f);
	// Repeated steps arrive and stay
	vr::FingerPose now = open;
	for(int i = 0; i < 20; i++) {
		now = vr::approachPose(now, closed, 10.f);
	}
	poseCheck("repeated steps", closed, now, 0.001f);
}
