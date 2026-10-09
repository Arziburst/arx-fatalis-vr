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

#include "vr/BindingCaptureTest.h"

#include <initializer_list>
#include <string>

#include "vr/BindingCapture.h"
#include "vr/VRControls.h"

CppUnit::AutoRegisterSuite<BindingCaptureTest> g_registerBindingCaptureTest;

using namespace vr;

namespace {

const Control CapLeftA = control(Hand::Left, ControlKind::A);
const Control CapLeftB = control(Hand::Left, ControlKind::B);
const Control CapLeftGrip = control(Hand::Left, ControlKind::Grip);
const Control CapLeftTrigger = control(Hand::Left, ControlKind::Trigger);
const Control CapLeftStickUp = control(Hand::Left, ControlKind::StickUp);
const Control CapLeftStickClick = control(Hand::Left, ControlKind::StickClick);
const Control CapRightA = control(Hand::Right, ControlKind::A);
const Control CapRightB = control(Hand::Right, ControlKind::B);
const Control CapRightTrigger = control(Hand::Right, ControlKind::Trigger);
const Control CapRightStickLeft = control(Hand::Right, ControlKind::StickLeft);
const Control CapRightStickDown = control(Hand::Right, ControlKind::StickDown);

ControlSet held(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

typedef BindingCapture::State CaptureState;

} // anonymous namespace

void BindingCaptureTest::singleTest() {

	BindingCapture capture;
	CPPUNIT_ASSERT(capture.state() == CaptureState::Idle);
	capture.start(0, held());
	CPPUNIT_ASSERT(capture.state() == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.active());

	CPPUNIT_ASSERT(capture.update(16, held()) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.update(32, held({ CapRightB })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapRightB));
	CPPUNIT_ASSERT(capture.update(100, held({ CapRightB })) == CaptureState::Holding);

	// The release counts once it stayed up for the debounce time; the hold lasted 132 - 32 ms
	CPPUNIT_ASSERT(capture.update(132, held()) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.update(282, held()) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapRightB));
	CPPUNIT_ASSERT(!capture.active());

	// Done stays done
	CPPUNIT_ASSERT(capture.update(300, held({ CapRightA })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapRightB));
}

void BindingCaptureTest::tapTest() {

	// Pressed and released between two updates
	BindingCapture capture;
	capture.start(0, held());
	CPPUNIT_ASSERT(capture.update(16, held(), held({ CapLeftA })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapLeftA));
}

void BindingCaptureTest::longPressTest() {

	BindingCapture capture;
	capture.start(0, held());
	capture.update(100, held({ CapLeftStickClick }));
	CPPUNIT_ASSERT(!capture.heldLong(500));
	CPPUNIT_ASSERT(capture.heldLong(600));
	// Holding past the time does not end the capture: the player may still add a chord's second control
	CPPUNIT_ASSERT(capture.update(900, held({ CapLeftStickClick })) == CaptureState::Holding);
	capture.update(1000, held());
	CPPUNIT_ASSERT(capture.update(1200, held()) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::hold(CapLeftStickClick));
	CPPUNIT_ASSERT_EQUAL(std::string("left.stick_click:long"), formatBinding(capture.binding()));

	// Just below the time: a single press
	capture.start(2000, held());
	capture.update(2000, held({ CapLeftStickClick }));
	capture.update(2499, held());
	CPPUNIT_ASSERT(capture.update(2700, held()) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapLeftStickClick));
}

