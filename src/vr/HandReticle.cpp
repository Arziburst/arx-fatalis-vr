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

#include "vr/HandReticle.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

s32 HoverFilter::update(s32 candidate, double now) {

	if(candidate != None && candidate == m_hovered) {
		m_lastSeen = now;
		m_candidate = None;
		return m_hovered;
	}

	// Something new under the ray: it takes over once it was there for HoverInSeconds in a row - a time, not a
	// count of frames, so a sweep past it is filtered the same at every display rate (#23)
	if(candidate != m_candidate) {
		m_candidate = candidate;
		m_candidateSince = now;
	}
	if(m_candidate != None && now - m_candidateSince >= double(HoverInSeconds)) {
		m_hovered = m_candidate;
		m_lastSeen = now;
		m_candidate = None;
		return m_hovered;
	}

	// The old hover is kept for a moment after the ray left it
	if(m_hovered != None && now - m_lastSeen > double(HoverHoldSeconds)) {
		m_hovered = None;
	}
	return m_hovered;
}

Vec2s worldCursorPixel(const Vec2f & projected) {
	const float limit = float(WorldCursorClamp);
	const float x = std::clamp(projected.x, -limit, limit);
	const float y = std::clamp(projected.y, -limit, limit);
	return Vec2s(s16(WorldCursorBandX + s32(std::lround(x))), s16(std::lround(y)));
}

bool isWorldCursorPixel(const Vec2s & pixel) {
	return s32(pixel.x) >= WorldCursorBandX - WorldCursorClamp;
}

void PickHistory::record(const Vec2s & pixel, s32 entity) {
	Record & record = m_records[m_next];
	record.valid = true;
	record.pixel = pixel;
	record.entity = entity;
	m_next = (m_next + 1) % Size;
}

void PickHistory::press(Hand hand, const Vec2s & pixel, s32 entity) {
	Record & record = m_presses[size_t(hand)];
	record.valid = true;
	record.pixel = pixel;
	record.entity = entity;
	this->record(pixel, entity);
}

std::optional<s32> PickHistory::find(const Vec2s & pixel) const {
	// Newest first
	for(size_t i = 0; i < Size; i++) {
		const Record & record = m_records[(m_next + Size - 1 - i) % Size];
		if(record.valid && record.pixel == pixel) {
			return record.entity;
		}
	}
	for(const Record & record : m_presses) {
		if(record.valid && record.pixel == pixel) {
			return record.entity;
		}
	}
	return std::nullopt;
}

bool reticleQuad(const Vec3f & hit, const Vec3f & eye, float angleDegrees, ReticleQuad & quad) {

	const Vec3f toEye = eye - hit;
	const float distance = glm::length(toEye);
	if(distance < 1e-4f) {
		return false;
	}
	const Vec3f z = toEye / distance;

	// Up as close to the space's up as the face allows; looking straight down any up will do
	Vec3f up(0.f, 1.f, 0.f);
	if(std::abs(glm::dot(up, z)) > 0.999f) {
		up = Vec3f(0.f, 0.f, -1.f);
	}
	const Vec3f x = glm::normalize(glm::cross(up, z));
	const Vec3f y = glm::cross(z, x);

	quad.center = hit;
	quad.orientation = glm::quat_cast(glm::mat3(x, y, z));
	quad.size = 2.f * distance * std::tan(glm::radians(angleDegrees) * 0.5f);
	return true;
}

} // namespace vr
