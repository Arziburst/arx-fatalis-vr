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

#include "vr/BodyYaw.h"

#include <cmath>

namespace vr {

float wrapSignedDegrees(float degrees) {
	float wrapped = std::fmod(degrees, 360.f);
	if(wrapped > 180.f) {
		wrapped -= 360.f;
	} else if(wrapped <= -180.f) {
		wrapped += 360.f;
	}
	return wrapped;
}

BodyFollow followHead(float body, float head, float seconds, const BodyFollowParams & params) {

	// The body turns toward the head the short way round
	float gap = wrapSignedDegrees(head - body);

	if(seconds > 0.f) {
		if(params.settleSeconds > 0.f) {
			gap *= std::exp(-3.f * seconds / params.settleSeconds);
		} else {
			gap = 0.f;
		}
	}

	BodyFollow result;
	const float deadZone = std::fmax(params.deadZoneDegrees, 0.f);
	if(gap > deadZone) {
		gap = deadZone;
		result.dragged = true;
	} else if(gap < -deadZone) {
		gap = -deadZone;
		result.dragged = true;
	}

	result.yaw = wrapSignedDegrees(head - gap);
	return result;
}

} // namespace vr
