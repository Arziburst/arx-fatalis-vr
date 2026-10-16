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

#include "vr/HandConformingTest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<HandConformingTest> g_registerHandConformingTest;

namespace {

// Helpers are prefixed hct: the test sources are one unity build

void hctExpectNear(const std::string & what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

glm::vec3 hctPos(const vr::HandJoints & joints, vr::HandJoint joint) {
	Vec3f p = joints[joint];
	return glm::vec3(p.x, p.y, p.z);
}

const vr::Hand hctHands[] = { vr::Hand::Left, vr::Hand::Right };

//! A hand-shape JSON of \a joints joints: joint i at (0.01 i, 0.002 i, 0) with a rotation of 10 i degrees about Z
//! (the MRTK hand-pose layout: items of { joint, pose { position, rotation } }, the OpenXR joint order)
std::string hctShapeJson(int joints) {
	std::string json = "{\"items\":[";
	for(int i = 0; i < joints; i++) {
		float half = glm::radians(10.f * float(i)) * 0.5f;
		if(i) {
			json += ",";
		}
		json += "{\"joint\":" + std::to_string(i) + ",\"pose\":{\"position\":{\"x\":" + std::to_string(0.01f * float(i))
		        + ",\"y\":" + std::to_string(0.002f * float(i)) + ",\"z\":0.0},\"rotation\":{\"x\":0.0,\"y\":0.0,\"z\":"
		        + std::to_string(std::sin(half)) + ",\"w\":" + std::to_string(std::cos(half)) + "}}}";
	}
	return json + "]}";
}

std::string hctReadGlove(char side) {
#ifdef ARX_TEST_SOURCE_DIR
	std::string path = std::string(ARX_TEST_SOURCE_DIR) + "/data/core/vr/arms/Hand_Glove_low_" + side + ".gltf";
	std::ifstream file(path.c_str(), std::ios::binary);
	if(!file) {
		CPPUNIT_FAIL("cannot open " + path);
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
#else
	(void)side;
	CPPUNIT_FAIL("ARX_TEST_SOURCE_DIR is not defined for arxtest");
	return std::string();
#endif
}

vr::SkinnedMesh hctLoadGlove(char side) {
	std::string data = hctReadGlove(side);
	vr::SkinnedMesh mesh;
	std::string error;
	bool ok = vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error);
	CPPUNIT_ASSERT_MESSAGE("load failed: " + error, ok);
	return mesh;
}

std::vector<glm::mat4> hctWorld(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & local) {
	CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), local.size());
	std::vector<glm::mat4> world;
	for(size_t i = 0; i < local.size(); i++) {
		glm::mat4 m = local[i];
		for(int parent = mesh.skeleton[i].parent; parent >= 0; parent = mesh.skeleton[size_t(parent)].parent) {
			m = local[size_t(parent)] * m;
		}
		world.push_back(m);
	}
	return world;
}

std::vector<glm::mat4> hctRestLocals(const vr::SkinnedMesh & mesh) {
	std::vector<glm::mat4> local;
	for(const auto & joint : mesh.skeleton) {
		local.push_back(joint.restLocal);
	}
	return local;
}

/*!
 * A rest-pose joint set of a glove: what a runtime would report for a hand in the glove's rest - the rest world
 * positions and the glove's rest frames in the OpenXR joint convention (restHandJoints)
 */
vr::HandJoints hctRestJoints(const vr::SkinnedMesh & mesh) {
	std::vector<glm::mat4> world = hctWorld(mesh, hctRestLocals(mesh));
	vr::HandRig rig = vr::buildHandRig(mesh);
	CPPUNIT_ASSERT(rig.valid);
	vr::HandJoints joints = vr::restHandJoints(mesh, rig);
	CPPUNIT_ASSERT(joints.valid);
	for(size_t j = 0; j < vr::HandJointCount; j++) {
		int index = vr::findHandJoint(mesh, vr::HandJoint(j));
		CPPUNIT_ASSERT(index >= 0);
		glm::vec3 p(world[size_t(index)][3]);
		hctExpectNear("rest joint position", 0.f, glm::length(hctPos(joints, vr::HandJoint(j)) - p), 1e-5f);
		hctExpectNear("rest joint frame", 1.f, glm::length(joints.orientation[j]), 1e-3f);
	}
	return joints;
}

