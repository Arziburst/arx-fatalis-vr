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

#include "vr/VRSettingsRowsTest.h"

#include <cmath>
#include <string>

#include "vr/HeadPose.h"
#include "vr/VRSettingsRows.h"
#include "vr/VRTurn.h"

CppUnit::AutoRegisterSuite<VRSettingsRowsTest> g_registerVRSettingsRowsTest;

using namespace vr;

void VRSettingsRowsTest::defaultsAreChoicesTest() {
	// Every row shows its default exactly: 45 degrees (D-028), 120 degrees per second, 0.4 s, 0 cm
	CPPUNIT_ASSERT_EQUAL(DefaultSnapAngle, snapAngleChoices()[nearestChoice(snapAngleChoices(), DefaultSnapAngle)]);
	CPPUNIT_ASSERT_EQUAL(DefaultSmoothSpeed,
	                     smoothSpeedChoices()[nearestChoice(smoothSpeedChoices(), DefaultSmoothSpeed)]);
	CPPUNIT_ASSERT_EQUAL(DefaultSnapRepeat, snapRepeatChoices()[nearestChoice(snapRepeatChoices(), DefaultSnapRepeat)]);
	CPPUNIT_ASSERT(std::abs(heightOffsetChoices()[nearestChoice(heightOffsetChoices(), 0.f)]) < 1e-6f);
	// The owner's snap angles: 30 to 90 in 5 degree steps
	CPPUNIT_ASSERT_EQUAL(size_t(13), snapAngleChoices().size());
	CPPUNIT_ASSERT_EQUAL(30.f, snapAngleChoices()[0]);
	CPPUNIT_ASSERT_EQUAL(35.f, snapAngleChoices()[1]);
	CPPUNIT_ASSERT_EQUAL(90.f, snapAngleChoices()[12]);
	// The smooth speed presets: five, by name, "medium" is #32's 120, slowest first
	CPPUNIT_ASSERT_EQUAL(size_t(5), smoothSpeedChoices().size());
	CPPUNIT_ASSERT_EQUAL(std::string("medium"), std::string(smoothSpeedName(nearestChoice(smoothSpeedChoices(), 120.f))));
	CPPUNIT_ASSERT_EQUAL(std::string("very slow"), std::string(smoothSpeedName(0)));
	CPPUNIT_ASSERT_EQUAL(std::string("very fast"), std::string(smoothSpeedName(4)));
	CPPUNIT_ASSERT_EQUAL(std::string(), std::string(smoothSpeedName(5)));
	for(size_t i = 1; i < smoothSpeedChoices().size(); i++) {
		CPPUNIT_ASSERT(smoothSpeedChoices()[i] > smoothSpeedChoices()[i - 1]);
	}
	// Each list lies within the range cfg.ini clamps to
	for(float angle : snapAngleChoices()) {
		CPPUNIT_ASSERT(angle >= MinSnapAngle && angle <= MaxSnapAngle);
	}
	for(float speed : smoothSpeedChoices()) {
		CPPUNIT_ASSERT(speed >= MinSmoothSpeed && speed <= MaxSmoothSpeed);
	}
	for(float repeat : snapRepeatChoices()) {
		CPPUNIT_ASSERT(repeat >= 0.f && repeat <= MaxSnapRepeat);
	}
}

void VRSettingsRowsTest::nearestChoiceTest() {
	// A hand-edited cfg.ini value shows as its neighbour
	CPPUNIT_ASSERT_EQUAL(size_t(4), nearestChoice(snapAngleChoices(), 50.f));
	CPPUNIT_ASSERT_EQUAL(size_t(4), nearestChoice(snapAngleChoices(), 51.f));
	CPPUNIT_ASSERT_EQUAL(size_t(12), nearestChoice(snapAngleChoices(), 180.f));
	CPPUNIT_ASSERT_EQUAL(size_t(0), nearestChoice(snapAngleChoices(), 5.f));
	// Halfway: the first of the two
	CPPUNIT_ASSERT_EQUAL(size_t(0), nearestChoice(std::vector<float>{ 10.f, 20.f }, 15.f));
	CPPUNIT_ASSERT_EQUAL(size_t(0), nearestChoice(std::vector<float>(), 15.f));
	CPPUNIT_ASSERT_EQUAL(size_t(0), nearestChoice(snapRepeatChoices(), 0.f));
}

