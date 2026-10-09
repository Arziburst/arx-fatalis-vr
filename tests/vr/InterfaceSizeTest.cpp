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

#include "vr/InterfaceSizeTest.h"

#include <cstdlib>

#include <glm/glm.hpp>

#include "vr/InterfaceSize.h"

CppUnit::AutoRegisterSuite<InterfaceSizeTest> g_registerInterfaceSizeTest;

namespace {

//! The head-locked quad as VRSystem places it: 2.4 m wide, 1.75 m ahead
const float UiQuadWidth = 2.4f;
const float UiQuadDistance = 1.75f;

vr::EyeFov uiDegrees(float left, float right, float up, float down) {
	vr::EyeFov fov;
	fov.left = glm::radians(left);
	fov.right = glm::radians(right);
	fov.up = glm::radians(up);
	fov.down = glm::radians(down);
	return fov;
}

//! The Valve Index's left eye as SteamVR reports it (TESTING.md, "Stereo rendering (#19)")
vr::EyeFov uiIndexFov() {
	return uiDegrees(-54.f, 40.f, 44.f, -55.f);
}

const Vec2i UiIndexEye(2468, 2740);
const Vec2i UiNoLimit(0, 0);

void checkUiNear(const char * what, s32 expected, s32 actual, s32 tolerance) {
	CPPUNIT_ASSERT_MESSAGE(what, std::abs(expected - actual) <= tolerance);
}

} // anonymous namespace

void InterfaceSizeTest::testIndexDensity() {
	// 2468 px over tan 54 + tan 40 = 2.2155 is 1114 px per unit; the quad spans 2.4 / 1.75 = 1.3714
	Vec2i size = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(640, 480), UiNoLimit);
	checkUiNear("width at the Index's density", 1528, size.x, 2);
	checkUiNear("height of a 4:3 window", 1146, size.y, 2);
}

void InterfaceSizeTest::testWindowAspectIsKept() {
	Vec2i wide = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(1920, 1080), UiNoLimit);
	checkUiNear("the width does not depend on the window", 1528, wide.x, 2);
	checkUiNear("16:9", 860, wide.y, 2);
	// A bigger window of the same shape gives the same quad: the window's size does not matter
	Vec2i smallWindow = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(800, 600), UiNoLimit);
	Vec2i bigWindow = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(2400, 1800), UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(smallWindow.x, bigWindow.x);
	CPPUNIT_ASSERT_EQUAL(smallWindow.y, bigWindow.y);
}

void InterfaceSizeTest::testClampedToLimits() {
	Vec2i tiny = vr::interfaceResolution(Vec2i(300, 300), uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(640, 480), UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(vr::MinInterfaceWidth, tiny.x);
	CPPUNIT_ASSERT_EQUAL(s32(480), tiny.y);
	Vec2i huge = vr::interfaceResolution(Vec2i(20000, 20000), uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(640, 480), UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(vr::MaxInterfaceWidth, huge.x);
	CPPUNIT_ASSERT_EQUAL(s32(3072), huge.y);
}

void InterfaceSizeTest::testClampedToTheSwapchain() {
	Vec2i byWidth = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(640, 480), Vec2i(1024, 4096));
	CPPUNIT_ASSERT_EQUAL(s32(1024), byWidth.x);
	CPPUNIT_ASSERT_EQUAL(s32(768), byWidth.y);
	// A tall window hits the height limit first; the shape is kept
	Vec2i byHeight = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(600, 1200), Vec2i(4096, 1000));
	CPPUNIT_ASSERT_EQUAL(s32(500), byHeight.x);
	CPPUNIT_ASSERT_EQUAL(s32(1000), byHeight.y);
}

