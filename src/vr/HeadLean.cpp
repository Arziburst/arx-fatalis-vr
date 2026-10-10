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

#include "vr/HeadLean.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

HeadLean leanHead(const Vec2f & moved, const Vec2f & anchor, const LeanParams & params) {

	HeadLean result;
	result.lean = moved - anchor;
	result.distance = glm::length(result.lean);

	const float radius = std::max(params.followRadius, 0.f);
	if(result.distance > radius) {
		result.step = result.lean * ((result.distance - radius) / result.distance);
	}

	if(params.farFull <= params.farStart) {
		result.farFade = (result.distance >= params.farStart) ? 1.f : 0.f;
	} else {
		result.farFade = glm::clamp((result.distance - params.farStart) / (params.farFull - params.farStart), 0.f, 1.f);
	}

	return result;
}

Vec2f turnToWorld(const Vec2f & v, float turnDegrees) {
	// Vec2f(x, z): the view-to-world rotation Ry(-turn) of headToCamera
	const float t = glm::radians(turnDegrees);
	const float c = std::cos(t);
	const float s = std::sin(t);
	return Vec2f(c * v.x - s * v.y, s * v.x + c * v.y);
}

Vec2f turnFromWorld(const Vec2f & v, float turnDegrees) {
	return turnToWorld(v, -turnDegrees);
}

Vec2f anchorAfterStep(const Vec2f & anchor, const Vec2f & movedWorld, float turnDegrees, float unitsPerMeter) {
	return anchor + turnFromWorld(movedWorld, turnDegrees) / unitsPerMeter;
}

float distanceToTriangle(const Vec3f & p, const Vec3f & a, const Vec3f & b, const Vec3f & c) {

	// The closest point by the triangle's Voronoi regions (Ericson, Real-Time Collision Detection, 5.1.5)
	const Vec3f ab = b - a;
	const Vec3f ac = c - a;
	const Vec3f ap = p - a;
	const float d1 = glm::dot(ab, ap);
	const float d2 = glm::dot(ac, ap);
	if(d1 <= 0.f && d2 <= 0.f) {
		return glm::distance(p, a);
	}

	const Vec3f bp = p - b;
	const float d3 = glm::dot(ab, bp);
	const float d4 = glm::dot(ac, bp);
	if(d3 >= 0.f && d4 <= d3) {
		return glm::distance(p, b);
	}

	const float vc = d1 * d4 - d3 * d2;
	if(vc <= 0.f && d1 >= 0.f && d3 <= 0.f && d1 - d3 > 0.f) {
		return glm::distance(p, a + ab * (d1 / (d1 - d3)));
	}

	const Vec3f cp = p - c;
	const float d5 = glm::dot(ab, cp);
	const float d6 = glm::dot(ac, cp);
	if(d6 >= 0.f && d5 <= d6) {
		return glm::distance(p, c);
	}

	const float vb = d5 * d2 - d1 * d6;
	if(vb <= 0.f && d2 >= 0.f && d6 <= 0.f && d2 - d6 > 0.f) {
		return glm::distance(p, a + ac * (d2 / (d2 - d6)));
	}

	const float va = d3 * d6 - d5 * d4;
	if(va <= 0.f && d4 - d3 >= 0.f && d5 - d6 >= 0.f && (d4 - d3) + (d5 - d6) > 0.f) {
		return glm::distance(p, b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))));
	}

	const float sum = va + vb + vc;
	if(sum <= 0.f) {
		// Degenerate (a line or a point): the nearest corner is close enough for a wall check
		return std::min(glm::distance(p, a), std::min(glm::distance(p, b), glm::distance(p, c)));
	}
	const float v = vb / sum;
	const float w = vc / sum;
	return glm::distance(p, a + ab * v + ac * w);
}

float stepFade(float current, float target, float seconds, const FadeParams & params) {

	target = glm::clamp(target, 0.f, 1.f);
	if(seconds <= 0.f) {
		return glm::clamp(current, 0.f, 1.f);
	}

	const float duration = (target > current) ? params.outSeconds : params.inSeconds;
	if(duration <= 0.f) {
		return target;
	}

	const float change = seconds / duration;
	if(target > current) {
		return std::min(current + change, target);
	}
	return std::max(current - change, target);
}

} // namespace vr
