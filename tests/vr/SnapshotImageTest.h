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

#ifndef ARX_TESTS_VR_SNAPSHOTIMAGETEST_H
#define ARX_TESTS_VR_SNAPSHOTIMAGETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * A hold's periodic snapshots (#574; written before the implementation; the schedule and the names are in
 * tests/vr/MarkHoldTest.h): the LEFT eye at half resolution, as a JPEG (quality 85) - about 1008x1120 on the Index,
 * 150-250 KB, at most 31 a hold. The engine had no JPEG writer (VRMarks hand-rolls PNG, ImageSave writes BMP):
 * stb_image_write.h is vendored for it (its notice under LICENSE.stb). The engine's stb_image decodes what it writes
 * here, the way any viewer (and the coordinator's image input) must.
 *
 * In the game the halving may run on the GPU (glBlitFramebuffer GL_LINEAR at exactly half: each pixel the mean of a
 * 2x2 block, the same picture as \ref halfSize) with an asynchronous read-back; the JPEG is written on the writer
 * thread, never the frame's.
 *
 * The interface these tests expect, in src/vr/SnapshotImage.h (pure: no OpenGL, no engine globals, no file system),
 * built into arxtest with src/vr/SnapshotImage.cpp, and src/graphics/image/stb_image.cpp added to arxtest_SOURCES for
 * the decoder these tests use:
 *
 *   namespace vr {
 *
 *   //! The JPEG quality of a snapshot
 *   const int SnapshotJpegQuality = 85;
 *
 *   //! \a image at half its size: width / 2 x height / 2 (rounded down; an odd last row or column is dropped), each
 *   //! pixel the mean of its 2x2 block (rounded to the nearest), rows still bottom-up, no alpha plane. Empty when
 *   //! either half is 0
 *   RgbImage halfSize(const RgbImage & image);
 *
 *   //! \a image (RGB8, rows bottom-up - the OpenGL order, vr/EyeImage.h) as a baseline JPEG file's bytes, rows
 *   //! top-down as every viewer shows them, at \a quality (1-100); "" for an empty image
 *   std::string formatJpeg(const RgbImage & image, int quality = SnapshotJpegQuality);
 *
 *   }
 */
class SnapshotImageTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(SnapshotImageTest);
	CPPUNIT_TEST(halfSizeMeanTest);
	CPPUNIT_TEST(halfSizeOddTest);
	CPPUNIT_TEST(jpegOfTheIndexEyeTest);
	CPPUNIT_TEST(jpegOddSizeTest);
	CPPUNIT_TEST(jpegQualityTest);
	CPPUNIT_TEST(jpegEmptyTest);
	CPPUNIT_TEST_SUITE_END();

public:

	//! Each pixel the mean of its 2x2 block
	void halfSizeMeanTest();
	//! Odd sizes, 1x1, empty
	void halfSizeOddTest();
	//! A 2016x2240 eye halved to 1008x1120 decodes with stb_image, the right way up, its colours kept
	void jpegOfTheIndexEyeTest();
	//! A size that is no multiple of the JPEG's 8x8 / 16x16 blocks
	void jpegOddSizeTest();
	//! The quality is used: 85 by default, a lower one is smaller
	void jpegQualityTest();
	void jpegEmptyTest();

};

#endif // ARX_TESTS_VR_SNAPSHOTIMAGETEST_H
