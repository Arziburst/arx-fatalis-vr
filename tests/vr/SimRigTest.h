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

#ifndef ARX_TESTS_VR_SIMRIGTEST_H
#define ARX_TESTS_VR_SIMRIGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/SimRig.h"

/*!
 * The simulated headset of --vr-sim (#112): where its eyes are for a head pose, and how its
 * script sets the rig up, runs commands at their frames and moves numbers along ramps.
 */
class SimRigTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(SimRigTest);
	CPPUNIT_TEST(testEyeSeparationIsTheIpd);
	CPPUNIT_TEST(testYawRotatesBothEyes);
	CPPUNIT_TEST(testPositiveYawTurnsLeft);
	CPPUNIT_TEST(testHeadOffsetMovesBothEyes);
	CPPUNIT_TEST(testEyesSeenFromTheHead);
	CPPUNIT_TEST(testAnglesRoundTrip);
	CPPUNIT_TEST(testDefaultFovIsAsymmetricAndMirrored);
	CPPUNIT_TEST(testSetupCommands);
	CPPUNIT_TEST(testTimedCommandRunsAtItsFrame);
	CPPUNIT_TEST(testRampInterpolates);
	CPPUNIT_TEST(testRelativeFramesFollowThePreviousLine);
	CPPUNIT_TEST(testErrorsNameTheLine);
	CPPUNIT_TEST(testBadScriptAppliesNothing);
	CPPUNIT_TEST(testShotAndQuitAreRequests);
	CPPUNIT_TEST(testSetupOnlyCommandsAfterTheSessionStarted);
	CPPUNIT_TEST(testActionValues);
	CPPUNIT_TEST(testChannelTimedLineCountsFromNow);
	CPPUNIT_TEST(testShotCannotBeARamp);
	CPPUNIT_TEST(testFocusAndFault);
	CPPUNIT_TEST(testFingers);
	CPPUNIT_TEST(testSessionEndsAndExitFaults);
	CPPUNIT_TEST_SUITE_END();

public:

	void testEyeSeparationIsTheIpd();
	void testYawRotatesBothEyes();
	void testPositiveYawTurnsLeft();
	void testHeadOffsetMovesBothEyes();
	void testEyesSeenFromTheHead();
	void testAnglesRoundTrip();
	void testDefaultFovIsAsymmetricAndMirrored();
	void testSetupCommands();
	void testTimedCommandRunsAtItsFrame();
	void testRampInterpolates();
	void testRelativeFramesFollowThePreviousLine();
	void testErrorsNameTheLine();
	void testBadScriptAppliesNothing();
	void testShotAndQuitAreRequests();
	void testSetupOnlyCommandsAfterTheSessionStarted();
	void testActionValues();
	void testChannelTimedLineCountsFromNow();
	void testShotCannotBeARamp();
	void testFocusAndFault();
	void testFingers();
	void testSessionEndsAndExitFaults();

};

#endif // ARX_TESTS_VR_SIMRIGTEST_H
