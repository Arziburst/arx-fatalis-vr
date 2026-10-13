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

#include "vr/VRControlsTest.h"

#include <initializer_list>
#include <string>

#include "vr/VRControls.h"

CppUnit::AutoRegisterSuite<VRControlsTest> g_registerVRControlsTest;

using namespace vr;

namespace {

const Control LeftA = control(Hand::Left, ControlKind::A);
const Control LeftB = control(Hand::Left, ControlKind::B);
const Control LeftStickClick = control(Hand::Left, ControlKind::StickClick);
const Control RightA = control(Hand::Right, ControlKind::A);
const Control RightTrigger = control(Hand::Right, ControlKind::Trigger);
const Control RightGrip = control(Hand::Right, ControlKind::Grip);

ControlSet controls(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

} // anonymous namespace

void VRControlsTest::singlePressTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// Fires on the press itself: right A leads no chord and has no long press
	resolver.update(0, controls({ RightA }));
	CPPUNIT_ASSERT(resolver.active(Event::Jump));
	CPPUNIT_ASSERT(resolver.pressed(Event::Jump));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Jump));

	resolver.update(16, controls({ RightA }));
	CPPUNIT_ASSERT(resolver.active(Event::Jump));
	CPPUNIT_ASSERT(!resolver.pressed(Event::Jump));

	resolver.update(32, controls());
	CPPUNIT_ASSERT(!resolver.active(Event::Jump));
	CPPUNIT_ASSERT(resolver.released(Event::Jump));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Jump));

	// The trigger is the second control of the stash chord, not its leader: no latency either
	resolver.update(48, controls({ RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::InteractRight));
	resolver.update(64, controls());
	CPPUNIT_ASSERT(resolver.released(Event::InteractRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::StashRight));
}

void VRControlsTest::tapBetweenUpdatesTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// Pressed and released between two updates: delivered once, on for exactly one update
	resolver.update(0, controls(), controls({ RightA }));
	CPPUNIT_ASSERT(resolver.active(Event::Jump));
	CPPUNIT_ASSERT(resolver.pressed(Event::Jump));
	CPPUNIT_ASSERT(!resolver.down()[RightA]);

	resolver.update(16, controls());
	CPPUNIT_ASSERT(!resolver.active(Event::Jump));
	CPPUNIT_ASSERT(resolver.released(Event::Jump));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Jump));

	// The same for a control whose single binding waits for the release (the grip leads a chord),
	// after the release debounce window
	resolver.update(32, controls(), controls({ RightGrip }));
	CPPUNIT_ASSERT(!resolver.active(Event::UseRight));
	resolver.update(32 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.pressed(Event::UseRight));
	resolver.update(48 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.released(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::UseRight));
}

void VRControlsTest::chordTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// Hold the grip, press the trigger: straight into the inventory, neither click nor use
	resolver.update(0, controls({ RightGrip }));
	CPPUNIT_ASSERT(!resolver.active(Event::UseRight));
	resolver.update(16, controls({ RightGrip, RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::StashRight));
	// The same chord acts on a door, lever, chest or body (#38): the reticle's target decides which one does something
	CPPUNIT_ASSERT(resolver.pressed(Event::ActRight));
	CPPUNIT_ASSERT(!resolver.active(Event::ActLeft));
	CPPUNIT_ASSERT(!resolver.active(Event::InteractRight));

	resolver.update(32, controls({ RightGrip }));
	CPPUNIT_ASSERT(resolver.released(Event::StashRight));
	CPPUNIT_ASSERT(resolver.released(Event::ActRight));

	// Both controls were used by the chord: releasing the grip is no tap
	resolver.update(48, controls());
	resolver.update(48 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(!resolver.down()[RightGrip]);
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::StashRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::InteractRight));

	// The trigger alone clicks again
	resolver.update(300, controls({ RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::InteractRight));
	CPPUNIT_ASSERT(!resolver.active(Event::StashRight));
}

