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

#include "vr/StereoEffects.h"

#include <cmath>

#include <glm/glm.hpp>

namespace vr {

float spriteHalfSize(float size, float depth, float focal) {

	if(size < 0.f) {
		// A fixed share of the image: so many pixels per focal at any depth
		return -size / focal * depth;
	}

	// The engine's pixels at this depth, times depth / ppu: siz * (3000 / z - 1) * k * z
	float half = size * (3000.f - depth) * (350.f / 310.f * 0.001f);
	// Past depth 3000 the engine draws a speck, not a sprite turned inside out
	return half > 0.f ? half : 0.00000001f * depth;
}

FacingAxes facingAxes(const Vec3f & center) {

	FacingAxes axes;

	const float length = glm::length(center);
	if(!(length > 0.f)) {
		return axes;
	}
	const Vec3f facing = center / length;

	// Level: perpendicular to the head's down axis and the line of sight (x right, y down, z forward
	// is right-handed: down x forward = right)
	const Vec3f right = glm::cross(Vec3f(0.f, 1.f, 0.f), facing);
	const float rightLength = glm::length(right);
	if(rightLength < 1e-4f) {
		// Straight above or below the head: no level direction, keep the head's right
		axes.down = glm::normalize(glm::cross(facing, axes.right));
		return axes;
	}

	axes.right = right / rightLength;
	axes.down = glm::cross(facing, axes.right);
	return axes;
}

std::array<Vec3f, 4> overlayCornerDirections(const EyeFov & fov) {
	const FovTangents t = tangents(fov);
	// Engine y is down: the top edge is at -up
	return {
		Vec3f(t.left, -t.up, 1.f),
		Vec3f(t.right, -t.up, 1.f),
		Vec3f(t.right, -t.down, 1.f),
		Vec3f(t.left, -t.down, 1.f),
	};
}

} // namespace vr
