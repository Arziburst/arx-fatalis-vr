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

void MeleeSwingTest::testContactAim() {
	// #465: in a swing, nothing below 1.5 m/s at the contact point; from 1.5 the engine's weakest strike (0.1) rising
	// to the fully charged one (1) at 4.5; faster stays full. vr_melee_noswing=min: the minimum instead of nothing
	vr::SwingParams params;
	CPPUNIT_ASSERT_EQUAL(1.5f, params.speedMin);
	CPPUNIT_ASSERT_EQUAL(4.5f, params.speedMax);
	CPPUNIT_ASSERT(params.noSwing == vr::NoSwing::None);
	struct Case {
		float speed;
		float aim;
	};
	const Case cases[] = { { 0.f, 0.f }, { 1.49f, 0.f }, { 1.5f, 0.1f }, { 2.25f, 0.325f }, { 3.f, 0.55f },
	                       { 4.f, 0.85f }, { 4.5f, 1.f }, { 6.f, 1.f }, { 15.f, 1.f } };
	for(const Case & c : cases) {
		params.noSwing = vr::NoSwing::None;
		const float none = vr::contactAim(c.speed, true, params);
		params.noSwing = vr::NoSwing::Min;
		const float min = vr::contactAim(c.speed, true, params);
		const float expectedMin = c.speed < 1.5f ? vr::MinStrikeAim : c.aim;
		if(std::abs(none - c.aim) > 1e-5f || std::abs(min - expectedMin) > 1e-5f) {
			CPPUNIT_FAIL("speed " + std::to_string(c.speed) + ": none " + std::to_string(none) + ", min "
			             + std::to_string(min) + ", expected " + std::to_string(c.aim));
		}
	}
	// Outside a swing (a touch, a wrist flick) any speed is no swing: nothing, or the minimum
	for(float speed : { 0.f, 3.f, 9.f }) {
		params.noSwing = vr::NoSwing::None;
		CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(speed, false, params));
		params.noSwing = vr::NoSwing::Min;
		CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(speed, false, params));
	}
	// The same speeds both ends: a step from nothing to full
	params.noSwing = vr::NoSwing::None;
	params.speedMin = params.speedMax = 3.f;
	CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(2.9f, true, params));
	CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(3.f, true, params));
	// The cfg.ini names
	CPPUNIT_ASSERT(vr::parseNoSwing("min") == vr::NoSwing::Min);
	CPPUNIT_ASSERT(vr::parseNoSwing("none") == vr::NoSwing::None);
	CPPUNIT_ASSERT(vr::parseNoSwing("bogus") == vr::NoSwing::None);
	CPPUNIT_ASSERT_EQUAL(std::string("min"), std::string(vr::noSwingName(vr::NoSwing::Min)));
	CPPUNIT_ASSERT_EQUAL(std::string("none"), std::string(vr::noSwingName(vr::NoSwing::None)));
}

void MeleeSwingTest::testPointVelocity() {
	// The lever point's velocity is the detector's speed
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-3.f, 0.f, 0.f), 6);
		CPPUNIT_ASSERT_EQUAL(detector.speed(), glm::length(detector.pointVelocity(detector.params().lever)));
		CPPUNIT_ASSERT(std::abs(glm::length(detector.pointVelocity(0.8f)) - 3.f) < 1e-3f);
	}
	// A wrist turn at 5 rad/s with the hand still: the tip 0.8 m out moves at 4 m/s, the grip not at all, and the
	// lever point too slowly for a swing (F-61) - a contact of the tip outside a swing deals nothing
	{
		vr::SwingDetector detector;
		const float rate = 5.f;
		double time = 0.;
		for(int i = 0; i < 12; i++) {
			time += Frame;
			const glm::quat turn = glm::angleAxis(rate * float(time), Vec3f(0.f, 1.f, 0.f));
			CPPUNIT_ASSERT(detector.update(sample(time, Rest, turn), true) == vr::SwingChange::None);
		}
		CPPUNIT_ASSERT(!detector.swinging());
		const float tip = glm::length(detector.pointVelocity(0.8f));
		if(std::abs(tip - rate * 0.8f) > 0.02f) {
			CPPUNIT_FAIL("the tip of a wrist turn at 5 rad/s read " + std::to_string(tip) + " m/s");
		}
		CPPUNIT_ASSERT(glm::length(detector.pointVelocity(0.f)) < 1e-4f);
		CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(tip, detector.swinging(), detector.params()));
	}
	// Before four samples: no velocity
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(-3.f, 0.f, 0.f), 3);
		CPPUNIT_ASSERT_EQUAL(0.f, glm::length(detector.pointVelocity(0.5f)));
	}
}

