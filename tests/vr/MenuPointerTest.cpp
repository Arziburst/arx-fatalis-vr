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

#include "vr/MenuPointerTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "vr/MenuPointer.h"

CppUnit::AutoRegisterSuite<MenuPointerTest> g_registerMenuPointerTest;

namespace {

typedef std::array<vr::PointerRay, vr::HandCount> Rays;
typedef std::array<bool, vr::HandCount> Triggers;

const Rays NoRays = { };
const Triggers Up = { false, false };
const Triggers LeftDown = { true, false };
const Triggers RightDown = { false, true };

vr::PointerRay on(float x, float y) {
	vr::PointerRay ray;
	ray.onPanel = true;
	ray.pixel = Vec2f(x, y);
	return ray;
}

Rays rays(const vr::PointerRay & left, const vr::PointerRay & right) {
	Rays result = { left, right };
	return result;
}

void checkCursor(const std::string & what, const vr::MenuPointer & pointer, float x, float y) {
	CPPUNIT_ASSERT_MESSAGE(what + ": active", pointer.active());
	if(pointer.cursor() != Vec2f(x, y)) {
		CPPUNIT_FAIL(what + ": expected the cursor at " + std::to_string(x) + ", " + std::to_string(y) + ", got "
		             + std::to_string(pointer.cursor().x) + ", " + std::to_string(pointer.cursor().y));
	}
}

} // anonymous namespace

void MenuPointerTest::testNothingWithoutThePanel() {
	vr::MenuPointer pointer;
	pointer.update(false, rays(on(10.f, 10.f), on(20.f, 20.f)), Up);
	CPPUNIT_ASSERT(!pointer.active());
	// The game's own clicks in the world: not counted, not muted, the owner stays
	pointer.update(false, NoRays, LeftDown);
	CPPUNIT_ASSERT(!pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.offPanelPresses());
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Right);
	// A trigger held while the panel appears is no press on it
	pointer.update(true, rays(on(10.f, 10.f), vr::PointerRay()), LeftDown);
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());
}

void MenuPointerTest::testRayMovesTheCursor() {
	vr::MenuPointer pointer;
	pointer.update(true, rays(vr::PointerRay(), on(100.f, 200.f)), Up);
	checkCursor("the right hand by default", pointer, 100.f, 200.f);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Right);
	pointer.update(true, rays(on(5.f, 5.f), on(140.f, 260.f)), Up);
	checkCursor("follows the owner, not the other hand", pointer, 140.f, 260.f);
	pointer.update(true, NoRays, Up);
	CPPUNIT_ASSERT(!pointer.active()); // pointing away: no beam
}

void MenuPointerTest::testLastPressOwnsTheCursor() {
	vr::MenuPointer pointer;
	pointer.update(true, rays(on(50.f, 60.f), on(300.f, 400.f)), Up);
	checkCursor("right owns it", pointer, 300.f, 400.f);

	// The left hand presses: it takes the cursor, at its own pixel, in the same update - the click lands there
	pointer.update(true, rays(on(50.f, 60.f), on(300.f, 400.f)), LeftDown);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(pointer.ownerChanged());
	CPPUNIT_ASSERT(pointer.pressedOnPanel(vr::Hand::Left));
	checkCursor("under the left hand", pointer, 50.f, 60.f);
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.clicks());

	// Released, both still on the panel: the left hand keeps it
	pointer.update(true, rays(on(70.f, 60.f), on(300.f, 400.f)), Up);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	checkCursor("still left", pointer, 70.f, 60.f);

	// The right hand presses: back to the right
	pointer.update(true, rays(on(70.f, 60.f), on(310.f, 400.f)), RightDown);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Right);
	checkCursor("under the right hand", pointer, 310.f, 400.f);
	CPPUNIT_ASSERT_EQUAL(u64(2), pointer.clicks());
	CPPUNIT_ASSERT_EQUAL(u64(1 + 1), pointer.ownerChanges());
}

void MenuPointerTest::testIdleOwnerHandsOver() {
	vr::MenuPointer pointer;
	pointer.update(true, rays(vr::PointerRay(), on(300.f, 400.f)), Up);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Right);

	// The right hand drops, the left points at the panel: the left takes it without a click
	pointer.update(true, rays(on(80.f, 90.f), vr::PointerRay()), Up);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	checkCursor("under the left hand", pointer, 80.f, 90.f);
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());

	// A drag holds on to its hand: the owner's trigger down off the panel keeps it
	pointer.update(true, rays(on(80.f, 90.f), on(300.f, 400.f)), LeftDown);
	pointer.update(true, rays(vr::PointerRay(), on(300.f, 400.f)), LeftDown);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(!pointer.active());
}

