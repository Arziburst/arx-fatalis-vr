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

#include "vr/SnapshotImageTest.h"

#include <cmath>
#include <cstdlib>
#include <string>

#include "graphics/image/stb_image.h"
#include "platform/Platform.h"
#include "vr/EyeImage.h"
#include "vr/SnapshotImage.h"

CppUnit::AutoRegisterSuite<SnapshotImageTest> g_registerSnapshotImageTest;

using namespace vr;

namespace {

struct SiColor {
	int r, g, b;
};

void siSet(RgbImage & image, size_t x, size_t y, SiColor c) {
	u8 * p = image.at(x, y);
	p[0] = u8(c.r);
	p[1] = u8(c.g);
	p[2] = u8(c.b);
}

//! The decoded JPEG: rows top-down, RGB8
struct SiDecoded {
	int width = 0;
	int height = 0;
	int components = 0;
	std::string pixels;
	bool ok = false;
	SiColor at(int x, int y) const {
		const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 3;
		return { u8(pixels[i]), u8(pixels[i + 1]), u8(pixels[i + 2]) };
	}
};

SiDecoded siDecode(const std::string & jpeg) {
	SiDecoded result;
	const stbi::stbi_uc * raw = reinterpret_cast<const stbi::stbi_uc *>(jpeg.data());
	unsigned char * pixels = stbi::stbi_load_from_memory(raw, int(jpeg.size()), &result.width, &result.height,
	                                                     &result.components, 3);
	if(pixels) {
		result.pixels.assign(reinterpret_cast<const char *>(pixels), size_t(result.width) * size_t(result.height) * 3);
		stbi::stbi_image_free(pixels);
		result.ok = true;
	}
	return result;
}

void siAssertNear(const std::string & what, SiColor expected, SiColor actual, int tolerance) {
	const std::string text = what + ": expected " + std::to_string(expected.r) + "," + std::to_string(expected.g) + ","
	                         + std::to_string(expected.b) + ", got " + std::to_string(actual.r) + ","
	                         + std::to_string(actual.g) + "," + std::to_string(actual.b);
	CPPUNIT_ASSERT_MESSAGE(text, std::abs(expected.r - actual.r) <= tolerance);
	CPPUNIT_ASSERT_MESSAGE(text, std::abs(expected.g - actual.g) <= tolerance);
	CPPUNIT_ASSERT_MESSAGE(text, std::abs(expected.b - actual.b) <= tolerance);
}

SiColor siPixel(const RgbImage & image, size_t x, size_t y) {
	const u8 * p = image.at(x, y);
	return { p[0], p[1], p[2] };
}

const SiColor SiRed = { 200, 40, 40 };
const SiColor SiGreen = { 40, 180, 60 };
const SiColor SiBlue = { 30, 60, 200 };
const SiColor SiLight = { 230, 230, 210 };

//! A picture in four quadrants, rows bottom-up: red bottom left, green bottom right, blue top left, light top right
RgbImage siQuadrants(size_t width, size_t height) {
	RgbImage image(width, height);
	for(size_t y = 0; y < height; y++) {
		for(size_t x = 0; x < width; x++) {
			const bool right = x >= width / 2;
			const bool top = y >= height / 2;
			siSet(image, x, y, top ? (right ? SiLight : SiBlue) : (right ? SiGreen : SiRed));
		}
	}
	return image;
}

bool siIsJpeg(const std::string & bytes) {
	return bytes.size() > 4 && u8(bytes[0]) == 0xFF && u8(bytes[1]) == 0xD8 && u8(bytes[2]) == 0xFF
	       && u8(bytes[bytes.size() - 2]) == 0xFF && u8(bytes[bytes.size() - 1]) == 0xD9;
}

} // anonymous namespace

