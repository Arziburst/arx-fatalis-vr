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

#ifndef ARX_TESTS_VR_HANDCONFORMINGTEST_H
#define ARX_TESTS_VR_HANDCONFORMINGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The conforming hand (#385.7): HandJoints keep orientations (identity by default), an MRTK3-style hand-shape
 * JSON loads into HandJoints (a malformed one is refused with an error), the hilt pose keeps the fingertips
 * outside the hilt's cylinder with the thumb across the index finger on both hands (mirrored), the glove
 * posed from joint orientations reproduces its rest for a rest-pose joint set and moves a finger when that
 * finger's joint turns, and the palm sits behind the grip by handle radius + palm radius on the grip's X axis.
 */
class HandConformingTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HandConformingTest);
	CPPUNIT_TEST(testJointsKeepIdentityOrientationsByDefault);
	CPPUNIT_TEST(testLoadHandShapeReadsTwentySixJoints);
	CPPUNIT_TEST(testLoadHandShapeRefusesMalformedInput);
	CPPUNIT_TEST(testHiltPoseFingertipsStayOutsideTheHilt);
	CPPUNIT_TEST(testHiltPoseThumbLiesAcrossTheIndexFinger);
	CPPUNIT_TEST(testHiltPoseLeftMirrorsRight);
	CPPUNIT_TEST(testRestJointsReproduceTheGlovesRestLocals);
	CPPUNIT_TEST(testTurningAFingerJointMovesOnlyThatFinger);
	CPPUNIT_TEST(testPalmSitsBehindTheGripOnBothHands);
	CPPUNIT_TEST_SUITE_END();

public:

	void testJointsKeepIdentityOrientationsByDefault();
	void testLoadHandShapeReadsTwentySixJoints();
	void testLoadHandShapeRefusesMalformedInput();
	void testHiltPoseFingertipsStayOutsideTheHilt();
	void testHiltPoseThumbLiesAcrossTheIndexFinger();
	void testHiltPoseLeftMirrorsRight();
	void testRestJointsReproduceTheGlovesRestLocals();
	void testTurningAFingerJointMovesOnlyThatFinger();
	void testPalmSitsBehindTheGripOnBothHands();

};

#endif // ARX_TESTS_VR_HANDCONFORMINGTEST_H
