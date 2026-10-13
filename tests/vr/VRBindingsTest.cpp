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

#include "vr/VRBindingsTest.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "vr/VRBindings.h"
#include "vr/VRControls.h"

CppUnit::AutoRegisterSuite<VRBindingsTest> g_registerVRBindingsTest;

using namespace vr;

namespace {

/*!
 * The input and output paths of a profile, per hand, as the OpenXR 1.0 spec lists them
 * ("6.4 Interaction Profiles"). A thumbstick or trackpad parent ("input/thumbstick") is the
 * spec's two-axis identifier for a vector2f action and is valid where its /x and /y are.
 */
std::set<std::string> specPaths(ControllerType type, Hand hand) {

	std::vector<const char *> components;

	switch(type) {

		case ControllerType::Simple: {
			components = { "input/select/click", "input/menu/click" };
			break;
		}

		case ControllerType::Index: {
			components = {
				"input/system/click", "input/system/touch",
				"input/a/click", "input/a/touch", "input/b/click", "input/b/touch",
				"input/squeeze/value", "input/squeeze/force",
				"input/trigger/click", "input/trigger/value", "input/trigger/touch",
				"input/thumbstick", "input/thumbstick/x", "input/thumbstick/y", "input/thumbstick/click",
				"input/thumbstick/touch",
				"input/trackpad", "input/trackpad/x", "input/trackpad/y", "input/trackpad/force",
				"input/trackpad/touch",
			};
			break;
		}

		case ControllerType::Touch: {
			if(hand == Hand::Left) {
				components = { "input/x/click", "input/x/touch", "input/y/click", "input/y/touch", "input/menu/click" };
			} else {
				components = { "input/a/click", "input/a/touch", "input/b/click", "input/b/touch", "input/system/click" };
			}
			for(const char * component : {
				"input/squeeze/value", "input/trigger/value", "input/trigger/touch",
				"input/thumbstick", "input/thumbstick/x", "input/thumbstick/y", "input/thumbstick/click",
				"input/thumbstick/touch", "input/thumbrest/touch",
			}) {
				components.push_back(component);
			}
			break;
		}

		case ControllerType::Unknown: break;
	}

	if(type != ControllerType::Unknown) {
		for(const char * component : { "input/grip/pose", "input/aim/pose", "output/haptic" }) {
			components.push_back(component);
		}
	}

	std::set<std::string> paths;
	for(const char * component : components) {
		paths.insert(std::string(handPath(hand)) + '/' + component);
	}
	return paths;
}

bool endsWith(const std::string & text, const std::string & suffix) {
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // anonymous namespace

void VRBindingsTest::profilePathTest() {

	CPPUNIT_ASSERT(interactionProfilePath(ControllerType::Unknown) == nullptr);
	CPPUNIT_ASSERT(suggestedBindings(ControllerType::Unknown).empty());

	CPPUNIT_ASSERT_EQUAL(size_t(3), suggestedControllerTypes().size());
	for(ControllerType type : suggestedControllerTypes()) {
		// The path we suggest for is the one the profile detection reads back
		CPPUNIT_ASSERT(interactionProfilePath(type) != nullptr);
		CPPUNIT_ASSERT(controllerTypeFromProfile(interactionProfilePath(type)) == type);
	}

	CPPUNIT_ASSERT_EQUAL(std::string("/user/hand/right/input/a/click"),
	                     bindingPath({ PhysicalAction::A, Hand::Right, "input/a/click", false }));
}

void VRBindingsTest::specPathTest() {

	for(ControllerType type : suggestedControllerTypes()) {
		for(const SuggestedBinding & binding : suggestedBindings(type)) {

			const std::string path = bindingPath(binding);
			const std::string where = std::string(controllerTypeName(type)) + ": " + path;

			// One unknown path makes the runtime reject the profile's whole list (F-123)
			CPPUNIT_ASSERT_MESSAGE(where + " is not a path of the profile",
			                       specPaths(type, binding.hand).count(path) == 1);

			// The action type fits the path: a pose on a pose, haptics on an output, a vector on a 2D parent
			const bool isPose = endsWith(path, "/pose");
			const bool isOutput = path.find("/output/") != std::string::npos;
			const bool is2D = endsWith(path, "/thumbstick") || endsWith(path, "/trackpad");
			switch(binding.action) {
				case PhysicalAction::GripPose: CPPUNIT_ASSERT_MESSAGE(where, endsWith(path, "/grip/pose")); break;
				case PhysicalAction::AimPose: CPPUNIT_ASSERT_MESSAGE(where, endsWith(path, "/aim/pose")); break;
				case PhysicalAction::Haptic: CPPUNIT_ASSERT_MESSAGE(where, isOutput); break;
				case PhysicalAction::Stick: CPPUNIT_ASSERT_MESSAGE(where, is2D); break;
				default: CPPUNIT_ASSERT_MESSAGE(where, !isPose && !isOutput && !is2D); break;
			}
		}
	}
}

void VRBindingsTest::noDuplicateTest() {

	for(ControllerType type : suggestedControllerTypes()) {
		std::set<std::pair<PhysicalAction, std::string>> seen;
		std::set<std::pair<PhysicalAction, Hand>> actions;
		for(const SuggestedBinding & binding : suggestedBindings(type)) {
			const std::string where = std::string(controllerTypeName(type)) + ": " + bindingPath(binding);
			CPPUNIT_ASSERT_MESSAGE(where + " suggested twice", seen.insert({ binding.action, bindingPath(binding) }).second);
			// One path per action and hand: two would be combined by the runtime, the game reads one control
			CPPUNIT_ASSERT_MESSAGE(where + ": a second path for the action", actions.insert({ binding.action, binding.hand }).second);
		}
	}
}

void VRBindingsTest::everyActionBoundTest() {

	// Hands and poses are what the game needs from any controller; the Index only is optional
	for(ControllerType type : suggestedControllerTypes()) {
		for(Hand hand : { Hand::Left, Hand::Right }) {
			std::set<PhysicalAction> bound;
			for(const SuggestedBinding & binding : suggestedBindings(type)) {
				if(binding.hand == hand) {
					bound.insert(binding.action);
					CPPUNIT_ASSERT(!binding.optional || type == ControllerType::Index);
				}
			}
			for(PhysicalAction action : { PhysicalAction::Trigger, PhysicalAction::GripPose, PhysicalAction::AimPose,
			                              PhysicalAction::Haptic }) {
				CPPUNIT_ASSERT_MESSAGE(std::string(controllerTypeName(type)) + " " + handName(hand),
				                       bound.count(action) == 1);
			}
		}
	}

	// The Index and the Touch give every control D-019 uses on both hands
	for(ControllerType type : { ControllerType::Index, ControllerType::Touch }) {
		const ControlSet controls = suggestedControls(type);
		for(Hand hand : { Hand::Left, Hand::Right }) {
			for(ControlKind kind : { ControlKind::Trigger, ControlKind::Grip, ControlKind::A, ControlKind::B,
			                         ControlKind::StickClick, ControlKind::StickUp, ControlKind::StickDown,
			                         ControlKind::StickLeft, ControlKind::StickRight }) {
				CPPUNIT_ASSERT_MESSAGE(controlName(control(hand, kind)), controls[control(hand, kind)]);
			}
		}
	}

	// The Index trackpad is the only optional path (F-123: retried without it)
	CPPUNIT_ASSERT(suggestedControls(ControllerType::Index)[control(Hand::Left, ControlKind::Trackpad)]);
	CPPUNIT_ASSERT(!suggestedControls(ControllerType::Index)[control(Hand::Left, ControlKind::Menu)]);
	CPPUNIT_ASSERT(suggestedControls(ControllerType::Touch)[control(Hand::Left, ControlKind::Menu)]);
	CPPUNIT_ASSERT(!suggestedControls(ControllerType::Touch)[control(Hand::Right, ControlKind::Menu)]);
}

void VRBindingsTest::layoutCoverageTest() {

	// A default layout never waits for a control its profile does not give
	for(ControllerType type : suggestedControllerTypes()) {
		const ControlSet controls = suggestedControls(type);
		const ControlLayout layout = defaultLayout(type);
		for(size_t i = 0; i < EventCount; i++) {
			const Binding & binding = layout.bindings[i];
			const std::string where = std::string(controllerTypeName(type)) + ": " + eventName(Event(i)) + " = "
			                          + formatBinding(binding);
			if(binding.isBound()) {
				CPPUNIT_ASSERT_MESSAGE(where, controls[binding.first]);
			}
			if(binding.isChord()) {
				CPPUNIT_ASSERT_MESSAGE(where, controls[binding.second]);
			}
		}
		CPPUNIT_ASSERT_MESSAGE(controllerTypeName(type), findConflicts(layout).empty());
	}
}

void VRBindingsTest::defaultLayoutsTest() {

	// Every event of the D-019 table on the Index and the Touch (magic mode since #43), except the inventory and
	// the book: since D-042 (#376) they are items of the radial menus on B, with no control of their own
	for(ControllerType type : { ControllerType::Index, ControllerType::Touch }) {
		const ControlLayout layout = defaultLayout(type);
		for(size_t i = 0; i < EventCount; i++) {
			const bool inRadialMenu = (Event(i) == Event::Inventory || Event(i) == Event::Book);
			CPPUNIT_ASSERT_MESSAGE(std::string(controllerTypeName(type)) + ": " + eventName(Event(i)),
			                       layout.bindings[i].isBound() != inRadialMenu);
		}
		CPPUNIT_ASSERT_EQUAL(std::string("left.b"), formatBinding(layout[Event::RadialLeft]));
		CPPUNIT_ASSERT_EQUAL(std::string("right.b"), formatBinding(layout[Event::RadialRight]));
	}

	// The Touch is the Index with the game menu on its menu button and magic mode on the X it leaves free
	// (the Touch has no trackpad)
	const ControlLayout index = defaultLayout(ControllerType::Index);
	const ControlLayout touch = defaultLayout(ControllerType::Touch);
	for(size_t i = 0; i < EventCount; i++) {
		if(Event(i) != Event::Menu && Event(i) != Event::MagicMode) {
			CPPUNIT_ASSERT_MESSAGE(eventName(Event(i)), index.bindings[i] == touch.bindings[i]);
		}
	}
	CPPUNIT_ASSERT_EQUAL(std::string("left.a"), formatBinding(index[Event::Menu]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.menu"), formatBinding(touch[Event::Menu]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.stick_click:long"), formatBinding(touch[Event::Recenter]));

	// The simple controller: select clicks, the menu buttons carry the menus and recenter
	const ControlLayout simple = defaultLayout(ControllerType::Simple);
	CPPUNIT_ASSERT_EQUAL(std::string("left.trigger"), formatBinding(simple[Event::InteractLeft]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.trigger"), formatBinding(simple[Event::InteractRight]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.menu"), formatBinding(simple[Event::Menu]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.menu:long"), formatBinding(simple[Event::Recenter]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.menu"), formatBinding(simple[Event::Inventory]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.menu:long"), formatBinding(simple[Event::Book]));
	CPPUNIT_ASSERT(!simple[Event::Jump].isBound());
	CPPUNIT_ASSERT(!simple[Event::UseRight].isBound());

	// The game menu and the click are reachable on every controller (F-173: never stuck outside the menus)
	for(ControllerType type : { ControllerType::Unknown, ControllerType::Index, ControllerType::Touch,
	                            ControllerType::Simple }) {
		const ControlLayout layout = defaultLayout(type);
		CPPUNIT_ASSERT(layout[Event::Menu].isBound());
		CPPUNIT_ASSERT(layout[Event::InteractRight].isBound());
		CPPUNIT_ASSERT(layout[Event::Recenter].isBound());
	}
}
