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

#include "vr/EyeImageTest.h"

#include <cmath>
#include <string>

CppUnit::AutoRegisterSuite<EyeImageTest> g_registerEyeImageTest;

namespace {

const double ImageEpsilon = 1e-4;
const u8 ImageBackground[3] = { 38, 38, 38 };
const vr::ImageRegion WholeImage;

vr::RgbImage filledImage(size_t width, size_t height, u8 r, u8 g, u8 b) {
	vr::RgbImage image(width, height);
	for(size_t i = 0; i < width * height; i++) {
		image.pixels[i * 3] = r;
		image.pixels[i * 3 + 1] = g;
		image.pixels[i * 3 + 2] = b;
	}
	return image;
}

void setImagePixel(vr::RgbImage & image, size_t x, size_t y, u8 value) {
	u8 * p = image.at(x, y);
	p[0] = p[1] = p[2] = value;
}

//! Vertical stripes of varying width and brightness: detail that matches only at one shift
vr::RgbImage stripeImage(size_t width, size_t height, int offset) {
	vr::RgbImage image(width, height);
	for(size_t y = 0; y < height; y++) {
		for(size_t x = 0; x < width; x++) {
			long column = long(x) - offset;
			u8 value = u8((column * column * 7 + column * 13) % 251);
			setImagePixel(image, x, y, value);
		}
	}
	return image;
}

vr::MirrorFov imageFov(float left, float right, float up, float down) {
	vr::MirrorFov result;
	result.left = left;
	result.right = right;
	result.up = up;
	result.down = down;
	return result;
}

const glm::quat ImageAhead(1.f, 0.f, 0.f, 0.f);

//! The head-locked flat frame quad as VRSystem submits it: 2.4 m wide, 1.75 m ahead
vr::QuadPlacement flatFrameQuad() {
	vr::QuadPlacement quad;
	quad.position = Vec3f(0.f, 0.f, -1.75f);
	quad.width = 2.4f;
	quad.height = 1.35f;
	return quad;
}

} // anonymous namespace

void EyeImageTest::testBlackPictureIsBlack() {
	vr::PixelStats stats = vr::pixelStats(filledImage(40, 30, 0, 0, 0), WholeImage, ImageBackground);
	CPPUNIT_ASSERT(stats.verdict == vr::ImageVerdict::Black);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, stats.black, ImageEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, stats.mean, ImageEpsilon);
}

void EyeImageTest::testBackgroundPictureIsBackground() {
	vr::PixelStats stats = vr::pixelStats(filledImage(40, 30, 39, 37, 38), WholeImage, ImageBackground);
	CPPUNIT_ASSERT(stats.verdict == vr::ImageVerdict::Background);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, stats.background, ImageEpsilon);
}

void EyeImageTest::testFlatColorIsUniform() {
	vr::PixelStats stats = vr::pixelStats(filledImage(40, 30, 120, 60, 200), WholeImage, ImageBackground);
	CPPUNIT_ASSERT(stats.verdict == vr::ImageVerdict::Uniform);
	CPPUNIT_ASSERT(stats.stddev < 0.01f);
}

void EyeImageTest::testDetailIsWorld() {
	vr::PixelStats stats = vr::pixelStats(stripeImage(64, 48, 0), WholeImage, ImageBackground);
	CPPUNIT_ASSERT_EQUAL(std::string("world"), std::string(vr::imageVerdictName(stats.verdict)));
	// A dark scene is still a scene: mostly dim pixels with some detail
	vr::RgbImage dim = filledImage(64, 48, 6, 6, 6);
	for(size_t x = 0; x < 64; x += 4) {
		setImagePixel(dim, x, 20, 40);
	}
	CPPUNIT_ASSERT(vr::pixelStats(dim, WholeImage, ImageBackground).verdict == vr::ImageVerdict::World);
}

void EyeImageTest::testHudAroundABlackMiddleIsBlack() {
	// The bug of #215: the HUD at the edges, nothing where the world should be
	vr::RgbImage image = filledImage(100, 100, 0, 0, 0);
	for(size_t y = 0; y < 100; y++) {
		for(size_t x = 0; x < 100; x++) {
			if(y < 20 || x < 10 || x >= 90) {
				setImagePixel(image, x, y, u8(100 + (x * 7 + y * 3) % 100));
			}
		}
	}
	vr::ImageRegion middle = { 0.3f, 0.35f, 0.7f, 0.75f };
	CPPUNIT_ASSERT(vr::pixelStats(image, middle, ImageBackground).verdict == vr::ImageVerdict::Black);
	CPPUNIT_ASSERT(vr::pixelStats(image, WholeImage, ImageBackground).verdict != vr::ImageVerdict::Black);
}

