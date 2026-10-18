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

#include "vr/HudFrameTest.h"

#include <cstring>
#include <set>
#include <string>

#include "vr/HudFrame.h"

CppUnit::AutoRegisterSuite<HudFrameTest> g_registerHudFrameTest;

using vr::HudElement;
using vr::HudHome;

namespace {

HudElement element(size_t i) {
	return HudElement(i);
}

vr::HudGate vrGate() {
	vr::HudGate gate;
	gate.vr = true;
	gate.wristVitals = true;
	return gate;
}

} // anonymous namespace

void HudFrameTest::testEveryElementHasAHome() {

	std::set<std::string> names;
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		const vr::HudElementInfo & info = vr::hudElementInfo(element(i));
		CPPUNIT_ASSERT(info.name != nullptr && std::strlen(info.name) > 0);
		CPPUNIT_ASSERT_MESSAGE(info.name, info.where != nullptr && std::strlen(info.where) > 10);
		CPPUNIT_ASSERT_MESSAGE(info.name, names.insert(info.name).second);
	}

	// The owner's list (#464, 2026-10-14): health and mana on the wrists; level-up, book and inventory through the
	// radial menu
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Health).home == HudHome::Wrist);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Mana).home == HudHome::Wrist);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::LevelUp).home == HudHome::Radial);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Book).home == HudHome::Radial);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Backpack).home == HudHome::Radial);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Torch).home == HudHome::Radial);
	// Without an obvious home: listed for the owner
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::PrecastSpells).home == HudHome::Question);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::ChangeLevel).home == HudHome::Question);
	// The owner's scope note (#469, 2026-10-15): the minimap, the info messages and the speech go too
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::MiniMap).home == HudHome::Radial);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Notifications).home == HudHome::Question);
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::Speech).home == HudHome::Question);
	// The mouse's and the monitor's own
	CPPUNIT_ASSERT(vr::hudElementInfo(HudElement::ScreenArrows).home == HudHome::None);

	CPPUNIT_ASSERT_EQUAL(std::string("question"), std::string(vr::hudHomeName(HudHome::Question)));
	CPPUNIT_ASSERT_EQUAL(std::string("wrist"), std::string(vr::hudHomeName(HudHome::Wrist)));
}

void HudFrameTest::testNamesRoundTrip() {
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		CPPUNIT_ASSERT(vr::hudElementFromName(vr::hudElementInfo(element(i)).name) == element(i));
	}
	CPPUNIT_ASSERT(vr::hudElementFromName("level_up") == HudElement::LevelUp);
	CPPUNIT_ASSERT(vr::hudElementFromName("nothing") == HudElement::Count);
	CPPUNIT_ASSERT(vr::hudElementFromName("") == HudElement::Count);
}

void HudFrameTest::testFlatDrawsTheFrame() {
	vr::HudGate gate;
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		CPPUNIT_ASSERT_MESSAGE(vr::hudElementInfo(element(i)).name, vr::hudElementDrawn(element(i), gate));
	}
	// The orbs step aside for the wrist bars, as since #116
	gate.wristVitals = true;
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Health, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Mana, gate));
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::Book, gate));
}

void HudFrameTest::testVrDrawsNoFrame() {

	vr::HudGate gate = vrGate();
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		CPPUNIT_ASSERT_MESSAGE(vr::hudElementInfo(element(i)).name, !vr::hudElementDrawn(element(i), gate));
	}

	// No left arm posed (no left controller): no fallback, the orbs are not drawn on the 2D interface either (#484)
	gate.wristVitals = false;
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Health, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Mana, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::LevelUp, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Backpack, gate));
}

void HudFrameTest::testNoOrbFallbackWithoutLeftArm() {

	// The owner (#484, 2026-10-15): before the controllers come on, no HP/mana half-spheres on the head-locked quad.
	// VR, the default switches, no left arm posed: every element of the frame stays off, the orbs included
	vr::HudGate gate;
	gate.vr = true;
	gate.wristVitals = false;
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		CPPUNIT_ASSERT_MESSAGE(vr::hudElementInfo(element(i)).name, !vr::hudElementDrawn(element(i), gate));
	}

	// The left arm posed and lost again, frame by frame: never the orbs on the 2D interface
	for(bool posed : { true, false, true, false }) {
		gate.wristVitals = posed;
		CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Health, gate));
		CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Mana, gate));
	}

	// Flat mode is not touched: the orbs are drawn there without wrist bars, as before
	vr::HudGate flat;
	flat.wristVitals = false;
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::Health, flat));
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::Mana, flat));
}

void HudFrameTest::testSwitchesBringItBack() {

	vr::HudGate gate = vrGate();
	gate.frame = true;
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::Book, gate));
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::Stealth, gate));
	// The wrist bars win over the orbs even with the old frame back
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Health, gate));

	gate.frame = false;
	gate.keep = vr::hudElementBit(HudElement::PrecastSpells) | vr::hudElementBit(HudElement::ChangeLevel);
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::PrecastSpells, gate));
	CPPUNIT_ASSERT(vr::hudElementDrawn(HudElement::ChangeLevel, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::Book, gate));
	CPPUNIT_ASSERT(!vr::hudElementDrawn(HudElement::ActiveSpells, gate));
}

