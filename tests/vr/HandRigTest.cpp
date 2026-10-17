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

#include "vr/HandRigTest.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<HandRigTest> g_registerHandRigTest;

namespace {

// Helpers are prefixed hrt: the test sources are one unity build with ArmMeshTest's anonymous namespace

//! Joints per finger from the root joint that rotates, in the order of FingerPose::flex, then the tip
std::vector<vr::HandJoint> hrtChain(vr::Finger finger) {
	std::vector<vr::HandJoint> chain;
	int first = (finger == vr::Finger::Thumb) ? int(vr::HandJoint::ThumbMetacarpal)
	                                          : int(vr::HandJoint::IndexMetacarpal) + 5 * (int(finger) - 1);
	int count = (finger == vr::Finger::Thumb) ? 4 : 5;
	for(int i = 0; i < count; i++) {
		chain.push_back(vr::HandJoint(first + i));
	}
	return chain;
}

//! The index in hrtChain of the joint that flex[finger][k] turns
size_t hrtRotatingJoint(vr::Finger finger, size_t k) {
	return (finger == vr::Finger::Thumb) ? k : k + 1;
}

bool hrtInChain(vr::Finger finger, vr::HandJoint joint) {
	for(vr::HandJoint member : hrtChain(finger)) {
		if(member == joint) {
			return true;
		}
	}
	return false;
}

std::string hrtReadHandFile(char side) {
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

vr::SkinnedMesh hrtLoadHand(char side) {
	std::string data = hrtReadHandFile(side);
	vr::SkinnedMesh mesh;
	std::string error;
	bool ok = vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error);
	CPPUNIT_ASSERT_MESSAGE("load failed: " + error, ok);
	return mesh;
}

glm::mat4 hrtWorldOf(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & local, int joint) {
	glm::mat4 world = local[size_t(joint)];
	for(int parent = mesh.skeleton[size_t(joint)].parent; parent >= 0; parent = mesh.skeleton[size_t(parent)].parent) {
		world = local[size_t(parent)] * world;
	}
	return world;
}

//! The world matrices of a pose: the locals composed with their parents (a parent may come after its child)
std::vector<glm::mat4> hrtWorldMatrices(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & local) {
	CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), local.size());
	std::vector<glm::mat4> world;
	for(size_t i = 0; i < mesh.skeleton.size(); i++) {
		world.push_back(hrtWorldOf(mesh, local, int(i)));
	}
	return world;
}

//! The world matrices of the hand posed by \a pose
std::vector<glm::mat4> hrtPosedWorld(const vr::SkinnedMesh & mesh, const vr::HandRig & rig, const vr::FingerPose & pose) {
	std::vector<glm::mat4> local;
	vr::poseHand(mesh, rig, pose, local);
	return hrtWorldMatrices(mesh, local);
}

glm::vec3 hrtPosition(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & world, vr::HandJoint joint) {
	int index = vr::findHandJoint(mesh, joint);
	CPPUNIT_ASSERT(index >= 0);
	return glm::vec3(world[size_t(index)][3]);
}

float hrtDistance(const glm::vec3 & a, const glm::vec3 & b) {
	return glm::length(a - b);
}

vr::HandRig hrtValidRig(const vr::SkinnedMesh & mesh) {
	vr::HandRig rig = vr::buildHandRig(mesh);
	CPPUNIT_ASSERT_MESSAGE("the rig of a shipped glove must be valid", rig.valid);
	return rig;
}

//! A curl of one finger, every joint bent a lot (a thumb less: its joints are limited to 40 / 50 / 70)
vr::FingerPose hrtCurlOf(vr::Finger finger, float degrees) {
	vr::FingerPose pose;
	for(size_t k = 0; k < 3; k++) {
		pose.flex[size_t(finger)][k] = degrees;
	}
	return pose;
}