void VRControlsTest::chordLeaderTapTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// The grip leads a chord, so its own binding (use) fires on a short release
	resolver.update(0, controls({ RightGrip }));
	resolver.update(100, controls({ RightGrip }));
	CPPUNIT_ASSERT(!resolver.active(Event::UseRight));
	resolver.update(200, controls());
	CPPUNIT_ASSERT(!resolver.active(Event::UseRight)); // the release waits for the debounce window
	resolver.update(200 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.pressed(Event::UseRight));
	CPPUNIT_ASSERT(resolver.active(Event::UseRight));
	resolver.update(216 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.released(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::UseRight));

	// The grip has no long press of its own: a hand kept closed for seconds and opened without a
	// trigger is still one "use", when it opens (D-019, owner 2026-10-10)
	resolver.update(1000, controls({ RightGrip }));
	resolver.update(4000, controls({ RightGrip }));
	CPPUNIT_ASSERT(!resolver.active(Event::UseRight));
	resolver.update(5000, controls());
	resolver.update(5000 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.pressed(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(2), resolver.presses(Event::UseRight));

	// ... and with a trigger while it was closed, at any time, only the stash
	resolver.update(6000, controls({ RightGrip }));
	resolver.update(8000, controls({ RightGrip, RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::StashRight));
	resolver.update(8100, controls());
	resolver.update(9000, controls());
	CPPUNIT_ASSERT_EQUAL(u32(2), resolver.presses(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::InteractRight));
}

void VRControlsTest::simultaneousChordTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// Both in one update count as the chord, whatever the order of the controls' numbers
	resolver.update(0, controls({ RightTrigger, RightGrip }));
	CPPUNIT_ASSERT(resolver.pressed(Event::StashRight));
	CPPUNIT_ASSERT(!resolver.active(Event::InteractRight));
	resolver.update(16, controls());
	resolver.update(16 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::UseRight));
}

void VRControlsTest::gripFlickerTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));

	// The owner's headset run of #30: the grip flickers around its threshold before the chord.
	// One hold, then the chord - and not a single "use"
	resolver.update(0, controls({ RightGrip }));
	resolver.update(40, controls());
	resolver.update(80, controls({ RightGrip }));
	resolver.update(120, controls());
	resolver.update(160, controls({ RightGrip }));
	CPPUNIT_ASSERT(resolver.down()[RightGrip]);
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::UseRight));
	resolver.update(300, controls({ RightGrip, RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::StashRight));
	resolver.update(400, controls());
	resolver.update(400 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::StashRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::UseRight));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::InteractRight));

	// A flicker inside the window, even as taps between updates, is the same hold
	resolver.update(1000, controls({ RightGrip }));
	resolver.update(1040, controls());
	resolver.update(1100, controls(), controls({ RightGrip }));
	resolver.update(1240, controls());
	CPPUNIT_ASSERT(resolver.down()[RightGrip]);
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::UseRight));

	// ... and ends as one tap of "use" once the grip stays up
	resolver.update(1100 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.pressed(Event::UseRight));
	resolver.update(2000, controls());
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::UseRight));

	// The chord's second control pressed inside the window still makes the chord
	resolver.update(3000, controls({ RightGrip }));
	resolver.update(3040, controls());
	resolver.update(3100, controls({ RightTrigger }));
	CPPUNIT_ASSERT(resolver.pressed(Event::StashRight));
	CPPUNIT_ASSERT(!resolver.active(Event::InteractRight));

	// A control with only a single binding is not debounced: released at once
	resolver.update(4000, controls({ RightA }));
	resolver.update(4016, controls());
	CPPUNIT_ASSERT(resolver.released(Event::Jump));
}

