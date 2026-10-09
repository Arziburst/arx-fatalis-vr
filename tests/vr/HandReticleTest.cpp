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

#include "vr/HandReticleTest.h"

#include <cmath>

#include <glm/glm.hpp>

#include "vr/HandReticle.h"
#include "vr/MenuPointer.h"

CppUnit::AutoRegisterSuite<HandReticleTest> g_registerHandReticleTest;

namespace {

typedef std::array<vr::PointerRay, vr::HandCount> ReticleRays;
typedef std::array<bool, vr::HandCount> HandFlags;

const HandFlags ReticleNoHands = { false, false };
const HandFlags ReticleLeftOnly = { true, false };
const HandFlags ReticleRightOnly = { false, true };

const s32 ReticleKey = 7;
const s32 ReticleNote = 12;

vr::PointerRay worldRay(float x, float y) {
	vr::PointerRay ray;
	ray.onWorld = true;
	ray.pixel = Vec2f(vr::worldCursorPixel(Vec2f(x, y)));
	return ray;
}

vr::PointerRay panelRay(float x, float y) {
	vr::PointerRay ray;
	ray.onPanel = true;
	ray.pixel = Vec2f(x, y);
	return ray;
}

ReticleRays reticleRays(const vr::PointerRay & left, const vr::PointerRay & right) {
	ReticleRays result = { left, right };
	return result;
}

Vec2f bandPixel(float x, float y) {
	return Vec2f(vr::worldCursorPixel(Vec2f(x, y)));
}

} // anonymous namespace

void HandReticleTest::testHoverNeedsTwoFrames() {
	vr::HoverFilter hover;
	// One frame over the key is not enough (a hand sweeping past it)
	CPPUNIT_ASSERT_EQUAL(vr::HoverFilter::None, hover.update(ReticleKey, 0.0));
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(ReticleKey, 0.011));
	// A different item takes over after its own two frames; the key stays meanwhile
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(ReticleNote, 0.022));
	CPPUNIT_ASSERT_EQUAL(ReticleNote, hover.update(ReticleNote, 0.033));
	// Interrupted candidates start counting again
	CPPUNIT_ASSERT_EQUAL(ReticleNote, hover.update(ReticleKey, 0.044));
	CPPUNIT_ASSERT_EQUAL(ReticleNote, hover.update(ReticleNote, 0.055));
	CPPUNIT_ASSERT_EQUAL(ReticleNote, hover.update(ReticleKey, 0.066));
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(ReticleKey, 0.077));
}

void HandReticleTest::testHoverHeldBriefly() {
	vr::HoverFilter hover;
	hover.update(ReticleKey, 0.0);
	hover.update(ReticleKey, 0.01);
	// The ray slips off the thin key: it is still hovered for 150 ms after it was last seen
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(vr::HoverFilter::None, 0.05));
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(vr::HoverFilter::None, 0.15));
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(ReticleKey, 0.155)); // back on it in time: seen again
	CPPUNIT_ASSERT_EQUAL(ReticleKey, hover.update(vr::HoverFilter::None, 0.30));
	CPPUNIT_ASSERT_EQUAL(vr::HoverFilter::None, hover.update(vr::HoverFilter::None, 0.31));
	hover.update(ReticleKey, 0.4);
	hover.update(ReticleKey, 0.41);
	hover.reset();
	CPPUNIT_ASSERT_EQUAL(vr::HoverFilter::None, hover.hovered());
}

void HandReticleTest::testWorldBand() {
	// Right of the interface, distances kept; the interface's own pixels are never in the band
	const Vec2s a = vr::worldCursorPixel(Vec2f(640.f, 900.f));
	const Vec2s b = vr::worldCursorPixel(Vec2f(656.f, 900.f));
	CPPUNIT_ASSERT_EQUAL(s32(16), s32(b.x) - s32(a.x));
	CPPUNIT_ASSERT_EQUAL(s16(900), a.y);
	CPPUNIT_ASSERT(vr::isWorldCursorPixel(a));
	CPPUNIT_ASSERT(vr::isWorldCursorPixel(vr::worldCursorPixel(Vec2f(-5000.f, -5000.f))));
	CPPUNIT_ASSERT(!vr::isWorldCursorPixel(Vec2s(0, 0)));
	CPPUNIT_ASSERT(!vr::isWorldCursorPixel(Vec2s(3840, 2160)));
	// A hit far off to the side is clamped, never wraps around
	const Vec2s clamped = vr::worldCursorPixel(Vec2f(1e9f, -1e9f));
	CPPUNIT_ASSERT_EQUAL(s32(vr::WorldCursorBandX + vr::WorldCursorClamp), s32(clamped.x));
	CPPUNIT_ASSERT_EQUAL(s32(-vr::WorldCursorClamp), s32(clamped.y));
}

