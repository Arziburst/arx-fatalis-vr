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

#include "vr/RadialMenu.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

const float SegmentDegrees = 360.f / float(RadialSegmentCount);

const RadialItem HalfRingItems[HalfRingItemCount] = {
	RadialItem::GameMenu,
	RadialItem::Character,
	RadialItem::Spells,
	RadialItem::Map,
	RadialItem::Quests,
	RadialItem::QuickSave,
	RadialItem::QuickLoad,
	RadialItem::Recenter,
	RadialItem::CancelSpell,
};

//! [0, 360)
float wrap360(float degrees) {
	degrees = std::fmod(degrees, 360.f);
	return degrees < 0.f ? degrees + 360.f : degrees;
}

//! (-180, 180]
float wrap180(float degrees) {
	degrees = wrap360(degrees);
	return degrees > 180.f ? degrees - 360.f : degrees;
}

bool isClassName(std::string_view name) {
	if(name.empty()) {
		return false;
	}
	for(char c : name) {
		if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
			return false;
		}
	}
	return true;
}

} // anonymous namespace

std::string radialChoiceName(const RadialChoice & choice) {
	switch(choice.item) {
		case RadialItem::None: return "none";
		case RadialItem::Inventory: return "inventory";
		case RadialItem::QuickSlot: return "slot" + std::to_string(int(choice.slot) + 1);
		case RadialItem::GameMenu: return "game_menu";
		case RadialItem::Character: return "character";
		case RadialItem::Spells: return "spells";
		case RadialItem::Map: return "map";
		case RadialItem::Quests: return "quests";
		case RadialItem::QuickSave: return "quick_save";
		case RadialItem::QuickLoad: return "quick_load";
		case RadialItem::Recenter: return "recenter";
		case RadialItem::CancelSpell: return "cancel_spell";
	}
	return "none";
}

const char * radialItemLabel(RadialItem item) {
	switch(item) {
		case RadialItem::None: return "";
		case RadialItem::Inventory: return "Inventory";
		case RadialItem::QuickSlot: return "";
		case RadialItem::GameMenu: return "Game menu";
		case RadialItem::Character: return "Character";
		case RadialItem::Spells: return "Spells";
		case RadialItem::Map: return "Map";
		case RadialItem::Quests: return "Quests";
		case RadialItem::QuickSave: return "Quick save";
		case RadialItem::QuickLoad: return "Quick load";
		case RadialItem::Recenter: return "Recenter";
		case RadialItem::CancelSpell: return "Cancel spell";
	}
	return "";
}

RadialChoice segmentChoice(size_t segment) {
	RadialChoice choice;
	if(segment == 0) {
		choice.item = RadialItem::Inventory;
	} else if(segment < RadialSegmentCount) {
		choice.item = RadialItem::QuickSlot;
		choice.slot = u8(segment - 1);
	}
	return choice;
}

RadialItem halfRingItem(size_t index) {
	return index < HalfRingItemCount ? HalfRingItems[index] : RadialItem::None;
}

float segmentCenterDegrees(size_t segment) {
	return wrap360(90.f - SegmentDegrees * float(segment));
}

float halfRingCenterDegrees(size_t index) {
	return 180.f - HalfRingItemDegrees * (float(index) + 0.5f);
}

RadialPose placeRadial(const Vec3f & hand, const Vec3f & eye, const RadialParams & params) {

	Vec3f away = hand - eye;
	const float distance = glm::length(away);
	// A hand at the eye has no direction away from it: the ring then faces the room's -z
	away = (distance > 1e-3f) ? away / distance : Vec3f(0.f, 0.f, -1.f);

	RadialPose pose;
	pose.center = hand + away * params.push;

	const Vec3f z = -away;
	Vec3f up(0.f, 1.f, 0.f);
	if(std::abs(glm::dot(up, z)) > 0.999f) {
		up = Vec3f(0.f, 0.f, -1.f);
	}
	const Vec3f x = glm::normalize(glm::cross(up, z));
	const Vec3f y = glm::cross(z, x);
	pose.orientation = glm::quat_cast(glm::mat3(x, y, z));

	return pose;
}

Vec2f radialPlanePoint(const RadialPose & pose, const Vec3f & point) {
	const Vec3f d = point - pose.center;
	return Vec2f(glm::dot(d, pose.orientation * Vec3f(1.f, 0.f, 0.f)),
	             glm::dot(d, pose.orientation * Vec3f(0.f, 1.f, 0.f)));
}

void RadialSelector::reset() {
	m_hovered = RadialChoice();
	m_segment = -1;
	m_halfItem = -1;
	m_halfOpen = false;
}

const RadialChoice & RadialSelector::update(const Vec2f & point, const RadialParams & params) {

	const float radius = glm::length(point);
	const float angle = glm::degrees(std::atan2(point.y, point.x));

	if(m_halfOpen) {
		if(radius < params.halfCloseRadius || point.y < params.halfCloseHeight) {
			m_halfOpen = false;
			m_halfItem = -1;
		}
	} else if(point.y > params.halfOpenHeight) {
		m_halfOpen = true;
		m_segment = -1;
	}

	if(m_halfOpen) {
		// Left (180) to right (0); a hand a little below the horizontal still counts for the end items
		const float along = std::clamp(180.f - (point.y < 0.f ? (point.x < 0.f ? 180.f : 0.f) : angle), 0.f, 180.f);
		const bool keep = m_halfItem >= 0
		                  && std::abs(along - HalfRingItemDegrees * (float(m_halfItem) + 0.5f))
		                     <= HalfRingItemDegrees * 0.5f + params.marginDegrees;
		if(!keep) {
			m_halfItem = std::min(int(along / HalfRingItemDegrees), int(HalfRingItemCount) - 1);
		}
		m_hovered = RadialChoice();
		m_hovered.item = halfRingItem(size_t(m_halfItem));
		return m_hovered;
	}

	if(radius < params.holeRadius) {
		m_segment = -1;
		m_hovered = RadialChoice();
		return m_hovered;
	}

	// Clockwise from the top
	const float clockwise = wrap360(90.f - angle);
	const bool keep = m_segment >= 0
	                  && std::abs(wrap180(clockwise - SegmentDegrees * float(m_segment)))
	                     <= SegmentDegrees * 0.5f + params.marginDegrees;
	if(!keep) {
		m_segment = int(wrap360(clockwise + SegmentDegrees * 0.5f) / SegmentDegrees) % int(RadialSegmentCount);
	}
	m_hovered = segmentChoice(size_t(m_segment));
	return m_hovered;
}

std::string formatQuickSlots(const QuickSlotSet & slots) {
	std::string result;
	for(size_t i = 0; i < slots.size(); i++) {
		if(i > 0) {
			result += '|';
		}
		result += slots[i];
	}
	return result;
}

QuickSlotSet parseQuickSlots(std::string_view text) {
	QuickSlotSet slots;
	size_t slot = 0;
	while(slot < slots.size()) {
		const size_t end = text.find('|');
		const std::string_view name = text.substr(0, end);
		if(isClassName(name)) {
			slots[slot] = std::string(name);
		}
		slot++;
		if(end == std::string_view::npos) {
			break;
		}
		text.remove_prefix(end + 1);
	}
	return slots;
}

} // namespace vr
