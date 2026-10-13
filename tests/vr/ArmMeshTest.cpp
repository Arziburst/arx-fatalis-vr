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

#include "vr/ArmMeshTest.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"

CppUnit::AutoRegisterSuite<ArmMeshTest> g_registerArmMeshTest;

namespace {

// Counts from the accessors of the shipped files (godot-xr-tools @6c6189b, Hand_Glove_low_L/R.gltf):
// POSITION count 812, indices count 3480 (1160 triangles), 26 joints in the skin
const size_t HandJointCount = 26;
const size_t HandVertexCount = 812;
const size_t HandIndexCount = 3480;
const size_t HandTriangleCount = HandIndexCount / 3;

void expectNear(const char * what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

glm::vec3 asGlm(const Vec3f & v) {
	return glm::vec3(v.x, v.y, v.z);
}

void nearVec(const char * what, const glm::vec3 & expected, const glm::vec3 & actual, float tolerance) {
	expectNear((std::string(what) + ".x").c_str(), expected.x, actual.x, tolerance);
	expectNear((std::string(what) + ".y").c_str(), expected.y, actual.y, tolerance);
	expectNear((std::string(what) + ".z").c_str(), expected.z, actual.z, tolerance);
}

std::string readHandFile(char side) {
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

vr::SkinnedMesh loadHand(char side) {
	std::string data = readHandFile(side);
	vr::SkinnedMesh mesh;
	std::string error;
	bool ok = vr::loadGltfSkinnedMesh(data.data(), data.size(), mesh, error);
	CPPUNIT_ASSERT_MESSAGE("load failed: " + error, ok);
	return mesh;
}

//! A mesh with the given number of root joints at the origin (identity rest and inverse bind) and no vertices
vr::SkinnedMesh tinyMesh(size_t jointCount) {
	vr::SkinnedMesh mesh;
	for(size_t i = 0; i < jointCount; i++) {
		vr::MeshJoint joint;
		joint.name = "J" + std::to_string(i);
		joint.parent = -1;
		joint.restLocal = glm::mat4(1.f);
		joint.inverseBind = glm::mat4(1.f);
		mesh.skeleton.push_back(joint);
	}
	return mesh;
}

void addVertex(vr::SkinnedMesh & mesh, glm::vec3 position, glm::vec3 normal,
               std::array<u8, 4> joints, std::array<float, 4> weights) {
	mesh.positions.push_back(Vec3f(position.x, position.y, position.z));
	mesh.normals.push_back(Vec3f(normal.x, normal.y, normal.z));
	mesh.uvs.push_back(Vec2f(0.f, 0.f));
	mesh.joints.push_back(joints);
	mesh.weights.push_back(weights);
}

//! Radius of a vertex around the arm's X axis
float radiusOf(const Vec3f & p) {
	return std::sqrt(p.y * p.y + p.z * p.z);
}

//! The vertices whose X is closest to the given X (the nearest ring, including any cap on it)
std::vector<size_t> verticesNearestX(const vr::SkinnedMesh & mesh, float x) {
	float best = 1e9f;
	for(const Vec3f & p : mesh.positions) {
		best = std::min(best, std::abs(p.x - x));
	}
	std::vector<size_t> result;
	for(size_t i = 0; i < mesh.positions.size(); i++) {
		if(std::abs(mesh.positions[i].x - x) <= best + 0.01f) {
			result.push_back(i);
		}
	}
	return result;
}

float weightOn(const vr::SkinnedMesh & mesh, size_t vertex, std::initializer_list<int> joints) {
	float sum = 0.f;
	for(int k = 0; k < 4; k++) {
		for(int joint : joints) {
			if(mesh.joints[vertex][k] == joint) {
				sum += mesh.weights[vertex][k];
			}
		}
	}
	return sum;
}

} // anonymous namespace

void ArmMeshTest::testLoadsBothHands() {
	for(char side : { 'L', 'R' }) {
		vr::SkinnedMesh mesh = loadHand(side);
		CPPUNIT_ASSERT_EQUAL(HandJointCount, mesh.skeleton.size());
		CPPUNIT_ASSERT_EQUAL(HandVertexCount, mesh.positions.size());
		CPPUNIT_ASSERT_EQUAL(HandVertexCount, mesh.normals.size());
		CPPUNIT_ASSERT_EQUAL(HandVertexCount, mesh.uvs.size());
		CPPUNIT_ASSERT_EQUAL(HandVertexCount, mesh.joints.size());
		CPPUNIT_ASSERT_EQUAL(HandVertexCount, mesh.weights.size());
		CPPUNIT_ASSERT_EQUAL(HandIndexCount, mesh.indices.size());
		CPPUNIT_ASSERT_EQUAL(HandTriangleCount, mesh.indices.size() / 3);
		for(u16 index : mesh.indices) {
			CPPUNIT_ASSERT(index < mesh.positions.size());
		}
		for(const vr::MeshJoint & joint : mesh.skeleton) {
			CPPUNIT_ASSERT(joint.parent < int(mesh.skeleton.size()));
		}
	}
	// Joint names are kept as in the file
	vr::SkinnedMesh right = loadHand('R');
	CPPUNIT_ASSERT(vr::findJoint(right, "Wrist_R") >= 0);
	CPPUNIT_ASSERT(vr::findJoint(right, "Palm_R") >= 0);
	CPPUNIT_ASSERT(vr::findJoint(right, "Thumb_Metacarpal_R") >= 0);
	CPPUNIT_ASSERT(vr::findJoint(right, "Little_Tip_R") >= 0);
	CPPUNIT_ASSERT(vr::findJoint(right, "Little_Tip_L") < 0);
	vr::SkinnedMesh left = loadHand('L');
	CPPUNIT_ASSERT(vr::findJoint(left, "Little_Tip_L") >= 0);
}

void ArmMeshTest::testEveryHandJointIsFound() {
	for(char side : { 'L', 'R' }) {
		vr::SkinnedMesh mesh = loadHand(side);
		std::vector<int> found;
		for(int j = 0; j < int(vr::HandJoint::Count); j++) {
			int index = vr::findHandJoint(mesh, vr::HandJoint(j));
			CPPUNIT_ASSERT_MESSAGE("hand joint " + std::to_string(j) + " not found", index >= 0);
			CPPUNIT_ASSERT(index < int(mesh.skeleton.size()));
			CPPUNIT_ASSERT(std::find(found.begin(), found.end(), index) == found.end());
			found.push_back(index);
		}
		int tip = vr::findHandJoint(mesh, vr::HandJoint::LittleTip);
		CPPUNIT_ASSERT_EQUAL(std::string(side == 'L' ? "Little_Tip_L" : "Little_Tip_R"), mesh.skeleton[tip].name);
		int wrist = vr::findHandJoint(mesh, vr::HandJoint::Wrist);
		CPPUNIT_ASSERT_EQUAL(std::string(side == 'L' ? "Wrist_L" : "Wrist_R"), mesh.skeleton[wrist].name);
	}
}

void ArmMeshTest::testWeightsAreNormalised() {
	for(char side : { 'L', 'R' }) {
		vr::SkinnedMesh mesh = loadHand(side);
		for(size_t v = 0; v < mesh.weights.size(); v++) {
			float sum = 0.f;
			for(int k = 0; k < 4; k++) {
				CPPUNIT_ASSERT(mesh.weights[v][k] >= 0.f);
				if(mesh.weights[v][k] > 0.f) {
					CPPUNIT_ASSERT(mesh.joints[v][k] < mesh.skeleton.size());
				}
				sum += mesh.weights[v][k];
			}
			expectNear("weight sum", 1.f, sum, 1e-4f);
		}
	}
}

void ArmMeshTest::testMalformedBufferFails() {
	vr::SkinnedMesh mesh;
	std::string error;

	const std::string garbage = "this is not a glTF file at all";
	CPPUNIT_ASSERT(!vr::loadGltfSkinnedMesh(garbage.data(), garbage.size(), mesh, error));
	CPPUNIT_ASSERT(!error.empty());

	error.clear();
	CPPUNIT_ASSERT(!vr::loadGltfSkinnedMesh("", 0, mesh, error));
	CPPUNIT_ASSERT(!error.empty());

	// A real file cut in half: the JSON is broken
	std::string data = readHandFile('R');
	error.clear();
	CPPUNIT_ASSERT(!vr::loadGltfSkinnedMesh(data.data(), data.size() / 2, mesh, error));
	CPPUNIT_ASSERT(!error.empty());

	// Valid JSON that holds no skinned mesh
	const std::string empty = "{\"asset\":{\"version\":\"2.0\"}}";
	error.clear();
	CPPUNIT_ASSERT(!vr::loadGltfSkinnedMesh(empty.data(), empty.size(), mesh, error));
	CPPUNIT_ASSERT(!error.empty());
}

void ArmMeshTest::testRestPoseReproducesRestPositions() {
	vr::SkinnedMesh mesh = loadHand('R');
	std::vector<glm::mat4> world;
	vr::restWorldMatrices(mesh, world);
	CPPUNIT_ASSERT_EQUAL(mesh.skeleton.size(), world.size());
	std::vector<Vec3f> positions, normals;
	vr::skinMesh(mesh, world, positions, normals);
	CPPUNIT_ASSERT_EQUAL(mesh.positions.size(), positions.size());
	CPPUNIT_ASSERT_EQUAL(mesh.normals.size(), normals.size());
	for(size_t v = 0; v < positions.size(); v++) {
		nearVec("rest position", asGlm(mesh.positions[v]), asGlm(positions[v]), 1e-4f);
		nearVec("rest normal", glm::normalize(asGlm(mesh.normals[v])), asGlm(normals[v]), 1e-3f);
	}
}

void ArmMeshTest::testHalfWeightLandsHalfWay() {
	// Joint 0 at the origin, joint 1 (its child) at x = 1; the vertex sits at x = 2 and is weighted 0.5 / 0.5
	vr::SkinnedMesh mesh = tinyMesh(2);
	mesh.skeleton[1].parent = 0;
	mesh.skeleton[1].restLocal = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
	mesh.skeleton[1].inverseBind = glm::inverse(mesh.skeleton[1].restLocal);
	addVertex(mesh, glm::vec3(2.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), { 0, 1, 0, 0 }, { 0.5f, 0.5f, 0.f, 0.f });

	std::vector<glm::mat4> world;
	vr::restWorldMatrices(mesh, world);
	CPPUNIT_ASSERT_EQUAL(size_t(2), world.size());
	nearVec("joint 1 at rest", glm::vec3(1.f, 0.f, 0.f), glm::vec3(world[1][3]), 1e-5f);

	// Joint 1 rotated 90 degrees about Z around its own origin
	std::vector<glm::mat4> posed = world;
	posed[1] = world[1] * glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));