//! Direction from the metacarpals' plane to the palm: unit, from the rest mesh
glm::vec3 hrtPalmNormal(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & rest) {
	glm::vec3 across = hrtPosition(mesh, rest, vr::HandJoint::IndexMetacarpal)
	                   - hrtPosition(mesh, rest, vr::HandJoint::LittleMetacarpal);
	glm::vec3 along = hrtPosition(mesh, rest, vr::HandJoint::MiddleProximal)
	                  - hrtPosition(mesh, rest, vr::HandJoint::MiddleMetacarpal);
	glm::vec3 normal = glm::normalize(glm::cross(across, along));
	// The Palm joint sits on the palm's side of the knuckles' plane
	glm::vec3 toPalm = hrtPosition(mesh, rest, vr::HandJoint::Palm) - hrtPosition(mesh, rest, vr::HandJoint::MiddleMetacarpal);
	return glm::dot(normal, toPalm) < 0.f ? -normal : normal;
}

void hrtExpectNear(const std::string & what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void hrtExpectVec(const std::string & what, const glm::vec3 & expected, const glm::vec3 & actual, float tolerance) {
	hrtExpectNear(what + ".x", expected.x, actual.x, tolerance);
	hrtExpectNear(what + ".y", expected.y, actual.y, tolerance);
	hrtExpectNear(what + ".z", expected.z, actual.z, tolerance);
}

const char hrtSides[] = { 'L', 'R' };

} // anonymous namespace

void HandRigTest::testRigIsValidOnBothHands() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		for(size_t j = 0; j < vr::HandJointCount; j++) {
			CPPUNIT_ASSERT_EQUAL(vr::findHandJoint(mesh, vr::HandJoint(j)), rig.joint[j]);
			CPPUNIT_ASSERT(rig.joint[j] >= 0);
		}
		// Every finger joint has a flexion axis: a unit vector across its bone
		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			std::vector<vr::HandJoint> chain = hrtChain(vr::Finger(finger));
			for(size_t k = 0; k < 3; k++) {
				glm::vec3 axis(rig.flexAxis[finger * 3 + k].x, rig.flexAxis[finger * 3 + k].y, rig.flexAxis[finger * 3 + k].z);
				hrtExpectNear("flex axis length", 1.f, glm::length(axis), 1e-3f);
				// The axis is in the joint's local frame; the bone to the next joint is that child's rest translation
				size_t at = hrtRotatingJoint(vr::Finger(finger), k);
				int child = rig.joint[size_t(chain[at + 1])];
				glm::vec3 bone = glm::normalize(glm::vec3(mesh.skeleton[size_t(child)].restLocal[3]));
				hrtExpectNear("flex axis across the bone", 0.f, glm::dot(axis, bone), 0.02f);
			}
			glm::vec3 spread(rig.spreadAxis[finger].x, rig.spreadAxis[finger].y, rig.spreadAxis[finger].z);
			hrtExpectNear("spread axis length", 1.f, glm::length(spread), 1e-3f);
		}
	}
}

void HandRigTest::testRigIsInvalidWithAMissingJoint() {
	// No skeleton at all
	vr::SkinnedMesh empty;
	CPPUNIT_ASSERT(!vr::buildHandRig(empty).valid);

	// A glove with one joint renamed away
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		CPPUNIT_ASSERT(hrtValidRig(mesh).valid);
		int tip = vr::findHandJoint(mesh, vr::HandJoint::IndexTip);
		CPPUNIT_ASSERT(tip >= 0);
		mesh.skeleton[size_t(tip)].name = "NotAHandJoint";
		CPPUNIT_ASSERT(!vr::buildHandRig(mesh).valid);
	}
}

void HandRigTest::testOpenPoseReproducesRestWorldMatrices() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> local;
		vr::poseHand(mesh, rig, vr::FingerPose(), local);
		CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), local.size());

		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);
		std::vector<glm::mat4> posed = hrtWorldMatrices(mesh, local);
		for(size_t i = 0; i < rest.size(); i++) {
			for(int column = 0; column < 4; column++) {
				for(int row = 0; row < 4; row++) {
					hrtExpectNear("open pose " + mesh.skeleton[i].name, rest[i][column][row], posed[i][column][row], 1e-5f);
				}
			}
		}
	}
}

