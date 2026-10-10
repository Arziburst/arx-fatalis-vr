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

#include "vr/RuneTrailTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "vr/HeadPose.h"
#include "vr/RuneTrail.h"

CppUnit::AutoRegisterSuite<RuneTrailTest> g_registerRuneTrailTest;

namespace {

void trailCheckVector(const std::string & what, const Vec3f & expected, const Vec3f & actual,
                      float tolerance = 0.001f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y) + ", "
		             + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

//! Where the body looks at a yaw (engine axes, as the body of #36 computes it)
Vec3f bodyForward(float yawDegrees) {
	const float yaw = glm::radians(yawDegrees);
	return Vec3f(-std::sin(yaw), 0.f, std::cos(yaw));
}

//! The trail point of a hand held at \a offset (meters, LOCAL) from a head at the reference
Vec3f pointFor(float handYaw, float handPitch, float bodyYaw, const Vec3f & offset, const Vec3f & camera) {
	vr::HeadReference reference;
	vr::HeadSample head;
	head.position = Vec3f(0.f, 1.6f, 0.f);
	reference.position = head.position;
	vr::HandSample hand;
	hand.position = head.position + offset;
	hand.orientation = vr::headOrientationFromDegrees(handYaw, handPitch, 0.f);
	return vr::runeTrailPoint(camera, vr::handToWorld(hand, head, reference, bodyYaw));
}

typedef std::array<vr::StrokeHand, vr::HandCount> Hands;

Hands hands(bool leftTrigger, bool rightTrigger, const Vec3f & leftPoint = Vec3f(0.f),
            const Vec3f & rightPoint = Vec3f(0.f)) {
	Hands result;
	result[size_t(vr::Hand::Left)].trigger = leftTrigger;
	result[size_t(vr::Hand::Left)].located = true;
	result[size_t(vr::Hand::Left)].point = leftPoint;
	result[size_t(vr::Hand::Right)].trigger = rightTrigger;
	result[size_t(vr::Hand::Right)].located = true;
	result[size_t(vr::Hand::Right)].point = rightPoint;
	return result;
}

} // anonymous namespace

void RuneTrailTest::testPointIsAheadOfTheGrip() {

	const Vec3f camera(100.f, -170.f, 300.f);
	const float tip = vr::RuneTipMeters * vr::UnitsPerMeter;

	// A hand held straight ahead of a head at the reference: the glow is 8 cm further along where the
	// body looks, whatever the body's yaw
	for(float bodyYaw : { 0.f, 90.f, 200.f, 315.f }) {
		const Vec3f grip = pointFor(0.f, 0.f, bodyYaw, Vec3f(0.f), camera) - bodyForward(bodyYaw) * tip;
		trailCheckVector("a hand at the head: the grip is at the camera, yaw " + std::to_string(bodyYaw), camera, grip);
	}

	// Pitched up by 90 degrees the controller points up: engine y is down
	trailCheckVector("pitched up", camera + Vec3f(0.f, -tip, 0.f), pointFor(0.f, 90.f, 0.f, Vec3f(0.f), camera));

	// A hand turned 90 degrees left at body yaw 0 points where a body turned to 90 looks
	trailCheckVector("turned left", camera + bodyForward(90.f) * tip, pointFor(90.f, 0.f, 0.f, Vec3f(0.f), camera));

	// The grip's own offset moves the point with it: 30 cm right and 20 cm down of the head (LOCAL x right,
	// y up) at body yaw 0 are engine +x and +y
	trailCheckVector("moved with the hand", camera + Vec3f(30.f, 20.f, tip),
	                 pointFor(0.f, 0.f, 0.f, Vec3f(0.3f, -0.2f, 0.f), camera));

	// A longer tip goes further along the same line
	vr::HandPlacement placement;
	trailCheckVector("tip length", camera + Vec3f(0.f, 0.f, 15.f), vr::runeTrailPoint(camera, placement, 0.15f));
}