	std::vector<Vec3f> positions, normals;
	vr::skinMesh(mesh, posed, positions, normals);
	CPPUNIT_ASSERT_EQUAL(size_t(1), positions.size());

	// Rigid results: joint 0 leaves the vertex at (2, 0, 0); joint 1 puts it at (1, 1, 0); half way is (1.5, 0.5, 0)
	nearVec("half way", glm::vec3(1.5f, 0.5f, 0.f), asGlm(positions[0]), 1e-4f);
	glm::vec3 rigid0 = glm::vec3(posed[0] * glm::vec4(2.f, 0.f, 0.f, 1.f));
	glm::vec3 rigid1 = glm::vec3(posed[1] * mesh.skeleton[1].inverseBind * glm::vec4(2.f, 0.f, 0.f, 1.f));
	nearVec("mean of the rigid results", (rigid0 + rigid1) * 0.5f, asGlm(positions[0]), 1e-4f);
}

void ArmMeshTest::testFourJointVertexMatchesHandComputation() {
	// Four root joints with identity inverse bind, posed by pure translations: the result is p + sum(w_i * t_i)
	vr::SkinnedMesh mesh = tinyMesh(4);
	addVertex(mesh, glm::vec3(1.f, 1.f, 1.f), glm::vec3(0.f, 0.f, 1.f), { 0, 1, 2, 3 }, { 0.1f, 0.2f, 0.3f, 0.4f });
	std::vector<glm::mat4> posed(4, glm::mat4(1.f));
	posed[0] = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 0.f, 0.f));
	posed[1] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.f, 0.f));
	posed[2] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, 3.f));
	posed[3] = glm::translate(glm::mat4(1.f), glm::vec3(-1.f, 0.f, 0.f));
	std::vector<Vec3f> positions, normals;
	vr::skinMesh(mesh, posed, positions, normals);
	// x: 1 + 0.1 * 1 + 0.4 * -1 = 0.7; y: 1 + 0.2 * 2 = 1.4; z: 1 + 0.3 * 3 = 1.9
	nearVec("four joints", glm::vec3(0.7f, 1.4f, 1.9f), asGlm(positions[0]), 1e-5f);
	// Translations do not move a normal
	nearVec("four joints normal", glm::vec3(0.f, 0.f, 1.f), asGlm(normals[0]), 1e-5f);

	// With a rotation: joint 0 turned 90 degrees about Z maps (1, 1, 1) to (-1, 1, 1); the others stay
	posed.assign(4, glm::mat4(1.f));
	posed[0] = glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	vr::skinMesh(mesh, posed, positions, normals);
	// 0.1 * (-1, 1, 1) + 0.9 * (1, 1, 1) = (0.8, 1, 1)
	nearVec("four joints rotated", glm::vec3(0.8f, 1.f, 1.f), asGlm(positions[0]), 1e-5f);
}

