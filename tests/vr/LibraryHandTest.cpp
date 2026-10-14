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

#include "vr/LibraryHandTest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/ArmIK.h"
#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<LibraryHandTest> g_registerLibraryHandTest;

namespace {

// Helpers are prefixed lht: the test sources are one unity build

const vr::Hand lhtHands[] = { vr::Hand::Left, vr::Hand::Right };

std::string lhtName(vr::Hand hand) {
	return hand == vr::Hand::Right ? "right" : "left";
}

vr::SkinnedMesh lhtLoad(vr::Hand hand) {
#ifdef ARX_TEST_SOURCE_DIR
	const std::string path = std::string(ARX_TEST_SOURCE_DIR) + "/data/core/vr/hands/webxr/" + lhtName(hand) + ".glb";
	std::ifstream file(path.c_str(), std::ios::binary);
	if(!file) {
		CPPUNIT_FAIL("cannot open " + path);
	}
	const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	vr::SkinnedMesh mesh;
	std::string error;
	CPPUNIT_ASSERT_MESSAGE(path + ": " + error, vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error));
	return mesh;
#else
	(void)hand;
	CPPUNIT_FAIL("ARX_TEST_SOURCE_DIR is not defined for arxtest");
	return vr::SkinnedMesh();
#endif
}

Vec3f lhtJoint(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & world, vr::HandJoint joint) {
	return Vec3f(world[size_t(vr::findHandJoint(mesh, joint))][3]);
}

//! The joints' world matrices for \a local (each composed with its parents')
std::vector<glm::mat4> lhtCompose(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & local) {
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

//! Out of the palm (mesh space): the rig's rest palm frame's -Y (OpenXR: a joint's +Y is the back of the hand)
Vec3f lhtPalmSide(const vr::HandRig & rig) {
	return rig.restFrame[size_t(vr::HandJoint::Palm)] * Vec3f(0.f, -1.f, 0.f);
}

} // anonymous namespace

void LibraryHandTest::testFlatSkeletonIsChained() {
	for(vr::Hand hand : lhtHands) {
		const vr::SkinnedMesh mesh = lhtLoad(hand);
		const std::string name = lhtName(hand);
		const int wrist = vr::findHandJoint(mesh, vr::HandJoint::Wrist);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(name + ": the wrist is the root", -1, mesh.skeleton[size_t(wrist)].parent);
		for(size_t j = size_t(vr::HandJoint::ThumbMetacarpal); j < vr::HandJointCount; j++) {
			const int index = vr::findHandJoint(mesh, vr::HandJoint(j));
			const bool metacarpal = j == size_t(vr::HandJoint::ThumbMetacarpal)
			                        || (j >= size_t(vr::HandJoint::IndexMetacarpal)
			                            && (j - size_t(vr::HandJoint::IndexMetacarpal)) % 5 == 0);
			const int expected = metacarpal ? wrist : vr::findHandJoint(mesh, vr::HandJoint(j - 1));
			CPPUNIT_ASSERT_EQUAL_MESSAGE(name + ": the parent of joint " + std::to_string(j), expected,
			                             mesh.skeleton[size_t(index)].parent);
		}
		// The palm is the middle metacarpal, chained as that
		CPPUNIT_ASSERT_EQUAL(vr::findHandJoint(mesh, vr::HandJoint::MiddleMetacarpal),
		                     vr::findHandJoint(mesh, vr::HandJoint::Palm));
	}
}

void LibraryHandTest::testRestSkinningKeepsTheMesh() {
	for(vr::Hand hand : lhtHands) {
		const vr::SkinnedMesh mesh = lhtLoad(hand);
		std::vector<glm::mat4> world;
		vr::restWorldMatrices(mesh, world);
		std::vector<Vec3f> positions;
		std::vector<Vec3f> normals;
		vr::skinMesh(mesh, world, positions, normals);
		float worst = 0.f;
		for(size_t v = 0; v < positions.size(); v++) {
			worst = std::max(worst, glm::distance(positions[v], mesh.positions[v]));
		}
		// Meters: the chained rest composes to the file's rest within a hundredth of a millimetre
		CPPUNIT_ASSERT_MESSAGE(lhtName(hand) + ": a rest vertex moved by " + std::to_string(worst), worst <= 1e-5f);
	}
}

