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

#include "vr/PerfStatsTest.h"

#include <string>

CppUnit::AutoRegisterSuite<PerfStatsTest> g_registerPerfStatsTest;

namespace {

const s64 Period90Hz = 11111; //!< the Index at 90 Hz, microseconds

//! A frame starting at \a start (microseconds) that took \a duration, all of it in \a phase
vr::PerfFrame frame(s64 start, s64 duration, s64 period = Period90Hz,
                    vr::PerfPhase phase = vr::PerfPhase::Render) {
	vr::PerfFrame f;
	f.start = start;
	f.duration = duration;
	f.period = period;
	f.vr = true;
	f.phases[size_t(phase)] = duration;
	return f;
}

//! Feed back-to-back frames of \a duration from \a start; returns the time after the last one
s64 feed(vr::PerfMonitor & monitor, s64 start, unsigned count, s64 duration) {
	for(unsigned i = 0; i < count; i++) {
		monitor.add(frame(start, duration));
		start += duration;
	}
	return start;
}

bool contains(const std::string & text, const std::string & part) {
	return text.find(part) != std::string::npos;
}

} // anonymous namespace

void PerfStatsTest::testSecondStats() {

	vr::PerfMonitor monitor;

	// Second 0: 89 frames of 11 ms and one of 12 ms; nothing closes until second 1 starts
	s64 t = feed(monitor, 0, 89, 11000);
	CPPUNIT_ASSERT(!monitor.add(frame(t, 12000)));
	CPPUNIT_ASSERT(!monitor.second().valid);

	CPPUNIT_ASSERT(monitor.add(frame(1000000, 11000)));
	const vr::PerfSecond & s = monitor.second();
	CPPUNIT_ASSERT(s.valid);
	CPPUNIT_ASSERT_EQUAL(u32(0), s.second);
	CPPUNIT_ASSERT_EQUAL(u32(90), s.frames);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.0, s.minMs, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL((89.0 * 11.0 + 12.0) / 90.0, s.meanMs, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.0, s.p95Ms, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(12.0, s.maxMs, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.111, s.periodMs, 1e-9);
	CPPUNIT_ASSERT(s.vr);
	CPPUNIT_ASSERT_EQUAL(u32(0), s.hitches);
	CPPUNIT_ASSERT_EQUAL(91ull, static_cast<unsigned long long>(monitor.frames()));
}

void PerfStatsTest::testP95IsTheNearestRank() {

	{
		// 95 of 100 frames at 5 ms: the 95th percentile is 5 ms even with five slow ones
		vr::PerfMonitor monitor;
		for(unsigned i = 0; i < 100; i++) {
			monitor.add(frame(0, i < 95 ? 5000 : 40000));
		}
		CPPUNIT_ASSERT(monitor.flush());
		CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, monitor.second().minMs, 1e-9);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, monitor.second().p95Ms, 1e-9);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(40.0, monitor.second().maxMs, 1e-9);
	}

	{
		// 94 fast and 6 slow: the 95th is a slow one
		vr::PerfMonitor monitor;
		for(unsigned i = 0; i < 100; i++) {
			monitor.add(frame(0, i < 94 ? 5000 : 40000));
		}
		CPPUNIT_ASSERT(monitor.flush());
		CPPUNIT_ASSERT_DOUBLES_EQUAL(40.0, monitor.second().p95Ms, 1e-9);
	}

	{
		// Ascending 1..100 ms (out of order): p95 = 95 ms
		vr::PerfMonitor monitor;
		for(int i = 100; i >= 1; i--) {
			monitor.add(frame(0, s64(i) * 1000));
		}
		CPPUNIT_ASSERT(monitor.flush());
		CPPUNIT_ASSERT_DOUBLES_EQUAL(95.0, monitor.second().p95Ms, 1e-9);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(100.0, monitor.second().maxMs, 1e-9);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, monitor.second().minMs, 1e-9);
	}
}