void ArmMeshTest::testNormalsAreRenormalised() {
	// One joint scaled by 3 stretches the normal, the other turns it: the blend must come out unit length
	vr::SkinnedMesh mesh = tinyMesh(2);
	addVertex(mesh, glm::vec3(0.f), glm::vec3(1.f, 0.f, 0.f), { 0, 1, 0, 0 }, { 0.5f, 0.5f, 0.f, 0.f });
	std::vector<glm::mat4> posed(2, glm::mat4(1.f));
	posed[0] = glm::scale(glm::mat4(1.f), glm::vec3(3.f));
	posed[1] = glm::rotate(glm::mat4(1.f), glm::half_pi<float>(), glm::vec3(0.f, 0.f, 1.f));
	std::vector<Vec3f> positions, normals;
	vr::skinMesh(mesh, posed, positions, normals);
	expectNear("normal length", 1.f, glm::length(asGlm(normals[0])), 1e-4f);
	// The blend of (3, 0, 0) and (0, 1, 0) points between +x and +y
	nearVec("blended normal", glm::normalize(glm::vec3(1.5f, 0.5f, 0.f)), asGlm(normals[0]), 1e-4f);
}

void ArmMeshTest::testSleeveCounts() {
	vr::SleeveParams params;
	CPPUNIT_ASSERT(params.rings >= 12);
	CPPUNIT_ASSERT(params.sides >= 12 && params.sides <= 16);
	expectNear("upper length", 27.6f, params.upperLength, 1e-4f);
	expectNear("forearm length", 30.5f, params.forearmLength, 1e-4f);

	vr::SkinnedMesh sleeve = vr::buildSleeve(params);
	CPPUNIT_ASSERT(sleeve.positions.size() >= size_t(params.rings * params.sides));
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), sleeve.normals.size());
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), sleeve.uvs.size());
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), sleeve.joints.size());
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), sleeve.weights.size());
	CPPUNIT_ASSERT(sleeve.skeleton.size() >= size_t(vr::SleeveWrist) + 1);
	CPPUNIT_ASSERT(!sleeve.indices.empty());
	CPPUNIT_ASSERT_EQUAL(size_t(0), sleeve.indices.size() % 3);
	for(u16 index : sleeve.indices) {
		CPPUNIT_ASSERT(index < sleeve.positions.size());
	}

	// At least `rings` distinct stations along the arm
	std::vector<float> stations;
	for(const Vec3f & p : sleeve.positions) {
		if(std::none_of(stations.begin(), stations.end(), [&](float x) { return std::abs(x - p.x) < 1e-3f; })) {
			stations.push_back(p.x);
		}
	}
	CPPUNIT_ASSERT(stations.size() >= size_t(params.rings));

	// A smaller resolution is honoured
	vr::SleeveParams smallParams;
	smallParams.rings = 12;
	smallParams.sides = 12;
	vr::SkinnedMesh smallSleeve = vr::buildSleeve(smallParams);
	CPPUNIT_ASSERT(smallSleeve.positions.size() >= size_t(144));
	CPPUNIT_ASSERT(smallSleeve.positions.size() < sleeve.positions.size());
}

