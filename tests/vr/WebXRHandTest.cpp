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

#include "vr/WebXRHandTest.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<WebXRHandTest> g_registerWebXRHandTest;

namespace {

// Helpers are prefixed wxh: the test sources are one unity build

const char wxhSides[] = { 'L', 'R' };

std::string wxhPath(char side) {
	return std::string("/data/core/vr/hands/webxr/") + (side == 'L' ? "left.glb" : "right.glb");
}

vr::SkinnedMesh wxhLoad(char side) {
#ifdef ARX_TEST_SOURCE_DIR
	std::string path = std::string(ARX_TEST_SOURCE_DIR) + wxhPath(side);
	std::ifstream file(path.c_str(), std::ios::binary);
	if(!file) {
		CPPUNIT_FAIL("cannot open " + path);
	}
	std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	CPPUNIT_ASSERT_MESSAGE(path + " is not a binary glTF", data.compare(0, 4, "glTF") == 0);
	vr::SkinnedMesh mesh;
	std::string error;
	bool ok = vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error);
	CPPUNIT_ASSERT_MESSAGE(path + ": load failed: " + error, ok);
	return mesh;
#else
	(void)side;
	CPPUNIT_FAIL("ARX_TEST_SOURCE_DIR is not defined for arxtest");
	return vr::SkinnedMesh();
#endif
}

std::vector<glm::mat4> wxhWorld(const vr::SkinnedMesh & mesh, const std::vector<glm::mat4> & local) {
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

} // anonymous namespace

void WebXRHandTest::testBothFilesLoadAsBinaryGltf() {
	for(char side : wxhSides) {
		vr::SkinnedMesh mesh = wxhLoad(side);
		// The file has 25 skin joints (the WebXR set: no palm); 26 only if the loader adds a synthetic Palm joint
		CPPUNIT_ASSERT_MESSAGE("joint count", mesh.skeleton.size() == 25 || mesh.skeleton.size() == 26);
		// About 1360 vertices and 2314 triangles
		CPPUNIT_ASSERT(mesh.positions.size() > 1000);
		CPPUNIT_ASSERT_EQUAL(mesh.positions.size(), mesh.normals.size());
		CPPUNIT_ASSERT_EQUAL(mesh.positions.size(), mesh.joints.size());
		CPPUNIT_ASSERT_EQUAL(mesh.positions.size(), mesh.weights.size());
		CPPUNIT_ASSERT_EQUAL(size_t(0), mesh.indices.size() % 3);
		CPPUNIT_ASSERT(mesh.indices.size() / 3 > 2000);
		for(unsigned short index : mesh.indices) {
			CPPUNIT_ASSERT(index < mesh.positions.size());
		}
	}
}

void WebXRHandTest::testEveryOpenXRJointIsFound() {
	for(char side : wxhSides) {
		vr::SkinnedMesh mesh = wxhLoad(side);
		std::vector<int> seen;
		for(size_t j = 0; j < vr::HandJointCount; j++) {
			// The WebXR names are mapped to XrHandJointEXT's (wrist -> Wrist, thumb-metacarpal -> ThumbMetacarpal,
			// index-finger-phalanx-proximal -> IndexProximal, pinky-finger-tip -> LittleTip, ...). The asset has no
			// palm joint: findHandJoint(Palm) must give the documented substitute (a joint of the asset), never -1.
			int index = vr::findHandJoint(mesh, vr::HandJoint(j));
			CPPUNIT_ASSERT_MESSAGE("joint " + std::to_string(j) + " of the OpenXR set", index >= 0);
			CPPUNIT_ASSERT(size_t(index) < mesh.skeleton.size());
			if(vr::HandJoint(j) != vr::HandJoint::Palm) {
				for(int other : seen) {
					CPPUNIT_ASSERT_MESSAGE("two joints map to one: " + std::to_string(j), other != index);
				}
				seen.push_back(index);
			}
		}
		CPPUNIT_ASSERT_EQUAL(size_t(25), seen.size());
	}
}

void WebXRHandTest::testWeightsSumToOne() {
	for(char side : wxhSides) {
		vr::SkinnedMesh mesh = wxhLoad(side);
		for(size_t v = 0; v < mesh.weights.size(); v++) {
			float sum = 0.f;
			for(size_t k = 0; k < 4; k++) {
				float w = mesh.weights[v][k];
				CPPUNIT_ASSERT(w >= 0.f);
				sum += w;
				if(w > 0.f) {
					CPPUNIT_ASSERT(size_t(mesh.joints[v][k]) < mesh.skeleton.size());
				}
			}
			if(!(std::abs(sum - 1.f) <= 1e-4f)) {
				CPPUNIT_FAIL("weights of vertex " + std::to_string(v) + " sum to " + std::to_string(sum));
			}
		}
	}
}

void WebXRHandTest::testHandRigIsValid() {
	for(char side : wxhSides) {
		vr::SkinnedMesh mesh = wxhLoad(side);
		vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT_MESSAGE(std::string("rig of side ") + side, rig.valid);
		for(size_t j = 0; j < vr::HandJointCount; j++) {
			CPPUNIT_ASSERT(rig.joint[j] >= 0);
			CPPUNIT_ASSERT(size_t(rig.joint[j]) < mesh.skeleton.size());
		}
	}
}

void WebXRHandTest::testRestJointsReproduceTheRestLocals() {
	for(char side : wxhSides) {
		vr::SkinnedMesh mesh = wxhLoad(side);
		vr::HandRig rig = vr::buildHandRig(mesh);
		CPPUNIT_ASSERT(rig.valid);
		vr::HandJoints joints = vr::restHandJoints(mesh, rig);
		CPPUNIT_ASSERT(joints.valid);

		std::vector<glm::mat4> restLocal;
		for(const auto & joint : mesh.skeleton) {
			restLocal.push_back(joint.restLocal);
		}
		std::vector<glm::mat4> world = wxhWorld(mesh, restLocal);
		for(size_t j = 0; j < vr::HandJointCount; j++) {
			int index = vr::findHandJoint(mesh, vr::HandJoint(j));
			CPPUNIT_ASSERT(index >= 0);
			glm::vec3 p(world[size_t(index)][3]);
			Vec3f q = joints[vr::HandJoint(j)];
			float distance = glm::length(glm::vec3(q.x, q.y, q.z) - p);
			CPPUNIT_ASSERT_MESSAGE("rest position of joint " + std::to_string(j), distance <= 1e-5f);
		}

		std::vector<glm::mat4> local;
		vr::poseHandFromJoints(mesh, rig, joints, local);
		CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), local.size());
		for(size_t i = 0; i < local.size(); i++) {
			for(int c = 0; c < 4; c++) {
				for(int r = 0; r < 3; r++) {
					float expected = mesh.skeleton[i].restLocal[c][r];
					if(!(std::abs(expected - local[i][c][r]) <= 2e-3f)) {
						CPPUNIT_FAIL("rest local " + std::to_string(i) + " of side " + side + ": expected "
						             + std::to_string(expected) + ", got " + std::to_string(local[i][c][r]));
					}
				}
			}
		}
	}
}
