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

#include "vr/StereoEffectsTest.h"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

CppUnit::AutoRegisterSuite<StereoEffectsTest> g_registerStereoEffectsTest;

namespace {

const float StereoEpsilon = 1e-4f;

vr::EyeFov stereoFovDegrees(float left, float right, float up, float down) {
	vr::EyeFov fov;
	fov.left = glm::radians(left);
	fov.right = glm::radians(right);
	fov.up = glm::radians(up);
	fov.down = glm::radians(down);
	return fov;
}

/*!
 * The Index as the game logs it (TESTING.md, "Stereo rendering"): 64 mm apart, the left eye's
 * L -54 R 40 U 44 D -55, the right eye mirrored, 5° between the eyes - each turned outward by half
 */
std::array<vr::EyeRig, 2> stereoIndexRig(float cantDegrees = 5.f) {
	vr::LocatedEye left;
	left.position = Vec3f(-0.032f, 0.f, 0.f);
	left.orientation = glm::angleAxis(glm::radians(cantDegrees / 2.f), Vec3f(0.f, 1.f, 0.f));
	left.fov = stereoFovDegrees(-54.f, 40.f, 44.f, -55.f);
	vr::LocatedEye right;
	right.position = Vec3f(0.032f, 0.f, 0.f);
	right.orientation = glm::angleAxis(glm::radians(-cantDegrees / 2.f), Vec3f(0.f, 1.f, 0.f));
	right.fov = stereoFovDegrees(-40.f, 54.f, 44.f, -55.f);
	return vr::eyeRig(left, right);
}

//! A point of the head's view space in an eye's view space
Vec3f stereoInEye(const vr::EyeRig & eye, const Vec3f & head) {
	return Vec3f(vr::eyeWorldToView(glm::mat4x4(1.f), eye) * Vec4f(head, 1.f));
}

//! How far right of the head's forward (radians) a direction in the head's view space points
float stereoAzimuth(const Vec3f & direction) {
	return std::atan2(direction.x, direction.z);
}

// A sprite 2 m away, 35° right of where the head looks: away from the middle, where the eyes' images
// differ most; the engine's size argument of a torch flame
const float StereoSpriteDistance = 200.f;
const float StereoSpriteAzimuth = glm::radians(35.f);
const float StereoSpriteSize = 30.f;
const float StereoFocal = 600.f;

Vec3f stereoSpriteCenter() {
	return Vec3f(std::sin(StereoSpriteAzimuth), 0.f, std::cos(StereoSpriteAzimuth)) * StereoSpriteDistance;
}

} // anonymous namespace

void StereoEffectsTest::testFacingAxesStraightAheadAreTheScreens() {
	vr::FacingAxes axes = vr::facingAxes(Vec3f(0.f, 0.f, 150.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, axes.right.x, StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::length(axes.right - Vec3f(1.f, 0.f, 0.f)), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::length(axes.down - Vec3f(0.f, 1.f, 0.f)), StereoEpsilon);
	// Straight above: no level direction, the head's right is kept and the plane still faces the head
	axes = vr::facingAxes(Vec3f(0.f, -80.f, 0.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::length(axes.right - Vec3f(1.f, 0.f, 0.f)), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::dot(axes.down, Vec3f(0.f, -1.f, 0.f)), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, glm::length(axes.down), StereoEpsilon);
}

void StereoEffectsTest::testFacingAxesFaceTheHead() {
	const Vec3f center(150.f, -60.f, 120.f);
	vr::FacingAxes axes = vr::facingAxes(center);
	const Vec3f facing = glm::normalize(center);
	// The plane is perpendicular to the line from the head, its axes square and unit long
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::dot(axes.right, facing), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::dot(axes.down, facing), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::dot(axes.right, axes.down), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, glm::length(axes.right), StereoEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, glm::length(axes.down), StereoEpsilon);
	// Level, and down is down
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, axes.right.y, StereoEpsilon);
	CPPUNIT_ASSERT(axes.down.y > 0.5f);
	// Right is to the right as seen from the head: right x forward points up (-y) - not a mirror image
	CPPUNIT_ASSERT(glm::dot(glm::cross(axes.right, axes.down), facing) > 0.99f);
}

void StereoEffectsTest::testHalfSizeKeepsTheEnginesPixels() {
	// The engine's sprite in pixels (graphics/Draw.cpp) for a view with ppu pixels per unit at depth 1:
	// the head-facing square straight ahead must project to exactly that many pixels
	const float ppu = 1144.f;
	for(float depth : { 50.f, 200.f, 900.f, 2500.f }) {
		const float enginePixels = ppu * StereoSpriteSize * (1.f / depth * 3000.f - 1.f) * (350.f / 310 * 0.001f);
		const float projected = ppu * vr::spriteHalfSize(StereoSpriteSize, depth, StereoFocal) / depth;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(enginePixels, projected, enginePixels * 1e-4f);
		// A fixed-share sprite (light flares: a negative size)
		const float engineShare = ppu * 12.f / StereoFocal;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(engineShare, ppu * vr::spriteHalfSize(-12.f, depth, StereoFocal) / depth, engineShare * 1e-4f);
	}
	// Past the engine's vanishing depth: a speck, never a negative size
	CPPUNIT_ASSERT(vr::spriteHalfSize(StereoSpriteSize, 3500.f, StereoFocal) > 0.f);
	CPPUNIT_ASSERT(vr::spriteHalfSize(StereoSpriteSize, 3500.f, StereoFocal) < 0.001f);
}

