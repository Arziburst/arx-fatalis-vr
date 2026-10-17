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

#ifndef ARX_GRAPHICS_IMAGE_PIXELSTORE_H
#define ARX_GRAPHICS_IMAGE_PIXELSTORE_H

#include <stddef.h>

/*!
 * \brief How many bytes OpenGL reads from a client image under the GL_UNPACK_* pixel-store state (#495)
 *
 * Pure arithmetic, no OpenGL: host-tested in tests/graphics/PixelStoreTest.cpp.
 *
 * An \ref Image is tightly packed (its stride is width * bytes per pixel), and the engine sets
 * GL_UNPACK_ALIGNMENT to 1 once at start-up so that glTexImage2D reads exactly that. Any code that
 * changes the unpack state and leaves it (an alignment of 4, a row length) makes the next upload read
 * each row at a padded stride - past the end of a tight buffer whose rows are not a multiple of the
 * alignment. That is the crash of #495: a 16-bit cinematic TGA loaded as RGB, cut into tiles 256 wide
 * with a last column 110 wide, uploaded with alignment 4 - 332-byte rows read from a buffer of
 * 330-byte rows, 510 bytes past its end.
 *
 * The formula is the one of the OpenGL specification (section "Unpacking", 2D images, one byte per
 * component): the row stride is the row length (GL_UNPACK_ROW_LENGTH, or the image width when 0) in
 * bytes, rounded up to the alignment; the bytes read are the skipped rows and the rows before the last
 * one at that stride, plus the skipped pixels and the last row's own pixels.
 */
namespace pixelstore {

//! The GL_UNPACK_* state that decides where a 2D upload reads its bytes (OpenGL's defaults)
struct Unpack {
	int alignment = 4;   //!< GL_UNPACK_ALIGNMENT: 1, 2, 4 or 8
	int rowLength = 0;   //!< GL_UNPACK_ROW_LENGTH: pixels per row of the client image, 0 = the width
	int skipPixels = 0;  //!< GL_UNPACK_SKIP_PIXELS
	int skipRows = 0;    //!< GL_UNPACK_SKIP_ROWS
};

//! The unpack state the engine keeps and \ref Image needs: tight rows, nothing skipped
constexpr Unpack tightUnpack() {
	return Unpack{ 1, 0, 0, 0 };
}

//! Bytes between the starts of two rows as GL reads them
constexpr size_t rowStride(size_t width, size_t bytesPerPixel, const Unpack & unpack) {
	size_t pixelsPerRow = unpack.rowLength > 0 ? size_t(unpack.rowLength) : width;
	size_t alignment = unpack.alignment > 0 ? size_t(unpack.alignment) : 1;
	size_t bytes = pixelsPerRow * bytesPerPixel;
	return (bytes + alignment - 1) / alignment * alignment;
}

//! Bytes GL reads from the client pointer for a width x height upload of bytesPerPixel-byte pixels
constexpr size_t bytesRead(size_t width, size_t height, size_t bytesPerPixel, const Unpack & unpack) {
	if(width == 0 || height == 0) {
		return 0;
	}
	size_t stride = rowStride(width, bytesPerPixel, unpack);
	size_t skipRows = unpack.skipRows > 0 ? size_t(unpack.skipRows) : 0;
	size_t skipPixels = unpack.skipPixels > 0 ? size_t(unpack.skipPixels) : 0;
	return (skipRows + height - 1) * stride + (skipPixels + width) * bytesPerPixel;
}

//! Whether an upload reads only inside a client buffer of bufferSize bytes
constexpr bool fitsBuffer(size_t width, size_t height, size_t bytesPerPixel, const Unpack & unpack,
                          size_t bufferSize) {
	return bytesRead(width, height, bytesPerPixel, unpack) <= bufferSize;
}

} // namespace pixelstore

#endif // ARX_GRAPHICS_IMAGE_PIXELSTORE_H
