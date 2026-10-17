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
 * three, gaps and tracking jumps, stab rule, the longest swing), the aim a hit is dealt with from the speed and the
 * stroke of the blade's point that touched - a thrust by the same rule, no charge (#465) - the per-target cooldown; the speed over
 * uneven frame times (#352) and the recovery after a hit per weapon type (#323).
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
	CPPUNIT_TEST(testContactAim);
	CPPUNIT_TEST(testDamageRanges);
	CPPUNIT_TEST(testPointVelocity);
	CPPUNIT_TEST(testStrokeSize);
	CPPUNIT_TEST(testThrustCountsLikeSlash);
	CPPUNIT_TEST(testTargetCooldown);
	CPPUNIT_TEST(testHitchIsNoSwing);
	CPPUNIT_TEST(testUnevenFramesPeak);
	CPPUNIT_TEST(testSameSwingsAt45And90Hz);
	CPPUNIT_TEST(testRecoveryTime);
	CPPUNIT_TEST(testRecoveryPerWeapon);
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
	void testContactAim();
	void testDamageRanges();
	void testPointVelocity();
	void testStrokeSize();
	void testThrustCountsLikeSlash();
	void testTargetCooldown();
	void testHitchIsNoSwing();
	void testUnevenFramesPeak();
	void testSameSwingsAt45And90Hz();
	void testRecoveryTime();
	void testRecoveryPerWeapon();

};

#endif // ARX_TESTS_VR_MELEESWINGTEST_H
