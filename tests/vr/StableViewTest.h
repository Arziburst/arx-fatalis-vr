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

#ifndef ARX_TESTS_VR_STABLEVIEWTEST_H
#define ARX_TESTS_VR_STABLEVIEWTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The stable view (#416): the view's height follows the body's feet and the stance's eye with no step above
 * 2 cm per frame at 90 Hz (the same speed at 45 Hz); a level jump does not move it, an arc jump is a soft hop;
 * a fall pulls it along, a teleport cuts; and the camera motion meter tells the body's travel from the rest.
 */
class StableViewTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(StableViewTest);
	CPPUNIT_TEST(testFirstFrameAndStill);
	CPPUNIT_TEST(testStepUpGlides);
	CPPUNIT_TEST(testLevelJumpKeepsTheView);
	CPPUNIT_TEST(testLevelJumpOntoALedge);
	CPPUNIT_TEST(testLandingNearTheTakeOff);
	CPPUNIT_TEST(testArcJumpIsSoft);
	CPPUNIT_TEST(testCrouchGlides);
	CPPUNIT_TEST(testCrouchOnStairsKeepsTheLimit);
	CPPUNIT_TEST(testLevitationGlides);
	CPPUNIT_TEST(testFallPullsTheView);
	CPPUNIT_TEST(testTeleportCuts);
	CPPUNIT_TEST(testNames);
	CPPUNIT_TEST(testMeterStill);
	CPPUNIT_TEST(testMeterBodyTravelAndTurn);
	CPPUNIT_TEST(testMeterSway);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFirstFrameAndStill();
	void testStepUpGlides();
	void testLevelJumpKeepsTheView();
	void testLevelJumpOntoALedge();
	void testLandingNearTheTakeOff();
	void testArcJumpIsSoft();
	void testCrouchGlides();
	void testCrouchOnStairsKeepsTheLimit();
	void testLevitationGlides();
	void testFallPullsTheView();
	void testTeleportCuts();
	void testNames();
	void testMeterStill();
	void testMeterBodyTravelAndTurn();
	void testMeterSway();

};

#endif // ARX_TESTS_VR_STABLEVIEWTEST_H
