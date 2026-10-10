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

#ifndef ARX_TESTS_VR_FINGERCURLTEST_H
#define ARX_TESTS_VR_FINGERCURLTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The finger curl of the VR body (#305, F-218): the model's three curls per hand from the 26 hand
 * joints of XR_EXT_hand_tracking (an open hand bends nothing, a fist bends every segment to its
 * limit, a pointing hand only the others, in any space and for either hand), from the controller's
 * action values when there are no joints, the closed hand on a weapon, and the per-frame approach.
 */
class FingerCurlTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(FingerCurlTest);
	CPPUNIT_TEST(testFlexionDegrees);
	CPPUNIT_TEST(testSynthesizedHandLiesInTheGripFrame);
	CPPUNIT_TEST(testOpenHandIsStraight);
	CPPUNIT_TEST(testFistReachesTheLimits);
	CPPUNIT_TEST(testPointingHandAndThumbsUp);
	CPPUNIT_TEST(testHalfClosedIsBetween);
	CPPUNIT_TEST(testCurlIgnoresTheSpaceAndTheHand);
	CPPUNIT_TEST(testControlsFallback);
	CPPUNIT_TEST(testClosedHand);
	CPPUNIT_TEST(testApproachStepsTowardsTheTarget);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFlexionDegrees();
	void testSynthesizedHandLiesInTheGripFrame();
	void testOpenHandIsStraight();
	void testFistReachesTheLimits();
	void testPointingHandAndThumbsUp();
	void testHalfClosedIsBetween();
	void testCurlIgnoresTheSpaceAndTheHand();
	void testControlsFallback();
	void testClosedHand();
	void testApproachStepsTowardsTheTarget();

};

#endif // ARX_TESTS_VR_FINGERCURLTEST_H
