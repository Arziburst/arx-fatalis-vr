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

#include "vr/MotionProbeTest.h"

#include <algorithm>
#include <cmath>

#include "vr/MotionProbe.h"

CppUnit::AutoRegisterSuite<MotionProbeTest> g_registerMotionProbeTest;

namespace {

const double ProbeRates[] = { 72.0, 90.0, 120.0, 144.0 };

vr::MotionSample probeSample(double seconds, Vec3f position, float yaw = 0.f, bool jumping = false) {
	vr::MotionSample result;
	result.seconds = seconds;
	result.position = position;
	result.yaw = yaw;
	result.jumping = jumping;
	return result;
}

/*!
 * A jump of \a apex units lasting \a air seconds in the air, a parabola, starting at \a start: the
 * height above the floor at \a t (engine y points down, the floor is at y = 0)
 */
float probeJumpY(double t, double start, double air, float apex) {
	if(t <= start || t >= start + air) {
		return 0.f;
	}
	const double s = (t - start) / air;
	return -float(4.0 * double(apex) * s * (1.0 - s));
}

} // anonymous namespace

void MotionProbeTest::testWalkIsTheSameAtEveryRate() {
	// 192 units per second (the run) for half a second, after standing a moment: 96 units at any rate
	for(double rate : ProbeRates) {
		vr::MotionProbe probe;
		const int frames = int(std::lround(rate * 0.7));
		for(int i = 0; i <= frames; i++) {
			const double t = double(i) / rate;
			const double moving = std::clamp(t - 0.1, 0.0, 0.5);
			probe.update(probeSample(t, Vec3f(float(192.0 * moving), 0.f, 0.f)));
		}
		CPPUNIT_ASSERT_DOUBLES_EQUAL(96.0, double(probe.walked()), 0.01);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(double(frames) / rate, probe.seconds(), 1e-9);
		CPPUNIT_ASSERT_EQUAL(u64(frames), probe.frames());
	}
}

void MotionProbeTest::testTurnUnwrapsAcrossZero() {
	vr::MotionProbe probe;
	probe.update(probeSample(0.0, Vec3f(0.f), 350.f));
	probe.update(probeSample(0.01, Vec3f(0.f), 359.f));
	probe.update(probeSample(0.02, Vec3f(0.f), 10.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, double(probe.turned()), 1e-4);
	probe.update(probeSample(0.03, Vec3f(0.f), 340.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-10.0, double(probe.turned()), 1e-4);
}

void MotionProbeTest::testTeleportIsNoWalk() {
	vr::MotionProbe probe;
	probe.update(probeSample(0.0, Vec3f(0.f)));
	probe.update(probeSample(0.01, Vec3f(2.f, 0.f, 0.f)));
	probe.update(probeSample(0.02, Vec3f(1000.f, 0.f, 0.f))); // a load: 998 units in a frame
	probe.update(probeSample(0.03, Vec3f(1003.f, 0.f, 0.f)));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, double(probe.walked()), 1e-4);
}

void MotionProbeTest::testJumpIsTheSameAtEveryRate() {
	// 130 units high, 0.6 s from the take-off to the landing: the time above 50 units is the same at any rate,
	// though the frames land elsewhere on the curve
	const double start = 0.2, air = 0.6;
	const float apex = 130.f;
	const double leave = start + air * (1.0 - std::sqrt(1.0 - double(vr::MotionProbe::AirborneUnits) / double(apex))) / 2.0;
	const double expected = air - 2.0 * (leave - start);
	for(double rate : ProbeRates) {
		vr::MotionProbe probe;
		const int frames = int(std::lround(rate * 1.2));
		for(int i = 0; i <= frames; i++) {
			const double t = double(i) / rate + 0.003; // off the curve's own times
			const bool jumping = t >= start && t < start + air + 0.1; // the engine's phases outlast the flight
			probe.update(probeSample(t, Vec3f(0.f, probeJumpY(t, start, air, apex), 0.f), 0.f, jumping));
		}
		CPPUNIT_ASSERT_EQUAL(u32(1), probe.jumps());
		CPPUNIT_ASSERT(!probe.airborne());
		// A frame near the top misses it by at most 4 * 130 * (half a 72 Hz frame / 0.6 s)^2 = 0.07 units
		CPPUNIT_ASSERT_DOUBLES_EQUAL(double(apex), double(probe.jumpApex()), 0.1);
		// The crossings between frames are linear on a curve: off by far less than a frame (13.9 ms at 72 Hz)
		CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, probe.jumpSeconds(), 0.001);
	}
}

