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

#include "vr/StableViewTest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "vr/StableView.h"

CppUnit::AutoRegisterSuite<StableViewTest> g_registerStableViewTest;

namespace {

const float Standing = 170.f;
const float Crouched = 120.f;

//! One frame of the body
struct BodyFrame {
	float feetY;
	float eye;
	bool jumping;
};

//! Runs the frames at \a hz and returns the camera heights (the first is the start)
std::vector<float> run(const std::vector<BodyFrame> & frames, float hz, const vr::StableViewParams & params,
                       vr::StableView * out = nullptr) {
	vr::StableView view;
	std::vector<float> heights;
	for(const BodyFrame & frame : frames) {
		vr::StableViewInput input;
		input.dt = 1.f / hz;
		input.feetY = frame.feetY;
		input.eyeAboveFeet = frame.eye;
		input.jumping = frame.jumping;
		vr::updateStableView(view, input, params);
		heights.push_back(view.cameraY());
	}
	if(out) {
		*out = view;
	}
	return heights;
}

//! The largest change of one frame
float largestStep(const std::vector<float> & heights) {
	float result = 0.f;
	for(size_t i = 1; i < heights.size(); i++) {
		result = std::max(result, std::abs(heights[i] - heights[i - 1]));
	}
	return result;
}

/*!
 * The engine's jump (Player.cpp): 130 units up in 200 ms, then a fall back down to \a landY - with
 * \a restBefore / \a restAfter frames on the ground around it
 */
std::vector<BodyFrame> jump(float hz, float landY, int restAfter = 200) {
	std::vector<BodyFrame> frames;
	const float dt = 1.f / hz;
	for(int i = 0; i < 10; i++) {
		frames.push_back({ 0.f, Standing, false });
	}
	float t = 0.f;
	while(t < 0.2f) {
		t += dt;
		frames.push_back({ -130.f * std::min(t / 0.2f, 1.f), Standing, true });
	}
	float y = -130.f;
	float v = 0.f;
	while(y < landY) {
		v += 1000.f * dt;
		y = std::min(y + v * dt, landY);
		frames.push_back({ y, Standing, true });
	}
	for(int i = 0; i < restAfter; i++) {
		frames.push_back({ landY, Standing, false });
	}
	return frames;
}

} // anonymous namespace

void StableViewTest::testFirstFrameAndStill() {
	vr::StableView view;
	vr::StableViewInput input;
	input.dt = 1.f / 90.f;
	input.feetY = 500.f;
	input.eyeAboveFeet = Standing;
	vr::updateStableView(view, input, vr::StableViewParams());
	// The first frame is where the body is
	CPPUNIT_ASSERT(view.valid);
	CPPUNIT_ASSERT_EQUAL(330.f, view.cameraY());
	CPPUNIT_ASSERT_EQUAL(u64(1), view.cuts);
	for(int i = 0; i < 100; i++) {
		vr::updateStableView(view, input, vr::StableViewParams());
		CPPUNIT_ASSERT_EQUAL(330.f, view.cameraY());
	}
	CPPUNIT_ASSERT_EQUAL(u64(1), view.cuts);
}

void StableViewTest::testStepUpGlides() {
	// A stair step of 30 units in one frame: the view rises 2 units a frame at 90 Hz, 4 at 45 Hz (the same speed)
	for(float hz : { 90.f, 45.f }) {
		std::vector<BodyFrame> frames(5, BodyFrame{ 0.f, Standing, false });
		frames.resize(100, BodyFrame{ -30.f, Standing, false });
		const std::vector<float> heights = run(frames, hz, vr::StableViewParams());
		CPPUNIT_ASSERT(largestStep(heights) <= 180.f / hz + 1e-3f);
		CPPUNIT_ASSERT(largestStep(heights) >= 180.f / hz - 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-30.f - Standing, heights.back(), 1e-3f);
	}
}

void StableViewTest::testLevelJumpKeepsTheView() {
	// A jump on the spot does not move the level view at all
	for(float hz : { 90.f, 45.f }) {
		const std::vector<float> heights = run(jump(hz, 0.f), hz, vr::StableViewParams());
		for(float h : heights) {
			CPPUNIT_ASSERT_EQUAL(-Standing, h);
		}
	}
}

void StableViewTest::testLevelJumpOntoALedge() {
	// Landing 40 units higher: the view stays level in the air and glides up after the landing, within the limit
	vr::StableView view;
	const std::vector<float> heights = run(jump(90.f, -40.f), 90.f, vr::StableViewParams(), &view);
	CPPUNIT_ASSERT(largestStep(heights) <= 2.f + 1e-3f);
	CPPUNIT_ASSERT(*std::min_element(heights.begin(), heights.end()) >= -40.f - Standing - 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-40.f - Standing, heights.back(), 1e-3f);
	CPPUNIT_ASSERT_EQUAL(u64(1), view.cuts);
}

