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

#include "vr/StableView.h"

#include <algorithm>
#include <cmath>

#include "vr/BodyYaw.h"

namespace vr {

bool parseJumpView(std::string_view text, JumpView & value) {
	if(text == "level") {
		value = JumpView::Level;
	} else if(text == "arc") {
		value = JumpView::Arc;
	} else {
		return false;
	}
	return true;
}

bool parseCrouchGlide(std::string_view text, CrouchGlide & value) {
	if(text == "slow") {
		value = CrouchGlide::Slow;
	} else if(text == "medium") {
		value = CrouchGlide::Medium;
	} else if(text == "fast") {
		value = CrouchGlide::Fast;
	} else {
		return false;
	}
	return true;
}

bool parseDeathView(std::string_view text, DeathView & value) {
	if(text == "body") {
		value = DeathView::Body;
	} else if(text == "headless") {
		value = DeathView::Headless;
	} else {
		return false;
	}
	return true;
}

const char * jumpViewName(JumpView value) {
	switch(value) {
		case JumpView::Level: return "level";
		case JumpView::Arc:   return "arc";
	}
	return "?";
}

const char * crouchGlideName(CrouchGlide value) {
	switch(value) {
		case CrouchGlide::Slow:   return "slow";
		case CrouchGlide::Medium: return "medium";
		case CrouchGlide::Fast:   return "fast";
	}
	return "?";
}

const char * deathViewName(DeathView value) {
	switch(value) {
		case DeathView::Body:     return "body";
		case DeathView::Headless: return "headless";
	}
	return "?";
}

float crouchGlideSpeed(CrouchGlide value) {
	switch(value) {
		case CrouchGlide::Slow:   return 50.f;
		case CrouchGlide::Medium: return 100.f;
		case CrouchGlide::Fast:   return ViewMaxSpeed;
	}
	return 50.f;
}

namespace {

//! \return \a value moved towards \a target by at most \a maxStep
float moveTowards(float value, float target, float maxStep) {
	return value + std::clamp(target - value, -maxStep, maxStep);
}

} // anonymous namespace

void updateStableView(StableView & view, const StableViewInput & input, const StableViewParams & params) {

	// The first frame, and a body that moved further than any travel in one frame (a teleport, a level load,
	// a respawn): the view is put where the body is - a cut, not a glide across the level
	if(!view.valid || std::abs(input.feetY - view.bodyFeetY) > params.teleport) {
		view.valid = true;
		view.feetY = input.feetY;
		view.eye = input.eyeAboveFeet;
		view.bodyFeetY = input.feetY;
		view.jumping = input.jumping;
		view.takeoffY = input.feetY;
		view.lagCap = params.maxLag;
		view.cuts++;
		return;
	}
	view.bodyFeetY = input.feetY;

	const float dt = std::max(input.dt, 0.f);
	const float maxStep = params.maxSpeed * dt;

	if(input.jumping && !view.jumping) {
		view.takeoffY = view.feetY;
	}
	view.jumping = input.jumping;

	// Where the view's feet go: the body's feet - in a level jump never above the take-off (engine y is down)
	float target = input.feetY;
	if(input.jumping && params.jump == JumpView::Level) {
		target = std::max(input.feetY, view.takeoffY);
	}
	const float feetBefore = view.feetY;
	view.feetY = moveTowards(view.feetY, target, maxStep);

	// A fall faster than the limit pulls the view along: it stays within the lag of the body. A jump's arc is only
	// ever followed at the limit (Arc); once it is over, a lag it left beyond the cap closes at the catch-up speed
	const float lag = std::abs(target - view.feetY);
	if(input.jumping && params.jump == JumpView::Arc) {
		view.lagCap = std::max(params.maxLag, lag);
	} else {
		view.lagCap = std::max(params.maxLag, view.lagCap - params.catchUp * dt);
		if(lag > view.lagCap) {
			view.feetY = target + (view.feetY > target ? view.lagCap : -view.lagCap);
		}
	}

	// The eye glides to the stance's eye within what the feet left of this frame's limit
	const float feetStep = std::abs(view.feetY - feetBefore);
	const float eyeStep = std::min(params.crouchSpeed * dt, std::max(0.f, maxStep - feetStep));
	view.eye = moveTowards(view.eye, input.eyeAboveFeet, eyeStep);
}

void resetCameraMotion(CameraMotion & motion) {
	motion = CameraMotion();
}

void addCameraSample(CameraMotion & motion, const CameraSample & sample) {

	const Vec3f offset = sample.camera - sample.body;
	if(!motion.have) {
		motion.have = true;
		motion.last = sample;
		motion.minOffset = motion.maxOffset = offset;
		motion.minCameraY = motion.maxCameraY = sample.camera.y;
		motion.minBodyY = motion.maxBodyY = sample.body.y;
		return;
	}

	const CameraSample & last = motion.last;
	const Vec3f step = sample.camera - last.camera;
	const Vec3f bodyStep = sample.body - last.body;
	const Vec3f offStep = step - bodyStep;

	motion.frames++;
	motion.maxDt = std::max(motion.maxDt, sample.dt);
	motion.maxStepY = std::max(motion.maxStepY, std::abs(step.y));
	if(sample.dt > 0.f) {
		motion.maxSpeedY = std::max(motion.maxSpeedY, std::abs(step.y) / sample.dt);
	}
	motion.maxStepXZ = std::max(motion.maxStepXZ, std::sqrt(step.x * step.x + step.z * step.z));
	motion.maxOffStepXZ = std::max(motion.maxOffStepXZ, std::sqrt(offStep.x * offStep.x + offStep.z * offStep.z));
	motion.maxOffStepY = std::max(motion.maxOffStepY, std::abs(offStep.y));

	const float pitch = std::abs(wrapSignedDegrees(sample.angle.getPitch() - last.angle.getPitch()));
	const float roll = std::abs(wrapSignedDegrees(sample.angle.getRoll() - last.angle.getRoll()));
	const float yaw = wrapSignedDegrees(sample.angle.getYaw() - last.angle.getYaw());
	const float turn = wrapSignedDegrees(sample.bodyYaw - last.bodyYaw);
	motion.maxPitchStep = std::max(motion.maxPitchStep, pitch);
	motion.maxRollStep = std::max(motion.maxRollStep, roll);
	motion.maxYawStep = std::max(motion.maxYawStep, std::abs(yaw));
	motion.maxOffYawStep = std::max(motion.maxOffYawStep, std::abs(wrapSignedDegrees(yaw - turn)));

	motion.minOffset = glm::min(motion.minOffset, offset);
	motion.maxOffset = glm::max(motion.maxOffset, offset);
	motion.minCameraY = std::min(motion.minCameraY, sample.camera.y);
	motion.maxCameraY = std::max(motion.maxCameraY, sample.camera.y);
	motion.minBodyY = std::min(motion.minBodyY, sample.body.y);
	motion.maxBodyY = std::max(motion.maxBodyY, sample.body.y);
	motion.last = sample;
}

} // namespace vr
