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

#include "vr/VRGameInputTest.h"

#include <cmath>
#include <initializer_list>
#include <string>

#include "vr/VRControls.h"
#include "vr/VRGameInput.h"

CppUnit::AutoRegisterSuite<VRGameInputTest> g_registerVRGameInputTest;

using namespace vr;

namespace {

const Control GiLeftA = control(Hand::Left, ControlKind::A);
const Control GiLeftB = control(Hand::Left, ControlKind::B);
const Control GiLeftTrigger = control(Hand::Left, ControlKind::Trigger);
const Control GiLeftStickClick = control(Hand::Left, ControlKind::StickClick);
const Control GiRightA = control(Hand::Right, ControlKind::A);
const Control GiRightB = control(Hand::Right, ControlKind::B);
const Control GiRightTrigger = control(Hand::Right, ControlKind::Trigger);
const Control GiRightGrip = control(Hand::Right, ControlKind::Grip);
const Control GiRightStickDown = control(Hand::Right, ControlKind::StickDown);
const Control GiRightStickUp = control(Hand::Right, ControlKind::StickUp);
const Control GiRightStickClick = control(Hand::Right, ControlKind::StickClick);
const Control GiLeftGrip = control(Hand::Left, ControlKind::Grip);

/*!
 * D-019's layout, which the mapping rules were written against and a player can still bind (#377 changed the
 * defaults): the grip uses when it opens, grip + trigger stashes, the left A is the game menu, a long left stick
 * click recenters, the left trackpad is the casting stance
 */
ControlLayout giRuleLayout() {
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::Jump] = Binding::single(GiRightA);
	layout[Event::UseRight] = Binding::single(GiRightGrip);
	layout[Event::UseLeft] = Binding::single(GiLeftGrip);
	layout[Event::StashRight] = Binding::chord(GiRightGrip, GiRightTrigger);
	layout[Event::StashLeft] = Binding::chord(GiLeftGrip, GiLeftTrigger);
	layout[Event::Menu] = Binding::single(GiLeftA);
	layout[Event::Recenter] = Binding::hold(GiLeftStickClick);
	layout[Event::MagicMode] = Binding::single(control(Hand::Left, ControlKind::Trackpad));
	return layout;
}

ControlSet downSet(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

//! The resolver and the mapper of one frame after another, as VRInput.cpp runs them
struct Frames {

	ControlResolver resolver;
	GameInputMapper mapper;
	ControllerInput input;
	HandRoles roles;
	s64 time = 0;

	explicit Frames(const ControlLayout & layout = defaultLayout(ControllerType::Index)) : resolver(layout) { }

	const GameInputState & frame(const ControlSet & down, const ControlSet & taps = ControlSet()) {
		time += 11; // 90 Hz
		resolver.update(time, down, taps);
		return mapper.update(resolver, input, roles);
	}

	//! Frames with nothing held until the debounce window of every released control is over
	const GameInputState & settle() {
		const s64 end = time + resolver.releaseDebounceMs + 11;
		while(time < end) {
			frame(downSet());
		}
		return mapper.state();
	}

};

bool nearlyEqual(float a, float b) {
	return std::abs(a - b) < 1e-4f;
}

} // anonymous namespace

void VRGameInputTest::clickTest() {

	Frames f;

	// The trigger is the mouse's left button, held as long as the trigger: click and drag
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet()).click);

	// Either hand: both hands work as the mouse (D-016); one hand letting go keeps the other's
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftTrigger })).click);
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftTrigger, GiRightTrigger })).click);
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet()).click);

	// A click between two frames still reaches the game for one frame
	CPPUNIT_ASSERT(f.frame(downSet(), downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet()).click);

	// A click is never a use or a stealth action
	CPPUNIT_ASSERT(!f.mapper.state().use);
	CPPUNIT_ASSERT(!f.mapper.state().action(GameAction::Stealth));
}

void VRGameInputTest::useTest() {

	Frames f(giRuleLayout());

	// The grip is a closed hand: nothing while it is closed, "use" for one frame when it opens
	for(int i = 0; i < 30; i++) {
		const GameInputState & state = f.frame(downSet({ GiRightGrip }));
		CPPUNIT_ASSERT(!state.use);
		CPPUNIT_ASSERT(!state.click);
	}
	int useFrames = 0;
	const s64 end = f.time + f.resolver.releaseDebounceMs + 50;
	while(f.time < end) {
		const GameInputState & state = f.frame(downSet());
		useFrames += state.use ? 1 : 0;
		CPPUNIT_ASSERT_EQUAL(state.use, state.action(GameAction::Use));
		CPPUNIT_ASSERT(!state.click);
	}
	CPPUNIT_ASSERT_EQUAL(1, useFrames);
}

