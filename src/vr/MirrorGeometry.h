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

#ifndef ARX_VR_MIRRORGEOMETRY_H
#define ARX_VR_MIRRORGEOMETRY_H

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"

/*!
 * \file
 * \brief Where the desktop mirror (#21, research row F-24) takes its picture from
 *
 * Pure geometry: no OpenXR, no OpenGL, no engine globals, so it is covered by a host unit test
 * (tests/vr/MirrorGeometryTest.cpp). The mirror shows what the left eye sees - its swapchain image,
 * cropped to the window's aspect, with the head-locked flat frame quad drawn where it appears in
 * that eye. Image coordinates are OpenGL texture coordinates: u to the right, v up, 0..1.
 */
namespace vr {

//! Field of view half-angles in radians, OpenXR convention (left and down are negative)
struct MirrorFov {
	float left = 0.f;
	float right = 0.f;
	float up = 0.f;
	float down = 0.f;
};

//! A rectangle of the eye image in texture coordinates
struct MirrorRect {
	float u0 = 0.f;
	float v0 = 0.f;
	float u1 = 1.f;
	float v1 = 1.f;
};

/*!
 * \return where the eye looks straight ahead in its image: OpenXR eye fields of view are
 *         asymmetric (more to the outside than to the nose), so this is not the image center
 */
inline Vec2f mirrorForwardPoint(const MirrorFov & fov) {
	float tanLeft = std::tan(fov.left), tanRight = std::tan(fov.right);
	float tanDown = std::tan(fov.down), tanUp = std::tan(fov.up);
	return Vec2f(-tanLeft / (tanRight - tanLeft), -tanDown / (tanUp - tanDown));
}

/*!
 * \brief The part of the eye image the window shows
 *
 * The window is filled without stretching: the eye image (taller than a 16:9 window) is cropped to
 * the window's aspect, centered on the eye's forward point so the view is centered on where the
 * player looks; the crop is shifted back inside the image where centering would leave it.
 *
 * \param eyeSize    size of the eye image in pixels
 * \param windowSize size of the window in pixels
 */
inline MirrorRect mirrorCrop(const MirrorFov & fov, const Vec2i & eyeSize, const Vec2i & windowSize) {

	MirrorRect crop;
	if(eyeSize.x <= 0 || eyeSize.y <= 0 || windowSize.x <= 0 || windowSize.y <= 0) {
		return crop;
	}

	float eyeAspect = float(eyeSize.x) / float(eyeSize.y);
	float windowAspect = float(windowSize.x) / float(windowSize.y);
	Vec2f forward = mirrorForwardPoint(fov);

	// One axis keeps the whole image, the other is cut to the window's aspect
	Vec2f extent(1.f);
	if(windowAspect > eyeAspect) {
		extent.y = eyeAspect / windowAspect;
	} else {
		extent.x = windowAspect / eyeAspect;
	}

	float u0 = std::clamp(forward.x - extent.x * 0.5f, 0.f, 1.f - extent.x);
	float v0 = std::clamp(forward.y - extent.y * 0.5f, 0.f, 1.f - extent.y);
	crop.u0 = u0;
	crop.v0 = v0;
	crop.u1 = u0 + extent.x;
	crop.v1 = v0 + extent.y;
	return crop;
}

/*!
 * \brief Project a point into an eye image
 *
 * \param point          the point, in the space the eye pose is given in (meters)
 * \param eyePosition    the eye's position in that space
 * \param eyeOrientation the eye's orientation in that space (looking down -z, y up)
 * \param uv             receives the texture coordinate of the point in the eye image (may lie
 *                       outside 0..1: outside the field of view)
 * \param depth          receives the distance in front of the eye along its view axis
 *
 * \return false if the point is not in front of the eye
 */
inline bool mirrorProject(const Vec3f & point, const Vec3f & eyePosition, const glm::quat & eyeOrientation,
                          const MirrorFov & fov, Vec2f & uv, float & depth) {

	Vec3f p = glm::inverse(eyeOrientation) * (point - eyePosition);
	if(p.z >= -1e-4f) {
		return false;
	}

	depth = -p.z;
	float tanLeft = std::tan(fov.left), tanRight = std::tan(fov.right);
	float tanDown = std::tan(fov.down), tanUp = std::tan(fov.up);
	uv.x = (p.x / depth - tanLeft) / (tanRight - tanLeft);
	uv.y = (p.y / depth - tanDown) / (tanUp - tanDown);
	return true;
}

//! \return \a uv of the eye image as a window position, -1..1 (OpenGL normalized device coordinates)
inline Vec2f mirrorToWindow(const MirrorRect & crop, const Vec2f & uv) {
	return Vec2f((uv.x - crop.u0) / (crop.u1 - crop.u0) * 2.f - 1.f,
	             (uv.y - crop.v0) / (crop.v1 - crop.v0) * 2.f - 1.f);
}

} // namespace vr

#endif // ARX_VR_MIRRORGEOMETRY_H
