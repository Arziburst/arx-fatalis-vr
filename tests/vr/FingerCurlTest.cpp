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
	shape.fingers = fingers;
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
