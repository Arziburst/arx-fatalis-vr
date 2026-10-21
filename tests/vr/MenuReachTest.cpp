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

const Control MrLeftStickClick = control(Hand::Left, ControlKind::StickClick);
const Control MrRightStickClick = control(Hand::Right, ControlKind::StickClick);
const Control MrLeftMenu = control(Hand::Left, ControlKind::Menu);
const Control MrRightMenu = control(Hand::Right, ControlKind::Menu);
const Control MrLeftTrigger = control(Hand::Left, ControlKind::Trigger);
const Control MrLeftTrackpad = control(Hand::Left, ControlKind::Trackpad);
const Control MrLeftB = control(Hand::Left, ControlKind::B);
const Control MrRightA = control(Hand::Right, ControlKind::A);
const Control MrRightB = control(Hand::Right, ControlKind::B);
const Control MrRightGrip = control(Hand::Right, ControlKind::Grip);

/*
 * The fallback of #326 (both thumbsticks held together for 2 s opened the game menu) is removed by #551 (the owner,
 * 2026-10-19/20): the hold of both sticks does nothing special (the mark is both trackpads, MarkMomentTest.h), the
 * menu is reached through the radial menu. How long the tests hold both stick clicks: past the old 2 s and past 3 s
 */
const s64 LongHoldMs = 3000;
const s64 LongerHoldMs = 5000;

ControlSet mrSet(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

const ControlSet BothSticks = mrSet({ MrLeftStickClick, MrRightStickClick });

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

/*!
 * What the game menu key (Escape) did. The engine opens the game menu on Escape's RELEASE (VRGameInput.h), so "the
 * menu opens at 2 s while the sticks are held" is Escape on and off again before the sticks are let go
 */
struct MenuKey {
	s64 firstMs = -1;       //!< when Escape first came on, counted from when all of \a down were held; -1: never
	s64 offMs = -1;         //!< when it first went off again after that, counted the same way; -1: never
	u32 presses = 0;        //!< how often Escape came on (the lead, the hold and the second after the release)
	bool onAfter = false;   //!< Escape still on a second after the release
};

/*!
 * Hold \a lead for \a leadMs (one stick before the other), then \a down for \a durationMs (\a taps: pressed and
 * released between the first two updates), then nothing for a second
 */
MenuKey holdFor(MrFrames & f, const ControlSet & down, s64 durationMs, const ControlSet & taps = ControlSet(),
                const ControlSet & lead = ControlSet(), s64 leadMs = 0) {

	MenuKey key;
	bool was = f.mapper.state().escape;
	s64 start = f.time;
	const auto note = [&](const GameInputState & state) {
		if(state.escape && !was) {
			key.presses++;
			if(key.firstMs < 0) {
				key.firstMs = f.time - start;
			}
		}
		if(!state.escape && was && key.firstMs >= 0 && key.offMs < 0) {
			key.offMs = f.time - start;
		}
		was = state.escape;
	};

	const s64 leadStart = f.time;
	while(f.time - leadStart < leadMs) {
		note(f.frame(lead));
	}

	start = f.time + 11; // the next frame is the first with all of down held
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

void assertNoMenu(const std::string & name, const MenuKey & key) {
	CPPUNIT_ASSERT_EQUAL_MESSAGE(at(name, "both stick clicks held together open no game menu (the D-058 fallback is gone, #551)"),
	                             u32(0), key.presses);
	CPPUNIT_ASSERT_MESSAGE(at(name, "... and the game menu key is off after the release"), !key.onAfter);
}

void assertBothSticksOpenNoMenu(const std::string & name, const ControlLayout & layout) {

	MrFrames f(layout);

	// Past the old 2 s, past 3 s, twice; one stick 0.4 s before the other
	assertNoMenu(name + " (both pressed together, 3 s)", holdFor(f, BothSticks, LongHoldMs));
	assertNoMenu(name + " (again, 5 s)", holdFor(f, BothSticks, LongerHoldMs));
	assertNoMenu(name + " (left first)",
	             holdFor(f, BothSticks, LongHoldMs, ControlSet(), mrSet({ MrLeftStickClick }), 400));
	assertNoMenu(name + " (right first)",
	             holdFor(f, BothSticks, LongHoldMs, ControlSet(), mrSet({ MrRightStickClick }), 400));
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

void MenuReachTest::bothSticksOpenNoMenuTest() {

	// Whatever the bindings are (#551 removes the fallback of #326): nothing bound at all, the defaults (the right
	// stick click draws the weapon), no way to the game menu left, and the way moved to bindings the player does not
	// know - the hold of both stick clicks never presses the game menu key
	ControlLayout moved = defaultLayout(ControllerType::Index);
	moved[Event::RadialRight] = Binding::hold(MrLeftTrackpad);
	moved[Event::Menu] = Binding::chord(MrRightGrip, MrRightA);

	assertBothSticksOpenNoMenu("no binding at all", ControlLayout());
	assertBothSticksOpenNoMenu("the Index default", defaultLayout(ControllerType::Index));
	assertBothSticksOpenNoMenu("the Touch default", defaultLayout(ControllerType::Touch));
	assertBothSticksOpenNoMenu("the Index default without a way to the menu", lockedOutLayout());
	assertBothSticksOpenNoMenu("the menu's way moved where the player does not know", moved);
}

void MenuReachTest::bothSticksBoundOpenNoMenuTest() {

	// The player bound both stick clicks to other events - a single press, a long press of the layout's own (500 ms),
	// a chord led by one - and the menu event to another button: the hold of both opens no menu either
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::Jump] = Binding::single(MrLeftStickClick);
	layout[Event::Recenter] = Binding::hold(MrLeftStickClick);
	layout[Event::Book] = Binding::chord(MrLeftStickClick, MrLeftTrigger);
	layout[Event::MagicMode] = Binding::hold(MrRightStickClick);
	layout[Event::Menu] = Binding::single(MrRightA);
	assertBothSticksOpenNoMenu("both stick clicks bound to other events", layout);
}

void MenuReachTest::fallbackShortPressTest() {

	// One stick alone, or both for a short time, is no fallback: nothing opens the game menu
	for(const ControlLayout & layout : { lockedOutLayout(), defaultLayout(ControllerType::Index) }) {

		MrFrames f(layout);

		MenuKey key = holdFor(f, mrSet({ MrLeftStickClick }), LongHoldMs);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("the left stick click alone held 3 s opens no menu", u32(0), key.presses);

		key = holdFor(f, mrSet({ MrRightStickClick }), LongHoldMs);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("the right stick click alone held 3 s opens no menu", u32(0), key.presses);

		// Both pressed and released between two updates, then both for 0.3 s
		key = holdFor(f, ControlSet(), 0, BothSticks);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("a tap of both stick clicks opens no menu", u32(0), key.presses);
		key = holdFor(f, BothSticks, 300);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks for 0.3 s open no menu", u32(0), key.presses);

		key = holdFor(f, BothSticks, 1200);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks for 1.2 s open no menu", u32(0), key.presses);

		// The left held 3 s, the right only tapped after 1 s: both together only for a moment
		key = holdFor(f, mrSet({ MrLeftStickClick }), 2000, mrSet({ MrRightStickClick }), mrSet({ MrLeftStickClick }), 1000);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("one stick held and the other tapped opens no menu", u32(0), key.presses);
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