void MeleeSwingTest::testThrustCountsLikeSlash() {
	// A thrust along the blade (forward, -z) and a slash across it (sideways) at the same 3 m/s: the same contact
	// speed and the same damage, told apart only by their kind
	const vr::SwingParams params;
	struct Motion {
		Vec3f velocity;
		vr::ContactKind kind;
		float speed = 0.f;
		float aim = 0.f;
		vr::ContactKind seen = vr::ContactKind::Slash;
	};
	Motion motions[] = { { Vec3f(0.f, 0.f, -3.f), vr::ContactKind::Thrust }, { Vec3f(-3.f, 0.f, 0.f), vr::ContactKind::Slash },
	                     { Vec3f(0.f, 3.f, 0.f), vr::ContactKind::Slash } };
	for(Motion & motion : motions) {
		vr::SwingDetector detector(params);
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, motion.velocity, 6);
		CPPUNIT_ASSERT(detector.swinging());
		const Vec3f velocity = detector.pointVelocity(0.5f);
		motion.speed = glm::length(velocity);
		motion.aim = vr::contactAim(motion.speed, detector.swinging(), params);
		motion.seen = vr::contactKind(velocity, detector.blade(), params);
	}
	for(const Motion & motion : motions) {
		CPPUNIT_ASSERT(motion.seen == motion.kind);
		CPPUNIT_ASSERT(std::abs(motion.speed - 3.f) < 1e-3f);
		CPPUNIT_ASSERT(std::abs(motion.aim - motions[0].aim) < 1e-6f);
		CPPUNIT_ASSERT(std::abs(motion.aim - 0.55f) < 1e-3f);
	}
	// A fast thrust is full damage; pulling the blade back is no thrust
	{
		vr::SwingDetector detector(params);
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(0.f, 0.f, -6.f), 6);
		const Vec3f velocity = detector.pointVelocity(0.3f);
		CPPUNIT_ASSERT(vr::contactKind(velocity, detector.blade(), params) == vr::ContactKind::Thrust);
		CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(glm::length(velocity), detector.swinging(), params));
		CPPUNIT_ASSERT(vr::contactKind(-velocity, detector.blade(), params) == vr::ContactKind::Slash);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("thrust"), std::string(vr::contactKindName(vr::ContactKind::Thrust)));
	CPPUNIT_ASSERT_EQUAL(std::string("slash"), std::string(vr::contactKindName(vr::ContactKind::Slash)));
}

void MeleeSwingTest::testTouchCooldown() {
	// A touch outside a swing (vr_melee_noswing=min) is held back by the cooldown only, never by its swing number
	vr::TargetCooldown cooldown;
	const float wait = 0.25f;
	const u64 touch = vr::TargetCooldown::Touch;
	cooldown.hit(7, touch, 1.0);
	CPPUNIT_ASSERT(!cooldown.allowed(7, touch, 1.1, wait));
	CPPUNIT_ASSERT(cooldown.allowed(7, touch, 1.3, wait));
	CPPUNIT_ASSERT(cooldown.blocked(touch, 1.3, wait).empty());
	// A swing after a touch: the cooldown, then it hits
	CPPUNIT_ASSERT(!cooldown.allowed(7, 4, 1.1, wait));
	CPPUNIT_ASSERT(cooldown.allowed(7, 4, 1.3, wait));
	// A touch after a swing's hit: the cooldown, not the swing's own once-per-target
	cooldown.hit(7, 4, 2.0);
	CPPUNIT_ASSERT(!cooldown.allowed(7, 4, 3.0, wait));
	CPPUNIT_ASSERT(cooldown.allowed(7, touch, 3.0, wait));
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
		// The cfg.ini key wins (0 too: no recovery), a negative one is the type's default
		vr::RecoveryTime time = vr::recoveryTime(weapon, -1.f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Default);
		CPPUNIT_ASSERT_EQUAL(vr::defaultRecovery(weapon), time.seconds);
		time = vr::recoveryTime(weapon, 0.4f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Config);
		CPPUNIT_ASSERT_EQUAL(0.4f, time.seconds);
		time = vr::recoveryTime(weapon, 0.f);
		CPPUNIT_ASSERT(time.source == vr::RecoverySource::Config);
		CPPUNIT_ASSERT_EQUAL(0.f, time.seconds);
	}
	// The dagger recovers fastest, the two-handed weapon slowest: 0.6, 0.8, 1.0 s
	CPPUNIT_ASSERT_EQUAL(0.6f, vr::defaultRecovery(vr::MeleeWeapon::Dagger));
	CPPUNIT_ASSERT_EQUAL(0.8f, vr::defaultRecovery(vr::MeleeWeapon::OneHanded));
	CPPUNIT_ASSERT_EQUAL(1.f, vr::defaultRecovery(vr::MeleeWeapon::TwoHanded));
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
		const float seconds = vr::recoveryTime(weapon, -1.f).seconds;
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