void VRControlsTest::indexGripThresholdTest() {

	// The Index grip is how far the strapped hand is closed (D-019, owner 2026-10-10):
	// an open hand, fingers off or resting lightly, is no grip
	const Hysteresis & index = gripThreshold(ControllerType::Index);
	bool down = false;
	for(float value : { 0.f, 0.05f, 0.15f, 0.30f, 0.22f, 0.45f, 0.10f }) {
		down = index.update(down, value);
		CPPUNIT_ASSERT(!down);
	}
	// A light close is the grip, and the relaxed closed hand's flicker around 0.6 - 0.7 (the owner's
	// first headset run, six false "use" events against 0.70 / 0.55) keeps it closed
	down = index.update(down, 0.55f);
	CPPUNIT_ASSERT(down);
	for(float value : { 0.62f, 0.68f, 0.58f, 0.71f, 0.60f, 0.35f }) {
		down = index.update(down, value);
		CPPUNIT_ASSERT(down);
	}
	// Opening the hand lets go
	down = index.update(down, 0.25f);
	CPPUNIT_ASSERT(!down);

	// The same flicker against the Touch's squeeze threshold would release and press again
	bool touch = GripThreshold.update(false, 0.71f);
	CPPUNIT_ASSERT(touch);
	touch = GripThreshold.update(touch, 0.54f);
	CPPUNIT_ASSERT(!touch);

	// The others keep squeeze/value with 0.70 / 0.55 (F-41)
	CPPUNIT_ASSERT(&gripThreshold(ControllerType::Touch) == &GripThreshold);
	CPPUNIT_ASSERT(&gripThreshold(ControllerType::Simple) == &GripThreshold);
	CPPUNIT_ASSERT(&gripThreshold(ControllerType::Unknown) == &GripThreshold);
	CPPUNIT_ASSERT(&gripThreshold(ControllerType::Index) == &IndexGripThreshold);
}

void VRControlsTest::longPressTest() {

	ControlLayout layout;
	layout[Event::Book] = Binding::single(LeftB);
	layout[Event::Menu] = Binding::hold(LeftB);
	layout[Event::Recenter] = Binding::hold(LeftStickClick);
	ControlResolver resolver(layout);

	// A tap is the single binding, on the release
	resolver.update(0, controls({ LeftB }));
	CPPUNIT_ASSERT(!resolver.active(Event::Book));
	resolver.update(100, controls());
	resolver.update(100 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.pressed(Event::Book));
	CPPUNIT_ASSERT(!resolver.active(Event::Menu));

	// A long press fires once at the threshold and never the tap
	resolver.update(1000, controls({ LeftB }));
	resolver.update(1000 + resolver.longPressMs - 1, controls({ LeftB }));
	CPPUNIT_ASSERT(!resolver.active(Event::Menu));
	resolver.update(1000 + resolver.longPressMs, controls({ LeftB }));
	CPPUNIT_ASSERT(resolver.pressed(Event::Menu));
	resolver.update(2000, controls({ LeftB }));
	CPPUNIT_ASSERT(resolver.active(Event::Menu));
	CPPUNIT_ASSERT(!resolver.pressed(Event::Menu));
	resolver.update(2100, controls());
	resolver.update(2100 + resolver.releaseDebounceMs, controls());
	CPPUNIT_ASSERT(resolver.released(Event::Menu));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Menu));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Book));

	// A control with only a long press: a short press does nothing
	resolver.update(3000, controls({ LeftStickClick }));
	resolver.update(3100, controls());
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::Recenter));
}

