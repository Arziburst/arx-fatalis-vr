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

#ifndef ARX_TESTS_GRAPHICS_PIXELSTORETEST_H
#define ARX_TESTS_GRAPHICS_PIXELSTORETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The bytes glTexImage2D reads under the GL_UNPACK_* state (#495): tight rows read exactly the image;
 * alignment 4 reads past a 3-byte-per-pixel image whose rows are not a multiple of 4 - the crash's own
 * tile, 110 x 256 RGB of akbaa dream.tga, and every cinematic tile width of the game data; RGBA is safe
 * at any alignment; a row length and skips move the read window.
 */
class PixelStoreTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(PixelStoreTest);
	CPPUNIT_TEST(tightRowsReadTheImage);
	CPPUNIT_TEST(theCrashTileOverflowsAtAlignment4);
	CPPUNIT_TEST(everyCinematicTileFitsTight);
	CPPUNIT_TEST(rgbaFitsAtAnyAlignment);
	CPPUNIT_TEST(rowLengthAndSkipsMoveTheWindow);
	CPPUNIT_TEST_SUITE_END();

public:

	void tightRowsReadTheImage();
	void theCrashTileOverflowsAtAlignment4();
	void everyCinematicTileFitsTight();
	void rgbaFitsAtAnyAlignment();
	void rowLengthAndSkipsMoveTheWindow();

};

#endif // ARX_TESTS_GRAPHICS_PIXELSTORETEST_H
