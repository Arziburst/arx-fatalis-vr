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

#ifndef ARX_TESTS_VR_ARMFITTEST_H
#define ARX_TESTS_VR_ARMFITTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The custom arm's fit to the player (#385.6, F-307): the segment lengths come from the body height until
 * the reach is measured (a running maximum that ignores one outlier), keep the Arx upper arm to forearm
 * ratio, and the arm stretches by at most MaxArmStretch to a controller beyond its reach.
 */
class ArmFitTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ArmFitTest);
	CPPUNIT_TEST(testLengthsFromHeight);
	CPPUNIT_TEST(testReachNeedsSamples);
	CPPUNIT_TEST(testReachIgnoresOneOutlier);
	CPPUNIT_TEST(testReachResets);
	CPPUNIT_TEST(testLengthsFromReach);
	CPPUNIT_TEST(testStretchWithinReachIsZero);
	CPPUNIT_TEST(testStretchIsTheOvershootFraction);
	CPPUNIT_TEST(testStretchIsClamped);
	CPPUNIT_TEST_SUITE_END();

public:

	void testLengthsFromHeight();
	void testReachNeedsSamples();
	void testReachIgnoresOneOutlier();
	void testReachResets();
	void testLengthsFromReach();
	void testStretchWithinReachIsZero();
	void testStretchIsTheOvershootFraction();
	void testStretchIsClamped();

};

#endif // ARX_TESTS_VR_ARMFITTEST_H
