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

#include "vr/RuneProjectionTest.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "vr/HeadPose.h"
#include "vr/RuneProjection.h"
#include "vr/RuneTrail.h"

CppUnit::AutoRegisterSuite<RuneProjectionTest> g_registerRuneProjectionTest;

namespace {

void check2(const std::string & what, float expectedX, float expectedY, const Vec2f & actual, float tolerance = 0.01f) {
	if(!(std::abs(expectedX - actual.x) <= tolerance && std::abs(expectedY - actual.y) <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected (" + std::to_string(expectedX) + ", " + std::to_string(expectedY) + "), got ("
		             + std::to_string(actual.x) + ", " + std::to_string(actual.y) + ")");
	}
}

//! The trail point and the placement of a hand at \a offset (meters, LOCAL) from a head at the reference
vr::HandPlacement placementFor(float handYaw, float handPitch, float bodyYaw, const Vec3f & offset) {
	vr::HeadReference reference;
	vr::HeadSample head;
	head.position = Vec3f(0.f, 1.6f, 0.f);
	reference.position = head.position;
	vr::HandSample hand;
	hand.position = head.position + offset;
	hand.orientation = vr::headOrientationFromDegrees(handYaw, handPitch, 0.f);
	return vr::handToWorld(hand, head, reference, bodyYaw);
}

float stepBetween(const Vec2s & a, const Vec2s & b) {
	return glm::distance(Vec2f(a), Vec2f(b));
}

//! Two yaws are the same heading: 359.9999 is 0
void checkYaw(const std::string & what, float expected, float actual, float tolerance = 0.01f) {
	float diff = std::fmod(std::abs(expected - actual), 360.f);
	diff = std::min(diff, 360.f - diff);
	if(!(diff <= tolerance)) {
		CPPUNIT_FAIL(what + ": expected yaw " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

} // anonymous namespace

void RuneProjectionTest::testProjectionConvention() {

	const Vec3f origin(1000.f, -170.f, 2000.f);

	// Looking along +z (yaw 0): the right is +x, down is +y, the depth (z) is dropped; 10 px per unit
	{
		vr::RunePlane plane = vr::makeRunePlane(0.f, origin);
		check2("yaw 0, right", 100.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(10.f, 0.f, 0.f)));
		check2("yaw 0, down", 0.f, 100.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 10.f, 0.f)));
		check2("yaw 0, up", 0.f, -100.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, -10.f, 0.f)));
		check2("yaw 0, ahead", 0.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 0.f, 10.f)));
		check2("yaw 0, origin", 0.f, 0.f, vr::projectOntoRunePlane(plane, origin));
	}

	// A quarter turn to the left (yaw 90): the look is -x, so the right is +z and x is the depth
	{
		vr::RunePlane plane = vr::makeRunePlane(90.f, origin);
		check2("yaw 90, right", 100.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 0.f, 10.f)));
		check2("yaw 90, ahead", 0.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(-10.f, 0.f, 0.f)));
		check2("yaw 90, down", 0.f, 100.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 10.f, 0.f)));
	}

	// Turned around (yaw 180): the right is -x
	{
		vr::RunePlane plane = vr::makeRunePlane(180.f, origin);
		check2("yaw 180, right", 100.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(-10.f, 0.f, 0.f)));
		check2("yaw 180, left", -100.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(10.f, 0.f, 0.f)));
	}

	// A yaw given beyond a turn is the same plane; a diagonal keeps both components
	{
		vr::RunePlane plane = vr::makeRunePlane(360.f + 270.f, origin);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(270.f, plane.yaw, 0.001f);
		check2("yaw 270, right", 100.f, 0.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 0.f, -10.f)));
		check2("yaw 270, right and down", 50.f, 50.f, vr::projectOntoRunePlane(plane, origin + Vec3f(0.f, 5.f, -5.f)));
	}

	// Another scale
	{
		vr::RunePlane plane = vr::makeRunePlane(0.f, origin, 4.f);
		check2("4 px per unit", 40.f, 20.f, vr::projectOntoRunePlane(plane, origin + Vec3f(10.f, 5.f, 0.f)));
	}

}

