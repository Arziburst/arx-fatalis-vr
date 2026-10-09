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

#ifndef ARX_TESTS_VR_EYEIMAGETEST_H
#define ARX_TESTS_VR_EYEIMAGETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/EyeImage.h"

/*!
 * What the simulated compositor (#112) reads from eye pictures: "is the world drawn or black"
 * (the bug of #215), the left/right difference and the parallax sign of the two-eyes check
 * (F-06), and where the head-locked quad lands in each eye.
 */
class EyeImageTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(EyeImageTest);
	CPPUNIT_TEST(testBlackPictureIsBlack);
	CPPUNIT_TEST(testBackgroundPictureIsBackground);
	CPPUNIT_TEST(testFlatColorIsUniform);
	CPPUNIT_TEST(testDetailIsWorld);
	CPPUNIT_TEST(testHudAroundABlackMiddleIsBlack);
	CPPUNIT_TEST(testEmptyPicture);
	CPPUNIT_TEST(testGridKeepsTheAspect);
	CPPUNIT_TEST(testMeanAbsDiff);
	CPPUNIT_TEST(testShiftFindsTheOffset);
	CPPUNIT_TEST(testSwappedPicturesShiftTheOtherWay);
	CPPUNIT_TEST(testQuadFillsTheMiddleOfAnEye);
	CPPUNIT_TEST(testQuadBehindTheEyeIsNotDrawn);
	CPPUNIT_TEST(testQuadAppearsRightOfTheLeftEyesForward);
	CPPUNIT_TEST(testComposedEyesHaveAPositiveShift);
	CPPUNIT_TEST_SUITE_END();

public:

	void testBlackPictureIsBlack();
	void testBackgroundPictureIsBackground();
	void testFlatColorIsUniform();
	void testDetailIsWorld();
	void testHudAroundABlackMiddleIsBlack();
	void testEmptyPicture();
	void testGridKeepsTheAspect();
	void testMeanAbsDiff();
	void testShiftFindsTheOffset();
	void testSwappedPicturesShiftTheOtherWay();
	void testQuadFillsTheMiddleOfAnEye();
	void testQuadBehindTheEyeIsNotDrawn();
	void testQuadAppearsRightOfTheLeftEyesForward();
	void testComposedEyesHaveAPositiveShift();

};

#endif // ARX_TESTS_VR_EYEIMAGETEST_H
