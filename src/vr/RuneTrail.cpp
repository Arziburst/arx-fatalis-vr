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

#include "vr/RuneTrail.h"

#include <cmath>

#include <glm/glm.hpp>

namespace vr {

Vec3f runeTrailPoint(const Vec3f & headCamera, const HandPlacement & hand, float tipMeters, float unitsPerMeter) {
	// The grip's forward (OpenXR -Z) is +Z in the engine axes the placement turns into the world
	return headCamera + hand.offset + hand.orientation * Vec3f(0.f, 0.f, tipMeters * unitsPerMeter);
}

std::vector<Vec3f> fillTrailSegment(const Vec3f & from, const Vec3f & to, float step) {
	std::vector<Vec3f> points;
	const float distance = glm::distance(from, to);
	if(!(step > 0.f) || !(distance > step)) {
		return points;
	}
	const Vec3f direction = (to - from) / distance;
	for(float along = step; along < distance; along += step) {
		points.push_back(from + direction * along);
	}
	return points;
}

const char * strokeEndName(StrokeEnd end) {
	switch(end) {
		case StrokeEnd::None: return "none";
		case StrokeEnd::Trigger: return "trigger";
		case StrokeEnd::Stance: return "stance";
		case StrokeEnd::Lost: return "lost";
	}
	return "?";
}

void RuneStroke::update(s64 timeMs, bool stance, const std::array<StrokeHand, HandCount> & hands) {

	m_started = false;
	m_ended = StrokeEnd::None;

	// A released trigger may draw again
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(!hands[hand].trigger) {
			m_ignored[hand] = false;
		}
	}

	if(m_drawing) {
		const StrokeHand & current = hands[size_t(m_hand)];
		StrokeEnd end = StrokeEnd::None;
		if(!stance) {
			end = StrokeEnd::Stance;
		} else if(!current.trigger) {
			end = StrokeEnd::Trigger;
		} else if(!current.located) {
			end = StrokeEnd::Lost;
		}
		m_durationMs = timeMs - m_startMs;
		if(end != StrokeEnd::None) {
			m_drawing = false;
			m_ended = end;
			m_lastEnd = end;
		} else {
			m_length += glm::distance(m_point, current.point);
			m_point = current.point;
		}
	}

	if(!m_drawing && stance) {
		// The first trigger wins; two in the same update: the right hand
		for(Hand hand : { Hand::Right, Hand::Left }) {
			const StrokeHand & candidate = hands[size_t(hand)];
			if(candidate.trigger && candidate.located && !m_ignored[size_t(hand)]) {
				m_drawing = true;
				m_hand = hand;
				m_point = candidate.point;
				m_length = 0.f;
				m_startMs = timeMs;
				m_durationMs = 0;
				m_strokes++;
				m_started = true;
				break;
			}
		}
	}

	// While one hand draws, the other's trigger does nothing until it is released
	if(m_drawing) {
		for(size_t hand = 0; hand < HandCount; hand++) {
			if(Hand(hand) != m_hand && hands[hand].trigger) {
				m_ignored[hand] = true;
			}
		}
	}
}

} // namespace vr
