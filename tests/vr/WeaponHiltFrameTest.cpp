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

#include "vr/WeaponHiltFrameTest.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vr/WeaponGrip.h"

CppUnit::AutoRegisterSuite<WeaponHiltFrameTest> g_registerWeaponHiltFrameTest;

namespace {

// Names unique to this file (prefix whf): the unity build sees every test file's helpers (TRAPS.md)

void whfCheck(const char * what, float expected, float actual, float tolerance) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! A ring of \a count vertices of \a radius about the z axis at \a z
void whfRing(std::vector<Vec3f> & vertices, float z, float radius, int count = 6) {
	for(int i = 0; i < count; i++) {
		const float a = 6.2831853f * float(i) / float(count);
		vertices.emplace_back(radius * std::cos(a), radius * std::sin(a), z);
	}
}

/*!
 * A sword along +z, relative to its attach vertex: the handle (radius 1.5) from the pommel at -6 to the guard at 7
 * (10 wide along x), the blade 5 wide along x and 0.8 thick to the tip at 80
 */
std::vector<Vec3f> whfSword() {
	std::vector<Vec3f> vertices;
	for(float z : { -6.f, -3.f, 0.f, 3.f, 6.f }) {
		whfRing(vertices, z, 1.5f);
	}
	for(float y : { -0.6f, 0.6f }) {
		vertices.emplace_back(-5.f, y, 7.f);
		vertices.emplace_back(5.f, y, 7.f);
	}
	for(float z = 8.f; z < 80.f; z += 8.f) {
		vertices.emplace_back(-2.5f, 0.f, z);
		vertices.emplace_back(2.5f, 0.f, z);
		vertices.emplace_back(0.f, -0.4f, z);
		vertices.emplace_back(0.f, 0.4f, z);
	}
	vertices.emplace_back(0.f, 0.f, 80.f);
	return vertices;
}

} // anonymous namespace

void WeaponHiltFrameTest::testSwordHandleFromMesh() {
	const std::vector<Vec3f> sword = whfSword();
	const vr::BladeAxis blade = vr::bladeAxisFromMesh(sword);
	CPPUNIT_ASSERT(blade.valid);
	CPPUNIT_ASSERT(blade.axis.z > 0.99f);
	const vr::WeaponHandle handle = vr::weaponHandleFromMesh(sword, blade);
	CPPUNIT_ASSERT(handle.valid);
	CPPUNIT_ASSERT(handle.guardFound);
	whfCheck("pommel", -6.f, handle.pommel, 0.01f);
	whfCheck("handle length: the pommel to the guard", 13.f, handle.length, 0.01f);
	whfCheck("handle radius", 1.5f, handle.radius, 0.05f);
	whfCheck("centre line on the axis", 0.f, glm::length(handle.centre), 0.01f);
	// The edge across the blade's width (x), never its thickness, perpendicular to the blade
	CPPUNIT_ASSERT(std::abs(handle.edge.x) > 0.99f);
	whfCheck("edge perpendicular to the blade", 0.f, glm::dot(handle.edge, blade.axis), 0.001f);
	// The sword's anchor at the handle's centre: 6.5 above the pommel, 0.5 past the attach vertex
	const float offset = vr::weaponAnchorOffset(vr::WeaponClass::Sword, handle.length, 9.f);
	whfCheck("sword anchor along the axis", 0.5f, handle.pommel + offset, 0.01f);
}

