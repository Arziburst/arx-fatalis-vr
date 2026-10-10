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

#ifndef ARX_TESTS_VR_PHYSICALCROUCHTEST_H
#define ARX_TESTS_VR_PHYSICALCROUCHTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Physical crouch (#100, D-018): the head 30 cm below its reference crouches, it stands again above
 * 20 cm (no flicker between them), the switch turns it off; and the character's crouch never lowers
 * the camera a second time on top of the head's own drop.
 */
class PhysicalCrouchTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(PhysicalCrouchTest);
	CPPUNIT_TEST(testThresholds);
	CPPUNIT_TEST(testHysteresis);
	CPPUNIT_TEST(testSwitchedOff);
	CPPUNIT_TEST(testMisconfiguredExit);
	CPPUNIT_TEST(testCameraStanding);
	CPPUNIT_TEST(testCameraCrouched);
	CPPUNIT_TEST(testCameraContinuousThroughTheCrouch);
	CPPUNIT_TEST_SUITE_END();

public:

	void testThresholds();
	void testHysteresis();
	void testSwitchedOff();
	void testMisconfiguredExit();
	void testCameraStanding();
	void testCameraCrouched();
	void testCameraContinuousThroughTheCrouch();

};

#endif // ARX_TESTS_VR_PHYSICALCROUCHTEST_H
