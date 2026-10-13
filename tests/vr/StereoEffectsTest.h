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

#ifndef ARX_TESTS_VR_STEREOEFFECTSTEST_H
#define ARX_TESTS_VR_STEREOEFFECTSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/StereoEffects.h"

/*!
 * Sprites and full-view overlays in stereo (#22): a sprite facing the head is the engine's own
 * square straight ahead and keeps its pixel size there; on canted eyes (the Index) the engine's
 * screen-space square is a different square for each eye - away from the middle the two eyes see
 * it at sizes more than 5 % apart (the negative control) - while the head-facing square is one
 * square, both eyes see it at the size its distance gives; an overlay over the union frustum covers
 * each eye's whole image.
 */
class StereoEffectsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(StereoEffectsTest);
	CPPUNIT_TEST(testFacingAxesStraightAheadAreTheScreens);
	CPPUNIT_TEST(testFacingAxesFaceTheHead);
	CPPUNIT_TEST(testHalfSizeKeepsTheEnginesPixels);
	CPPUNIT_TEST(testScreenSquaresDisagreeOnCantedEyes);
	CPPUNIT_TEST(testFacingSquareAgreesOnCantedEyes);
	CPPUNIT_TEST(testOverlayCoversBothCantedEyes);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFacingAxesStraightAheadAreTheScreens();
	void testFacingAxesFaceTheHead();
	void testHalfSizeKeepsTheEnginesPixels();
	void testScreenSquaresDisagreeOnCantedEyes();
	void testFacingSquareAgreesOnCantedEyes();
	void testOverlayCoversBothCantedEyes();

};

#endif // ARX_TESTS_VR_STEREOEFFECTSTEST_H