void StableViewTest::testArcJumpIsSoft() {
	// The arc: a soft hop - up and back, never faster than the limit, much lower than the jump's 130 units
	vr::StableViewParams params;
	params.jump = vr::JumpView::Arc;
	for(float hz : { 90.f, 45.f }) {
		const std::vector<float> heights = run(jump(hz, 0.f), hz, params);
		CPPUNIT_ASSERT(largestStep(heights) <= 180.f / hz + 1e-3f);
		const float top = *std::min_element(heights.begin(), heights.end());
		CPPUNIT_ASSERT(top < -Standing - 20.f);
		CPPUNIT_ASSERT(top > -Standing - 130.f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-Standing, heights.back(), 1e-3f);
	}
}

void StableViewTest::testCrouchGlides() {
	// The crouch toggle: the eye glides the 50 units at the chosen speed - slow takes a second, fast keeps the limit
	const vr::CrouchGlide glides[] = { vr::CrouchGlide::Slow, vr::CrouchGlide::Medium, vr::CrouchGlide::Fast };
	for(vr::CrouchGlide glide : glides) {
		vr::StableViewParams params;
		params.crouchSpeed = vr::crouchGlideSpeed(glide);
		std::vector<BodyFrame> frames(5, BodyFrame{ 0.f, Standing, false });
		frames.resize(200, BodyFrame{ 0.f, Crouched, false });
		const std::vector<float> heights = run(frames, 90.f, params);
		CPPUNIT_ASSERT(largestStep(heights) <= params.crouchSpeed / 90.f + 1e-3f);
		CPPUNIT_ASSERT(largestStep(heights) <= 2.f + 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-Crouched, heights.back(), 1e-3f);
		// Not there in the first frame after the toggle
		CPPUNIT_ASSERT(heights[5] < -Crouched - 45.f);
	}
	// Slow: the 50 units in about a second
	vr::StableViewParams slow;
	std::vector<BodyFrame> frames(1, BodyFrame{ 0.f, Standing, false });
	frames.resize(1 + 80, BodyFrame{ 0.f, Crouched, false });
	CPPUNIT_ASSERT(run(frames, 90.f, slow).back() < -Crouched - 4.f);
	frames.resize(1 + 95, BodyFrame{ 0.f, Crouched, false });
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-Crouched, run(frames, 90.f, slow).back(), 1e-3f);
}

void StableViewTest::testCrouchOnStairsKeepsTheLimit() {
	// Going up stairs (1.5 m/s up) while crouching at the fast glide: feet and eye share one limit
	vr::StableViewParams params;
	params.crouchSpeed = vr::crouchGlideSpeed(vr::CrouchGlide::Fast);
	std::vector<BodyFrame> frames;
	frames.push_back({ 0.f, Standing, false });
	for(int i = 1; i < 60; i++) {
		frames.push_back({ -150.f * float(i) / 90.f, Crouched, false });
	}
	frames.resize(300, BodyFrame{ -150.f * 59.f / 90.f, Crouched, false });
	const std::vector<float> heights = run(frames, 90.f, params);
	CPPUNIT_ASSERT(largestStep(heights) <= 2.f + 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-150.f * 59.f / 90.f - Crouched, heights.back(), 1e-3f);
}

void StableViewTest::testLevitationGlides() {
	// Levitating, the head floats 30 units over the standing eye: the view rises there and back down when it ends,
	// at the glide's speed, never faster than the limit, at 90 and 45 Hz
	for(float hz : { 90.f, 45.f }) {
		std::vector<BodyFrame> frames(5, BodyFrame{ 0.f, Standing, false });
		frames.resize(200, BodyFrame{ 0.f, Standing + vr::LevitateEyeRise, false });
		const size_t top = frames.size() - 1;
		frames.resize(400, BodyFrame{ 0.f, Standing, false });
		const std::vector<float> heights = run(frames, hz, vr::StableViewParams());
		CPPUNIT_ASSERT(largestStep(heights) <= 50.f / hz + 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-Standing - vr::LevitateEyeRise, heights[top], 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-Standing, heights.back(), 1e-3f);
	}
}

