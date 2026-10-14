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

#include "vr/BodyView.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

namespace vr {

float pushForPitch(float pitchDownDegrees, const BodyViewParams & params) {
	const float down = glm::clamp(pitchDownDegrees, 0.f, 90.f);
	return params.push + params.pushDown * std::sin(glm::radians(down));
}

Vec3f neckPivot(const Vec3f & eye, float yawDegrees, const BodyViewParams & params) {
	const float yaw = glm::radians(yawDegrees);
	const Vec3f forward(-std::sin(yaw), 0.f, std::cos(yaw));
	return eye + Vec3f(0.f, params.neckBelowEye, 0.f) - forward * params.push;
}

Vec3f bodyOffset(const Vec3f & eye, float yawDegrees, const Vec3f & modelNeck, const BodyViewParams & params) {
	return neckPivot(eye, yawDegrees, params) - modelNeck;
}

bool faceBelowKeepLine(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight,
                       const BodyViewParams & params) {
	// Engine y is down: the highest corner has the smallest y
	const float eyes = 0.5f * (eyeLeft.y + eyeRight.y);
	return std::min({ a.y, b.y, c.y }) - eyes > params.keepBelow;
}

float faceDistanceToEyes(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight) {
	float nearest = std::numeric_limits<float>::infinity();
	for(const Vec3f * corner : { &a, &b, &c }) {
		nearest = std::min({ nearest, glm::distance(*corner, eyeLeft), glm::distance(*corner, eyeRight) });
	}
	return nearest;
}

bool hideFaceNearEye(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight,
                     const BodyViewParams & params) {
	return faceDistanceToEyes(a, b, c, eyeLeft, eyeRight) <= params.clearance
	       && !faceBelowKeepLine(a, b, c, eyeLeft, eyeRight, params);
}

} // namespace vr
