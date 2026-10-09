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

#include "vr/UIPanel.h"

#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

//! Below this horizontal length the view direction has no usable heading (about 11 degrees from a pole)
const float MinHeadingLength = 0.2f;

} // anonymous namespace

Vec3f headHeading(const glm::quat & orientation) {

	const Vec3f forward = orientation * Vec3f(0.f, 0.f, -1.f);
	Vec3f heading(forward.x, 0.f, forward.z);

	if(glm::length(heading) < MinHeadingLength) {
		// Looking down, the top of the head points the way the player faces; looking up, away from it
		const Vec3f up = orientation * Vec3f(0.f, 1.f, 0.f);
		heading = (forward.y < 0.f) ? Vec3f(up.x, 0.f, up.z) : Vec3f(-up.x, 0.f, -up.z);
	}

	const float length = glm::length(heading);
	if(length < 1e-6f) {
		return Vec3f(0.f, 0.f, -1.f);
	}
	return heading / length;
}

PanelPose placePanel(const HeadSample & head, const PanelParams & params) {

	const Vec3f heading = headHeading(head.orientation);

	PanelPose pose;
	pose.position = head.position + heading * params.distance + Vec3f(0.f, -params.drop, 0.f);

	// Turn -z onto the heading, then tilt the face (+z) up towards the eyes
	const float yaw = std::atan2(-heading.x, -heading.z);
	const float tilt = std::atan2(params.drop, params.distance);
	pose.orientation = glm::angleAxis(yaw, Vec3f(0.f, 1.f, 0.f)) * glm::angleAxis(-tilt, Vec3f(1.f, 0.f, 0.f));

	return pose;
}

Vec2f panelSize(float width, const Vec2i & imageSize) {
	if(imageSize.x <= 0 || imageSize.y <= 0) {
		return Vec2f(width, width * 9.f / 16.f);
	}
	return Vec2f(width, width * float(imageSize.y) / float(imageSize.x));
}

bool panelHit(const PanelPose & pose, const Vec2f & size, const Vec3f & origin, const Vec3f & direction,
              Vec2f & uv, float & distance) {

	const Vec3f normal = pose.orientation * Vec3f(0.f, 0.f, 1.f);
	const float along = glm::dot(direction, normal);
	if(along > -1e-6f) {
		// Parallel, or the ray comes from behind the panel
		return false;
	}

	const float t = glm::dot(pose.position - origin, normal) / along;
	if(t <= 0.f) {
		return false;
	}

	const Vec3f local = glm::inverse(pose.orientation) * (origin + direction * t - pose.position);
	const Vec2f hit(local.x / size.x + 0.5f, 0.5f - local.y / size.y);
	if(hit.x < 0.f || hit.x >= 1.f || hit.y < 0.f || hit.y >= 1.f) {
		return false;
	}

	uv = hit;
	distance = t;
	return true;
}

const char * panelModeName(PanelMode mode) {
	switch(mode) {
		case PanelMode::HeadLocked: return "head";
		case PanelMode::World:      return "world";
	}
	return "unknown";
}

const char * panelPlacementName(PanelPlacement placement) {
	switch(placement) {
		case PanelPlacement::None:     return "none";
		case PanelPlacement::Open:     return "open";
		case PanelPlacement::Recenter: return "recenter";
		case PanelPlacement::Tracking: return "tracking";
	}
	return "unknown";
}

PanelPlacement PanelAnchor::update(bool menuOpen, s64 nowUs, const HeadSample * head, const PanelParams & params) {

	if(!menuOpen) {
		if(m_open) {
			m_open = false;
			m_closedAt = nowUs;
		}
		if(m_recenter) {
			// Recentered while closed: the next menu is placed at the head, grace or not
			m_recenter = false;
			m_placed = false;
		}
		m_pending = PanelPlacement::None;
		m_waitedForTracking = false;
		return PanelPlacement::None;
	}

	if(!m_open) {
		m_open = true;
		const bool keep = m_placed && nowUs - m_closedAt <= params.reopenGraceUs;
		if(!keep) {
			m_placed = false;
			m_pending = PanelPlacement::Open;
		}
	}

	if(m_recenter) {
		// The old pose stays shown until the head is tracked to place it anew
		m_recenter = false;
		m_pending = PanelPlacement::Recenter;
	}

	if(m_pending == PanelPlacement::None) {
		return PanelPlacement::None;
	}

	if(!head) {
		m_waitedForTracking = true;
		return PanelPlacement::None;
	}

	const PanelPlacement reason = m_waitedForTracking ? PanelPlacement::Tracking : m_pending;
	m_pose = placePanel(*head, params);
	m_placed = true;
	m_pending = PanelPlacement::None;
	m_waitedForTracking = false;
	m_placements++;

	return reason;
}

} // namespace vr
