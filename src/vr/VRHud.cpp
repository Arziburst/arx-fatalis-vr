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

#include "vr/VRHud.h"

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "game/Player.h"
#include "io/log/Logger.h"
#include "vr/VRSystem.h"
#include "vr/WristHud.h"

namespace vr {

namespace {

struct HudFrameState {
	HudGate gate;
	HudStatus status;
	LevelUpIndicator levelUp;
	std::string keepParsed;   //!< the vr_hud_keep the mask was parsed from
	bool logged = false;
	HudGate loggedGate;
};

HudFrameState g_hud;

void parseHudKeepSetting() {
	const std::string & keep = config.video.vrHudKeep;
	if(g_hud.logged && keep == g_hud.keepParsed) {
		return;
	}
	std::string unknown;
	g_hud.gate.keep = parseHudKeep(keep, &unknown);
	g_hud.keepParsed = keep;
	if(!unknown.empty()) {
		LogWarning << "[vr] vr_hud_keep: unknown HUD element \"" << unknown << "\" ignored";
	}
}

std::string_view nextHudWord(std::string_view & text) {
	const size_t start = text.find_first_not_of(" \t");
	if(start == std::string_view::npos) {
		text = std::string_view();
		return text;
	}
	text.remove_prefix(start);
	const size_t end = text.find_first_of(" \t");
	const std::string_view word = text.substr(0, end);
	text.remove_prefix(end == std::string_view::npos ? text.size() : end);
	return word;
}

} // anonymous namespace

void updateHud() {

	parseHudKeepSetting();
	HudGate & gate = g_hud.gate;
	gate.vr = isEnabled();
	gate.frame = config.video.vrHudFrame;
	gate.wristVitals = wristVitalsShown();

	HudStatus & status = g_hud.status;
	status.vr = gate.vr;
	status.frame = gate.frame;
	status.keep = config.video.vrHudKeep;
	status.frames++;

	// Logged when it changes, never per frame
	if(!g_hud.logged || gate.vr != g_hud.loggedGate.vr || gate.frame != g_hud.loggedGate.frame
	   || gate.keep != g_hud.loggedGate.keep) {
		g_hud.logged = true;
		g_hud.loggedGate = gate;
		gameevent::Event("vr.hud")("vr", gate.vr ? "yes" : "no")("frame", gate.frame ? "on" : "off")
			("keep", status.keep.empty() ? std::string("none") : status.keep);
	}

	const LevelUpMode mode = levelUpModeFromName(config.video.vrLevelUp);
	const bool pending = player.Skill_Redistribute != 0 || player.Attribute_Redistribute != 0;
	const s64 nowUs = toUs(g_platformTime.frameStart() - PlatformInstant(0));
	if(g_hud.levelUp.update(pending, s32(player.level), nowUs, mode)) {
		gameevent::Event("vr.levelup")("level", s32(player.level))("mode", levelUpModeName(mode))
			("radial", g_hud.levelUp.radialMark() ? "yes" : "no")("wrist", g_hud.levelUp.wristNotice() ? "yes" : "no");
	}
}

bool hudDraw(HudElement element) {
	if(!hudShown(element)) {
		return false;
	}
	g_hud.status.drawn[size_t(element)]++;
	return true;
}

bool hudShown(HudElement element) {
	return hudElementDrawn(element, g_hud.gate);
}

const LevelUpIndicator & levelUpIndicator() {
	return g_hud.levelUp;
}

void noteLevelUpWristDrawn() {
	g_hud.status.levelUpWristViews++;
}

void noteLevelUpRadialDrawn() {
	g_hud.status.levelUpRadialImages++;
}

const HudStatus & hudStatus() {
	return g_hud.status;
}

u64 hudDrawnTotal() {
	u64 total = 0;
	for(u64 drawn : g_hud.status.drawn) {
		total += drawn;
	}
	return total;
}

std::string hudCommand(std::string_view arguments) {

	std::string_view rest = arguments;
	const std::string_view verb = nextHudWord(rest);
	const std::string_view value = nextHudWord(rest);

	if(verb == "frame" && (value == "on" || value == "off")) {
		config.video.vrHudFrame = (value == "on");
		LogInfo << "[vr] HUD frame " << value << " in VR (test channel)";
		return std::string();
	}
	if(verb == "keep" && !value.empty()) {
		// The names may be separated by spaces too: everything after "keep"
		std::string list = std::string(value) + std::string(rest);
		if(list == "none") {
			list.clear();
		}
		std::string unknown;
		parseHudKeep(list, &unknown);
		if(!unknown.empty()) {
			return "vr-hud keep: unknown HUD element \"" + unknown + "\"";
		}
		config.video.vrHudKeep = list;
		LogInfo << "[vr] HUD elements kept in VR: " << (list.empty() ? "none" : list) << " (test channel)";
		return std::string();
	}
	if(verb == "levelup" && value == levelUpModeName(levelUpModeFromName(value, LevelUpMode::Off))) {
		config.video.vrLevelUp = std::string(value);
		LogInfo << "[vr] level-up indication: " << value << " (test channel)";
		return std::string();
	}
	if(verb == "reset" && value.empty()) {
		g_hud.status.drawn.fill(0);
		return std::string();
	}

	return "vr-hud: expected frame on|off, keep <names>|none, levelup both|radial|wrist|off or reset, got \""
	       + std::string(arguments) + "\"";
}

} // namespace vr
