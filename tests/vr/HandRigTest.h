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

#ifndef ARX_TESTS_VR_HANDRIGTEST_H
#define ARX_TESTS_VR_HANDRIGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The five-finger rig of the custom hand (#385.4): the rig built from the shipped glove meshes is valid
 * on both hands (a missing joint makes it invalid), an open pose reproduces the rest world matrices,
 * a joint's flexion moves only the joints below it, a curled finger's tip moves toward the palm side
 * and toward the wrist, the knuckle's spread turns the finger sideways, and the left hand mirrors the
 * right. The shipped files are found through the compiled-in ARX_TEST_SOURCE_DIR.
 */
class HandRigTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HandRigTest);
	CPPUNIT_TEST(testRigIsValidOnBothHands);
	CPPUNIT_TEST(testRigIsInvalidWithAMissingJoint);
	CPPUNIT_TEST(testOpenPoseReproducesRestWorldMatrices);
	CPPUNIT_TEST(testCurlingOneFingerMovesOnlyThatFinger);
	CPPUNIT_TEST(testEachFlexionMovesOnlyTheJointsBelowIt);
	CPPUNIT_TEST(testCurledTipMovesTowardThePalmAndTheWrist);
	CPPUNIT_TEST(testFlexionIsMonotonic);
	CPPUNIT_TEST(testSpreadTurnsTheFingerSidewaysTowardTheThumb);
	CPPUNIT_TEST(testLeftHandMirrorsTheRight);
	CPPUNIT_TEST_SUITE_END();

public:

	void testRigIsValidOnBothHands();
	void testRigIsInvalidWithAMissingJoint();
	void testOpenPoseReproducesRestWorldMatrices();
	void testCurlingOneFingerMovesOnlyThatFinger();
	void testEachFlexionMovesOnlyTheJointsBelowIt();
	void testCurledTipMovesTowardThePalmAndTheWrist();
	void testFlexionIsMonotonic();
	void testSpreadTurnsTheFingerSidewaysTowardTheThumb();
	void testLeftHandMirrorsTheRight();

};

#endif // ARX_TESTS_VR_HANDRIGTEST_H