void LibraryHandTest::testPalmFacesTheThumbsSide() {
	for(vr::Hand hand : lhtHands) {
		const vr::SkinnedMesh mesh = lhtLoad(hand);
		const vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT(rig.valid);
		std::vector<glm::mat4> world;
		vr::restWorldMatrices(mesh, world);
		// The asset's own wrist node is its OpenXR frame: its +Y the back of the hand
		const glm::mat3 wrist(world[size_t(vr::findHandJoint(mesh, vr::HandJoint::Wrist))]);
		const Vec3f back = glm::normalize(wrist * Vec3f(0.f, 1.f, 0.f));
		const float agree = glm::dot(lhtPalmSide(rig), back);
		CPPUNIT_ASSERT_MESSAGE(lhtName(hand) + ": the palm faces the back's side, " + std::to_string(agree), agree < -0.8f);
	}
}

void LibraryHandTest::testHiltPoseCurlsRoundTheHandle() {
	for(vr::Hand hand : lhtHands) {
		const vr::SkinnedMesh mesh = lhtLoad(hand);
		const std::string name = lhtName(hand);
		const vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT(rig.valid);
		std::vector<glm::mat4> rest;
		vr::restWorldMatrices(mesh, rest);
		std::vector<glm::mat4> local;
		vr::poseHandFromJoints(mesh, rig, vr::palmRelativeJoints(vr::hiltPose(hand, vr::DefaultHiltRadius)), local);
		const std::vector<glm::mat4> held = lhtCompose(mesh, local);

		// Every fingertip closes toward the palm's side by at least 2 cm (the shortest, the little finger's, about 2.7)
		const Vec3f palmSide = lhtPalmSide(rig);
		for(vr::HandJoint tip : { vr::HandJoint::IndexTip, vr::HandJoint::MiddleTip, vr::HandJoint::RingTip,
		                          vr::HandJoint::LittleTip }) {
			const float closed = glm::dot(lhtJoint(mesh, held, tip) - lhtJoint(mesh, rest, tip), palmSide);
			CPPUNIT_ASSERT_MESSAGE(name + ": the tip of joint " + std::to_string(size_t(tip)) + " closed by "
			                       + std::to_string(closed), closed > 0.02f);
		}

		// The curl the fingers wrap lies in front of the palm, and the hand placed as vr/VRArms.cpp places it lays it
		// along the handle
		const vr::GloveCurl curl = vr::gloveCurlAxis(mesh, rig, local);
		CPPUNIT_ASSERT_MESSAGE(name + ": no curl", curl.valid);
		const Vec3f palmCentre = 0.5f * (lhtJoint(mesh, rest, vr::HandJoint::MiddleMetacarpal)
		                                 + lhtJoint(mesh, rest, vr::HandJoint::MiddleProximal));
		CPPUNIT_ASSERT_MESSAGE(name + ": the curl behind the palm", glm::dot(curl.centre - palmCentre, palmSide) > 0.f);

		const Vec3f wrist = lhtJoint(mesh, rest, vr::HandJoint::Wrist);
		const Vec3f palmLocal = lhtJoint(mesh, rest, vr::HandJoint::Palm) - wrist;
		const Vec3f thumbLocal = lhtJoint(mesh, rest, vr::HandJoint::ThumbProximal) - wrist;
		std::vector<Vec3f> vertices;
		for(const Vec3f & position : mesh.positions) {
			vertices.push_back(position - wrist);
		}
		const vr::HandFrame frame = vr::handFrameFromMesh(vertices, &palmLocal, &thumbLocal);
		CPPUNIT_ASSERT(frame.valid);
		const glm::quat gripOrientation = glm::angleAxis(0.7f, glm::normalize(Vec3f(1.f, 2.f, -1.f)));
		const glm::quat handQuat = glm::normalize(gripOrientation * vr::gloveOnHandle(curl, vr::handToGrip(frame)));
		const Vec3f handle = gripOrientation * Vec3f(0.f, 0.f, 1.f);
		const float angle = glm::degrees(std::acos(std::min(std::abs(glm::dot(handQuat * curl.axis, handle)), 1.f)));
		CPPUNIT_ASSERT_MESSAGE(name + ": the curl axis off the handle by " + std::to_string(angle), angle <= 10.f);
	}
}
