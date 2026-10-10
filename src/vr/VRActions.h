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

#ifndef ARX_VR_VRACTIONS_H
#define ARX_VR_VRACTIONS_H

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <array>
#include <string>

#include "vr/VRBindings.h"
#include "vr/VRControls.h"
#include "vr/VRSystem.h"

namespace vr {

/*!
 * \brief The OpenXR action set: one action per physical control (#30, D-020)
 *
 * Every action has a left and a right subaction path: trigger, grip, A (X on the left Touch),
 * B (Y), thumbstick, thumbstick click, Index trackpad press, menu, grip pose, aim pose, haptic.
 * Which gameplay event a control triggers is decided by the game's own control table
 * (VRControls.h), so players remap inside the game; SteamVR's binding UI still rebinds the
 * physical actions on top.
 *
 * The action names are the key of the players' saved SteamVR bindings: renaming or removing one
 * breaks those bindings, so they stay as they are once shipped.
 *
 * Suggested bindings are logged per profile with their count and outcome (F-123): a runtime
 * rejects a profile's whole list for one path it does not know, silently, so a rejected list is
 * retried without its optional paths.
 */
class ActionSet {

public:

	//! The action set, its actions and the suggested bindings - before the session is created
	bool create(XrInstance instance);

	//! Attach the action set to the session and create the hands' pose spaces
	bool attach(XrSession session);

	//! The pose spaces belong to the session: destroy them before the session
	void detach();

	//! Destroy the action set (the instance destroys it too)
	void destroy();

	bool isAttached() const { return m_session != XR_NULL_HANDLE; }

	//! Ask for the interaction profiles to be read at the next sync (the runtime sent a change event)
	void profilesChanged() { m_profileCheckDue = true; }

	/*!
	 * Sync the actions and read every control. While the session is not focused the runtime gives
	 * no input: \a input is then empty and false is returned.
	 *
	 * \param viewSpace,time the head and a recent display time, for the handedness check
	 */
	bool sync(ControllerInput & input, XrSpace viewSpace, XrTime time);

	/*!
	 * Locate both hands' grip poses in \a space for \a time (#36): \a hands gets each hand's pose
	 * and whether its position was valid; nothing is tracked without a session, a space or a time
	 */
	void locateHands(XrSpace space, XrTime time, FrameHands & hands);

	//! The interaction profile path per hand, empty while there is none
	const std::array<std::string, HandCount> & profiles() const { return m_profiles; }

	//! How often the actions were synced while focused
	u64 syncs() const { return m_syncs; }

private:

	struct Actions {
		XrAction trigger = XR_NULL_HANDLE;
		XrAction grip = XR_NULL_HANDLE;
		XrAction a = XR_NULL_HANDLE;
		XrAction b = XR_NULL_HANDLE;
		XrAction stick = XR_NULL_HANDLE;
		XrAction stickClick = XR_NULL_HANDLE;
		XrAction trackpad = XR_NULL_HANDLE;
		XrAction menu = XR_NULL_HANDLE;
		XrAction gripPose = XR_NULL_HANDLE;
		XrAction aimPose = XR_NULL_HANDLE;
		XrAction haptic = XR_NULL_HANDLE;
		XrAction thumbTouch = XR_NULL_HANDLE; //!< the thumb resting on a control (#305): the fingers' fallback
	};

	XrAction createAction(const char * name, const char * label, XrActionType type);
	//! The action of a physical control of the suggestion table (VRBindings.h)
	XrAction action(PhysicalAction id) const;
	void suggestBindings();
	void updateProfiles();
	void checkHandedness(XrSpace viewSpace, XrTime time);

	float readFloat(XrAction action, Hand hand, bool & active);
	bool readBoolean(XrAction action, Hand hand, bool & active, bool & tapped);

	XrInstance m_instance = XR_NULL_HANDLE;
	XrSession m_session = XR_NULL_HANDLE;
	XrActionSet m_set = XR_NULL_HANDLE;
	Actions m_actions;
	std::array<XrPath, HandCount> m_hands = { };
	std::array<XrSpace, HandCount> m_gripSpaces = { };
	std::array<bool, HandCount> m_handTracked = { }; //!< the last located state per hand, for the tracked / lost events

	ControlSet m_down;     //!< after the last sync, for the thresholds' hysteresis
	ControlSet m_bound;    //!< the controls the runtime reported active, logged when it changes
	bool m_loggedBound = false;
	bool m_focused = false;
	u32 m_syncFailures = 0;
	u64 m_syncs = 0;

	std::array<std::string, HandCount> m_profiles;
	bool m_profileCheckDue = true;
	u64 m_lastProfileCheck = 0;

	//! The profiles the handedness was last measured for, so it is measured once per controller pair
	std::string m_handednessFor;

};

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_VRACTIONS_H