void HudFrameTest::testParseKeep() {

	std::string unknown;
	CPPUNIT_ASSERT_EQUAL(vr::HudElementMask(0), vr::parseHudKeep("", &unknown));
	CPPUNIT_ASSERT(unknown.empty());

	const vr::HudElementMask two = vr::hudElementBit(HudElement::PrecastSpells) | vr::hudElementBit(HudElement::Stealth);
	CPPUNIT_ASSERT_EQUAL(two, vr::parseHudKeep("precast_spells,stealth"));
	CPPUNIT_ASSERT_EQUAL(two, vr::parseHudKeep(" precast_spells ,  stealth; "));
	CPPUNIT_ASSERT_EQUAL(two, vr::parseHudKeep("stealth precast_spells"));

	CPPUNIT_ASSERT_EQUAL(vr::hudElementBit(HudElement::Stealth), vr::parseHudKeep("bogus, stealth, other", &unknown));
	CPPUNIT_ASSERT_EQUAL(std::string("bogus"), unknown);

	const vr::HudElementMask all = vr::parseHudKeep("all");
	for(size_t i = 0; i < vr::HudElementCount; i++) {
		CPPUNIT_ASSERT(all & vr::hudElementBit(element(i)));
	}
	CPPUNIT_ASSERT(!(all & vr::hudElementBit(HudElement::Count)));
}

void HudFrameTest::testLevelUpModes() {

	for(vr::LevelUpMode mode : { vr::LevelUpMode::Both, vr::LevelUpMode::Radial, vr::LevelUpMode::Wrist,
	                             vr::LevelUpMode::Off }) {
		CPPUNIT_ASSERT(vr::levelUpModeFromName(vr::levelUpModeName(mode)) == mode);
	}
	CPPUNIT_ASSERT(vr::levelUpModeFromName("bogus") == vr::LevelUpMode::Both);
	CPPUNIT_ASSERT(vr::levelUpModeFromName("bogus", vr::LevelUpMode::Off) == vr::LevelUpMode::Off);

	// Each mode shows its own part of the indication
	const struct { vr::LevelUpMode mode; bool radial; bool wrist; } cases[] = {
		{ vr::LevelUpMode::Both, true, true },
		{ vr::LevelUpMode::Radial, true, false },
		{ vr::LevelUpMode::Wrist, false, true },
		{ vr::LevelUpMode::Off, false, false },
	};
	for(const auto & c : cases) {
		vr::LevelUpIndicator indicator;
		indicator.update(false, 1, 0, c.mode);
		CPPUNIT_ASSERT(indicator.update(true, 2, 1000, c.mode));
		CPPUNIT_ASSERT_EQUAL(c.radial, indicator.radialMark());
		CPPUNIT_ASSERT_EQUAL(c.wrist, indicator.wristNotice());
		CPPUNIT_ASSERT(indicator.pending());
	}
}

void HudFrameTest::testLevelUpNotice() {

	const s64 second = 1000000;
	vr::LevelUpIndicator indicator;

	// A loaded save with points to spend: the mark, no notice
	CPPUNIT_ASSERT(!indicator.update(true, 3, 0, vr::LevelUpMode::Both));
	CPPUNIT_ASSERT(indicator.radialMark());
	CPPUNIT_ASSERT(!indicator.wristNotice());
	CPPUNIT_ASSERT_EQUAL(u64(0), indicator.notices());

	// The points spent: no mark
	indicator.update(false, 3, second, vr::LevelUpMode::Both);
	CPPUNIT_ASSERT(!indicator.radialMark());

	// A level-up: the mark and the notice, which runs out after 8 s while the mark stays
	CPPUNIT_ASSERT(indicator.update(true, 4, 2 * second, vr::LevelUpMode::Both));
	CPPUNIT_ASSERT(indicator.wristNotice());
	CPPUNIT_ASSERT_EQUAL(u64(1), indicator.notices());
	CPPUNIT_ASSERT(!indicator.update(true, 4, 6 * second, vr::LevelUpMode::Both));
	CPPUNIT_ASSERT(indicator.wristNotice());
	CPPUNIT_ASSERT(indicator.noticeProgress() > 0.49f && indicator.noticeProgress() < 0.51f);
	indicator.update(true, 4, 10 * second, vr::LevelUpMode::Both);
	CPPUNIT_ASSERT(!indicator.wristNotice());
	CPPUNIT_ASSERT(indicator.radialMark());

	// Another level while the points still wait: a new notice
	CPPUNIT_ASSERT(indicator.update(true, 5, 11 * second, vr::LevelUpMode::Both));
	CPPUNIT_ASSERT(indicator.wristNotice());
	CPPUNIT_ASSERT_EQUAL(u64(2), indicator.notices());

	// Spending the points ends the notice at once
	indicator.update(false, 5, 12 * second, vr::LevelUpMode::Both);
	CPPUNIT_ASSERT(!indicator.wristNotice());
	CPPUNIT_ASSERT(!indicator.radialMark());
	CPPUNIT_ASSERT_EQUAL(u64(2), indicator.notices());
}
