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
#include <iterator>
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

namespace {

vr::ContactMeasure contact(float speed, float size, vr::ContactKind kind = vr::ContactKind::Slash, bool swing = true) {
	vr::ContactMeasure c;
	c.speed = speed;
	c.size = size;
	c.kind = kind;
	c.swing = swing;
	return c;
}

bool nearly(float a, float b) {
	return std::abs(a - b) < 1e-4f;
}

} // anonymous namespace

void MeleeSwingTest::testContactAim() {
	// #465: the sword's ranges of research #502 - a slash's path 0.3 -> 0.8 m and speed 2.5 -> 6 m/s, a stab's lunge
	// 0.2 -> 0.5 m and speed 2 -> 4 m/s; a touch is the minimum
	vr::DamageParams params;
	params.slash = vr::defaultRanges(vr::MeleeClass::Sword).slash;
	params.stab = vr::defaultRanges(vr::MeleeClass::Sword).stab;
	CPPUNIT_ASSERT(params.noSwing == vr::NoSwing::Min);
	CPPUNIT_ASSERT(params.quality == vr::Quality::Min);
	CPPUNIT_ASSERT_EQUAL(1.f, params.pathScale);
	struct Case {
		float speed;
		float size;
		vr::ContactKind kind;
		float aim;
	};
	const vr::ContactKind slash = vr::ContactKind::Slash;
	const vr::ContactKind thrust = vr::ContactKind::Thrust;
	const Case cases[] = {
		// A big slash: the speed decides - the minimum up to 2.5 m/s, the full strike from 6
		{ 0.f, 2.f, slash, 0.1f }, { 2.49f, 2.f, slash, 0.1f }, { 2.5f, 2.f, slash, 0.1f }, { 4.25f, 2.f, slash, 0.55f },
		{ 6.f, 2.f, slash, 1.f }, { 9.f, 2.f, slash, 1.f },
		// A fast slash: the path decides - the minimum up to 0.3 m, the full strike from 0.8
		{ 9.f, 0.2f, slash, 0.1f }, { 9.f, 0.3f, slash, 0.1f }, { 9.f, 0.55f, slash, 0.55f }, { 9.f, 0.8f, slash, 1.f },
		// The weaker factor decides: half the speed range and a quarter of the path's is a quarter
		{ 4.25f, 0.425f, slash, 0.325f },
		// A slow big swing and a fast tiny flick both stay low
		{ 2.6f, 2.f, slash, 0.1f + 0.9f * 0.1f / 3.5f }, { 9.f, 0.31f, slash, 0.1f + 0.9f * 0.02f },
		// A thrust: the stab's ranges, the same rule
		{ 9.f, 0.5f, thrust, 1.f }, { 9.f, 0.35f, thrust, 0.55f }, { 3.f, 0.5f, thrust, 0.55f }, { 9.f, 0.19f, thrust, 0.1f },
		{ 1.9f, 0.5f, thrust, 0.1f }
	};
	for(const Case & c : cases) {
		const float aim = vr::contactAim(contact(c.speed, c.size, c.kind), params);
		if(!nearly(aim, c.aim)) {
			CPPUNIT_FAIL(std::string(vr::contactKindName(c.kind)) + " " + std::to_string(c.speed) + " m/s, "
			             + std::to_string(c.size) + " m: " + std::to_string(aim) + ", expected " + std::to_string(c.aim));
		}
	}
	// Outside a swing (a touch, a tap, a wrist flick) any stroke is the minimum
	for(float speed : { 0.f, 3.f, 9.f }) {
		CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(contact(speed, 2.f, slash, false), params));
		CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(contact(speed, 2.f, thrust, false), params));
	}
	// The other qualities of half the speed range and a quarter of the path's: the geometric mean, the product
	params.quality = vr::Quality::GeoMean;
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(4.25f, 0.425f), params), 0.1f + 0.9f * std::sqrt(0.125f)));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(9.f, 0.3f), params), 0.1f));
	params.quality = vr::Quality::Product;
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(4.25f, 0.425f), params), 0.1f + 0.9f * 0.125f));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(9.f, 2.f), params), 1.f));
	params.quality = vr::Quality::Min;
	// Seated (the path ranges halved): 0.15 -> 0.4 m
	params.pathScale = 0.5f;
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(9.f, 0.275f), params), 0.55f));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(9.f, 0.4f), params), 1.f));
	params.pathScale = 1.f;
	// vr_melee_noswing=none: nothing instead of the minimum; above the ranges the same
	params.noSwing = vr::NoSwing::None;
	CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(contact(9.f, 2.f, slash, false), params));
	CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(contact(1.f, 2.f), params));
	CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(contact(9.f, 0.1f), params));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(4.25f, 2.f), params), 0.55f));
	// A cap: a one-handed blunt weapon's stab reaches half the strike at most; a two-handed one has none (#502)
	params.noSwing = vr::NoSwing::Min;
	params.stab = vr::defaultRanges(vr::MeleeClass::Blunt).stab;
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(9.f, 2.f, thrust), params), 0.1f + 0.9f * 0.5f));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(3.f, 2.f, thrust), params), 0.1f + 0.9f * 0.5f));
	CPPUNIT_ASSERT(nearly(vr::contactAim(contact(2.5f, 2.f, thrust), params), 0.1f + 0.9f * 0.25f));
	params.stab = vr::defaultRanges(vr::MeleeClass::TwoHandedBlunt).stab;
	CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(contact(9.f, 2.f, thrust), params));
	params.noSwing = vr::NoSwing::None;
	CPPUNIT_ASSERT_EQUAL(0.f, vr::contactAim(contact(9.f, 2.f, thrust), params));
	params.noSwing = vr::NoSwing::Min;
	CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(contact(9.f, 2.f), params)); // its slash is full
	// The same numbers at both ends of a range: a step to the full factor; 0 0 is always full
	params.slash = { 0.f, 0.f, 3.f, 3.f };
	CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(contact(2.9f, 0.f), params));
	CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(contact(3.f, 0.f), params));
	// The cfg.ini names
	CPPUNIT_ASSERT(vr::parseNoSwing("min") == vr::NoSwing::Min);
	CPPUNIT_ASSERT(vr::parseNoSwing("none") == vr::NoSwing::None);
	// An unknown value is the default, the minimum, and not ok (cfg.ini then warns)
	bool ok = true;
	CPPUNIT_ASSERT(vr::parseNoSwing("bogus", &ok) == vr::NoSwing::Min);
	CPPUNIT_ASSERT(!ok);
	CPPUNIT_ASSERT(vr::parseNoSwing("none", &ok) == vr::NoSwing::None);
	CPPUNIT_ASSERT(ok);
	CPPUNIT_ASSERT(vr::parseNoSwing("min", &ok) == vr::NoSwing::Min);
	CPPUNIT_ASSERT(ok);
	CPPUNIT_ASSERT_EQUAL(std::string("min"), std::string(vr::noSwingName(vr::NoSwing::Min)));
	CPPUNIT_ASSERT_EQUAL(std::string("none"), std::string(vr::noSwingName(vr::NoSwing::None)));
	for(vr::Quality quality : { vr::Quality::Min, vr::Quality::GeoMean, vr::Quality::Product }) {
		CPPUNIT_ASSERT(vr::parseQuality(vr::qualityName(quality)) == quality);
	}
	CPPUNIT_ASSERT(vr::parseQuality("bogus") == vr::Quality::Min);
}

