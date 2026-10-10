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

#ifndef ARX_TESTS_VR_ARMIKTEST_H
#define ARX_TESTS_VR_ARMIKTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The arm IK of the full body (#36, D-009, F-52) and the placement of a hand in the engine world
 * (HeadPose.h): a reachable target is met exactly with the bone lengths kept and the elbow on the
 * hint's side; an out-of-reach target slides the shoulder up to the limit and then stretches the arm
 * at it; a hand is placed rigid with the head the way the head itself is placed; the hand frame read
 * from a mesh has the knuckles away from the wrist and the thumb on the wider side.
 */
class ArmIKTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ArmIKTest);
	CPPUNIT_TEST(testRotationBetween);
	CPPUNIT_TEST(testReachableTargetIsMet);
	CPPUNIT_TEST(testElbowBendsTowardsTheHint);
	CPPUNIT_TEST(testOutOfReachSlidesThenStretches);
	CPPUNIT_TEST(testFoldedAndDegenerateTargets);
	CPPUNIT_TEST(testHandMovesLikeTheHead);
	CPPUNIT_TEST(testHandTurnsLikeTheHead);
	CPPUNIT_TEST(testHandFrameFromMesh);
	CPPUNIT_TEST(testHandToGripPutsTheAxesOnTheGrip);
	CPPUNIT_TEST_SUITE_END();

public:

	void testRotationBetween();
	void testReachableTargetIsMet();
	void testElbowBendsTowardsTheHint();
	void testOutOfReachSlidesThenStretches();
	void testFoldedAndDegenerateTargets();
	void testHandMovesLikeTheHead();
	void testHandTurnsLikeTheHead();
	void testHandFrameFromMesh();
	void testHandToGripPutsTheAxesOnTheGrip();

};

#endif // ARX_TESTS_VR_ARMIKTEST_H
