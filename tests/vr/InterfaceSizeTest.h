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

#ifndef ARX_TESTS_VR_INTERFACESIZETEST_H
#define ARX_TESTS_VR_INTERFACESIZETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The interface's own resolution in VR (#73): the head-locked quad gets the headset's pixel
 * density with the window's shape, within limits; the mouse maps between the window and the
 * interface's virtual screen.
 */
class InterfaceSizeTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(InterfaceSizeTest);
	CPPUNIT_TEST(testIndexDensity);
	CPPUNIT_TEST(testWindowAspectIsKept);
	CPPUNIT_TEST(testClampedToLimits);
	CPPUNIT_TEST(testClampedToTheSwapchain);
	CPPUNIT_TEST(testUnusableInputIsTheWindow);
	CPPUNIT_TEST(testPixelsPerDegree);
	CPPUNIT_TEST(testEqualSizesMapToThemselves);
	CPPUNIT_TEST(testMouseMapsAcross);
	CPPUNIT_TEST(testWindowPositionsRoundTrip);
	CPPUNIT_TEST_SUITE_END();

public:

	void testIndexDensity();
	void testWindowAspectIsKept();
	void testClampedToLimits();
	void testClampedToTheSwapchain();
	void testUnusableInputIsTheWindow();
	void testPixelsPerDegree();
	void testEqualSizesMapToThemselves();
	void testMouseMapsAcross();
	void testWindowPositionsRoundTrip();

};

#endif // ARX_TESTS_VR_INTERFACESIZETEST_H
