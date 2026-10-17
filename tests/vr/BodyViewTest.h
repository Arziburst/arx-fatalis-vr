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

#ifndef ARX_TESTS_VR_BODYVIEWTEST_H
#define ARX_TESTS_VR_BODYVIEWTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The own body never blocks the view (#385.9, #451): the neck pivot is below the eyes and the body sits
 * pushed back behind them along the facing (engine yaw, forward = (-sin, 0, cos), engine y down), the body
 * offset puts the model's neck on the pivot, and the faces near an eye are hidden - but never the legs.
 */
class BodyViewTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(BodyViewTest);
	CPPUNIT_TEST(testPivotIsBelowTheEye);
	CPPUNIT_TEST(testPivotIsPushedBehindTheFacing);
	CPPUNIT_TEST(testPushFollowsTheParameters);
	CPPUNIT_TEST(testOffsetPutsTheNeckOnThePivot);
	CPPUNIT_TEST(testFaceAtTheEyeIsHidden);
	CPPUNIT_TEST(testFaceFarFromTheEyesIsDrawn);
	CPPUNIT_TEST(testEitherEyeCounts);
	CPPUNIT_TEST(testAnyCornerCounts);
	CPPUNIT_TEST(testLegsAreNeverHidden);
	CPPUNIT_TEST_SUITE_END();

public:

	void testPivotIsBelowTheEye();
	void testPivotIsPushedBehindTheFacing();
	void testPushFollowsTheParameters();
	void testOffsetPutsTheNeckOnThePivot();
	void testFaceAtTheEyeIsHidden();
	void testFaceFarFromTheEyesIsDrawn();
	void testEitherEyeCounts();
	void testAnyCornerCounts();
	void testLegsAreNeverHidden();

};

#endif // ARX_TESTS_VR_BODYVIEWTEST_H
