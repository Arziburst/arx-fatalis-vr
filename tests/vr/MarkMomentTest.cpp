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

#include "vr/MarkMomentTest.h"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "platform/Platform.h"
#include "vr/MarkMoment.h"
#include "vr/VRControls.h"

CppUnit::AutoRegisterSuite<MarkMomentTest> g_registerMarkMomentTest;

using namespace vr;

namespace {

const Control MmLeftStickClick = control(Hand::Left, ControlKind::StickClick);
const Control MmRightStickClick = control(Hand::Right, ControlKind::StickClick);
const Control MmLeftTrigger = control(Hand::Left, ControlKind::Trigger);
const Control MmLeftPad = control(Hand::Left, ControlKind::Trackpad);
const Control MmRightPad = control(Hand::Right, ControlKind::Trackpad);
const Control MmRightA = control(Hand::Right, ControlKind::A);
const Control MmRightGrip = control(Hand::Right, ControlKind::Grip);

//! One update of the resolver at 90 Hz
const s64 MmStepMs = 11;
//! A release long enough to be one (past ControlResolver::releaseDebounceMs, the analog flicker window)
const s64 MmReleaseMs = 300;

ControlSet mmSet(std::initializer_list<Control> list = { }) {
	ControlSet result;
	for(Control c : list) {
		result[c] = true;
	}
	return result;
}

//! The gesture (the owner, 2026-10-20): both Index trackpads pressed
const ControlSet MmBoth = mmSet({ MmLeftPad, MmRightPad });
//! No longer anything special: both stick clicks
const ControlSet MmSticks = mmSet({ MmLeftStickClick, MmRightStickClick });

//! What the gesture did over a stretch of updates
struct MmHold {
	u32 gestures = 0;     //!< updates with markGesture() on
	s64 firstMs = -1;     //!< when it first came on, counted from the stretch's first update; -1: never
};

//! The resolver, one update after another as VRInput.cpp runs it (90 Hz)
struct MmFrames {

	ControlResolver resolver;
	s64 time = 1000;

	explicit MmFrames(const ControlLayout & layout = ControlLayout()) : resolver(layout) { }

	/*!
	 * \a down for \a durationMs (\a taps: pressed and released before the first update); counted from the stretch's
	 * first update, which is the first with \a down held
	 */
	MmHold hold(const ControlSet & down, s64 durationMs, const ControlSet & taps = ControlSet()) {
		MmHold result;
		const s64 start = time + MmStepMs;
		bool first = true;
		while(first || time + MmStepMs - start <= durationMs) {
			time += MmStepMs;
			resolver.update(time, down, first ? taps : ControlSet());
			first = false;
			if(resolver.markGesture()) {
				result.gestures++;
				if(result.firstMs < 0) {
					result.firstMs = time - start;
				}
			}
		}
		return result;
	}

	//! Nothing held for \a durationMs
	MmHold release(s64 durationMs = MmReleaseMs) {
		return hold(ControlSet(), durationMs);
	}

};

std::string mmAt(const std::string & name, const char * what) {
	return name + ": " + what;
}

void mmAssertMarks(const std::string & name, const MmHold & hold) {
	CPPUNIT_ASSERT_EQUAL_MESSAGE(mmAt(name, "both trackpads pressed mark the moment once"), u32(1), hold.gestures);
	CPPUNIT_ASSERT_EQUAL_MESSAGE(mmAt(name, "... at once: on the update both are down"), s64(0), hold.firstMs);
}

//! The Index default with no way to the game menu left: no menu event, no radial menu
ControlLayout mmLockedOutLayout() {
	ControlLayout layout = defaultLayout(ControllerType::Index);
	layout[Event::Menu] = Binding();
	layout[Event::RadialLeft] = Binding();
	layout[Event::RadialRight] = Binding();
	return layout;
}

} // anonymous namespace

