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

#include "game/JumpPhysicsTest.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/JumpPhysics.h"

CppUnit::AutoRegisterSuite<JumpPhysicsTest> g_registerJumpPhysicsTest;

namespace {

//! The forward push of a jump with the walk held, per millisecond (PlayerMovementIterate's scale while jumping forward)
constexpr float ForwardPush = 7.9f / 1000.f;

//! Where MotionProbe's jump gauge counts the player as in the air
constexpr float GaugeHeight = 50.f;

/*!
 * One jump, flown step by step as PlayerMovementIterate integrates it (the engine's y points down, this height up):
 * the rise of \ref jumpRise by the step of frames while ascending, then the world's gravity as a force per
 * millisecond of the physics time into the velocity and the velocity into the position; forward the friction, then
 * the push into the velocity, the velocity into the position; the floor ends the step that would cross it.
 */
struct Flight {

	float height = 0.f;
	float forward = 0.f;
	float apex = 0.f;
	float rise = -1.f; //!< the engine's jumplastposition
	bool ascending = true;
	bool landed = false;
	float velocityUp = 0.f;
	float velocityForward = 0.f;
	int steps = 0;
	int stepsAboveGauge = 0;
	float airMs = 0.f; //!< the frames' time until the landing
	float aboveGaugeMs = 0.f; //!< the frames' time above the gauge: what the probe's jumpSeconds measures

	//! \param frameMs the step's length in the frames' time; \param physicsMs the physics time it integrates
	void step(float frameMs, float physicsMs, bool pushForward) {
		if(landed) {
			return;
		}
		steps++;
		airMs += frameMs;
		velocityForward = dampenVelocity(velocityForward, physicsMs);
		if(pushForward) {
			velocityForward += ForwardPush * physicsMs;
		}
		forward += velocityForward * physicsMs;
		if(ascending) {
			if(rise == -1.f) {
				rise = 0.f;
			}
			height += jumpRise(rise, frameMs);
			velocityUp = 0.f;
			if(rise >= 1.f) {
				ascending = false;
			}
		} else {
			velocityUp -= PlayerWorldGravity / PlayerTargetDt * physicsMs;
			height += velocityUp * physicsMs;
			if(height <= 0.f) {
				height = 0.f;
				landed = true;
			}
		}
		apex = std::max(apex, height);
		if(height > GaugeHeight) {
			stepsAboveGauge++;
			aboveGaugeMs += frameMs;
		}
	}

};

//! The jump flown with these frames, repeated until it lands, in fixed steps that integrate \a timeScale their length
Flight fly(const std::vector<float> & framesMs, bool pushForward, float timeScale = JumpTimeScaleDefault) {
	FixedStepClock clock(JumpStepMs);
	Flight flight;
	for(size_t i = 0; !flight.landed; i++) {
		clock.add(framesMs[i % framesMs.size()]);
		while(clock.takeStep()) {
			flight.step(JumpStepMs, JumpStepMs * timeScale, pushForward);
		}
	}
	return flight;
}

//! The same jump flown as the engine's old loop flew it: once 25 ms of frames had added up, one step by all of them
Flight flyByPile(const std::vector<float> & framesMs, bool pushForward) {
	Flight flight;
	float pile = 0.f;
	for(size_t i = 0; !flight.landed; i++) {
		pile += framesMs[i % framesMs.size()];
		while(pile > JumpStepMs) {
			flight.step(pile, pile, pushForward);
			pile -= JumpStepMs;
		}
	}
	return flight;
}

const std::vector<float> Rates = { 72.f, 90.f, 120.f, 144.f };

//! Frames of every rate, two hitches (116 ms, and 200 ms, the frame cap) and a stall among them
const std::vector<float> IrregularFrames = { 11.1f, 116.5f, 6.9f, 33.3f, 8.3f, 47.2f, 13.9f, 200.f, 11.1f, 6.9f, 6.9f, 71.f };

void assertSameFlight(const Flight & a, const Flight & b) {
	CPPUNIT_ASSERT_EQUAL(a.steps, b.steps);
	CPPUNIT_ASSERT_EQUAL(a.stepsAboveGauge, b.stepsAboveGauge);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(a.apex, b.apex, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(a.forward, b.forward, 1e-3);
}

} // anonymous namespace

void JumpPhysicsTest::testTheClockTakesTheSameStepsForTheSameTime() {
	for(float rate : Rates) {
		const float frame = 1000.f / rate;
		FixedStepClock clock(JumpStepMs);
		int steps = 0;
		float total = 0.f;
		for(int i = 0; i < int(rate) * 2; i++) {
			clock.add(frame);
			total += frame;
			while(clock.takeStep()) {
				steps++;
			}
			// Never a step short, never one ahead: the steps and the remainder are the time that went in
			CPPUNIT_ASSERT(clock.stored() >= 0.f);
			CPPUNIT_ASSERT(clock.stored() < JumpStepMs);
			CPPUNIT_ASSERT_DOUBLES_EQUAL(total, float(steps) * JumpStepMs + clock.stored(), 0.01);
		}
		// Two seconds of frames are eighty steps, give or take the frames' rounding at the end
		CPPUNIT_ASSERT(steps == 80 || steps == 79);
	}
}

void JumpPhysicsTest::testAHitchTakesItsStepsAtOnce() {
	FixedStepClock clock(JumpStepMs);
	clock.add(11.1f);
	CPPUNIT_ASSERT(!clock.takeStep());
	clock.add(116.5f);
	int steps = 0;
	while(clock.takeStep()) {
		steps++;
	}
	CPPUNIT_ASSERT_EQUAL(5, steps);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.6f, clock.stored(), 0.01);
	// A frame of walking takes all that is stored, and the clock is empty
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.6f, clock.takeAll(), 0.01);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, clock.stored(), 0.f);
}

