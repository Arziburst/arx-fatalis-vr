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

#include "vr/HandTracking.h"

#if ARX_HAVE_OPENXR

#include <string>

#include "core/GameEvent.h"
#include "io/log/Logger.h"
#include "vr/FingerCurl.h"

namespace vr {

static_assert(HandJointCount == XR_HAND_JOINT_COUNT_EXT, "vr/FingerCurl.h lists the joints of XR_EXT_hand_tracking");

namespace {

//! How many failed locates are logged (one line each) before the log goes quiet about them
const u32 MaxLoggedLocateFailures = 10;

} // anonymous namespace

const char * HandTracking::extensionName() {
	return XR_EXT_HAND_TRACKING_EXTENSION_NAME;
}

const char * HandTracking::motionRangeExtensionName() {
	return XR_EXT_HAND_JOINTS_MOTION_RANGE_EXTENSION_NAME;
}

bool HandTracking::bind(XrInstance instance, bool motionRange) {

	unbind();

	const struct { const char * name; PFN_xrVoidFunction * function; } functions[] = {
		{ "xrCreateHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction *>(&m_createHandTracker) },
		{ "xrDestroyHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction *>(&m_destroyHandTracker) },
		{ "xrLocateHandJointsEXT", reinterpret_cast<PFN_xrVoidFunction *>(&m_locateHandJoints) },
	};
	for(const auto & function : functions) {
		XrResult result = xrGetInstanceProcAddr(instance, function.name, function.function);
		if(XR_FAILED(result) || !*function.function) {
			LogWarning << "[vr] hands: the runtime lists " << extensionName() << " but does not provide "
			           << function.name << " (" << resultName(result) << ") - the fingers follow the controls instead";
			unbind();
			return false;
		}
	}

	m_status.extension = true;
	m_status.motionRange = motionRange;
	LogInfo << "[vr] hands: " << extensionName() << " v" << XR_EXT_hand_tracking_SPEC_VERSION
	        << " enabled on the instance (the fingers follow the real fingers once the session has trackers); "
	        << (motionRange ? std::string(motionRangeExtensionName()) + ": the joints conform to the controller"
	                        : std::string("no ") + motionRangeExtensionName() + ": the joints are the runtime's unobstructed range");

	return true;
}

void HandTracking::setSupported(bool supported) {
	m_status.supported = supported;
}

bool HandTracking::create(XrSession session) {

	destroy();

	if(!m_status.extension) {
		return false;
	}
	if(!m_status.supported) {
		LogInfo << "[vr] hands: the headset reports no hand tracking (supportsHandTracking = 0) - the fingers"
		           " follow the controls";
		return false;
	}

	for(size_t hand = 0; hand < HandCount; hand++) {
		XrHandTrackerCreateInfoEXT info = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
		info.hand = (Hand(hand) == Hand::Left) ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
		info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
		XrResult result = m_createHandTracker(session, &info, &m_trackers[hand]);
		if(XR_FAILED(result)) {
			m_trackers[hand] = XR_NULL_HANDLE;
			LogWarning << "[vr] hands: could not create the " << handName(Hand(hand)) << " hand tracker ("
			           << resultName(result) << ") - the fingers follow the controls";
			destroy();
			return false;
		}
	}

	m_status.trackers = true;
	LogInfo << "[vr] hands: hand trackers created for both hands (" << XR_HAND_JOINT_COUNT_EXT
	        << " joints each, located with the grip poses every frame)";
	gameevent::Event("vr.hands.trackers")("state", "created");

	return true;
}

void HandTracking::locate(XrSpace space, XrTime time, FrameHands & hands) {

	for(size_t hand = 0; hand < HandCount; hand++) {

		HandJoints & joints = hands.joints[hand];
		joints = HandJoints();

		bool active = false;
		if(m_trackers[hand] != XR_NULL_HANDLE && space != XR_NULL_HANDLE && time != 0) {
			XrHandJointsLocateInfoEXT info = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
			info.baseSpace = space;
			info.time = time;
			// SteamVR's own hand around the controller's handle, thumb included (#385.7, research F-464)
			XrHandJointsMotionRangeInfoEXT range = { XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT };
			range.handJointsMotionRange = XR_HAND_JOINTS_MOTION_RANGE_CONFORMING_TO_CONTROLLER_EXT;
			if(m_status.motionRange) {
				info.next = &range;
			}
			std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> locations = { };
			XrHandJointLocationsEXT located = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
			located.jointCount = uint32_t(locations.size());
			located.jointLocations = locations.data();
			XrResult result = m_locateHandJoints(m_trackers[hand], &info, &located);
			if(XR_FAILED(result)) {
				m_status.failures++;
				if(m_status.failures <= MaxLoggedLocateFailures) {
					LogWarning << "[vr] hands: xrLocateHandJointsEXT failed for the " << handName(Hand(hand))
					           << " hand: " << resultName(result)
					           << (m_status.failures == MaxLoggedLocateFailures ? " (further failures are not logged)" : "");
				}
			} else if(located.isActive) {
				// Every joint must have a position (the curl is read from the angles between them) and an
				// orientation (the glove is posed by them, #385.7)
				active = true;
				const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
				for(size_t joint = 0; joint < locations.size(); joint++) {
					if((locations[joint].locationFlags & needed) != needed) {
						active = false;
						break;
					}
					const XrVector3f & p = locations[joint].pose.position;
					const XrQuaternionf & q = locations[joint].pose.orientation;
					joints.position[joint] = Vec3f(p.x, p.y, p.z);
					joints.orientation[joint] = glm::normalize(glm::quat(q.w, q.x, q.y, q.z));
				}
			}
		}
		joints.valid = active;

		// An episode, not a frame: the runtime starting or stopping to give this hand's fingers
		if(active != m_status.active[hand]) {
			m_status.active[hand] = active;
			gameevent::Event("vr.hands.joints", 200)("hand", handName(Hand(hand)))("state", active ? "active" : "inactive");
		}
		if(active) {
			m_status.activeFrames[hand]++;
		}
	}

	m_status.locates++;
}

void HandTracking::destroy() {

	for(size_t hand = 0; hand < HandCount; hand++) {
		if(m_trackers[hand] != XR_NULL_HANDLE) {
			if(m_destroyHandTracker) {
				m_destroyHandTracker(m_trackers[hand]);
			}
			m_trackers[hand] = XR_NULL_HANDLE;
		}
		m_status.active[hand] = false;
	}
	m_status.trackers = false;
}

void HandTracking::unbind() {
	destroy();
	m_createHandTracker = nullptr;
	m_destroyHandTracker = nullptr;
	m_locateHandJoints = nullptr;
	m_status = HandJointsStatus();
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
