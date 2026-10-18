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

#include "vr/MenuRebindTest.h"

#include <initializer_list>
#include <string>

#include "vr/VRControls.h"
#include "vr/VRGameInput.h"

CppUnit::AutoRegisterSuite<MenuRebindTest> g_registerMenuRebindTest;

using namespace vr;

namespace {

const Control RbLeftA = control(Hand::Left, ControlKind::A);
const Control RbLeftMenu = control(Hand::Left, ControlKind::Menu);
const Control RbLeftTrackpad = control(Hand::Left, ControlKind::Trackpad);
const Control RbRightB = control(Hand::Right, ControlKind::B);
const Control RbRightMenu = control(Hand::Right, ControlKind::Menu);

//! The longest note the VR settings page shows for a refused assignment: "short" (the contract)
const size_t MaxNoteLength = 120;

ControlSet rbSet(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

/*!
 * Press what \a binding names the way a player does - a chord's first control, then its second; a long press held
 * past the layout's long press time, anything else a short press - and \return whether the controls gave the engine
 * the game menu key (Escape) on the way or in the half second after the release
 */
bool pressOpensMenu(const ControlLayout & layout, const Binding & binding) {

	if(!binding.isBound()) {
		return false;
	}

	ControlResolver resolver(layout);
	GameInputMapper mapper;
	ControllerInput input;
	s64 time = 1000;
	bool escape = false;
	const auto frame = [&](const ControlSet & down) {
		time += 11;
		resolver.update(time, down);
		escape = mapper.update(resolver, input).escape || escape;
	};
	const auto holdFor = [&](const ControlSet & down, s64 ms) {
		const s64 end = time + ms;
		while(time < end) {
			frame(down);
		}
	};

	ControlSet down = rbSet({ binding.first });
	if(binding.isChord()) {
		holdFor(down, 100);
		down[binding.second] = true;
	}
	holdFor(down, binding.longPress ? resolver.longPressMs + 200 : 100);
	holdFor(ControlSet(), 500);
	return escape;
}

} // anonymous namespace

void MenuRebindTest::menuReachableTest() {

	// Every default layout has a way to the game menu: the right B's radial menu (Index, Touch, an unknown
	// controller: D-042), the left menu button (the simple controller)
	for(ControllerType type : { ControllerType::Index, ControllerType::Touch, ControllerType::Simple,
	                            ControllerType::Unknown }) {
		CPPUNIT_ASSERT_MESSAGE(controllerTypeName(type), menuReachable(defaultLayout(type)));
	}

	// Nothing bound, or no menu event and no radial menu: no way
	CPPUNIT_ASSERT(!menuReachable(ControlLayout()));
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::RadialRight] = Binding();
	CPPUNIT_ASSERT(!menuReachable(layout));

	// The menu event, or either hand's radial menu, is a way
	ControlLayout menu = layout;
	menu[Event::Menu] = Binding::single(RbLeftTrackpad);
	CPPUNIT_ASSERT(menuReachable(menu));
	ControlLayout radialLeft = layout;
	radialLeft[Event::RadialLeft] = Binding::hold(RbLeftTrackpad);
	CPPUNIT_ASSERT(menuReachable(radialLeft));
}

void MenuRebindTest::rebindMenuButtonTest() {

	// The owner's case (#326): the menu button's input given to another action. The menu keeps a binding, and
	// pressing it opens the menu - whether the assignment was made next to it, or refused to keep it
	std::string note;

	// The simple controller: the left menu button (the menu) given to the inventory
	ControlLayout simple = defaultLayout(ControllerType::Simple);
	assignBinding(simple, Event::Inventory, Binding::single(RbLeftMenu), &note);
	CPPUNIT_ASSERT_MESSAGE("simple: the menu keeps a binding", simple[Event::Menu].isBound());
	CPPUNIT_ASSERT(menuReachable(simple));
	CPPUNIT_ASSERT_MESSAGE("simple: pressing the menu's binding opens the menu", pressOpensMenu(simple, simple[Event::Menu]));

	// D-019's menu on the left A (the layout the owner remapped), without the radial menu: the left A given to the
	// casting stance
	ControlLayout leftA = defaultLayout(ControllerType::Index);
	leftA[Event::Menu] = Binding::single(RbLeftA);
	leftA[Event::Inventory] = Binding();
	leftA[Event::RadialRight] = Binding();
	assignBinding(leftA, Event::MagicMode, Binding::single(RbLeftA), &note);
	CPPUNIT_ASSERT_MESSAGE("left A: the menu keeps a binding", leftA[Event::Menu].isBound());
	CPPUNIT_ASSERT(menuReachable(leftA));
	CPPUNIT_ASSERT_MESSAGE("left A: pressing the menu's binding opens the menu", pressOpensMenu(leftA, leftA[Event::Menu]));

	// The Index default: the right B (the radial menu, the only way to the game menu, D-042) given to jump - the
	// radial menu stays on the right B (its ring opening the menu is the game's: menu-rebind-kept.xrs)
	ControlLayout index = defaultLayout(ControllerType::Index);
	assignBinding(index, Event::Jump, Binding::single(RbRightB), &note);
	CPPUNIT_ASSERT_MESSAGE("index: the radial menu stays on the right B", index[Event::RadialRight] == Binding::single(RbRightB));
	CPPUNIT_ASSERT(menuReachable(index));
}

