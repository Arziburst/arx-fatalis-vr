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

#ifndef ARX_VR_SNAPSHOTIMAGE_H
#define ARX_VR_SNAPSHOTIMAGE_H

#include <string>

#include "vr/EyeImage.h"

/*!
 * \file
 * \brief A hold mark's periodic snapshots (#574): the left eye at half resolution as a JPEG
 *
 * About 1008x1120 on the Index, 150-250 KB, at most vr::SnapshotMax a hold. The game halves the eye on the GPU (a
 * linear blit at exactly half: each pixel the mean of a 2x2 block, the picture \ref halfSize gives) and reads it
 * back asynchronously; the JPEG is written on the mark writer thread (vr/VRMarks.cpp). The JPEG encoder is
 * stb_image_write (libs/stb, LICENSE.stb). Pure: no OpenGL, no engine globals, no file system - covered by
 * tests/vr/SnapshotImageTest.cpp.
 */
namespace vr {

//! The JPEG quality of a snapshot
const int SnapshotJpegQuality = 85;

/*!
 * \a image at half its size: width / 2 x height / 2 (rounded down; an odd last row or column is dropped), each pixel
 * the mean of its 2x2 block (rounded to the nearest), rows still bottom-up, no alpha plane. Empty when either half
 * is 0
 */
RgbImage halfSize(const RgbImage & image);

/*!
 * \a image (RGB8, rows bottom-up - the OpenGL order) as a baseline JPEG file's bytes, rows top-down as every viewer
 * shows them, at \a quality (1-100); "" for an empty image
 */
std::string formatJpeg(const RgbImage & image, int quality = SnapshotJpegQuality);

} // namespace vr

#endif // ARX_VR_SNAPSHOTIMAGE_H