void MarkMomentTest::gestureAtOnceTest() {

	MmFrames f(defaultLayout(ControllerType::Index));
	CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.markGestures());
	CPPUNIT_ASSERT(!f.resolver.markGesture());

	// Both pressed on the same update: the mark on that update, no hold (the owner, 2026-10-20)
	f.time += MmStepMs;
	f.resolver.update(f.time, MmBoth);
	CPPUNIT_ASSERT_MESSAGE("both trackpads pressed: the mark on that very update", f.resolver.markGesture());
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());
	f.time += MmStepMs;
	f.resolver.update(f.time, MmBoth);
	CPPUNIT_ASSERT_MESSAGE("the gesture is one update long: off on the next, both still held", !f.resolver.markGesture());
	f.release();

	// Pressed for a single update and let go: that is a mark too
	mmAssertMarks("both for one update", f.hold(MmBoth, 0));
	f.release();
	mmAssertMarks("both for 100 ms", f.hold(MmBoth, 100));
	CPPUNIT_ASSERT_EQUAL(u32(3), f.resolver.markGestures());

	// Released: nothing
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the release marks nothing", u32(0), f.release(1000).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(3), f.resolver.markGestures());
}

void MarkMomentTest::gestureOncePerPressTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	// Held on 12 s: the one mark of the press
	mmAssertMarks("held 12 s", f.hold(MmBoth, 12000));
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());

	// The left let go (the right still held) and pressed again: a second mark, at once
	CPPUNIT_ASSERT_EQUAL_MESSAGE("one trackpad let go marks nothing", u32(0),
	                             f.hold(mmSet({ MmRightPad }), MmReleaseMs).gestures);
	mmAssertMarks("the left pressed again", f.hold(MmBoth, 2000));
	// The right let go and pressed again
	CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(mmSet({ MmLeftPad }), MmReleaseMs).gestures);
	mmAssertMarks("the right pressed again", f.hold(MmBoth, 2000));
	// Both let go, both again
	f.release();
	mmAssertMarks("both pressed again", f.hold(MmBoth, 2000));
	CPPUNIT_ASSERT_EQUAL(u32(4), f.resolver.markGestures());
}

void MarkMomentTest::gestureSecondLaterTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	// The left first, the right 1 s later: nothing while one is down, the mark when the second goes down
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the left alone for 1 s marks nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad }), 1000).gestures);
	mmAssertMarks("the right 1 s after the left", f.hold(MmBoth, 1500));
	f.release();

	// The other way round
	CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(mmSet({ MmRightPad }), 1000).gestures);
	mmAssertMarks("the left 1 s after the right", f.hold(MmBoth, 1500));
	CPPUNIT_ASSERT_EQUAL(u32(2), f.resolver.markGestures());
}

void MarkMomentTest::gestureOneTrackpadTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the left trackpad alone held 5 s marks nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad }), 5000).gestures);
	f.release();
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the right trackpad alone held 5 s marks nothing", u32(0),
	                             f.hold(mmSet({ MmRightPad }), 5000).gestures);
	f.release();

	// One trackpad, again and again, never both
	for(int i = 0; i < 3; i++) {
		CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(mmSet({ MmLeftPad }), 200).gestures);
		CPPUNIT_ASSERT_EQUAL(u32(0), f.release().gestures);
		CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(mmSet({ MmRightPad }), 200).gestures);
		CPPUNIT_ASSERT_EQUAL(u32(0), f.release().gestures);
	}

	// One trackpad with the other hand's stick click: not the gesture
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the left trackpad and the right stick click mark nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad, MmRightStickClick }), 2000).gestures);
	f.release();

	// Other controls alone mark nothing; held with both trackpads they change nothing
	CPPUNIT_ASSERT_EQUAL_MESSAGE("triggers, grips and A mark nothing", u32(0),
	                             f.hold(mmSet({ MmLeftTrigger, MmRightGrip, MmRightA }), 2000).gestures);
	f.release();
	ControlSet more = MmBoth;
	more[MmLeftTrigger] = true;
	mmAssertMarks("both trackpads and the left trigger", f.hold(more, 500));
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());
}

