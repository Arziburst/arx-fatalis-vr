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

#ifndef ARX_TESTS_VR_MIRRORGEOMETRYTEST_H
#define ARX_TESTS_VR_MIRRORGEOMETRYTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/MirrorGeometry.h"

/*!
 * The desktop mirror (#21) takes its picture from the left eye image by geometry alone: where the
 * eye looks ahead in its asymmetric field of view, which part of the image fills the window, and
 * where the head-locked flat frame quad appears in that eye.
 */
class MirrorGeometryTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MirrorGeometryTest);
	CPPUNIT_TEST(testSymmetricFovLooksAtTheCenter);
	CPPUNIT_TEST(testLeftEyeLooksRightOfTheCenter);
	CPPUNIT_TEST(testWideWindowCropsTheHeight);
	CPPUNIT_TEST(testTallWindowCropsTheWidth);
	CPPUNIT_TEST(testCropFollowsTheForwardPoint);
	CPPUNIT_TEST(testCropStaysInsideTheImage);
	CPPUNIT_TEST(testEmptySizesKeepTheWholeImage);
	CPPUNIT_TEST(testForwardPointProjectsToItself);
	CPPUNIT_TEST(testPointBehindTheEyeIsRefused);
	CPPUNIT_TEST(testHeadCenterAppearsRightOfTheLeftEyesForward);
	CPPUNIT_TEST(testCropCornersAreTheWindowCorners);
	CPPUNIT_TEST_SUITE_END();

public:

	void testSymmetricFovLooksAtTheCenter();
	void testLeftEyeLooksRightOfTheCenter();
	void testWideWindowCropsTheHeight();
	void testTallWindowCropsTheWidth();
	void testCropFollowsTheForwardPoint();
	void testCropStaysInsideTheImage();
	void testEmptySizesKeepTheWholeImage();
	void testForwardPointProjectsToItself();
	void testPointBehindTheEyeIsRefused();
	void testHeadCenterAppearsRightOfTheLeftEyesForward();
	void testCropCornersAreTheWindowCorners();

};

#endif // ARX_TESTS_VR_MIRRORGEOMETRYTEST_H
