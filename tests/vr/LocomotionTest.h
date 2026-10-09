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

#ifndef ARX_TESTS_VR_LOCOMOTIONTEST_H
#define ARX_TESTS_VR_LOCOMOTIONTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Smooth stick locomotion of #31 (D-008): the stick's forward is the look yaw, the direction keeps the
 * stick's angle, the movement flags are the eight key directions, a turn of the heading in this frame counts;
 * the pace of #33: a slight deflection sneaks, a full one runs, with hysteresis.
 */
class LocomotionTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(LocomotionTest);
	CPPUNIT_TEST(testRestingStickDoesNotWalk);
	CPPUNIT_TEST(testForwardIsTheLook);
	CPPUNIT_TEST(testStrafeAndBack);
	CPPUNIT_TEST(testDiagonal);
	CPPUNIT_TEST(testSectors);
	CPPUNIT_TEST(testWalkYawFollowsTheHead);
	CPPUNIT_TEST(testPaceByDeflection);
	CPPUNIT_TEST(testPaceHysteresis);
	CPPUNIT_TEST_SUITE_END();

public:

	void testRestingStickDoesNotWalk();
	void testForwardIsTheLook();
	void testStrafeAndBack();
	void testDiagonal();
	void testSectors();
	void testWalkYawFollowsTheHead();
	void testPaceByDeflection();
	void testPaceHysteresis();

};

#endif // ARX_TESTS_VR_LOCOMOTIONTEST_H