void MarkMomentTest::gestureSticksDoNotMarkTest() {

	// Both stick clicks are nothing special (the owner, 2026-10-20): pressed together, held 12 s, one before the
	// other, with any layout - no mark
	const ControlLayout layouts[] = { defaultLayout(ControllerType::Index), ControlLayout(), mmLockedOutLayout() };
	for(const ControlLayout & layout : layouts) {
		MmFrames f(layout);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks pressed mark nothing", u32(0), f.hold(MmSticks, 500).gestures);
		f.release();
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks held 12 s mark nothing", u32(0),
		                             f.hold(MmSticks, 12000).gestures);
		f.release();
		f.hold(mmSet({ MmLeftStickClick }), 400);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("the left stick first, then both: nothing", u32(0),
		                             f.hold(MmSticks, 2000).gestures);
		f.release();
		CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.markGestures());

		// ... and the trackpads still mark right after
		mmAssertMarks("both trackpads after the sticks", f.hold(MmBoth, 500));
	}
}

void MarkMomentTest::gestureTouchTest() {

	// Only a controller with trackpads has the gesture: the Index. The Touch (no trackpads), the simple controller
	// and a controller not identified yet have none
	CPPUNIT_ASSERT_MESSAGE("the Index has the mark gesture", markGestureAvailable(ControllerType::Index));
	CPPUNIT_ASSERT_MESSAGE("the Touch has no mark gesture (no trackpads)", !markGestureAvailable(ControllerType::Touch));
	CPPUNIT_ASSERT_MESSAGE("the simple controller has no mark gesture", !markGestureAvailable(ControllerType::Simple));
	CPPUNIT_ASSERT_MESSAGE("an unknown controller has no mark gesture", !markGestureAvailable(ControllerType::Unknown));

	// The Touch's default layout binds no trackpad, and its sticks held do not mark
	const ControlLayout touch = defaultLayout(ControllerType::Touch);
	for(size_t i = 0; i < EventCount; i++) {
		const Binding & binding = touch[Event(i)];
		CPPUNIT_ASSERT_MESSAGE("the Touch layout binds no trackpad",
		                       !binding.isBound() || (controlKind(binding.first) != ControlKind::Trackpad
		                       && (!binding.isChord() || controlKind(binding.second) != ControlKind::Trackpad)));
	}
	MmFrames f(touch);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the Touch's stick clicks held 5 s mark nothing", u32(0),
	                             f.hold(MmSticks, 5000).gestures);
}

void MarkMomentTest::gestureAnyLayoutTest() {

	// Fixed, not part of the layout: nothing bound, no way to the menu, the trackpads bound to other events (a single
	// press, a long press, a chord led by one), the radial menu moved to a trackpad - both pressed still mark at once
	ControlLayout bound = defaultLayout(ControllerType::Index);
	bound[Event::Jump] = Binding::single(MmLeftPad);
	bound[Event::Recenter] = Binding::hold(MmLeftPad);
	bound[Event::Book] = Binding::chord(MmLeftPad, MmLeftTrigger);
	bound[Event::MagicMode] = Binding::hold(MmRightPad);
	bound[Event::Menu] = Binding::single(MmRightA);
	ControlLayout moved = defaultLayout(ControllerType::Index);
	moved[Event::RadialRight] = Binding::hold(MmLeftPad);
	moved[Event::Menu] = Binding::chord(MmRightGrip, MmRightA);

	const std::vector<std::pair<std::string, ControlLayout>> layouts = {
		{ "no binding at all", ControlLayout() },
		{ "no way to the menu", mmLockedOutLayout() },
		{ "both trackpads bound to other events", bound },
		{ "the menu's way moved to a trackpad", moved },
	};
	for(const auto & layout : layouts) {
		MmFrames f(layout.second);
		mmAssertMarks(layout.first, f.hold(MmBoth, 1000));
		f.release();
		f.hold(mmSet({ MmLeftPad }), 500);
		mmAssertMarks(layout.first + " (the left first)", f.hold(MmBoth, 1000));
		CPPUNIT_ASSERT_EQUAL_MESSAGE(mmAt(layout.first, "two presses, two marks"), u32(2), f.resolver.markGestures());
	}

	// A new layout set while both are held (the VR settings page, a controller change): no extra mark
	MmFrames f(defaultLayout(ControllerType::Index));
	mmAssertMarks("before the layout change", f.hold(MmBoth, 500));
	f.resolver.setLayout(bound);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a layout change while both are held makes no second mark", u32(0),
	                             f.hold(MmBoth, 2000).gestures);
}

