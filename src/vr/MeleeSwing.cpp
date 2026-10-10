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

#include "vr/MeleeSwing.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace vr {

const char * swingKindName(SwingKind kind) {
	switch(kind) {
		case SwingKind::None: return "none";
		case SwingKind::Slash: return "slash";
		case SwingKind::Stab: return "stab";
	}
	return "none";
}

namespace {

//! The blade's direction: the grip's -Z (#75 lays the blade along the controller's handle)
Vec3f bladeDirection(const glm::quat & orientation) {
	return orientation * Vec3f(0.f, 0.f, -1.f);
}

float median(float a, float b, float c) {
	return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

Vec3f median(const Vec3f & a, const Vec3f & b, const Vec3f & c) {
	return Vec3f(median(a.x, b.x, c.x), median(a.y, b.y, c.y), median(a.z, b.z, c.z));
}

} // anonymous namespace

SwingChange SwingDetector::drop() {
	const bool was = swinging();
	m_kind = SwingKind::None;
	m_seeded = false;
	m_pointCount = 0;
	m_filteredValid = false;
	m_speed = 0.f;
	m_thrust = 0.f;
	m_thrusts.clear();
	return was ? SwingChange::Lost : SwingChange::None;
}

SwingChange SwingDetector::reset() {
	return drop();
}

SwingChange SwingDetector::update(const SwingSample & sample, bool tracked) {

	if(!tracked) {
		return drop();
	}

	const Vec3f blade = bladeDirection(sample.orientation);
	const Vec3f point = sample.hand + blade * m_params.lever;

	if(!m_seeded) {
		m_seeded = true;
		m_lastTime = sample.time;
		m_lastPoint = point;
		m_lastHand = sample.hand;
		m_points[0] = point;
		m_pointCount = 1;
		return SwingChange::None;
	}

	const double step = sample.time - m_lastTime;
	if(step < double(m_params.minStep)) {
		return SwingChange::None; // the same pose again, or too close to measure
	}
	if(step > double(m_params.maxStep)) {
		m_gaps++;
		const SwingChange change = drop();
		update(sample, true); // seed with it
		return change;
	}

	const float seconds = float(step);
	const float raw = glm::length(point - m_lastPoint) / seconds;
	if(raw > m_params.jumpSpeed) {
		m_jumps++;
		const SwingChange change = drop();
		update(sample, true);
		return change;
	}

	// The speed of the median of the last three lever points (F-59's median of three, taken on the
	// positions: a sample that is off and back gives two fast speeds, but its position is never the median)
	m_points[2] = m_points[1];
	m_points[1] = m_points[0];
	m_points[0] = point;
	m_pointCount = std::min<size_t>(m_pointCount + 1, 3);
	if(m_pointCount == 3) {
		const Vec3f filtered = median(m_points[0], m_points[1], m_points[2]);
		m_speed = m_filteredValid ? glm::length(filtered - m_filtered) / seconds : 0.f;
		m_filtered = filtered;
		m_filteredValid = true;
	} else {
		m_speed = 0.f;
	}

	// The thrust along the blade within the stab window (F-61)
	const float along = glm::dot(sample.hand - m_lastHand, blade);
	m_thrusts.push_back(Thrust{ sample.time, along });
	const double since = sample.time - double(m_params.stabWindow);
	m_thrusts.erase(std::remove_if(m_thrusts.begin(), m_thrusts.end(),
	                               [since](const Thrust & t) { return t.time < since; }),
	                m_thrusts.end());
	m_thrust = 0.f;
	for(const Thrust & t : m_thrusts) {
		m_thrust += t.along;
	}
	const float alongSpeed = along / seconds;

	m_lastTime = sample.time;
	m_lastPoint = point;
	m_lastHand = sample.hand;

	if(!swinging()) {
		SwingKind kind = SwingKind::None;
		if(m_speed >= m_params.startSpeed) {
			kind = SwingKind::Slash;
		} else if(m_thrust >= m_params.stabDistance && alongSpeed >= m_params.stabSpeed) {
			kind = SwingKind::Stab;
		}
		if(kind == SwingKind::None) {
			return SwingChange::None;
		}
		m_kind = kind;
		m_peak = std::max(m_speed, alongSpeed);
		m_startTime = sample.time;
		m_swings++;
		return SwingChange::Started;
	}

	m_peak = std::max(m_peak, m_speed);
	const bool slowed = m_speed < m_params.endSpeed;
	const bool tooLong = sample.time - m_startTime > double(m_params.maxDuration);
	if(slowed || tooLong) {
		m_kind = SwingKind::None;
		return SwingChange::Ended;
	}
	return SwingChange::None;
}

float SwingDetector::factor() const {
	const float range = m_params.fullSpeed - m_params.startSpeed;
	const float t = range > 0.f ? glm::clamp((m_peak - m_params.startSpeed) / range, 0.f, 1.f) : 1.f;
	return m_params.minFactor + (1.f - m_params.minFactor) * t;
}

void StrikeCharge::update(bool held, float seconds) {
	if(!held) {
		m_held = false;
		m_spent = false; // released: the next press charges again
		m_seconds = 0.f;
		return;
	}
	if(!m_held) {
		m_held = true;
		m_seconds = 0.f;
	}
	if(!m_spent) {
		m_seconds += std::max(seconds, 0.f);
	}
}

void StrikeCharge::spend() {
	m_spent = true;
	m_seconds = 0.f;
	m_spends++;
}

float StrikeCharge::ratio(float fullSeconds) const {
	if(!charging()) {
		return 0.f;
	}
	if(fullSeconds <= 0.f) {
		return 1.f;
	}
	// The engine's StrikeAimtime: the time held against Full_AimTime, 0.1 to 1
	return glm::clamp(m_seconds / fullSeconds, 0.1f, 1.f);
}

float strikeAim(float chargeRatio, float speedFactor, const SwingParams & params) {
	const float base = chargeRatio > 0.f ? std::max(params.weakAim, chargeRatio) : params.weakAim;
	return base * glm::clamp(speedFactor, 0.f, 1.f);
}

bool TargetCooldown::allowed(size_t target, u64 swing, double time, float cooldown) const {
	for(const Hit & hit : m_hits) {
		if(hit.target != target) {
			continue;
		}
		if(hit.swing == swing || time - hit.time < double(cooldown)) {
			return false;
		}
	}
	return true;
}

void TargetCooldown::hit(size_t target, u64 swing, double time) {
	// Forget hits long past any cooldown, and the target's own older hit
	m_hits.erase(std::remove_if(m_hits.begin(), m_hits.end(), [&](const Hit & h) {
		return h.target == target || time - h.time > 10.;
	}), m_hits.end());
	m_hits.push_back(Hit{ target, swing, time });
}

std::vector<size_t> TargetCooldown::blocked(u64 swing, double time, float cooldown) const {
	std::vector<size_t> result;
	for(const Hit & hit : m_hits) {
		if(!allowed(hit.target, swing, time, cooldown)) {
			result.push_back(hit.target);
		}
	}
	return result;
}

} // namespace vr