void MeleeSwingTest::testDamageRanges() {
	// Research #502's provisional numbers per class: a slash's path and speed, a stab's lunge and speed
	struct Expected {
		vr::MeleeClass meleeClass;
		const char * name;
		vr::DamageRange slash;
		vr::DamageRange stab;
	};
	const Expected expected[] = {
		{ vr::MeleeClass::Dagger, "dagger", { 0.25f, 0.6f, 2.f, 5.f }, { 0.2f, 0.45f, 2.f, 4.5f } },
		{ vr::MeleeClass::Sword, "sword", { 0.3f, 0.8f, 2.5f, 6.f }, { 0.2f, 0.5f, 2.f, 4.f } },
		// A one-handed blunt weapon's stab at most half the strike, a two-handed one none
		{ vr::MeleeClass::Blunt, "blunt", { 0.35f, 0.9f, 2.5f, 5.5f }, { 0.2f, 0.5f, 2.f, 4.f, 0.5f } },
		{ vr::MeleeClass::TwoHandedSword, "2h_sword", { 0.4f, 1.1f, 2.5f, 5.f }, { 0.25f, 0.5f, 2.f, 3.5f } },
		{ vr::MeleeClass::TwoHandedBlunt, "2h_blunt", { 0.5f, 1.2f, 2.5f, 4.5f }, { 0.25f, 0.5f, 2.f, 3.5f, 0.f } },
	};
	auto same = [](const vr::DamageRange & a, const vr::DamageRange & b) {
		return a.pathMin == b.pathMin && a.pathMax == b.pathMax && a.speedMin == b.speedMin && a.speedMax == b.speedMax
		       && a.cap == b.cap;
	};
	size_t index = 0;
	for(const Expected & e : expected) {
		CPPUNIT_ASSERT(vr::MeleeClasses[index++] == e.meleeClass);
		const vr::MeleeRanges ranges = vr::defaultRanges(e.meleeClass);
		CPPUNIT_ASSERT_EQUAL(std::string(e.name), std::string(vr::meleeClassName(e.meleeClass)));
		CPPUNIT_ASSERT(same(ranges.slash, e.slash));
		CPPUNIT_ASSERT(same(ranges.stab, e.stab));
	}
	// The cfg.ini value: four numbers, empty for the default; anything else is the default and not ok
	const vr::DamageRange fallback = { 1.f, 2.f, 3.f, 4.f };
	bool ok = false;
	CPPUNIT_ASSERT(same(vr::parseDamageRange("0.25 0.6 2 5", fallback, &ok), { 0.25f, 0.6f, 2.f, 5.f }));
	CPPUNIT_ASSERT(ok);
	CPPUNIT_ASSERT(same(vr::parseDamageRange("", fallback, &ok), fallback));
	CPPUNIT_ASSERT(ok);
	CPPUNIT_ASSERT(same(vr::parseDamageRange("  ", fallback, &ok), fallback));
	CPPUNIT_ASSERT(ok);
	// A fifth number is the cap, 0 to 1
	CPPUNIT_ASSERT(same(vr::parseDamageRange("0.2 0.5 2 4 0.5", fallback, &ok), { 0.2f, 0.5f, 2.f, 4.f, 0.5f }));
	CPPUNIT_ASSERT(ok);
	CPPUNIT_ASSERT(same(vr::parseDamageRange("0.2 0.5 2 4 0 ", fallback, &ok), { 0.2f, 0.5f, 2.f, 4.f, 0.f }));
	CPPUNIT_ASSERT(ok);
	for(const char * bad : { "1 2 3", "1 2 3 4 5", "1 2 3 4 0.5 1", "1 2 3 4 x", "-1 2 3 4", "1 2 3 25", "a b c d" }) {
		CPPUNIT_ASSERT(same(vr::parseDamageRange(bad, fallback, &ok), fallback));
		CPPUNIT_ASSERT(!ok);
	}
	// A maximum below its minimum is raised to it
	CPPUNIT_ASSERT(same(vr::parseDamageRange("2 1 4 3", fallback, &ok), { 2.f, 2.f, 4.f, 4.f }));
	CPPUNIT_ASSERT(ok);
}