void RuneProjectionTest::testProjectionOfTheHand() {

	// A hand moved to the player's right (OpenXR +x) with the body and the look at any yaw: the trail's point
	// projects to +x on the plane of that look, and a hand raised (OpenXR +y) to -y
	const Vec3f camera(500.f, -170.f, 500.f);
	for(float yaw : { 0.f, 90.f, 180.f, 270.f, 37.f }) {
		const Vec3f start = vr::runeTrailPoint(camera, placementFor(0.f, 0.f, yaw, Vec3f(0.f, -0.2f, -0.4f)));
		const Vec3f right = vr::runeTrailPoint(camera, placementFor(0.f, 0.f, yaw, Vec3f(0.3f, -0.2f, -0.4f)));
		const Vec3f up = vr::runeTrailPoint(camera, placementFor(0.f, 0.f, yaw, Vec3f(0.f, 0.05f, -0.4f)));
		const Vec3f ahead = vr::runeTrailPoint(camera, placementFor(0.f, 0.f, yaw, Vec3f(0.f, -0.2f, -0.6f)));
		vr::RunePlane plane = vr::makeRunePlane(yaw, start);
		const std::string at = "body yaw " + std::to_string(int(yaw)) + ": ";
		check2(at + "the hand 30 cm to the right", 300.f, 0.f, vr::projectOntoRunePlane(plane, right), 0.1f);
		check2(at + "the hand 25 cm up", 0.f, -250.f, vr::projectOntoRunePlane(plane, up), 0.1f);
		check2(at + "the hand 20 cm ahead", 0.f, 0.f, vr::projectOntoRunePlane(plane, ahead), 0.1f);
	}

}

