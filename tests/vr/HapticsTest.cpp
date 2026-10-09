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

#include "vr/HapticsTest.h"

#include <cmath>
#include <string>

#include "vr/Haptics.h"

CppUnit::AutoRegisterSuite<HapticsTest> g_registerHapticsTest;

using vr::Hand;
using vr::HapticEvent;
using vr::HapticPulse;

namespace {

const size_t Left = size_t(Hand::Left);
const size_t Right = size_t(Hand::Right);

HapticPulse pulse(float amplitude, float seconds) {
	HapticPulse p;
	p.amplitude = amplitude;
	p.seconds = seconds;
	p.frequency = 100.f;
	return p;
}

} // anonymous namespace

void HapticsTest::testNames() {
	for(size_t i = 0; i < vr::HapticEventCount; i++) {
		const HapticEvent event = HapticEvent(i);
		std::optional<HapticEvent> parsed = vr::parseHapticEvent(vr::hapticEventName(event));
		CPPUNIT_ASSERT(parsed.has_value());
		CPPUNIT_ASSERT(*parsed == event);
	}
	CPPUNIT_ASSERT(!vr::parseHapticEvent("hit").has_value());
	CPPUNIT_ASSERT_EQUAL(std::string("weapon_hit"), std::string(vr::hapticEventName(HapticEvent::WeaponHit)));
}

void HapticsTest::testTable() {
	// Every row is a real pulse within the OpenXR range, and short: a pulse is feedback, not a rumble
	for(size_t i = 0; i < vr::HapticEventCount; i++) {
		const HapticPulse p = vr::hapticPulse(HapticEvent(i), 1.f);
		const std::string name = vr::hapticEventName(HapticEvent(i));
		CPPUNIT_ASSERT_MESSAGE(name, !p.empty());
		CPPUNIT_ASSERT_MESSAGE(name, p.amplitude <= 1.f);
		CPPUNIT_ASSERT_MESSAGE(name, p.seconds <= 0.25f);
		CPPUNIT_ASSERT_MESSAGE(name, p.frequency > 0.f);
	}
	CPPUNIT_ASSERT(vr::hapticHand(HapticEvent::HitTaken) == vr::HapticHand::Both);
	CPPUNIT_ASSERT(vr::hapticHand(HapticEvent::WeaponHit) == vr::HapticHand::Weapon);
	CPPUNIT_ASSERT(vr::hapticHand(HapticEvent::ItemGrabbed) == vr::HapticHand::Pointer);
	CPPUNIT_ASSERT_EQUAL(vr::WeaponHitAmplitude, vr::hapticPulse(HapticEvent::WeaponHit).amplitude);
	// The grab is a tick: weaker and shorter than a hit
	const HapticPulse grab = vr::hapticPulse(HapticEvent::ItemGrabbed);
	const HapticPulse hit = vr::hapticPulse(HapticEvent::WeaponHit);
	CPPUNIT_ASSERT(grab.amplitude < hit.amplitude);
	CPPUNIT_ASSERT(grab.seconds < hit.seconds);
}

void HapticsTest::testHitTakenScalesWithDamage() {
	CPPUNIT_ASSERT_EQUAL(vr::HitTakenBase, vr::hapticPulse(HapticEvent::HitTaken, 0.f).amplitude);
	const float tenth = vr::hapticPulse(HapticEvent::HitTaken, 0.1f).amplitude;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.45, double(tenth), 1e-5);
	CPPUNIT_ASSERT_EQUAL(vr::HitTakenMax, vr::hapticPulse(HapticEvent::HitTaken, 0.5f).amplitude);
	CPPUNIT_ASSERT_EQUAL(vr::HitTakenMax, vr::hapticPulse(HapticEvent::HitTaken, 7.f).amplitude);
	CPPUNIT_ASSERT_EQUAL(vr::HitTakenBase, vr::hapticPulse(HapticEvent::HitTaken, -1.f).amplitude);
	CPPUNIT_ASSERT_EQUAL(vr::HitTakenBase, vr::hapticPulse(HapticEvent::HitTaken, std::nanf("")).amplitude);
}

