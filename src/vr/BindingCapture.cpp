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

#include "vr/BindingCapture.h"

namespace vr {

namespace {

//! The lowest control in \a set, Binding::None when it is empty
Control firstControl(const ControlSet & set) {
	for(size_t i = 0; i < ControlCount; i++) {
		if(set[i]) {
			return Control(i);
		}
	}
	return Binding::None;
}

} // anonymous namespace

bool isRemappable(Control control, const char ** why) {

	const char * reason = nullptr;
	const ControlKind kind = controlKind(control);
	const bool direction = kind == ControlKind::StickUp || kind == ControlKind::StickDown
	                       || kind == ControlKind::StickLeft || kind == ControlKind::StickRight;
	if(control >= ControlCount) {
		reason = "not a control";
	} else if(direction && controlHand(control) == Hand::Left) {
		reason = "the left stick walks";
	} else if((kind == ControlKind::StickLeft || kind == ControlKind::StickRight) && controlHand(control) == Hand::Right) {
		reason = "the right stick turns";
	}

	if(why) {
		*why = reason ? reason : "";
	}
	return reason == nullptr;
}

void BindingCapture::start(s64 timeMs, const ControlSet & down) {
	m_state = State::Waiting;
	m_startMs = timeMs;
	m_ignored = down;
	m_binding = Binding();
	m_heldSince = 0;
	m_upSince = -1;
	m_refused = Binding::None;
	m_refusedWhy = "";
}

void BindingCapture::cancel() {
	if(active()) {
		m_state = State::Cancelled;
	}
}

bool BindingCapture::heldLong(s64 timeMs) const {
	return m_state == State::Holding && timeMs - m_heldSince >= longPressMs;
}

void BindingCapture::finish(s64 timeMs) {
	m_binding.longPress = timeMs - m_heldSince >= longPressMs;
	m_state = State::Done;
}

BindingCapture::State BindingCapture::update(s64 timeMs, const ControlSet & down, const ControlSet & taps) {

	if(!active()) {
		return m_state;
	}

	// Controls held from before the capture count again once they were released
	m_ignored &= down;
	ControlSet live = down & ~m_ignored;
	ControlSet tapped = taps & ~down;

	// The walk and turn sticks keep their jobs
	for(size_t i = 0; i < ControlCount; i++) {
		const char * why = nullptr;
		if((live[i] || tapped[i]) && !isRemappable(Control(i), &why)) {
			live[i] = false;
			tapped[i] = false;
			m_refused = Control(i);
			m_refusedWhy = why;
		}
	}

	if(m_state == State::Waiting) {

		if(m_ignored.any()) {
			// The time-out counts from the moment the player could start
			m_startMs = timeMs;
		}

		Control pressed = firstControl(live);
		if(pressed != Binding::None) {
			m_binding = Binding::single(pressed);
			m_heldSince = timeMs;
			m_upSince = -1;
			m_state = State::Holding;
		} else {
			pressed = firstControl(tapped);
			if(pressed != Binding::None) {
				// Pressed and released between two updates: a single press
				m_binding = Binding::single(pressed);
				m_heldSince = timeMs;
				finish(timeMs);
				return m_state;
			}
			if(timeMs - m_startMs >= timeoutMs) {
				m_state = State::Cancelled;
			}
			return m_state;
		}

	}

	const Hand hand = controlHand(m_binding.first);

	// A second control of the same hand while the first is held makes it a chord
	if(!m_binding.isChord()) {
		ControlSet others = live | tapped;
		others[m_binding.first] = false;
		for(size_t i = 0; i < ControlCount; i++) {
			if(!others[i]) {
				continue;
			}
			if(controlHand(Control(i)) != hand) {
				m_refused = Control(i);
				m_refusedWhy = "a chord is two controls of one hand";
				continue;
			}
			m_binding.second = Control(i);
			m_heldSince = timeMs;
			m_upSince = -1;
			if(tapped[i]) {
				finish(timeMs);
				return m_state;
			}
			break;
		}
	}

	// The capture ends when the player lets go of the binding's controls
	const bool held = live[m_binding.first] && (!m_binding.isChord() || live[m_binding.second]);
	if(held) {
		m_upSince = -1;
	} else {
		if(m_upSince < 0) {
			m_upSince = timeMs;
		}
		if(timeMs - m_upSince >= releaseDebounceMs) {
			finish(m_upSince);
		}
	}

	return m_state;
}

} // namespace vr