void StableViewTest::testFallPullsTheView() {
	// A fall off a ledge (no jump): the view follows, never more than the lag above the body, and lands with it
	std::vector<BodyFrame> frames(5, BodyFrame{ 0.f, Standing, false });
	float y = 0.f;
	float v = 0.f;
	while(y < 500.f) {
		v += 1000.f / 90.f;
		y = std::min(y + v / 90.f, 500.f);
		frames.push_back({ y, Standing, false });
	}
	frames.resize(frames.size() + 100, BodyFrame{ 500.f, Standing, false });
	vr::StableView view;
	vr::StableViewInput input;
	input.dt = 1.f / 90.f;
	for(const BodyFrame & frame : frames) {
		input.feetY = frame.feetY;
		input.eyeAboveFeet = frame.eye;
		vr::updateStableView(view, input, vr::StableViewParams());
		CPPUNIT_ASSERT(frame.feetY - view.feetY <= vr::ViewMaxLag + 1e-3f);
		CPPUNIT_ASSERT(view.feetY <= frame.feetY + 1e-3f);
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(500.f - Standing, view.cameraY(), 1e-3f);
	CPPUNIT_ASSERT_EQUAL(u64(1), view.cuts);
}

void StableViewTest::testTeleportCuts() {
	std::vector<BodyFrame> frames(5, BodyFrame{ 0.f, Standing, false });
	frames.push_back({ 1000.f, Crouched, false });
	vr::StableView view;
	const std::vector<float> heights = run(frames, 90.f, vr::StableViewParams(), &view);
	CPPUNIT_ASSERT_EQUAL(1000.f - Crouched, heights.back());
	CPPUNIT_ASSERT_EQUAL(u64(2), view.cuts);
}

void StableViewTest::testNames() {
	vr::JumpView jump = vr::JumpView::Arc;
	CPPUNIT_ASSERT(vr::parseJumpView("level", jump));
	CPPUNIT_ASSERT(jump == vr::JumpView::Level);
	CPPUNIT_ASSERT(vr::parseJumpView(vr::jumpViewName(vr::JumpView::Arc), jump));
	CPPUNIT_ASSERT(jump == vr::JumpView::Arc);
	CPPUNIT_ASSERT(!vr::parseJumpView("high", jump));
	CPPUNIT_ASSERT(jump == vr::JumpView::Arc);
	vr::CrouchGlide glide = vr::CrouchGlide::Slow;
	for(vr::CrouchGlide g : { vr::CrouchGlide::Slow, vr::CrouchGlide::Medium, vr::CrouchGlide::Fast }) {
		CPPUNIT_ASSERT(vr::parseCrouchGlide(vr::crouchGlideName(g), glide));
		CPPUNIT_ASSERT(glide == g);
	}
	CPPUNIT_ASSERT(!vr::parseCrouchGlide("", glide));
	vr::DeathView death = vr::DeathView::Body;
	CPPUNIT_ASSERT(vr::parseDeathView("headless", death));
	CPPUNIT_ASSERT(death == vr::DeathView::Headless);
	CPPUNIT_ASSERT(vr::parseDeathView(vr::deathViewName(vr::DeathView::Body), death));
	CPPUNIT_ASSERT(death == vr::DeathView::Body);
	CPPUNIT_ASSERT(!vr::parseDeathView("engine", death));
}

namespace {

vr::CameraSample sample(const Vec3f & camera, const Vec3f & body, float yaw = 0.f, float bodyYaw = 0.f,
                        float pitch = 0.f) {
	vr::CameraSample s;
	s.dt = 1.f / 90.f;
	s.camera = camera;
	s.body = body;
	s.angle = Anglef(pitch, yaw, 0.f);
	s.bodyYaw = bodyYaw;
	return s;
}

} // anonymous namespace

void StableViewTest::testMeterStill() {
	vr::CameraMotion motion;
	for(int i = 0; i < 10; i++) {
		vr::addCameraSample(motion, sample(Vec3f(10.f, -170.f, 5.f), Vec3f(0.f, -170.f, 0.f)));
	}
	CPPUNIT_ASSERT_EQUAL(u64(9), motion.frames);
	CPPUNIT_ASSERT_EQUAL(0.f, motion.maxStepY);
	CPPUNIT_ASSERT_EQUAL(0.f, motion.maxOffStepXZ);
	CPPUNIT_ASSERT_EQUAL(0.f, motion.maxYawStep);
	CPPUNIT_ASSERT(motion.offsetRange() == Vec3f(0.f));
	CPPUNIT_ASSERT_EQUAL(0.f, motion.cameraRangeY());
	vr::resetCameraMotion(motion);
	CPPUNIT_ASSERT_EQUAL(u64(0), motion.frames);
	CPPUNIT_ASSERT(!motion.have);
}

void StableViewTest::testMeterBodyTravelAndTurn() {
	// The camera walks with the body and turns with the heading: nothing that is not the body's own
	vr::CameraMotion motion;
	for(int i = 0; i < 20; i++) {
		const Vec3f body(3.f * float(i), -170.f, 0.f);
		const float yaw = 350.f + 2.f * float(i); // across 0
		vr::addCameraSample(motion, sample(body, body, yaw, yaw));
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3.f, motion.maxStepXZ, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, motion.maxOffStepXZ, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.f, motion.maxYawStep, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, motion.maxOffYawStep, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, motion.offsetRange().x, 1e-4f);
}

void StableViewTest::testMeterSway() {
	// A camera bobbing on a still body: height steps, a range and a pitch
	vr::CameraMotion motion;
	const float bob[] = { 0.f, 1.5f, 3.f, 1.5f, 0.f, -1.5f };
	int i = 0;
	for(float b : bob) {
		vr::addCameraSample(motion, sample(Vec3f(0.f, -170.f + b, 0.5f * b), Vec3f(0.f, -170.f, 0.f), 0.f, 0.f,
		                                   i++ % 2 ? 1.f : 0.f));
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.5f, motion.maxStepY, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(135.f, motion.maxSpeedY, 1e-2f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.5f, motion.maxOffStepY, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.75f, motion.maxOffStepXZ, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(4.5f, motion.offsetRange().y, 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(4.5f, motion.cameraRangeY(), 1e-4f);
	CPPUNIT_ASSERT_EQUAL(0.f, motion.bodyRangeY());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, motion.maxPitchStep, 1e-4f);
}
