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

#include "vr/SnapshotImage.h"

#include <algorithm>
#include <cstring>
#include <vector>

// stb_image_write v1.16 (public domain / MIT, LICENSE.stb): only its JPEG writer, to memory, private to this file
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb/stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vr {

namespace {

void appendBytes(void * context, void * data, int size) {
	if(size > 0) {
		static_cast<std::string *>(context)->append(static_cast<const char *>(data), size_t(size));
	}
}

} // anonymous namespace

RgbImage halfSize(const RgbImage & image) {

	const size_t width = image.width / 2;
	const size_t height = image.height / 2;
	if(width == 0 || height == 0 || image.pixels.size() < image.width * image.height * 3) {
		return RgbImage();
	}

	RgbImage half(width, height);
	for(size_t y = 0; y < height; y++) {
		const u8 * below = image.at(0, 2 * y);
		const u8 * above = image.at(0, 2 * y + 1);
		u8 * out = half.at(0, y);
		for(size_t x = 0; x < width; x++) {
			for(size_t c = 0; c < 3; c++) {
				const size_t left = 6 * x + c;
				const unsigned sum = unsigned(below[left]) + below[left + 3] + above[left] + above[left + 3];
				out[3 * x + c] = u8((sum + 2) / 4);
			}
		}
	}
	return half;
}

std::string formatJpeg(const RgbImage & image, int quality) {

	if(image.empty() || image.pixels.size() < image.width * image.height * 3) {
		return std::string();
	}

	// Top-down for the file: the JPEG's first row is the picture's top (the image's last row)
	const size_t stride = image.width * 3;
	std::vector<u8> rows(stride * image.height);
	for(size_t y = 0; y < image.height; y++) {
		std::memcpy(&rows[y * stride], image.at(0, image.height - 1 - y), stride);
	}

	std::string jpeg;
	if(!stbi_write_jpg_to_func(appendBytes, &jpeg, int(image.width), int(image.height), 3, rows.data(),
	                           std::clamp(quality, 1, 100))) {
		return std::string();
	}
	return jpeg;
}

} // namespace vr