void VRGameInputTest::stashTest() {

	Frames f(giRuleLayout());

	// Grip + trigger: the left button with stealth held - the original's stealth-click
	f.frame(downSet({ GiRightGrip }));
	const GameInputState & state = f.frame(downSet({ GiRightGrip, GiRightTrigger }));
	CPPUNIT_ASSERT(state.click);
	CPPUNIT_ASSERT(state.action(GameAction::Stealth));
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightGrip, GiRightTrigger })).action(GameAction::Stealth));

	// The game stashes when the button goes up: stealth is still held in that frame, then let go
	bool clicked = true;
	int frames = 0;
	while(clicked && frames < 100) {
		const GameInputState & now = f.frame(downSet());
		frames++;
		clicked = now.click;
		CPPUNIT_ASSERT(now.action(GameAction::Stealth));
	}
	CPPUNIT_ASSERT(!clicked);
	CPPUNIT_ASSERT(!f.frame(downSet()).action(GameAction::Stealth));

	// Never a use with it (D-019: the hand closed + the trigger = stash only)
	f.settle();
	CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.presses(Event::UseRight));
}

void VRGameInputTest::buttonActionsTest() {

	Frames f;

	// The B buttons hold the radial menus (D-042, #376): no engine action of their own - the inventory and
	// the book are items of the menus. Bound by the player, the inventory and book events still give theirs
	ControlLayout menus = defaultLayout(ControllerType::Index);
	menus[Event::Inventory] = Binding::single(GiRightB);
	menus[Event::Book] = Binding::single(GiLeftB);
	menus[Event::RadialLeft] = Binding();
	menus[Event::RadialRight] = Binding();
	Frames remapped;
	remapped.resolver.setLayout(menus);

	for(const GameInputState & held : { f.frame(downSet({ GiRightB })), f.frame(downSet({ GiLeftB, GiRightB })) }) {
		for(size_t i = 0; i < GameActionCount; i++) {
			CPPUNIT_ASSERT(!held.actions[i]);
		}
	}
	f.frame(downSet());
	f.settle();

	// Each button is the engine action of its event while held
	struct Case { Frames & frames; Control control; GameAction action; };
	const Case cases[] = {
		{ f, GiRightStickUp, GameAction::Jump },
		{ remapped, GiRightB, GameAction::Inventory },
		{ remapped, GiLeftB, GameAction::Book },
		{ f, GiRightStickDown, GameAction::CrouchToggle },
	};
	for(const Case & c : cases) {
		const GameInputState & held = c.frames.frame(downSet({ c.control }));
		CPPUNIT_ASSERT(held.action(c.action));
		for(size_t i = 0; i < GameActionCount; i++) {
			CPPUNIT_ASSERT_EQUAL(GameAction(i) == c.action, held.actions[i]);
		}
		CPPUNIT_ASSERT(!held.click && !held.use && !held.escape);
		CPPUNIT_ASSERT(c.frames.frame(downSet({ c.control })).action(c.action));
		CPPUNIT_ASSERT(!c.frames.frame(downSet()).action(c.action));
	}

	// Magic mode (#43): held with the left A (D-042: A is the casting stance; one for both hands until #378), as
	// long as it is pressed, and not by the other face buttons or the trackpads (free by default)
	CPPUNIT_ASSERT(!f.frame(downSet({ GiLeftB, GiRightA, GiRightB, control(Hand::Left, ControlKind::Trackpad),
	                                  control(Hand::Right, ControlKind::Trackpad) })).action(GameAction::MagicMode));
	f.settle();
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftA })).action(GameAction::MagicMode));
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftA })).action(GameAction::MagicMode));
	// a trigger held in the stance is still the click: the engine draws the rune with it
	const GameInputState & drawing = f.frame(downSet({ GiLeftA, control(Hand::Right, ControlKind::Trigger) }));
	CPPUNIT_ASSERT(drawing.action(GameAction::MagicMode) && drawing.click);
	CPPUNIT_ASSERT(!f.frame(downSet()).action(GameAction::MagicMode));
	f.settle();
	// Remapped, it is held like the others
	ControlLayout layout = f.resolver.layout();
	layout[Event::MagicMode] = Binding::single(control(Hand::Right, ControlKind::Trackpad));
	f.resolver.setLayout(layout);
	CPPUNIT_ASSERT(f.frame(downSet({ control(Hand::Right, ControlKind::Trackpad) })).action(GameAction::MagicMode));
	CPPUNIT_ASSERT(!f.frame(downSet()).action(GameAction::MagicMode));
}

