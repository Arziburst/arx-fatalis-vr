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

#ifndef ARX_VR_HANDTRACKING_H
#define ARX_VR_HANDTRACKING_H

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <array>

#include "platform/Platform.h"
#include "vr/VRControls.h"
#include "vr/VRSystem.h"

namespace vr {

/*!
 * \brief \c XR_EXT_hand_tracking: the 26 joints of each hand, located once per frame with the controllers (#305)
 *
 * The extension's functions are not exported by the OpenXR loader (like the OpenGL binding's), so
 * they come from \c xrGetInstanceProcAddr once the instance exists - the simulated runtime answers
 * that call with its own (vr/VRSimulator.cpp). A tracker per hand is created with the session and
 * located in the frame's world-locked space for the frame's display time, the same as the grip
 * poses, so the fingers and the hand belong to one moment (F-21). The body module reads the curl
 * off the joints (vr/FingerCurl.h); a hand whose joints are not active this frame - a runtime
 * without the extension, a headset that reports no support, a controller the runtime cannot read
 * fingers from - falls back to the action values, and says so once per episode.
 */
class HandTracking {

public:

	//! The extension's name, for the instance's list
	static const char * extensionName();

	//! The instance was created with the extension: get its functions. False when the runtime lacks one.
	bool bind(XrInstance instance);

	//! What the system reports (\c XrSystemHandTrackingPropertiesEXT chained to \c xrGetSystemProperties)
	void setSupported(bool supported);

	//! A tracker per hand, once the session exists; false (said in the log) when there will be none
	bool create(XrSession session);

	//! Locate both hands' joints in \a space for \a time into \a hands (\c joints[hand].valid when all were located)
	void locate(XrSpace space, XrTime time, FrameHands & hands);

	//! Destroy the trackers - before the session
	void destroy();

	//! Forget the functions - with the instance
	void unbind();

	const HandJointsStatus & status() const { return m_status; }

private:

	PFN_xrCreateHandTrackerEXT m_createHandTracker = nullptr;
	PFN_xrDestroyHandTrackerEXT m_destroyHandTracker = nullptr;
	PFN_xrLocateHandJointsEXT m_locateHandJoints = nullptr;
	std::array<XrHandTrackerEXT, HandCount> m_trackers = { };
	HandJointsStatus m_status;

};

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_HANDTRACKING_H
