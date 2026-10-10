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

#include "vr/MeleeSwingTest.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "vr/MeleeSwing.h"

CppUnit::AutoRegisterSuite<MeleeSwingTest> g_registerMeleeSwingTest;

namespace {

const double Frame = 1.0 / 90.0;

//! The grip held ahead, the blade (grip -Z) pointing forward
const Vec3f Rest(0.2f, -0.4f, -0.35f);

vr::SwingSample sample(double time, Vec3f hand, glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f)) {
	vr::SwingSample s;
	s.time = time;
	s.hand = hand;
	s.orientation = orientation;
	return s;
}

//! Feed \a frames samples moving the hand by \a velocity (m/s) from \a from; \return the changes seen
std::vector<vr::SwingChange> move(vr::SwingDetector & detector, double & time, Vec3f & hand, Vec3f velocity,
                                  int frames) {
	std::vector<vr::SwingChange> changes;
	for(int i = 0; i < frames; i++) {
		time += Frame;
		hand += velocity * float(Frame);
		vr::SwingChange change = detector.update(sample(time, hand), true);
		if(change != vr::SwingChange::None) {
			changes.push_back(change);
		}
	}
	return changes;
}

size_t count(const std::vector<vr::SwingChange> & changes, vr::SwingChange which) {
	size_t n = 0;
	for(vr::SwingChange change : changes) {
		n += (change == which) ? 1 : 0;
	}
	return n;
}

} // anonymous namespace

void MeleeSwingTest::testStillAndSlowHandsDoNotSwing() {
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 90).empty());
	// A reach for something at 1 m/s, and a brisk one at 2 m/s sideways: no swing
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(1.f, 0.f, 0.f), 30).empty());
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f, 2.f, 0.f), 20).empty());
	CPPUNIT_ASSERT(!detector.swinging());
	CPPUNIT_ASSERT_EQUAL(u64(0), detector.swings());
}

void MeleeSwingTest::testSlashStartsAndEnds() {
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	move(detector, time, hand, Vec3f(0.f), 10);
	// A sideways slash at 4 m/s: starts within three samples (the median needs two fast ones)
	std::vector<vr::SwingChange> changes = move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 3);
	CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
	CPPUNIT_ASSERT(detector.swinging());
	CPPUNIT_ASSERT(detector.kind() == vr::SwingKind::Slash);
	changes = move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 15);
	CPPUNIT_ASSERT(changes.empty());
	CPPUNIT_ASSERT(std::abs(detector.peak() - 4.f) < 0.01f);
	// Stopped: ends once the median is below the end speed
	changes = move(detector, time, hand, Vec3f(0.f), 5);
	CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Ended));
	CPPUNIT_ASSERT(!detector.swinging());
	CPPUNIT_ASSERT_EQUAL(u64(1), detector.swings());
	// The next one is a new swing
	changes = move(detector, time, hand, Vec3f(4.f, 0.f, 0.f), 10);
	CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
	CPPUNIT_ASSERT_EQUAL(u64(2), detector.swings());
}

void MeleeSwingTest::testWristFlickIsNoSwing() {
	// The hand stays, the wrist turns the blade 60 degrees in 0.1 s (600 deg/s): the tip of a 0.7 m blade
	// moves at ~7 m/s, the point 0.125 m along it at ~1.3 m/s (F-61)
	for(float lever : { 0.125f, 0.7f }) {
		vr::SwingParams params;
		params.lever = lever;
		vr::SwingDetector detector(params);
		double time = 0.;
		size_t started = 0;
		for(int i = 0; i <= 30; i++) {
			const float t = glm::clamp(float(i - 10) / 9.f, 0.f, 1.f); // turns over 9 frames (0.1 s)
			const glm::quat orientation = glm::angleAxis(glm::radians(-60.f * t), Vec3f(0.f, 1.f, 0.f));
			time += Frame;
			if(detector.update(sample(time, Rest, orientation), true) == vr::SwingChange::Started) {
				started++;
			}
		}
		if(lever < 0.5f) {
			CPPUNIT_ASSERT_EQUAL(size_t(0), started);
		} else {
			CPPUNIT_ASSERT_EQUAL(size_t(1), started); // measured at the tip, the flick would swing
		}
	}
}

void MeleeSwingTest::testOneBadSampleIsNoSwing() {
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	move(detector, time, hand, Vec3f(0.f), 10);
	// One sample 5 cm off (4.5 m/s) and back: the median of three ignores it
	time += Frame;
	CPPUNIT_ASSERT(detector.update(sample(time, Rest + Vec3f(0.05f, 0.f, 0.f)), true) == vr::SwingChange::None);
	time += Frame;
	CPPUNIT_ASSERT(detector.update(sample(time, Rest), true) == vr::SwingChange::None);
	hand = Rest;
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 10).empty());
	CPPUNIT_ASSERT_EQUAL(u64(0), detector.swings());
}

