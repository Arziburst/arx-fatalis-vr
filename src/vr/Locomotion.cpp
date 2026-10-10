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

#include "vr/Locomotion.h"

#include <cmath>

#include "graphics/Math.h"
#include "vr/BodyYaw.h"

namespace vr {

StickWalk stickWalk(Stick move, float yaw) {

	StickWalk walk;

	const float length = std::sqrt(move.x * move.x + move.y * move.y);
	if(!(length > 0.f)) {
		return walk;
	}

	// The same axes as the keys: forward along the yaw, right is the yaw - 90 degrees
	Vec3f direction = angleToVectorXZ(yaw) * (move.y / length) + angleToVectorXZ(yaw - 90.f) * (move.x / length);
	walk.direction = direction / glm::length(direction);

	// A component counts unless the stick is within the sector of the other axis
	const float threshold = std::sin(glm::radians(StickWalkSectorDegrees));
	walk.forward = move.y / length > threshold;
	walk.backward = move.y / length < -threshold;
	walk.right = move.x / length > threshold;
	walk.left = move.x / length < -threshold;

	return walk;
}

float stickWalkYaw(float heading, float lookYaw, float bodyYaw, float bodyOffset) {
	return heading + wrapSignedDegrees(lookYaw - (bodyYaw - bodyOffset));
}

bool StickPacer::update(float deflection, const StickPace & pace) {

	if(!(deflection > 0.f)) {
		m_running = false;
		return false;
	}

	if(m_running ? deflection < pace.sneakBelow : deflection >= pace.runAbove) {
		m_running = !m_running;
	}

	return !m_running;
}

} // namespace vr
