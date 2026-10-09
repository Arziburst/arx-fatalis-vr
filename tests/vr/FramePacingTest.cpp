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

#include "vr/FramePacingTest.h"

#include <string>

CppUnit::AutoRegisterSuite<FramePacingTest> g_registerFramePacingTest;

namespace {

const s64 Window = 3000000; //!< the report window, 3 s

} // anonymous namespace

vr::FrameTiming FramePacingTest::Loop::frame(s64 interval, s64 wait, s64 work, s64 end) {
	vr::FrameTiming t;
	t.waitEnd = first ? 1000000 : lastWaitEnd + interval;
	t.waitStart = t.waitEnd - wait;
	t.endStart = t.waitEnd + work;
	t.endEnd = t.endStart + end;
	t.displayPeriod = period;
	t.shown = true;
	first = false;
	lastWaitEnd = t.waitEnd;
	lastEndEnd = t.endEnd;
	return t;
}

unsigned FramePacingTest::feed(vr::PacingWindow & window, Loop & loop, unsigned count, s64 interval,
                               s64 wait, s64 work, s64 end) {
	unsigned reports = 0;
	for(unsigned i = 0; i < count; i++) {
		if(window.add(loop.frame(interval, wait, work, end))) {
			reports++;
		}
	}
	return reports;
}

void FramePacingTest::testMatchedEvenAtTheNativeRate() {

	vr::PacingWindow window(Window);
	Loop loop;

	// 80 Hz: one frame per 12.5 ms slot, 11 ms of it inside xrWaitFrame
	CPPUNIT_ASSERT_EQUAL(1u, feed(window, loop, 300, 12500));

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT(r.valid);
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::Matched, r.verdict);
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Even, r.cadence);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(12.5, r.periodMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(80.0, r.slotsPerSecond, 0.01);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(80.0, r.framesPerSecond, 0.01);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, r.slotsPerFrame, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, r.cadenceSdMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.0, r.waitMeanMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, r.endMeanMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.5, r.budgetMeanMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.5, r.budgetMaxMs, 0.001);
	// From xrEndFrame to the next xrWaitFrame: 12.5 - 11 - 0.5 - 0.5
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, r.gameMeanMs, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(12.5, r.worstFrameMs, 0.001);
	CPPUNIT_ASSERT_EQUAL(u32(0), r.worstRunFrames);
	CPPUNIT_ASSERT_EQUAL(u32(1), r.divisor);
	CPPUNIT_ASSERT(r.frames >= 240);
	CPPUNIT_ASSERT_EQUAL(r.frames, r.shown);
	CPPUNIT_ASSERT(r.seconds >= 2.99);

}

void FramePacingTest::testHalfRateWithTheCapIsMatchedAndEven() {

	vr::PacingWindow window(Window);
	window.setDivisor(2);
	Loop loop;

	// Every second slot, exactly: the even-cadence cap at work
	CPPUNIT_ASSERT(feed(window, loop, 200, 25000) >= 1);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT(r.valid);
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::Matched, r.verdict);
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Even, r.cadence);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, r.slotsPerFrame, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(40.0, r.framesPerSecond, 0.01);
	CPPUNIT_ASSERT_EQUAL(u32(2), r.divisor);

}

void FramePacingTest::testHalfRateWithoutTheCapIsUnderSubmitting() {

	vr::PacingWindow window(Window);
	Loop loop;

	// The same frames judged against every slot: half of them go unfilled
	CPPUNIT_ASSERT(feed(window, loop, 200, 25000) >= 1);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::UnderSubmitting, r.verdict);
	// Still evenly spaced: the picture is slow, not ghosting
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Even, r.cadence);

}

void FramePacingTest::testJitterIsUnevenEvenWhenMatched() {

	vr::PacingWindow window(Window);
	Loop loop;

	// 80 frames per second on average, but 9 ms and 16 ms apart in turn: the ghosting case
	unsigned reports = 0;
	for(unsigned i = 0; i < 300; i++) {
		reports += feed(window, loop, 1, (i % 2) ? 16000 : 9000);
	}
	CPPUNIT_ASSERT(reports >= 1);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::Matched, r.verdict);
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Uneven, r.cadence);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, r.slotsPerFrame, 0.01);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3.5, r.cadenceSdMs, 0.01);

}