void MeleeSwingTest::testTrackingJump() {
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	move(detector, time, hand, Vec3f(0.f), 10);
	// The controller jumps 40 cm in one frame (36 m/s) and stays there: no swing
	hand += Vec3f(0.4f, 0.f, 0.f);
	time += Frame;
	CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::None);
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 10).empty());
	CPPUNIT_ASSERT_EQUAL(u64(1), detector.jumps());
	CPPUNIT_ASSERT_EQUAL(u64(0), detector.swings());
	// A jump in the middle of a swing drops it
	move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 5);
	CPPUNIT_ASSERT(detector.swinging());
	hand += Vec3f(0.f, 0.5f, 0.f);
	time += Frame;
	CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::Lost);
	CPPUNIT_ASSERT(!detector.swinging());
}

void MeleeSwingTest::testGapAndLostTracking() {
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	move(detector, time, hand, Vec3f(0.f), 5);
	// 150 ms without a frame, the hand 30 cm on (2 m/s - but nobody knows): starts over, no swing
	time += 0.15;
	hand += Vec3f(0.3f, 0.f, 0.f);
	CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::None);
	CPPUNIT_ASSERT_EQUAL(u64(1), detector.gaps());
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 5).empty());
	// A sample repeated within 4 ms is skipped, not a speed of zero
	move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 4);
	CPPUNIT_ASSERT(detector.swinging());
	CPPUNIT_ASSERT(detector.update(sample(time + 0.001, hand), true) == vr::SwingChange::None);
	CPPUNIT_ASSERT(detector.swinging());
	// The controller lost mid-swing: dropped; found again: seeds first, no swing from the jump back
	CPPUNIT_ASSERT(detector.update(sample(time + Frame, hand), false) == vr::SwingChange::Lost);
	time += 2 * Frame;
	hand = Rest;
	CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::None);
	CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 5).empty());
}

void MeleeSwingTest::testStab() {
	// A thrust along the blade (forward, -z) at 2.2 m/s: below the slash speed, a stab after 0.2 m
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		std::vector<vr::SwingChange> changes = move(detector, time, hand, Vec3f(0.f, 0.f, -2.2f), 12);
		CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
		CPPUNIT_ASSERT(detector.kind() == vr::SwingKind::Stab);
		CPPUNIT_ASSERT(detector.thrust() >= 0.2f);
	}
	// The same speed sideways, or pulling the blade back: nothing
	for(Vec3f velocity : { Vec3f(2.2f, 0.f, 0.f), Vec3f(0.f, 0.f, 2.2f) }) {
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		CPPUNIT_ASSERT(move(detector, time, hand, velocity, 20).empty());
	}
	// A short jab (10 cm) is no stab
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f, 0.f, -2.2f), 4).empty());
		CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 10).empty());
	}
}

void MeleeSwingTest::testLongestSwing() {
	// Circling the blade at 4 m/s for 1.5 s: the swing ends after 1 s and a new one starts
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	move(detector, time, hand, Vec3f(0.f), 5);
	size_t started = 0;
	size_t ended = 0;
	for(int i = 0; i < 135; i++) {
		const float angle = float(i) * 4.f / 0.2f * float(Frame); // radius 0.2 m at 4 m/s
		time += Frame;
		hand = Rest + Vec3f(std::cos(angle) - 1.f, std::sin(angle), 0.f) * 0.2f;
		vr::SwingChange change = detector.update(sample(time, hand), true);
		started += (change == vr::SwingChange::Started) ? 1 : 0;
		ended += (change == vr::SwingChange::Ended) ? 1 : 0;
	}
	CPPUNIT_ASSERT_EQUAL(size_t(2), started);
	CPPUNIT_ASSERT_EQUAL(size_t(1), ended);
}

void MeleeSwingTest::testSpeedFactor() {
	vr::SwingParams params;
	for(float speed : { 2.6f, 4.25f, 9.f }) {
		vr::SwingDetector detector(params);
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-speed, 0.f, 0.f), 8);
		CPPUNIT_ASSERT(detector.swinging());
		const float expected = (speed >= params.fullSpeed) ? 1.f
		                       : params.minFactor + (1.f - params.minFactor)
		                         * (speed - params.startSpeed) / (params.fullSpeed - params.startSpeed);
		if(std::abs(detector.factor() - expected) > 0.01f) {
			CPPUNIT_FAIL("speed " + std::to_string(speed) + ": factor " + std::to_string(detector.factor())
			             + ", expected " + std::to_string(expected));
		}
	}
}