void HandReticleTest::testPickHistory() {
	vr::PickHistory history;
	const Vec2s press = vr::worldCursorPixel(Vec2f(500.f, 700.f));
	CPPUNIT_ASSERT(!history.find(press));
	history.press(vr::Hand::Left, press, ReticleKey);
	CPPUNIT_ASSERT_EQUAL(ReticleKey, *history.find(press));
	// Many frames later, the ring full of other pixels: the press still answers for its pixel
	for(s16 i = 0; i < 100; i++) {
		history.record(vr::worldCursorPixel(Vec2f(600.f + i, 700.f)), ReticleNote);
	}
	CPPUNIT_ASSERT_EQUAL(ReticleKey, *history.find(press));
	CPPUNIT_ASSERT_EQUAL(ReticleNote, *history.find(vr::worldCursorPixel(Vec2f(699.f, 700.f))));
	CPPUNIT_ASSERT(!history.find(vr::worldCursorPixel(Vec2f(600.f, 700.f)))); // pushed out of the ring
	// The newest record of a pixel wins: the cursor back on the press pixel over nothing
	history.record(press, vr::HoverFilter::None);
	CPPUNIT_ASSERT_EQUAL(vr::HoverFilter::None, *history.find(press));
	history.clear();
	CPPUNIT_ASSERT(!history.find(press));
}

void HandReticleTest::testReticleQuad() {
	const Vec3f eye(0.f, 1.6f, 0.f);
	vr::ReticleQuad closeQuad;
	vr::ReticleQuad distantQuad;
	CPPUNIT_ASSERT(vr::reticleQuad(Vec3f(0.3f, 0.8f, -0.9f), eye, 1.5f, closeQuad));
	CPPUNIT_ASSERT(vr::reticleQuad(Vec3f(0.6f, 0.f, -1.8f), eye, 1.5f, distantQuad));
	// The same angle at any distance: the size grows with it
	const float nearDistance = glm::distance(Vec3f(0.3f, 0.8f, -0.9f), eye);
	const float farDistance = glm::distance(Vec3f(0.6f, 0.f, -1.8f), eye);
	CPPUNIT_ASSERT(std::abs(distantQuad.size / closeQuad.size - farDistance / nearDistance) < 1e-4f);
	CPPUNIT_ASSERT(std::abs(closeQuad.size - 2.f * nearDistance * std::tan(glm::radians(0.75f))) < 1e-6f);
	// Facing the eye, upright
	const Vec3f z = closeQuad.orientation * Vec3f(0.f, 0.f, 1.f);
	CPPUNIT_ASSERT(glm::dot(z, glm::normalize(eye - closeQuad.center)) > 0.9999f);
	CPPUNIT_ASSERT(std::abs((closeQuad.orientation * Vec3f(1.f, 0.f, 0.f)).y) < 1e-5f);
	// Straight below the eye still has a face; at the eye there is nothing to draw
	CPPUNIT_ASSERT(vr::reticleQuad(Vec3f(0.f, 0.f, 0.f), eye, 1.5f, closeQuad));
	CPPUNIT_ASSERT(glm::dot(closeQuad.orientation * Vec3f(0.f, 0.f, 1.f), Vec3f(0.f, 1.f, 0.f)) > 0.9999f);
	CPPUNIT_ASSERT(!vr::reticleQuad(eye, eye, 1.5f, closeQuad));
}

void HandReticleTest::testWorldPressClicks() {
	vr::MenuPointer pointer;
	// No panel, the reticles on: the right hand's reticle is the mouse
	pointer.update(true, reticleRays(vr::PointerRay(), worldRay(400.f, 600.f)), ReticleNoHands);
	CPPUNIT_ASSERT(pointer.active());
	CPPUNIT_ASSERT(!pointer.onPanel());
	CPPUNIT_ASSERT(pointer.cursor() == bandPixel(400.f, 600.f));
	// A press on the world clicks (no beam, no panel click)
	pointer.update(true, reticleRays(vr::PointerRay(), worldRay(400.f, 600.f)), ReticleRightOnly);
	CPPUNIT_ASSERT(pointer.pressedOnWorld(vr::Hand::Right));
	CPPUNIT_ASSERT(!pointer.pressedOnPanel(vr::Hand::Right));
	CPPUNIT_ASSERT(!pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.worldClicks());
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());
	pointer.update(true, reticleRays(vr::PointerRay(), worldRay(400.f, 600.f)), ReticleNoHands);
	// The left hand points at nothing (the sky, behind the head) and presses: muted, the mouse goes to it
	pointer.update(true, reticleRays(vr::PointerRay(), worldRay(400.f, 600.f)), ReticleLeftOnly);
	CPPUNIT_ASSERT(pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.offPanelPresses());
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(!pointer.active());
}