void MeleeSwingTest::testPointVelocity() {
	// The lever point's velocity is the detector's speed
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-3.f, 0.f, 0.f), 6);
		// (the same sum, compiled apart: the last bit may differ)
		CPPUNIT_ASSERT(nearly(detector.speed(), glm::length(detector.pointVelocity(detector.params().lever))));
		CPPUNIT_ASSERT(nearly(detector.speed(), vr::measureContact(detector).speed));
		CPPUNIT_ASSERT(std::abs(glm::length(detector.pointVelocity(0.8f)) - 3.f) < 1e-3f);
	}
	// A wrist turn at 5 rad/s with the hand still: the tip 0.8 m out moves at 4 m/s, the grip not at all, and the
	// lever point too slowly for a swing (F-61) - a contact is a touch: the minimum
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
		const vr::ContactMeasure flick = vr::measureContact(detector);
		CPPUNIT_ASSERT(!flick.swing);
		CPPUNIT_ASSERT(std::abs(flick.speed - rate * detector.params().lever) < 0.01f);
		CPPUNIT_ASSERT_EQUAL(vr::MinStrikeAim, vr::contactAim(flick, vr::DamageParams()));
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

void MeleeSwingTest::testStrokeSize() {
	// A slash at 3 m/s from a still hand: the stroke is the whole way since the hand left its rest - the filtered
	// speed lags a sample, the stroke starts at the sample before
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		for(int frames = 1; frames <= 12; frames++) {
			move(detector, time, hand, Vec3f(-3.f, 0.f, 0.f), 1);
			const float expected = 3.f * float(frames) * float(Frame);
			if(std::abs(detector.strokePath(0.f) - expected) > 1e-4f) {
				CPPUNIT_FAIL(std::to_string(frames) + " frames at 3 m/s: a path of " + std::to_string(detector.strokePath(0.f))
				             + " m, expected " + std::to_string(expected));
			}
			CPPUNIT_ASSERT(detector.strokeLunge(0.f) < 1e-5f); // across the blade: no lunge
		}
		CPPUNIT_ASSERT(detector.swinging());
		const vr::ContactMeasure slash = vr::measureContact(detector);
		CPPUNIT_ASSERT(slash.kind == vr::ContactKind::Slash);
		CPPUNIT_ASSERT_EQUAL(detector.strokePath(0.f), slash.size);
		// Stopped: the swing ends and the next stroke starts from the rest
		move(detector, time, hand, Vec3f(0.f), 6);
		CPPUNIT_ASSERT(!detector.swinging());
		CPPUNIT_ASSERT(detector.strokePath(0.f) < 1e-5f);
	}
	// A slow reach (1 m/s, below the end speed) keeps starting over: no stroke builds up
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(1.f, 0.f, 0.f), 60);
		CPPUNIT_ASSERT(detector.strokePath(0.f) < 1.f * 2.f * float(Frame) + 1e-4f);
	}
	// A lunge along the blade at 3 m/s for 10 frames: 0.33 m, at any point of the blade; pulled back: none
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(0.f, 0.f, -3.f), 10);
		const float expected = 3.f * 10.f * float(Frame);
		CPPUNIT_ASSERT(std::abs(detector.strokeLunge(0.f) - expected) < 1e-4f);
		CPPUNIT_ASSERT(std::abs(detector.strokeLunge(0.7f) - expected) < 1e-4f);
		CPPUNIT_ASSERT(std::abs(detector.strokePath(0.3f) - expected) < 1e-4f);
		const vr::ContactMeasure lunge = vr::measureContact(detector);
		CPPUNIT_ASSERT(lunge.kind == vr::ContactKind::Thrust);
		CPPUNIT_ASSERT_EQUAL(detector.strokeLunge(0.f), lunge.size);
	}
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(0.f, 0.f, 3.f), 10);
		CPPUNIT_ASSERT_EQUAL(0.f, detector.strokeLunge(0.3f));
	}
	// A fast waggle back and forth (4 m/s, 6 frames a way), still and drifting upward faster than a swing ends (the
	// speed never drops at the turns): each way is a stroke of its own - the waggle's path is not carried into the
	// stroke that follows it (12 frames, give or take the filter's lag of two samples at the turn)
	for(float drift : { 0.f, 2.5f }) {
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		for(int leg = 0; leg < 8; leg++) {
			move(detector, time, hand, Vec3f(leg % 2 == 0 ? 4.f : -4.f, drift, 0.f), 6);
		}
		move(detector, time, hand, Vec3f(4.f, drift, 0.f), 12);
		const float step = glm::length(Vec3f(4.f, drift, 0.f)) * float(Frame);
		const float path = detector.strokePath(0.f);
		if(path < 10.f * step || path > 14.f * step) {
			CPPUNIT_FAIL("a stroke of 12 steps after a waggle (drift " + std::to_string(drift) + " m/s): a path of "
			             + std::to_string(path / step) + " steps");
		}
	}
	// A swing of the forearm about the elbow (0.35 m back of the grip) at 8 rad/s: each point of the blade travels its
	// arc - the tip 0.8 m out of the grip (0.35 + 0.8) m per radian, the hand 0.35 m per radian
	{
		vr::SwingDetector detector;
		const Vec3f elbow = Rest + Vec3f(0.f, 0.f, 0.35f);
		const float rate = 8.f;
		double time = 0.;
		float angle = 0.f;
		auto at = [&](float a) {
			const glm::quat turn = glm::angleAxis(a, Vec3f(0.f, 1.f, 0.f));
			return sample(time, elbow + turn * Vec3f(0.f, 0.f, -0.35f), turn);
		};
		for(int i = 0; i < 5; i++) {
			time += Frame;
			detector.update(at(0.f), true);
		}
		for(int i = 0; i < 10; i++) {
			time += Frame;
			angle += rate * float(Frame);
			detector.update(at(angle), true);
		}
		CPPUNIT_ASSERT(detector.swinging());
		CPPUNIT_ASSERT(std::abs(detector.strokePath(0.8f) - angle * 1.15f) < 0.01f);
		CPPUNIT_ASSERT(std::abs(detector.strokePath(0.f) - angle * 0.35f) < 0.01f);
		CPPUNIT_ASSERT(std::abs(vr::measureContact(detector).speed - rate * (0.35f + detector.params().lever)) < 0.05f);
	}
}