void MeleeSwingTest::testCharge() {
	const float full = 1.5f; // the engine's minimum Full_AimTime
	vr::StrikeCharge charge;
	charge.update(false, 0.1f);
	CPPUNIT_ASSERT(!charge.charging());
	CPPUNIT_ASSERT_EQUAL(0.f, charge.ratio(full));
	// A tap gives the engine's floor of 0.1, half the full time 0.5, longer is capped at 1
	charge.update(true, 0.05f);
	CPPUNIT_ASSERT(charge.charging());
	CPPUNIT_ASSERT(std::abs(charge.ratio(full) - 0.1f) < 1e-4f);
	charge.update(true, 0.7f);
	CPPUNIT_ASSERT(std::abs(charge.ratio(full) - 0.5f) < 1e-4f);
	charge.update(true, 3.f);
	CPPUNIT_ASSERT_EQUAL(1.f, charge.ratio(full));
	// A charged hit spends it: held on, it does not charge again
	charge.spend();
	CPPUNIT_ASSERT(!charge.charging());
	CPPUNIT_ASSERT(charge.spent());
	charge.update(true, 3.f);
	CPPUNIT_ASSERT_EQUAL(0.f, charge.ratio(full));
	CPPUNIT_ASSERT_EQUAL(u64(1), charge.spends());
	// Released and pressed again: charges from zero
	charge.update(false, 0.01f);
	CPPUNIT_ASSERT(!charge.spent());
	charge.update(true, 0.75f);
	CPPUNIT_ASSERT(std::abs(charge.ratio(full) - 0.5f) < 1e-4f);
	// Released before a swing: the charge is gone
	charge.update(false, 0.01f);
	CPPUNIT_ASSERT_EQUAL(0.f, charge.ratio(full));
}

void MeleeSwingTest::testStrikeAim() {
	vr::SwingParams params;
	// Uncharged: the weak aim, scaled by the speed factor
	CPPUNIT_ASSERT(std::abs(vr::strikeAim(0.f, 1.f, params) - params.weakAim) < 1e-6f);
	CPPUNIT_ASSERT(std::abs(vr::strikeAim(0.f, 0.5f, params) - params.weakAim * 0.5f) < 1e-6f);
	// Charged: the charge ratio, never weaker than an uncharged swing
	CPPUNIT_ASSERT(std::abs(vr::strikeAim(1.f, 1.f, params) - 1.f) < 1e-6f);
	CPPUNIT_ASSERT(std::abs(vr::strikeAim(1.f, 0.75f, params) - 0.75f) < 1e-6f);
	CPPUNIT_ASSERT(std::abs(vr::strikeAim(0.1f, 1.f, params) - params.weakAim) < 1e-6f);
	// A full charge is four times a plain swing of the same speed
	CPPUNIT_ASSERT(vr::strikeAim(1.f, 0.6f, params) >= 3.9f * vr::strikeAim(0.f, 0.6f, params));
}

void MeleeSwingTest::testTargetCooldown() {
	vr::TargetCooldown cooldown;
	const float wait = 0.25f;
	CPPUNIT_ASSERT(cooldown.allowed(7, 1, 0., wait));
	cooldown.hit(7, 1, 1.0);
	// The same swing never hits the same target twice; another target is fine
	CPPUNIT_ASSERT(!cooldown.allowed(7, 1, 3.0, wait));
	CPPUNIT_ASSERT(cooldown.allowed(8, 1, 1.0, wait));
	// A new swing within the cooldown: not yet; after it: yes
	CPPUNIT_ASSERT(!cooldown.allowed(7, 2, 1.1, wait));
	CPPUNIT_ASSERT(cooldown.allowed(7, 2, 1.3, wait));
	std::vector<size_t> blocked = cooldown.blocked(2, 1.1, wait);
	CPPUNIT_ASSERT_EQUAL(size_t(1), blocked.size());
	CPPUNIT_ASSERT_EQUAL(size_t(7), blocked[0]);
	CPPUNIT_ASSERT(cooldown.blocked(2, 1.3, wait).empty());
	cooldown.hit(7, 2, 1.3);
	CPPUNIT_ASSERT(!cooldown.allowed(7, 2, 5.0, wait));
	CPPUNIT_ASSERT(cooldown.allowed(7, 3, 5.0, wait));
}
