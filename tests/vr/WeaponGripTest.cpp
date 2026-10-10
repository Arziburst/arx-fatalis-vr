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

#include "vr/WeaponGripTest.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/ArmIK.h"
#include "vr/WeaponGrip.h"

CppUnit::AutoRegisterSuite<WeaponGripTest> g_registerWeaponGripTest;

namespace {

// Names unique to this file: the unity build sees every test file's helpers (TRAPS.md)

void gripCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.002f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void gripCheckFloat(const char * what, float expected, float actual, float tolerance = 0.01f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

/*!
 * A sword relative to its attach vertex: a hilt reaching \a pommel units behind the attach point and a
 * blade \a blade units ahead along \a along, a crossguard 24 wide across it at the attach point, the
 * blade 4 wide; the tip is the last vertex
 */
std::vector<Vec3f> swordMesh(const Vec3f & along, float pommel, float blade) {
	const Vec3f d = glm::normalize(along);
	const Vec3f side = glm::normalize(glm::cross(d, std::abs(d.y) < 0.9f ? Vec3f(0.f, 1.f, 0.f) : Vec3f(1.f, 0.f, 0.f)));
	const Vec3f flat = glm::cross(d, side);
	std::vector<Vec3f> mesh;
	mesh.push_back(-d * pommel);
	mesh.push_back(-d * pommel + side * 1.5f);
	mesh.push_back(side * 12.f);
	mesh.push_back(-side * 12.f);
	mesh.push_back(flat * 1.f);
	for(float t = 10.f; t < blade; t += 10.f) {
		mesh.push_back(d * t + side * 2.f);
		mesh.push_back(d * t - side * 2.f);
		mesh.push_back(d * t + flat * 0.5f);
	}
	mesh.push_back(d * blade);
	return mesh;
}

} // anonymous namespace

void WeaponGripTest::testBladeAxisOfASword() {

	const Vec3f directions[] = {
		Vec3f(0.f, 0.f, 1.f), Vec3f(0.f, -1.f, 0.f), Vec3f(1.f, 2.f, -3.f), Vec3f(-0.3f, 0.1f, 0.95f),
	};
	for(const Vec3f & direction : directions) {
		const std::vector<Vec3f> mesh = swordMesh(direction, 12.f, 80.f);
		const vr::BladeAxis blade = vr::bladeAxisFromMesh(mesh);
		CPPUNIT_ASSERT(blade.valid);
		// The crossguard pulls the principal axis by a fraction of a degree at most
		CPPUNIT_ASSERT_MESSAGE("blade along the sword", vr::angleDegrees(direction, blade.axis) < 1.f);
		CPPUNIT_ASSERT_EQUAL(mesh.size() - 1, blade.tip);
		gripCheckFloat("blade length", 80.f, blade.length, 0.1f);
	}
}

void WeaponGripTest::testBladeAxisPointsAwayFromThePommel() {

	// The attach point near one end: the axis points at the far end, whichever way the mesh was built
	const vr::BladeAxis forward = vr::bladeAxisFromMesh(swordMesh(Vec3f(0.f, 0.f, 1.f), 5.f, 60.f));
	gripCheckVector("tip ahead", Vec3f(0.f, 0.f, 1.f), forward.axis, 0.01f);
	const vr::BladeAxis backward = vr::bladeAxisFromMesh(swordMesh(Vec3f(0.f, 0.f, -1.f), 5.f, 60.f));
	gripCheckVector("tip behind", Vec3f(0.f, 0.f, -1.f), backward.axis, 0.01f);

	// A dagger held in the middle of a long hilt still points at its blade
	const vr::BladeAxis dagger = vr::bladeAxisFromMesh(swordMesh(Vec3f(1.f, 0.f, 0.f), 15.f, 30.f));
	gripCheckVector("dagger", Vec3f(1.f, 0.f, 0.f), dagger.axis, 0.02f);
	gripCheckFloat("dagger length", 30.f, dagger.length, 0.1f);
}

void WeaponGripTest::testDegenerateMeshes() {

	CPPUNIT_ASSERT(!vr::bladeAxisFromMesh(std::vector<Vec3f>()).valid);
	CPPUNIT_ASSERT(!vr::bladeAxisFromMesh(std::vector<Vec3f>(1, Vec3f(3.f, 0.f, 0.f))).valid);
	const vr::BladeAxis same = vr::bladeAxisFromMesh(std::vector<Vec3f>(5, Vec3f(1.f, 2.f, 3.f)));
	CPPUNIT_ASSERT(!same.valid);
	CPPUNIT_ASSERT(std::isfinite(same.axis.x) && std::isfinite(same.axis.y) && std::isfinite(same.axis.z));

	gripCheckFloat("no angle to a zero vector", 0.f, vr::angleDegrees(Vec3f(0.f), Vec3f(1.f, 0.f, 0.f)));
	gripCheckFloat("right angle", 90.f, vr::angleDegrees(Vec3f(2.f, 0.f, 0.f), Vec3f(0.f, 0.f, 5.f)));
	gripCheckFloat("opposite", 180.f, vr::angleDegrees(Vec3f(0.f, 1.f, 0.f), Vec3f(0.f, -3.f, 0.f)));
}

void WeaponGripTest::testBladeOntoHandle() {

	const Vec3f pairs[][2] = {
		{ Vec3f(0.f, 1.f, 0.f), Vec3f(0.f, 0.f, 1.f) },
		{ Vec3f(1.f, 0.f, 0.f), Vec3f(-1.f, 0.f, 0.f) },
		{ glm::normalize(Vec3f(1.f, 2.f, 3.f)), glm::normalize(Vec3f(-2.f, 0.5f, 1.f)) },
		{ Vec3f(0.f, 0.f, 1.f), Vec3f(0.f, 0.f, 1.f) },
	};
	for(const auto & pair : pairs) {
		const glm::quat q = vr::bladeOntoHandle(pair[0], pair[1]);
		gripCheckVector("blade turned onto the thumb axis", pair[1], q * pair[0]);
		gripCheckFloat("a unit quaternion", 1.f, glm::length(q), 0.001f);
	}
}

void WeaponGripTest::testHeldBladeLiesOnGripMinusZ() {

	// A hand frame at rest (any orientation), a controller turned somewhere: the hand bone's world rotation
	// is the grip's times handToGrip, as the body module poses it (vr/VRBody.cpp)
	vr::HandFrame frame;
	frame.valid = true;
	frame.knuckles = glm::normalize(Vec3f(0.2f, 0.1f, 1.f));
	frame.thumb = glm::normalize(glm::cross(Vec3f(0.f, 1.f, 0.f), frame.knuckles));
	frame.palm = glm::normalize(glm::cross(frame.knuckles, frame.thumb));
	const glm::quat toGrip = vr::handToGrip(frame);
	const glm::quat grips[] = {
		glm::quat(1.f, 0.f, 0.f, 0.f),
		glm::normalize(glm::quat(0.8f, 0.3f, -0.4f, 0.2f)),
		glm::angleAxis(glm::radians(120.f), glm::normalize(Vec3f(1.f, -1.f, 0.5f))),
	};
	for(const glm::quat & grip : grips) {
		const glm::quat handWorld = grip * toGrip;
		// The hand's own axes, seen from the grip: knuckles on -Y, thumb on -Z (the OpenXR grip pose), palm on +X
		gripCheckVector("knuckles on grip -Y", Vec3f(0.f, -1.f, 0.f),
		                vr::directionInGrip(handWorld * frame.knuckles, handWorld, toGrip));
		gripCheckVector("thumb on grip -Z", Vec3f(0.f, 0.f, -1.f),
		                vr::directionInGrip(handWorld * frame.thumb, handWorld, toGrip));
		gripCheckVector("palm on grip +X", Vec3f(1.f, 0.f, 0.f),
		                vr::directionInGrip(handWorld * frame.palm, handWorld, toGrip));
		// A blade the asset puts anywhere in the fist ends up on grip -Z once corrected
		const Vec3f assetBlades[] = { frame.knuckles, -frame.palm, glm::normalize(Vec3f(1.f, -2.f, 0.3f)) };
		for(const Vec3f & assetBlade : assetBlades) {
			const glm::quat correction = vr::bladeOntoHandle(assetBlade, frame.thumb);
			const Vec3f worldBlade = handWorld * (correction * assetBlade);
			gripCheckVector("corrected blade on grip -Z", Vec3f(0.f, 0.f, -1.f),
			                vr::directionInGrip(worldBlade, handWorld, toGrip));
		}
	}
}
