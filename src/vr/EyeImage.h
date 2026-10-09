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

#ifndef ARX_VR_EYEIMAGE_H
#define ARX_VR_EYEIMAGE_H

#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/MirrorGeometry.h"

/*!
 * \file
 * \brief What the simulated compositor (#112) reads from the eye images: pixel stats, a verdict,
 *        the two-eyes numbers (research row F-06), and the head-locked quad composed into an eye
 *
 * Pure: no OpenGL, no engine globals - covered by a host unit test (tests/vr/EyeImageTest.cpp).
 * Pictures are RGB8 with rows bottom-up (the OpenGL order), sRGB-encoded as the game writes them.
 */
namespace vr {

//! An RGB8 picture, rows bottom-up, with an optional alpha plane (empty = opaque)
struct RgbImage {

	size_t width = 0;
	size_t height = 0;
	std::vector<u8> pixels;
	std::vector<u8> alpha; //!< one byte per pixel when the picture has coverage (the blended quad, #19)

	RgbImage() = default;
	RgbImage(size_t w, size_t h) : width(w), height(h), pixels(w * h * 3, 0) { }

	u8 * at(size_t x, size_t y) { return &pixels[(y * width + x) * 3]; }
	const u8 * at(size_t x, size_t y) const { return &pixels[(y * width + x) * 3]; }
	//! \return the pixel's alpha, 255 for a picture without an alpha plane
	u8 alphaAt(size_t x, size_t y) const { return alpha.empty() ? u8(255) : alpha[y * width + x]; }
	bool empty() const { return width == 0 || height == 0; }

};

//! A part of a picture in fractions of its size, y up (0, 0 = bottom left)
struct ImageRegion {
	float x0 = 0.f;
	float y0 = 0.f;
	float x1 = 1.f;
	float y1 = 1.f;
};

/*!
 * What a region of a picture shows - the answer to "is the world drawn?" (the black-world bug of
 * #215: the head-tracked world was black, only the HUD was drawn, and no flat check saw it)
 */
enum class ImageVerdict {
	Empty,       //!< no picture
	World,       //!< something with detail is drawn
	Black,       //!< (almost) every pixel is black: nothing was drawn, or the scene is missing
	Background,  //!< (almost) every pixel is the eye's background color: the eye got no picture
	Uniform,     //!< one flat color that is neither: a clear color, a fade, a fog wall
	Transparent, //!< a blended picture with (almost) nothing drawn in the region: the world shows through (#19)
};

const char * imageVerdictName(ImageVerdict verdict);

//! Pixel statistics of a region, luma 0-255
struct PixelStats {
	float mean = 0.f;
	float stddev = 0.f;
	float black = 0.f;      //!< fraction of pixels with luma <= BlackLuma
	float background = 0.f; //!< fraction of pixels within BackgroundTolerance of the background color
	ImageVerdict verdict = ImageVerdict::Empty;
};

//! Luma at or below which a pixel counts as black
const float BlackLuma = 4.f;
//! How far (per channel) a pixel may be from the background color and still count as it
const int BackgroundTolerance = 3;
//! Fraction of a region that makes it black / background
const float VerdictFraction = 0.9f;
//! Luma standard deviation below which a region is one flat color
const float UniformStddev = 1.5f;

/*!
 * \param background the eye's background color (the color the eyes are cleared to), RGB8
 */
PixelStats pixelStats(const RgbImage & image, const ImageRegion & region, const u8 background[3]);

//! \return the mean alpha of a region as a fraction 0..1 (1 for a picture without an alpha plane)
float alphaCoverage(const RgbImage & image, const ImageRegion & region);

//! \return the region of the eye image around where the eye looks straight ahead, \a half of the image each way
ImageRegion forwardRegion(const MirrorFov & fov, float half);

//! A small luma picture for comparing frames and eyes
struct LumaGrid {
	size_t width = 0;
	size_t height = 0;
	std::vector<float> values;
};

//! \return the picture shrunk to \a width columns (the aspect kept), each cell the mean luma of its pixels
LumaGrid lumaGrid(const RgbImage & image, size_t width);

//! \return the mean absolute luma difference of two grids of the same size; -1 if the sizes differ
float meanAbsDiff(const LumaGrid & a, const LumaGrid & b);

/*!
 * How far the left eye's picture is shifted to the right of the right eye's: the parallax sign.
 * A correct pair has a positive shift - a thing in front of the viewer appears right of where the
 * left eye looks and left of where the right eye looks, and the asymmetric fields of view add to
 * it; swapped eyes give a negative shift.
 */
struct ShiftEstimate {
	int cells = 0;          //!< the shift in grid columns
	float fraction = 0.f;   //!< the shift as a fraction of the grid width
	float residual = -1.f;  //!< the mean absolute luma difference at that shift
	float unshifted = -1.f; //!< the same without shift
};

/*!
 * \return the shift s in [-maxShift, maxShift] for which left(x) best matches right(x - s), judged
 *         on the overlap of the two (which must keep at least half the width)
 */
ShiftEstimate horizontalShift(const LumaGrid & left, const LumaGrid & right, int maxShift);

//! The head-locked quad layer, in the head (view) space
struct QuadPlacement {
	Vec3f position = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	float width = 0.f;    //!< meters
	float height = 0.f;   //!< meters
	bool blended = false; //!< the layer is blended by its texture's alpha (premultiplied), not opaque
};

/*!
 * \brief Draw the quad layer's picture over an eye picture as the compositor would
 *
 * Each eye pixel is a ray from the eye through its field of view; where the ray meets the quad,
 * the pixel takes the quad's picture (nearest sample) - opaque, or, for a \c blended placement
 * with an alpha plane, premultiplied over the eye (\c XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
 * the 2D interface over the stereo world, #19).
 *
 * \param eyePosition,eyeOrientation the eye in the head (view) space
 * \return the number of eye pixels the quad covers (blended: with any alpha at all)
 */
size_t composeQuad(RgbImage & eye, const MirrorFov & fov, const Vec3f & eyePosition, const glm::quat & eyeOrientation,
                   const RgbImage & quad, const QuadPlacement & placement);

} // namespace vr

#endif // ARX_VR_EYEIMAGE_H