void InterfaceSizeTest::testUnusableInputIsTheWindow() {
	const Vec2i window(1024, 768);
	Vec2i noWindow = vr::interfaceResolution(UiIndexEye, uiIndexFov(), UiQuadWidth, UiQuadDistance, Vec2i(0, 0), UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(s32(0), noWindow.x);
	Vec2i noEye = vr::interfaceResolution(Vec2i(0, 0), uiIndexFov(), UiQuadWidth, UiQuadDistance, window, UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(window.x, noEye.x);
	Vec2i noFov = vr::interfaceResolution(UiIndexEye, vr::EyeFov(), UiQuadWidth, UiQuadDistance, window, UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(window.x, noFov.x);
	CPPUNIT_ASSERT_EQUAL(window.y, noFov.y);
	Vec2i inverted = vr::interfaceResolution(UiIndexEye, uiDegrees(40.f, -54.f, 44.f, -55.f), UiQuadWidth, UiQuadDistance, window, UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(window.x, inverted.x);
	Vec2i noQuad = vr::interfaceResolution(UiIndexEye, uiIndexFov(), 0.f, UiQuadDistance, window, UiNoLimit);
	CPPUNIT_ASSERT_EQUAL(window.x, noQuad.x);
}

void InterfaceSizeTest::testPixelsPerDegree() {
	// 1114 px per tangent unit straight ahead: about 19.4 px per degree
	float ppd = vr::eyePixelsPerDegree(UiIndexEye.x, uiIndexFov());
	CPPUNIT_ASSERT(ppd > 19.2f && ppd < 19.6f);
	CPPUNIT_ASSERT_EQUAL(0.f, vr::eyePixelsPerDegree(UiIndexEye.x, vr::EyeFov()));
}

void InterfaceSizeTest::testEqualSizesMapToThemselves() {
	const Vec2i size(1024, 768);
	for(s32 x = 0; x < size.x; x += 37) {
		Vec2i p(x, (x * 3) % size.y);
		Vec2i there = vr::windowToInterface(p, size, size);
		CPPUNIT_ASSERT_EQUAL(p.x, there.x);
		CPPUNIT_ASSERT_EQUAL(p.y, there.y);
		Vec2i back = vr::interfaceToWindow(p, size, size);
		CPPUNIT_ASSERT_EQUAL(p.x, back.x);
		CPPUNIT_ASSERT_EQUAL(p.y, back.y);
	}
}

void InterfaceSizeTest::testMouseMapsAcross() {
	const Vec2i window(1024, 768);
	const Vec2i ui(1528, 1146);
	Vec2i corner = vr::windowToInterface(Vec2i(0, 0), window, ui);
	CPPUNIT_ASSERT_EQUAL(s32(0), corner.x);
	CPPUNIT_ASSERT_EQUAL(s32(0), corner.y);
	Vec2i last = vr::windowToInterface(window - Vec2i(1), window, ui);
	CPPUNIT_ASSERT_EQUAL(ui.x - 1, last.x);
	CPPUNIT_ASSERT_EQUAL(ui.y - 1, last.y);
	Vec2i center = vr::windowToInterface(window / s32(2), window, ui);
	checkUiNear("center x", ui.x / 2, center.x, 1);
	checkUiNear("center y", ui.y / 2, center.y, 1);
	// Outside the window stays inside the interface
	Vec2i outside = vr::windowToInterface(Vec2i(-50, 5000), window, ui);
	CPPUNIT_ASSERT_EQUAL(s32(0), outside.x);
	CPPUNIT_ASSERT_EQUAL(ui.y - 1, outside.y);
}

void InterfaceSizeTest::testWindowPositionsRoundTrip() {
	const Vec2i window(1024, 768);
	const Vec2i ui(1528, 1146);
	for(s32 x = 0; x < window.x; x++) {
		Vec2i p(x, x % window.y);
		Vec2i back = vr::interfaceToWindow(vr::windowToInterface(p, window, ui), window, ui);
		CPPUNIT_ASSERT_EQUAL(p.x, back.x);
		CPPUNIT_ASSERT_EQUAL(p.y, back.y);
	}
}
