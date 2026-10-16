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

#include "vr/ArmFitTest.h"

#include <cmath>
#include <string>

#include "vr/ArmFit.h"

CppUnit::AutoRegisterSuite<ArmFitTest> g_registerArmFitTest;

namespace {

void fitCheckFloat(const char * what, float expected, float actual, float tolerance = 0.001f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! Samples fed until the tracker reports a measurement; returns how many it took (0 if never within the cap)
int fitSamplesUntilMeasured(vr::ReachTracker & tracker, float value) {
	for(int i = 1; i <= 1000; i++) {
		tracker.add(value);
		if(tracker.measured()) {
			return i;
		}
	}
	return 0;
}

} // anonymous namespace

void ArmFitTest::testLengthsFromHeight() {
	vr::ArmLengths shorter = vr::armLengthsFromHeight(150.f);
	vr::ArmLengths taller = vr::armLengthsFromHeight(190.f);
	CPPUNIT_ASSERT(shorter.upper > 0.f && shorter.forearm > 0.f);
	fitCheckFloat("upper share (short)", vr::ArxUpperRatio, shorter.upper / (shorter.upper + shorter.forearm), 0.0001f);
	fitCheckFloat("upper share (tall)", vr::ArxUpperRatio, taller.upper / (taller.upper + taller.forearm), 0.0001f);
	// Taller players have longer arms, in proportion
	CPPUNIT_ASSERT(taller.upper > shorter.upper);
	CPPUNIT_ASSERT(taller.forearm > shorter.forearm);
	fitCheckFloat("proportional", (shorter.upper + shorter.forearm) * (190.f / 150.f), taller.upper + taller.forearm,
	              0.01f * (taller.upper + taller.forearm));
	// A different ratio is honoured
	vr::ArmLengths even = vr::armLengthsFromHeight(170.f, 0.5f);
	fitCheckFloat("custom ratio", even.upper, even.forearm, 0.0001f);
}

void ArmFitTest::testReachNeedsSamples() {
	vr::ReachTracker tracker;
	CPPUNIT_ASSERT(!tracker.measured());
	tracker.add(60.f);
	CPPUNIT_ASSERT_MESSAGE("one sample is not a measurement", !tracker.measured());
	vr::ReachTracker other;
	int needed = fitSamplesUntilMeasured(other, 60.f);
	CPPUNIT_ASSERT_MESSAGE("measured() never became true", needed > 0);
	CPPUNIT_ASSERT_MESSAGE("the minimum sample count is more than one", needed > 1);
	fitCheckFloat("reach of equal samples", 60.f, other.reach());
}

void ArmFitTest::testReachIgnoresOneOutlier() {
	vr::ReachTracker tracker;
	for(int i = 0; i < 200; i++) {
		tracker.add(50.f + float(i % 10)); // 50 .. 59
	}
	CPPUNIT_ASSERT(tracker.measured());
	fitCheckFloat("reach before the outlier", 59.f, tracker.reach());
	tracker.add(120.f); // one tracking glitch
	fitCheckFloat("one outlier is ignored", 59.f, tracker.reach());
	tracker.add(121.f); // a second large sample: the largest kept is now the smaller of the two
	fitCheckFloat("the second largest counts", 120.f, tracker.reach());
}

void ArmFitTest::testReachResets() {
	vr::ReachTracker tracker;
	for(int i = 0; i < 200; i++) {
		tracker.add(70.f);
	}
	CPPUNIT_ASSERT(tracker.measured());
	tracker.reset();
	CPPUNIT_ASSERT(!tracker.measured());
	for(int i = 0; i < 200; i++) {
		tracker.add(40.f);
	}
	CPPUNIT_ASSERT(tracker.measured());
	fitCheckFloat("only samples after the reset count", 40.f, tracker.reach());
}

void ArmFitTest::testLengthsFromReach() {
	vr::ArmLengths lengths = vr::armLengthsFromReach(58.1f);
	fitCheckFloat("sums to the reach", 58.1f, lengths.upper + lengths.forearm, 0.001f);
	fitCheckFloat("upper share", vr::ArxUpperRatio, lengths.upper / 58.1f, 0.0001f);
	fitCheckFloat("Arx upper arm", 27.6f, lengths.upper, 0.01f);
	fitCheckFloat("Arx forearm", 30.5f, lengths.forearm, 0.01f);
	vr::ArmLengths longer = vr::armLengthsFromReach(70.f, 0.4f);
	fitCheckFloat("sums to the reach (ratio 0.4)", 70.f, longer.upper + longer.forearm, 0.001f);
	fitCheckFloat("upper (ratio 0.4)", 28.f, longer.upper, 0.001f);
	fitCheckFloat("forearm (ratio 0.4)", 42.f, longer.forearm, 0.001f);
}

void ArmFitTest::testStretchWithinReachIsZero() {
	vr::ArmLengths lengths = vr::armLengthsFromReach(60.f);
	fitCheckFloat("default limit", 0.10f, vr::MaxArmStretch, 0.00001f);
	fitCheckFloat("at the shoulder", 0.f, vr::armStretch(0.f, lengths), 0.00001f);
	fitCheckFloat("half way", 0.f, vr::armStretch(30.f, lengths), 0.00001f);
	fitCheckFloat("exactly at reach", 0.f, vr::armStretch(60.f, lengths), 0.00001f);
}

void ArmFitTest::testStretchIsTheOvershootFraction() {
	vr::ArmLengths lengths = vr::armLengthsFromReach(60.f);
	fitCheckFloat("3 units past a 60 reach", 0.05f, vr::armStretch(63.f, lengths), 0.0005f);
	fitCheckFloat("1.2 units past", 0.02f, vr::armStretch(61.2f, lengths), 0.0005f);
	fitCheckFloat("a larger limit", 0.15f, vr::armStretch(69.f, lengths, 0.3f), 0.0005f);
}

void ArmFitTest::testStretchIsClamped() {
	vr::ArmLengths lengths = vr::armLengthsFromReach(60.f);
	fitCheckFloat("at the limit", vr::MaxArmStretch, vr::armStretch(66.f, lengths), 0.0005f);
	fitCheckFloat("far beyond", vr::MaxArmStretch, vr::armStretch(200.f, lengths), 0.00001f);
	fitCheckFloat("far beyond, own limit", 0.25f, vr::armStretch(200.f, lengths, 0.25f), 0.00001f);
	fitCheckFloat("no stretch allowed", 0.f, vr::armStretch(200.f, lengths, 0.f), 0.00001f);
}