void JumpPhysicsTest::testTheRiseIsTheSameWhateverTheStep() {
	// Eight steps of the game's make the whole rise; any steps summing to the rise's time make it too, and no more
	float progress = 0.f;
	float risen = 0.f;
	for(int i = 0; i < 8; i++) {
		risen += jumpRise(progress, JumpStepMs);
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(JumpRiseHeight, risen, 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, progress, 0.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, jumpRise(progress, JumpStepMs), 0.f);
	progress = 0.f;
	risen = jumpRise(progress, 150.f) + jumpRise(progress, 150.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(JumpRiseHeight, risen, 1e-4);
}

void JumpPhysicsTest::testTheFlightIsTheSameAtEveryRate() {
	for(float timeScale : { 1.f, JumpTimeScaleDefault, 1.5f }) {
		const Flight reference = fly({ 1000.f / 90.f }, false, timeScale);
		const Flight referenceForward = fly({ 1000.f / 90.f }, true, timeScale);
		for(float rate : Rates) {
			assertSameFlight(reference, fly({ 1000.f / rate }, false, timeScale));
			assertSameFlight(referenceForward, fly({ 1000.f / rate }, true, timeScale));
		}
		// A flat game's frames too
		assertSameFlight(reference, fly({ 1000.f / 60.f }, false, timeScale));
		assertSameFlight(referenceForward, fly({ 1000.f / 60.f }, true, timeScale));
	}
}

void JumpPhysicsTest::testTheFlightIsTheSameUnderIrregularFrames() {
	assertSameFlight(fly({ 1000.f / 90.f }, false), fly(IrregularFrames, false));
	assertSameFlight(fly({ 1000.f / 90.f }, true), fly(IrregularFrames, true));
}

void JumpPhysicsTest::testTheFlightIsTheGamesJump() {
	// The flat game's arc (refresh-rate.xrs measures it on the simulated headset, the probe placing the crossings
	// between the steps): 130 units up in eight steps (200 ms), above MotionProbe's gauge for 12 steps (300 ms; the
	// probe reads 0.30-0.32 s), on the floor again after 18 steps (450 ms)
	const Flight flight = fly({ 1000.f / 90.f }, false);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(130.f, flight.apex, 1e-4);
	CPPUNIT_ASSERT_EQUAL(12, flight.stepsAboveGauge);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(300.f, flight.aboveGaugeMs, 1e-3);
	CPPUNIT_ASSERT_EQUAL(18, flight.steps);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(450.f, flight.airMs, 1e-3);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, flight.forward, 0.f);
	// With the walk held the push adds up against the friction: a few metres forward (100 units are a metre)
	const Flight forward = fly({ 1000.f / 90.f }, true);
	CPPUNIT_ASSERT_EQUAL(flight.steps, forward.steps);
	CPPUNIT_ASSERT(forward.forward > 100.f);
	CPPUNIT_ASSERT(forward.forward < 500.f);
	// The constants' own arc, jump_time_scale 1: the same rise, a tenth longer in the air (0.34 s above the gauge)
	const Flight plain = fly({ 1000.f / 90.f }, false, 1.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(130.f, plain.apex, 1e-4);
	CPPUNIT_ASSERT_EQUAL(13, plain.stepsAboveGauge);
	CPPUNIT_ASSERT_EQUAL(20, plain.steps);
}

void JumpPhysicsTest::testAVariableStepWouldNotBe() {
	// The old loop, one step by all the time that had added up, flew another jump as soon as the frames were not
	// regular - a hitch made one step of its whole length: this is what the fixed step is for
	const Flight regular = flyByPile({ 1000.f / 90.f }, true);
	const Flight hitched = flyByPile(IrregularFrames, true);
	CPPUNIT_ASSERT(std::abs(regular.aboveGaugeMs - hitched.aboveGaugeMs) > 40.f);
	CPPUNIT_ASSERT(std::abs(regular.forward - hitched.forward) > 10.f);
	// The fixed step flies them the same
	assertSameFlight(fly({ 1000.f / 90.f }, true), fly(IrregularFrames, true));
}