void VRGameInputTest::menuTest() {

	Frames f(giRuleLayout());

	// The game menu's event (D-019's left A; the Touch's menu button): Escape held as long as the button
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftA })).escape);
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftA })).escape);
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftA })).escape);
	CPPUNIT_ASSERT(!f.frame(downSet()).escape);

	// The game acts on Escape's release, seen only after two frames held: a quick tap lasts two
	CPPUNIT_ASSERT(f.frame(downSet(), downSet({ GiLeftA })).escape);
	CPPUNIT_ASSERT(f.frame(downSet()).escape);
	CPPUNIT_ASSERT(!f.frame(downSet()).escape);
	CPPUNIT_ASSERT(!f.frame(downSet()).escape);
}

void VRGameInputTest::recenterTest() {

	Frames f(giRuleLayout());

	// A long press of the left stick click: one request, when the hold is long enough
	int requests = 0;
	const s64 end = f.time + f.resolver.longPressMs + 200;
	while(f.time < end) {
		requests += f.frame(downSet({ GiLeftStickClick })).recenter ? 1 : 0;
	}
	CPPUNIT_ASSERT_EQUAL(1, requests);
	f.settle();
	CPPUNIT_ASSERT(!f.mapper.state().recenter);
}

void VRGameInputTest::deadZoneTest() {

	// Inside the dead zone: nothing
	Stick rest = applyDeadZone(Stick{ 0.1f, -0.1f }, 0.2f);
	CPPUNIT_ASSERT(rest.x == 0.f && rest.y == 0.f);

	// Full deflection stays full, the direction is kept
	Stick full = applyDeadZone(Stick{ 0.f, 1.f }, 0.2f);
	CPPUNIT_ASSERT(nearlyEqual(full.x, 0.f) && nearlyEqual(full.y, 1.f));
	Stick diagonal = applyDeadZone(Stick{ 1.f, 1.f }, 0.2f); // a square stick's corner: no faster than 1
	CPPUNIT_ASSERT(nearlyEqual(diagonal.x, std::sqrt(0.5f)) && nearlyEqual(diagonal.y, std::sqrt(0.5f)));

	// The edge of the dead zone maps to 0, the range above it is rescaled to 0 - 1
	Stick half = applyDeadZone(Stick{ -0.6f, 0.f }, 0.2f);
	CPPUNIT_ASSERT(nearlyEqual(half.x, -0.5f) && nearlyEqual(half.y, 0.f));
}

void VRGameInputTest::sticksTest() {

	Frames f;

	// Walk with the left stick, turn with the right one's x (D-019)
	f.input.sticks[size_t(Hand::Left)] = Stick{ 0.f, 1.f };
	f.input.sticks[size_t(Hand::Right)] = Stick{ -1.f, 0.9f };
	const GameInputState & state = f.frame(downSet());
	CPPUNIT_ASSERT(nearlyEqual(state.move.x, 0.f) && nearlyEqual(state.move.y, 1.f));
	CPPUNIT_ASSERT(nearlyEqual(state.turn, -1.f));

	// A resting stick's drift does nothing
	f.input.sticks[size_t(Hand::Left)] = Stick{ 0.05f, -0.08f };
	f.input.sticks[size_t(Hand::Right)] = Stick{ 0.1f, 0.f };
	const GameInputState & resting = f.frame(downSet());
	CPPUNIT_ASSERT(resting.move.x == 0.f && resting.move.y == 0.f);
	CPPUNIT_ASSERT(resting.turn == 0.f);

	// The axes are reported, not acted on: no engine action from a stick (#31, #32)
	CPPUNIT_ASSERT(resting.actions.none());
	CPPUNIT_ASSERT_EQUAL(std::string(), formatGameInput(resting));
}

void VRGameInputTest::crouchDoesNotTurnTest() {

	Frames f;

	// The right stick pushed down for the crouch toggle, drifting sideways past the turn dead zone:
	// the crouch toggle, and no turn (F-121 axis dominance, #33)
	f.input.sticks[size_t(Hand::Right)] = Stick{ 0.4f, -0.9f };
	const GameInputState & down = f.frame(downSet({ GiRightStickDown }));
	CPPUNIT_ASSERT(down.action(GameAction::CrouchToggle));
	CPPUNIT_ASSERT(down.turn == 0.f);

	// A diagonal with x at least 0.6 of y still turns
	f.input.sticks[size_t(Hand::Right)] = Stick{ 0.6f, -0.9f };
	CPPUNIT_ASSERT(f.frame(downSet()).turn > 0.f);
	f.input.sticks[size_t(Hand::Right)] = Stick{ -0.9f, 0.4f };
	CPPUNIT_ASSERT(f.frame(downSet()).turn < 0.f);
}

