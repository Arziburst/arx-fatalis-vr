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

//! The owner's hold time (#551): 3 s
const s64 MmHoldMs = 3000;
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
	CPPUNIT_ASSERT_EQUAL_MESSAGE(mmAt(name, "both trackpads held past 3 s mark the moment once"), u32(1), hold.gestures);
	CPPUNIT_ASSERT_MESSAGE(mmAt(name, "... not before 3 s of holding both"), hold.firstMs >= MmHoldMs);
	CPPUNIT_ASSERT_MESSAGE(mmAt(name, "... at the first update at or after 3 s"), hold.firstMs < MmHoldMs + MmStepMs);
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

void MarkMomentTest::gestureAtThreeSecondsTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the hold is 3 s (the owner, #551)", MmHoldMs, ControlResolver::markHoldMs);

	MmFrames f(defaultLayout(ControllerType::Index));
	CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.markGestures());
	CPPUNIT_ASSERT(!f.resolver.markGesture());

	// Held 3.5 s: the gesture comes on once, at 3 s, for one update
	mmAssertMarks("the Index default", f.hold(MmBoth, 3500));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("markGestures counts it", u32(1), f.resolver.markGestures());
	CPPUNIT_ASSERT_MESSAGE("the gesture is one update long: off on the next", !f.resolver.markGesture());

	// Released: nothing more
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the release marks nothing", u32(0), f.release(1000).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());

	// One trackpad first, the other 0.4 s later: the 3 s count from when both are down
	f.hold(mmSet({ MmLeftPad }), 400);
	mmAssertMarks("the left first", f.hold(MmBoth, 3500));
	f.release();
	f.hold(mmSet({ MmRightPad }), 400);
	mmAssertMarks("the right first", f.hold(MmBoth, 3500));
	CPPUNIT_ASSERT_EQUAL(u32(3), f.resolver.markGestures());
}

void MarkMomentTest::gestureOncePerHoldTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	// Held 12 s: one mark, not one every 3 s
	MmHold hold = f.hold(MmBoth, 12000);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("12 s of holding both is one mark: a second needs the trackpads released first",
	                             u32(1), hold.gestures);
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());

	// Released, held again: the second mark
	f.release();
	mmAssertMarks("the second hold", f.hold(MmBoth, 3500));
	CPPUNIT_ASSERT_EQUAL(u32(2), f.resolver.markGestures());

	// Only ONE trackpad let go and pressed again is a release too
	f.hold(mmSet({ MmLeftPad }), MmReleaseMs);
	mmAssertMarks("the right let go and pressed again", f.hold(MmBoth, 3500));
	f.hold(mmSet({ MmRightPad }), MmReleaseMs);
	mmAssertMarks("the left let go and pressed again", f.hold(MmBoth, 3500));
	CPPUNIT_ASSERT_EQUAL(u32(4), f.resolver.markGestures());
}

void MarkMomentTest::gestureShortHoldTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	// Released before 3 s: nothing, whatever the time short of it
	for(s64 ms : { s64(0), s64(300), s64(1200), s64(2000), s64(2500), s64(2900), s64(2980) }) {
		const MmHold hold = f.hold(MmBoth, ms);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both trackpads let go before 3 s mark nothing (" + std::to_string(ms) + " ms)",
		                             u32(0), hold.gestures);
		CPPUNIT_ASSERT_EQUAL(u32(0), f.release().gestures);
	}

	// A tap of both (pressed and released between two updates)
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a tap of both marks nothing", u32(0), f.hold(ControlSet(), 0, MmBoth).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(0), f.release(4000).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.markGestures());
}

void MarkMomentTest::gestureReleaseResetsTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	// 2 s, both released, 2 s again: 4 s in all, but no 3 s in one hold
	CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(MmBoth, 2000).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(0), f.release().gestures);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a release resets the count: 2 s + 2 s is no mark", u32(0), f.hold(MmBoth, 2000).gestures);
	f.release();

	// 2 s, the right let go for a moment while the left stays held, then both again: the count starts at the re-press
	CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(MmBoth, 2000).gestures);
	CPPUNIT_ASSERT_EQUAL(u32(0), f.hold(mmSet({ MmLeftPad }), MmReleaseMs).gestures);
	const MmHold hold = f.hold(MmBoth, 3500);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("one trackpad let go resets the count too", u32(1), hold.gestures);
	CPPUNIT_ASSERT_MESSAGE("... the 3 s count from the re-press", hold.firstMs >= MmHoldMs);
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());
}

