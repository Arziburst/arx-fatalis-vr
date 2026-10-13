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

#include <algorithm>
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

/*!
 * Feed samples of the hand moving by \a velocity (m/s) over the frame times \a steps (seconds); \return the changes
 * seen, and the fastest filtered speed in \a fastest
 */
std::vector<vr::SwingChange> moveSteps(vr::SwingDetector & detector, double & time, Vec3f & hand, Vec3f velocity,
                                       const std::vector<double> & steps, float * fastest = nullptr) {
	std::vector<vr::SwingChange> changes;
	for(double step : steps) {
		time += step;
		hand += velocity * float(step);
		vr::SwingChange change = detector.update(sample(time, hand), true);
		if(change != vr::SwingChange::None) {
			changes.push_back(change);
		}
		if(fastest) {
			*fastest = std::max(*fastest, detector.speed());
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

void MeleeSwingTest::testHitchIsNoSwing() {
	// #352: a hand reaching at 1 m/s with 11 ms frames and one 50 ms hitch moves 5 cm in the long frame; the
	// median moves those 5 cm one sample later, in an 11 ms frame - 4.5 m/s over the frame's step, 1 m/s over
	// the step it really moved over
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 10);
		std::vector<double> steps(10, 0.011);
		steps.push_back(0.05);
		steps.insert(steps.end(), 10, 0.011);
		float fastest = 0.f;
		CPPUNIT_ASSERT(moveSteps(detector, time, hand, Vec3f(1.f, 0.f, 0.f), steps, &fastest).empty());
		CPPUNIT_ASSERT_EQUAL(u64(0), detector.swings());
		if(std::abs(fastest - 1.f) > 0.01f) {
			CPPUNIT_FAIL("1 m/s through a hitch read " + std::to_string(fastest) + " m/s");
		}
	}
	// A stuttering game: 50 ms and 11 ms frames by turns, the hand at 2 m/s (below the swing speed) - every
	// short frame read 9.1 m/s before
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 10);
		std::vector<double> steps;
		for(int i = 0; i < 10; i++) {
			steps.push_back(0.05);
			steps.push_back(0.011);
		}
		float fastest = 0.f;
		CPPUNIT_ASSERT(moveSteps(detector, time, hand, Vec3f(0.f, 2.f, 0.f), steps, &fastest).empty());
		CPPUNIT_ASSERT_EQUAL(u64(0), detector.swings());
		CPPUNIT_ASSERT(std::abs(fastest - 2.f) < 0.01f);
	}
}

void MeleeSwingTest::testUnevenFramesPeak() {
	// A 9.6 m/s swing over frames of 50 ms and 11 ms by turns: one swing, its peak the hand's speed
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 10);
		std::vector<double> steps;
		for(int i = 0; i < 6; i++) {
			steps.push_back(0.05);
			steps.push_back(0.011);
		}
		std::vector<vr::SwingChange> changes = moveSteps(detector, time, hand, Vec3f(-9.6f, 0.f, 0.f), steps);
		CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
		CPPUNIT_ASSERT(detector.swinging());
		if(std::abs(detector.peak() - 9.6f) > 0.05f) {
			CPPUNIT_FAIL("9.6 m/s over uneven frames peaked at " + std::to_string(detector.peak()) + " m/s");
		}
	}
	// The CI case of #352 (run 38089069974): the hand moved 0.43 m in one 99.5 ms frame and then stood, frames of
	// 10.5 ms - the peak is the 4.3 m/s it moved at, not 40.9
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		moveSteps(detector, time, hand, Vec3f(0.f), std::vector<double>(10, 0.0105));
		time += 0.0995;
		hand += Vec3f(0.43f, 0.f, 0.f);
		detector.update(sample(time, hand), true);
		moveSteps(detector, time, hand, Vec3f(0.f), std::vector<double>(10, 0.0105));
		CPPUNIT_ASSERT_EQUAL(u64(0), detector.jumps());
		if(std::abs(detector.peak() - 0.43f / 0.0995f) > 0.01f) {
			CPPUNIT_FAIL("0.43 m in 99.5 ms peaked at " + std::to_string(detector.peak()) + " m/s");
		}
	}
}

void MeleeSwingTest::testSameSwingsAt45And90Hz() {
	// The same three swings (0.85 m in 89 ms, the sequences' ramp, then 0.4 s still) at 90 Hz, at 45 Hz and at
	// an uneven 90 (8 and 14 ms frames by turns): the same swings with the same peaks
	struct Run {
		const char * name;
		std::vector<double> steps; //!< repeated
		u64 swings = 0;
		std::vector<float> peaks;
	};
	Run runs[] = { { "90 Hz", { 1. / 90. } }, { "45 Hz", { 1. / 45. } }, { "uneven 90 Hz", { 0.008, 0.014 } } };
	const double ramp = 8. / 90.;
	const float distance = 0.85f;
	const double starts[] = { 0.5, 1.0, 1.5 };
	for(Run & run : runs) {
		vr::SwingDetector detector;
		double time = 0.;
		size_t step = 0;
		while(time < 2.) {
			time += run.steps[step++ % run.steps.size()];
			// The hand's way: back and forth between two points, a ramp at each start
			float x = 0.f;
			for(size_t i = 0; i < 3; i++) {
				const float t = glm::clamp(float((time - starts[i]) / ramp), 0.f, 1.f);
				x += (i % 2 == 0 ? -distance : distance) * t;
			}
			if(detector.update(sample(time, Rest + Vec3f(x, 0.f, 0.f)), true) == vr::SwingChange::Ended) {
				run.peaks.push_back(detector.peak());
			}
		}
		run.swings = detector.swings();
	}
	const float speed = distance / float(ramp);
	for(const Run & run : runs) {
		CPPUNIT_ASSERT_EQUAL(u64(3), run.swings);
		CPPUNIT_ASSERT_EQUAL(size_t(3), run.peaks.size());
		for(float peak : run.peaks) {
			if(std::abs(peak - speed) > speed * 0.01f) {
				CPPUNIT_FAIL(std::string(run.name) + ": a swing of " + std::to_string(speed) + " m/s peaked at "
				             + std::to_string(peak));
			}
		}
	}
}