void MarkMomentTest::markHeldTest() {

	MmFrames f(defaultLayout(ControllerType::Index));
	const auto step = [&f](const ControlSet & down) {
		f.time += MmStepMs;
		f.resolver.update(f.time, down);
	};

	CPPUNIT_ASSERT_MESSAGE("before any mark: not held", !f.resolver.markHeld());

	// One trackpad alone, both stick clicks: never held
	for(int i = 0; i < 90; i++) {
		step(mmSet({ MmLeftPad }));
		CPPUNIT_ASSERT_MESSAGE("one trackpad alone is not the mark's hold", !f.resolver.markHeld());
	}
	for(int i = 0; i < 90; i++) {
		step(MmSticks);
		CPPUNIT_ASSERT_MESSAGE("both stick clicks are not the mark's hold", !f.resolver.markHeld());
	}
	f.release();

	// Both pressed: held from the mark's update, every update while both stay down (2 s)
	step(MmBoth);
	CPPUNIT_ASSERT(f.resolver.markGesture());
	CPPUNIT_ASSERT_MESSAGE("held on the mark's update", f.resolver.markHeld());
	for(int i = 0; i < 180; i++) {
		step(MmBoth);
		CPPUNIT_ASSERT_MESSAGE("held while both stay down", f.resolver.markHeld());
		CPPUNIT_ASSERT(!f.resolver.markGesture());
	}

	// The left let go: not held on that very update, nor while only the right stays down
	step(mmSet({ MmRightPad }));
	CPPUNIT_ASSERT_MESSAGE("either trackpad let go: not held on that update", !f.resolver.markHeld());
	for(int i = 0; i < 30; i++) {
		step(mmSet({ MmRightPad }));
		CPPUNIT_ASSERT(!f.resolver.markHeld());
	}

	// The left pressed again: the second mark, held again
	step(MmBoth);
	CPPUNIT_ASSERT(f.resolver.markGesture());
	CPPUNIT_ASSERT_MESSAGE("the next mark is held again", f.resolver.markHeld());

	// Both let go at once: not held
	step(ControlSet());
	CPPUNIT_ASSERT(!f.resolver.markHeld());

	// A press of both for a single update: held on it, not on the next
	step(MmBoth);
	CPPUNIT_ASSERT(f.resolver.markHeld());
	step(ControlSet());
	CPPUNIT_ASSERT_MESSAGE("a quick press and release: not held on the release's update", !f.resolver.markHeld());

	// Another control let go with both trackpads still down changes nothing
	ControlSet more = MmBoth;
	more[MmLeftTrigger] = true;
	step(more);
	CPPUNIT_ASSERT(f.resolver.markHeld());
	step(MmBoth);
	CPPUNIT_ASSERT_MESSAGE("the trigger let go: still held", f.resolver.markHeld());
	f.release();
	CPPUNIT_ASSERT(!f.resolver.markHeld());
	CPPUNIT_ASSERT_EQUAL(u32(4), f.resolver.markGestures());
}