void MeleeSwingTest::testThrustCountsLikeSlash() {
	// A thrust along the blade (forward, -z) and a slash across it (sideways, up) at the same 3 m/s, both long
	// strokes, with the same ranges: the same damage by the same rule, told apart only by their kind
	vr::DamageParams params;
	params.slash = params.stab = { 0.2f, 0.5f, 2.f, 4.f };
	struct Motion {
		Vec3f velocity;
		vr::ContactKind kind;
		vr::ContactMeasure seen;
		float aim = 0.f;
	};
	Motion motions[] = { { Vec3f(0.f, 0.f, -3.f), vr::ContactKind::Thrust, { }, 0.f },
	                     { Vec3f(-3.f, 0.f, 0.f), vr::ContactKind::Slash, { }, 0.f },
	                     { Vec3f(0.f, 3.f, 0.f), vr::ContactKind::Slash, { }, 0.f } };
	for(Motion & motion : motions) {
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, motion.velocity, 20);
		CPPUNIT_ASSERT(detector.swinging());
		motion.seen = vr::measureContact(detector);
		motion.aim = vr::contactAim(motion.seen, params);
	}
	for(const Motion & motion : motions) {
		CPPUNIT_ASSERT(motion.seen.kind == motion.kind);
		CPPUNIT_ASSERT(std::abs(motion.seen.speed - 3.f) < 1e-3f);
		CPPUNIT_ASSERT(std::abs(motion.seen.size - 20.f * 3.f * float(Frame)) < 1e-3f);
		CPPUNIT_ASSERT(std::abs(motion.aim - 0.55f) < 1e-3f);
	}
	// The sword's own stab range (0.2 -> 0.5 m, 2 -> 4 m/s): a fast long lunge is the full strike, a fast short jab
	// (0.2 m) little; pulling the blade back is no thrust
	params.stab = vr::defaultRanges(vr::MeleeClass::Sword).stab;
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(0.f, 0.f, -6.f), 8);
		const vr::ContactMeasure lunge = vr::measureContact(detector);
		CPPUNIT_ASSERT(lunge.kind == vr::ContactKind::Thrust);
		CPPUNIT_ASSERT(std::abs(lunge.size - 6.f * 8.f * float(Frame)) < 1e-3f);
		CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(lunge, params));
		const Vec3f velocity = detector.pointVelocity(detector.params().lever);
		CPPUNIT_ASSERT(vr::contactKind(-velocity, detector.blade(), detector.params()) == vr::ContactKind::Slash);
	}
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(0.f, 0.f, -6.f), 3);
		move(detector, time, hand, Vec3f(0.f, 0.f, -0.6f), 1); // stopping: the median still reads the jab's speed
		const vr::ContactMeasure jab = vr::measureContact(detector);
		CPPUNIT_ASSERT(detector.swinging());
		CPPUNIT_ASSERT(jab.kind == vr::ContactKind::Thrust);
		CPPUNIT_ASSERT(jab.speed > params.stab.speedMax);
		CPPUNIT_ASSERT(jab.size < 0.21f);
		CPPUNIT_ASSERT(vr::contactAim(jab, params) < 0.2f);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("thrust"), std::string(vr::contactKindName(vr::ContactKind::Thrust)));
	CPPUNIT_ASSERT_EQUAL(std::string("slash"), std::string(vr::contactKindName(vr::ContactKind::Slash)));
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