void FramePacingTest::testAlternatingSlotsIsUneven() {

	vr::PacingWindow window(Window);
	Loop loop;

	// One slot, two slots, one slot, ...: 1.5 slots per frame on average
	unsigned reports = 0;
	for(unsigned i = 0; i < 300; i++) {
		reports += feed(window, loop, 1, (i % 2) ? 25000 : 12500);
	}
	CPPUNIT_ASSERT(reports >= 1);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Uneven, r.cadence);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.5, r.slotsPerFrame, 0.01);
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::UnderSubmitting, r.verdict);

}

void FramePacingTest::testUnpacedLoopIsOverSubmitting() {

	vr::PacingWindow window(Window);
	Loop loop;

	// 200 frames per second on an 80 Hz headset: xrWaitFrame is not blocking
	CPPUNIT_ASSERT(feed(window, loop, 700, 5000, 100, 200, 100) >= 1);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::OverSubmitting, r.verdict);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(200.0, r.framesPerSecond, 0.01);
	// 0.4 slots per frame is not a whole number of slots
	CPPUNIT_ASSERT_EQUAL(vr::CadenceVerdict::Uneven, r.cadence);

}

void FramePacingTest::testWorstFrameAndWorstRun() {

	vr::PacingWindow window(Window);
	Loop loop;

	feed(window, loop, 100, 12500);
	// One late frame (3 slots), then a run of four frames at 40 ms each
	feed(window, loop, 1, 37500);
	feed(window, loop, 50, 12500);
	feed(window, loop, 4, 40000);
	unsigned reports = feed(window, loop, 100, 12500);
	CPPUNIT_ASSERT_EQUAL(1u, reports);

	const vr::PacingReport & r = window.report();
	CPPUNIT_ASSERT_DOUBLES_EQUAL(40.0, r.worstFrameMs, 0.001);
	CPPUNIT_ASSERT_EQUAL(u32(4), r.worstRunFrames);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(160.0, r.worstRunMs, 0.001);

}