void HandReticleTest::testDragStaysWithItsHand() {
	vr::MenuPointer pointer;
	// The left hand presses on the key on the floor; the right hand's reticle wanders over the note
	pointer.update(true, reticleRays(worldRay(300.f, 900.f), worldRay(800.f, 700.f)), ReticleLeftOnly);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(pointer.holding());
	const HandFlags rightClaims = { false, true };
	pointer.update(true, reticleRays(worldRay(306.f, 905.f), worldRay(820.f, 700.f)), ReticleLeftOnly, vr::PointerParams(), rightClaims);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left); // a claim never takes a held press
	CPPUNIT_ASSERT(pointer.cursor() == bandPixel(300.f, 900.f)); // inside the dead zone
	// The hand moves on: the cursor follows - a drag
	pointer.update(true, reticleRays(worldRay(360.f, 880.f), worldRay(820.f, 700.f)), ReticleLeftOnly);
	CPPUNIT_ASSERT(!pointer.holding());
	CPPUNIT_ASSERT(pointer.cursor() == bandPixel(360.f, 880.f));
	// Onto the bag on the panel, trigger still held: the cursor is the panel's pixel, no new click
	pointer.update(true, reticleRays(panelRay(200.f, 650.f), worldRay(820.f, 700.f)), ReticleLeftOnly);
	CPPUNIT_ASSERT(pointer.onPanel());
	CPPUNIT_ASSERT(pointer.cursor() == Vec2f(200.f, 650.f));
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.clicks());
	CPPUNIT_ASSERT_EQUAL(u64(1), pointer.worldClicks());
	// Released over the bag: still the left hand's mouse
	pointer.update(true, reticleRays(panelRay(200.f, 650.f), worldRay(820.f, 700.f)), ReticleNoHands);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
}

void HandReticleTest::testClaimTakesIdleCursor() {
	vr::MenuPointer pointer;
	pointer.update(true, reticleRays(worldRay(300.f, 900.f), worldRay(800.f, 700.f)), ReticleNoHands);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Right);
	// Both on the world; the left hand's reticle moves onto the note: the left names it now (D-019)
	pointer.update(true, reticleRays(worldRay(310.f, 900.f), worldRay(800.f, 700.f)), ReticleNoHands, vr::PointerParams(), ReticleLeftOnly);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(pointer.ownerChanged());
	CPPUNIT_ASSERT(pointer.cursor() == bandPixel(310.f, 900.f));
	// A claim of a hand with no target does nothing
	const vr::PointerRay none;
	pointer.update(true, reticleRays(worldRay(310.f, 900.f), none), ReticleNoHands, vr::PointerParams(), ReticleRightOnly);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	// Reticles off (a menu that is not the inventory, combat mode): the game's own clicks again
	pointer.update(false, reticleRays(worldRay(310.f, 900.f), worldRay(800.f, 700.f)), ReticleLeftOnly);
	CPPUNIT_ASSERT(!pointer.active());
	CPPUNIT_ASSERT(!pointer.suppressClick());
	CPPUNIT_ASSERT_EQUAL(u64(0), pointer.worldClicks());
}

void HandReticleTest::testPanelWinsOverWorld() {
	vr::MenuPointer pointer;
	// The inventory is open: the right hand on the panel, the left in the world
	pointer.update(true, reticleRays(worldRay(300.f, 900.f), panelRay(150.f, 600.f)), ReticleNoHands);
	CPPUNIT_ASSERT(pointer.onPanel());
	// The right hand points away from both: the left hand's reticle takes the idle cursor
	pointer.update(true, reticleRays(worldRay(300.f, 900.f), vr::PointerRay()), ReticleNoHands);
	CPPUNIT_ASSERT(pointer.owner() == vr::Hand::Left);
	CPPUNIT_ASSERT(pointer.active());
	CPPUNIT_ASSERT(!pointer.onPanel());
	CPPUNIT_ASSERT(pointer.cursor() == bandPixel(300.f, 900.f));
}