void MenuRebindTest::refuseUnbindingTest() {

	// Unbinding the last way to the game menu is refused: the layout stays as it was, with a short note
	for(ControllerType type : { ControllerType::Index, ControllerType::Touch }) {
		ControlLayout layout = defaultLayout(type);
		const ControlLayout before = layout;
		std::string note;
		CPPUNIT_ASSERT_MESSAGE(controllerTypeName(type), !assignBinding(layout, Event::RadialRight, Binding(), &note));
		CPPUNIT_ASSERT_MESSAGE("the layout is unchanged", layout == before);
		CPPUNIT_ASSERT_MESSAGE("the refusal has a note for the page", !note.empty());
		CPPUNIT_ASSERT_MESSAGE("the note is short", note.size() <= MaxNoteLength);
		CPPUNIT_ASSERT(menuReachable(layout));
	}

	{
		ControlLayout layout = defaultLayout(ControllerType::Simple);
		const ControlLayout before = layout;
		std::string note;
		CPPUNIT_ASSERT_MESSAGE("simple: menu = none is refused", !assignBinding(layout, Event::Menu, Binding(), &note));
		CPPUNIT_ASSERT(layout == before);
		CPPUNIT_ASSERT(!note.empty() && note.size() <= MaxNoteLength);
		// Without a note to fill it is refused all the same
		CPPUNIT_ASSERT(!assignBinding(layout, Event::Menu, Binding()));
		CPPUNIT_ASSERT(layout[Event::Menu] == Binding::single(RbLeftMenu));
	}

	{
		// Two ways: the first may go, the last may not
		ControlLayout layout = defaultLayout(ControllerType::Index);
		layout[Event::Menu] = Binding::single(RbLeftTrackpad);
		std::string note;
		CPPUNIT_ASSERT_MESSAGE("the menu event may go while the radial menu is bound", assignBinding(layout, Event::Menu, Binding(), &note));
		CPPUNIT_ASSERT(!layout[Event::Menu].isBound());
		const ControlLayout before = layout;
		CPPUNIT_ASSERT_MESSAGE("then the radial menu is the last way and stays", !assignBinding(layout, Event::RadialRight, Binding(), &note));
		CPPUNIT_ASSERT(layout == before);
		CPPUNIT_ASSERT(!note.empty());
	}
}

void MenuRebindTest::allowedRebindTest() {

	// Everything else is remapped as before (#178)
	ControlLayout layout = defaultLayout(ControllerType::Index);
	ControlLayout expected = layout;
	std::string note;

	CPPUNIT_ASSERT_MESSAGE("another event may be unbound", assignBinding(layout, Event::Jump, Binding(), &note));
	expected[Event::Jump] = Binding();
	CPPUNIT_ASSERT(layout == expected);

	// The way to the menu may move: it keeps a binding
	CPPUNIT_ASSERT_MESSAGE("the radial menu may move to another control",
	                       assignBinding(layout, Event::RadialRight, Binding::single(RbLeftTrackpad), &note));
	CPPUNIT_ASSERT(layout[Event::RadialRight] == Binding::single(RbLeftTrackpad));
	CPPUNIT_ASSERT_MESSAGE("... or to a long press", assignBinding(layout, Event::RadialRight, Binding::hold(RbRightB), &note));
	CPPUNIT_ASSERT(layout[Event::RadialRight] == Binding::hold(RbRightB));

	ControlLayout simple = defaultLayout(ControllerType::Simple);
	CPPUNIT_ASSERT_MESSAGE("simple: the menu may move to the right menu button's long press",
	                       assignBinding(simple, Event::Menu, Binding::hold(RbRightMenu), &note));
	CPPUNIT_ASSERT(simple[Event::Menu] == Binding::hold(RbRightMenu));
}
