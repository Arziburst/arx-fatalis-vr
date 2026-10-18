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

#include "vr/MenuReachTest.h"

#include <initializer_list>
#include <string>
#include <vector>

#include "vr/VRControls.h"
#include "vr/VRGameInput.h"

CppUnit::AutoRegisterSuite<MenuReachTest> g_registerMenuReachTest;

using namespace vr;

namespace {

const Control MrLeftMenu = control(Hand::Left, ControlKind::Menu);
const Control MrRightMenu = control(Hand::Right, ControlKind::Menu);
const Control MrLeftTrigger = control(Hand::Left, ControlKind::Trigger);
const Control MrLeftTrackpad = control(Hand::Left, ControlKind::Trackpad);
const Control MrLeftB = control(Hand::Left, ControlKind::B);
const Control MrRightA = control(Hand::Right, ControlKind::A);
const Control MrRightB = control(Hand::Right, ControlKind::B);
const Control MrRightGrip = control(Hand::Right, ControlKind::Grip);

/*
 * The contract's default fallback (#326; another button or hold time is the owner's choice, D-017): the left
 * controller's menu button held about 2 s. "About": the menu opens after 1.5 s of holding and by 2.1 s
 */
const s64 FallbackNotBeforeMs = 1500;
const s64 FallbackByMs = 2100;

ControlSet mrSet(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

//! The resolver and the mapper of one frame after another, as VRInput.cpp runs them (90 Hz)
struct MrFrames {

	ControlResolver resolver;
	GameInputMapper mapper;
	ControllerInput input;
	HandRoles roles;
	s64 time = 1000;

	explicit MrFrames(const ControlLayout & layout) : resolver(layout) { }

	const GameInputState & frame(const ControlSet & down, const ControlSet & taps = ControlSet()) {
		time += 11;
		resolver.update(time, down, taps);
		return mapper.update(resolver, input, roles);
	}

};

//! What the game menu key (Escape) did while controls were held and in the second after they were let go
struct MenuKey {
	s64 firstMs = -1;       //!< when Escape first came on, counted from the press; -1: never
	u32 presses = 0;        //!< how often Escape came on
	bool onAfter = false;   //!< Escape still on a second after the release
};

MenuKey holdFor(MrFrames & f, const ControlSet & down, s64 durationMs, const ControlSet & taps = ControlSet()) {

	MenuKey key;
	bool was = f.mapper.state().escape;
	const s64 start = f.time;
	const auto note = [&](const GameInputState & state) {
		if(state.escape && !was) {
			key.presses++;
			if(key.firstMs < 0) {
				key.firstMs = f.time - start;
			}
		}
		was = state.escape;
	};

	note(f.frame(down, taps));
	while(f.time - start < durationMs) {
		note(f.frame(down));
	}
	const s64 released = f.time;
	while(f.time - released < 1000) {
		note(f.frame(ControlSet()));
	}
	key.onAfter = f.mapper.state().escape;
	return key;
}

std::string at(const std::string & name, const char * what) {
	return name + ": " + what;
}

void assertLongPressOpensMenu(const std::string & name, const ControlLayout & layout) {

	MrFrames f(layout);

	// Two long presses one after the other: each opens the game menu once, at about 2 s, and lets it go after
	for(int round = 0; round < 2; round++) {
		const MenuKey key = holdFor(f, mrSet({ MrLeftMenu }), 3000);
		CPPUNIT_ASSERT_MESSAGE(at(name, "a 3 s hold of the left menu button opens the game menu"), key.presses >= 1);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... once, not repeatedly while held"), key.presses == 1);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... not before about 2 s of holding"), key.firstMs >= FallbackNotBeforeMs);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... by about 2 s of holding"), key.firstMs <= FallbackByMs);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... and the game menu key is let go after the release"), !key.onAfter);
	}
}

//! The Index default with no way to the game menu left: no menu event, no radial menu (D-042: the radial is the way)
ControlLayout lockedOutLayout() {
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::Menu] = Binding();
	layout[Event::RadialLeft] = Binding();
	layout[Event::RadialRight] = Binding();
	return layout;
}

//! Whether some binding leads to the game menu (D-042): the menu event or a radial menu event
bool menuWayBound(const ControlLayout & layout) {
	return layout[Event::Menu].isBound() || layout[Event::RadialLeft].isBound() || layout[Event::RadialRight].isBound();
}

ControlLayout readSaved(const std::string & text, ControllerType type) {
	ControlLayout layout;
	std::string error;
	readSavedLayout(text, type, layout, &error);
	return layout;
}

} // anonymous namespace

void MenuReachTest::fallbackOpensMenuTest() {

	// Whatever the bindings are (#326): nothing bound at all, the defaults with no binding on the left menu button,
	// no way to the game menu left, and the owner's case - the way moved to bindings he does not know
	ControlLayout moved = defaultLayout(ControllerType::Index);
	moved[Event::RadialRight] = Binding::hold(MrLeftTrackpad);
	moved[Event::Menu] = Binding::chord(MrRightGrip, MrRightA);

	assertLongPressOpensMenu("no binding at all", ControlLayout());
	assertLongPressOpensMenu("the Index default", defaultLayout(ControllerType::Index));
	assertLongPressOpensMenu("the Touch default", defaultLayout(ControllerType::Touch));
	assertLongPressOpensMenu("the Index default without a way to the menu", lockedOutLayout());
	assertLongPressOpensMenu("the menu's way moved where the player does not know", moved);
}