void MarkMomentTest::noticeTextTest() {

	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#1)"), markNoticeText(1));
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#2)"), markNoticeText(2));
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#12)"), markNoticeText(12));

	// The voice note's indicator (the owner, 2026-10-21): the microphone mark (U+1F3A4), the elapsed time counting up
	const std::string mic = "\xF0\x9F\x8E\xA4";
	CPPUNIT_ASSERT_EQUAL(mic + " 0:00", noteIndicatorText(0));
	CPPUNIT_ASSERT_EQUAL(mic + " 0:00", noteIndicatorText(999));
	CPPUNIT_ASSERT_EQUAL(mic + " 0:01", noteIndicatorText(1000));
	CPPUNIT_ASSERT_EQUAL(mic + " 0:09", noteIndicatorText(9500));
	CPPUNIT_ASSERT_EQUAL(mic + " 0:23", noteIndicatorText(23000));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("whole seconds rounded down", mic + " 0:23", noteIndicatorText(23999));
	CPPUNIT_ASSERT_EQUAL(mic + " 0:59", noteIndicatorText(59999));
	CPPUNIT_ASSERT_EQUAL(mic + " 1:00", noteIndicatorText(60000));
	CPPUNIT_ASSERT_EQUAL(mic + " 1:01", noteIndicatorText(61000));
	CPPUNIT_ASSERT_EQUAL(mic + " 4:59", noteIndicatorText(299999));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the longest note", mic + " 5:00", noteIndicatorText(300000));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no time before the start", mic + " 0:00", noteIndicatorText(-5));

	// The brief confirmation of a saved note, with its length in the same form
	CPPUNIT_ASSERT_EQUAL(mic + " Note saved (0:23)", noteSavedText(23400));
	CPPUNIT_ASSERT_EQUAL(mic + " Note saved (0:00)", noteSavedText(300));
	CPPUNIT_ASSERT_EQUAL(mic + " Note saved (1:05)", noteSavedText(65999));
	CPPUNIT_ASSERT_EQUAL(mic + " Note saved (5:00)", noteSavedText(300000));
}

void MarkMomentTest::folderNameTest() {

	// UTC, whatever the time zone of the machine (the owner's is UTC+3)
	CPPUNIT_ASSERT_EQUAL(std::string("20261020T153012Z-1"), markFolderName(1792510212, 1));
	CPPUNIT_ASSERT_EQUAL(std::string("20261020T153012Z-12"), markFolderName(1792510212, 12));
	CPPUNIT_ASSERT_EQUAL(std::string("20270101T000059Z-3"), markFolderName(1798761659, 3));
	CPPUNIT_ASSERT_EQUAL(std::string("20260228T235959Z-1"), markFolderName(1772323199, 1));
	CPPUNIT_ASSERT_EQUAL(std::string("19700101T000000Z-1"), markFolderName(0, 1));

	// Names sort by time
	CPPUNIT_ASSERT(markFolderName(1792510212, 1) < markFolderName(1792510213, 1));
	CPPUNIT_ASSERT(markFolderName(1772323199, 9) < markFolderName(1792510212, 1));

	// Only characters every file system takes, no separator
	const std::string name = markFolderName(1792510212, 7);
	CPPUNIT_ASSERT(name.find_first_of("\\/:*?\"<>| ") == std::string::npos);
}

void MarkMomentTest::metaFormatTest() {

	MarkMeta meta;
	meta.number = 1;
	meta.unixSeconds = 1792510212;
	meta.area = 10;
	meta.position = Vec3f(1234.5f, -150.f, 4321.3f);
	meta.yaw = 90.f;
	meta.build = "Arx Fatalis VR 1.3-dev abc1234 Release";
	meta.state = "GAMEPLAY";
	meta.session = "focused";

	const std::string expected =
		"# moment mark (#551)\n"
		"mark=1\n"
		"time=2026-10-20T15:30:12Z\n"
		"area=10\n"
		"position=1234.5 -150.0 4321.3\n"
		"yaw=90.0\n"
		"build=Arx Fatalis VR 1.3-dev abc1234 Release\n"
		"state=GAMEPLAY\n"
		"session=focused\n";
	CPPUNIT_ASSERT_EQUAL(expected, formatMarkMeta(meta));

	// No level, at the origin, a second mark
	MarkMeta none;
	none.number = 2;
	none.unixSeconds = 0;
	none.state = "MENU";
	none.session = "visible";
	const std::string text = formatMarkMeta(none);
	CPPUNIT_ASSERT(text.find("\nmark=2\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\ntime=1970-01-01T00:00:00Z\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\narea=0\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\nposition=0.0 0.0 0.0\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\nbuild=\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\nstate=MENU\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\nsession=visible\n") != std::string::npos);
}