void EyeImageTest::testEmptyPicture() {
	CPPUNIT_ASSERT(vr::pixelStats(vr::RgbImage(), WholeImage, ImageBackground).verdict == vr::ImageVerdict::Empty);
	vr::ImageRegion nothing = { 0.5f, 0.5f, 0.5f, 0.5f };
	CPPUNIT_ASSERT(vr::pixelStats(filledImage(10, 10, 1, 1, 1), nothing, ImageBackground).verdict == vr::ImageVerdict::Empty);
}

void EyeImageTest::testGridKeepsTheAspect() {
	vr::LumaGrid grid = vr::lumaGrid(filledImage(320, 160, 255, 255, 255), 80);
	CPPUNIT_ASSERT_EQUAL(size_t(80), grid.width);
	CPPUNIT_ASSERT_EQUAL(size_t(40), grid.height);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(255.0, grid.values[0], 0.01);
}

void EyeImageTest::testMeanAbsDiff() {
	vr::LumaGrid a = vr::lumaGrid(filledImage(20, 20, 10, 10, 10), 20);
	vr::LumaGrid b = vr::lumaGrid(filledImage(20, 20, 30, 30, 30), 20);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, vr::meanAbsDiff(a, a), 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, vr::meanAbsDiff(a, b), 0.01);
	vr::LumaGrid other = vr::lumaGrid(filledImage(10, 20, 10, 10, 10), 10);
	CPPUNIT_ASSERT(vr::meanAbsDiff(a, other) < 0.f);
}

void EyeImageTest::testShiftFindsTheOffset() {
	// The left picture is the right one moved 5 columns to the right
	vr::LumaGrid left = vr::lumaGrid(stripeImage(80, 20, 5), 80);
	vr::LumaGrid right = vr::lumaGrid(stripeImage(80, 20, 0), 80);
	vr::ShiftEstimate shift = vr::horizontalShift(left, right, 20);
	CPPUNIT_ASSERT_EQUAL(5, shift.cells);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, shift.residual, 0.01);
	CPPUNIT_ASSERT(shift.unshifted > 1.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0 / 80.0, shift.fraction, ImageEpsilon);
}

void EyeImageTest::testSwappedPicturesShiftTheOtherWay() {
	vr::LumaGrid left = vr::lumaGrid(stripeImage(80, 20, 5), 80);
	vr::LumaGrid right = vr::lumaGrid(stripeImage(80, 20, 0), 80);
	CPPUNIT_ASSERT_EQUAL(-5, vr::horizontalShift(right, left, 20).cells);
}

void EyeImageTest::testQuadFillsTheMiddleOfAnEye() {
	vr::RgbImage eye = filledImage(100, 100, 38, 38, 38);
	vr::RgbImage quad = filledImage(16, 9, 200, 10, 10);
	vr::MirrorFov symmetric = imageFov(-0.8f, 0.8f, 0.8f, -0.8f);
	size_t covered = vr::composeQuad(eye, symmetric, Vec3f(0.f), ImageAhead, quad, flatFrameQuad());
	CPPUNIT_ASSERT(covered > 0);
	CPPUNIT_ASSERT_EQUAL(200, int(eye.at(50, 50)[0]));
	CPPUNIT_ASSERT_EQUAL(38, int(eye.at(0, 0)[0]));
	CPPUNIT_ASSERT_EQUAL(38, int(eye.at(99, 99)[0]));
	// tan(0.8) = 1.03: the quad's half width 1.2 at 1.75 m is tan 0.686 - about 2/3 of the width
	size_t row = 0;
	for(size_t x = 0; x < 100; x++) {
		row += eye.at(x, 50)[0] == 200 ? 1 : 0;
	}
	CPPUNIT_ASSERT(row > 60 && row < 70);
}

void EyeImageTest::testQuadBehindTheEyeIsNotDrawn() {
	vr::RgbImage eye = filledImage(50, 50, 38, 38, 38);
	vr::QuadPlacement behind = flatFrameQuad();
	behind.position.z = 1.75f;
	CPPUNIT_ASSERT_EQUAL(size_t(0), vr::composeQuad(eye, imageFov(-0.8f, 0.8f, 0.8f, -0.8f), Vec3f(0.f), ImageAhead,
	                                                filledImage(4, 4, 255, 255, 255), behind));
}