void MotionProbeTest::testSteppedJumpIsTheSameAtEveryRate() {
	// The engine moves a jumping player in steps of 25-35 ms (#435): between two steps the frames show the last
	// step's position. The same 0.6 s parabola, 130 units high, stepped every 25, 30 and 35 ms from different
	// phases and watched at 72, 90, 120 and 144 Hz: the time above 50 units is the parabola's within a few ms,
	// where placing the crossings between frames was off by up to a step (CI: 0.300 s at 90 Hz, 0.343 at 120)
	const double start = 0.2, air = 0.6;
	const float apex = 130.f;
	const double s = 0.5 * (1.0 - std::sqrt(1.0 - double(vr::MotionProbe::AirborneUnits) / double(apex)));
	const double expected = air * (1.0 - 2.0 * s);
	const double steps[] = { 0.025, 0.030, 0.035 };
	const double phases[] = { 0.0, 0.4, 0.8 };
	for(double rate : ProbeRates) {
		for(double step : steps) {
			for(double phase : phases) {
				vr::MotionProbe probe;
				const int frames = int(std::lround(rate * 1.2));
				for(int i = 0; i <= frames; i++) {
					const double t = double(i) / rate;
					// The last engine step at or before this frame
					const double moveTime = std::floor((t - phase * step) / step) * step + phase * step;
					const double at = std::max(moveTime, 0.0);
					const bool jumping = t >= start && t < start + air + 0.1;
					probe.update(probeSample(t, Vec3f(0.f, probeJumpY(at, start, air, apex), 0.f), 0.f, jumping));
				}
				CPPUNIT_ASSERT_EQUAL(u32(1), probe.jumps());
				CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, probe.jumpSeconds(), 0.012);
			}
		}
	}
}

void MotionProbeTest::testJumpThatNeverLeavesTheGround() {
	// The engine's jump under a low ceiling: the phases run, the player stays on the floor - no jump landed
	vr::MotionProbe probe;
	probe.update(probeSample(0.0, Vec3f(0.f)));
	probe.update(probeSample(0.01, Vec3f(0.f, -0.5f, 0.f), 0.f, true));
	probe.update(probeSample(0.02, Vec3f(0.f), 0.f, true));
	probe.update(probeSample(0.03, Vec3f(0.f), 0.f, false));
	CPPUNIT_ASSERT_EQUAL(u32(0), probe.jumps());
	CPPUNIT_ASSERT(!probe.airborne());
	// The next one is measured from its own take-off
	probe.update(probeSample(0.04, Vec3f(0.f, -60.f, 0.f), 0.f, true));
	CPPUNIT_ASSERT(probe.airborne());
	probe.update(probeSample(0.05, Vec3f(0.f), 0.f, true));
	CPPUNIT_ASSERT_EQUAL(u32(1), probe.jumps());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(60.0, double(probe.jumpApex()), 1e-4);
}

void MotionProbeTest::testResetStartsFromZero() {
	vr::MotionProbe probe;
	probe.update(probeSample(0.0, Vec3f(0.f), 0.f));
	probe.update(probeSample(0.1, Vec3f(10.f, 0.f, 0.f), 30.f));
	probe.reset();
	CPPUNIT_ASSERT_EQUAL(0.f, probe.walked());
	CPPUNIT_ASSERT_EQUAL(0.f, probe.turned());
	CPPUNIT_ASSERT_EQUAL(u64(0), probe.frames());
	// The first frame after the reset counts from where the player was before it
	probe.update(probeSample(0.2, Vec3f(13.f, 0.f, 4.f), 40.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, double(probe.walked()), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, double(probe.turned()), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, probe.seconds(), 1e-9);
}
