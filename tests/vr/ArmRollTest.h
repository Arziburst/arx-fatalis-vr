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

#ifndef ARX_TESTS_VR_ARMROLLTEST_H
#define ARX_TESTS_VR_ARMROLLTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The roll-aware arm IK (#304, F-212, RENIK's rules): the hand's roll about the forearm is measured
 * against the bend plane, shared between the elbow swing, the forearm and the upper arm, and kept
 * on its side past a half turn until the overflow; a hand at its rest relation leaves every bone
 * without roll, and the joints stay where solveArm put them.
 */
class ArmRollTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ArmRollTest);
	CPPUNIT_TEST(testTwistAboutAxis);
	CPPUNIT_TEST(testUnwrapRollKeepsTheSide);
	CPPUNIT_TEST(testRestElbowDirection);
	CPPUNIT_TEST(testRestRelationHasNoRoll);
	CPPUNIT_TEST(testRollIsSharedAlongTheArm);
	CPPUNIT_TEST(testLargeRollSwingsTheElbow);
	CPPUNIT_TEST(testOverflowThroughTheSolver);
	CPPUNIT_TEST(testStraightRestArm);
	CPPUNIT_TEST(testOldRuleWringsTheWrist);
	CPPUNIT_TEST_SUITE_END();

public:

	void testTwistAboutAxis();
	void testUnwrapRollKeepsTheSide();
	void testRestElbowDirection();
	void testRestRelationHasNoRoll();
	void testRollIsSharedAlongTheArm();
	void testLargeRollSwingsTheElbow();
	void testOverflowThroughTheSolver();
	void testStraightRestArm();
	void testOldRuleWringsTheWrist();

};

#endif // ARX_TESTS_VR_ARMROLLTEST_H
