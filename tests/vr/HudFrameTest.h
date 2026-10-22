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

#ifndef ARX_TESTS_VR_HUDFRAMETEST_H
#define ARX_TESTS_VR_HUDFRAMETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The screen-frame HUD in VR (#469): every element has a name and a VR home (the level-up, book and bag
 * icons in the radial menu, health and mana on the wrist); flat mode draws every element, VR none -
 * except what vr_hud_frame / vr_hud_keep bring back (no orb fallback without the wrist bars, #484); the level-up
 * indication marks the radial item while points wait and shows the wrist notice for a while after a
 * level-up.
 */
class HudFrameTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HudFrameTest);
	CPPUNIT_TEST(testEveryElementHasAHome);
	CPPUNIT_TEST(testNamesRoundTrip);
	CPPUNIT_TEST(testFlatDrawsTheFrame);
	CPPUNIT_TEST(testVrDrawsNoFrame);
	CPPUNIT_TEST(testNoOrbFallbackWithoutLeftArm);
	CPPUNIT_TEST(testSwitchesBringItBack);
	CPPUNIT_TEST(testParseKeep);
	CPPUNIT_TEST(testLevelUpModes);
	CPPUNIT_TEST(testLevelUpNotice);
	CPPUNIT_TEST_SUITE_END();

public:

	void testEveryElementHasAHome();
	void testNamesRoundTrip();
	void testFlatDrawsTheFrame();
	void testVrDrawsNoFrame();
	void testNoOrbFallbackWithoutLeftArm();
	void testSwitchesBringItBack();
	void testParseKeep();
	void testLevelUpModes();
	void testLevelUpNotice();

};

#endif // ARX_TESTS_VR_HUDFRAMETEST_H
