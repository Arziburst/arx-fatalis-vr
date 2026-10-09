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

#include "vr/UIPanelTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "vr/UIPanel.h"

CppUnit::AutoRegisterSuite<UIPanelTest> g_registerUIPanelTest;

namespace {

void checkPanelVector(const std::string & what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(glm::length(expected - actual) > tolerance) {
		CPPUNIT_FAIL(what + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void checkPanelNear(const std::string & what, float expected, float actual, float tolerance = 0.001f) {
	if(std::abs(expected - actual) > tolerance) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! A standing head 1.6 m up, turned by yaw / pitch / roll in degrees (yaw + left, pitch + up)
vr::HeadSample standingHead(float yaw, float pitch = 0.f, float roll = 0.f, const Vec3f & position = Vec3f(0.f, 1.6f, 0.f)) {
	vr::HeadSample sample;
	sample.orientation = vr::headOrientationFromDegrees(yaw, pitch, roll);
	sample.position = position;
	return sample;
}

Vec3f panelNormal(const vr::PanelPose & pose) {
	return pose.orientation * Vec3f(0.f, 0.f, 1.f);
}

const s64 FrameUs = 11111; // 90 Hz in microseconds

} // anonymous namespace

void UIPanelTest::testPlacedAheadBelowTheEyes() {
	vr::PanelPose pose = vr::placePanel(standingHead(0.f));
	checkPanelVector("0.9 m ahead, 0.3 m below the eyes", Vec3f(0.f, 1.3f, -0.9f), pose.position);

	vr::PanelParams params;
	params.distance = 0.8f;
	params.drop = 0.4f;
	pose = vr::placePanel(standingHead(0.f, 0.f, 0.f, Vec3f(0.5f, 1.2f, 2.f)), params);
	checkPanelVector("settings and a seated head elsewhere in the room", Vec3f(0.5f, 0.8f, 1.2f), pose.position);
}

void UIPanelTest::testFacesTheEyes() {
	for(float yaw : { 0.f, 37.f, 90.f, -135.f, 180.f }) {
		const vr::HeadSample h = standingHead(yaw);
		const vr::PanelPose pose = vr::placePanel(h);
		const Vec3f toEyes = glm::normalize(h.position - pose.position);
		checkPanelNear("face points at the eyes, yaw " + std::to_string(yaw), 1.f, glm::dot(panelNormal(pose), toEyes));
		// Upright: its x axis stays horizontal (no roll), its y axis leans back, never upside down
		const Vec3f xAxis = pose.orientation * Vec3f(1.f, 0.f, 0.f);
		const Vec3f yAxis = pose.orientation * Vec3f(0.f, 1.f, 0.f);
		checkPanelNear("x axis horizontal", 0.f, xAxis.y);
		CPPUNIT_ASSERT(yAxis.y > 0.9f);
	}
	// The default tilt: atan(0.3 / 0.9)
	const vr::PanelPose pose = vr::placePanel(standingHead(0.f));
	checkPanelNear("tilt in degrees", 18.43f, glm::degrees(std::asin(panelNormal(pose).y)), 0.01f);
}

void UIPanelTest::testOnlyTheYawPlacesIt() {
	// Turned 90 degrees left and looking down 40 degrees with the head tilted: left of the player, level
	const vr::PanelPose pose = vr::placePanel(standingHead(90.f, -40.f, 15.f));
	checkPanelVector("on the heading only", Vec3f(-0.9f, 1.3f, 0.f), pose.position);
	const vr::PanelPose level = vr::placePanel(standingHead(90.f));
	checkPanelVector("same as with a level head", level.position, pose.position);
	checkPanelNear("same orientation", 1.f, std::abs(glm::dot(level.orientation, pose.orientation)));
}

void UIPanelTest::testHeadingAtThePoles() {
	// Straight down or up, the view has no heading: the top of the head gives it
	const Vec3f left30 = vr::headHeading(vr::headOrientationFromDegrees(30.f, 0.f, 0.f));
	checkPanelVector("down, facing 30 left", left30, vr::headHeading(vr::headOrientationFromDegrees(30.f, -90.f, 0.f)));
	checkPanelVector("up, facing 30 left", left30, vr::headHeading(vr::headOrientationFromDegrees(30.f, 90.f, 0.f)));
	checkPanelVector("nearly down", vr::headHeading(vr::headOrientationFromDegrees(-60.f, 0.f, 0.f)),
	            vr::headHeading(vr::headOrientationFromDegrees(-60.f, -85.f, 0.f)));
	checkPanelNear("unit length", 1.f, glm::length(vr::headHeading(vr::headOrientationFromDegrees(10.f, -89.9f, 0.f))));
}

void UIPanelTest::testSizeFollowsTheImage() {
	const Vec2f size = vr::panelSize(1.f, Vec2i(1920, 1080));
	checkPanelNear("width", 1.f, size.x);
	checkPanelNear("height", 0.5625f, size.y);
	checkPanelNear("4:3", 0.75f, vr::panelSize(1.f, Vec2i(640, 480)).y);
	checkPanelNear("no image yet: 16:9", 0.5625f, vr::panelSize(1.f, Vec2i(0)).y);
}

void UIPanelTest::testRayToPixel() {
	const vr::HeadSample h = standingHead(25.f);
	const vr::PanelPose pose = vr::placePanel(h);
	const Vec2f size = vr::panelSize(1.f, Vec2i(1600, 900));

	Vec2f uv(-1.f);
	float distance = 0.f;
	CPPUNIT_ASSERT(vr::panelHit(pose, size, h.position, pose.position - h.position, uv, distance));
	checkPanelNear("centre u", 0.5f, uv.x);
	checkPanelNear("centre v", 0.5f, uv.y);
	checkPanelNear("distance in ray lengths", 1.f, distance);

	// A point a quarter in from the top left corner, in the panel's own axes: u right, v down
	const Vec3f point = pose.position + pose.orientation * Vec3f(-0.25f * size.x, 0.25f * size.y, 0.f);
	const Vec3f origin = h.position + Vec3f(0.1f, -0.2f, 0.05f); // a hand, not the head
	CPPUNIT_ASSERT(vr::panelHit(pose, size, origin, glm::normalize(point - origin), uv, distance));
	checkPanelNear("u", 0.25f, uv.x);
	checkPanelNear("v (down)", 0.25f, uv.y);
	checkPanelNear("pixel x", 400.f, uv.x * 1600.f, 0.5f);
	checkPanelNear("pixel y", 225.f, uv.y * 900.f, 0.5f);
	checkPanelNear("distance", glm::length(point - origin), distance);
}

void UIPanelTest::testRayMisses() {
	const vr::HeadSample h = standingHead(0.f);
	const vr::PanelPose pose = vr::placePanel(h);
	const Vec2f size = vr::panelSize(1.f, Vec2i(16, 9));
	Vec2f uv(-1.f);
	float distance = 0.f;
	CPPUNIT_ASSERT(!vr::panelHit(pose, size, h.position, Vec3f(0.f, 0.f, 1.f), uv, distance));  // away from it
	CPPUNIT_ASSERT(!vr::panelHit(pose, size, h.position, Vec3f(0.f, 1.f, -0.2f), uv, distance)); // above it
	CPPUNIT_ASSERT(!vr::panelHit(pose, size, h.position, Vec3f(1.f, 0.f, 0.f), uv, distance));  // beside it
	// From behind the panel, through it towards the player
	const Vec3f behind = pose.position - panelNormal(pose);
	CPPUNIT_ASSERT(!vr::panelHit(pose, size, behind, panelNormal(pose), uv, distance));
	CPPUNIT_ASSERT_EQUAL(-1.f, uv.x); // untouched
}

void UIPanelTest::testFixedWhileOpen() {
	vr::PanelAnchor anchor;
	vr::HeadSample h = standingHead(0.f);
	s64 t = 0;
	CPPUNIT_ASSERT(anchor.update(false, t, &h) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::HeadLocked);

	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, &h) == vr::PanelPlacement::Open);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::World);
	const Vec3f placed = anchor.pose().position;
	checkPanelVector("placed ahead", Vec3f(0.f, 1.3f, -0.9f), placed);

	// The head turns, walks and crouches: the panel stays
	for(int i = 1; i <= 90; i++) {
		h = standingHead(float(i), -float(i) / 3.f, 0.f, Vec3f(float(i) * 0.005f, 1.6f - float(i) * 0.003f, 0.f));
		CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, &h) == vr::PanelPlacement::None);
	}
	checkPanelVector("fixed in the play space", placed, anchor.pose().position);
	CPPUNIT_ASSERT_EQUAL(u64(1), anchor.placements());

	CPPUNIT_ASSERT(anchor.update(false, t += FrameUs, &h) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::HeadLocked);
}