void MenuPointerTest::testClickDeadZone() {
	vr::MenuPointer pointer;
	pointer.update(true, rays(vr::PointerRay(), on(500.f, 500.f)), Up);
	pointer.update(true, rays(vr::PointerRay(), on(500.f, 500.f)), RightDown);
	// The trigger pull shakes the ray by up to 16 px: the cursor stays on the pixel of the press
	pointer.update(true, rays(vr::PointerRay(), on(510.f, 509.f)), RightDown);
	checkCursor("held inside the dead zone", pointer, 500.f, 500.f);
	pointer.update(true, rays(vr::PointerRay(), on(490.f, 512.f)), RightDown);
	checkCursor("still held", pointer, 500.f, 500.f);
	// Further: a drag, the cursor follows from now on, also back inside the circle
	pointer.update(true, rays(vr::PointerRay(), on(520.f, 500.f)), RightDown);
	checkCursor("dragging", pointer, 520.f, 500.f);
	pointer.update(true, rays(vr::PointerRay(), on(505.f, 500.f)), RightDown);
	checkCursor("dragging back", pointer, 505.f, 500.f);
	// Released: the ray again
	pointer.update(true, rays(vr::PointerRay(), on(400.f, 300.f)), Up);
	checkCursor("free", pointer, 400.f, 300.f);
}

void MenuPointerTest::testBeamFacesTheEyes() {
	const Vec3f hand(0.2f, -0.45f, -0.35f);
	const Vec3f hit(0.05f, -0.3f, -0.85f);
	const Vec3f eye(0.f);
	vr::BeamQuad quad;
	CPPUNIT_ASSERT(vr::beamQuad(hand, hit, eye, 0.003f, quad));
	const Vec3f y = quad.orientation * Vec3f(0.f, 1.f, 0.f);
	const Vec3f z = quad.orientation * Vec3f(0.f, 0.f, 1.f);
	// From the hand to the hit, as long as the beam, as wide as asked
	CPPUNIT_ASSERT(glm::distance(quad.center - y * (quad.size.y * 0.5f), hand) < 1e-4f);
	CPPUNIT_ASSERT(glm::distance(quad.center + y * (quad.size.y * 0.5f), hit) < 1e-4f);
	CPPUNIT_ASSERT(std::abs(quad.size.x - 0.003f) < 1e-6f);
	// The face turned to the eye as far as the line allows: the eye lies in the plane of y and z, in front
	const Vec3f toEye = glm::normalize(eye - quad.center);
	CPPUNIT_ASSERT(std::abs(glm::dot(quad.orientation * Vec3f(1.f, 0.f, 0.f), toEye)) < 1e-4f);
	CPPUNIT_ASSERT(glm::dot(z, toEye) > 0.f);
	// Nothing to draw: no length, or seen exactly along its line
	CPPUNIT_ASSERT(!vr::beamQuad(hand, hand, eye, 0.003f, quad));
	CPPUNIT_ASSERT(!vr::beamQuad(Vec3f(0.f, 0.f, -0.2f), Vec3f(0.f, 0.f, -1.f), eye, 0.003f, quad));
}

void MenuPointerTest::testPressOffThePanelIsMuted() {
	vr::MenuPointer pointer;
	pointer.update(true, rays(vr::PointerRay(), on(300.f, 400.f)), Up);

	// The left hand presses pointing away: it owns the cursor now, but the engine must not click
	pointer.update(true, rays(vr::PointerRay(), on(300.f, 400.f)), LeftDown);
	CPPUNIT_ASSERT(pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.offPanelPresses());
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(!pointer.active());
	// Moving onto the panel with the trigger still down stays muted - it is no new press
	pointer.update(true, rays(on(60.f, 60.f), on(300.f, 400.f)), LeftDown);
	CPPUNIT_ASSERT(pointer.suppressClick());

	// A real press of the right hand during it is a click: the button goes down
	const Triggers both = { true, true };
	pointer.update(true, rays(on(60.f, 60.f), on(300.f, 400.f)), both);
	CPPUNIT_ASSERT(!pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.clicks());

	// All up: nothing muted; the next left press on the panel clicks
	pointer.update(true, rays(on(60.f, 60.f), on(300.f, 400.f)), Up);
	CPPUNIT_ASSERT(!pointer.suppressClick());
	pointer.update(true, rays(on(60.f, 60.f), on(300.f, 400.f)), LeftDown);
	CPPUNIT_ASSERT(!pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(2), pointer.clicks());
}