void VRSettingsRowsTest::heightChoicesTest() {
	const std::vector<float> & choices = heightOffsetChoices();
	// -20 cm to +20 cm in 2 cm steps
	CPPUNIT_ASSERT_EQUAL(size_t(21), choices.size());
	CPPUNIT_ASSERT(std::abs(choices.front() + MaxHeightOffset) < 1e-6f);
	CPPUNIT_ASSERT(std::abs(choices.back() - MaxHeightOffset) < 1e-6f);
	CPPUNIT_ASSERT_EQUAL(size_t(10), nearestChoice(choices, 0.f));
	CPPUNIT_ASSERT_EQUAL(size_t(12), nearestChoice(choices, 0.04f));
	CPPUNIT_ASSERT_EQUAL(size_t(13), nearestChoice(choices, 0.059f));
	for(size_t i = 1; i < choices.size(); i++) {
		CPPUNIT_ASSERT(std::abs(choices[i] - choices[i - 1] - HeightOffsetStep) < 1e-5f);
	}
}

void VRSettingsRowsTest::formatTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("+4 cm"), formatHeightOffset(0.04f));
	CPPUNIT_ASSERT_EQUAL(std::string("-10 cm"), formatHeightOffset(-0.1f));
	CPPUNIT_ASSERT_EQUAL(std::string("0 cm"), formatHeightOffset(0.f));
	CPPUNIT_ASSERT_EQUAL(std::string("+20 cm"), formatHeightOffset(heightOffsetChoices().back()));
	CPPUNIT_ASSERT_EQUAL(std::string("off"), formatSnapRepeat(0.f));
	CPPUNIT_ASSERT_EQUAL(std::string("0.4 s"), formatSnapRepeat(0.4f));
	CPPUNIT_ASSERT_EQUAL(std::string("0.25 s"), formatSnapRepeat(0.25f));
}

void VRSettingsRowsTest::notchTest() {
	// 13 snap angles along the bar: the ends are the first and the last, the middle is the seventh (60 degrees)
	CPPUNIT_ASSERT_EQUAL(size_t(0), notchAt(0.f, 13));
	CPPUNIT_ASSERT_EQUAL(size_t(12), notchAt(1.f, 13));
	CPPUNIT_ASSERT_EQUAL(size_t(6), notchAt(0.5f, 13));
	CPPUNIT_ASSERT_EQUAL(60.f, snapAngleChoices()[notchAt(0.5f, snapAngleChoices().size())]);
	// A drag past the ends stays on them; a dot let go between two notches goes to the nearer one
	CPPUNIT_ASSERT_EQUAL(size_t(0), notchAt(-0.4f, 13));
	CPPUNIT_ASSERT_EQUAL(size_t(12), notchAt(1.7f, 13));
	CPPUNIT_ASSERT_EQUAL(size_t(1), notchAt(0.1f, 13));  // 1.2 notches
	CPPUNIT_ASSERT_EQUAL(size_t(2), notchAt(0.13f, 13)); // 1.56 notches
	// Five speed presets: a quarter of the bar each
	CPPUNIT_ASSERT_EQUAL(size_t(3), notchAt(0.75f, 5));
	CPPUNIT_ASSERT_EQUAL(std::string("fast"), std::string(smoothSpeedName(notchAt(0.7f, 5))));
	// Where a notch is, and back
	for(size_t i = 0; i < 13; i++) {
		CPPUNIT_ASSERT_EQUAL(i, notchAt(notchFraction(i, 13), 13));
	}
	CPPUNIT_ASSERT_EQUAL(0.25f, notchFraction(1, 5));
	// Degenerate bars
	CPPUNIT_ASSERT_EQUAL(size_t(0), notchAt(0.8f, 1));
	CPPUNIT_ASSERT_EQUAL(size_t(0), notchAt(0.8f, 0));
	CPPUNIT_ASSERT_EQUAL(0.f, notchFraction(3, 1));
}
