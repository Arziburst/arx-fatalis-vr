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

#ifndef ARX_TESTS_VR_MENUPOINTERTEST_H
#define ARX_TESTS_VR_MENUPOINTERTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The menu pointer of #34 (D-010, D-019, F-81): nothing without the panel; the hand that pressed
 * last owns the cursor and the other hand's press takes it over at its own pixel; the other hand
 * takes an idle cursor its owner pointed away from; the cursor holds the press pixel for the click
 * dead zone, then drags; a press off the panel never reaches the engine.
 */
class MenuPointerTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MenuPointerTest);
	CPPUNIT_TEST(testNothingWithoutThePanel);
	CPPUNIT_TEST(testRayMovesTheCursor);
	CPPUNIT_TEST(testLastPressOwnsTheCursor);
	CPPUNIT_TEST(testIdleOwnerHandsOver);
	CPPUNIT_TEST(testClickDeadZone);
	CPPUNIT_TEST(testPressOffThePanelIsMuted);
	CPPUNIT_TEST(testBeamFacesTheEyes);
	CPPUNIT_TEST_SUITE_END();

public:

	void testNothingWithoutThePanel();
	void testRayMovesTheCursor();
	void testLastPressOwnsTheCursor();
	void testIdleOwnerHandsOver();
	void testClickDeadZone();
	void testPressOffThePanelIsMuted();
	void testBeamFacesTheEyes();

};

#endif // ARX_TESTS_VR_MENUPOINTERTEST_H