void MeleeSwingTest::testTouchCooldown() {
	// #465: a touch outside a swing asks as swing Touch - after a swing's hit only the cooldown holds it back, never the
	// swing's own once per target
	vr::TargetCooldown cooldown;
	const float wait = 0.25f;
	const u64 touch = vr::TargetCooldown::Touch;
	cooldown.hit(7, 4, 1.0);
	CPPUNIT_ASSERT(!cooldown.allowed(7, touch, 1.1, wait));
	CPPUNIT_ASSERT(!cooldown.blocked(touch, 1.1, wait).empty());
	CPPUNIT_ASSERT(cooldown.allowed(7, touch, 1.3, wait));
	CPPUNIT_ASSERT(cooldown.blocked(touch, 1.3, wait).empty());
	CPPUNIT_ASSERT(!cooldown.allowed(7, 4, 1.3, wait)); // the swing itself still not
	CPPUNIT_ASSERT(cooldown.allowed(8, touch, 1.1, wait));
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

namespace {

//! Where a hand is at \a time in a swing that started at \a start
using SwingPath = Vec3f (*)(double time, double start);

/*!
 * A real arm's swing (#613): 0.85 m sideways from rest to rest in 0.3 s along the minimum-jerk profile of a
 * point-to-point arm movement - its speed a bell from 0 through a 5.3 m/s peak (1.875 x the mean) back to 0, so the
 * lever point's speed sweeps the dagger's whole 2-5 m/s range while it accelerates and again while it slows
 */
Vec3f bellSwing(double time, double start) {
	const double s = glm::clamp((time - start) / 0.3, 0., 1.);
	const float way = float(s * s * s * (10. - s * (15. - 6. * s)));
	return Rest + Vec3f(-0.85f * way, 0.f, 0.f);
}

//! The sequences' fast ramp (melee-damage.xrs, `@+1-8` at 90 Hz): 0.85 m in 89 ms at a constant 9.6 m/s
Vec3f rampSwing(double time, double start) {
	const float way = float(glm::clamp((time - start) / (8. / 90.), 0., 1.));
	return Rest + Vec3f(-0.85f * way, 0.f, 0.f);
}

//! The aim a contact would deal at each sample of a swing, as VRMelee's contact() takes it: in a swing only
struct CadenceRun {
	int divisor = 1;          //!< a sample every divisor-th 90 Hz slot: 1 is 90 Hz, 2 is 45 Hz
	std::vector<int> slots;   //!< the 90 Hz slots (time = slot / 90 s) the detector was swinging at
	std::vector<float> aims;
	std::vector<float> speeds;
};

CadenceRun sampleSwing(SwingPath path, double start, int divisor, const vr::DamageParams & damage) {
	CadenceRun run;
	run.divisor = divisor;
	vr::SwingDetector detector;
	for(int slot = 0; slot <= 135; slot += divisor) { // 1.5 s: still, the swing, still
		const double time = double(slot) / 90.;
		detector.update(sample(time, path(time, start)), true);
		if(detector.swinging()) {
			const vr::ContactMeasure contact = vr::measureContact(detector);
			run.slots.push_back(slot);
			run.aims.push_back(vr::contactAim(contact, damage));
			run.speeds.push_back(contact.speed);
		}
	}
	return run;
}

/*!
 * \return "" when every contact moment both runs swing at deals the same aim within \a tolerance, else what differs;
 *         \a compared counts those moments, \a midRange the ones the 90 Hz aim is off both ends of the scale at
 */
std::string compareAims(const CadenceRun & reference, const CadenceRun & other, float tolerance, size_t & compared,
                        size_t & midRange) {
	std::string differences;
	compared = 0;
	midRange = 0;
	for(size_t i = 0; i < other.slots.size(); i++) {
		const auto at = std::find(reference.slots.begin(), reference.slots.end(), other.slots[i]);
		if(at == reference.slots.end()) {
			continue; // the 90 Hz swing already ended (or not yet started) there: no contact moment in common
		}
		const size_t j = size_t(at - reference.slots.begin());
		compared++;
		if(reference.aims[j] > 0.15f && reference.aims[j] < 0.95f) {
			midRange++;
		}
		if(std::abs(other.aims[i] - reference.aims[j]) > tolerance) {
			differences += " t=" + std::to_string(int(std::lround(other.slots[i] * 1000. / 90.))) + "ms: aim "
			               + std::to_string(reference.aims[j]) + " (speed " + std::to_string(reference.speeds[j])
			               + ") at 90 Hz, " + std::to_string(other.aims[i]) + " (speed " + std::to_string(other.speeds[i])
			               + ") at " + std::to_string(90 / other.divisor) + " Hz;";
		}
	}
	return differences;
}

} // anonymous namespace

void MeleeSwingTest::testSameContactAimAt45And90Hz() {
	// #613: a contact's aim is the swing's, not the frame rate's. The same physical swing, sampled at 90 Hz and at
	// 45 Hz (a loaded PC, or a player whose PC renders every other slot), must deal the same aim at every moment both
	// see it swinging. The tolerance, 0.05 of aim: on the dagger's speed scale (2-5 m/s onto 0.1-1) that is 0.17 m/s,
	// 3 % of the full strike's speed. What 45 Hz cannot know is small: the bell's speed is smooth over a 22 ms sample,
	// so a reading of the speed at the moment 90 Hz reads it (the 90 Hz step before the last one) from the 45 Hz
	// samples - the last step's mean speed carried back by a quarter of the change from the step before - is off by
	// 0.02 m/s here (0.005 of aim); the log rounds the aim to 0.01. The reading today is the median's step, 1.5 frames
	// back - 17 ms at 90 Hz, 33 ms at 45 Hz - so while the arm speeds up 45 Hz reads it up to 0.9 m/s slower (0.26 of
	// aim) and while it slows down faster: the same swing hits weaker or harder by the frame rate.
	std::string failures;
	{
		// The sequences' ramp at 9.6 m/s with the dagger's defaults, the stroke's size deciding: started on a slot both
		// rates sample, every common moment deals the same aim - today's behaviour, kept by a fix
		vr::DamageParams damage;
		damage.slash = vr::defaultRanges(vr::MeleeClass::Dagger).slash;
		const double start = 40. / 90.;
		const CadenceRun at90 = sampleSwing(rampSwing, start, 1, damage);
		const CadenceRun at45 = sampleSwing(rampSwing, start, 2, damage);
		size_t compared = 0;
		size_t midRange = 0;
		const std::string differences = compareAims(at90, at45, 0.05f, compared, midRange);
		if(compared < 2) {
			failures += " the 9.6 m/s ramp gave " + std::to_string(compared) + " common contact moments;";
		}
		if(!differences.empty()) {
			failures += " the sequences' 9.6 m/s ramp dealt a different aim at 45 Hz than at 90 Hz:" + differences;
		}
	}
	{
		// The speed alone (the path range always full, as melee-damage.xrs group 3 sets it): the bell swing
		vr::DamageParams damage;
		damage.slash = { 0.f, 0.f, 2.f, 5.f };
		const double start = 0.5;
		const CadenceRun at90 = sampleSwing(bellSwing, start, 1, damage);
		const CadenceRun at45 = sampleSwing(bellSwing, start, 2, damage);
		size_t compared = 0;
		size_t midRange = 0;
		const std::string differences = compareAims(at90, at45, 0.05f, compared, midRange);
		// Not vacuous: the swings overlap for several 45 Hz samples, a few of them off both ends of the aim scale
		if(compared < 6 || midRange < 3) {
			failures += " the bell swing gave " + std::to_string(compared) + " common contact moments, "
			            + std::to_string(midRange) + " in the aim scale's middle: the test swing no longer measures;";
		}
		if(!differences.empty()) {
			failures += " the same 0.3 s swing dealt a different aim at 45 Hz than at 90 Hz:" + differences;
		}
	}
	if(!failures.empty()) {
		CPPUNIT_FAIL(failures);
	}
}

void MeleeSwingTest::testAimAt90HzPinned() {
	// #620: the fix for the frame rate changes nothing at 90 Hz. The aims the test swings dealt before it (the speed read
	// as the median's step, 1.5 frames back), at every 90 Hz slot the detector was swinging at, printed by the code
	// before the fix: pinned within 0.01 of aim (the log's rounding) and 0.01 m/s of speed
	struct Pinned {
		int slot;
		float aim;
		float speed;
	};
	// The bell swing with the dagger's speed range alone (the speed decides)
	const Pinned bell[] = {
		{ 53, 0.3517f, 2.8389f }, { 54, 0.5257f, 3.4190f }, { 55, 0.6856f, 3.9521f }, { 56, 0.8251f, 4.4171f },
		{ 57, 0.9390f, 4.7966f }, { 58, 1.0000f, 5.0773f }, { 59, 1.0000f, 5.2496f }, { 60, 1.0000f, 5.3076f },
		{ 61, 1.0000f, 5.2496f }, { 62, 1.0000f, 5.0773f }, { 63, 0.9390f, 4.7966f }, { 64, 0.8251f, 4.4171f },
		{ 65, 0.6856f, 3.9521f }, { 66, 0.5257f, 3.4190f }, { 67, 0.3517f, 2.8389f }, { 68, 0.1710f, 2.2367f },
		{ 69, 0.1000f, 1.6413f }
	};
	// The sequences' 9.6 m/s ramp with the dagger's defaults (the path decides)
	const Pinned ramp[] = {
		{ 42, 0.1000f, 9.5625f }, { 43, 0.2768f, 9.5625f }, { 44, 0.5500f, 9.5625f }, { 45, 0.8232f, 9.5625f },
		{ 46, 1.0000f, 9.5625f }, { 47, 1.0000f, 9.5625f }, { 48, 1.0000f, 9.5625f }, { 49, 1.0000f, 9.5625f }
	};
	std::string failures;
	auto check = [&](const char * name, const CadenceRun & run, const Pinned * pinned, size_t count) {
		if(run.slots.size() != count) {
			failures += std::string(" ") + name + ": swinging at " + std::to_string(run.slots.size()) + " slots, pinned "
			            + std::to_string(count) + ";";
			return;
		}
		for(size_t i = 0; i < count; i++) {
			if(run.slots[i] != pinned[i].slot || std::abs(run.aims[i] - pinned[i].aim) > 0.01f
			   || std::abs(run.speeds[i] - pinned[i].speed) > 0.01f) {
				failures += std::string(" ") + name + " slot " + std::to_string(run.slots[i]) + ": aim "
				            + std::to_string(run.aims[i]) + " (speed " + std::to_string(run.speeds[i]) + "), pinned slot "
				            + std::to_string(pinned[i].slot) + " aim " + std::to_string(pinned[i].aim) + " (speed "
				            + std::to_string(pinned[i].speed) + ");";
			}
		}
	};
	{
		vr::DamageParams damage;
		damage.slash = { 0.f, 0.f, 2.f, 5.f };
		check("the bell swing", sampleSwing(bellSwing, 0.5, 1, damage), bell, std::size(bell));
	}
	{
		vr::DamageParams damage;
		damage.slash = vr::defaultRanges(vr::MeleeClass::Dagger).slash;
		check("the 9.6 m/s ramp", sampleSwing(rampSwing, 40. / 90., 1, damage), ramp, std::size(ramp));
	}
	// The fixture's constant swings from a still hand with the sword's defaults, frame by frame from the first moving
	// sample: a 4 m/s slash (the speed factor 0.43 once the path is there), a 3 m/s thrust (0.5), a 6 m/s slash (full
	// from 0.8 m); the speed reads from the second moving sample, as before
	struct Frame {
		float aim;
		float speed;
		float size;
	};
	struct Constant {
		const char * name;
		Vec3f velocity;
		std::vector<Frame> frames;
	};
	const Constant constants[] = {
		{ "4 m/s sideways", Vec3f(-4.f, 0.f, 0.f), {
			{ 0.1000f, 0.0000f, 0.0444f }, { 0.1000f, 4.0000f, 0.0889f }, { 0.1000f, 4.0000f, 0.1333f },
			{ 0.1000f, 4.0000f, 0.1778f }, { 0.1000f, 4.0000f, 0.2222f }, { 0.1000f, 4.0000f, 0.2667f },
			{ 0.1200f, 4.0000f, 0.3111f }, { 0.2000f, 4.0000f, 0.3556f }, { 0.2800f, 4.0000f, 0.4000f },
			{ 0.3600f, 4.0000f, 0.4444f }, { 0.4400f, 4.0000f, 0.4889f }, { 0.4857f, 4.0000f, 0.5333f },
			{ 0.4857f, 4.0000f, 0.5778f }, { 0.4857f, 4.0000f, 0.6222f }, { 0.4857f, 4.0000f, 0.6667f },
			{ 0.4857f, 4.0000f, 0.7111f }, { 0.4857f, 4.0000f, 0.7556f }, { 0.4857f, 4.0000f, 0.8000f },
			{ 0.4857f, 4.0000f, 0.8444f }, { 0.4857f, 4.0000f, 0.8889f } } },
		{ "3 m/s along the blade", Vec3f(0.f, 0.f, -3.f), {
			{ 0.1000f, 0.0000f, 0.0333f }, { 0.1000f, 3.0000f, 0.0667f }, { 0.1000f, 3.0000f, 0.1000f },
			{ 0.1000f, 3.0000f, 0.1333f }, { 0.1000f, 3.0000f, 0.1667f }, { 0.1000f, 3.0000f, 0.2000f },
			{ 0.2000f, 3.0000f, 0.2333f }, { 0.3000f, 3.0000f, 0.2667f }, { 0.4000f, 3.0000f, 0.3000f },
			{ 0.5000f, 3.0000f, 0.3333f }, { 0.5500f, 3.0000f, 0.3667f }, { 0.5500f, 3.0000f, 0.4000f },
			{ 0.5500f, 3.0000f, 0.4333f }, { 0.5500f, 3.0000f, 0.4667f }, { 0.5500f, 3.0000f, 0.5000f },
			{ 0.5500f, 3.0000f, 0.5333f }, { 0.5500f, 3.0000f, 0.5667f }, { 0.5500f, 3.0000f, 0.6000f },
			{ 0.5500f, 3.0000f, 0.6333f }, { 0.5500f, 3.0000f, 0.6667f } } },
		{ "6 m/s sideways", Vec3f(-6.f, 0.f, 0.f), {
			{ 0.1000f, 0.0000f, 0.0667f }, { 0.1000f, 6.0000f, 0.1333f }, { 0.1000f, 6.0000f, 0.2000f },
			{ 0.1000f, 6.0000f, 0.2667f }, { 0.1600f, 6.0000f, 0.3333f }, { 0.2800f, 6.0000f, 0.4000f },
			{ 0.4000f, 6.0000f, 0.4667f }, { 0.5200f, 6.0000f, 0.5333f }, { 0.6400f, 6.0000f, 0.6000f },
			{ 0.7600f, 6.0000f, 0.6667f }, { 0.8800f, 6.0000f, 0.7333f }, { 1.0000f, 6.0000f, 0.8000f },
			{ 1.0000f, 6.0000f, 0.8667f }, { 1.0000f, 6.0000f, 0.9333f }, { 1.0000f, 6.0000f, 1.0000f },
			{ 1.0000f, 6.0000f, 1.0667f }, { 1.0000f, 6.0000f, 1.1333f }, { 1.0000f, 6.0000f, 1.2000f },
			{ 1.0000f, 6.0000f, 1.2667f }, { 1.0000f, 6.0000f, 1.3333f } } }
	};
	for(const Constant & constant : constants) {
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		for(size_t i = 0; i < constant.frames.size(); i++) {
			move(detector, time, hand, constant.velocity, 1);
			const vr::ContactMeasure contact = vr::measureContact(detector);
			const float aim = vr::contactAim(contact, vr::DamageParams());
			const Frame & pinned = constant.frames[i];
			if(std::abs(aim - pinned.aim) > 0.01f || std::abs(contact.speed - pinned.speed) > 0.01f
			   || std::abs(contact.size - pinned.size) > 0.001f) {
				failures += std::string(" ") + constant.name + " frame " + std::to_string(i) + ": aim " + std::to_string(aim)
				            + " speed " + std::to_string(contact.speed) + " size " + std::to_string(contact.size)
				            + ", pinned " + std::to_string(pinned.aim) + " " + std::to_string(pinned.speed) + " "
				            + std::to_string(pinned.size) + ";";
			}
		}
	}
	if(!failures.empty()) {
		CPPUNIT_FAIL("the 90 Hz aim changed:" + failures);
	}
}

void MeleeSwingTest::testGapInSwingGoesOn() {
	// #620: a frame over 100 ms in a swing that visibly went on across it - the hand's mean speed over the gap at least
	// the end speed - keeps the swing, so a hitch on a slow PC loses no blow: the speed reads the gap's mean, the stroke
	// grows by the way across it. The hand all but stopped across the gap, or a jump: dropped as before (and outside a
	// swing a gap starts over, testGapAndLostTracking)
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 5);
		CPPUNIT_ASSERT(detector.swinging());
		const float before = detector.strokePath(0.f);
		time += 0.15;
		hand += Vec3f(-0.6f, 0.f, 0.f);
		CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::None);
		CPPUNIT_ASSERT(detector.swinging());
		CPPUNIT_ASSERT_EQUAL(u64(0), detector.gaps());
		CPPUNIT_ASSERT_EQUAL(u64(1), detector.swings());
		if(std::abs(detector.speed() - 4.f) > 0.01f) {
			CPPUNIT_FAIL("0.6 m across a 150 ms frame in a swing read " + std::to_string(detector.speed()) + " m/s");
		}
		CPPUNIT_ASSERT(std::abs(detector.strokePath(0.f) - before - 0.6f) < 1e-4f);
		CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 3).empty());
		CPPUNIT_ASSERT(detector.swinging());
		std::vector<vr::SwingChange> changes = move(detector, time, hand, Vec3f(0.f), 5);
		CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Ended));
		CPPUNIT_ASSERT_EQUAL(u64(1), detector.swings());
	}
	// The hand 10 cm on after 150 ms (0.67 m/s): the swing is lost, nobody knows the way the hand took
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 5);
		time += 0.15;
		hand += Vec3f(-0.1f, 0.f, 0.f);
		CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::Lost);
		CPPUNIT_ASSERT(!detector.swinging());
		CPPUNIT_ASSERT_EQUAL(u64(1), detector.gaps());
		CPPUNIT_ASSERT(move(detector, time, hand, Vec3f(0.f), 5).empty());
	}
	// 3.5 m on after 150 ms (23 m/s): a tracking jump, not a swing that went on
	{
		vr::SwingDetector detector;
		double time = 0.;
		Vec3f hand = Rest;
		move(detector, time, hand, Vec3f(0.f), 5);
		move(detector, time, hand, Vec3f(-4.f, 0.f, 0.f), 5);
		time += 0.15;
		hand += Vec3f(-3.5f, 0.f, 0.f);
		CPPUNIT_ASSERT(detector.update(sample(time, hand), true) == vr::SwingChange::Lost);
		CPPUNIT_ASSERT(!detector.swinging());
		CPPUNIT_ASSERT_EQUAL(u64(1), detector.jumps());
		CPPUNIT_ASSERT_EQUAL(u64(0), detector.gaps());
	}
}