void WeaponHiltFrameTest::testAxeHandleAndBit() {
	// A shaft (radius 1) from -20 to 30 off a centre line at x = 0.5, the head from 30 to 40: the bit 10 out at +x,
	// the back 3 out at -x
	std::vector<Vec3f> axe;
	for(float z = -20.f; z <= 30.f; z += 5.f) {
		for(int i = 0; i < 6; i++) {
			const float a = 6.2831853f * float(i) / 6.f;
			axe.emplace_back(0.5f + std::cos(a), std::sin(a), z);
		}
	}
	for(float z : { 31.f, 36.f, 40.f }) {
		axe.emplace_back(10.5f, 0.f, z);
		axe.emplace_back(-2.5f, 0.f, z);
		axe.emplace_back(0.5f, 0.5f, z);
		axe.emplace_back(0.5f, -0.5f, z);
	}
	const vr::BladeAxis blade = vr::bladeAxisFromMesh(axe);
	CPPUNIT_ASSERT(blade.valid);
	CPPUNIT_ASSERT(std::abs(blade.axis.z) > 0.98f);
	const vr::WeaponHandle handle = vr::weaponHandleFromMesh(axe, blade);
	CPPUNIT_ASSERT(handle.valid && handle.guardFound);
	CPPUNIT_ASSERT_MESSAGE("the handle runs from the shaft's end to the head",
	                       handle.length > 45.f && handle.length < 52.f);
	CPPUNIT_ASSERT_MESSAGE("the edge toward the bit, not the back", handle.edge.x > 0.9f);
	CPPUNIT_ASSERT_MESSAGE("the centre line off the attach vertex", std::abs(handle.centre.x - 0.5f) < 0.3f);
}

void WeaponHiltFrameTest::testHandleWithoutGuard() {
	// A plain rod: no vertex grows wider - the handle is taken as twice the attach vertex's depth, never past the mesh
	std::vector<Vec3f> rod;
	for(float z = -10.f; z <= 30.f; z += 5.f) {
		whfRing(rod, z, 1.f);
	}
	const vr::BladeAxis blade = vr::bladeAxisFromMesh(rod);
	const vr::WeaponHandle handle = vr::weaponHandleFromMesh(rod, blade);
	CPPUNIT_ASSERT(handle.valid);
	CPPUNIT_ASSERT(!handle.guardFound);
	whfCheck("twice the depth", 20.f, handle.length, 0.01f);
	whfCheck("perpendicular edge", 0.f, glm::dot(handle.edge, blade.axis), 0.001f);
	// Too few vertices: invalid, never NaN
	const vr::WeaponHandle none = vr::weaponHandleFromMesh(std::vector<Vec3f>(1, Vec3f(0.f)), blade);
	CPPUNIT_ASSERT(!none.valid);
}

void WeaponHiltFrameTest::testClassFromItem() {
	CPPUNIT_ASSERT(vr::weaponClassOf(true, false, "sword") == vr::WeaponClass::Dagger);
	CPPUNIT_ASSERT(vr::weaponClassOf(false, true, "axe") == vr::WeaponClass::TwoHanded);
	CPPUNIT_ASSERT(vr::weaponClassOf(false, false, "AXE") == vr::WeaponClass::Axe);
	CPPUNIT_ASSERT(vr::weaponClassOf(false, false, "club") == vr::WeaponClass::Mace);
	CPPUNIT_ASSERT(vr::weaponClassOf(false, false, "sword") == vr::WeaponClass::Sword);
	CPPUNIT_ASSERT(vr::weaponClassOf(false, false, "") == vr::WeaponClass::Sword);
	CPPUNIT_ASSERT(std::string(vr::weaponClassName(vr::WeaponClass::TwoHanded)) == "twoHanded");
}