void EyeImageTest::testQuadAppearsRightOfTheLeftEyesForward() {
	// The left eye sits left of the quad's center: the quad's middle appears right of where it looks
	vr::RgbImage quad(2, 1);
	setImagePixel(quad, 0, 0, 100);
	setImagePixel(quad, 1, 0, 200);
	vr::RgbImage eye = filledImage(200, 100, 0, 0, 0);
	vr::MirrorFov symmetric = imageFov(-0.8f, 0.8f, 0.8f, -0.8f);
	vr::composeQuad(eye, symmetric, Vec3f(-0.032f, 0.f, 0.f), ImageAhead, quad, flatFrameQuad());
	size_t edge = 0;
	for(size_t x = 1; x < 200; x++) {
		if(eye.at(x - 1, 50)[0] == 100 && eye.at(x, 50)[0] == 200) {
			edge = x;
		}
	}
	CPPUNIT_ASSERT(edge > 100);
}

void EyeImageTest::testComposedEyesHaveAPositiveShift() {
	// Both eyes of the default simulated headset (F-165 angles) looking at the flat frame quad
	vr::RgbImage quad = stripeImage(320, 180, 0);
	vr::RgbImage left = filledImage(320, 320, 38, 38, 38);
	vr::RgbImage right = filledImage(320, 320, 38, 38, 38);
	vr::composeQuad(left, imageFov(-0.86f, 0.78f, 0.8f, -0.8f), Vec3f(-0.032f, 0.f, 0.f), ImageAhead, quad, flatFrameQuad());
	vr::composeQuad(right, imageFov(-0.78f, 0.86f, 0.8f, -0.8f), Vec3f(0.032f, 0.f, 0.f), ImageAhead, quad, flatFrameQuad());
	vr::LumaGrid l = vr::lumaGrid(left, 160);
	vr::LumaGrid r = vr::lumaGrid(right, 160);
	CPPUNIT_ASSERT(vr::meanAbsDiff(l, r) > 1.f);
	vr::ShiftEstimate shift = vr::horizontalShift(l, r, 40);
	CPPUNIT_ASSERT(shift.cells > 0);
	CPPUNIT_ASSERT(vr::horizontalShift(r, l, 40).cells < 0);
}

void EyeImageTest::testBlendedQuadShowsTheEyeThrough() {

	// The 2D interface on the quad (#19): premultiplied color with coverage in the alpha plane - a
	// transparent middle leaves the eye as it is, an opaque pixel replaces it, a half-covered one mixes
	vr::RgbImage quad = filledImage(16, 9, 200, 10, 10);
	quad.alpha.assign(16 * 9, 0);
	quad.alpha[4 * 16 + 2] = 255;                  // one opaque pixel, lower left of the middle
	quad.alpha[4 * 16 + 13] = 128;                 // one half-covered pixel, right of the middle
	for(size_t c = 0; c < 3; c++) {
		quad.at(13, 4)[c] = u8(quad.at(13, 4)[c] / 2); // premultiplied by its alpha
	}
	vr::QuadPlacement blended = flatFrameQuad();
	blended.blended = true;
	vr::MirrorFov symmetric = imageFov(-0.8f, 0.8f, 0.8f, -0.8f);

	vr::RgbImage eye = filledImage(100, 100, 38, 38, 38);
	size_t covered = vr::composeQuad(eye, symmetric, Vec3f(0.f), ImageAhead, quad, blended);
	CPPUNIT_ASSERT(covered > 0);
	// The middle of the eye still shows the eye, not the quad's red
	CPPUNIT_ASSERT_EQUAL(38, int(eye.at(50, 50)[0]));
	// Somewhere left of the middle the opaque pixel replaced the eye; right of it the half pixel mixed
	bool opaque = false, mixed = false;
	for(size_t y = 0; y < 100; y++) {
		for(size_t x = 0; x < 100; x++) {
			const u8 * p = eye.at(x, y);
			if(p[0] == 200 && p[1] == 10) {
				opaque = true;
			}
			if(p[0] == 100 + 19 && p[1] == 5 + 19) {
				mixed = true;
			}
		}
	}
	CPPUNIT_ASSERT(opaque);
	CPPUNIT_ASSERT(mixed);

	// The same quad drawn opaque paints the eye's middle red: the flag is what the compositor honours
	vr::RgbImage opaqueEye = filledImage(100, 100, 38, 38, 38);
	vr::composeQuad(opaqueEye, symmetric, Vec3f(0.f), ImageAhead, quad, flatFrameQuad());
	CPPUNIT_ASSERT_EQUAL(200, int(opaqueEye.at(50, 50)[0]));

	// Coverage: the quad is almost all transparent; a picture without an alpha plane is opaque
	CPPUNIT_ASSERT(vr::alphaCoverage(quad, vr::ImageRegion()) < 0.02f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, vr::alphaCoverage(eye, vr::ImageRegion()), 1e-6);
	CPPUNIT_ASSERT_EQUAL(std::string("transparent"), std::string(vr::imageVerdictName(vr::ImageVerdict::Transparent)));
}
