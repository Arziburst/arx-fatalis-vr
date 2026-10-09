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

#include "vr/MenuPointer.h"

#include <glm/glm.hpp>

namespace vr {

namespace {

Hand otherHand(Hand hand) {
	return hand == Hand::Left ? Hand::Right : Hand::Left;
}

} // anonymous namespace

bool beamQuad(const Vec3f & from, const Vec3f & to, const Vec3f & eye, float width, BeamQuad & quad) {

	const Vec3f along = to - from;
	const float length = glm::length(along);
	if(length < 0.001f) {
		return false;
	}
	const Vec3f y = along / length;

	const Vec3f center = (from + to) * 0.5f;
	const Vec3f side = glm::cross(y, eye - center);
	if(glm::length(side) < 1e-6f) {
		return false;
	}
	const Vec3f x = glm::normalize(side);
	const Vec3f z = glm::cross(x, y);

	quad.center = center;
	quad.orientation = glm::quat_cast(glm::mat3(x, y, z));
	quad.size = Vec2f(width, length);
	return true;
}

void MenuPointer::setOwner(Hand hand) {
	if(hand != m_owner) {
		m_owner = hand;
		m_ownerChanged = true;
		m_ownerChanges++;
	}
}

void MenuPointer::update(bool panelShown, const std::array<PointerRay, HandCount> & rays,
                         const std::array<bool, HandCount> & trigger, const PointerParams & params) {

	m_ownerChanged = false;
	m_pressedOnPanel = { };

	if(!panelShown) {
		// The game's own clicks: nothing to mute, nothing to hold
		m_active = false;
		m_holding = false;
		m_muted = { };
		m_trigger = trigger;
		return;
	}

	// Presses: the pressing hand owns the cursor; the right hand's press wins a tie
	for(size_t i = 0; i < HandCount; i++) {
		if(!trigger[i] || m_trigger[i]) {
			continue;
		}
		const Hand hand = Hand(i);
		setOwner(hand);
		if(rays[i].onPanel) {
			m_holding = true;
			m_pressPixel = rays[i].pixel;
			m_pressedOnPanel[i] = true;
			m_clicks++;
		} else {
			m_muted[i] = true;
			m_offPanelPresses++;
		}
	}
	for(size_t i = 0; i < HandCount; i++) {
		if(!trigger[i]) {
			m_muted[i] = false;
		}
	}

	// The owner points away, the other hand at the panel: the other hand takes the cursor
	const Hand other = otherHand(m_owner);
	if(!rays[size_t(m_owner)].onPanel && !trigger[size_t(m_owner)] && rays[size_t(other)].onPanel) {
		setOwner(other);
	}

	const size_t owner = size_t(m_owner);
	if(!trigger[owner]) {
		m_holding = false;
	}
	m_active = rays[owner].onPanel;
	if(m_active) {
		if(m_holding && glm::distance(rays[owner].pixel, m_pressPixel) > params.clickDeadZone) {
			m_holding = false; // a drag: from now on the cursor follows the ray
		}
		m_cursor = m_holding ? m_pressPixel : rays[owner].pixel;
	}

	m_trigger = trigger;
}

bool MenuPointer::suppressClick() const {
	bool held = false;
	for(size_t i = 0; i < HandCount; i++) {
		if(m_trigger[i]) {
			if(!m_muted[i]) {
				return false;
			}
			held = true;
		}
	}
	return held;
}

} // namespace vr