void HapticsTest::testStrength() {
	vr::HapticMixer mixer;
	mixer.strength = 0.5f;
	CPPUNIT_ASSERT(mixer.request(Hand::Right, pulse(0.6f, 0.1f)));
	auto sent = mixer.take(0.0);
	CPPUNIT_ASSERT(sent[Right].has_value());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, double(sent[Right]->amplitude), 1e-6);
	CPPUNIT_ASSERT(!sent[Left].has_value());
	// Above 1 the amplitude is cut at the OpenXR maximum
	mixer.strength = 2.f;
	CPPUNIT_ASSERT(mixer.request(Hand::Right, pulse(0.8f, 0.1f)));
	CPPUNIT_ASSERT_EQUAL(1.f, mixer.take(1.0)[Right]->amplitude);
	// Off sends nothing
	mixer.strength = 0.f;
	CPPUNIT_ASSERT(!mixer.request(Hand::Right, pulse(0.8f, 0.1f)));
	CPPUNIT_ASSERT(!mixer.take(2.0)[Right].has_value());
	// An empty pulse is refused
	mixer.strength = 1.f;
	CPPUNIT_ASSERT(!mixer.request(Hand::Left, pulse(0.f, 0.1f)));
	CPPUNIT_ASSERT(!mixer.request(Hand::Left, pulse(0.5f, 0.f)));
}

void HapticsTest::testStrongestOfAFrameWins() {
	vr::HapticMixer mixer;
	mixer.request(Hand::Left, pulse(0.3f, 0.2f));
	mixer.request(Hand::Left, pulse(0.7f, 0.05f));
	mixer.request(Hand::Left, pulse(0.5f, 0.3f));
	auto sent = mixer.take(0.0);
	CPPUNIT_ASSERT(sent[Left].has_value());
	CPPUNIT_ASSERT_EQUAL(0.7f, sent[Left]->amplitude);
	CPPUNIT_ASSERT_EQUAL(u64(2), mixer.merged());
	// The queue is empty after a take
	CPPUNIT_ASSERT(!mixer.take(1.0)[Left].has_value());
}

void HapticsTest::testWeakerDoesNotCutStronger() {
	vr::HapticMixer mixer;
	mixer.request(Hand::Right, pulse(0.8f, 0.2f));
	CPPUNIT_ASSERT(mixer.take(10.0)[Right].has_value());
	// While the strong one plays, a weaker one is dropped
	mixer.request(Hand::Right, pulse(0.25f, 0.05f));
	CPPUNIT_ASSERT(!mixer.take(10.1)[Right].has_value());
	CPPUNIT_ASSERT_EQUAL(u64(1), mixer.masked());
	// An equal or stronger one replaces it
	mixer.request(Hand::Right, pulse(0.8f, 0.1f));
	CPPUNIT_ASSERT(mixer.take(10.15)[Right].has_value());
	// Once it is over, a weak one plays
	mixer.request(Hand::Right, pulse(0.25f, 0.05f));
	CPPUNIT_ASSERT(mixer.take(10.3)[Right].has_value());
	CPPUNIT_ASSERT_EQUAL(u64(1), mixer.masked());
}

void HapticsTest::testHandsAreIndependent() {
	vr::HapticMixer mixer;
	mixer.request(Hand::Right, pulse(0.9f, 0.2f));
	mixer.take(0.0);
	mixer.request(Hand::Left, pulse(0.2f, 0.05f));
	mixer.request(Hand::Right, pulse(0.2f, 0.05f));
	auto sent = mixer.take(0.05);
	CPPUNIT_ASSERT(sent[Left].has_value());
	CPPUNIT_ASSERT(!sent[Right].has_value());
}

void HapticsTest::testClear() {
	vr::HapticMixer mixer;
	mixer.request(Hand::Right, pulse(0.9f, 0.2f));
	mixer.take(0.0);
	mixer.request(Hand::Left, pulse(0.5f, 0.2f));
	mixer.clear();
	// The queued one is gone and the playing one no longer masks
	CPPUNIT_ASSERT(!mixer.take(0.01)[Left].has_value());
	mixer.request(Hand::Right, pulse(0.2f, 0.05f));
	CPPUNIT_ASSERT(mixer.take(0.02)[Right].has_value());
}
