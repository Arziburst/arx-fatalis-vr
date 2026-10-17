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

#include "vr/VRComfort.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "io/log/Logger.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

ComfortMode g_mode = ComfortMode::Auto;
ComfortMode g_stableMode = ComfortMode::Auto;

// The stable view (#416)
StableView g_view;
bool g_viewPlaced = false;
bool g_ghost = false;
CameraMotion g_motion;
int g_traceFrames = 0;
float g_viewSeconds = 0.f;
s64 g_lastDisplayTime = 0;
float g_stepSeconds = 0.f;
std::array<float, 30> g_slots = {};
size_t g_slotIndex = 0;
ViewVariants g_variants;
bool g_variantsLogged = false;
std::string g_loggedVariants;

// What was logged last, so the policy line is written on changes only - never per frame
bool g_logged = false;
ComfortMode g_loggedMode = ComfortMode::Auto;
bool g_loggedActive = false;
ComfortPolicy g_policy;

const char * onOff(bool on) {
	return on ? "on" : "off";
}

//! Who decided the current policy, for the log line
const char * decider(bool active) {
	switch(g_mode) {
		case ComfortMode::On:  return "the test channel (forced on)";
		case ComfortMode::Off: return "the test channel (forced off), cfg.ini decides";
		case ComfortMode::Auto: break;
	}
	return active ? "the VR session, all off" : "flat mode, cfg.ini decides";
}

} // anonymous namespace

bool comfortActive() {
	switch(g_mode) {
		case ComfortMode::On:  return true;
		case ComfortMode::Off: return false;
		case ComfortMode::Auto: break;
	}
	return isEnabled();
}

const ComfortPolicy & comfort() {

	bool active = comfortActive();

	ComfortPolicy policy;
	policy.viewBobbing = !active && config.video.viewBobbing;
	policy.screenShake = !active && config.video.screenShake;
	policy.fovZoom = !active;
	switch(g_stableMode) {
		case ComfortMode::On:   policy.stableView = true; break;
		case ComfortMode::Off:  policy.stableView = false; break;
		case ComfortMode::Auto: policy.stableView = active; break;
	}

	if(!g_logged || policy != g_policy || active != g_loggedActive || g_mode != g_loggedMode) {
		std::string ignored;
		if(active && (config.video.viewBobbing || config.video.screenShake)) {
			ignored = " (cfg.ini view_bobbing=" + std::to_string(int(config.video.viewBobbing)) + " screen_shake="
			          + std::to_string(int(config.video.screenShake)) + " ignored)";
		}
		LogInfo << "[vr] comfort: decided by " << decider(active) << " - view bobbing " << onOff(policy.viewBobbing)
		        << ", screen shake " << onOff(policy.screenShake) << ", FOV zoom " << onOff(policy.fovZoom)
		        << ", stable view " << onOff(policy.stableView)
		        << (g_stableMode != ComfortMode::Auto ? " (forced by the test channel)" : "") << ignored;
		g_logged = true;
		g_loggedMode = g_mode;
		g_loggedActive = active;
		g_policy = policy;
	}

	return g_policy;
}

void setComfortMode(ComfortMode mode) {
	if(mode == g_mode) {
		return;
	}
	LogInfo << "[vr] comfort: mode " << comfortModeName(g_mode) << " -> " << comfortModeName(mode)
	        << " (test channel)";
	g_mode = mode;
	comfort(); // log the new policy right away
}

ComfortMode comfortMode() {
	return g_mode;
}

const char * comfortModeName(ComfortMode mode) {
	switch(mode) {
		case ComfortMode::Auto: return "auto";
		case ComfortMode::On:   return "on";
		case ComfortMode::Off:  return "off";
	}
	return "?";
}

void setStableViewMode(ComfortMode mode) {
	if(mode == g_stableMode) {
		return;
	}
	LogInfo << "[vr] view: stable view mode " << comfortModeName(g_stableMode) << " -> " << comfortModeName(mode)
	        << " (test channel)";
	g_stableMode = mode;
	comfort(); // log the new policy right away
}

ComfortMode stableViewMode() {
	return g_stableMode;
}

