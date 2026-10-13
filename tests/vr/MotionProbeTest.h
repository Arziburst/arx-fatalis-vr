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

#ifndef ARX_TESTS_VR_MOTIONPROBETEST_H
#define ARX_TESTS_VR_MOTIONPROBETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The motion probe of the refresh-rate check (#23): the same walk, turn and jump measured at 72, 90,
 * 120 and 144 frames per second give the same numbers - the gauge itself must not depend on the rate.
 */
class MotionProbeTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MotionProbeTest);
	CPPUNIT_TEST(testWalkIsTheSameAtEveryRate);
	CPPUNIT_TEST(testTurnUnwrapsAcrossZero);
	CPPUNIT_TEST(testTeleportIsNoWalk);
	CPPUNIT_TEST(testJumpIsTheSameAtEveryRate);
	CPPUNIT_TEST(testJumpThatNeverLeavesTheGround);
	CPPUNIT_TEST(testResetStartsFromZero);
	CPPUNIT_TEST_SUITE_END();

public:

	void testWalkIsTheSameAtEveryRate();
	void testTurnUnwrapsAcrossZero();
	void testTeleportIsNoWalk();
	void testJumpIsTheSameAtEveryRate();
	void testJumpThatNeverLeavesTheGround();
	void testResetStartsFromZero();

};

#endif // ARX_TESTS_VR_MOTIONPROBETEST_H