/*!
 * The negative control: the engine's square in each eye's own screen space, the same number of pixels
 * in both (the eyes have the same scale), is a different square in the world for each canted eye.
 * Measured as the angle the sprite spans from each eye, in the head's axes.
 */
void StereoEffectsTest::testScreenSquaresDisagreeOnCantedEyes() {

	const std::array<vr::EyeRig, 2> rig = stereoIndexRig();
	float width[2];
	for(size_t i = 0; i < 2; i++) {
		const Vec3f c = stereoInEye(rig[i], stereoSpriteCenter());
		// The engine's half size in this eye, in units of tangent: pixels / ppu
		const float half = vr::spriteHalfSize(StereoSpriteSize, c.z, StereoFocal) / c.z;
		const Vec3f leftEdge = rig[i].rotation * Vec3f(c.x / c.z - half, c.y / c.z, 1.f);
		const Vec3f rightEdge = rig[i].rotation * Vec3f(c.x / c.z + half, c.y / c.z, 1.f);
		width[i] = stereoAzimuth(rightEdge) - stereoAzimuth(leftEdge);
	}

	// From where they are the eyes would see one square within 2 %; these differ by more than 5 %
	CPPUNIT_ASSERT(std::abs(width[0] / width[1] - 1.f) > 0.05f);

	// With parallel eyes the screen squares are one square in the world
	const std::array<vr::EyeRig, 2> parallel = stereoIndexRig(0.f);
	for(size_t i = 0; i < 2; i++) {
		const Vec3f c = stereoInEye(parallel[i], stereoSpriteCenter());
		const float half = vr::spriteHalfSize(StereoSpriteSize, c.z, StereoFocal) / c.z;
		width[i] = stereoAzimuth(Vec3f(c.x / c.z + half, 0.f, 1.f)) - stereoAzimuth(Vec3f(c.x / c.z - half, 0.f, 1.f));
	}
	CPPUNIT_ASSERT(std::abs(width[0] / width[1] - 1.f) < 0.03f);
}

void StereoEffectsTest::testFacingSquareAgreesOnCantedEyes() {

	const std::array<vr::EyeRig, 2> rig = stereoIndexRig();
	const Vec3f center = stereoSpriteCenter();
	const vr::FacingAxes axes = vr::facingAxes(center);
	const float half = vr::spriteHalfSize(StereoSpriteSize, center.z, StereoFocal);
	const Vec3f leftEdge = center - axes.right * half;
	const Vec3f rightEdge = center + axes.right * half;

	float width[2];
	float distance[2];
	for(size_t i = 0; i < 2; i++) {
		// Both eyes project the same corners: the angle each sees is set by its distance alone
		const Vec3f l = stereoInEye(rig[i], leftEdge);
		const Vec3f r = stereoInEye(rig[i], rightEdge);
		CPPUNIT_ASSERT(l.z > 0.f && r.z > 0.f);
		width[i] = stereoAzimuth(rig[i].rotation * (r / r.z)) - stereoAzimuth(rig[i].rotation * (l / l.z));
		distance[i] = glm::length(center - rig[i].offset);
	}
	CPPUNIT_ASSERT(std::abs(width[0] / width[1] - 1.f) < 0.03f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(distance[1] / distance[0], width[0] / width[1], 0.005f);

	// And it is the size the engine gives that point, seen from the head
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.f * std::atan(half / StereoSpriteDistance), stereoAzimuth(rightEdge) - stereoAzimuth(leftEdge), 0.002f);
}

void StereoEffectsTest::testOverlayCoversBothCantedEyes() {

	const std::array<vr::EyeRig, 2> rig = stereoIndexRig();
	const std::array<Vec3f, 4> corners = vr::overlayCornerDirections(vr::unionFov(rig));

	// Top left, top right, bottom right, bottom left (engine y down)
	CPPUNIT_ASSERT(corners[0].x < 0.f && corners[0].y < 0.f);
	CPPUNIT_ASSERT(corners[1].x > 0.f && corners[1].y < 0.f);
	CPPUNIT_ASSERT(corners[2].x > 0.f && corners[2].y > 0.f);
	CPPUNIT_ASSERT(corners[3].x < 0.f && corners[3].y > 0.f);

	// Every corner of each eye's image is a direction inside the overlay
	for(const vr::EyeRig & eye : rig) {
		const vr::FovTangents t = vr::tangents(eye.fov);
		const Vec3f imageCorners[4] = {
			Vec3f(t.left, -t.up, 1.f), Vec3f(t.right, -t.up, 1.f),
			Vec3f(t.right, -t.down, 1.f), Vec3f(t.left, -t.down, 1.f),
		};
		for(const Vec3f & corner : imageCorners) {
			const Vec3f d = eye.rotation * corner;
			CPPUNIT_ASSERT(d.x / d.z >= corners[0].x - StereoEpsilon);
			CPPUNIT_ASSERT(d.x / d.z <= corners[1].x + StereoEpsilon);
			CPPUNIT_ASSERT(d.y / d.z >= corners[0].y - StereoEpsilon);
			CPPUNIT_ASSERT(d.y / d.z <= corners[2].y + StereoEpsilon);
		}
	}
}