//! The distance of a point from the grip axis (Z in the OpenXR grip frame)
float hctAxisDistance(const glm::vec3 & p) {
	return std::sqrt(p.x * p.x + p.y * p.y);
}

const vr::HandJoint hctFingertips[] = {
	vr::HandJoint::IndexTip, vr::HandJoint::MiddleTip, vr::HandJoint::RingTip, vr::HandJoint::LittleTip
};

} // anonymous namespace

void HandConformingTest::testJointsKeepIdentityOrientationsByDefault() {
	vr::HandJoints joints;
	CPPUNIT_ASSERT_EQUAL(size_t(26), joints.orientation.size());
	for(const glm::quat & q : joints.orientation) {
		hctExpectNear("w", 1.f, q.w, 1e-6f);
		hctExpectNear("x", 0.f, q.x, 1e-6f);
		hctExpectNear("y", 0.f, q.y, 1e-6f);
		hctExpectNear("z", 0.f, q.z, 1e-6f);
	}
}

void HandConformingTest::testLoadHandShapeReadsTwentySixJoints() {
	std::string json = hctShapeJson(26);
	vr::HandJoints joints;
	std::string error;
	bool ok = vr::loadHandShape(json.data(), json.size(), joints, error);
	CPPUNIT_ASSERT_MESSAGE("load failed: " + error, ok);
	CPPUNIT_ASSERT(joints.valid);
	// Handedness of the source (a Unity space) may be converted; distances and rotation angles are not
	glm::vec3 a = hctPos(joints, vr::HandJoint::Palm);
	glm::vec3 b = hctPos(joints, vr::HandJoint::IndexTip);
	hctExpectNear("palm to index tip", glm::length(glm::vec3(0.1f, 0.02f, 0.f)), glm::length(b - a), 1e-4f);
	hctExpectNear("palm to wrist", glm::length(glm::vec3(0.01f, 0.002f, 0.f)),
	              glm::length(hctPos(joints, vr::HandJoint::Wrist) - a), 1e-4f);
	for(const glm::quat & q : joints.orientation) {
		hctExpectNear("unit rotation", 1.f, glm::length(q), 1e-3f);
	}
	// The index tip (joint 10) is rotated 100 degrees: |w| = cos(50)
	hctExpectNear("rotation angle", std::cos(glm::radians(50.f)),
	              std::abs(joints.orientation[size_t(vr::HandJoint::IndexTip)].w), 1e-3f);
	hctExpectNear("identity joint", 1.f, std::abs(joints.orientation[0].w), 1e-3f);
}

void HandConformingTest::testLoadHandShapeRefusesMalformedInput() {
	const std::string bad[] = {
		std::string(),
		"not json at all",
		"{\"items\":[",
		hctShapeJson(25),
		"{\"items\":[]}",
	};
	for(const std::string & json : bad) {
		vr::HandJoints joints;
		std::string error;
		CPPUNIT_ASSERT_MESSAGE("accepted: " + json.substr(0, 30), !vr::loadHandShape(json.data(), json.size(), joints, error));
		CPPUNIT_ASSERT_MESSAGE("no error text", !error.empty());
		CPPUNIT_ASSERT(!joints.valid);
	}
}

void HandConformingTest::testHiltPoseFingertipsStayOutsideTheHilt() {
	const float radii[] = { 0.012f, 0.02f, 0.03f };
	for(vr::Hand hand : hctHands) {
		float previous = 0.f;
		for(float radius : radii) {
			vr::HandJoints pose = vr::hiltPose(hand, radius);
			CPPUNIT_ASSERT(pose.valid);
			float sum = 0.f;
			for(vr::HandJoint tip : hctFingertips) {
				float distance = hctAxisDistance(hctPos(pose, tip));
				// Within a millimetre of the surface at least: never through the handle
				CPPUNIT_ASSERT_MESSAGE("fingertip inside the hilt, radius " + std::to_string(radius) + ": "
				                       + std::to_string(distance), distance >= radius - 0.001f);
				sum += distance;
			}
			// A thicker hilt is gripped wider
			CPPUNIT_ASSERT_MESSAGE("a bigger hilt does not open the hand", sum > previous);
			previous = sum;
		}
	}
}