void BindingCaptureTest::chordTest() {

	// Hold the grip, press the trigger: the stash chord of D-019
	BindingCapture capture;
	capture.start(0, held());
	capture.update(16, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(100, held({ CapLeftGrip, CapLeftTrigger })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::chord(CapLeftGrip, CapLeftTrigger));
	// Letting go of the second control ends it, the first may stay closed
	capture.update(200, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(400, held({ CapLeftGrip })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::chord(CapLeftGrip, CapLeftTrigger));

	// A second control tapped between two updates while the first is held
	capture.start(1000, held());
	capture.update(1016, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(1032, held({ CapLeftGrip }), held({ CapLeftA })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::chord(CapLeftGrip, CapLeftA));
}

void BindingCaptureTest::longChordTest() {

	BindingCapture capture;
	capture.start(0, held());
	capture.update(0, held({ CapLeftGrip }));
	capture.update(100, held({ CapLeftGrip, CapLeftB }));
	CPPUNIT_ASSERT(capture.heldLong(600));
	capture.update(700, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(900, held()) == CaptureState::Done);
	Binding expected = Binding::chord(CapLeftGrip, CapLeftB);
	expected.longPress = true;
	CPPUNIT_ASSERT(capture.binding() == expected);
}

void BindingCaptureTest::slowChordTest() {

	// The grip closed for two seconds before the trigger: still a short chord, not a long grip
	BindingCapture capture;
	capture.start(0, held());
	capture.update(0, held({ CapLeftGrip }));
	capture.update(2000, held({ CapLeftGrip }));
	capture.update(2016, held({ CapLeftGrip, CapLeftTrigger }));
	capture.update(2100, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(2300, held({ CapLeftGrip })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::chord(CapLeftGrip, CapLeftTrigger));
}

void BindingCaptureTest::heldFromBeforeTest() {

	// The trigger that clicked the row is still held when the capture starts
	BindingCapture capture;
	capture.start(0, held({ CapRightTrigger }));
	CPPUNIT_ASSERT(capture.update(16, held({ CapRightTrigger })) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.update(32, held()) == CaptureState::Waiting);
	// Released once, it counts again
	CPPUNIT_ASSERT(capture.update(48, held({ CapRightTrigger })) == CaptureState::Holding);
	capture.update(64, held());
	CPPUNIT_ASSERT(capture.update(300, held()) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapRightTrigger));

	// A control pressed while the ignored one is still held is captured
	capture.start(1000, held({ CapRightTrigger }));
	CPPUNIT_ASSERT(capture.update(1016, held({ CapRightTrigger, CapLeftA })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapLeftA));
}

void BindingCaptureTest::flickerTest() {

	// The Index grip dropping below its threshold for a moment is one hold that still becomes the chord
	BindingCapture capture;
	capture.start(0, held());
	capture.update(0, held({ CapLeftGrip }));
	capture.update(300, held());
	capture.update(380, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(400, held({ CapLeftGrip, CapLeftTrigger })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::chord(CapLeftGrip, CapLeftTrigger));
}

void BindingCaptureTest::otherHandTest() {

	// A control of the other hand does not make a chord: it is refused with the reason
	BindingCapture capture;
	capture.start(0, held());
	capture.update(0, held({ CapLeftGrip }));
	CPPUNIT_ASSERT(capture.update(100, held({ CapLeftGrip, CapRightTrigger })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapLeftGrip));
	CPPUNIT_ASSERT(capture.refused() == CapRightTrigger);
	CPPUNIT_ASSERT(std::string(capture.refusedWhy()).find("one hand") != std::string::npos);
	capture.update(200, held({ CapRightTrigger }));
	CPPUNIT_ASSERT(capture.update(400, held({ CapRightTrigger })) == CaptureState::Done);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapLeftGrip));
}

void BindingCaptureTest::reservedTest() {

	const char * why = nullptr;
	CPPUNIT_ASSERT(!isRemappable(CapLeftStickUp, &why));
	CPPUNIT_ASSERT_EQUAL(std::string("the left stick walks"), std::string(why));
	CPPUNIT_ASSERT(!isRemappable(CapRightStickLeft, &why));
	CPPUNIT_ASSERT_EQUAL(std::string("the right stick turns"), std::string(why));
	// The right stick's down is the crouch toggle of D-019, its up is free; the stick clicks are buttons
	CPPUNIT_ASSERT(isRemappable(CapRightStickDown, &why));
	CPPUNIT_ASSERT_EQUAL(std::string(), std::string(why));
	CPPUNIT_ASSERT(isRemappable(control(Hand::Right, ControlKind::StickUp)));
	CPPUNIT_ASSERT(isRemappable(CapLeftStickClick));
	CPPUNIT_ASSERT(!isRemappable(Binding::None));

	// Walking while the capture waits does not bind the walk stick
	BindingCapture capture;
	capture.start(0, held());
	CPPUNIT_ASSERT(capture.update(16, held({ CapLeftStickUp })) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.refused() == CapLeftStickUp);
	CPPUNIT_ASSERT(capture.update(32, held(), held({ CapRightStickLeft })) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.refused() == CapRightStickLeft);
	CPPUNIT_ASSERT(capture.update(48, held({ CapRightStickDown })) == CaptureState::Holding);
	CPPUNIT_ASSERT(capture.binding() == Binding::single(CapRightStickDown));
}

void BindingCaptureTest::timeoutTest() {

	BindingCapture capture;
	capture.start(0, held());
	CPPUNIT_ASSERT(capture.update(7999, held()) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.update(8000, held()) == CaptureState::Cancelled);
	CPPUNIT_ASSERT(!capture.active());

	// The time counts from the release of the controls held from before
	capture.start(0, held({ CapRightTrigger }));
	capture.update(5000, held({ CapRightTrigger }));
	CPPUNIT_ASSERT(capture.update(12000, held()) == CaptureState::Waiting);
	CPPUNIT_ASSERT(capture.update(13001, held()) == CaptureState::Cancelled);

	// A held control never times out; cancel ends any capture
	capture.start(0, held());
	capture.update(0, held({ CapRightA }));
	CPPUNIT_ASSERT(capture.update(20000, held({ CapRightA })) == CaptureState::Holding);
	capture.cancel();
	CPPUNIT_ASSERT(capture.state() == CaptureState::Cancelled);
}

void BindingCaptureTest::labelTest() {

	CPPUNIT_ASSERT_EQUAL(std::string("Right A"), controlLabel(CapRightA, ControllerType::Index));
	// The left Touch's face buttons are X and Y
	CPPUNIT_ASSERT_EQUAL(std::string("Left X"), controlLabel(CapLeftA, ControllerType::Touch));
	CPPUNIT_ASSERT_EQUAL(std::string("Left Y"), controlLabel(CapLeftB, ControllerType::Touch));
	CPPUNIT_ASSERT_EQUAL(std::string("Left B"), controlLabel(CapLeftB, ControllerType::Index));
	CPPUNIT_ASSERT_EQUAL(std::string("Right B"), controlLabel(CapRightB, ControllerType::Touch));

	CPPUNIT_ASSERT_EQUAL(std::string("---"), bindingLabel(Binding(), ControllerType::Index));
	CPPUNIT_ASSERT_EQUAL(std::string("Left grip + Left trigger"),
	                     bindingLabel(Binding::chord(CapLeftGrip, CapLeftTrigger), ControllerType::Index));
	CPPUNIT_ASSERT_EQUAL(std::string("Left stick click (hold)"),
	                     bindingLabel(Binding::hold(CapLeftStickClick), ControllerType::Index));
}

void BindingCaptureTest::layoutChangesTest() {

	const ControlLayout base = defaultLayout(ControllerType::Index);
	CPPUNIT_ASSERT_EQUAL(std::string(), formatLayoutChanges(base, base));

	// Only the remapped events are written; read over the default they give the layout back
	ControlLayout layout = base;
	layout[Event::Jump] = Binding::single(CapRightB);
	layout[Event::MagicMode] = Binding::chord(CapLeftGrip, CapLeftA);
	layout[Event::Recenter] = Binding();
	const std::string text = formatLayoutChanges(layout, base);
	CPPUNIT_ASSERT_EQUAL(std::string("jump = right.b\nrecenter = none\nmagic_mode = left.grip+left.a\n"), text);

	ControlLayout loaded = base;
	std::string error;
	CPPUNIT_ASSERT(parseLayout("# a comment\n" + text, loaded, &error));
	CPPUNIT_ASSERT(loaded == layout);

	// Jump on right B now shares it with the inventory: shown as a conflict
	CPPUNIT_ASSERT(findConflicts(layout).size() == 1);
	CPPUNIT_ASSERT(findConflicts(layout)[0].first == Event::Jump);
	CPPUNIT_ASSERT(findConflicts(layout)[0].second == Event::Inventory);
}
