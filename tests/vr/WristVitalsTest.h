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

#ifndef ARX_TESTS_VR_WRISTVITALSTEST_H
#define ARX_TESTS_VR_WRISTVITALSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The wrist bars (#116): the two bars lie on the inner side of the wrist, health towards the elbow,
 * filling towards the thumb by the value; a left wrist held across the face like a watch shows red
 * left of blue, both standing up; they fade from 50 to 80 degrees off the eyes; a degenerate arm
 * draws nothing.
 */
class WristVitalsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(WristVitalsTest);
	CPPUNIT_TEST(testBarsOnTheInnerWrist);
	CPPUNIT_TEST(testFillFollowsTheValue);
	CPPUNIT_TEST(testWatchPoseReadsLikeTheFlatHud);
	CPPUNIT_TEST(testFadesAsTheWristTurnsAway);
	CPPUNIT_TEST(testDegenerateArms);
	CPPUNIT_TEST_SUITE_END();

public:

	void testBarsOnTheInnerWrist();
	void testFillFollowsTheValue();
	void testWatchPoseReadsLikeTheFlatHud();
	void testFadesAsTheWristTurnsAway();
	void testDegenerateArms();

};

#endif // ARX_TESTS_VR_WRISTVITALSTEST_H