void MenuReachTest::fallbackNotRebindableTest() {

	// The player bound the left menu button to other events - a single press, a long press of the layout's own
	// (500 ms) and a chord led by it - and the menu event to another button: the fixed long press still opens the menu
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::Jump] = Binding::single(MrLeftMenu);
	layout[Event::Recenter] = Binding::hold(MrLeftMenu);
	layout[Event::Book] = Binding::chord(MrLeftMenu, MrLeftTrigger);
	layout[Event::Menu] = Binding::single(MrRightA);
	assertLongPressOpensMenu("the left menu button bound to other events", layout);

	// The simple controller's default: the left menu button is the menu on a tap and recenter on a long press (500 ms).
	// Held on, past the recenter, the fixed long press still opens the game menu
	assertLongPressOpensMenu("the simple controller's default", defaultLayout(ControllerType::Simple));
}

void MenuReachTest::fallbackShortPressTest() {

	// A short press of the left menu button is no long press: with no binding on it nothing opens the game menu
	for(const ControlLayout & layout : { lockedOutLayout(), defaultLayout(ControllerType::Index) }) {

		MrFrames f(layout);

		// Pressed and released between two updates
		MenuKey key = holdFor(f, ControlSet(), 0, mrSet({ MrLeftMenu }));
		CPPUNIT_ASSERT_EQUAL_MESSAGE("a tap of the left menu button opens no menu", u32(0), key.presses);

		key = holdFor(f, mrSet({ MrLeftMenu }), 300);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("a 0.3 s press of the left menu button opens no menu", u32(0), key.presses);

		key = holdFor(f, mrSet({ MrLeftMenu }), 1200);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("a 1.2 s press of the left menu button opens no menu", u32(0), key.presses);

		// The fallback is the LEFT controller's menu button (the contract; another button is the owner's choice)
		key = holdFor(f, mrSet({ MrRightMenu }), 3000);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("a long press of the right menu button is no fallback", u32(0), key.presses);
	}
}

void MenuReachTest::savedLayoutKeepsMenuTest() {

	const std::vector<ControllerType> types = {
		ControllerType::Index, ControllerType::Touch, ControllerType::Simple, ControllerType::Unknown
	};

	for(ControllerType type : types) {

		const ControlLayout base = defaultLayout(type);
		const std::string name = controllerTypeName(type);

		// An empty file, a broken one (a line without "="), one with an unknown event: the default layout
		CPPUNIT_ASSERT_MESSAGE(at(name, "an empty controls file gives the default layout"), readSaved("", type) == base);
		CPPUNIT_ASSERT_MESSAGE(at(name, "a broken controls file gives the default layout"),
		                       readSaved("jump = right.b\nthis is no binding\n", type) == base);
		CPPUNIT_ASSERT_MESSAGE(at(name, "a controls file with an unknown event gives the default layout"),
		                       readSaved("no_such_event = right.b\n", type) == base);

		// A file without the menu's lines: the default way to the menu
		const ControlLayout jump = readSaved("jump = left.trackpad\n", type);
		CPPUNIT_ASSERT_MESSAGE(at(name, "a file without the menu keeps the default menu binding"),
		                       jump[Event::Menu] == base[Event::Menu] && jump[Event::RadialRight] == base[Event::RadialRight]);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... and the file's own change"), jump[Event::Jump] == Binding::single(MrLeftTrackpad));
	}

	// A file that moves the way to the menu elsewhere keeps it there: the menu is still reachable, the player chose it
	ControlLayout layout = readSaved("radial_right = left.trackpad\n", ControllerType::Index);
	CPPUNIT_ASSERT(layout[Event::RadialRight] == Binding::single(MrLeftTrackpad));
	layout = readSaved("radial_right = none\nradial_left = left.b\n", ControllerType::Index);
	CPPUNIT_ASSERT(layout[Event::RadialLeft] == Binding::single(MrLeftB));
	CPPUNIT_ASSERT(!layout[Event::RadialRight].isBound());
	layout = readSaved("radial_right = none\nmenu = left.trackpad:long\n", ControllerType::Index);
	CPPUNIT_ASSERT(layout[Event::Menu] == Binding::hold(MrLeftTrackpad));
	layout = readSaved("menu = right.menu:long\nbook = none\n", ControllerType::Simple);
	CPPUNIT_ASSERT(layout[Event::Menu] == Binding::hold(MrRightMenu));
}

void MenuReachTest::savedLayoutRestoresMenuTest() {

	// A controls file read at a start that leaves no way to the game menu gives the default way back (#326)
	ControlLayout layout = readSaved("jump = right.b\nradial_right = none\n", ControllerType::Index);
	CPPUNIT_ASSERT_MESSAGE("index: radial_right = none - the right B's radial menu (the way to the menu) is back",
	                       layout[Event::RadialRight] == Binding::single(MrRightB));

	for(ControllerType type : { ControllerType::Index, ControllerType::Touch, ControllerType::Unknown }) {
		const std::string name = controllerTypeName(type);
		layout = readSaved("menu = none\nradial_left = none\nradial_right = none\n", type);
		CPPUNIT_ASSERT_MESSAGE(at(name, "no menu, no radial menu - the default way to the menu is back"),
		                       layout[Event::RadialRight] == defaultLayout(type)[Event::RadialRight]);
		CPPUNIT_ASSERT_MESSAGE(at(name, "... so a binding leads to the menu"), menuWayBound(layout));
	}

	layout = readSaved("menu = none\n", ControllerType::Simple);
	CPPUNIT_ASSERT_MESSAGE("simple: menu = none - the left menu button is the menu again",
	                       layout[Event::Menu] == Binding::single(MrLeftMenu));
}
