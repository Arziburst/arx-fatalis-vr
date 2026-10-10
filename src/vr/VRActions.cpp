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

#include "vr/VRActions.h"

#if ARX_HAVE_OPENXR

#include <iomanip>
#include <sstream>
#include <vector>

#include "core/GameEvent.h"
#include "io/log/Logger.h"
#include "util/String.h"
#include "vr/VRBindings.h"

namespace vr {

namespace {

//! Sync failures logged per session before it goes quiet (the sync runs every frame)
const u32 MaxLoggedSyncFailures = 5;

//! How often the interaction profiles are read without a change event (F-123: every 30 frames)
const u64 ProfilePollSyncs = 30;

} // anonymous namespace

XrAction ActionSet::createAction(const char * name, const char * label, XrActionType type) {

	XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
	util::storeStringTerminated(info.actionName, name);
	util::storeStringTerminated(info.localizedActionName, label);
	info.actionType = type;
	info.countSubactionPaths = uint32_t(m_hands.size());
	info.subactionPaths = m_hands.data();

	XrAction action = XR_NULL_HANDLE;
	XrResult result = xrCreateAction(m_set, &info, &action);
	if(XR_FAILED(result)) {
		LogError << "[vr] input: could not create the OpenXR action \"" << name << "\": " << resultName(result);
		return XR_NULL_HANDLE;
	}

	return action;
}

bool ActionSet::create(XrInstance instance) {

	arx_assert(m_set == XR_NULL_HANDLE);

	m_instance = instance;

	for(size_t hand = 0; hand < HandCount; hand++) {
		XrResult result = xrStringToPath(m_instance, handPath(Hand(hand)), &m_hands[hand]);
		if(XR_FAILED(result)) {
			LogError << "[vr] input: could not make the path " << handPath(Hand(hand)) << ": " << resultName(result);
			return false;
		}
	}

	XrActionSetCreateInfo setInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
	util::storeStringTerminated(setInfo.actionSetName, "controls");
	util::storeStringTerminated(setInfo.localizedActionSetName, "Arx Fatalis VR controls");
	setInfo.priority = 0;
	XrResult result = xrCreateActionSet(m_instance, &setInfo, &m_set);
	if(XR_FAILED(result)) {
		m_set = XR_NULL_HANDLE;
		LogError << "[vr] input: could not create the OpenXR action set: " << resultName(result);
		return false;
	}

	// The names are the key of the players' saved SteamVR bindings - never rename one once shipped
	Actions & a = m_actions;
	a.trigger = createAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
	a.grip = createAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
	a.a = createAction("button_a", "A / X button (lower)", XR_ACTION_TYPE_BOOLEAN_INPUT);
	a.b = createAction("button_b", "B / Y button (upper)", XR_ACTION_TYPE_BOOLEAN_INPUT);
	a.stick = createAction("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT);
	a.stickClick = createAction("thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
	a.trackpad = createAction("trackpad_press", "Trackpad press", XR_ACTION_TYPE_FLOAT_INPUT);
	a.menu = createAction("menu", "Menu button", XR_ACTION_TYPE_BOOLEAN_INPUT);
	a.gripPose = createAction("grip_pose", "Hand pose", XR_ACTION_TYPE_POSE_INPUT);
	a.aimPose = createAction("aim_pose", "Pointer pose", XR_ACTION_TYPE_POSE_INPUT);
	a.haptic = createAction("haptic", "Haptic feedback", XR_ACTION_TYPE_VIBRATION_OUTPUT);
	for(XrAction action : { a.trigger, a.grip, a.a, a.b, a.stick, a.stickClick, a.trackpad, a.menu,
	                        a.gripPose, a.aimPose, a.haptic }) {
		if(action == XR_NULL_HANDLE) {
			destroy();
			return false;
		}
	}

	suggestBindings();

	LogInfo << "[vr] input: action set \"controls\" created - 11 actions per hand (one per physical control);"
	           " the game's control table maps them to gameplay events";

	return true;
}

XrAction ActionSet::action(PhysicalAction id) const {
	const Actions & a = m_actions;
	switch(id) {
		case PhysicalAction::Trigger: return a.trigger;
		case PhysicalAction::Grip: return a.grip;
		case PhysicalAction::A: return a.a;
		case PhysicalAction::B: return a.b;
		case PhysicalAction::Stick: return a.stick;
		case PhysicalAction::StickClick: return a.stickClick;
		case PhysicalAction::Trackpad: return a.trackpad;
		case PhysicalAction::Menu: return a.menu;
		case PhysicalAction::GripPose: return a.gripPose;
		case PhysicalAction::AimPose: return a.aimPose;
		case PhysicalAction::Haptic: return a.haptic;
		case PhysicalAction::Count: break;
	}
	return XR_NULL_HANDLE;
}

void ActionSet::suggestBindings() {

	// The table per profile and its checks against the OpenXR spec: VRBindings.h, tests/vr/VRBindingsTest
	for(ControllerType type : suggestedControllerTypes()) {

		const char * profilePath = interactionProfilePath(type);
		const std::vector<SuggestedBinding> list = suggestedBindings(type);

		XrPath profile = XR_NULL_PATH;
		XrResult result = xrStringToPath(m_instance, profilePath, &profile);
		if(XR_FAILED(result)) {
			LogError << "[vr] input: " << profilePath << ": not a valid path: " << resultName(result);
			continue;
		}

		bool hasOptional = false;
		for(const SuggestedBinding & suggestion : list) {
			hasOptional = hasOptional || suggestion.optional;
		}

		// The runtime rejects the whole list for one path it does not know: then try without the optional ones
		for(bool withOptional : { true, false }) {

			if(!withOptional && !hasOptional) {
				break;
			}

			std::vector<XrActionSuggestedBinding> bindings;
			std::string invalid;
			for(const SuggestedBinding & suggestion : list) {
				if(suggestion.optional && !withOptional) {
					continue;
				}
				const std::string path = bindingPath(suggestion);
				XrPath xrPath = XR_NULL_PATH;
				if(XR_FAILED(xrStringToPath(m_instance, path.c_str(), &xrPath))) {
					invalid += ' ' + path;
					continue;
				}
				bindings.push_back({ action(suggestion.action), xrPath });
			}
			if(!invalid.empty()) {
				LogError << "[vr] input: " << profilePath << ": invalid paths left out:" << invalid;
			}

			XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
			suggested.interactionProfile = profile;
			suggested.countSuggestedBindings = uint32_t(bindings.size());
			suggested.suggestedBindings = bindings.data();
			result = xrSuggestInteractionProfileBindings(m_instance, &suggested);

			const char * which = withOptional ? "" : " (without the optional paths)";
			if(XR_SUCCEEDED(result)) {
				LogInfo << "[vr] input: " << profilePath << ": " << bindings.size() << " bindings suggested"
				        << which << " -> accepted";
				break;
			}

			LogWarning << "[vr] input: " << profilePath << ": " << bindings.size() << " bindings suggested" << which
			           << " -> REJECTED (" << resultName(result) << ")"
			           << (withOptional && hasOptional ? ", retrying without the optional paths"
			                                           : " - these controllers get no input from us");
		}
	}
}

bool ActionSet::attach(XrSession session) {

	if(m_set == XR_NULL_HANDLE) {
		return false;
	}

	arx_assert(m_session == XR_NULL_HANDLE);

	XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &m_set;
	XrResult result = xrAttachSessionActionSets(session, &attachInfo);
	if(XR_FAILED(result)) {
		LogError << "[vr] input: could not attach the action set to the session (" << resultName(result)
		         << "), the controllers do nothing";
		return false;
	}

	m_session = session;
	m_down.reset();
	m_bound.reset();
	m_loggedBound = false;
	m_focused = false;
	m_syncFailures = 0;
	m_profiles = { };
	m_profileCheckDue = true;
	m_handednessFor.clear();

	for(size_t hand = 0; hand < HandCount; hand++) {
		XrActionSpaceCreateInfo spaceInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
		spaceInfo.action = m_actions.gripPose;
		spaceInfo.subactionPath = m_hands[hand];
		spaceInfo.poseInActionSpace.orientation.w = 1.f;
		result = xrCreateActionSpace(session, &spaceInfo, &m_gripSpaces[hand]);
		if(XR_FAILED(result)) {
			m_gripSpaces[hand] = XR_NULL_HANDLE;
			LogWarning << "[vr] input: could not create the " << handName(Hand(hand)) << " hand's pose space: "
			           << resultName(result);
		}
		// The pointer ray of the menu pointer (#34)
		spaceInfo.action = m_actions.aimPose;
		result = xrCreateActionSpace(session, &spaceInfo, &m_aimSpaces[hand]);
		if(XR_FAILED(result)) {
			m_aimSpaces[hand] = XR_NULL_HANDLE;
			LogWarning << "[vr] input: could not create the " << handName(Hand(hand)) << " hand's aim space, no menu"
			              " pointer for it: " << resultName(result);
		}
	}

	LogInfo << "[vr] input: action set attached to the session - the interaction profiles follow once the"
	           " session is focused";

	return true;
}

void ActionSet::detach() {

	for(XrSpace & space : m_gripSpaces) {
		if(space != XR_NULL_HANDLE) {
			xrDestroySpace(space);
			space = XR_NULL_HANDLE;
		}
	}
	for(XrSpace & space : m_aimSpaces) {
		if(space != XR_NULL_HANDLE) {
			xrDestroySpace(space);
			space = XR_NULL_HANDLE;
		}
	}

	m_session = XR_NULL_HANDLE;
	m_down.reset();
	m_focused = false;
}

void ActionSet::destroy() {

	detach();

	if(m_set != XR_NULL_HANDLE) {
		// Destroys its actions as well
		xrDestroyActionSet(m_set);
		m_set = XR_NULL_HANDLE;
	}

	m_actions = Actions();
	m_instance = XR_NULL_HANDLE;
}

float ActionSet::readFloat(XrAction action, Hand hand, bool & active) {

	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action = action;
	info.subactionPath = m_hands[size_t(hand)];
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
	if(XR_FAILED(xrGetActionStateFloat(m_session, &info, &state)) || !state.isActive) {
		active = false;
		return 0.f;
	}

	active = true;
	return state.currentState;
}

bool ActionSet::readBoolean(XrAction action, Hand hand, bool & active, bool & tapped) {

	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	info.action = action;
	info.subactionPath = m_hands[size_t(hand)];
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
	if(XR_FAILED(xrGetActionStateBoolean(m_session, &info, &state)) || !state.isActive) {
		active = false;
		tapped = false;
		return false;
	}

	active = true;
	// Released now but changed since the last sync: a press that started and ended between two syncs (F-122)
	tapped = !state.currentState && state.changedSinceLastSync;
	return state.currentState;
}

void ActionSet::updateProfiles() {

	m_profileCheckDue = false;
	m_lastProfileCheck = m_syncs;

	for(size_t hand = 0; hand < HandCount; hand++) {

		XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };
		XrResult result = xrGetCurrentInteractionProfile(m_session, m_hands[hand], &state);

		std::string profile;
		if(XR_SUCCEEDED(result) && state.interactionProfile != XR_NULL_PATH) {
			char buffer[XR_MAX_PATH_LENGTH];
			uint32_t length = 0;
			if(XR_SUCCEEDED(xrPathToString(m_instance, state.interactionProfile, uint32_t(sizeof(buffer)), &length, buffer))) {
				profile = buffer;
			}
		}

		if(profile == m_profiles[hand]) {
			continue;
		}

		const char * name = handName(Hand(hand));
		if(XR_FAILED(result)) {
			LogWarning << "[vr] input: " << name << " hand: could not read the interaction profile: " << resultName(result);
		} else if(profile.empty()) {
			LogInfo << "[vr] input: " << name << " hand: no interaction profile (XR_NULL_PATH) - no device for that"
			           " hand, switch the controller on";
		} else {
			LogInfo << "[vr] input: " << name << " hand: interaction profile " << profile << " ("
			        << controllerTypeName(controllerTypeFromProfile(profile)) << ')';
		}
		m_profiles[hand] = profile;
	}
}

void ActionSet::checkHandedness(XrSpace viewSpace, XrTime time) {

	if(viewSpace == XR_NULL_HANDLE || time == 0 || m_profiles[0].empty() || m_profiles[1].empty()
	   || m_gripSpaces[0] == XR_NULL_HANDLE || m_gripSpaces[1] == XR_NULL_HANDLE) {
		return;
	}

	const std::string pair = m_profiles[0] + " + " + m_profiles[1];
	if(pair == m_handednessFor || m_syncs % ProfilePollSyncs != 0) {
		return;
	}

	std::array<XrSpaceLocation, HandCount> located;
	for(size_t hand = 0; hand < HandCount; hand++) {
		located[hand] = { XR_TYPE_SPACE_LOCATION };
		if(XR_FAILED(xrLocateSpace(m_gripSpaces[hand], viewSpace, time, &located[hand]))
		   || !(located[hand].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
			return; // not tracked yet, measured on a later sync
		}
	}

	// In the view space the head's right axis is +x: the right hand should be to the right of the left one
	m_handednessFor = pair;
	const float offset = located[1].pose.position.x - located[0].pose.position.x;
	std::ostringstream distance;
	distance << std::fixed << std::setprecision(2) << std::abs(offset);
	if(offset > 0.f) {
		LogInfo << "[vr] input: handedness measured: the right controller is " << distance.str()
		        << " m right of the left one along the head's right axis";
	} else {
		LogWarning << "[vr] input: handedness measured: the right controller is " << distance.str()
		           << " m LEFT of the left one along the head's right axis - hands crossed, or the runtime"
		              " swapped the hands";
	}
}

std::array<ActionSet::AimPose, HandCount> ActionSet::locateAim(XrSpace base, XrTime time) const {

	std::array<AimPose, HandCount> poses;
	if(m_session == XR_NULL_HANDLE || base == XR_NULL_HANDLE || time == 0 || !m_focused) {
		return poses;
	}

	const XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(m_aimSpaces[hand] == XR_NULL_HANDLE) {
			continue;
		}
		XrSpaceLocation located = { XR_TYPE_SPACE_LOCATION };
		if(XR_FAILED(xrLocateSpace(m_aimSpaces[hand], base, time, &located))
		   || (located.locationFlags & tracked) != tracked) {
			continue;
		}
		AimPose & pose = poses[hand];
		pose.valid = true;
		pose.position = Vec3f(located.pose.position.x, located.pose.position.y, located.pose.position.z);
		pose.orientation = glm::quat(located.pose.orientation.w, located.pose.orientation.x,
		                             located.pose.orientation.y, located.pose.orientation.z);
	}

	return poses;
}

void ActionSet::locateHands(XrSpace space, XrTime time, FrameHands & hands) {

	hands = FrameHands();

	if(m_session == XR_NULL_HANDLE || space == XR_NULL_HANDLE || time == 0) {
		return;
	}

	for(size_t hand = 0; hand < HandCount; hand++) {
		bool tracked = false;
		if(m_gripSpaces[hand] != XR_NULL_HANDLE) {
			XrSpaceLocation located = { XR_TYPE_SPACE_LOCATION };
			XrResult result = xrLocateSpace(m_gripSpaces[hand], space, time, &located);
			const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
			// A failed call counts as untracked: said once per episode below, like a lost controller
			if(XR_SUCCEEDED(result) && (located.locationFlags & valid) == valid) {
				tracked = true;
				hands.hands[hand].position = Vec3f(located.pose.position.x, located.pose.position.y,
				                                   located.pose.position.z);
				hands.hands[hand].orientation = glm::quat(located.pose.orientation.w, located.pose.orientation.x,
				                                          located.pose.orientation.y, located.pose.orientation.z);
			}
		}
		hands.tracked[hand] = tracked;
		// An episode, not a frame: a controller coming into tracking or leaving it
		if(tracked != m_handTracked[hand]) {
			m_handTracked[hand] = tracked;
			gameevent::Event("vr.hand", 200)("hand", handName(Hand(hand)))("state", tracked ? "tracked" : "lost");
		}
	}

}

bool ActionSet::sync(ControllerInput & input, XrSpace viewSpace, XrTime time) {

	input = ControllerInput();

	if(m_session == XR_NULL_HANDLE) {
		return false;
	}

	XrActiveActionSet active = { m_set, XR_NULL_PATH };
	XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &active;
	XrResult result = xrSyncActions(m_session, &syncInfo);

	// XR_SESSION_NOT_FOCUSED is a success code: the runtime gives no input while another app has it
	const bool focused = (result == XR_SUCCESS);
	if(XR_FAILED(result)) {
		m_syncFailures++;
		if(m_syncFailures <= MaxLoggedSyncFailures) {
			LogError << "[vr] input: xrSyncActions failed: " << resultName(result)
			         << (m_syncFailures == MaxLoggedSyncFailures ? " (further failures are not logged)" : "");
		}
	}
	if(focused != m_focused) {
		LogInfo << "[vr] input: controller input " << (focused ? "on (session focused)"
		                                                        : "off - session not focused, no input");
		m_focused = focused;
	}
	if(!focused) {
		m_down.reset();
		return false;
	}

	m_syncs++;
	if(m_profileCheckDue || m_syncs - m_lastProfileCheck >= ProfilePollSyncs) {
		updateProfiles();
	}

	ControlSet down;
	ControlSet bound;
	for(size_t i = 0; i < HandCount; i++) {

		const Hand hand = Hand(i);
		bool isActive = false;

		struct { XrAction action; ControlKind kind; const Hysteresis & threshold; float * value; } analog[] = {
			{ m_actions.trigger, ControlKind::Trigger, TriggerThreshold, &input.trigger[i] },
			// The Index grip is a light close of the strapped hand, the Touch grip a squeeze (D-019)
			{ m_actions.grip, ControlKind::Grip, gripThreshold(controllerTypeFromProfile(m_profiles[i])), &input.grip[i] },
			{ m_actions.trackpad, ControlKind::Trackpad, TrackpadThreshold, nullptr },
		};
		for(const auto & entry : analog) {
			const Control c = control(hand, entry.kind);
			float value = readFloat(entry.action, hand, isActive);
			bound[c] = isActive;
			down[c] = entry.threshold.update(m_down[c], value);
			if(entry.kind == ControlKind::Grip && down[c] != m_down[c]) {
				// The grip threshold is the one judged in the headset: every crossing with its value
				LogInfo << "[vr] input: " << handName(hand) << " grip " << (down[c] ? "down" : "up") << " at "
				        << value << " (threshold " << entry.threshold.press << " / " << entry.threshold.release << ')';
			}
			if(entry.value) {
				*entry.value = value;
			}
		}

		const struct { XrAction action; ControlKind kind; } buttons[] = {
			{ m_actions.a, ControlKind::A },
			{ m_actions.b, ControlKind::B },
			{ m_actions.stickClick, ControlKind::StickClick },
			{ m_actions.menu, ControlKind::Menu },
		};
		for(const auto & entry : buttons) {
			const Control c = control(hand, entry.kind);
			bool tapped = false;
			down[c] = readBoolean(entry.action, hand, isActive, tapped);
			bound[c] = isActive;
			input.taps[c] = tapped && !m_down[c];
		}

		XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
		info.action = m_actions.stick;
		info.subactionPath = m_hands[i];
		XrActionStateVector2f stick = { XR_TYPE_ACTION_STATE_VECTOR2F };
		const bool stickActive = XR_SUCCEEDED(xrGetActionStateVector2f(m_session, &info, &stick)) && stick.isActive;
		if(stickActive) {
			input.sticks[i].x = stick.currentState.x;
			input.sticks[i].y = stick.currentState.y;
		}
		for(ControlKind kind : { ControlKind::StickUp, ControlKind::StickDown, ControlKind::StickLeft,
		                         ControlKind::StickRight }) {
			const Control c = control(hand, kind);
			down[c] = m_down[c];
			bound[c] = stickActive;
		}
		updateStickDirections(down, hand, input.sticks[i].x, input.sticks[i].y);
	}

	m_down = down;
	input.down = down;

	// "Bound" is what the runtime reports active, not what was suggested (F-123)
	if(bound != m_bound || !m_loggedBound) {
		std::string names;
		for(size_t c = 0; c < ControlCount; c++) {
			if(bound[c]) {
				names += ' ' + controlName(Control(c));
			}
		}
		if(names.empty()) {
			LogInfo << "[vr] input: no controls bound by the runtime yet";
		} else {
			LogInfo << "[vr] input: " << bound.count() << " controls bound by the runtime:" << names;
		}
		m_bound = bound;
		m_loggedBound = true;
	}

	checkHandedness(viewSpace, time);

	return true;
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
