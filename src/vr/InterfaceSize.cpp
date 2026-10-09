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

#include "vr/InterfaceSize.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

//! The tangent span of a field of view, 0 when it is empty or inverted
float tangentSpan(const EyeFov & fov) {
	FovTangents t = tangents(fov);
	float span = t.right - t.left;
	return (std::isfinite(span) && span > 0.f) ? span : 0.f;
}

//! One coordinate: the pixel center \a pos of \a from pixels lands in \a to pixels
s32 mapPixel(s32 pos, s32 from, s32 to) {
	if(from <= 0 || to <= 0) {
		return pos;
	}
	double mapped = std::floor((double(pos) + 0.5) * double(to) / double(from));
	return s32(std::max(0.0, std::min(mapped, double(to - 1))));
}

} // anonymous namespace

Vec2i interfaceResolution(const Vec2i & eyeSize, const EyeFov & fov, float quadWidth, float quadDistance,
                          const Vec2i & windowSize, const Vec2i & maxSize) {

	float span = tangentSpan(fov);
	if(windowSize.x <= 0 || windowSize.y <= 0 || eyeSize.x <= 0 || span <= 0.f
	   || !(quadWidth > 0.f) || !(quadDistance > 0.f)) {
		return windowSize;
	}

	const double aspect = double(windowSize.y) / double(windowSize.x);
	double width = double(eyeSize.x) / double(span) * double(quadWidth) / double(quadDistance);
	width = std::max(double(MinInterfaceWidth), std::min(width, double(MaxInterfaceWidth)));
	if(maxSize.x > 0) {
		width = std::min(width, double(maxSize.x));
	}
	if(maxSize.y > 0 && width * aspect > double(maxSize.y)) {
		width = double(maxSize.y) / aspect;
	}

	s32 w = std::max(s32(1), s32(std::lround(width)));
	s32 h = std::max(s32(1), s32(std::lround(width * aspect)));
	if(maxSize.y > 0) {
		h = std::min(h, maxSize.y);
	}
	return Vec2i(w, h);
}

float eyePixelsPerDegree(s32 eyeWidth, const EyeFov & fov) {
	float span = tangentSpan(fov);
	if(eyeWidth <= 0 || span <= 0.f) {
		return 0.f;
	}
	// Straight ahead one degree is tan(1 deg) of the tangent span
	return float(eyeWidth) / span * std::tan(glm::radians(1.f));
}

Vec2i windowToInterface(const Vec2i & pos, const Vec2i & windowSize, const Vec2i & interfaceSize) {
	return Vec2i(mapPixel(pos.x, windowSize.x, interfaceSize.x), mapPixel(pos.y, windowSize.y, interfaceSize.y));
}

Vec2i interfaceToWindow(const Vec2i & pos, const Vec2i & windowSize, const Vec2i & interfaceSize) {
	return Vec2i(mapPixel(pos.x, interfaceSize.x, windowSize.x), mapPixel(pos.y, interfaceSize.y, windowSize.y));
}

} // namespace vr