void MarkMomentTest::metaOneLinePerFieldTest() {

	// A control character in a text field never makes a line of its own
	MarkMeta meta;
	meta.number = 3;
	meta.unixSeconds = 1792510212;
	meta.area = 4;
	meta.build = "1.3-dev\nmark=99\r\tRelease";
	meta.state = "GAME\nPLAY";
	meta.session = "focused\n";
	const std::string text = formatMarkMeta(meta);

	size_t lines = 0;
	for(char c : text) {
		lines += (c == '\n') ? 1 : 0;
	}
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the comment and eight fields: nine lines", size_t(9), lines);
	CPPUNIT_ASSERT(text.find('\r') == std::string::npos);
	CPPUNIT_ASSERT(text.find('\t') == std::string::npos);
	CPPUNIT_ASSERT_MESSAGE("a new line in the build is a space", text.find("\nbuild=1.3-dev mark=99  Release\n") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE("... so it adds no mark= line", text.find("\nmark=99") == std::string::npos);
	CPPUNIT_ASSERT(text.find("\nstate=GAME PLAY\n") != std::string::npos);
	CPPUNIT_ASSERT(text.find("\nsession=focused \n") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE("the last line ends the text", !text.empty() && text.back() == '\n');
}

void MarkMomentTest::replayAndSimulatorTest() {

	// A replay (#550) plays the recorded trackpad presses back: they wrote their marks in the recorded session
	CPPUNIT_ASSERT_MESSAGE("no replay playing: the gesture marks", markAllowed(false));
	CPPUNIT_ASSERT_MESSAGE("a replay playing: the gesture writes no mark", !markAllowed(true));

	// The simulated headset (CI, a replay, a worker's run) never opens the real microphone
	CPPUNIT_ASSERT_MESSAGE("a real headset: the voice note may open the microphone", noteMicrophoneAllowed(false));
	CPPUNIT_ASSERT_MESSAGE("the simulated headset: no microphone", !noteMicrophoneAllowed(true));
}

void MarkMomentTest::noticeTransitionTest() {

	const std::string mic = "\xF0\x9F\x8E\xA4";
	std::string text;

	// Nothing marked: no notice, no line
	MarkNoticeState state;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::None);
	CPPUNIT_ASSERT(text.empty());

	// The mark, a quick press: the confirmation, never the indicator before the shortest note
	state.sinceMarkedMs = 0;
	state.markedNumber = 4;
	state.recording = true;
	state.recordingMs = 200;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Marked);
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#4)"), text);

	// Held on: the indicator counts up, in place of the confirmation
	state.recordingMs = 5300;
	state.sinceMarkedMs = 5300;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Recording);
	CPPUNIT_ASSERT_EQUAL(mic + " 0:05", text);

	// Let go: the note.wav is being written - the indicator stays, frozen at the note's length, never blank
	state.recording = false;
	state.recordingMs = 0;
	state.saving = true;
	state.savingLengthMs = 5300;
	for(s64 later : { s64(5311), s64(6000), s64(9000) }) {
		state.sinceMarkedMs = later;
		CPPUNIT_ASSERT_MESSAGE("between the release and the write: the indicator", markNotice(state, text) == MarkNotice::Recording);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("... frozen at the final time", mic + " 0:05", text);
	}

	// Written: "Note saved" at once, for about 1.5 s, then nothing
	state.saving = false;
	state.sinceSavedMs = 0;
	state.savedLengthMs = 5300;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Saved);
	CPPUNIT_ASSERT_EQUAL(mic + " Note saved (0:05)", text);
	state.sinceSavedMs = NoteSavedMs - 1;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Saved);
	state.sinceSavedMs = NoteSavedMs;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::None);
	CPPUNIT_ASSERT(text.empty());

	// A mark after a saved note: the confirmation for 2 s
	state.sinceMarkedMs = MarkNoticeMs - 1;
	state.markedNumber = 5;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::Marked);
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#5)"), text);
	state.sinceMarkedMs = MarkNoticeMs;
	CPPUNIT_ASSERT(markNotice(state, text) == MarkNotice::None);
}