void WeaponHiltFrameTest::testWeaponInGloveBothHands() {
	// A glove's hilt frame (the right hand, and the left as its mirror): any mesh blade and edge are drawn along the
	// handle line with the edge on the knuckles; the tilt leans the tip forward (to the knuckles) by its degrees
	const Vec3f meshBlades[] = { Vec3f(0.f, 0.f, 1.f), glm::normalize(Vec3f(0.2f, -0.9f, 0.3f)) };
	const Vec3f axes[] = { Vec3f(0.f, -1.f, 0.f), glm::normalize(Vec3f(0.3f, -0.8f, -0.5f)) };
	for(int hand = 0; hand < 2; hand++) {
		const float mirror = hand == 0 ? 1.f : -1.f;
		for(const Vec3f & meshBlade : meshBlades) {
			const Vec3f meshEdge = glm::normalize(glm::cross(meshBlade, Vec3f(0.4f, 0.5f, -0.7f)));
			for(const Vec3f & rawAxis : axes) {
				vr::GloveHilt hilt;
				hilt.axis = glm::normalize(Vec3f(rawAxis.x * mirror, rawAxis.y, rawAxis.z));
				hilt.knuckles = glm::normalize(glm::cross(hilt.axis, Vec3f(0.f, 0.f, 1.f)) * mirror + 0.3f * hilt.axis);
				const glm::quat q = vr::weaponInGlove(meshBlade, meshEdge, hilt, 0.f);
				CPPUNIT_ASSERT_MESSAGE("the blade on the handle line", vr::angleDegrees(q * meshBlade, hilt.axis) < 0.1f);
				const Vec3f edge = q * meshEdge;
				const Vec3f across = hilt.knuckles - hilt.axis * glm::dot(hilt.knuckles, hilt.axis);
				CPPUNIT_ASSERT_MESSAGE("the edge toward the knuckles (hand " + std::to_string(hand) + ")",
				                       vr::angleDegrees(edge, across) < 5.f);
				for(float tilt : { -15.f, -30.f }) {
					const glm::quat tilted = vr::weaponInGlove(meshBlade, meshEdge, hilt, tilt);
					const Vec3f drawn = tilted * meshBlade;
					whfCheck("tilt", -tilt, vr::angleDegrees(drawn, hilt.axis), 0.1f);
					CPPUNIT_ASSERT_MESSAGE("the tip leans forward", glm::dot(drawn, across) > 0.f);
					// The edge stays toward the knuckles across the tilted blade
					const Vec3f tiltedEdge = tilted * meshEdge;
					const Vec3f knucklesAcross = hilt.knuckles - drawn * glm::dot(hilt.knuckles, drawn);
					CPPUNIT_ASSERT(vr::angleDegrees(tiltedEdge, knucklesAcross) < 5.f);
				}
			}
		}
	}
}

void WeaponHiltFrameTest::testAnchorOnTheHoldPoint() {
	// The sword drawn as VRWeapon.cpp draws it - vertex v at centre + q * (v - anchor) - puts the handle's centre on
	// the glove's hold point and the guard and the pommel on the handle line, either side of it
	const std::vector<Vec3f> sword = whfSword();
	const vr::BladeAxis blade = vr::bladeAxisFromMesh(sword);
	const vr::WeaponHandle handle = vr::weaponHandleFromMesh(sword, blade);
	vr::GloveHilt hilt;
	hilt.centre = Vec3f(12.f, -140.f, 30.f);
	hilt.axis = glm::normalize(Vec3f(0.1f, -0.9f, 0.4f));
	hilt.knuckles = glm::normalize(glm::cross(hilt.axis, Vec3f(1.f, 0.f, 0.f)));
	const glm::quat q = vr::weaponInGlove(blade.axis, handle.edge, hilt, 0.f);
	const float offset = vr::weaponAnchorOffset(vr::WeaponClass::Sword, handle.length, 9.f);
	const Vec3f anchor = handle.centre + blade.axis * (handle.pommel + offset);
	auto drawn = [&](const Vec3f & v) { return hilt.centre + q * (v - anchor); };
	whfCheck("anchor on the hold point", 0.f, glm::distance(drawn(anchor), hilt.centre), 0.001f);
	const Vec3f guard = drawn(handle.centre + blade.axis * (handle.pommel + handle.length));
	const Vec3f pommel = drawn(handle.centre + blade.axis * handle.pommel);
	whfCheck("the guard half a handle toward the thumb side", 0.5f * handle.length,
	         glm::dot(guard - hilt.centre, hilt.axis), 0.01f);
	whfCheck("the pommel half a handle on the other side", -0.5f * handle.length,
	         glm::dot(pommel - hilt.centre, hilt.axis), 0.01f);
	whfCheck("the guard on the handle line", 0.f,
	         glm::length(glm::cross(guard - hilt.centre, hilt.axis)), 0.01f);
}