void ArmMeshTest::testSleeveWeightsSumToOne() {
	vr::SkinnedMesh sleeve = vr::buildSleeve(vr::SleeveParams());
	for(size_t v = 0; v < sleeve.weights.size(); v++) {
		float sum = 0.f;
		for(int k = 0; k < 4; k++) {
			CPPUNIT_ASSERT(sleeve.weights[v][k] >= 0.f);
			if(sleeve.weights[v][k] > 0.f) {
				CPPUNIT_ASSERT(sleeve.joints[v][k] < sleeve.skeleton.size());
			}
			sum += sleeve.weights[v][k];
		}
		expectNear("sleeve weight sum", 1.f, sum, 1e-4f);
	}
}

void ArmMeshTest::testSleeveElbowRingIsWeightedAcrossBothJoints() {
	vr::SleeveParams params;
	vr::SkinnedMesh sleeve = vr::buildSleeve(params);
	// The ring at the elbow joint is weighted 50/50 between the upper arm side and the forearm side
	std::vector<size_t> ring = verticesNearestX(sleeve, params.upperLength);
	CPPUNIT_ASSERT(!ring.empty());
	for(size_t v : ring) {
		expectNear("elbow ring x", params.upperLength, sleeve.positions[v].x, 0.5f);
		float upperSide = weightOn(sleeve, v, { vr::SleeveShoulder, vr::SleeveUpperTwist });
		float forearmSide = weightOn(sleeve, v, { vr::SleeveElbow, vr::SleeveForearmTwist1,
		                                          vr::SleeveForearmTwist2, vr::SleeveWrist });
		expectNear("elbow ring upper side", 0.5f, upperSide, 0.05f);
		expectNear("elbow ring forearm side", 0.5f, forearmSide, 0.05f);
	}

	// A ring 2 to 3 units on each side of it
	for(float side : { -1.f, 1.f }) {
		bool found = false;
		for(const Vec3f & p : sleeve.positions) {
			float d = (p.x - params.upperLength) * side;
			if(d >= 2.f - 1e-3f && d <= 3.f + 1e-3f) {
				found = true;
			}
		}
		CPPUNIT_ASSERT_MESSAGE("no ring 2 to 3 units from the elbow ring", found);
	}

	// The forearm twist rings are dominated by their own twist joint
	const std::pair<float, int> twists[] = {
		{ params.upperLength + params.forearmLength / 3.f, vr::SleeveForearmTwist1 },
		{ params.upperLength + params.forearmLength * 2.f / 3.f, vr::SleeveForearmTwist2 },
	};
	for(const auto & twist : twists) {
		for(size_t v : verticesNearestX(sleeve, twist.first)) {
			int best = -1;
			float bestWeight = 0.f;
			for(int j = 0; j < int(sleeve.skeleton.size()); j++) {
				float w = weightOn(sleeve, v, { j });
				if(w > bestWeight) {
					bestWeight = w;
					best = j;
				}
			}
			CPPUNIT_ASSERT_EQUAL(twist.second, best);
		}
	}

	// The ends belong to their own end of the chain
	for(size_t v : verticesNearestX(sleeve, 0.f)) {
		CPPUNIT_ASSERT(weightOn(sleeve, v, { vr::SleeveShoulder, vr::SleeveUpperTwist }) > 0.5f);
	}
	for(size_t v : verticesNearestX(sleeve, params.upperLength + params.forearmLength)) {
		CPPUNIT_ASSERT(weightOn(sleeve, v, { vr::SleeveForearmTwist2, vr::SleeveWrist }) > 0.5f);
	}
}