void SnapshotImageTest::halfSizeMeanTest() {

	// 4x2 (rows bottom-up): two 2x2 blocks
	RgbImage image(4, 2);
	siSet(image, 0, 0, { 10, 0, 255 });
	siSet(image, 1, 0, { 20, 100, 255 });
	siSet(image, 0, 1, { 30, 200, 255 });
	siSet(image, 1, 1, { 41, 50, 255 });
	siSet(image, 2, 0, { 0, 0, 0 });
	siSet(image, 3, 0, { 3, 3, 3 });
	siSet(image, 2, 1, { 0, 0, 0 });
	siSet(image, 3, 1, { 0, 1, 0 });

	const RgbImage half = halfSize(image);
	CPPUNIT_ASSERT_EQUAL(size_t(2), half.width);
	CPPUNIT_ASSERT_EQUAL(size_t(1), half.height);
	CPPUNIT_ASSERT_EQUAL(size_t(2 * 1 * 3), half.pixels.size());
	CPPUNIT_ASSERT_MESSAGE("no alpha plane", half.alpha.empty());

	// The mean of each block, rounded to the nearest (25.25, 87.5, 255 / 0.75, 1, 0.75)
	const SiColor a = siPixel(half, 0, 0);
	CPPUNIT_ASSERT_EQUAL(25, a.r);
	CPPUNIT_ASSERT_MESSAGE("87.5 rounds to 87 or 88", a.g == 87 || a.g == 88);
	CPPUNIT_ASSERT_EQUAL(255, a.b);
	const SiColor b = siPixel(half, 1, 0);
	CPPUNIT_ASSERT_EQUAL(1, b.r);
	CPPUNIT_ASSERT_EQUAL(1, b.g);
	CPPUNIT_ASSERT_EQUAL(1, b.b);

	// A flat picture stays exactly that colour; bottom-up stays bottom-up
	RgbImage flat(6, 4);
	for(size_t y = 0; y < 4; y++) {
		for(size_t x = 0; x < 6; x++) {
			siSet(flat, x, y, y < 2 ? SiColor{ 7, 8, 9 } : SiColor{ 250, 128, 1 });
		}
	}
	const RgbImage flatHalf = halfSize(flat);
	CPPUNIT_ASSERT_EQUAL(size_t(3), flatHalf.width);
	CPPUNIT_ASSERT_EQUAL(size_t(2), flatHalf.height);
	for(size_t x = 0; x < 3; x++) {
		siAssertNear("the bottom row", { 7, 8, 9 }, siPixel(flatHalf, x, 0), 0);
		siAssertNear("the top row", { 250, 128, 1 }, siPixel(flatHalf, x, 1), 0);
	}
}

void SnapshotImageTest::halfSizeOddTest() {

	// 5x3: 2x1, the last column and row dropped
	RgbImage odd(5, 3);
	for(size_t y = 0; y < 3; y++) {
		for(size_t x = 0; x < 5; x++) {
			const bool dropped = x == 4 || y == 2;
			siSet(odd, x, y, dropped ? SiColor{ 255, 255, 255 } : SiColor{ 100, 50, 25 });
		}
	}
	const RgbImage half = halfSize(odd);
	CPPUNIT_ASSERT_EQUAL(size_t(2), half.width);
	CPPUNIT_ASSERT_EQUAL(size_t(1), half.height);
	siAssertNear("the dropped column and row do not bleed in", { 100, 50, 25 }, siPixel(half, 0, 0), 0);
	siAssertNear("the dropped column and row do not bleed in", { 100, 50, 25 }, siPixel(half, 1, 0), 0);

	CPPUNIT_ASSERT_MESSAGE("1x1 has no half", halfSize(RgbImage(1, 1)).empty());
	CPPUNIT_ASSERT_MESSAGE("8x1 has no half", halfSize(RgbImage(8, 1)).empty());
	CPPUNIT_ASSERT_MESSAGE("no picture, no half", halfSize(RgbImage()).empty());
}

