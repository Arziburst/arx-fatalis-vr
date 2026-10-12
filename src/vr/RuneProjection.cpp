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

#include "vr/RuneProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

#include "graphics/Math.h"

namespace vr {

RunePlane makeRunePlane(float lookYaw, const Vec3f & firstPoint, float pixelsPerUnit) {
	RunePlane plane;
	plane.yaw = wrapDegrees(lookYaw);
	plane.origin = firstPoint;
	plane.pixelsPerUnit = pixelsPerUnit;
	return plane;
}

Vec3f runePlaneRight(float yaw) {
	// The engine's forward of a yaw is (-sin, 0, cos): its right is +x at yaw 0, turned the same way
	return turnAboutVertical(Vec3f(1.f, 0.f, 0.f), yaw);
}

Vec2f projectOntoRunePlane(const RunePlane & plane, const Vec3f & point) {
	// The depth (along the look) is dropped; the engine's y is down, as the screen's
	const Vec3f d = point - plane.origin;
	return Vec2f(glm::dot(d, runePlaneRight(plane.yaw)), d.y) * plane.pixelsPerUnit;
}

void RuneSampler::begin(const RunePlane & plane, float stepPixels) {
	m_plane = plane;
	m_step = stepPixels;
	m_points.clear();
	m_last = Vec2f(0.f);
	m_min2 = m_max2 = Vec2f(0.f);
	m_min3 = m_max3 = plane.origin;
	m_fed = 0;
}

size_t RuneSampler::add(const Vec3f & point) {

	const Vec2f target = projectOntoRunePlane(m_plane, point);
	if(m_fed == 0) {
		m_min3 = m_max3 = point;
		m_min2 = m_max2 = target;
	} else {
		m_min3 = glm::min(m_min3, point);
		m_max3 = glm::max(m_max3, point);
		m_min2 = glm::min(m_min2, target);
		m_max2 = glm::max(m_max2, target);
	}
	m_fed++;

	size_t emitted = 0;
	auto emit = [this, &emitted](const Vec2f & p) {
		const float limit = float(std::numeric_limits<s16>::max());
		m_points.emplace_back(s16(std::lround(glm::clamp(p.x, -limit, limit))),
		                      s16(std::lround(glm::clamp(p.y, -limit, limit))));
		emitted++;
	};

	if(m_points.empty()) {
		m_last = target;
		emit(target);
		return emitted;
	}

	if(!(m_step > 0.f)) {
		return 0;
	}

	// Chase the hand a step at a time: each new point a step from the last one towards the hand
	for(float distance = glm::distance(m_last, target); distance >= m_step; distance = glm::distance(m_last, target)) {
		m_last += (target - m_last) * (m_step / distance);
		emit(m_last);
	}

	return emitted;
}

Vec3f RuneSampler::centre() const {
	return (m_min3 + m_max3) * 0.5f;
}

Vec2f RuneSampler::size() const {
	return m_max2 - m_min2;
}

const char * runeGateName(RuneGate gate) {
	switch(gate) {
		case RuneGate::None: return "none";
		case RuneGate::Ok: return "ok";
		case RuneGate::Behind: return "behind";
		case RuneGate::Low: return "low";
		case RuneGate::High: return "high";
	}
	return "?";
}

RuneGateResult gateRuneStroke(const Vec3f & headCamera, float lookYaw, const Vec3f & centre,
                              const RuneGateParams & params) {

	RuneGateResult result;

	const Vec3f d = centre - headCamera;

	// The engine's y is down: a positive distance is below the head
	result.belowHeadMeters = d.y / params.unitsPerMeter;

	// The centre's heading from the head against the look's; a centre right at the head has none
	const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
	if(horizontal > 0.05f * params.unitsPerMeter) {
		const float yaw = glm::degrees(std::atan2(-d.x, d.z)); // the engine's yaw of a direction
		float off = wrapDegrees(yaw - lookYaw);
		if(off > 180.f) {
			off -= 360.f;
		}
		result.offYawDegrees = off;
	}

	if(std::abs(result.offYawDegrees) > params.yawDegrees) {
		result.gate = RuneGate::Behind;
	} else if(result.belowHeadMeters > params.belowHeadMeters) {
		result.gate = RuneGate::Low;
	} else if(-result.belowHeadMeters > params.aboveHeadMeters) {
		result.gate = RuneGate::High;
	} else {
		result.gate = RuneGate::Ok;
	}

	return result;
}

Anglef aimFromPlacement(const HandPlacement & hand) {
	// The grip's forward (OpenXR -Z) is +Z in the engine axes the placement turns into the world - the
	// axis the trail's tip is on (runeTrailPoint)
	const Vec3f forward = glm::normalize(hand.orientation * Vec3f(0.f, 0.f, 1.f));
	const Anglef angle = unitVectorToAngle(forward);
	return Anglef(glm::clamp(angle.getPitch(), -90.f, 90.f), wrapDegrees(angle.getYaw()), 0.f);
}

} // namespace vr
