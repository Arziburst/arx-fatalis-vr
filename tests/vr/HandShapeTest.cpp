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

#include "vr/HandShapeTest.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<HandShapeTest> g_registerHandShapeTest;

namespace {

// Helpers are prefixed hst: the test sources are one unity build

void hstExpectNear(const std::string & what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

std::string hstReadSource(const std::string & relative) {
#ifdef ARX_TEST_SOURCE_DIR
	std::string path = std::string(ARX_TEST_SOURCE_DIR) + "/" + relative;
	std::ifstream file(path.c_str(), std::ios::binary);
	if(!file) {
		CPPUNIT_FAIL("cannot open " + path);
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
#else
	(void)relative;
	CPPUNIT_FAIL("ARX_TEST_SOURCE_DIR is not defined for arxtest");
	return std::string();
#endif
}

vr::HandJoints hstGrab() {
	const std::string json = hstReadSource("data/core/vr/hands/ArticulatedHandPose_Grab.json");
	vr::HandJoints joints;
	std::string error;
	CPPUNIT_ASSERT_MESSAGE("Grab: " + error, vr::loadHandShape(json.data(), json.size(), joints, error));
	return joints;
}

//! The joints that have a bone to the next joint of their finger
std::vector<size_t> hstBoneJoints() {
	std::vector<size_t> joints;
	for(size_t j = size_t(vr::HandJoint::ThumbMetacarpal); j < size_t(vr::HandJoint::ThumbTip); j++) {
		joints.push_back(j);
	}
	for(size_t finger = 0; finger < 4; finger++) {
		const size_t root = size_t(vr::HandJoint::IndexMetacarpal) + 5 * finger;
		for(size_t j = root; j < root + 4; j++) {
			joints.push_back(j);
		}
	}
	return joints;
}

void hstExpectFramesAlongBones(const std::string & what, const vr::HandJoints & joints, float tolerance) {
	for(size_t j : hstBoneJoints()) {
		const Vec3f bone = glm::normalize(joints.position[j + 1] - joints.position[j]);
		const Vec3f minusZ = joints.orientation[j] * Vec3f(0.f, 0.f, -1.f);
		hstExpectNear(what + " joint " + std::to_string(j) + " -Z along its bone", 1.f, glm::dot(bone, minusZ), tolerance);
	}
}

void hstExpectSameJoints(const std::string & what, const vr::HandJoints & a, const vr::HandJoints & b, float tolerance) {
	for(size_t j = 0; j < vr::HandJointCount; j++) {
		hstExpectNear(what + " position " + std::to_string(j), 0.f, glm::distance(a.position[j], b.position[j]), tolerance);
		hstExpectNear(what + " orientation " + std::to_string(j), 1.f, std::abs(glm::dot(a.orientation[j], b.orientation[j])),
		              tolerance);
	}
}

vr::HandShape hstShape(float thumb, float index, float middle, float ring, float little) {
	vr::HandShape shape;
	shape.thumb = thumb;
	shape.index = index;
	shape.middle = middle;
	shape.ring = ring;
	shape.little = little;
	return shape;
}

vr::CustomHandShapes hstShapes(vr::Hand hand) {
	vr::CustomHandShapes shapes;
	shapes.open = vr::palmRelativeJoints(vr::synthesizeHandJoints(hand, vr::HandShape()));
	const vr::HandJoints grab = hstGrab();
	shapes.grab = vr::palmRelativeJoints(hand == vr::Hand::Right ? grab : vr::mirrorHandJoints(grab));
	shapes.hilt = vr::palmRelativeJoints(vr::hiltPose(hand, vr::DefaultHiltRadius));
	return shapes;
}

const vr::Hand hstHands[] = { vr::Hand::Left, vr::Hand::Right };

} // anonymous namespace

void HandShapeTest::testFramesRunAlongTheBones() {
	for(vr::Hand hand : hstHands) {
		hstExpectFramesAlongBones("synthesized fist", vr::synthesizeHandJoints(hand, hstShape(1, 1, 1, 1, 1)), 1e-4f);
		hstExpectFramesAlongBones("synthesized half", vr::synthesizeHandJoints(hand, hstShape(0.5f, 0.3f, 0.6f, 0.2f, 0.9f),
		                          Vec3f(0.2f, 1.1f, -0.3f), glm::angleAxis(1.f, glm::normalize(Vec3f(1.f, 2.f, 3.f)))), 1e-4f);
		hstExpectFramesAlongBones("hilt pose", vr::hiltPose(hand, vr::DefaultHiltRadius), 1e-4f);
	}
	for(char side : { 'L', 'R' }) {
		const std::string data = hstReadSource(std::string("data/core/vr/arms/Hand_Glove_low_") + side + ".gltf");
		vr::SkinnedMesh mesh;
		std::string error;
		CPPUNIT_ASSERT_MESSAGE("glove: " + error, vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error));
		const vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT(rig.valid);
		hstExpectFramesAlongBones(std::string("glove rest ") + side, vr::restHandJoints(mesh, rig), 1e-4f);
	}
	hstExpectFramesAlongBones("MRTK3 Grab", hstGrab(), 1e-3f);
}

void HandShapeTest::testLeftHandMirrorsTheRight() {
	const vr::HandShape shape = hstShape(0.4f, 0.7f, 0.2f, 1.f, 0.5f);
	hstExpectSameJoints("synthesized", vr::mirrorHandJoints(vr::synthesizeHandJoints(vr::Hand::Right, shape)),
	                    vr::synthesizeHandJoints(vr::Hand::Left, shape), 1e-5f);
	hstExpectSameJoints("hilt pose", vr::mirrorHandJoints(vr::hiltPose(vr::Hand::Right, 0.02f)),
	                    vr::hiltPose(vr::Hand::Left, 0.02f), 1e-5f);
}

void HandShapeTest::testHandShapeJsonRoundTrip() {
	const vr::HandJoints joints = vr::synthesizeHandJoints(vr::Hand::Left, hstShape(0.3f, 0.6f, 0.9f, 0.1f, 0.4f),
	                                                       Vec3f(0.5f, -1.f, 2.f),
	                                                       glm::angleAxis(2.f, glm::normalize(Vec3f(-1.f, 0.5f, 0.2f))));
	const std::string json = vr::handShapeJson(joints);
	vr::HandJoints back;
	std::string error;
	CPPUNIT_ASSERT_MESSAGE("reload: " + error, vr::loadHandShape(json.data(), json.size(), back, error));
	CPPUNIT_ASSERT(back.valid);
	hstExpectSameJoints("round trip", joints, back, 1e-5f);
}

void HandShapeTest::testShippedGrabLoads() {
	const vr::HandJoints grab = hstGrab();
	CPPUNIT_ASSERT(grab.valid);
	// The bends measured on the file's positions (finger-five.xrs derives its controls thresholds from them)
	const vr::FingerPose pose = vr::fingerPoseFromJoints(grab);
	hstExpectNear("Grab index knuckle", 64.5f, pose.flex[size_t(vr::Finger::Index)][0], 1.f);
	hstExpectNear("Grab middle knuckle", 72.9f, pose.flex[size_t(vr::Finger::Middle)][0], 1.f);
	hstExpectNear("Grab ring knuckle", 74.8f, pose.flex[size_t(vr::Finger::Ring)][0], 1.f);
	hstExpectNear("Grab little knuckle", 73.2f, pose.flex[size_t(vr::Finger::Little)][0], 1.f);
	// Recorded for the right hand: its fingers close toward the palm's face, which is -X of the right palm's frame
	// turned by the frame's +Y out of the back of the hand: the curl is toward the palm's -Y
	const vr::HandJoints relative = vr::palmRelativeJoints(grab);
	CPPUNIT_ASSERT_MESSAGE("the Grab index tip curls toward the palm", relative[vr::HandJoint::IndexTip].y < 0.f);
}

void HandShapeTest::testHiltPoseKeepsTheAnglesAtTheDefaultRadius() {
	vr::HiltAngles research;
	research.knuckle = 55.f;
	research.middle = 75.f;
	research.last = 35.f;
	for(const vr::HiltAngles & hilt : { vr::HiltAngles(), research }) {
		for(vr::Hand hand : hstHands) {
			const vr::HandJoints pose = vr::hiltPose(hand, vr::DefaultHiltRadius, hilt);
			const vr::FingerPose read = vr::fingerPoseFromJoints(pose);
			for(size_t finger = size_t(vr::Finger::Index); finger < vr::FingerCount; finger++) {
				const std::string name = "finger " + std::to_string(finger);
				hstExpectNear(name + " knuckle", hilt.knuckle, read.flex[finger][0], 0.5f);
				hstExpectNear(name + " middle", hilt.middle, read.flex[finger][1], 0.5f);
				hstExpectNear(name + " last", hilt.last, read.flex[finger][2], 0.5f);
			}
			// The thumb's outer joints at the configured bend
			hstExpectNear("thumb proximal", hilt.thumb, read.flex[size_t(vr::Finger::Thumb)][1], 1.f);
			hstExpectNear("thumb distal", hilt.thumb, read.flex[size_t(vr::Finger::Thumb)][2], 1.f);
		}
	}
}

void HandShapeTest::testHiltAnglesAreClamped() {
	vr::HiltAngles wild;
	wild.knuckle = -5.f;
	wild.middle = 200.f;
	wild.last = std::numeric_limits<float>::quiet_NaN();
	wild.thumb = 500.f;
	const vr::HiltAngles clamped = vr::clampHiltAngles(wild);
	hstExpectNear("knuckle", 0.f, clamped.knuckle, 0.f);
	hstExpectNear("middle", vr::FingerPoseLimits().flex[size_t(vr::Finger::Index)][1], clamped.middle, 0.f);
	hstExpectNear("last (NaN: the default)", vr::HiltAngles().last, clamped.last, 0.f);
	hstExpectNear("thumb", 90.f, clamped.thumb, 0.f);
}

void HandShapeTest::testCustomHandSourceOrder() {
	for(vr::Hand hand : hstHands) {
		vr::CustomHandShapes shapes = hstShapes(hand);
		const vr::HandJoints fist = vr::synthesizeHandJoints(hand, hstShape(1, 1, 1, 1, 1));
		vr::CustomFingerInput input;
		input.controls = true;

		// A held weapon: the default hilt pose, whatever the runtime's joints say
		input.holds = true;
		vr::CustomHandTarget target = vr::customHandTarget(input, vr::synthesizeHandJoints(hand, vr::HandShape()), shapes);
		CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::Held);
		CPPUNIT_ASSERT(target.hilt == vr::HiltPoseSource::Default);
		hstExpectNear("held knuckle", vr::HiltAngles().knuckle, target.pose.flex[size_t(vr::Finger::Index)][0], 0.5f);

		// A captured hilt pose comes first
		shapes.captured = vr::palmRelativeJoints(fist);
		target = vr::customHandTarget(input, vr::HandJoints(), shapes);
		CPPUNIT_ASSERT(target.hilt == vr::HiltPoseSource::Captured);
		hstExpectNear("captured knuckle", 90.f, target.pose.flex[size_t(vr::Finger::Index)][0], 0.5f);

		// No weapon: the runtime's joints, else the controls, else nothing
		input.holds = false;
		target = vr::customHandTarget(input, fist, shapes);
		CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::Joints);
		CPPUNIT_ASSERT(target.hilt == vr::HiltPoseSource::None);
		hstExpectSameJoints("joints", vr::palmRelativeJoints(fist), target.joints, 1e-5f);
		target = vr::customHandTarget(input, vr::HandJoints(), shapes);
		CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::Controls);
		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			for(size_t joint = 0; joint < 3; joint++) {
				CPPUNIT_ASSERT(target.pose.flex[finger][joint] < 10.f);
			}
		}
		input.controls = false;
		target = vr::customHandTarget(input, vr::HandJoints(), shapes);
		CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::None);
		hstExpectSameJoints("none is open", shapes.open, target.joints, 1e-6f);
	}
}