void RuneTrailTest::testSegmentFill() {

	const std::vector<Vec3f> points = vr::fillTrailSegment(Vec3f(0.f), Vec3f(10.f, 0.f, 0.f), 3.f);
	CPPUNIT_ASSERT_EQUAL(size_t(3), points.size());
	trailCheckVector("first", Vec3f(3.f, 0.f, 0.f), points[0]);
	trailCheckVector("last", Vec3f(9.f, 0.f, 0.f), points[2]);

	// Not the end itself: 9 / 3 gives the points at 3 and 6
	CPPUNIT_ASSERT_EQUAL(size_t(2), vr::fillTrailSegment(Vec3f(0.f), Vec3f(0.f, 9.f, 0.f), 3.f).size());
	CPPUNIT_ASSERT(vr::fillTrailSegment(Vec3f(0.f), Vec3f(0.f, 0.f, 2.f), 3.f).empty());
	CPPUNIT_ASSERT(vr::fillTrailSegment(Vec3f(0.f), Vec3f(0.f, 0.f, 20.f), 0.f).empty());
	CPPUNIT_ASSERT(vr::fillTrailSegment(Vec3f(1.f), Vec3f(1.f), 1.f).empty());
}

void RuneTrailTest::testStrokeNeedsStanceTriggerAndHand() {

	vr::RuneStroke stroke;

	// The trigger alone is a click, not a stroke
	stroke.update(0, false, hands(false, true));
	CPPUNIT_ASSERT(!stroke.drawing());
	CPPUNIT_ASSERT_EQUAL(u32(0), stroke.strokes());

	// The stance alone draws nothing
	stroke.update(10, true, hands(false, false));
	CPPUNIT_ASSERT(!stroke.drawing());

	// A hand that is not located does not draw
	Hands lost = hands(false, true);
	lost[size_t(vr::Hand::Right)].located = false;
	stroke.update(20, true, lost);
	CPPUNIT_ASSERT(!stroke.drawing());

	// Located: it draws - also a trigger that was already held when the stance began
	stroke.update(30, true, hands(false, true, Vec3f(0.f), Vec3f(5.f, 0.f, 0.f)));
	CPPUNIT_ASSERT(stroke.drawing());
	CPPUNIT_ASSERT(stroke.started());
	CPPUNIT_ASSERT(stroke.hand() == vr::Hand::Right);
	CPPUNIT_ASSERT_EQUAL(u32(1), stroke.strokes());
	trailCheckVector("starts at the hand", Vec3f(5.f, 0.f, 0.f), stroke.point());

	stroke.update(40, true, hands(false, true, Vec3f(0.f), Vec3f(5.f, 0.f, 0.f)));
	CPPUNIT_ASSERT(stroke.drawing());
	CPPUNIT_ASSERT(!stroke.started());
	CPPUNIT_ASSERT_EQUAL(u32(1), stroke.strokes());
}

void RuneTrailTest::testStrokeFollowsTheHand() {

	vr::RuneStroke stroke;
	stroke.update(1000, true, hands(true, false, Vec3f(0.f, 0.f, 0.f)));
	CPPUNIT_ASSERT(stroke.hand() == vr::Hand::Left);

	stroke.update(1011, true, hands(true, false, Vec3f(3.f, 4.f, 0.f)));
	trailCheckVector("follows", Vec3f(3.f, 4.f, 0.f), stroke.point());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(5.0, double(stroke.length()), 1e-4);

	stroke.update(1022, true, hands(true, false, Vec3f(3.f, 4.f, 12.f)));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(17.0, double(stroke.length()), 1e-4);
	CPPUNIT_ASSERT_EQUAL(s64(22), stroke.durationMs());

	// The release ends the rune where the hand last was; the measures stay for the report
	stroke.update(1033, true, hands(false, false, Vec3f(50.f, 0.f, 0.f)));
	CPPUNIT_ASSERT(!stroke.drawing());
	CPPUNIT_ASSERT(stroke.ended() == vr::StrokeEnd::Trigger);
	trailCheckVector("ends where it was", Vec3f(3.f, 4.f, 12.f), stroke.point());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(17.0, double(stroke.length()), 1e-4);
	CPPUNIT_ASSERT_EQUAL(s64(33), stroke.durationMs());

	// A new stroke measures from zero
	stroke.update(1100, true, hands(true, false, Vec3f(1.f, 0.f, 0.f)));
	CPPUNIT_ASSERT(stroke.started());
	CPPUNIT_ASSERT_EQUAL(0.0, double(stroke.length()));
	CPPUNIT_ASSERT_EQUAL(u32(2), stroke.strokes());
}

