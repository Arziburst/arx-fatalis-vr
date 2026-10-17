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

#include "vr/MotionProbe.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

float probeWrapSigned(float degrees) {
	float wrapped = std::fmod(degrees, 360.f);
	if(wrapped > 180.f) {
		wrapped -= 360.f;
	} else if(wrapped <= -180.f) {
		wrapped += 360.f;
	}
	return wrapped;
}

//! When the height crossed \a threshold between two frames, assuming it moved evenly between them
double probeCrossing(double t0, double t1, float h0, float h1, float threshold) {
	if(h1 == h0) {
		return t1;
	}
	const double f = std::clamp(double(threshold - h0) / double(h1 - h0), 0.0, 1.0);
	return t0 + f * (t1 - t0);
}

} // anonymous namespace

void MotionProbe::reset() {
	// The last sample stays: the frame after the reset counts from where the player was
	m_walked = 0.f;
	m_turned = 0.f;
	m_seconds = 0.0;
	m_frames = 0;
	m_jump = false;
	m_airborne = false;
	m_peak = 0.f;
	m_jumps = 0;
	m_apex = 0.f;
	m_air = 0.0;
}

void MotionProbe::update(const MotionSample & sample) {

	if(!m_have) {
		m_have = true;
		m_last = sample;
		return;
	}

	m_seconds += std::max(sample.seconds - m_last.seconds, 0.0);
	m_frames++;

	const float step = glm::length(Vec2f(sample.position.x - m_last.position.x, sample.position.z - m_last.position.z));
	const bool teleport = step > TeleportUnits;
	if(!teleport) {
		m_walked += step;
	}
	m_turned += probeWrapSigned(sample.yaw - m_last.yaw);

	if(teleport) {
		m_jump = false;
		m_airborne = false;
	} else {

		if(!m_jump && sample.jumping && !m_last.jumping) {
			// The take-off: the height the player stood at before the jump's first frame
			m_jump = true;
			m_airborne = false;
			m_takeoffY = m_last.position.y;
			m_peak = 0.f;
			m_moveSeconds = sample.seconds;
			m_moveHeight = 0.f;
		}

		if(m_jump) {
			// y points down: up is positive here
			const float now = m_takeoffY - sample.position.y;
			// The engine moves a jumping player in steps of 25-35 ms and the frames between two steps show the
			// last step's position: a crossing is placed between the two moves (when each position was real), not
			// between two frames, one of which only repeats an old position - that would move it by up to a step
			// with the rate's phase (#435)
			const bool moved = sample.position.y != m_last.position.y;
			const float before = m_moveHeight;
			const double beforeSeconds = m_moveSeconds;
			if(moved) {
				m_moveSeconds = sample.seconds;
				m_moveHeight = now;
			}
			m_peak = std::max(m_peak, now);
			if(!m_airborne && now > AirborneUnits) {
				m_airborne = true;
				m_left = probeCrossing(beforeSeconds, sample.seconds, before, now, AirborneUnits);
			} else if(m_airborne && now <= AirborneUnits) {
				m_air = probeCrossing(beforeSeconds, sample.seconds, before, now, AirborneUnits) - m_left;
				m_apex = m_peak;
				m_jumps++;
				m_jump = false;
				m_airborne = false;
			} else if(!m_airborne && !sample.jumping) {
				m_jump = false; // the engine's jump ended without leaving the ground
			}
		}

	}

	m_last = sample;
}

} // namespace vr
