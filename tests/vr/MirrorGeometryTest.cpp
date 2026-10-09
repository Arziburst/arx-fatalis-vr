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

#include "vr/MirrorGeometryTest.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

CppUnit::AutoRegisterSuite<MirrorGeometryTest> g_registerMirrorGeometryTest;

namespace {

const double Epsilon = 1e-4;

vr::MirrorFov fov(float left, float right, float up, float down) {
	vr::MirrorFov result;
	result.left = left;
	result.right = right;
	result.up = up;
	result.down = down;
	return result;
}

//! A Valve Index-like left eye: more field of view to the outside (left) than to the nose
vr::MirrorFov indexLeftEye() {
	return fov(std::atan(-1.4f), std::atan(1.0f), std::atan(1.2f), std::atan(-1.2f));
}

const glm::quat Ahead(1.f, 0.f, 0.f, 0.f);
const Vec2i IndexEye(2016, 2240);
const Vec2i TestWindow(1280, 720);

} // anonymous namespace

void MirrorGeometryTest::testSymmetricFovLooksAtTheCenter() {
	Vec2f forward = vr::mirrorForwardPoint(fov(-0.8f, 0.8f, 0.8f, -0.8f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, forward.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, forward.y, Epsilon);
}

void MirrorGeometryTest::testLeftEyeLooksRightOfTheCenter() {
	Vec2f forward = vr::mirrorForwardPoint(indexLeftEye());
	// tan -1.4 .. 1.0: straight ahead is 1.4 / 2.4 of the way across
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.4 / 2.4, forward.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, forward.y, Epsilon);
}

void MirrorGeometryTest::testWideWindowCropsTheHeight() {
	vr::MirrorRect crop = vr::mirrorCrop(indexLeftEye(), IndexEye, TestWindow);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, crop.u0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, crop.u1, Epsilon);
	// The shown part has the window's aspect in pixels: 2016 x (2240 * height) = 16:9
	double height = double(crop.v1 - crop.v0) * double(IndexEye.y);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1280.0 / 720.0, double(IndexEye.x) / height, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, (crop.v0 + crop.v1) * 0.5, Epsilon);
}

void MirrorGeometryTest::testTallWindowCropsTheWidth() {
	vr::MirrorRect crop = vr::mirrorCrop(fov(-0.8f, 0.8f, 0.8f, -0.8f), Vec2i(1000, 1000), Vec2i(500, 1000));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, crop.u0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.75, crop.u1, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, crop.v0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, crop.v1, Epsilon);
}

void MirrorGeometryTest::testCropFollowsTheForwardPoint() {
	// A tall window over the asymmetric eye: centered on where the eye looks, not on the image
	vr::MirrorRect crop = vr::mirrorCrop(indexLeftEye(), Vec2i(1000, 1000), Vec2i(500, 1000));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.4 / 2.4, (crop.u0 + crop.u1) * 0.5, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, crop.u1 - crop.u0, Epsilon);
}

void MirrorGeometryTest::testCropStaysInsideTheImage() {
	// Looking almost at the bottom edge: the crop is pushed back up into the image
	vr::MirrorFov low = fov(-0.8f, 0.8f, std::atan(3.f), std::atan(-0.1f));
	vr::MirrorRect crop = vr::mirrorCrop(low, Vec2i(1000, 1000), Vec2i(1000, 500));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, crop.v0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, crop.v1, Epsilon);
	// And almost at the top edge
	vr::MirrorFov high = fov(-0.8f, 0.8f, std::atan(0.1f), std::atan(-3.f));
	crop = vr::mirrorCrop(high, Vec2i(1000, 1000), Vec2i(1000, 500));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, crop.v0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, crop.v1, Epsilon);
}

void MirrorGeometryTest::testEmptySizesKeepTheWholeImage() {
	vr::MirrorRect crop = vr::mirrorCrop(indexLeftEye(), IndexEye, Vec2i(0, 0));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, crop.u0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, crop.u1, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, crop.v0, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, crop.v1, Epsilon);
}

void MirrorGeometryTest::testForwardPointProjectsToItself() {
	vr::MirrorFov eye = indexLeftEye();
	Vec3f position(-0.032f, 0.f, 0.f);
	Vec2f uv(0.f);
	float depth = 0.f;
	CPPUNIT_ASSERT(vr::mirrorProject(position + Vec3f(0.f, 0.f, -2.f), position, Ahead, eye, uv, depth));
	Vec2f forward = vr::mirrorForwardPoint(eye);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(forward.x, uv.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(forward.y, uv.y, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, depth, Epsilon);
}

void MirrorGeometryTest::testPointBehindTheEyeIsRefused() {
	Vec2f uv(0.f);
	float depth = 0.f;
	CPPUNIT_ASSERT(!vr::mirrorProject(Vec3f(0.f, 0.f, 1.f), Vec3f(0.f), Ahead, indexLeftEye(), uv, depth));
	// Turned around (half a turn about y), the same point is in front
	glm::quat turned = glm::angleAxis(glm::pi<float>(), Vec3f(0.f, 1.f, 0.f));
	CPPUNIT_ASSERT(vr::mirrorProject(Vec3f(0.f, 0.f, 1.f), Vec3f(0.f), turned, indexLeftEye(), uv, depth));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, depth, Epsilon);
}

void MirrorGeometryTest::testHeadCenterAppearsRightOfTheLeftEyesForward() {
	// The flat frame quad is centered in front of the head; the left eye sits 32 mm to the left
	vr::MirrorFov eye = indexLeftEye();
	Vec2f uv(0.f);
	float depth = 0.f;
	CPPUNIT_ASSERT(vr::mirrorProject(Vec3f(0.f, 0.f, -1.75f), Vec3f(-0.032f, 0.f, 0.f), Ahead, eye, uv, depth));
	Vec2f forward = vr::mirrorForwardPoint(eye);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(forward.x + (0.032 / 1.75) / 2.4, uv.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(forward.y, uv.y, Epsilon);
}

void MirrorGeometryTest::testCropCornersAreTheWindowCorners() {
	vr::MirrorRect crop = vr::mirrorCrop(indexLeftEye(), IndexEye, TestWindow);
	Vec2f bottomLeft = vr::mirrorToWindow(crop, Vec2f(crop.u0, crop.v0));
	Vec2f topRight = vr::mirrorToWindow(crop, Vec2f(crop.u1, crop.v1));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.0, bottomLeft.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.0, bottomLeft.y, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, topRight.x, Epsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, topRight.y, Epsilon);
}