void ArmMeshTest::testSleeveSpansTheArm() {
	vr::SleeveParams params;
	vr::SkinnedMesh sleeve = vr::buildSleeve(params);
	float minX = 1e9f, maxX = -1e9f;
	for(const Vec3f & p : sleeve.positions) {
		minX = std::min(minX, p.x);
		maxX = std::max(maxX, p.x);
	}
	// Units are about a centimetre: within one
	expectNear("shoulder end", 0.f, minX, 1.f);
	expectNear("wrist end", params.upperLength + params.forearmLength, maxX, 1.f);

	// The lengths are honoured, not hard-coded
	vr::SleeveParams longer;
	longer.upperLength = 40.f;
	longer.forearmLength = 35.f;
	vr::SkinnedMesh longSleeve = vr::buildSleeve(longer);
	float longMax = -1e9f;
	for(const Vec3f & p : longSleeve.positions) {
		longMax = std::max(longMax, p.x);
	}
	expectNear("longer wrist end", 75.f, longMax, 1.f);

	// The joints sit on the arm: the elbow upperLength from the shoulder, the wrist at the far end
	std::vector<glm::mat4> world;
	vr::restWorldMatrices(sleeve, world);
	expectNear("elbow joint x", params.upperLength, world[vr::SleeveElbow][3][0], 1.f);
	expectNear("wrist joint x", params.upperLength + params.forearmLength, world[vr::SleeveWrist][3][0], 1.f);
}

