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

#include "vr/HeadReferenceGate.h"

#include <glm/glm.hpp>

namespace vr {

const char * referenceKindName(ReferenceKind kind) {
	switch(kind) {
		case ReferenceKind::None:        return "none";
		case ReferenceKind::Provisional: return "provisional";
		case ReferenceKind::Settled:     return "settled";
	}
	return "?";
}

void HeadReferenceGate::reset() {
	m_wanted = true;
	m_needProvisional = true;
}

ReferenceKind HeadReferenceGate::update(double seconds, const Vec3f & position, bool worn, bool instant) {

	if(!worn) {
		if(m_worn && m_current != ReferenceKind::None) {
			// Taken off: whoever puts it on next is measured anew, standing or seated - nobody sees the view
			// meanwhile, so the old reference stays until then
			m_wanted = true;
		}
		m_worn = false;
		m_settling = false;
		m_anchorValid = false;
	} else {
		if(!m_worn) {
			// Just put on: the head moves while the straps are adjusted - wait for it to settle
			m_worn = true;
			m_wornSince = seconds;
			m_settling = !instant && m_wanted;
		}
		if(!m_anchorValid || glm::distance(position, m_anchor) > m_params.settleMeters) {
			m_anchor = position;
			m_anchorSince = seconds;
			m_anchorValid = true;
		}
	}

	if(m_wanted && m_worn) {
		// Worn before the reset (a level load, a recenter) or a simulated head: the head is where the player
		// is, at once - a wait would turn the view a second time if the head turned meanwhile
		const bool still = seconds - m_anchorSince >= double(m_params.settleSeconds);
		const bool waited = seconds - m_wornSince >= double(m_params.maxWaitSeconds);
		if(instant || !m_settling || still || waited) {
			m_wanted = false;
			m_needProvisional = false;
			m_settling = false;
			m_current = ReferenceKind::Settled;
			return m_current;
		}
	}

	if(m_needProvisional || m_current == ReferenceKind::None) {
		m_needProvisional = false;
		m_current = ReferenceKind::Provisional;
		return m_current;
	}

	return ReferenceKind::None;
}

} // namespace vr
