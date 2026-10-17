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

#include "vr/CustomFingersTest.h"

#include <cmath>
#include <string>

#include "vr/FingerCurl.h"
#include "vr/HandRig.h"

CppUnit::AutoRegisterSuite<CustomFingersTest> g_registerCustomFingersTest;

namespace {

// Helpers are prefixed cft: the test sources are one unity build

const vr::Hand cftHands[] = { vr::Hand::Left, vr::Hand::Right };

const char * const cftFingerNames[] = { "thumb", "index", "middle", "ring", "little" };

void cftExpectPose(const std::string & what, const vr::FingerPose & expected, const vr::FingerPose & actual,
                   float tolerance) {
	for(size_t finger = 0; finger < vr::FingerCount; finger++) {
		for(size_t joint = 0; joint < 3; joint++) {
			const float e = expected.flex[finger][joint];
			const float a = actual.flex[finger][joint];
			if(!(std::abs(e - a) <= tolerance)) {
				CPPUNIT_FAIL(what + ": " + cftFingerNames[finger] + " joint " + std::to_string(joint) + ": expected "
				             + std::to_string(e) + ", got " + std::to_string(a));
			}
		}
	}
}

vr::HandShape cftShape(float thumb, float index, float middle, float ring, float little) {
	vr::HandShape shape;
	shape.thumb = thumb;
	shape.index = index;
	shape.middle = middle;
	shape.ring = ring;
	shape.little = little;
	return shape;
}

} // anonymous namespace

void CustomFingersTest::testHeldClosesEitherHand() {
	// D-042: the held rule is the same on both hands - an open hand on the joints, a pulled trigger, both closed
	for(vr::Hand hand : cftHands) {
		const std::string name = (hand == vr::Hand::Left) ? "left" : "right";
		vr::CustomFingerInput input;
		input.holds = true;
		input.controls = true;
		input.trigger = 0.f;
		input.grip = 0.f;
		const vr::HandJoints open = vr::synthesizeHandJoints(hand, vr::HandShape());
		CPPUNIT_ASSERT(open.valid);
		vr::CustomFingerTarget target = vr::customFingerTarget(input, open);
		CPPUNIT_ASSERT_MESSAGE(name + " hand holding a weapon", target.source == vr::CustomFingerSource::Held);
		cftExpectPose(name + " held over open joints", vr::fingerPoseClosed(), target.pose, 1e-4f);

		target = vr::customFingerTarget(input, vr::HandJoints());
		CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::Held);
		cftExpectPose(name + " held without joints", vr::fingerPoseClosed(), target.pose, 1e-4f);

		// The weapon put away: the hand follows its joints again
		input.holds = false;
		CPPUNIT_ASSERT(vr::customFingerTarget(input, open).source == vr::CustomFingerSource::Joints);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("held"), std::string(vr::customFingerSourceName(vr::CustomFingerSource::Held)));
}

void CustomFingersTest::testJointsWithTheThumbPastItsRest() {
	const vr::FingerPoseLimits limits;
	for(vr::Hand hand : cftHands) {
		vr::CustomFingerInput input;
		input.controls = true;
		input.trigger = 1.f; // ignored: the joints come first

		// An open hand is open, its thumb too (its first joint's rest angle is not a bend)
		vr::CustomFingerTarget open = vr::customFingerTarget(input, vr::synthesizeHandJoints(hand, vr::HandShape()));
		CPPUNIT_ASSERT(open.source == vr::CustomFingerSource::Joints);
		cftExpectPose("open", vr::FingerPose(), open.pose, 2.f);

		// The thumb closed alone: its three joints bend, its first one within its limit, the rest stay open
		vr::CustomFingerTarget thumb = vr::customFingerTarget(input, vr::synthesizeHandJoints(hand, cftShape(1, 0, 0, 0, 0)));
		const float * flex = thumb.pose.flex[size_t(vr::Finger::Thumb)].data();
		CPPUNIT_ASSERT(flex[0] > 20.f && flex[0] <= limits.flex[0][0]);
		CPPUNIT_ASSERT(flex[1] > 25.f && flex[2] > 35.f);
		for(size_t finger = 1; finger < vr::FingerCount; finger++) {
			for(size_t joint = 0; joint < 3; joint++) {
				CPPUNIT_ASSERT(thumb.pose.flex[finger][joint] < 2.f);
			}
		}

		// A fist reaches the limits of the four fingers
		vr::CustomFingerTarget fist = vr::customFingerTarget(input, vr::synthesizeHandJoints(hand, cftShape(1, 1, 1, 1, 1)));
		for(size_t finger = 1; finger < vr::FingerCount; finger++) {
			for(size_t joint = 0; joint < 3; joint++) {
				CPPUNIT_ASSERT(std::abs(fist.pose.flex[finger][joint] - limits.flex[finger][joint]) < 2.f);
			}
		}
	}
}

void CustomFingersTest::testControlsWithoutJoints() {
	vr::CustomFingerInput input;
	input.controls = true;
	input.trigger = 0.4f;
	input.grip = 0.f;
	vr::CustomFingerTarget target = vr::customFingerTarget(input, vr::HandJoints());
	CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::Controls);
	cftExpectPose("trigger", vr::fingerPoseFromControls(0.4f, 0.f, false), target.pose, 1e-4f);
	CPPUNIT_ASSERT(target.pose.flex[size_t(vr::Finger::Index)][0] > 30.f);

	input.trigger = 0.f;
	input.grip = 0.4f;
	target = vr::customFingerTarget(input, vr::HandJoints());
	cftExpectPose("grip", vr::fingerPoseFromControls(0.f, 0.4f, false), target.pose, 1e-4f);
}

void CustomFingersTest::testNoSource() {
	vr::CustomFingerTarget target = vr::customFingerTarget(vr::CustomFingerInput(), vr::HandJoints());
	CPPUNIT_ASSERT(target.source == vr::CustomFingerSource::None);
	cftExpectPose("none", vr::FingerPose(), target.pose, 0.f);
	CPPUNIT_ASSERT_EQUAL(std::string("none"), std::string(vr::customFingerSourceName(target.source)));
}