void VRControlsTest::longChordTest() {

	ControlLayout layout;
	layout[Event::Recenter] = Binding::chord(LeftA, LeftB);
	layout[Event::Recenter].longPress = true;
	layout[Event::Book] = Binding::single(LeftB);
	ControlResolver resolver(layout);

	// Held long enough: the chord fires, the second control's own binding never does
	resolver.update(0, controls({ LeftA }));
	resolver.update(10, controls({ LeftA, LeftB }));
	CPPUNIT_ASSERT(!resolver.active(Event::Book));
	CPPUNIT_ASSERT(!resolver.active(Event::Recenter));
	resolver.update(10 + resolver.longPressMs, controls({ LeftA, LeftB }));
	CPPUNIT_ASSERT(resolver.pressed(Event::Recenter));
	resolver.update(1000, controls());
	CPPUNIT_ASSERT(resolver.released(Event::Recenter));

	// Let go too early: nothing at all
	resolver.update(2000, controls({ LeftA }));
	resolver.update(2010, controls({ LeftA, LeftB }));
	resolver.update(2100, controls());
	resolver.update(3000, controls());
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Recenter));
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::Book));

	// The second control alone is still the book
	resolver.update(4000, controls({ LeftB }));
	CPPUNIT_ASSERT(resolver.pressed(Event::Book));
}

void VRControlsTest::layoutChangeTest() {

	ControlResolver resolver(defaultLayout(ControllerType::Index));
	resolver.update(0, controls({ RightA }));
	CPPUNIT_ASSERT(resolver.active(Event::Jump));

	ControlLayout layout;
	layout[Event::Inventory] = Binding::single(RightA);
	resolver.setLayout(layout);

	// The active event is released; the control held across the change does nothing
	resolver.update(16, controls({ RightA }));
	CPPUNIT_ASSERT(resolver.released(Event::Jump));
	CPPUNIT_ASSERT(!resolver.active(Event::Inventory));
	resolver.update(32, controls());
	CPPUNIT_ASSERT_EQUAL(u32(0), resolver.presses(Event::Inventory));

	resolver.update(48, controls({ RightA }));
	CPPUNIT_ASSERT(resolver.pressed(Event::Inventory));
	CPPUNIT_ASSERT_EQUAL(u32(1), resolver.presses(Event::Jump));
}

void VRControlsTest::stickDirectionTest() {

	const Control up = control(Hand::Right, ControlKind::StickUp);
	const Control below = control(Hand::Right, ControlKind::StickDown);
	const Control right = control(Hand::Right, ControlKind::StickRight);

	ControlSet down;
	updateStickDirections(down, Hand::Right, 0.f, -0.6f);
	CPPUNIT_ASSERT(down.none());
	updateStickDirections(down, Hand::Right, 0.f, -0.75f);
	CPPUNIT_ASSERT(down[below]);
	CPPUNIT_ASSERT_EQUAL(size_t(1), down.count());

	// Hysteresis: stays down until the release threshold
	updateStickDirections(down, Hand::Right, 0.f, -0.55f);
	CPPUNIT_ASSERT(down[below]);
	updateStickDirections(down, Hand::Right, 0.f, -0.45f);
	CPPUNIT_ASSERT(down.none());

	// The dominant axis decides; one direction at a time
	updateStickDirections(down, Hand::Right, 0.8f, 0.75f);
	CPPUNIT_ASSERT(down[right]);
	CPPUNIT_ASSERT_EQUAL(size_t(1), down.count());
	updateStickDirections(down, Hand::Right, 0.3f, 0.9f);
	CPPUNIT_ASSERT(down[up]);
	CPPUNIT_ASSERT_EQUAL(size_t(1), down.count());

	// The other hand is untouched
	updateStickDirections(down, Hand::Left, 0.f, -1.f);
	CPPUNIT_ASSERT(down[up]);
	CPPUNIT_ASSERT(down[control(Hand::Left, ControlKind::StickDown)]);
}

void VRControlsTest::hysteresisTest() {
	CPPUNIT_ASSERT(!GripThreshold.update(false, 0.69f));
	CPPUNIT_ASSERT(GripThreshold.update(false, 0.70f));
	CPPUNIT_ASSERT(GripThreshold.update(true, 0.60f));
	CPPUNIT_ASSERT(!GripThreshold.update(true, 0.55f));
	CPPUNIT_ASSERT(!TriggerThreshold.update(false, 0.f));
}

