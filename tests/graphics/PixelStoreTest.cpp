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

#include "graphics/PixelStoreTest.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <vector>

#include <cppunit/TestAssert.h>

#include "graphics/image/PixelStore.h"

CppUnit::AutoRegisterSuite<PixelStoreTest> g_registerPixelStoreTest;

namespace {

const pixelstore::Unpack g_tight = pixelstore::tightUnpack();
const pixelstore::Unpack g_alignment4 = pixelstore::Unpack{ 4, 0, 0, 0 };

//! The tiles CreateCinematicBitmap cuts an image into: 256 x 256, the last column and row what is left
std::vector<size_t> tileLengths(size_t length) {
	std::vector<size_t> lengths;
	for(size_t left = length; left > 0; left -= std::min(left, size_t(256))) {
		lengths.push_back(std::min(left, size_t(256)));
	}
	return lengths;
}

/*!
 * Every cinematic image of the game data (data.pak, graph/interface/illustrations/*.cin, 26 distinct
 * sizes in 19 cinematics), as the scan of #495 listed them. All but one are 16-bit TGAs; Image::load
 * turns them, like the one 24-bit TGA, into 3 bytes per pixel.
 */
struct CinematicImage {
	size_t width;
	size_t height;
	const char * name;
};

const CinematicImage g_cinematicImages[] = {
	{ 1074, 820, "florence_guilde.tga" },
	{ 1075, 820, "herzog_sacrifice.tga" },
	{ 1080, 817, "meurtre.tga" },
	{ 1080, 818, "ordre.tga" },
	{ 1080, 819, "adoration.tga" },
	{ 1080, 822, "sylib_noden.tga" },
	{ 1080, 1736, "noden.tga" },
	{ 1090, 830, "sylib_arbre.tga" },
	{ 1100, 824, "autel.tga / sky breach flou.tga / sky no breach flou.tga" },
	{ 1100, 850, "sacrifice.tga" },
	{ 1100, 855, "planete_gros plan.tga" },
	{ 1100, 908, "soleil.tga" },
	{ 1100, 2000, "sky breach.tga / sky no breach.tga" },
	{ 1100, 2300, "tunnel.tga" },
	{ 1100, 2770, "introduction verticale_contraste.tga" },
	{ 1100, 2934, "horizon.tga" },
	{ 1110, 860, "ylside_porte.tga / ylside_epee.tga" },
	{ 1116, 822, "herzog_mort.tga" },
	{ 1361, 1592, "sylib_profil.tga" },
	{ 1390, 1390, "akbaa dream.tga" },
	{ 1444, 835, "traite femmeserpent.tga" },
	{ 1675, 1089, "pleureuse.tga" },
	{ 2100, 1000, "foret.tga" },
	{ 2465, 768, "ylside_fond.tga" },
	{ 3535, 1331, "planete.tga" },
	{ 4295, 820, "introduction_horizontale_contraste.tga" },
};

} // anonymous namespace

void PixelStoreTest::tightRowsReadTheImage() {

	// A full 256 x 256 RGB tile: tight rows read exactly the buffer, and alignment 4 is harmless (768 % 4 == 0)
	CPPUNIT_ASSERT_EQUAL(size_t(256 * 256 * 3), pixelstore::bytesRead(256, 256, 3, g_tight));
	CPPUNIT_ASSERT_EQUAL(size_t(256 * 256 * 3), pixelstore::bytesRead(256, 256, 3, g_alignment4));

	// Odd sizes and every channel count: tight is always the image's own size
	for(size_t bpp = 1; bpp <= 4; bpp++) {
		for(size_t width = 1; width <= 300; width += 7) {
			CPPUNIT_ASSERT_EQUAL(width * 13 * bpp, pixelstore::bytesRead(width, 13, bpp, g_tight));
			CPPUNIT_ASSERT(pixelstore::fitsBuffer(width, 13, bpp, g_tight, width * 13 * bpp));
		}
	}

	// Nothing is read for an empty image
	CPPUNIT_ASSERT_EQUAL(size_t(0), pixelstore::bytesRead(0, 13, 3, g_alignment4));
	CPPUNIT_ASSERT_EQUAL(size_t(0), pixelstore::bytesRead(13, 0, 3, g_alignment4));
}

