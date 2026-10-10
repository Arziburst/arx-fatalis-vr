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

#ifndef ARX_VR_VRBINDINGS_H
#define ARX_VR_VRBINDINGS_H

#include <string>
#include <vector>

#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief The suggested bindings per interaction profile (#35, D-019)
 *
 * Which input path of each supported controller drives which of the game's OpenXR actions
 * (VRActions.h). Pure data - no OpenXR - so tests/vr/VRBindingsTest checks every path against
 * the profiles' paths in the OpenXR spec and against the controls the default layouts use:
 * a runtime rejects a profile's whole list for one unknown path, silently (F-123).
 */
namespace vr {

//! The game's OpenXR actions: one per physical control, each with a left and a right subaction
enum class PhysicalAction : u8 {
	Trigger,
	Grip,
	A,
	B,
	Stick,
	StickClick,
	Trackpad,
	Menu,
	GripPose,
	AimPose,
	Haptic,
	ThumbTouch, //!< the thumb rests on a thumbstick, trackpad or face button: closes the model's thumb (#305), no gameplay event
	Count
};

const size_t PhysicalActionCount = size_t(PhysicalAction::Count);

//! One suggested binding: an action of one hand and the component path under that hand
struct SuggestedBinding {
	PhysicalAction action;
	Hand hand;
	const char * component; //!< e.g. "input/trigger/value"
	bool optional;          //!< left out when the runtime rejects the profile's list with it
};

//! "/user/hand/left" or "/user/hand/right"
const char * handPath(Hand hand);

//! The full path of a suggestion, e.g. "/user/hand/left/input/trigger/value"
std::string bindingPath(const SuggestedBinding & binding);

//! The interaction profile path of a controller type, nullptr for \ref ControllerType::Unknown
const char * interactionProfilePath(ControllerType type);

//! The controller types we suggest bindings for, in the order they are suggested
const std::vector<ControllerType> & suggestedControllerTypes();

//! The suggested bindings of a controller type (empty for \ref ControllerType::Unknown)
std::vector<SuggestedBinding> suggestedBindings(ControllerType type);

/*!
 * The controls (VRControls.h) the suggestions of a controller type give the game: a bound
 * thumbstick gives its four directions; optional paths count.
 */
ControlSet suggestedControls(ControllerType type);

} // namespace vr

#endif // ARX_VR_VRBINDINGS_H
