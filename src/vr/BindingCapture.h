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

#ifndef ARX_VR_BINDINGCAPTURE_H
#define ARX_VR_BINDINGCAPTURE_H

#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief Remapping in the VR settings (#178, D-020): what the player presses becomes a binding
 *
 * The player selects an event on the VR settings page and then presses what should trigger it. This
 * file turns the controls held in each update into the \ref Binding they meant:
 *  - press and release one control: a **single** binding;
 *  - hold it at least \ref BindingCapture::longPressMs before releasing: a **long press**;
 *  - while holding it, press a second control of the same hand: a **chord** (held together at least
 *    \ref BindingCapture::longPressMs: a long chord).
 * The capture ends when the player lets go, not when a time runs out, so holding the grip for a while
 * before pulling the trigger is still the grip + trigger chord. A release counts only after the control
 * stayed up for \ref BindingCapture::releaseDebounceMs (the Index grip flickers around its threshold,
 * TRAPS.md). Controls held when the capture starts - the trigger that clicked the row - are ignored
 * until released. The walk stick and the turn stick's directions are not remappable: pressing them
 * is refused with the reason. A control of the other hand while one is held is ignored (a chord is
 * two controls of one hand, VRControls.h). Nothing pressed for \ref BindingCapture::timeoutMs cancels.
 *
 * Pure logic - no OpenXR, no engine globals, no clock reads - covered by tests/vr/BindingCaptureTest.
 */
namespace vr {

/*!
 * \return whether players can bind \a control to an event; when not, \a why says what it does
 *         instead: the left stick's directions walk (D-008), the right stick's left / right turn (#32)
 */
bool isRemappable(Control control, const char ** why = nullptr);

class BindingCapture {

public:

	enum class State : u8 {
		Idle,      //!< not capturing
		Waiting,   //!< capturing, nothing pressed yet (or only controls held from before)
		Holding,   //!< capturing, a control (or a chord) is held: \ref binding shows it so far
		Done,      //!< \ref binding is what the player pressed
		Cancelled  //!< nothing was pressed in time, or \ref cancel was called
	};

	//! How long a control is held for a long press - the control table's own time (ControlResolver)
	s64 longPressMs = 500;
	//! How long a released control must stay up before the release counts
	s64 releaseDebounceMs = 150;
	//! How long the capture waits for the first press
	s64 timeoutMs = 8000;

	/*!
	 * Start capturing. \a down are the controls held now: ignored until they are released.
	 */
	void start(s64 timeMs, const ControlSet & down);

	void cancel();

	/*!
	 * \param timeMs a monotonic time in milliseconds
	 * \param down   the controls held now
	 * \param taps   controls pressed and released since the last update (not in \a down)
	 * \return the state after this update
	 */
	State update(s64 timeMs, const ControlSet & down, const ControlSet & taps = ControlSet());

	State state() const { return m_state; }
	bool active() const { return m_state == State::Waiting || m_state == State::Holding; }

	//! What was captured (\ref State::Done), or what is held so far (\ref State::Holding)
	const Binding & binding() const { return m_binding; }

	//! Holding: whether the held control (or chord) is already a long press if released now
	bool heldLong(s64 timeMs) const;

	//! The last control refused in this capture (Binding::None: none) and why
	Control refused() const { return m_refused; }
	const char * refusedWhy() const { return m_refusedWhy; }

private:

	void finish(s64 timeMs);

	State m_state = State::Idle;
	s64 m_startMs = 0;
	ControlSet m_ignored;      //!< held from before the capture, ignored until released
	Binding m_binding;
	s64 m_heldSince = 0;       //!< when the first control (or the chord's second) was pressed
	s64 m_upSince = -1;        //!< a held control seen up since then, not released yet (-1: down)
	Control m_refused = Binding::None;
	const char * m_refusedWhy = "";

};

} // namespace vr

#endif // ARX_VR_BINDINGCAPTURE_H