void HandRigTest::testCurlingOneFingerMovesOnlyThatFinger() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);

		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			vr::Finger which = vr::Finger(finger);
			float degrees = (which == vr::Finger::Thumb) ? 30.f : 50.f;
			std::vector<glm::mat4> posed = hrtPosedWorld(mesh, rig, hrtCurlOf(which, degrees));
			std::vector<vr::HandJoint> chain = hrtChain(which);
			size_t rotating = hrtRotatingJoint(which, 0);

			for(size_t j = 0; j < vr::HandJointCount; j++) {
				vr::HandJoint joint = vr::HandJoint(j);
				float moved = hrtDistance(hrtPosition(mesh, rest, joint), hrtPosition(mesh, posed, joint));
				std::string name = mesh.skeleton[size_t(rig.joint[j])].name;
				bool below = false;
				for(size_t c = rotating + 1; c < chain.size(); c++) {
					below = below || chain[c] == joint;
				}
				if(below) {
					CPPUNIT_ASSERT_MESSAGE(name + " of the curled finger did not move", moved > 1e-3f);
				} else {
					hrtExpectNear(name + " is not part of the curled finger", 0.f, moved, 1e-5f);
				}
			}
		}
	}
}

void HandRigTest::testEachFlexionMovesOnlyTheJointsBelowIt() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);

		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			vr::Finger which = vr::Finger(finger);
			std::vector<vr::HandJoint> chain = hrtChain(which);
			for(size_t k = 0; k < 3; k++) {
				vr::FingerPose pose;
				pose.flex[finger][k] = 45.f;
				std::vector<glm::mat4> posed = hrtPosedWorld(mesh, rig, pose);
				size_t rotating = hrtRotatingJoint(which, k);
				for(size_t j = 0; j < vr::HandJointCount; j++) {
					vr::HandJoint joint = vr::HandJoint(j);
					float moved = hrtDistance(hrtPosition(mesh, rest, joint), hrtPosition(mesh, posed, joint));
					std::string name = mesh.skeleton[size_t(rig.joint[j])].name + " with flex " + std::to_string(k);
					bool below = false;
					for(size_t c = rotating + 1; c < chain.size(); c++) {
						below = below || chain[c] == joint;
					}
					if(below) {
						CPPUNIT_ASSERT_MESSAGE(name + " did not move", moved > 2e-3f);
					} else {
						hrtExpectNear(name + " must stay", 0.f, moved, 1e-5f);
					}
				}
			}
		}
	}
}

void HandRigTest::testCurledTipMovesTowardThePalmAndTheWrist() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);
		glm::vec3 palmNormal = hrtPalmNormal(mesh, rest);
		glm::vec3 towardWrist = glm::normalize(hrtPosition(mesh, rest, vr::HandJoint::Wrist)
		                                       - hrtPosition(mesh, rest, vr::HandJoint::MiddleProximal));

		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			vr::Finger which = vr::Finger(finger);
			vr::HandJoint tip = hrtChain(which).back();
			std::vector<glm::mat4> posed = hrtPosedWorld(mesh, rig, hrtCurlOf(which, which == vr::Finger::Thumb ? 30.f : 50.f));
			glm::vec3 movement = hrtPosition(mesh, posed, tip) - hrtPosition(mesh, rest, tip);
			std::string name = mesh.skeleton[size_t(rig.joint[size_t(tip)])].name;
			CPPUNIT_ASSERT_MESSAGE(name + " must curl toward the palm side", glm::dot(movement, palmNormal) > 5e-3f);
			CPPUNIT_ASSERT_MESSAGE(name + " must curl toward the wrist", glm::dot(movement, towardWrist) > 5e-3f);
		}
	}
}

void HandRigTest::testFlexionIsMonotonic() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);
		for(size_t finger = 0; finger < vr::FingerCount; finger++) {
			vr::Finger which = vr::Finger(finger);
			vr::HandJoint tip = hrtChain(which).back();
			float previous = 0.f;
			for(float degrees : { 10.f, 20.f, 30.f }) {
				std::vector<glm::mat4> posed = hrtPosedWorld(mesh, rig, hrtCurlOf(which, degrees));
				float moved = hrtDistance(hrtPosition(mesh, rest, tip), hrtPosition(mesh, posed, tip));
				CPPUNIT_ASSERT_MESSAGE("a larger angle must move the tip further", moved > previous + 1e-3f);
				previous = moved;
			}
		}
	}
}