void VRControlsTest::bindingTextTest() {

	const Binding bindings[] = {
		Binding(),
		Binding::single(RightA),
		Binding::chord(RightGrip, RightTrigger),
		Binding::hold(LeftStickClick),
		[] { Binding b = Binding::chord(LeftA, LeftB); b.longPress = true; return b; }(),
	};
	for(const Binding & binding : bindings) {
		std::optional<Binding> parsed = parseBinding(formatBinding(binding));
		CPPUNIT_ASSERT(parsed.has_value());
		CPPUNIT_ASSERT(*parsed == binding);
	}

	CPPUNIT_ASSERT_EQUAL(std::string("none"), formatBinding(Binding()));
	CPPUNIT_ASSERT_EQUAL(std::string("right.grip+right.trigger"), formatBinding(Binding::chord(RightGrip, RightTrigger)));
	CPPUNIT_ASSERT_EQUAL(std::string("left.stick_click:long"), formatBinding(Binding::hold(LeftStickClick)));
	CPPUNIT_ASSERT(parseBinding("  right.a ") == Binding::single(RightA));

	std::string error;
	CPPUNIT_ASSERT(!parseBinding("left.grip+right.trigger", &error));
	CPPUNIT_ASSERT(error.find("across hands") != std::string::npos);
	CPPUNIT_ASSERT(!parseBinding("left.a+left.a", &error));
	CPPUNIT_ASSERT(error.find("itself") != std::string::npos);
	CPPUNIT_ASSERT(!parseBinding("left.thumb", &error));
	CPPUNIT_ASSERT(error.find("left.thumb") != std::string::npos);
	CPPUNIT_ASSERT(!parseBinding("right.a:short", &error));
	CPPUNIT_ASSERT(!parseBinding("left.grip+", &error));
	CPPUNIT_ASSERT(!parseBinding("middle.a", &error));
}

void VRControlsTest::layoutTextTest() {

	const ControlLayout index = defaultLayout(ControllerType::Index);
	ControlLayout parsed;
	std::string error;
	CPPUNIT_ASSERT(parseLayout(formatLayout(index), parsed, &error));
	CPPUNIT_ASSERT(parsed == index);

	// Comments and empty lines are skipped, events not named keep their binding
	ControlLayout layout = index;
	CPPUNIT_ASSERT(parseLayout("# remapped\n\njump = left.a\r\n", layout, &error));
	CPPUNIT_ASSERT(layout[Event::Jump] == Binding::single(LeftA));
	CPPUNIT_ASSERT(layout[Event::Book] == index[Event::Book]);

	// An error names the line and changes nothing
	ControlLayout unchanged = index;
	CPPUNIT_ASSERT(!parseLayout("jump = left.a\nfly = right.b\n", unchanged, &error));
	CPPUNIT_ASSERT(error.find("line 2") != std::string::npos);
	CPPUNIT_ASSERT(unchanged == index);
	CPPUNIT_ASSERT(!parseLayout("jump right.a\n", unchanged, &error));

	// Conflicts: two events on the same binding
	CPPUNIT_ASSERT(findConflicts(index).empty());
	CPPUNIT_ASSERT(findConflicts(layout).size() == 1); // jump and menu both on left.a now
	CPPUNIT_ASSERT(findConflicts(layout)[0].first == Event::Jump);
	CPPUNIT_ASSERT(findConflicts(layout)[0].second == Event::Menu);

	// #38, D-042: a hand's stash and act share grip + trigger by design - not a conflict; any other pair is
	ControlLayout shared = index;
	CPPUNIT_ASSERT(shared[Event::ActLeft] == shared[Event::StashLeft]);
	CPPUNIT_ASSERT(sharesBindingByDesign(Event::ActRight, Event::StashRight));
	CPPUNIT_ASSERT(sharesBindingByDesign(Event::StashLeft, Event::ActLeft));
	CPPUNIT_ASSERT(!sharesBindingByDesign(Event::StashLeft, Event::ActRight));
	CPPUNIT_ASSERT(!sharesBindingByDesign(Event::ActLeft, Event::InteractLeft));
	shared[Event::ActRight] = shared[Event::StashLeft]; // the right act on the left chord: three events on it
	CPPUNIT_ASSERT_EQUAL(size_t(2), findConflicts(shared).size());
	CPPUNIT_ASSERT(findConflicts(shared)[0].first == Event::StashLeft);
	CPPUNIT_ASSERT(findConflicts(shared)[0].second == Event::ActRight);
	CPPUNIT_ASSERT(findConflicts(shared)[1].first == Event::ActLeft);
	CPPUNIT_ASSERT(findConflicts(shared)[1].second == Event::ActRight);
}