void HandConformingTest::testHiltPoseThumbLiesAcrossTheIndexFinger() {
	// Grip frame: the thumb side is -Z, the little finger's +Z. Across = the thumb tip has crossed to the index
	// finger's middle segment (not out to the side of the hand), and sits no closer to the axis than the hilt.
	for(vr::Hand hand : hctHands) {
		const float radius = 0.02f;
		vr::HandJoints pose = vr::hiltPose(hand, radius);
		vr::HandJoints open = vr::synthesizeHandJoints(hand, vr::HandShape());
		glm::vec3 thumb = hctPos(pose, vr::HandJoint::ThumbTip);
		glm::vec3 index = hctPos(pose, vr::HandJoint::IndexIntermediate);
		glm::vec3 middle = hctPos(pose, vr::HandJoint::MiddleIntermediate);
		CPPUNIT_ASSERT(hctAxisDistance(thumb) >= radius - 0.001f);
		float across = std::abs(thumb.z - index.z);
		float openSide = std::abs(hctPos(open, vr::HandJoint::ThumbTip).z - index.z);
		CPPUNIT_ASSERT_MESSAGE("the thumb stays out to the side", across < openSide);
		// Over the index and middle finger, not out on the thumb side and not beyond the middle finger
		float lowest = std::min(index.z, middle.z);
		float highest = std::max(index.z, middle.z);
		CPPUNIT_ASSERT_MESSAGE("thumb tip out on the thumb side: " + std::to_string(thumb.z), thumb.z >= lowest - 0.01f);
		CPPUNIT_ASSERT_MESSAGE("thumb tip past the middle finger: " + std::to_string(thumb.z), thumb.z <= highest + 0.01f);
	}
}

void HandConformingTest::testHiltPoseLeftMirrorsRight() {
	vr::HandJoints left = vr::hiltPose(vr::Hand::Left, 0.02f);
	vr::HandJoints right = vr::hiltPose(vr::Hand::Right, 0.02f);
	for(size_t j = 0; j < vr::HandJointCount; j++) {
		hctExpectNear("x", -right.position[j].x, left.position[j].x, 1e-4f);
		hctExpectNear("y", right.position[j].y, left.position[j].y, 1e-4f);
		hctExpectNear("z", right.position[j].z, left.position[j].z, 1e-4f);
	}
}

void HandConformingTest::testRestJointsReproduceTheGlovesRestLocals() {
	for(char side : { 'L', 'R' }) {
		vr::SkinnedMesh mesh = hctLoadGlove(side);
		vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT(rig.valid);
		vr::HandJoints joints = hctRestJoints(mesh);
		std::vector<glm::mat4> local;
		vr::poseHandFromJoints(mesh, rig, joints, local);
		CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), local.size());
		for(size_t i = 0; i < local.size(); i++) {
			for(int c = 0; c < 4; c++) {
				for(int r = 0; r < 3; r++) {
					hctExpectNear("rest local " + std::to_string(i), mesh.skeleton[i].restLocal[c][r], local[i][c][r], 2e-3f);
				}
			}
		}
	}
}