void MeleeSwingTest::testRecoveryTime() {
	const vr::MeleeWeapon weapons[] = { vr::MeleeWeapon::Dagger, vr::MeleeWeapon::OneHanded, vr::MeleeWeapon::TwoHanded };
	for(vr::MeleeWeapon weapon : weapons) {
		// The cfg.ini key wins (0 too: no recovery), then the game's strike, then the fallback
		vr::RecoveryTime time = vr::recoveryTime(weapon, -1.f, 0.f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Fallback);
		CPPUNIT_ASSERT_EQUAL(vr::fallbackRecovery(weapon), time.seconds);
		time = vr::recoveryTime(weapon, -1.f, 0.9f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Game);
		CPPUNIT_ASSERT_EQUAL(0.9f, time.seconds);
		time = vr::recoveryTime(weapon, 0.4f, 0.9f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Config);
		CPPUNIT_ASSERT_EQUAL(0.4f, time.seconds);
		time = vr::recoveryTime(weapon, 0.f, 0.9f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Config);
		CPPUNIT_ASSERT_EQUAL(0.f, time.seconds);
	}
	// The dagger recovers fastest, the two-handed weapon slowest
	CPPUNIT_ASSERT(vr::fallbackRecovery(vr::MeleeWeapon::Dagger) < vr::fallbackRecovery(vr::MeleeWeapon::OneHanded));
	CPPUNIT_ASSERT(vr::fallbackRecovery(vr::MeleeWeapon::OneHanded) < vr::fallbackRecovery(vr::MeleeWeapon::TwoHanded));
	CPPUNIT_ASSERT_EQUAL(std::string("dagger"), std::string(vr::meleeWeaponName(vr::MeleeWeapon::Dagger)));
	CPPUNIT_ASSERT_EQUAL(std::string("1h"), std::string(vr::meleeWeaponName(vr::MeleeWeapon::OneHanded)));
	CPPUNIT_ASSERT_EQUAL(std::string("2h"), std::string(vr::meleeWeaponName(vr::MeleeWeapon::TwoHanded)));
	// Before a hit any swing may hit; after one, not before the recovery is over
	vr::StrikeRecovery recovery;
	CPPUNIT_ASSERT(recovery.ready(0.));
	CPPUNIT_ASSERT_EQUAL(0.f, recovery.remaining(0.));
	recovery.hit(1.0, 0.6f);
	CPPUNIT_ASSERT(!recovery.ready(1.3));
	CPPUNIT_ASSERT(std::abs(recovery.remaining(1.3) - 0.3f) < 1e-5f);
	CPPUNIT_ASSERT(recovery.ready(1.61));
	CPPUNIT_ASSERT_EQUAL(0.f, recovery.remaining(1.7));
	recovery.hit(2.0, 0.f);
	CPPUNIT_ASSERT(recovery.ready(2.0));
	recovery.hit(3.0, 0.6f);
	recovery.clear();
	CPPUNIT_ASSERT(recovery.ready(3.1));
}

void MeleeSwingTest::testRecoveryPerWeapon() {
	// Per weapon type, as VRMelee decides: a swing hits when it starts after the recovery of the last hit. A first
	// swing hits; a second one straight back does not; one started just before the recovery ends does not; one
	// after it does
	const vr::MeleeWeapon weapons[] = { vr::MeleeWeapon::Dagger, vr::MeleeWeapon::OneHanded, vr::MeleeWeapon::TwoHanded };
	for(vr::MeleeWeapon weapon : weapons) {
		const float seconds = vr::recoveryTime(weapon, -1.f, 0.f).seconds;
		vr::SwingDetector detector;
		vr::StrikeRecovery recovery;
		double time = 0.;
		Vec3f hand = Rest;
		double lastHit = -1.;
		std::vector<bool> hits;
		// A swing at 4 m/s for 8 frames, then still until it ended; it touches the target as it starts
		auto swing = [&](float direction) {
			std::vector<vr::SwingChange> changes = move(detector, time, hand, Vec3f(4.f * direction, 0.f, 0.f), 8);
			CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
			const bool hit = recovery.ready(detector.startTime());
			if(hit) {
				recovery.hit(detector.startTime(), seconds);
				lastHit = detector.startTime();
			}
			hits.push_back(hit);
			changes = move(detector, time, hand, Vec3f(0.f), 4);
			CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Ended));
		};
		// Still until the hand's next swing starts \a after seconds after the last hit (it starts 2 frames in)
		auto waitUntil = [&](double after) {
			const double until = lastHit + after - 2 * Frame;
			while(time + Frame < until) {
				move(detector, time, hand, Vec3f(0.f), 1);
			}
		};
		move(detector, time, hand, Vec3f(0.f), 10);
		swing(-1.f);
		swing(1.f);
		waitUntil(double(seconds) - 0.05);
		swing(-1.f);
		waitUntil(double(seconds) + 0.03);
		swing(1.f);
		const std::vector<bool> expected = { true, false, false, true };
		if(hits != expected) {
			std::string seen;
			for(bool hit : hits) {
				seen += hit ? "hit " : "no ";
			}
			CPPUNIT_FAIL(std::string(vr::meleeWeaponName(weapon)) + ": " + seen);
		}
	}
}
