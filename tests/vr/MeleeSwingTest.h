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

#ifndef ARX_TESTS_VR_MELEESWINGTEST_H
#define ARX_TESTS_VR_MELEESWINGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The decision core of physical melee (#39): a swing from the controller's motion (lever point, median of
 * three, gaps and tracking jumps, stab rule, the longest swing), the charge from the trigger (the engine's
 * ratio, spent by a charged hit until released), the aim a hit is dealt with, the per-target cooldown.
 */
class MeleeSwingTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MeleeSwingTest);
	CPPUNIT_TEST(testStillAndSlowHandsDoNotSwing);
	CPPUNIT_TEST(testSlashStartsAndEnds);
	CPPUNIT_TEST(testWristFlickIsNoSwing);
	CPPUNIT_TEST(testOneBadSampleIsNoSwing);
	CPPUNIT_TEST(testTrackingJump);
	CPPUNIT_TEST(testGapAndLostTracking);
	CPPUNIT_TEST(testStab);
	CPPUNIT_TEST(testLongestSwing);
	CPPUNIT_TEST(testSpeedFactor);
	CPPUNIT_TEST(testCharge);
	CPPUNIT_TEST(testStrikeAim);
	CPPUNIT_TEST(testTargetCooldown);
	CPPUNIT_TEST_SUITE_END();

public:

	void testStillAndSlowHandsDoNotSwing();
	void testSlashStartsAndEnds();
	void testWristFlickIsNoSwing();
	void testOneBadSampleIsNoSwing();
	void testTrackingJump();
	void testGapAndLostTracking();
	void testStab();
	void testLongestSwing();
	void testSpeedFactor();
	void testCharge();
	void testStrikeAim();
	void testTargetCooldown();

};

#endif // ARX_TESTS_VR_MELEESWINGTEST_H