void ArmMeshTest::testRecomputeNormalsOfAFlatStrip() {
	// A strip of two quads in the plane z = 0: every smooth normal is the plane normal (+z or -z by winding)
	vr::SkinnedMesh mesh = tinyMesh(1);
	for(int i = 0; i < 3; i++) {
		addVertex(mesh, glm::vec3(float(i), 0.f, 0.f), glm::vec3(1.f, 0.f, 0.f), { 0, 0, 0, 0 }, { 1.f, 0.f, 0.f, 0.f });
		addVertex(mesh, glm::vec3(float(i), 1.f, 0.f), glm::vec3(1.f, 0.f, 0.f), { 0, 0, 0, 0 }, { 1.f, 0.f, 0.f, 0.f });
	}
	for(u16 i = 0; i < 2; i++) {
		u16 a = u16(i * 2), b = u16(i * 2 + 1), c = u16(i * 2 + 2), d = u16(i * 2 + 3);
		for(u16 index : { a, c, b, b, c, d }) {
			mesh.indices.push_back(index);
		}
	}
	std::vector<Vec3f> normals;
	vr::recomputeNormals(mesh, mesh.positions, normals);
	CPPUNIT_ASSERT_EQUAL(mesh.positions.size(), normals.size());
	float sign = normals[0].z < 0.f ? -1.f : 1.f;
	for(const Vec3f & n : normals) {
		nearVec("flat strip normal", glm::vec3(0.f, 0.f, sign), asGlm(n), 1e-4f);
	}

	// The same strip lifted into the plane x = 0 has its normal along x
	std::vector<Vec3f> turned;
	for(const Vec3f & p : mesh.positions) {
		turned.push_back(Vec3f(0.f, p.y, p.x));
	}
	vr::recomputeNormals(mesh, turned, normals);
	for(const Vec3f & n : normals) {
		expectNear("turned strip normal", 1.f, std::abs(n.x), 1e-4f);
		expectNear("turned strip normal y", 0.f, n.y, 1e-4f);
		expectNear("turned strip normal z", 0.f, n.z, 1e-4f);
	}

	// A whole sleeve: unit-length normals everywhere
	vr::SkinnedMesh sleeve = vr::buildSleeve(vr::SleeveParams());
	vr::recomputeNormals(sleeve, sleeve.positions, normals);
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), normals.size());
	for(const Vec3f & n : normals) {
		expectNear("sleeve normal length", 1.f, glm::length(asGlm(n)), 1e-3f);
	}
}

