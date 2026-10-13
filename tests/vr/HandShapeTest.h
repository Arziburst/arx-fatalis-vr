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

#ifndef ARX_TESTS_VR_HANDSHAPETEST_H
#define ARX_TESTS_VR_HANDSHAPETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The hand shapes of the custom hand (#385.7, vr/HandRig.h): the joints' OpenXR frames run along the bones, the
 * left hand is the right one mirrored, a hand shape survives its JSON, the shipped MRTK3 Grab loads, the hilt pose
 * keeps the configured angles at the default hilt radius (finger-five.xrs' held asserts), the angles are clamped,
 * and the custom hand chooses its source in the documented order.
 */
class HandShapeTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HandShapeTest);
	CPPUNIT_TEST(testFramesRunAlongTheBones);
	CPPUNIT_TEST(testLeftHandMirrorsTheRight);
	CPPUNIT_TEST(testHandShapeJsonRoundTrip);
	CPPUNIT_TEST(testShippedGrabLoads);
	CPPUNIT_TEST(testHiltPoseKeepsTheAnglesAtTheDefaultRadius);
	CPPUNIT_TEST(testHiltAnglesAreClamped);
	CPPUNIT_TEST(testCustomHandSourceOrder);
	CPPUNIT_TEST(testControlsBlendTowardGrab);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFramesRunAlongTheBones();
	void testLeftHandMirrorsTheRight();
	void testHandShapeJsonRoundTrip();
	void testShippedGrabLoads();
	void testHiltPoseKeepsTheAnglesAtTheDefaultRadius();
	void testHiltAnglesAreClamped();
	void testCustomHandSourceOrder();
	void testControlsBlendTowardGrab();

};

#endif // ARX_TESTS_VR_HANDSHAPETEST_H