void MarkMomentTest::gestureOneTrackpadTest() {

	MmFrames f(defaultLayout(ControllerType::Index));

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the left trackpad alone held 3 s marks nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad }), 3500).gestures);
	f.release();
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the right trackpad alone held 5 s marks nothing", u32(0),
	                             f.hold(mmSet({ MmRightPad }), 5000).gestures);
	f.release();

	// The left held 5 s, the right only tapped after 1 s: both together only for a moment
	f.hold(mmSet({ MmLeftPad }), 1000);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("one trackpad held and the other tapped marks nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad }), 4000, mmSet({ MmRightPad })).gestures);
	f.release();

	// One trackpad with the other hand's stick click: not the gesture
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the left trackpad and the right stick click held 5 s mark nothing", u32(0),
	                             f.hold(mmSet({ MmLeftPad, MmRightStickClick }), 5000).gestures);
	f.release();

	// Other controls held with them change nothing; other controls alone mark nothing
	CPPUNIT_ASSERT_EQUAL_MESSAGE("both triggers and grips held 5 s mark nothing", u32(0),
	                             f.hold(mmSet({ MmLeftTrigger, MmRightGrip, MmRightA }), 5000).gestures);
	f.release();
	ControlSet more = MmBoth;
	more[MmLeftTrigger] = true;
	mmAssertMarks("both trackpads and the left trigger", f.hold(more, 3500));
	CPPUNIT_ASSERT_EQUAL(u32(1), f.resolver.markGestures());
}

void MarkMomentTest::gestureSticksDoNotMarkTest() {

	// Both stick clicks are nothing special any more (the owner, 2026-10-20): held 3 s, 5 s, 12 s, one before the
	// other, with any layout - no mark
	const ControlLayout layouts[] = { defaultLayout(ControllerType::Index), ControlLayout(), mmLockedOutLayout() };
	for(const ControlLayout & layout : layouts) {
		MmFrames f(layout);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks held 3 s mark nothing", u32(0), f.hold(MmSticks, 3500).gestures);
		f.release();
		CPPUNIT_ASSERT_EQUAL_MESSAGE("both stick clicks held 12 s mark nothing", u32(0), f.hold(MmSticks, 12000).gestures);
		f.release();
		f.hold(mmSet({ MmLeftStickClick }), 400);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("the left stick first, then both 5 s: nothing", u32(0), f.hold(MmSticks, 5000).gestures);
		f.release();
		CPPUNIT_ASSERT_EQUAL(u32(0), f.resolver.markGestures());

		// ... and the trackpads still mark right after
		mmAssertMarks("both trackpads after the sticks", f.hold(MmBoth, 3500));
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
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the Touch's stick clicks held 5 s mark nothing", u32(0), f.hold(MmSticks, 5000).gestures);
}

void MarkMomentTest::gestureAnyLayoutTest() {

	// Fixed, not part of the layout: nothing bound, no way to the menu, the trackpads bound to other
	// events (a single press, a long press, a chord led by one) - the hold of both still marks at 3 s
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
		{ "the menu's way moved", moved },
	};
	for(const auto & layout : layouts) {
		MmFrames f(layout.second);
		mmAssertMarks(layout.first, f.hold(MmBoth, 3500));
		f.release();
		mmAssertMarks(layout.first + " (again)", f.hold(MmBoth, 3500));
		CPPUNIT_ASSERT_EQUAL_MESSAGE(mmAt(layout.first, "two holds, two marks"), u32(2), f.resolver.markGestures());
	}

	// A new layout set while the trackpads are held (the VR settings page, a controller change): no extra mark
	MmFrames f(defaultLayout(ControllerType::Index));
	f.hold(MmBoth, 2000);
	f.resolver.setLayout(bound);
	const MmHold rest = f.hold(MmBoth, 3000);
	CPPUNIT_ASSERT_MESSAGE("a layout change during the hold makes at most the one mark of the hold", rest.gestures <= 1);
}

void MarkMomentTest::noticeTextTest() {

	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#1)"), markNoticeText(1));
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#2)"), markNoticeText(2));
	CPPUNIT_ASSERT_EQUAL(std::string("Moment marked (#12)"), markNoticeText(12));

	CPPUNIT_ASSERT_EQUAL(std::string("Recording note... (0 s)"), noteNoticeText(0));
	CPPUNIT_ASSERT_EQUAL(std::string("Recording note... (3 s)"), noteNoticeText(3));
	CPPUNIT_ASSERT_EQUAL(std::string("Recording note... (10 s)"), noteNoticeText(10));
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