void SnapshotImageTest::jpegOfTheIndexEyeTest() {

	// The Index's eye (about 2016x2240), halved as the snapshot is: 1008x1120
	const RgbImage eye = siQuadrants(2016, 2240);
	const RgbImage half = halfSize(eye);
	CPPUNIT_ASSERT_EQUAL(size_t(1008), half.width);
	CPPUNIT_ASSERT_EQUAL(size_t(1120), half.height);
	siAssertNear("halved: bottom left", SiRed, siPixel(half, 10, 10), 0);
	siAssertNear("halved: bottom right", SiGreen, siPixel(half, 1000, 10), 0);
	siAssertNear("halved: top left", SiBlue, siPixel(half, 10, 1110), 0);
	siAssertNear("halved: top right", SiLight, siPixel(half, 1000, 1110), 0);

	const std::string jpeg = formatJpeg(half);
	CPPUNIT_ASSERT_MESSAGE("a JPEG file: SOI first, EOI last", siIsJpeg(jpeg));
	CPPUNIT_ASSERT_MESSAGE("compressed (" + std::to_string(jpeg.size()) + " bytes)", jpeg.size() < size_t(1008 * 1120 * 3 / 4));

	const SiDecoded decoded = siDecode(jpeg);
	CPPUNIT_ASSERT_MESSAGE("stb_image decodes it", decoded.ok);
	CPPUNIT_ASSERT_EQUAL(1008, decoded.width);
	CPPUNIT_ASSERT_EQUAL(1120, decoded.height);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("colour (3 components)", 3, decoded.components);

	// The right way up: the JPEG's first rows are the picture's top (the eye image's last rows, bottom-up)
	siAssertNear("the JPEG's top left is the picture's top left", SiBlue, decoded.at(252, 280), 10);
	siAssertNear("the JPEG's top right", SiLight, decoded.at(756, 280), 10);
	siAssertNear("the JPEG's bottom left", SiRed, decoded.at(252, 840), 10);
	siAssertNear("the JPEG's bottom right", SiGreen, decoded.at(756, 840), 10);
}

void SnapshotImageTest::jpegOddSizeTest() {

	// 7x5, a grey ramp left to right (no colour: the chroma subsampling does not matter)
	RgbImage ramp(7, 5);
	for(size_t y = 0; y < 5; y++) {
		for(size_t x = 0; x < 7; x++) {
			const int v = int(x) * 40;
			siSet(ramp, x, y, { v, v, v });
		}
	}
	const std::string jpeg = formatJpeg(ramp);
	CPPUNIT_ASSERT(siIsJpeg(jpeg));
	const SiDecoded decoded = siDecode(jpeg);
	CPPUNIT_ASSERT_MESSAGE("stb_image decodes it", decoded.ok);
	CPPUNIT_ASSERT_EQUAL(7, decoded.width);
	CPPUNIT_ASSERT_EQUAL(5, decoded.height);
	siAssertNear("the dark end", { 0, 0, 0 }, decoded.at(0, 2), 24);
	siAssertNear("the bright end", { 240, 240, 240 }, decoded.at(6, 2), 24);
	CPPUNIT_ASSERT_MESSAGE("dark left, bright right", decoded.at(0, 2).r + 100 < decoded.at(6, 2).r);
}

void SnapshotImageTest::jpegQualityTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("snapshots are JPEG quality 85", 85, SnapshotJpegQuality);

	// Noise: the quality shows in the size
	RgbImage noise(64, 64);
	u32 state = 574;
	for(size_t y = 0; y < 64; y++) {
		for(size_t x = 0; x < 64; x++) {
			state = state * 1664525u + 1013904223u;
			siSet(noise, x, y, { int((state >> 8) & 0xff), int((state >> 16) & 0xff), int((state >> 24) & 0xff) });
		}
	}
	const std::string low = formatJpeg(noise, 30);
	const std::string standard = formatJpeg(noise);
	const std::string high = formatJpeg(noise, 95);
	CPPUNIT_ASSERT(siIsJpeg(low) && siIsJpeg(standard) && siIsJpeg(high));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the default is SnapshotJpegQuality", formatJpeg(noise, SnapshotJpegQuality), standard);
	CPPUNIT_ASSERT_MESSAGE("quality 30 is smaller than 85", low.size() < standard.size());
	CPPUNIT_ASSERT_MESSAGE("85 is smaller than 95", standard.size() < high.size());
	CPPUNIT_ASSERT(siDecode(low).ok && siDecode(high).ok);
}

void SnapshotImageTest::jpegEmptyTest() {
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no picture, no JPEG", std::string(), formatJpeg(RgbImage()));
	CPPUNIT_ASSERT_EQUAL(std::string(), formatJpeg(RgbImage(0, 10)));
}
