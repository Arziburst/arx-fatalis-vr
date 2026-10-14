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

#include "vr/HudFrame.h"

#include <algorithm>
#include <array>
#include <string>

namespace vr {

namespace {

//! In the order of HudElement; the list in docs/vr/TESTING.md (The HUD frame in VR) follows it
const std::array<HudElementInfo, HudElementCount> ElementInfo = { {
	{ "health", HudHome::Wrist, "the red bar on the left wrist (#116); the orb only while no left arm is posed" },
	{ "mana", HudHome::Wrist, "the blue bar on the left wrist (#116); the orb only while no left arm is posed" },
	{ "level_up", HudHome::Radial,
	  "a gold mark on the radial menu's Character item, which opens the character page, and a gold notice on the "
	  "left wrist for 8 s (vr_levelup)" },
	{ "book", HudHome::Radial,
	  "the radial menu's Character, Spells, Map and Quests items (#376); the new-spell flash is not shown" },
	{ "backpack", HudHome::Radial, "the radial menu's Inventory item (#376)" },
	{ "purse", HudHome::Question, "the gold amount and taking gold out of the purse have no VR place yet" },
	{ "active_spells", HudHome::Question,
	  "which spells run has no VR place yet; the radial menu's Cancel spell ends the last one" },
	{ "precast_spells", HudHome::Question, "the precast spells are neither shown nor launchable in VR yet" },
	{ "stealth", HudHome::Question, "being unseen while sneaking has no VR indication yet" },
	{ "torch", HudHome::Radial, "the torch in a radial quick slot lights it and puts it out (#376)" },
	{ "steal", HudHome::Question, "the steal icon has no VR place yet (the container panel still opens)" },
	{ "damaged_equipment", HudHome::Question, "damaged equipment has no VR indication yet" },
	{ "change_level", HudHome::Question,
	  "a level's exit: jump goes (quick_level_transition = jump); the 'you can go' sign has no VR place yet" },
	{ "quick_save", HudHome::Question, "the radial menu's Quick save gives no confirmation yet" },
	{ "memorized_runes", HudHome::Question, "the runes drawn so far have no VR display besides the trail (#43)" },
	{ "hit_strength", HudHome::Question, "the charge of a swing (the held trigger, #39) has no VR display yet" },
	{ "mechanism_hint", HudHome::None, "a hint at the mouse's cursor mode - the hands are the mouse in VR" },
	{ "screen_arrows", HudHome::None, "the monitor's edges turn the view - the head turns it in VR" },
	{ "minimap", HudHome::Radial,
	  "the radial menu's Map item opens the book's map page (#376); a map always in view has no VR place yet" },
} };

bool isSeparator(char c) {
	return c == ',' || c == ' ' || c == '\t' || c == ';';
}

} // anonymous namespace

const char * hudHomeName(HudHome home) {
	switch(home) {
		case HudHome::Wrist:     return "wrist";
		case HudHome::Radial:    return "radial";
		case HudHome::None:      return "none";
		case HudHome::Question:  return "question";
	}
	return "question";
}

const HudElementInfo & hudElementInfo(HudElement element) {
	return ElementInfo[std::min(size_t(element), HudElementCount - 1)];
}

HudElement hudElementFromName(std::string_view name) {
	for(size_t i = 0; i < HudElementCount; i++) {
		if(name == ElementInfo[i].name) {
			return HudElement(i);
		}
	}
	return HudElement::Count;
}

HudElementMask parseHudKeep(std::string_view text, std::string * unknown) {

	if(unknown) {
		unknown->clear();
	}

	HudElementMask mask = 0;
	size_t pos = 0;
	while(pos < text.size()) {
		while(pos < text.size() && isSeparator(text[pos])) {
			pos++;
		}
		size_t end = pos;
		while(end < text.size() && !isSeparator(text[end])) {
			end++;
		}
		if(end == pos) {
			break;
		}
		const std::string_view name = text.substr(pos, end - pos);
		pos = end;
		if(name == "all") {
			mask |= hudElementBit(HudElement::Count) - 1;
			continue;
		}
		const HudElement element = hudElementFromName(name);
		if(element == HudElement::Count) {
			if(unknown && unknown->empty()) {
				*unknown = std::string(name);
			}
			continue;
		}
		mask |= hudElementBit(element);
	}

	return mask;
}

bool hudElementDrawn(HudElement element, const HudGate & gate) {

	const bool orb = (element == HudElement::Health || element == HudElement::Mana);
	if(orb && gate.wristVitals) {
		return false;
	}
	if(!gate.vr || gate.frame || (gate.keep & hudElementBit(element))) {
		return true;
	}
	// Without a posed left arm health and mana have no other home: the orbs stay (an owner question)
	return orb;
}

const char * levelUpModeName(LevelUpMode mode) {
	switch(mode) {
		case LevelUpMode::Both:   return "both";
		case LevelUpMode::Radial: return "radial";
		case LevelUpMode::Wrist:  return "wrist";
		case LevelUpMode::Off:    return "off";
	}
	return "both";
}

LevelUpMode levelUpModeFromName(std::string_view name, LevelUpMode fallback) {
	for(LevelUpMode mode : { LevelUpMode::Both, LevelUpMode::Radial, LevelUpMode::Wrist, LevelUpMode::Off }) {
		if(name == levelUpModeName(mode)) {
			return mode;
		}
	}
	return fallback;
}

bool LevelUpIndicator::update(bool pending, s32 level, s64 nowUs, LevelUpMode mode, s64 noticeUs) {

	m_mode = mode;

	bool started = false;
	if(m_started && pending && (!m_pending || level > m_level)) {
		m_notice = true;
		m_noticeStart = nowUs;
		m_notices++;
		started = true;
	}
	m_started = true;
	m_pending = pending;
	m_level = level;

	if(m_notice) {
		const s64 elapsed = std::max(nowUs - m_noticeStart, s64(0));
		if(!pending || noticeUs <= 0 || elapsed >= noticeUs) {
			m_notice = false;
			m_progress = 0.f;
		} else {
			m_progress = float(elapsed) / float(noticeUs);
		}
	}

	return started;
}

} // namespace vr