void UIPanelTest::testShortCloseKeepsThePose() {
	vr::PanelAnchor anchor;
	vr::HeadSample h = standingHead(0.f);
	s64 t = 0;
	anchor.update(true, t, &h);
	const Vec3f first = anchor.pose().position;

	// The inventory closes, the book opens 200 ms later with the head turned: same place
	anchor.update(false, t += FrameUs, &h);
	h = standingHead(60.f);
	CPPUNIT_ASSERT(anchor.update(true, t += 200000, &h) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::World);
	checkPanelVector("kept over a short close", first, anchor.pose().position);

	// Closed for 300 ms: placed at the head as it is now
	anchor.update(false, t += FrameUs, &h);
	CPPUNIT_ASSERT(anchor.update(true, t += 300000, &h) == vr::PanelPlacement::Open);
	checkPanelVector("placed anew", vr::placePanel(h).position, anchor.pose().position);
	CPPUNIT_ASSERT_EQUAL(u64(2), anchor.placements());
}

void UIPanelTest::testRecenterPlacesAnew() {
	vr::PanelAnchor anchor;
	vr::HeadSample h = standingHead(0.f);
	s64 t = 0;
	anchor.update(true, t, &h);

	h = standingHead(-70.f);
	anchor.recenter();
	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, &h) == vr::PanelPlacement::Recenter);
	checkPanelVector("at the head after the recenter", vr::placePanel(h).position, anchor.pose().position);

	// A recenter while closed: the next menu is placed at the head even within the grace time
	anchor.update(false, t += FrameUs, &h);
	anchor.recenter();
	anchor.update(false, t += FrameUs, &h);
	h = standingHead(20.f);
	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, &h) == vr::PanelPlacement::Open);
	checkPanelVector("placed after a recenter while closed", vr::placePanel(h).position, anchor.pose().position);
}

void UIPanelTest::testWaitsForTracking() {
	vr::PanelAnchor anchor;
	s64 t = 0;
	// A menu opens while the head is not tracked: no panel at a made-up origin (F-128)
	CPPUNIT_ASSERT(anchor.update(true, t, nullptr) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::HeadLocked);
	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, nullptr) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT_EQUAL(u64(0), anchor.placements());

	const vr::HeadSample h = standingHead(45.f);
	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, &h) == vr::PanelPlacement::Tracking);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::World);
	checkPanelVector("placed when tracked", vr::placePanel(h).position, anchor.pose().position);

	// Tracking lost while open: the panel stays
	CPPUNIT_ASSERT(anchor.update(true, t += FrameUs, nullptr) == vr::PanelPlacement::None);
	CPPUNIT_ASSERT(anchor.mode() == vr::PanelMode::World);
}