void PixelStoreTest::theCrashTileOverflowsAtAlignment4() {

	// The crash of #495: akbaa dream.tga 1390 x 1390 RGB, its last column of tiles 110 pixels wide. A row is
	// 330 bytes; at alignment 4 GL reads rows 332 bytes apart - 255 padded rows and the last one, 510 bytes
	// past the 84480-byte tile. The last tile of the column (110 x 110) overflows by 218 bytes.
	CPPUNIT_ASSERT_EQUAL(size_t(330), pixelstore::rowStride(110, 3, g_tight));
	CPPUNIT_ASSERT_EQUAL(size_t(332), pixelstore::rowStride(110, 3, g_alignment4));
	CPPUNIT_ASSERT_EQUAL(size_t(84480), pixelstore::bytesRead(110, 256, 3, g_tight));
	CPPUNIT_ASSERT_EQUAL(size_t(84990), pixelstore::bytesRead(110, 256, 3, g_alignment4));
	CPPUNIT_ASSERT(!pixelstore::fitsBuffer(110, 256, 3, g_alignment4, 84480));
	CPPUNIT_ASSERT(pixelstore::fitsBuffer(110, 256, 3, g_tight, 84480));
	CPPUNIT_ASSERT_EQUAL(size_t(36518), pixelstore::bytesRead(110, 110, 3, g_alignment4));
	CPPUNIT_ASSERT_EQUAL(size_t(36300), pixelstore::bytesRead(110, 110, 3, g_tight));

	// The other alignments GL allows read past a 330-byte row too (2 only when the row is odd: 330 is not)
	CPPUNIT_ASSERT_EQUAL(size_t(84480), pixelstore::bytesRead(110, 256, 3, pixelstore::Unpack{ 2, 0, 0, 0 }));
	CPPUNIT_ASSERT_EQUAL(size_t(336 * 255 + 330), pixelstore::bytesRead(110, 256, 3, pixelstore::Unpack{ 8, 0, 0, 0 }));
}

void PixelStoreTest::everyCinematicTileFitsTight() {

	// Every tile of every cinematic image of the game, as the loader cuts and uploads it (3 bytes per pixel):
	// tight rows read exactly the tile's buffer ...
	std::set<size_t> overflowingWidths;
	size_t tiles = 0;
	for(const CinematicImage & image : g_cinematicImages) {
		for(size_t tileHeight : tileLengths(image.height)) {
			for(size_t tileWidth : tileLengths(image.width)) {
				tiles++;
				size_t buffer = tileWidth * tileHeight * 3;
				std::ostringstream where;
				where << image.name << " tile " << tileWidth << 'x' << tileHeight;
				CPPUNIT_ASSERT_EQUAL_MESSAGE(where.str(), buffer, pixelstore::bytesRead(tileWidth, tileHeight, 3, g_tight));
				if(!pixelstore::fitsBuffer(tileWidth, tileHeight, 3, g_alignment4, buffer)) {
					overflowingWidths.insert(tileWidth);
				}
			}
		}
	}
	CPPUNIT_ASSERT(tiles > 500);

	// ... while the state the VR overlays left behind (alignment 4) overflows on exactly the tile widths whose
	// 3-byte rows are not a multiple of 4 - the last column of 17 images in 12 of the 19 cinematics
	const std::set<size_t> expected = { 50, 51, 66, 81, 86, 110, 139, 161, 199, 207 };
	CPPUNIT_ASSERT(overflowingWidths == expected);
	for(size_t width : overflowingWidths) {
		CPPUNIT_ASSERT((width * 3) % 4 != 0);
	}
}

void PixelStoreTest::rgbaFitsAtAnyAlignment() {

	// 4 bytes per pixel: every row is a multiple of 4, so the overlays' own uploads (RGBA, alignment 4) and the
	// engine's RGBA textures read exactly their buffer whatever the alignment up to 4
	for(int alignment : { 1, 2, 4 }) {
		pixelstore::Unpack unpack{ alignment, 0, 0, 0 };
		CPPUNIT_ASSERT_EQUAL(size_t(8 * 64 * 4), pixelstore::bytesRead(8, 64, 4, unpack));
		CPPUNIT_ASSERT_EQUAL(size_t(110 * 256 * 4), pixelstore::bytesRead(110, 256, 4, unpack));
		CPPUNIT_ASSERT_EQUAL(size_t(33 * 7 * 4), pixelstore::bytesRead(33, 7, 4, unpack));
	}
	// Alignment 8 pads an odd RGBA row count
	CPPUNIT_ASSERT_EQUAL(size_t(136 * 6 + 132), pixelstore::bytesRead(33, 7, 4, pixelstore::Unpack{ 8, 0, 0, 0 }));
}

void PixelStoreTest::rowLengthAndSkipsMoveTheWindow() {

	// A row length longer than the width: the rows are read at the longer stride, the last one only to its width
	pixelstore::Unpack rowLength{ 1, 640, 0, 0 };
	CPPUNIT_ASSERT_EQUAL(size_t(640 * 3), pixelstore::rowStride(110, 3, rowLength));
	CPPUNIT_ASSERT_EQUAL(size_t(640 * 3 * 255 + 110 * 3), pixelstore::bytesRead(110, 256, 3, rowLength));
	CPPUNIT_ASSERT(!pixelstore::fitsBuffer(110, 256, 3, rowLength, 110 * 256 * 3));

	// Skipped pixels and rows push the window further into the buffer
	pixelstore::Unpack skips{ 1, 0, 5, 2 };
	CPPUNIT_ASSERT_EQUAL(size_t((2 + 255) * 330 + (5 + 110) * 3), pixelstore::bytesRead(110, 256, 3, skips));

	// A tight image with a row length equal to its width is the plain tight read
	pixelstore::Unpack sameLength{ 1, 110, 0, 0 };
	CPPUNIT_ASSERT_EQUAL(size_t(84480), pixelstore::bytesRead(110, 256, 3, sameLength));
}
