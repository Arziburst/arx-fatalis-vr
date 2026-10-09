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

#include "vr/PhysicalCrouch.h"

#include <algorithm>

namespace vr {

bool updatePhysicalCrouch(bool crouched, float depthMeters, const CrouchParams & params) {

	if(!params.enabled) {
		return false;
	}

	if(crouched) {
		// The exit is never deeper than the entry: a misconfigured pair still has no state that flickers
		return depthMeters >= std::min(params.exitMeters, params.enterMeters);
	}

	return depthMeters >= params.enterMeters;
}

float headDropOnCamera(float headDrop, float characterDrop) {
	return headDrop - std::clamp(characterDrop, 0.f, std::max(headDrop, 0.f));
}

} // namespace vr