void FramePacingTest::testFrameGapNamesThePhase() {

	vr::PacingWindow window(Window);
	Loop loop;

	feed(window, loop, 50, 12500);

	// A normal frame is no gap
	vr::FrameGap gap;
	window.add(loop.frame(12500), &gap);
	CPPUNIT_ASSERT(!gap.detected);

	// 150 ms with the usual wait and end: the game's own work took it
	window.add(loop.frame(150000), &gap);
	CPPUNIT_ASSERT(gap.detected);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(150.0, gap.ms, 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(12.5, gap.meanMs, 0.001);
	CPPUNIT_ASSERT_EQUAL(std::string("game"), std::string(gap.phase));

	// The runtime held xrWaitFrame for 140 of 150 ms
	feed(window, loop, 20, 12500);
	window.add(loop.frame(150000, 140000), &gap);
	CPPUNIT_ASSERT(gap.detected);
	CPPUNIT_ASSERT_EQUAL(std::string("wait"), std::string(gap.phase));

	// The previous frame's xrEndFrame took 140 ms, so this frame came 160 ms late
	feed(window, loop, 20, 12500);
	window.add(loop.frame(12500, 11000, 500, 140000), &gap);
	CPPUNIT_ASSERT(!gap.detected);
	window.add(loop.frame(160000), &gap);
	CPPUNIT_ASSERT(gap.detected);
	CPPUNIT_ASSERT_EQUAL(std::string("end"), std::string(gap.phase));

	// Below both thresholds: 60 ms is under 80 ms, even though it is 4.8 times the mean
	feed(window, loop, 20, 12500);
	window.add(loop.frame(60000), &gap);
	CPPUNIT_ASSERT(!gap.detected);

}

void FramePacingTest::testReportsComeOncePerWindow() {

	vr::PacingWindow window(Window);
	Loop loop;

	CPPUNIT_ASSERT(!window.report().valid);
	CPPUNIT_ASSERT_EQUAL(u32(0), window.reports());

	// 239 intervals of 12.5 ms = 2.9875 s: not yet
	CPPUNIT_ASSERT_EQUAL(0u, feed(window, loop, 240, 12500));
	CPPUNIT_ASSERT(!window.report().valid);
	// The 241st frame is 3 s after the first: the window closes with it
	CPPUNIT_ASSERT_EQUAL(1u, feed(window, loop, 1, 12500));
	CPPUNIT_ASSERT_EQUAL(u32(241), window.report().frames);
	CPPUNIT_ASSERT_EQUAL(u32(1), window.reports());

	// The next window starts with the next frame and takes another 3 s
	CPPUNIT_ASSERT_EQUAL(0u, feed(window, loop, 240, 12500));
	CPPUNIT_ASSERT_EQUAL(1u, feed(window, loop, 1, 12500));
	CPPUNIT_ASSERT_EQUAL(u32(2), window.reports());
	CPPUNIT_ASSERT_EQUAL(u32(241), window.report().frames);

}

void FramePacingTest::testFlushClosesAShortWindow() {

	vr::PacingWindow window(Window);
	Loop loop;

	// A single frame has no interval: nothing to report
	feed(window, loop, 1, 12500);
	CPPUNIT_ASSERT(!window.flush());
	CPPUNIT_ASSERT(!window.report().valid);

	// Twenty frames (a new session: the single frame above is forgotten): a report, then nothing
	window.reset();
	Loop loop2;
	CPPUNIT_ASSERT_EQUAL(0u, feed(window, loop2, 20, 12500));
	CPPUNIT_ASSERT(window.flush());
	CPPUNIT_ASSERT(window.report().valid);
	CPPUNIT_ASSERT_EQUAL(u32(20), window.report().frames);
	CPPUNIT_ASSERT_EQUAL(vr::SubmitVerdict::Matched, window.report().verdict);
	CPPUNIT_ASSERT(!window.flush());

}

void FramePacingTest::testFormatNamesTheVerdicts() {

	vr::PacingWindow window(Window);
	Loop loop;
	feed(window, loop, 300, 12500);

	std::string line = vr::formatReport(window.report());
	CPPUNIT_ASSERT(line.find("MATCHED") != std::string::npos);
	CPPUNIT_ASSERT(line.find("EVEN CADENCE") != std::string::npos);
	CPPUNIT_ASSERT(line.find("UNEVEN") == std::string::npos);
	CPPUNIT_ASSERT(line.find("80.0/s on 80.0 slots/s") != std::string::npos);
	CPPUNIT_ASSERT(line.find("hmd 12.50 ms") != std::string::npos);
	CPPUNIT_ASSERT(line.find("1.00 slots/frame") != std::string::npos);
	CPPUNIT_ASSERT(line.find("cap:") == std::string::npos);

	// A new session with the cap: the open window and the previous frame are forgotten
	window.reset();
	window.setDivisor(2);
	Loop loop2;
	feed(window, loop2, 200, 25000);
	line = vr::formatReport(window.report());
	CPPUNIT_ASSERT(line.find("2.00 slots/frame") != std::string::npos);
	CPPUNIT_ASSERT(line.find("(cap: every 2. slot)") != std::string::npos);

	CPPUNIT_ASSERT_EQUAL(std::string("OVER-SUBMITTING"), std::string(vr::verdictName(vr::SubmitVerdict::OverSubmitting)));
	CPPUNIT_ASSERT_EQUAL(std::string("UNDER-SUBMITTING"), std::string(vr::verdictName(vr::SubmitVerdict::UnderSubmitting)));
	CPPUNIT_ASSERT_EQUAL(std::string("unknown"), std::string(vr::cadenceName(vr::CadenceVerdict::Unknown)));

}