const ViewVariants & viewVariants() {

	ViewVariants variants;
	std::string rejected;
	if(!parseJumpView(config.video.vrViewJump, variants.jump)) {
		rejected += " vr_view_jump=\"" + config.video.vrViewJump + "\"";
	}
	if(!parseCrouchGlide(config.video.vrViewCrouch, variants.crouch)) {
		rejected += " vr_view_crouch=\"" + config.video.vrViewCrouch + "\"";
	}
	if(!parseDeathView(config.video.vrDeathView, variants.death)) {
		rejected += " vr_death_view=\"" + config.video.vrDeathView + "\"";
	}
	g_variants = variants;

	std::string line = std::string("jump ") + jumpViewName(variants.jump) + ", crouch glide "
	                   + crouchGlideName(variants.crouch) + ", death " + deathViewName(variants.death);
	if(!g_variantsLogged || line != g_loggedVariants) {
		LogInfo << "[vr] view: " << line << " (cfg.ini vr_view_jump, vr_view_crouch, vr_death_view)";
		if(!rejected.empty()) {
			LogWarning << "[vr] view: not a variant, the default is used instead:" << rejected;
		}
		g_variantsLogged = true;
		g_loggedVariants = line;
	}

	return g_variants;
}

StableViewParams stableViewParams() {
	const ViewVariants & variants = viewVariants();
	StableViewParams params;
	params.jump = variants.jump;
	params.crouchSpeed = crouchGlideSpeed(variants.crouch);
	return params;
}

std::string setViewVariant(std::string_view name, std::string_view value) {
	if(name == "jump") {
		JumpView jump;
		if(!parseJumpView(value, jump)) {
			return "view jump: expected level or arc, got \"" + std::string(value) + "\"";
		}
		config.video.vrViewJump = jumpViewName(jump);
	} else if(name == "crouch") {
		CrouchGlide crouch;
		if(!parseCrouchGlide(value, crouch)) {
			return "view crouch: expected slow, medium or fast, got \"" + std::string(value) + "\"";
		}
		config.video.vrViewCrouch = crouchGlideName(crouch);
	} else if(name == "death") {
		DeathView death;
		if(!parseDeathView(value, death)) {
			return "view death: expected headless or body, got \"" + std::string(value) + "\"";
		}
		config.video.vrDeathView = deathViewName(death);
	} else {
		return "view: expected jump, crouch or death, got \"" + std::string(name) + "\"";
	}
	viewVariants(); // log the change right away
	return std::string();
}

StableView & stableView() {
	return g_view;
}

bool stableViewPlaced() {
	return g_viewPlaced;
}

void setStableViewPlaced(bool placed) {
	g_viewPlaced = placed;
}

bool deathGhost() {
	return g_ghost;
}

void setDeathGhost(bool ghost) {
	g_ghost = ghost;
}

CameraMotion & cameraMotion() {
	return g_motion;
}

void noteViewFrame(float gameSeconds) {
	g_viewSeconds = gameSeconds;
	g_stepSeconds = gameSeconds;
	const FrameViews & views = frameViews();
	if(views.displayTime != 0 && views.displayTime != g_lastDisplayTime) {
		const double seconds = double(views.displayTime - g_lastDisplayTime) * 1e-9;
		if(g_lastDisplayTime != 0 && seconds > 0.0 && seconds < 0.25) {
			g_viewSeconds = float(seconds);
			// One display slot: the shortest of the last frames' intervals (a missed slot is a longer one)
			g_slots[g_slotIndex] = g_viewSeconds;
			g_slotIndex = (g_slotIndex + 1) % g_slots.size();
			float slot = g_viewSeconds;
			for(float s : g_slots) {
				if(s > 0.f) {
					slot = std::min(slot, s);
				}
			}
			g_stepSeconds = slot;
		}
		g_lastDisplayTime = views.displayTime;
	}
}

float viewFrameSeconds() {
	return g_viewSeconds;
}

float viewStepSeconds() {
	return g_stepSeconds;
}

void setViewTrace(int frames) {
	g_traceFrames = std::clamp(frames, 0, 2000);
}

bool takeViewTraceFrame() {
	if(g_traceFrames <= 0) {
		return false;
	}
	g_traceFrames--;
	return true;
}

void reportCameraMotion(std::string_view name) {
	const CameraMotion & m = g_motion;
	const Vec3f range = m.offsetRange();
	const float rangeXZ = std::sqrt(range.x * range.x + range.z * range.z);
	gameevent::Event("vr.view.motion")("name", std::string(name))("stable", comfort().stableView)
		("frames", m.frames)("maxDtMs", m.maxDt * 1000.f, 1)
		("maxStepY", m.maxStepY, 2)("maxStepY90", m.maxSpeedY / 90.f, 2)("cameraRangeY", m.cameraRangeY(), 1)
		("bodyRangeY", m.bodyRangeY(), 1)
		("maxStepXZ", m.maxStepXZ, 2)("maxOffStepXZ", m.maxOffStepXZ, 2)("maxOffStepY", m.maxOffStepY, 2)
		("rangeXZ", rangeXZ, 1)("rangeY", range.y, 1)
		("maxPitchStep", m.maxPitchStep, 2)("maxRollStep", m.maxRollStep, 2)("maxOffYawStep", m.maxOffYawStep, 2);
}

} // namespace vr