void RuneTrailTest::testStrokeEnds() {

	vr::RuneStroke stroke;

	// Released stance: the runes are cast, the stroke ends even with the trigger still held
	stroke.update(0, true, hands(false, true));
	stroke.update(10, false, hands(false, true));
	CPPUNIT_ASSERT(!stroke.drawing());
	CPPUNIT_ASSERT(stroke.ended() == vr::StrokeEnd::Stance);
	CPPUNIT_ASSERT_EQUAL(std::string("stance"), std::string(vr::strokeEndName(stroke.lastEnd())));
	stroke.update(20, false, hands(false, true));
	CPPUNIT_ASSERT(stroke.ended() == vr::StrokeEnd::None);
	CPPUNIT_ASSERT(stroke.lastEnd() == vr::StrokeEnd::Stance);

	// The hand lost by the tracking ends its stroke; found again with the trigger held, a new one starts
	stroke.update(30, true, hands(false, true));
	CPPUNIT_ASSERT(stroke.started());
	Hands lost = hands(false, true);
	lost[size_t(vr::Hand::Right)].located = false;
	stroke.update(40, true, lost);
	CPPUNIT_ASSERT(stroke.ended() == vr::StrokeEnd::Lost);
	CPPUNIT_ASSERT(!stroke.drawing());
	stroke.update(50, true, hands(false, true));
	CPPUNIT_ASSERT(stroke.started());
	CPPUNIT_ASSERT_EQUAL(u32(3), stroke.strokes());
}

void RuneTrailTest::testFirstTriggerWins() {

	vr::RuneStroke stroke;

	// The left draws first; the right's trigger is ignored while it draws and after, until released
	stroke.update(0, true, hands(true, false));
	CPPUNIT_ASSERT(stroke.hand() == vr::Hand::Left);
	stroke.update(10, true, hands(true, true));
	CPPUNIT_ASSERT(stroke.hand() == vr::Hand::Left);
	CPPUNIT_ASSERT(stroke.ignored(vr::Hand::Right));
	stroke.update(20, true, hands(false, true));
	CPPUNIT_ASSERT(stroke.ended() == vr::StrokeEnd::Trigger);
	CPPUNIT_ASSERT(!stroke.drawing());
	stroke.update(30, true, hands(false, true));
	CPPUNIT_ASSERT(!stroke.drawing());
	CPPUNIT_ASSERT(stroke.ignored(vr::Hand::Right));

	// Released and pressed again: now it draws
	stroke.update(40, true, hands(false, false));
	CPPUNIT_ASSERT(!stroke.ignored(vr::Hand::Right));
	stroke.update(50, true, hands(false, true));
	CPPUNIT_ASSERT(stroke.drawing());
	CPPUNIT_ASSERT(stroke.hand() == vr::Hand::Right);

	// Both pressed in the same update: the right hand draws, the left is ignored
	vr::RuneStroke both;
	both.update(0, true, hands(true, true));
	CPPUNIT_ASSERT(both.hand() == vr::Hand::Right);
	CPPUNIT_ASSERT(both.ignored(vr::Hand::Left));
	CPPUNIT_ASSERT_EQUAL(u32(1), both.strokes());
}