void PerfStatsTest::testHitchThreshold() {

	vr::PerfMonitor monitor;
	vr::PerfHitch hitch;

	// 1.5 display periods at 90 Hz is 16.67 ms: 16.6 ms is late but no hitch, 16.7 ms is one
	monitor.add(frame(0, 16600), &hitch);
	CPPUNIT_ASSERT(!hitch.detected);
	monitor.add(frame(16600, 16700), &hitch);
	CPPUNIT_ASSERT(hitch.detected);
	CPPUNIT_ASSERT_EQUAL(1ull, static_cast<unsigned long long>(hitch.frame));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0166, hitch.atS, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(16.7, hitch.ms, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(16700.0 / 11111.0, hitch.slots, 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(11.111, hitch.periodMs, 1e-9);

	// Every frame resets the verdict
	monitor.add(frame(33300, 11111), &hitch);
	CPPUNIT_ASSERT(!hitch.detected);

	CPPUNIT_ASSERT(monitor.flush());
	CPPUNIT_ASSERT_EQUAL(u32(1), monitor.second().hitches);
}

void PerfStatsTest::testHitchThresholdFollowsTheCap() {

	vr::PerfMonitor monitor;
	vr::PerfHitch hitch;

	// Every second slot (vr_frame_divisor 2): 22.2 ms frames are the plan, 33.4 ms ones a hitch
	vr::PerfFrame capped = frame(0, 22222);
	capped.divisor = 2;
	monitor.add(capped, &hitch);
	CPPUNIT_ASSERT(!hitch.detected);

	capped = frame(22222, 33400);
	capped.divisor = 2;
	monitor.add(capped, &hitch);
	CPPUNIT_ASSERT(hitch.detected);

	CPPUNIT_ASSERT(monitor.flush());
	CPPUNIT_ASSERT_EQUAL(u32(2), monitor.second().divisor);
}

void PerfStatsTest::testNoHitchWithoutAPeriod() {

	vr::PerfMonitor monitor;
	vr::PerfHitch hitch;

	// Before the display is known (start-up) nothing is judged
	monitor.add(frame(0, 500000, 0), &hitch);
	CPPUNIT_ASSERT(!hitch.detected);

	CPPUNIT_ASSERT(monitor.flush());
	CPPUNIT_ASSERT_EQUAL(u32(0), monitor.second().hitches);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, monitor.second().periodMs, 1e-9);
}

void PerfStatsTest::testHitchNamesItsCause() {

	vr::PerfMonitor monitor;
	vr::PerfHitch hitch;

	// A level load from a channel command: 231 ms of it loading, the rest spread over the frame
	vr::PerfFrame f = frame(2000000, 250000, Period90Hz, vr::PerfPhase::Load);
	f.phases[size_t(vr::PerfPhase::Load)] = 231000;
	f.phases[size_t(vr::PerfPhase::Channel)] = 2000;
	f.phases[size_t(vr::PerfPhase::Textures)] = 9000;
	f.phases[size_t(vr::PerfPhase::Render)] = 5000;
	f.phases[size_t(vr::PerfPhase::VrWait)] = 3000;
	f.missed = 21;
	f.uploads = 37;
	monitor.add(f, &hitch);

	CPPUNIT_ASSERT(hitch.detected);
	CPPUNIT_ASSERT(hitch.cause == vr::PerfPhase::Load);
	CPPUNIT_ASSERT_EQUAL(u32(21), hitch.missed);
	CPPUNIT_ASSERT_EQUAL(u32(37), hitch.uploads);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(231.0, hitch.phaseMs[size_t(vr::PerfPhase::Load)], 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(9.0, hitch.phaseMs[size_t(vr::PerfPhase::Textures)], 1e-9);

	std::string line = vr::formatHitch(hitch);
	CPPUNIT_ASSERT(line.rfind("H t=2.000 frame=0 ms=250.0 slots=22.5 period=11.11 missed=21 uploads=37 cause=load |", 0) == 0);
	CPPUNIT_ASSERT(contains(line, " load=231.0 "));
	CPPUNIT_ASSERT(contains(line, " textures=9.0 "));
	CPPUNIT_ASSERT(contains(line, " vrend=0.0"));

	// A stall in a channel command: the channel is the cause
	f = frame(2250000, 200000, Period90Hz, vr::PerfPhase::Channel);
	f.phases[size_t(vr::PerfPhase::Render)] = 4000;
	monitor.add(f, &hitch);
	CPPUNIT_ASSERT(hitch.detected);
	CPPUNIT_ASSERT(hitch.cause == vr::PerfPhase::Channel);
	CPPUNIT_ASSERT(contains(vr::formatHitch(hitch), "cause=channel"));
}

void PerfStatsTest::testSecondSumsMissedUploadsAndPhases() {

	vr::PerfMonitor monitor;

	vr::PerfFrame a = frame(0, 10000);
	a.phases[size_t(vr::PerfPhase::Render)] = 6000;
	a.phases[size_t(vr::PerfPhase::Update)] = 4000;
	a.missed = 1;
	a.uploads = 2;
	monitor.add(a);

	vr::PerfFrame b = frame(10000, 20000);
	b.phases[size_t(vr::PerfPhase::Render)] = 10000;
	b.phases[size_t(vr::PerfPhase::Physics)] = 10000;
	b.missed = 2;
	b.uploads = 3;
	b.vr = false;
	monitor.add(b);

	CPPUNIT_ASSERT(monitor.flush());
	const vr::PerfSecond & s = monitor.second();
	CPPUNIT_ASSERT_EQUAL(u32(3), s.missed);
	CPPUNIT_ASSERT_EQUAL(u32(5), s.uploads);
	CPPUNIT_ASSERT(!s.vr); // a flat frame in the second
	CPPUNIT_ASSERT_DOUBLES_EQUAL(8.0, s.phaseMs[size_t(vr::PerfPhase::Render)], 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.0, s.phaseMs[size_t(vr::PerfPhase::Update)], 1e-9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, s.phaseMs[size_t(vr::PerfPhase::Physics)], 1e-9);

	// The phases are exclusive: their means add up to the mean frame
	double sum = 0.0;
	for(double ms : s.phaseMs) {
		sum += ms;
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(s.meanMs, sum, 1e-9);
}

void PerfStatsTest::testFlushClosesTheOpenSecond() {

	vr::PerfMonitor monitor;
	CPPUNIT_ASSERT(!monitor.flush());

	feed(monitor, 3000000, 10, 11111);
	CPPUNIT_ASSERT(monitor.flush());
	CPPUNIT_ASSERT_EQUAL(u32(3), monitor.second().second);
	CPPUNIT_ASSERT_EQUAL(u32(10), monitor.second().frames);

	// Nothing left to close
	CPPUNIT_ASSERT(!monitor.flush());

	// A long frame leaves the seconds it spans without a line: the next frame's second follows
	monitor.add(frame(4000000, 3000000));
	CPPUNIT_ASSERT(monitor.add(frame(7000000, 11111)));
	CPPUNIT_ASSERT_EQUAL(u32(4), monitor.second().second);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3000.0, monitor.second().maxMs, 1e-9);
}

void PerfStatsTest::testFormatSecond() {

	vr::PerfMonitor monitor;
	feed(monitor, 0, 90, 11111);
	CPPUNIT_ASSERT(monitor.flush());

	vr::PerfSecond s = monitor.second();
	s.selfUs = 1.5;
	s.selfMaxUs = 12.0;
	std::string line = vr::formatSecond(s);
	CPPUNIT_ASSERT(line.rfind("S t=0 frames=90 min=11.11 mean=11.11 p95=11.11 max=11.11 hz=90.0 period=11.11 div=1 vr=1"
	                          " missed=0 hitches=0 | other=0.00 input=0.00 ", 0) == 0);
	CPPUNIT_ASSERT(contains(line, " render=11.11 "));
	CPPUNIT_ASSERT(contains(line, " limit=0.00 vrwait=0.00 vrend=0.00 | uploads=0 self=1.5 selfmax=12"));
}
