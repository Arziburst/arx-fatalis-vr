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

#ifndef ARX_TESTS_VR_MELEECHARGECUETEST_H
#define ARX_TESTS_VR_MELEECHARGECUETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The melee charge cues (#322): the setting's names, which variants a setting shows, the fill following the
 * engine's charge (empty when not charging, after the strike spent it and after the release), the wrist ring
 * turned to the eye and filling clockwise from 12 o'clock, the blade strip from the hilt towards the tip, the
 * weapon's glow, and degenerate input drawing nothing.
 */
class MeleeChargeCueTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MeleeChargeCueTest);
	CPPUNIT_TEST(testNames);
	CPPUNIT_TEST(testWhichVariantsShow);
	CPPUNIT_TEST(testFillFollowsTheEngineCharge);
	CPPUNIT_TEST(testRingAtTheWristFacingTheEye);
	CPPUNIT_TEST(testRingFillsClockwise);
	CPPUNIT_TEST(testBladeStrip);
	CPPUNIT_TEST(testGlow);
	CPPUNIT_TEST(testDegenerateInput);
	CPPUNIT_TEST_SUITE_END();

public:

	void testNames();
	void testWhichVariantsShow();
	void testFillFollowsTheEngineCharge();
	void testRingAtTheWristFacingTheEye();
	void testRingFillsClockwise();
	void testBladeStrip();
	void testGlow();
	void testDegenerateInput();

};

#endif // ARX_TESTS_VR_MELEECHARGECUETEST_H