void ArmMeshTest::testElbowCorrective() {
	vr::SleeveParams params;
	vr::SkinnedMesh sleeve = vr::buildSleeve(params);
	const float k = 2.f;

	// Bend 0 changes nothing
	std::vector<Vec3f> unbent = sleeve.positions;
	vr::applyElbowCorrective(sleeve, 0.f, k, unbent);
	CPPUNIT_ASSERT_EQUAL(sleeve.positions.size(), unbent.size());
	for(size_t v = 0; v < unbent.size(); v++) {
		nearVec("unbent", asGlm(sleeve.positions[v]), asGlm(unbent[v]), 1e-5f);
	}

	// At 90 degrees the outer side (-Y) of the elbow ring moves out by k * sin(45 degrees), the inner side (+Y) in
	std::vector<Vec3f> bent = sleeve.positions;
	vr::applyElbowCorrective(sleeve, 90.f, k, bent);
	const float push = k * std::sin(glm::radians(45.f));
	std::vector<size_t> ring = verticesNearestX(sleeve, params.upperLength);
	size_t outer = ring[0], inner = ring[0];
	for(size_t v : ring) {
		if(sleeve.positions[v].y < sleeve.positions[outer].y) {
			outer = v;
		}
		if(sleeve.positions[v].y > sleeve.positions[inner].y) {
			inner = v;
		}
	}
	float outerDy = bent[outer].y - sleeve.positions[outer].y;
	CPPUNIT_ASSERT_MESSAGE("the outer elbow vertex must move outward (y decreases)", outerDy < -1e-3f);
	CPPUNIT_ASSERT(-outerDy <= push + 1e-3f);
	if(std::abs(sleeve.positions[outer].z) < 1e-3f) {
		expectNear("outer elbow push", -push, outerDy, 1e-3f);
	}
	// Inner: its distance to the arm's axis shrinks
	float innerBefore = radiusOf(sleeve.positions[inner]);
	float innerAfter = radiusOf(bent[inner]);
	CPPUNIT_ASSERT_MESSAGE("the inner elbow vertex must move toward the fold", innerAfter < innerBefore - 1e-3f);

	// Vertices far from the elbow are unchanged
	for(size_t v = 0; v < bent.size(); v++) {
		if(std::abs(sleeve.positions[v].x - params.upperLength) > 12.f) {
			nearVec("far from the elbow", asGlm(sleeve.positions[v]), asGlm(bent[v]), 1e-5f);
		}
	}
}

void ArmMeshTest::testSleeveTopRingIsWider() {
	vr::SleeveParams params;
	vr::SkinnedMesh sleeve = vr::buildSleeve(params);
	float topRadius = 0.f;
	for(size_t v : verticesNearestX(sleeve, 0.f)) {
		topRadius = std::max(topRadius, radiusOf(sleeve.positions[v]));
	}
	float elbowRadius = 0.f;
	for(size_t v : verticesNearestX(sleeve, params.upperLength)) {
		elbowRadius = std::max(elbowRadius, radiusOf(sleeve.positions[v]));
	}
	CPPUNIT_ASSERT(elbowRadius > 0.f);
	CPPUNIT_ASSERT_MESSAGE("the top ring must be wider than the elbow ring", topRadius > elbowRadius);
}
