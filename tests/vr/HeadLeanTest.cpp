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

#include "vr/HeadLeanTest.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "vr/HeadLean.h"

CppUnit::AutoRegisterSuite<HeadLeanTest> g_registerHeadLeanTest;

namespace {

void checkNear(const char * what, float expected, float actual, float tolerance = 1e-4f) {
	if(std::abs(expected - actual) > tolerance) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void checkVec(const char * what, const Vec2f & expected, const Vec2f & actual, float tolerance = 1e-4f) {
	checkNear((std::string(what) + " x").c_str(), expected.x, actual.x, tolerance);
	checkNear((std::string(what) + " z").c_str(), expected.y, actual.y, tolerance);
}

const float UnitsPerMeter = 100.f;

} // anonymous namespace

void HeadLeanTest::testInsideTheRadiusTheBodyStays() {
	// A glance, a small lean: 8 cm off the body - the camera takes it, no step
	vr::HeadLean lean = vr::leanHead(Vec2f(0.08f, 0.f), Vec2f(0.f));
	checkVec("lean", Vec2f(0.08f, 0.f), lean.lean);
	checkVec("no step", Vec2f(0.f), lean.step);
	checkNear("distance", 0.08f, lean.distance);
	checkNear("no fade", 0.f, lean.farFade);
	// Exactly at the radius is still inside
	lean = vr::leanHead(Vec2f(0.f, 0.10f), Vec2f(0.f));
	checkVec("at the radius", Vec2f(0.f), lean.step);
}

void HeadLeanTest::testBeyondTheRadiusTheBodySteps() {
	// The head 50 cm forward of the reference, the body 20 cm right of it: the lean is (-0.2, 0.5)
	vr::HeadLean lean = vr::leanHead(Vec2f(0.f, 0.5f), Vec2f(0.2f, 0.f));
	checkVec("lean", Vec2f(-0.2f, 0.5f), lean.lean);
	// The step brings the head back to 10 cm along the same direction
	const float distance = std::sqrt(0.2f * 0.2f + 0.5f * 0.5f);
	checkNear("distance", distance, lean.distance);
	const Vec2f rest = lean.lean - lean.step;
	checkNear("rest at the radius", 0.10f, std::sqrt(rest.x * rest.x + rest.y * rest.y));
	CPPUNIT_ASSERT(lean.step.x < 0.f && lean.step.y > 0.f);
	checkNear("same direction", lean.lean.x / lean.lean.y, lean.step.x / lean.step.y);
	// A larger radius from the settings
	vr::LeanParams params;
	params.followRadius = 0.3f;
	lean = vr::leanHead(Vec2f(0.5f, 0.f), Vec2f(0.f), params);
	checkVec("radius 0.3", Vec2f(0.2f, 0.f), lean.step);
	// Radius 0: the body stays under the head
	params.followRadius = 0.f;
	lean = vr::leanHead(Vec2f(0.5f, 0.f), Vec2f(0.f), params);
	checkVec("radius 0", Vec2f(0.5f, 0.f), lean.step);
}

void HeadLeanTest::testWalkingAMeter() {
	// The head walks 1 m forward in 1 cm a frame with the body free: the engine moves the body exactly the
	// step each frame (at the body's yaw 30). The body ends 0.9 m on, the head 10 cm ahead of it
	Vec2f anchor(0.f);
	const float turn = 30.f;
	for(int i = 1; i <= 100; i++) {
		vr::HeadLean lean = vr::leanHead(Vec2f(0.f, 0.01f * float(i)), anchor);
		CPPUNIT_ASSERT(lean.distance <= 0.10f + 0.0101f);
		const Vec2f movedWorld = vr::turnToWorld(lean.step, turn) * UnitsPerMeter;
		anchor = vr::anchorAfterStep(anchor, movedWorld, turn, UnitsPerMeter);
	}
	checkVec("anchor", Vec2f(0.f, 0.9f), anchor, 1e-3f);
	vr::HeadLean lean = vr::leanHead(Vec2f(0.f, 1.f), anchor);
	checkVec("lean", Vec2f(0.f, 0.1f), lean.lean, 1e-3f);
	checkVec("settled", Vec2f(0.f), lean.step, 1e-3f);
}

void HeadLeanTest::testBlockedBodyLeavesTheLean() {
	// A wall 30 cm ahead stops the body; the head goes on to 60 cm: the lean grows past the radius
	Vec2f anchor(0.f);
	for(int i = 1; i <= 60; i++) {
		vr::HeadLean lean = vr::leanHead(Vec2f(0.f, 0.01f * float(i)), anchor);
		const float allowed = std::max(0.f, std::min(lean.step.y, 0.3f - anchor.y));
		anchor = vr::anchorAfterStep(anchor, Vec2f(0.f, allowed * UnitsPerMeter), 0.f, UnitsPerMeter);
	}
	checkVec("body at the wall", Vec2f(0.f, 0.3f), anchor, 1e-4f);
	vr::HeadLean lean = vr::leanHead(Vec2f(0.f, 0.6f), anchor);
	checkNear("lean past the wall", 0.3f, lean.distance, 1e-4f);
	// The wall slid the body sideways instead: the anchor takes what really happened
	anchor = vr::anchorAfterStep(Vec2f(0.f), Vec2f(5.f, 0.f), 0.f, UnitsPerMeter);
	checkVec("slide", Vec2f(0.05f, 0.f), anchor);
}

void HeadLeanTest::testTurnRoundTrip() {
	// Engine yaw 90 turns the reference forward (+z) to the world's -x (headToCamera's convention)
	checkVec("forward at 90", Vec2f(-1.f, 0.f), vr::turnToWorld(Vec2f(0.f, 1.f), 90.f));
	checkVec("right at 90", Vec2f(0.f, 1.f), vr::turnToWorld(Vec2f(1.f, 0.f), 90.f));
	const Vec2f v(0.3f, -0.7f);
	for(float turn = -270.f; turn <= 270.f; turn += 37.f) {
		checkVec("round trip", v, vr::turnFromWorld(vr::turnToWorld(v, turn), turn));
	}
}

void HeadLeanTest::testTurningKeepsTheWalk() {
	// The body walked 1 m forward in the room, then the stick turned it by 45: the anchor (reference frame)
	// is unchanged, so the head at the same room spot has the same small lean - the body is not thrown
	// back across the room by the turn
	const Vec2f anchor(0.f, 0.9f);
	const Vec2f head(0.f, 1.f);
	vr::HeadLean lean = vr::leanHead(head, anchor);
	checkNear("lean before", 0.1f, lean.distance, 1e-4f);
	checkVec("no step", Vec2f(0.f), lean.step, 1e-4f);
	// The lean in the world turns with the body, its length stays
	const Vec2f before = vr::turnToWorld(lean.lean, 0.f);
	const Vec2f after = vr::turnToWorld(lean.lean, 45.f);
	checkNear("same length", std::sqrt(before.x * before.x + before.y * before.y),
	          std::sqrt(after.x * after.x + after.y * after.y));
}

void HeadLeanTest::testFarFade() {
	checkNear("near", 0.f, vr::leanHead(Vec2f(0.69f, 0.f), Vec2f(0.f)).farFade);
	checkNear("start", 0.f, vr::leanHead(Vec2f(0.7f, 0.f), Vec2f(0.f)).farFade);
	checkNear("half", 0.5f, vr::leanHead(Vec2f(0.f, 0.85f), Vec2f(0.f)).farFade);
	checkNear("full", 1.f, vr::leanHead(Vec2f(1.0f, 0.f), Vec2f(0.f)).farFade);
	checkNear("beyond", 1.f, vr::leanHead(Vec2f(3.f, 0.f), Vec2f(0.f)).farFade);
	// Measured from the body, not from the reference
	checkNear("body walked along", 0.f, vr::leanHead(Vec2f(3.f, 0.f), Vec2f(2.9f, 0.f)).farFade);
	// A hard edge when both distances are the same
	vr::LeanParams params;
	params.farStart = params.farFull = 0.5f;
	checkNear("edge below", 0.f, vr::leanHead(Vec2f(0.49f, 0.f), Vec2f(0.f), params).farFade);
	checkNear("edge", 1.f, vr::leanHead(Vec2f(0.5f, 0.f), Vec2f(0.f), params).farFade);
}

void HeadLeanTest::testFadeRates() {
	// To black in 0.15 s, back in 0.25 s
	float fade = 0.f;
	fade = vr::stepFade(fade, 1.f, 0.075f);
	checkNear("half way out", 0.5f, fade);
	fade = vr::stepFade(fade, 1.f, 0.1f);
	checkNear("black, not beyond", 1.f, fade);
	fade = vr::stepFade(fade, 0.f, 0.125f);
	checkNear("half way in", 0.5f, fade);
	fade = vr::stepFade(fade, 0.f, 1.f);
	checkNear("clear, not below", 0.f, fade);
	// A partial target (the far fade) is approached and kept
	fade = vr::stepFade(0.f, 0.3f, 1.f);
	checkNear("partial", 0.3f, fade);
	fade = vr::stepFade(fade, 0.3f, 1.f);
	checkNear("kept", 0.3f, fade);
	// No time, no change; a zero duration is at once
	checkNear("no time", 0.4f, vr::stepFade(0.4f, 1.f, 0.f));
	vr::FadeParams instant;
	instant.outSeconds = instant.inSeconds = 0.f;
	checkNear("instant out", 1.f, vr::stepFade(0.f, 1.f, 0.001f, instant));
	checkNear("instant in", 0.f, vr::stepFade(1.f, 0.f, 0.001f, instant));
}

void HeadLeanTest::testDistanceToTriangle() {
	// A large wall in the plane x = 100 (game units): the middle of it counts, which the engine's sphere
	// test (corners and edge midpoints only) misses
	const Vec3f a(100.f, 0.f, -500.f);
	const Vec3f b(100.f, -400.f, 500.f);
	const Vec3f c(100.f, 400.f, 500.f);
	checkNear("in front of the middle", 10.f, vr::distanceToTriangle(Vec3f(90.f, 0.f, 200.f), a, b, c));
	checkNear("behind the middle", 10.f, vr::distanceToTriangle(Vec3f(110.f, 0.f, 200.f), a, b, c));
	checkNear("on it", 0.f, vr::distanceToTriangle(Vec3f(100.f, 0.f, 200.f), a, b, c));
	// Beyond a corner, beyond an edge
	checkNear("corner", 5.f, vr::distanceToTriangle(Vec3f(100.f, 0.f, -505.f), a, b, c));
	checkNear("edge", 3.f, vr::distanceToTriangle(Vec3f(100.f, 0.f, 503.f), a, b, c), 1e-3f);
	checkNear("edge and in front", 5.f, vr::distanceToTriangle(Vec3f(96.f, 0.f, 503.f), a, b, c), 1e-3f);
	// Degenerate: a point
	checkNear("point", 1.f, vr::distanceToTriangle(Vec3f(0.f, 1.f, 0.f), Vec3f(0.f), Vec3f(0.f), Vec3f(0.f)));
}
