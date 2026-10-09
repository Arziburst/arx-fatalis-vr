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

#include "vr/VRTurn.h"

#include <algorithm>
#include <cmath>

namespace vr {

const char * turnModeName(TurnMode mode) {
	switch(mode) {
		case TurnMode::Snap: return "snap";
		case TurnMode::Smooth: return "smooth";
	}
	return "?";
}

std::optional<TurnMode> parseTurnMode(std::string_view name) {
	for(TurnMode mode : { TurnMode::Snap, TurnMode::Smooth }) {
		if(name == turnModeName(mode)) {
			return mode;
		}
	}
	return std::nullopt;
}

float TurnController::update(const TurnSettings & settings, float axis, float seconds, bool allowed) {

	const float deflection = std::abs(axis);
	m_updates++;
	m_allowed = allowed;

	if(!allowed) {
		// A stick held through the end of a menu does not snap the moment the menu closes
		m_armed = m_armed && deflection < SnapRearm;
		return 0.f;
	}

	float yaw = 0.f;
	if(settings.mode == TurnMode::Smooth) {
		// Switching to snap with the stick pushed waits for it to come back
		m_armed = deflection < SnapRearm;
		yaw = std::clamp(axis, -1.f, 1.f) * settings.smoothSpeed * std::max(seconds, 0.f);
	} else if(m_armed) {
		if(deflection >= SnapFire) {
			m_armed = false;
			m_lastSnap = (axis > 0.f) ? 1 : -1;
			m_snaps++;
			yaw = float(m_lastSnap) * settings.snapAngle;
		}
	} else if(deflection < SnapRearm) {
		m_armed = true;
	}

	m_turned += yaw;
	return yaw;
}

} // namespace vr