void MeleeSwingTest::testLungeAtLowRateReadsItsSpeed() {
	// #620: at 25 frames/s a lunge's first step covers the stab distance by itself, and the swing starts on that very
	// sample. Before the fix the speed there was 0 (the median's step had not moved yet): a contact on it read
	// speed 0, kind slash, and dealt the minimum. Now the speed is the step's mean, the kind a thrust: the full strike
	// with the sequences' stab range (melee-damage.xrs group 6, the lunge at a 30 Hz cap)
	vr::SwingDetector detector;
	double time = 0.;
	Vec3f hand = Rest;
	moveSteps(detector, time, hand, Vec3f(0.f), std::vector<double>(5, 0.04));
	std::vector<vr::SwingChange> changes = moveSteps(detector, time, hand, Vec3f(0.f, 0.f, -5.5f), { 0.04 });
	CPPUNIT_ASSERT_EQUAL(size_t(1), count(changes, vr::SwingChange::Started));
	CPPUNIT_ASSERT(detector.swinging());
	const vr::ContactMeasure lunge = vr::measureContact(detector);
	if(std::abs(lunge.speed - 5.5f) > 0.01f) {
		CPPUNIT_FAIL("a 0.22 m lunge in one 40 ms frame read " + std::to_string(lunge.speed) + " m/s");
	}
	CPPUNIT_ASSERT(lunge.kind == vr::ContactKind::Thrust);
	CPPUNIT_ASSERT(std::abs(lunge.size - 0.22f) < 1e-3f);
	vr::DamageParams damage;
	damage.stab = { 0.f, 0.f, 2.f, 4.5f };
	CPPUNIT_ASSERT_EQUAL(1.f, vr::contactAim(lunge, damage));
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