void RuneProjectionTest::testSamplerStep() {

	const Vec3f origin(100.f, -150.f, 300.f);
	vr::RuneSampler sampler;
	sampler.begin(vr::makeRunePlane(0.f, origin), 20.f);
	CPPUNIT_ASSERT_EQUAL(size_t(0), sampler.points().size());

	// The first hand point is the origin, (0, 0)
	CPPUNIT_ASSERT_EQUAL(size_t(1), sampler.add(origin));
	CPPUNIT_ASSERT_EQUAL(size_t(1), sampler.points().size());
	CPPUNIT_ASSERT_EQUAL(s16(0), sampler.points()[0].x);
	CPPUNIT_ASSERT_EQUAL(s16(0), sampler.points()[0].y);

	// A hand point within a step (1.5 cm = 15 px) adds nothing; a still hand adds nothing
	CPPUNIT_ASSERT_EQUAL(size_t(0), sampler.add(origin + Vec3f(1.5f, 0.f, 0.f)));
	CPPUNIT_ASSERT_EQUAL(size_t(0), sampler.add(origin + Vec3f(1.5f, 0.f, 0.f)));
	CPPUNIT_ASSERT_EQUAL(size_t(3), sampler.fed());

	// 3 cm on: one point, a step from the origin
	CPPUNIT_ASSERT_EQUAL(size_t(1), sampler.add(origin + Vec3f(3.f, 0.f, 0.f)));
	CPPUNIT_ASSERT_EQUAL(s16(20), sampler.points()[1].x);
	CPPUNIT_ASSERT_EQUAL(s16(0), sampler.points()[1].y);

	// A fast hand, 30 cm to the right in one frame: the path is filled a step at a time up to the hand
	CPPUNIT_ASSERT_EQUAL(size_t(14), sampler.add(origin + Vec3f(30.f, 0.f, 0.f)));
	CPPUNIT_ASSERT_EQUAL(size_t(16), sampler.points().size());
	CPPUNIT_ASSERT_EQUAL(s16(300), sampler.points().back().x);
	for(size_t i = 1; i < sampler.points().size(); i++) {
		const float step = stepBetween(sampler.points()[i - 1], sampler.points()[i]);
		CPPUNIT_ASSERT_MESSAGE("step " + std::to_string(i) + " is " + std::to_string(step), step > 19.f && step < 21.f);
		CPPUNIT_ASSERT_MESSAGE("every point to the right", sampler.points()[i].x > sampler.points()[i - 1].x);
	}

	// The measures: the 2D size in pixels, the 3D centre of the hand's points
	check2("size", 300.f, 0.f, sampler.size());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(origin.x + 15.f, sampler.centre().x, 0.001f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(origin.y, sampler.centre().y, 0.001f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(origin.z, sampler.centre().z, 0.001f);

	// A new stroke starts from nothing
	sampler.begin(vr::makeRunePlane(0.f, origin));
	CPPUNIT_ASSERT_EQUAL(size_t(0), sampler.points().size());
	CPPUNIT_ASSERT_EQUAL(size_t(0), sampler.fed());

}

void RuneProjectionTest::testSamplerCorner() {

	// Right, then down, then right (TAAR's shape): the recogniser's chain needs the steps to turn with
	// the hand - the corners cut by at most one diagonal step
	const Vec3f origin(0.f, 0.f, 0.f);
	vr::RuneSampler sampler;
	sampler.begin(vr::makeRunePlane(0.f, origin), 20.f);
	sampler.add(origin);
	for(int i = 1; i <= 3; i++) {
		sampler.add(origin + Vec3f(5.f * i, 0.f, 0.f));
	}
	for(int i = 1; i <= 3; i++) {
		sampler.add(origin + Vec3f(15.f, 5.f * i, 0.f));
	}
	for(int i = 1; i <= 3; i++) {
		sampler.add(origin + Vec3f(15.f + 5.f * i, 15.f, 0.f));
	}

	const std::vector<Vec2s> & points = sampler.points();
	CPPUNIT_ASSERT(points.size() >= 20);
	size_t rightSteps = 0, downSteps = 0, diagonalSteps = 0;
	for(size_t i = 1; i < points.size(); i++) {
		const float step = stepBetween(points[i - 1], points[i]);
		CPPUNIT_ASSERT_MESSAGE("step " + std::to_string(i) + " is " + std::to_string(step), step > 19.f && step < 21.f);
		const float dx = float(points[i].x - points[i - 1].x);
		const float dy = float(points[i].y - points[i - 1].y);
		CPPUNIT_ASSERT_MESSAGE("never left or up", dx >= -1.f && dy >= -1.f);
		// The recogniser's own classification (SpellRecognition.cpp): a diagonal when 0.4 < |dx| / |dy| < 2.5
		const float a = std::abs(dx), b = std::abs(dy);
		if(b != 0.f && a / b > 0.4f && a / b < 2.5f) {
			diagonalSteps++;
		} else if(a > b) {
			rightSteps++;
		} else {
			downSteps++;
		}
	}
	CPPUNIT_ASSERT_MESSAGE("right steps: " + std::to_string(rightSteps), rightSteps >= 12);
	CPPUNIT_ASSERT_MESSAGE("down steps: " + std::to_string(downSteps), downSteps >= 6);
	CPPUNIT_ASSERT_MESSAGE("diagonal steps: " + std::to_string(diagonalSteps), diagonalSteps <= 2);
	check2("size", 300.f, 150.f, sampler.size());

}

void RuneProjectionTest::testViewGate() {

	const Vec3f head(1000.f, -170.f, 2000.f);
	const float look = 0.f; // along +z
	vr::RuneGateParams params;
	params.unitsPerMeter = 100.f;

	// In front, a little below the eyes: a rune
	{
		vr::RuneGateResult r = vr::gateRuneStroke(head, look, head + Vec3f(0.f, 20.f, 50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(r.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, r.offYawDegrees, 0.01f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.2f, r.belowHeadMeters, 0.001f);
	}

	// 60° to the left of the look: still in view; 80°: behind the view. The engine's yaw turns left
	{
		const float a60 = glm::radians(60.f), a80 = glm::radians(80.f);
		vr::RuneGateResult left = vr::gateRuneStroke(head, look, head + Vec3f(-std::sin(a60), 0.f, std::cos(a60)) * 50.f, params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(left.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(60.f, left.offYawDegrees, 0.01f);
		// (not "far": a Windows macro)
		vr::RuneGateResult farLeft = vr::gateRuneStroke(head, look, head + Vec3f(-std::sin(a80), 0.f, std::cos(a80)) * 50.f, params);
		CPPUNIT_ASSERT_EQUAL(std::string("behind"), std::string(vr::runeGateName(farLeft.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(80.f, farLeft.offYawDegrees, 0.01f);
		vr::RuneGateResult right = vr::gateRuneStroke(head, look, head + Vec3f(std::sin(a80), 0.f, std::cos(a80)) * 50.f, params);
		CPPUNIT_ASSERT_EQUAL(std::string("behind"), std::string(vr::runeGateName(right.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-80.f, right.offYawDegrees, 0.01f);
	}

	// Behind the back
	{
		vr::RuneGateResult r = vr::gateRuneStroke(head, look, head + Vec3f(0.f, 10.f, -50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("behind"), std::string(vr::runeGateName(r.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(180.f, std::abs(r.offYawDegrees), 0.01f);
	}

	// At the knee (1.2 m below the head), and above the head (0.6 m); the band's edges pass
	{
		vr::RuneGateResult low = vr::gateRuneStroke(head, look, head + Vec3f(0.f, 120.f, 50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("low"), std::string(vr::runeGateName(low.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(1.2f, low.belowHeadMeters, 0.001f);
		vr::RuneGateResult high = vr::gateRuneStroke(head, look, head + Vec3f(0.f, -60.f, 50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("high"), std::string(vr::runeGateName(high.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.6f, high.belowHeadMeters, 0.001f);
		vr::RuneGateResult hip = vr::gateRuneStroke(head, look, head + Vec3f(0.f, 85.f, 50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(hip.gate)));
		vr::RuneGateResult raised = vr::gateRuneStroke(head, look, head + Vec3f(0.f, -45.f, 50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(raised.gate)));
	}

	// Behind wins over low: a stroke behind the back at the hip is reported as behind
	{
		vr::RuneGateResult r = vr::gateRuneStroke(head, look, head + Vec3f(0.f, 120.f, -50.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("behind"), std::string(vr::runeGateName(r.gate)));
	}

	// A centre right at the head (the hand did not move) is not behind
	{
		vr::RuneGateResult r = vr::gateRuneStroke(head, look, head + Vec3f(1.f, 30.f, -1.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(r.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, r.offYawDegrees, 0.001f);
	}

	// Looking the other way (yaw 90, along -x) and across the wrap (yaw 350 vs a centre at yaw 10)
	{
		vr::RuneGateResult r = vr::gateRuneStroke(head, 90.f, head + Vec3f(-50.f, 0.f, 0.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(r.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, r.offYawDegrees, 0.01f);
		vr::RuneGateResult behind = vr::gateRuneStroke(head, 90.f, head + Vec3f(50.f, 0.f, 0.f), params);
		CPPUNIT_ASSERT_EQUAL(std::string("behind"), std::string(vr::runeGateName(behind.gate)));
		const float a10 = glm::radians(10.f);
		vr::RuneGateResult wrap = vr::gateRuneStroke(head, 350.f, head + Vec3f(-std::sin(a10), 0.f, std::cos(a10)) * 50.f, params);
		CPPUNIT_ASSERT_EQUAL(std::string("ok"), std::string(vr::runeGateName(wrap.gate)));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(20.f, wrap.offYawDegrees, 0.01f);
	}

	// The defaults: 75°, 0.9 m below, 0.5 m above, the world scale
	{
		vr::RuneGateParams defaults;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(75.f, defaults.yawDegrees, 0.001f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.9f, defaults.belowHeadMeters, 0.001f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5f, defaults.aboveHeadMeters, 0.001f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(vr::UnitsPerMeter, defaults.unitsPerMeter, 0.001f);
	}

}

void RuneProjectionTest::testAim() {

	const Vec3f offset(0.1f, -0.2f, -0.4f);

	// A controller pointing ahead at the reference: the body's yaw, level; the yaw is kept in [0, 360)
	for(float bodyYaw : { 0.f, 90.f, 200.f, 359.f }) {
		Anglef aim = vr::aimFromPlacement(placementFor(0.f, 0.f, bodyYaw, offset));
		const std::string at = "body yaw " + std::to_string(int(bodyYaw)) + ": ";
		CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(at + "pitch", 0.f, aim.getPitch(), 0.01f);
		checkYaw(at + "yaw", bodyYaw, aim.getYaw());
		CPPUNIT_ASSERT_MESSAGE(at + "yaw in [0, 360)", aim.getYaw() >= 0.f && aim.getYaw() < 360.f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL_MESSAGE(at + "roll", 0.f, aim.getRoll(), 0.01f);
	}

	// Pitched up 45°: the engine's pitch is positive downwards, so -45; pitched down 30°: +30
	{
		Anglef up = vr::aimFromPlacement(placementFor(0.f, 45.f, 0.f, offset));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-45.f, up.getPitch(), 0.01f);
		checkYaw("pitched up", 0.f, up.getYaw());
		Anglef down = vr::aimFromPlacement(placementFor(0.f, -30.f, 0.f, offset));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(30.f, down.getPitch(), 0.01f);
	}

	// Turned left 30°: the engine's yaw grows to the left; with the body at 350 it wraps to 20
	{
		Anglef left = vr::aimFromPlacement(placementFor(30.f, 0.f, 0.f, offset));
		checkYaw("turned left", 30.f, left.getYaw());
		Anglef wrapped = vr::aimFromPlacement(placementFor(30.f, 0.f, 350.f, offset));
		checkYaw("turned left, wrapped", 20.f, wrapped.getYaw());
		Anglef right = vr::aimFromPlacement(placementFor(-30.f, 0.f, 0.f, offset));
		checkYaw("turned right", 330.f, right.getYaw());
	}

	// Both at once, with the body turned: the heading and the elevation of the axis
	{
		Anglef aim = vr::aimFromPlacement(placementFor(30.f, 45.f, 10.f, offset));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-45.f, aim.getPitch(), 0.01f);
		checkYaw("turned and pitched", 40.f, aim.getYaw());
	}

	// The aim is the axis the trail's tip is on: the tip lies on the aim's direction from the grip
	{
		const Vec3f camera(500.f, -170.f, 500.f);
		vr::HandPlacement placement = placementFor(-20.f, 25.f, 123.f, offset);
		Anglef aim = vr::aimFromPlacement(placement);
		const Vec3f grip = camera + placement.offset;
		const Vec3f tip = vr::runeTrailPoint(camera, placement);
		const Vec3f along = glm::normalize(tip - grip);
		const float pitch = glm::radians(aim.getPitch()), yaw = glm::radians(aim.getYaw());
		const Vec3f expected(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
		CPPUNIT_ASSERT_MESSAGE("the tip is on the aim", glm::distance(along, expected) < 0.001f);
	}

}