void VRControlsTest::defaultLayoutTest() {

	CPPUNIT_ASSERT(controllerTypeFromProfile("/interaction_profiles/valve/index_controller") == ControllerType::Index);
	CPPUNIT_ASSERT(controllerTypeFromProfile("/interaction_profiles/oculus/touch_controller") == ControllerType::Touch);
	CPPUNIT_ASSERT(controllerTypeFromProfile("/interaction_profiles/khr/simple_controller") == ControllerType::Simple);
	CPPUNIT_ASSERT(controllerTypeFromProfile("") == ControllerType::Unknown);

	// D-019: A right jumps, B right inventory, B left book, right stick down crouches
	const ControlLayout index = defaultLayout(ControllerType::Index);
	CPPUNIT_ASSERT_EQUAL(std::string("right.a"), formatBinding(index[Event::Jump]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.b"), formatBinding(index[Event::Inventory]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.b"), formatBinding(index[Event::Book]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.stick_down"), formatBinding(index[Event::CrouchToggle]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.grip+left.trigger"), formatBinding(index[Event::StashLeft]));
	// #38: D-042's use / interact on the same chord (an interim default, #377 owns the layout)
	CPPUNIT_ASSERT_EQUAL(std::string("left.grip+left.trigger"), formatBinding(index[Event::ActLeft]));
	CPPUNIT_ASSERT_EQUAL(std::string("right.grip+right.trigger"), formatBinding(index[Event::ActRight]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.a"), formatBinding(index[Event::Menu]));
	// #43: magic mode held with the left hand - the Index's trackpad, the Touch's free X
	CPPUNIT_ASSERT_EQUAL(std::string("left.trackpad"), formatBinding(index[Event::MagicMode]));

	const ControlLayout touch = defaultLayout(ControllerType::Touch);
	CPPUNIT_ASSERT_EQUAL(std::string("left.menu"), formatBinding(touch[Event::Menu]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.a"), formatBinding(touch[Event::MagicMode]));
	CPPUNIT_ASSERT(touch[Event::Jump] == index[Event::Jump]);
	CPPUNIT_ASSERT(findConflicts(touch).empty());

	const ControlLayout simple = defaultLayout(ControllerType::Simple);
	CPPUNIT_ASSERT_EQUAL(std::string("right.trigger"), formatBinding(simple[Event::InteractRight]));
	CPPUNIT_ASSERT_EQUAL(std::string("left.menu"), formatBinding(simple[Event::Menu]));
	CPPUNIT_ASSERT(!simple[Event::Jump].isBound());
	CPPUNIT_ASSERT(!simple[Event::MagicMode].isBound());

	CPPUNIT_ASSERT(defaultLayout(ControllerType::Unknown) == index);

	// Every event has a distinct, parseable name
	for(size_t i = 0; i < EventCount; i++) {
		CPPUNIT_ASSERT(parseEvent(eventName(Event(i))) == Event(i));
	}
	for(size_t i = 0; i < ControlCount; i++) {
		CPPUNIT_ASSERT(parseControl(controlName(Control(i))) == Control(i));
	}
}
