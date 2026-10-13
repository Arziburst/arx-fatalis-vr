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

#include "vr/RadialMenuTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "vr/RadialMenu.h"

CppUnit::AutoRegisterSuite<RadialMenuTest> g_registerRadialMenuTest;

namespace {

void checkRadialNear(const std::string & what, float expected, float actual, float tolerance = 0.001f) {
	if(std::abs(expected - actual) > tolerance) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void checkChoice(const std::string & what, const std::string & expected, const vr::RadialChoice & actual) {
	const std::string name = vr::radialChoiceName(actual);
	if(name != expected) {
		CPPUNIT_FAIL(what + ": expected " + expected + ", got " + name);
	}
}

//! The hand this far from the ring's middle, in the direction \a degrees (counterclockwise from right)
Vec2f at(float degrees, float radius) {
	return Vec2f(std::cos(glm::radians(degrees)), std::sin(glm::radians(degrees))) * radius;
}

} // anonymous namespace

void RadialMenuTest::testPlacedAtTheHandFacingTheEyes() {

	const Vec3f eye(0.f, 1.6f, 0.f);
	const Vec3f hand(0.2f, 1.2f, -0.4f);
	const vr::RadialPose pose = vr::placeRadial(hand, eye);

	// Pushed 4 cm beyond the hand, away from the eyes
	const Vec3f away = glm::normalize(hand - eye);
	checkRadialNear("pushed", 0.04f, glm::dot(pose.center - hand, away));
	checkRadialNear("on the line from the eyes", 0.f, glm::length(glm::cross(pose.center - eye, away)));

	const Vec3f face = pose.orientation * Vec3f(0.f, 0.f, 1.f);
	const Vec3f x = pose.orientation * Vec3f(1.f, 0.f, 0.f);
	const Vec3f y = pose.orientation * Vec3f(0.f, 1.f, 0.f);
	checkRadialNear("faces the eyes", 1.f, glm::dot(face, -away));
	checkRadialNear("no roll: x horizontal", 0.f, x.y);
	CPPUNIT_ASSERT(y.y > 0.f);
	CPPUNIT_ASSERT(x.x > 0.8f); // the ring's right is the player's right

	// The hand at the eye has no direction: the ring faces the room's -z rather than failing
	const vr::RadialPose degenerate = vr::placeRadial(eye, eye);
	checkRadialNear("degenerate face", 1.f, (degenerate.orientation * Vec3f(0.f, 0.f, 1.f)).z);
}

void RadialMenuTest::testPlanePoint() {

	const vr::RadialPose pose = vr::placeRadial(Vec3f(0.f, 1.2f, -0.5f), Vec3f(0.f, 1.2f, 0.f));
	// Straight ahead: the plane's x is the room's x, its y the room's y
	const Vec2f p = vr::radialPlanePoint(pose, pose.center + Vec3f(0.05f, 0.1f, 0.3f));
	checkRadialNear("x", 0.05f, p.x);
	checkRadialNear("y", 0.1f, p.y);
	// The hand that opened the ring is in its middle
	const Vec2f hand = vr::radialPlanePoint(pose, Vec3f(0.f, 1.2f, -0.5f));
	checkRadialNear("hand at the middle", 0.f, glm::length(hand));
}

void RadialMenuTest::testMiddleSelectsNothing() {
	vr::RadialSelector selector;
	selector.reset();
	checkChoice("at the middle", "none", selector.update(Vec2f(0.f)));
	checkChoice("3 cm right", "none", selector.update(Vec2f(0.03f, 0.f)));
	checkChoice("5 cm right", "slot2", selector.update(Vec2f(0.05f, 0.f)));
	checkChoice("back in the hole", "none", selector.update(Vec2f(0.01f, 0.01f)));
}

void RadialMenuTest::testSegments() {

	const char * const expected[vr::RadialSegmentCount] = {
		"inventory", "slot1", "slot2", "slot3", "slot4", "slot5", "slot6", "slot7",
	};
	for(size_t segment = 0; segment < vr::RadialSegmentCount; segment++) {
		vr::RadialSelector selector;
		selector.reset();
		const float degrees = vr::segmentCenterDegrees(segment);
		checkChoice("segment " + std::to_string(segment), expected[segment], selector.update(at(degrees, 0.08f)));
		// Beyond the ring's edge it is still the same segment, except above it (the half-ring)
		if(segment != 0 && segment != 1 && segment != 7) {
			checkChoice("outside, segment " + std::to_string(segment), expected[segment],
			            selector.update(at(degrees, 0.2f)));
		}
	}

	checkRadialNear("top", 90.f, vr::segmentCenterDegrees(0));
	checkRadialNear("right", 0.f, vr::segmentCenterDegrees(2));
	checkRadialNear("bottom", 270.f, vr::segmentCenterDegrees(4));
	checkRadialNear("left", 180.f, vr::segmentCenterDegrees(6));
}

void RadialMenuTest::testSegmentHysteresis() {
	vr::RadialSelector selector;
	selector.reset();
	// The right segment spans -22.5 .. 22.5 degrees; it is kept 6 degrees past each edge
	checkChoice("right", "slot2", selector.update(at(0.f, 0.08f)));
	checkChoice("just past its upper edge", "slot2", selector.update(at(26.f, 0.08f)));
	checkChoice("past the margin", "slot1", selector.update(at(30.f, 0.08f)));
	checkChoice("back past the edge", "slot1", selector.update(at(19.f, 0.08f)));
	checkChoice("far back", "slot2", selector.update(at(10.f, 0.08f)));
	// Fresh, the edge decides
	vr::RadialSelector fresh;
	fresh.reset();
	checkChoice("fresh at 26 degrees", "slot1", fresh.update(at(26.f, 0.08f)));
}

void RadialMenuTest::testHalfRingOpensAboveTheRing() {
	vr::RadialSelector selector;
	selector.reset();
	checkChoice("the top segment", "inventory", selector.update(Vec2f(0.f, 0.1f)));
	CPPUNIT_ASSERT(!selector.halfRingOpen());
	checkChoice("at the ring's edge", "inventory", selector.update(Vec2f(0.f, 0.12f)));
	CPPUNIT_ASSERT(!selector.halfRingOpen());
	// Past the edge: the half-ring, its middle item straight up
	checkChoice("above the ring", "quests", selector.update(Vec2f(0.f, 0.15f)));
	CPPUNIT_ASSERT(selector.halfRingOpen());
	// Right of the ring but not above it: still a segment
	vr::RadialSelector side;
	side.reset();
	checkChoice("far right", "slot2", side.update(Vec2f(0.25f, 0.f)));
	CPPUNIT_ASSERT(!side.halfRingOpen());
}

void RadialMenuTest::testHalfRingItemsLeftToRight() {

	const char * const expected[vr::HalfRingItemCount] = {
		"game_menu", "character", "spells", "map", "quests", "quick_save", "quick_load", "recenter", "cancel_spell",
	};
	for(size_t item = 0; item < vr::HalfRingItemCount; item++) {
		vr::RadialSelector selector;
		selector.reset();
		selector.update(Vec2f(0.f, 0.17f)); // open it
		CPPUNIT_ASSERT(selector.halfRingOpen());
		checkChoice("item " + std::to_string(item), expected[item],
		            selector.update(at(vr::halfRingCenterDegrees(item), 0.17f)));
	}

	// The end items reach a little below the horizontal
	vr::RadialSelector selector;
	selector.reset();
	selector.update(Vec2f(0.f, 0.17f));
	checkChoice("left, a little low", "game_menu", selector.update(Vec2f(-0.17f, -0.01f)));
	checkChoice("right, a little low", "cancel_spell", selector.update(Vec2f(0.17f, -0.01f)));
}

void RadialMenuTest::testHalfRingCloses() {

	vr::RadialSelector selector;
	selector.reset();
	selector.update(Vec2f(0.f, 0.17f));
	CPPUNIT_ASSERT(selector.halfRingOpen());
	// Still open over the ring's upper half outside it
	checkChoice("over the top right", "quick_save", selector.update(at(70.f, 0.11f)));
	CPPUNIT_ASSERT(selector.halfRingOpen());
	// Back inside the ring: the segments again
	checkChoice("back in the ring", "inventory", selector.update(Vec2f(0.f, 0.08f)));
	CPPUNIT_ASSERT(!selector.halfRingOpen());

	selector.update(Vec2f(0.f, 0.17f));
	CPPUNIT_ASSERT(selector.halfRingOpen());
	checkChoice("dropped below the middle", "slot2", selector.update(Vec2f(0.17f, -0.03f)));
	CPPUNIT_ASSERT(!selector.halfRingOpen());

	// A new menu starts closed
	selector.update(Vec2f(0.f, 0.17f));
	selector.reset();
	CPPUNIT_ASSERT(!selector.halfRingOpen());
	checkChoice("reset", "none", selector.hovered());
}

void RadialMenuTest::testNames() {
	CPPUNIT_ASSERT_EQUAL(std::string("Inventory"), std::string(vr::radialItemLabel(vr::RadialItem::Inventory)));
	CPPUNIT_ASSERT_EQUAL(std::string("Cancel spell"), std::string(vr::radialItemLabel(vr::RadialItem::CancelSpell)));
	vr::RadialChoice slot;
	slot.item = vr::RadialItem::QuickSlot;
	slot.slot = 6;
	CPPUNIT_ASSERT_EQUAL(std::string("slot7"), vr::radialChoiceName(slot));
	CPPUNIT_ASSERT(vr::segmentChoice(8) == vr::RadialChoice());
	CPPUNIT_ASSERT(vr::halfRingItem(9) == vr::RadialItem::None);
}

void RadialMenuTest::testQuickSlotText() {

	vr::QuickSlotSet slots;
	slots[0] = "potion_life";
	slots[3] = "torch";
	const std::string text = vr::formatQuickSlots(slots);
	CPPUNIT_ASSERT_EQUAL(std::string("potion_life|||torch|||"), text);
	CPPUNIT_ASSERT(vr::parseQuickSlots(text) == slots);

	// Empty, short, broken, too long: never an item that was not a class name
	CPPUNIT_ASSERT(vr::parseQuickSlots("") == vr::QuickSlotSet());
	const vr::QuickSlotSet shortSet = vr::parseQuickSlots("food_bread");
	CPPUNIT_ASSERT_EQUAL(std::string("food_bread"), shortSet[0]);
	CPPUNIT_ASSERT(shortSet[1].empty());
	const vr::QuickSlotSet broken = vr::parseQuickSlots("Potion|../x|a b|torch");
	CPPUNIT_ASSERT(broken[0].empty() && broken[1].empty() && broken[2].empty());
	CPPUNIT_ASSERT_EQUAL(std::string("torch"), broken[3]);
	const vr::QuickSlotSet longSet = vr::parseQuickSlots("a|b|c|d|e|f|g|h|i");
	CPPUNIT_ASSERT_EQUAL(std::string("g"), longSet[6]);
}
