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

#include <string>

#include "core/Config.h"
#include "io/log/Logger.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

ComfortMode g_mode = ComfortMode::Auto;

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

	if(!g_logged || policy != g_policy || active != g_loggedActive || g_mode != g_loggedMode) {
		std::string ignored;
		if(active && (config.video.viewBobbing || config.video.screenShake)) {
			ignored = " (cfg.ini view_bobbing=" + std::to_string(int(config.video.viewBobbing)) + " screen_shake="
			          + std::to_string(int(config.video.screenShake)) + " ignored)";
		}
		LogInfo << "[vr] comfort: decided by " << decider(active) << " - view bobbing " << onOff(policy.viewBobbing)
		        << ", screen shake " << onOff(policy.screenShake) << ", FOV zoom " << onOff(policy.fovZoom) << ignored;
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

} // namespace vr