void HandRigTest::testSpreadTurnsTheFingerSidewaysTowardTheThumb() {
	for(char side : hrtSides) {
		vr::SkinnedMesh mesh = hrtLoadHand(side);
		vr::HandRig rig = hrtValidRig(mesh);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);
		glm::vec3 palmNormal = hrtPalmNormal(mesh, rest);
		// From the little finger's side to the index finger's: the thumb's side
		glm::vec3 towardThumb = glm::normalize(hrtPosition(mesh, rest, vr::HandJoint::IndexMetacarpal)
		                                       - hrtPosition(mesh, rest, vr::HandJoint::LittleMetacarpal));

		// The thumb's spread stays 0 (FingerPose); the four fingers turn at the knuckle
		for(size_t finger = 1; finger < vr::FingerCount; finger++) {
			vr::Finger which = vr::Finger(finger);
			std::vector<vr::HandJoint> chain = hrtChain(which);
			vr::FingerPose pose;
			pose.spread[finger] = 20.f;
			std::vector<glm::mat4> posed = hrtPosedWorld(mesh, rig, pose);
			vr::HandJoint tip = chain.back();
			glm::vec3 movement = hrtPosition(mesh, posed, tip) - hrtPosition(mesh, rest, tip);
			std::string name = mesh.skeleton[size_t(rig.joint[size_t(tip)])].name;
			CPPUNIT_ASSERT_MESSAGE(name + " must turn toward the thumb's side", glm::dot(movement, towardThumb) > 5e-3f);
			// A sideways turn, not a curl
			CPPUNIT_ASSERT_MESSAGE(name + " must not curl with the spread",
			                       std::abs(glm::dot(movement, palmNormal)) < 0.6f * glm::length(movement));
			// The metacarpal and the knuckle stay; nothing outside the finger moves
			for(size_t j = 0; j < vr::HandJointCount; j++) {
				vr::HandJoint joint = vr::HandJoint(j);
				if(!hrtInChain(which, joint) || joint == chain[0] || joint == chain[1]) {
					hrtExpectNear(mesh.skeleton[size_t(rig.joint[j])].name + " must stay",
					              0.f, hrtDistance(hrtPosition(mesh, rest, joint), hrtPosition(mesh, posed, joint)), 1e-5f);
				}
			}
		}
	}
}

void HandRigTest::testLeftHandMirrorsTheRight() {
	vr::SkinnedMesh right = hrtLoadHand('R');
	vr::SkinnedMesh left = hrtLoadHand('L');
	vr::HandRig rightRig = hrtValidRig(right);
	vr::HandRig leftRig = hrtValidRig(left);

	// The shipped left glove is the right one mirrored across x
	std::vector<vr::FingerPose> poses;
	poses.push_back(vr::FingerPose());
	for(size_t finger = 0; finger < vr::FingerCount; finger++) {
		poses.push_back(hrtCurlOf(vr::Finger(finger), finger == 0 ? 30.f : 50.f));
		if(finger > 0) {
			vr::FingerPose spread;
			spread.spread[finger] = 20.f;
			poses.push_back(spread);
		}
	}
	for(size_t p = 0; p < poses.size(); p++) {
		std::vector<glm::mat4> rightWorld = hrtPosedWorld(right, rightRig, poses[p]);
		std::vector<glm::mat4> leftWorld = hrtPosedWorld(left, leftRig, poses[p]);
		for(size_t j = 0; j < vr::HandJointCount; j++) {
			glm::vec3 r = hrtPosition(right, rightWorld, vr::HandJoint(j));
			glm::vec3 l = hrtPosition(left, leftWorld, vr::HandJoint(j));
			hrtExpectVec("mirrored joint " + std::to_string(j) + " of pose " + std::to_string(p),
			             glm::vec3(-r.x, r.y, r.z), l, 1e-4f);
		}
	}
}