void HandConformingTest::testTurningAFingerJointMovesOnlyThatFinger() {
	vr::SkinnedMesh mesh = hctLoadGlove('R');
	vr::HandRig rig = vr::buildHandRig(mesh);
	CPPUNIT_ASSERT(rig.valid);
	vr::HandJoints rest = hctRestJoints(mesh);
	std::vector<glm::mat4> before = hctWorld(mesh, hctRestLocals(mesh));

	// The index finger's proximal joint turned 60 degrees from its rest frame about each axis in turn (one of them is
	// its flexion); the glove is in meters, the index finger from the knuckle to the tip is about 0.1 m
	float biggest = 0.f;
	for(int axis = 0; axis < 3; axis++) {
		glm::vec3 about(0.f);
		about[axis] = 1.f;
		vr::HandJoints turned = rest;
		turned.orientation[size_t(vr::HandJoint::IndexProximal)]
			= glm::angleAxis(glm::radians(60.f), about) * rest.orientation[size_t(vr::HandJoint::IndexProximal)];
		std::vector<glm::mat4> local;
		vr::poseHandFromJoints(mesh, rig, turned, local);
		std::vector<glm::mat4> after = hctWorld(mesh, local);
		auto moved = [&](vr::HandJoint joint) {
			size_t i = size_t(vr::findHandJoint(mesh, joint));
			return glm::length(glm::vec3(after[i][3]) - glm::vec3(before[i][3]));
		};
		biggest = std::max(biggest, moved(vr::HandJoint::IndexTip));
		hctExpectNear("middle finger tip", 0.f, moved(vr::HandJoint::MiddleTip), 1e-3f);
		hctExpectNear("little finger tip", 0.f, moved(vr::HandJoint::LittleTip), 1e-3f);
		hctExpectNear("thumb tip", 0.f, moved(vr::HandJoint::ThumbTip), 1e-3f);
		hctExpectNear("palm", 0.f, moved(vr::HandJoint::Palm), 1e-3f);
		hctExpectNear("index knuckle", 0.f, moved(vr::HandJoint::IndexProximal), 1e-3f);
	}
	CPPUNIT_ASSERT_MESSAGE("a turned joint must move its finger tip", biggest > 0.03f);
}

void HandConformingTest::testPalmSitsBehindTheGripOnBothHands() {
	// OpenXR 1.0 section 12.38.6 (grip pose): the X axis is the palm's normal; the palm of the right hand faces -X
	// (+X points out of its back) and the palm of the left hand faces +X. The handle held in the palm therefore lies
	// at -X from the palm of the right hand, so the palm lies at +X from the grip for the right hand and at -X for
	// the left. The same convention as synthesizeHandJoints' doc in vr/FingerCurl.h ("the palm facing -X for the
	// right hand and +X for the left"); the second half of this test checks it against the synthesized fist.
	const float handle = 0.02f;
	const float palmRadius = 0.015f;
	const glm::quat turn = glm::angleAxis(glm::radians(70.f), glm::normalize(glm::vec3(0.3f, 1.f, 0.2f)));
	const glm::quat orientations[] = { glm::quat(1.f, 0.f, 0.f, 0.f), turn };
	for(vr::Hand hand : hctHands) {
		float sign = (hand == vr::Hand::Right) ? 1.f : -1.f;
		for(const glm::quat & q : orientations) {
			glm::vec3 grip(0.3f, 1.2f, -0.4f);
			Vec3f palm = vr::palmBehindGrip(hand, Vec3f(grip.x, grip.y, grip.z), q, handle, palmRadius);
			glm::vec3 expected = grip + q * glm::vec3(sign * (handle + palmRadius), 0.f, 0.f);
			hctExpectNear("palm.x", expected.x, palm.x, 1e-5f);
			hctExpectNear("palm.y", expected.y, palm.y, 1e-5f);
			hctExpectNear("palm.z", expected.z, palm.z, 1e-5f);
		}
		// A zero radius leaves the palm on the grip
		Vec3f same = vr::palmBehindGrip(hand, Vec3f(1.f, 2.f, 3.f), glm::quat(1.f, 0.f, 0.f, 0.f), 0.f, 0.f);
		hctExpectNear("zero offset", 1.f, same.x, 1e-6f);
	}
	// The palm side agrees with the synthesized hand: fingers close toward the palm's face, i.e. toward the handle,
	// which lies on the side of the grip opposite to the palm
	for(vr::Hand hand : hctHands) {
		vr::HandShape fist;
		fist.index = 1.f;
		float curl = hctPos(vr::synthesizeHandJoints(hand, fist), vr::HandJoint::IndexTip).x
		             - hctPos(vr::synthesizeHandJoints(hand, vr::HandShape()), vr::HandJoint::IndexTip).x;
		Vec3f palm = vr::palmBehindGrip(hand, Vec3f(0.f), glm::quat(1.f, 0.f, 0.f, 0.f), 0.02f, 0.015f);
		CPPUNIT_ASSERT_MESSAGE("palm on the wrong side of the grip", curl * palm.x < 0.f);
	}
}