void VRGameInputTest::defaultUseTest() {

	Frames f;

	// D-042 (#377): the grip alone is nothing - held, opened
	for(int i = 0; i < 30; i++) {
		const GameInputState & state = f.frame(downSet({ GiRightGrip }));
		CPPUNIT_ASSERT(!state.use && !state.click && state.actions.none());
	}
	CPPUNIT_ASSERT(!f.settle().use);
	CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.presses(Event::UseRight));

	// Grip, then trigger: the original's right click (use) while both are held - no click, no stealth (stash)
	f.frame(downSet({ GiRightGrip }));
	for(int i = 0; i < 5; i++) {
		const GameInputState & state = f.frame(downSet({ GiRightGrip, GiRightTrigger }));
		CPPUNIT_ASSERT(state.use && state.action(GameAction::Use));
		CPPUNIT_ASSERT(!state.click);
		CPPUNIT_ASSERT(!state.action(GameAction::Stealth));
	}
	CPPUNIT_ASSERT(!f.frame(downSet({ GiRightGrip })).use);
	f.settle();

	// The left hand the same
	f.frame(downSet({ GiLeftGrip }));
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftGrip, GiLeftTrigger })).use);
	f.settle();
}

void VRGameInputTest::handRolesTest() {

	Frames f;

	// A hand with a drawn weapon charges with its trigger: no click from that hand (D-042, #377) ...
	f.roles.charges[size_t(Hand::Right)] = true;
	CPPUNIT_ASSERT(!f.frame(downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet({ GiRightTrigger })).click);
	// ... the other hand still clicks and drags, also while the weapon hand charges
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightTrigger, GiLeftTrigger })).click);
	CPPUNIT_ASSERT(f.frame(downSet({ GiLeftTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet()).click);
	// Sheathed again, the trigger clicks
	f.roles.charges = { };
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightTrigger })).click);
	CPPUNIT_ASSERT(!f.frame(downSet()).click);
	f.settle();

	// A chord that acted on a door, lever, chest or body (#38) is no use of that hand; the other hand's use stays
	f.frame(downSet({ GiRightGrip, GiLeftGrip }));
	f.roles.acts[size_t(Hand::Right)] = true;
	const GameInputState & acted = f.frame(downSet({ GiRightGrip, GiRightTrigger, GiLeftGrip }));
	CPPUNIT_ASSERT(!acted.use && !acted.action(GameAction::Use) && !acted.click);
	CPPUNIT_ASSERT(f.frame(downSet({ GiRightGrip, GiRightTrigger, GiLeftGrip, GiLeftTrigger })).use);
	f.roles.acts = { };
	f.settle();

	// A stash the player bound onto the chord: no stealth-click either when the chord acted
	Frames stash(giRuleLayout());
	stash.roles.acts[size_t(Hand::Right)] = true;
	stash.frame(downSet({ GiRightGrip }));
	const GameInputState & stashActed = stash.frame(downSet({ GiRightGrip, GiRightTrigger }));
	CPPUNIT_ASSERT(!stashActed.click && !stashActed.action(GameAction::Stealth));
}

void VRGameInputTest::jumpDoesNotTurnTest() {

	Frames f;

	// The right stick pushed up jumps (D-042, #377) and does not turn: x is below 0.6 of y (F-121 axis dominance)
	f.input.sticks[size_t(Hand::Right)] = Stick{ 0.3f, 0.95f };
	const GameInputState & up = f.frame(downSet({ GiRightStickUp }));
	CPPUNIT_ASSERT(up.action(GameAction::Jump));
	CPPUNIT_ASSERT(up.turn == 0.f);
	CPPUNIT_ASSERT(!f.frame(downSet()).action(GameAction::Jump));

	// The stick click draws / sheathes the weapon: no engine action of its own (vr/VRWeapon.h does it)
	const GameInputState & click = f.frame(downSet({ GiRightStickClick }));
	CPPUNIT_ASSERT(f.resolver.pressed(Event::Weapon));
	CPPUNIT_ASSERT(click.actions.none() && !click.click && !click.use && !click.escape && !click.recenter);
}