void HandShapeTest::testControlsBlendTowardGrab() {
	for(vr::Hand hand : hstHands) {
		const vr::CustomHandShapes shapes = hstShapes(hand);
		const vr::FingerPose grab = vr::fingerPoseFromJoints(shapes.grab);
		vr::CustomFingerInput input;
		input.controls = true;
		input.trigger = 1.f;
		vr::CustomHandTarget target = vr::customHandTarget(input, vr::HandJoints(), shapes);
		for(size_t joint = 0; joint < 3; joint++) {
			hstExpectNear("full trigger: the index as Grab's", grab.flex[size_t(vr::Finger::Index)][joint],
			              target.pose.flex[size_t(vr::Finger::Index)][joint], 1.f);
			CPPUNIT_ASSERT(target.pose.flex[size_t(vr::Finger::Middle)][joint] < 1.f);
		}
		input.trigger = 0.f;
		input.grip = 0.4f;
		target = vr::customHandTarget(input, vr::HandJoints(), shapes);
		hstExpectNear("grip 0.4: the middle knuckle", 0.4f * grab.flex[size_t(vr::Finger::Middle)][0],
		              target.pose.flex[size_t(vr::Finger::Middle)][0], 4.f);
		CPPUNIT_ASSERT(target.pose.flex[size_t(vr::Finger::Index)][0] < 1.f);
	}
}
