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

#ifndef ARX_TESTS_VR_LIBRARYHANDTEST_H
#define ARX_TESTS_VR_LIBRARYHANDTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The library hand driven like the glove (#467.1, data/core/vr/hands/webxr): the file's flat skeleton chained as
 * XR_EXT_hand_tracking chains it, the rest kept, the palm's side found without a palm joint, and the hilt pose
 * curling its fingers round the controller's handle.
 */
class LibraryHandTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(LibraryHandTest);
	CPPUNIT_TEST(testFlatSkeletonIsChained);
	CPPUNIT_TEST(testRestSkinningKeepsTheMesh);
	CPPUNIT_TEST(testPalmFacesTheThumbsSide);
	CPPUNIT_TEST(testHiltPoseCurlsRoundTheHandle);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFlatSkeletonIsChained();
	void testRestSkinningKeepsTheMesh();
	void testPalmFacesTheThumbsSide();
	void testHiltPoseCurlsRoundTheHandle();

};

#endif // ARX_TESTS_VR_LIBRARYHANDTEST_H
