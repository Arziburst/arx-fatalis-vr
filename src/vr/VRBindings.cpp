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

#include "vr/VRBindings.h"

namespace vr {

namespace {

void addBoth(std::vector<SuggestedBinding> & list, PhysicalAction action, const char * component,
             bool optional = false) {
	list.push_back({ action, Hand::Left, component, optional });
	list.push_back({ action, Hand::Right, component, optional });
}

void addOne(std::vector<SuggestedBinding> & list, PhysicalAction action, Hand hand, const char * component) {
	list.push_back({ action, hand, component, false });
}

//! The poses and the haptics, the same path on every profile
void addPosesAndHaptics(std::vector<SuggestedBinding> & list) {
	addBoth(list, PhysicalAction::GripPose, "input/grip/pose");
	addBoth(list, PhysicalAction::AimPose, "input/aim/pose");
	addBoth(list, PhysicalAction::Haptic, "output/haptic");
}

} // anonymous namespace

const char * handPath(Hand hand) {
	return hand == Hand::Left ? "/user/hand/left" : "/user/hand/right";
}

std::string bindingPath(const SuggestedBinding & binding) {
	return std::string(handPath(binding.hand)) + '/' + binding.component;
}

const char * interactionProfilePath(ControllerType type) {
	switch(type) {
		case ControllerType::Index: return "/interaction_profiles/valve/index_controller";
		case ControllerType::Touch: return "/interaction_profiles/oculus/touch_controller";
		case ControllerType::Simple: return "/interaction_profiles/khr/simple_controller";
		case ControllerType::Unknown: return nullptr;
	}
	return nullptr;
}

const std::vector<ControllerType> & suggestedControllerTypes() {
	static const std::vector<ControllerType> types = {
		ControllerType::Index, ControllerType::Touch, ControllerType::Simple
	};
	return types;
}

std::vector<SuggestedBinding> suggestedBindings(ControllerType type) {

	std::vector<SuggestedBinding> list;

	switch(type) {

		case ControllerType::Index: {
			// The owner's controllers (D-019). The grip is squeeze/value, how far the hand is closed: the
			// controller is strapped on, a light close is the grip and an open hand is none (own threshold,
			// F-194 decided for the value, not squeeze/force). No menu button: the game menu is on left A
			addBoth(list, PhysicalAction::Trigger, "input/trigger/value");
			addBoth(list, PhysicalAction::Grip, "input/squeeze/value");
			addBoth(list, PhysicalAction::A, "input/a/click");
			addBoth(list, PhysicalAction::B, "input/b/click");
			addBoth(list, PhysicalAction::Stick, "input/thumbstick");
			addBoth(list, PhysicalAction::StickClick, "input/thumbstick/click");
			addBoth(list, PhysicalAction::Trackpad, "input/trackpad/force", true);
			// The thumb's rest (#305): the stick, where the thumb lies while walking or turning (one path
			// per action and hand - the runtime would OR several, the game reads one control)
			addBoth(list, PhysicalAction::ThumbTouch, "input/thumbstick/touch");
			addPosesAndHaptics(list);
			break;
		}

		case ControllerType::Touch: {
			// Quest 3 Touch Plus through SteamVR (it has no meta/touch_controller_plus): X / Y on the left
			// are the A / B actions, so D-019's positions hold; the menu button exists on the left only
			addBoth(list, PhysicalAction::Trigger, "input/trigger/value");
			addBoth(list, PhysicalAction::Grip, "input/squeeze/value");
			addOne(list, PhysicalAction::A, Hand::Left, "input/x/click");
			addOne(list, PhysicalAction::B, Hand::Left, "input/y/click");
			addOne(list, PhysicalAction::A, Hand::Right, "input/a/click");
			addOne(list, PhysicalAction::B, Hand::Right, "input/b/click");
			addBoth(list, PhysicalAction::Stick, "input/thumbstick");
			addBoth(list, PhysicalAction::StickClick, "input/thumbstick/click");
			addOne(list, PhysicalAction::Menu, Hand::Left, "input/menu/click");
			// The thumb's rest (#305): the stick, as on the Index
			addBoth(list, PhysicalAction::ThumbTouch, "input/thumbstick/touch");
			addPosesAndHaptics(list);
			break;
		}

		case ControllerType::Simple: {
			// The minimal fallback: select works as the trigger, menu on both hands
			addBoth(list, PhysicalAction::Trigger, "input/select/click");
			addBoth(list, PhysicalAction::Menu, "input/menu/click");
			addPosesAndHaptics(list);
			break;
		}

		case ControllerType::Unknown: break;
	}

	return list;
}

ControlSet suggestedControls(ControllerType type) {

	ControlSet controls;

	for(const SuggestedBinding & binding : suggestedBindings(type)) {
		const Hand hand = binding.hand;
		switch(binding.action) {
			case PhysicalAction::Trigger: controls.set(control(hand, ControlKind::Trigger)); break;
			case PhysicalAction::Grip: controls.set(control(hand, ControlKind::Grip)); break;
			case PhysicalAction::A: controls.set(control(hand, ControlKind::A)); break;
			case PhysicalAction::B: controls.set(control(hand, ControlKind::B)); break;
			case PhysicalAction::Stick: {
				for(ControlKind kind : { ControlKind::StickUp, ControlKind::StickDown, ControlKind::StickLeft,
				                         ControlKind::StickRight }) {
					controls.set(control(hand, kind));
				}
				break;
			}
			case PhysicalAction::StickClick: controls.set(control(hand, ControlKind::StickClick)); break;
			case PhysicalAction::Trackpad: controls.set(control(hand, ControlKind::Trackpad)); break;
			case PhysicalAction::Menu: controls.set(control(hand, ControlKind::Menu)); break;
			case PhysicalAction::GripPose:
			case PhysicalAction::AimPose:
			case PhysicalAction::Haptic:
			case PhysicalAction::ThumbTouch: // the fingers' input, not a control of the game's table
			case PhysicalAction::Count: break;
		}
	}

	return controls;
}

} // namespace vr
