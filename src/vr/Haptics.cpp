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

#include "vr/Haptics.h"

#include <algorithm>
#include <cmath>

namespace vr {

const char * hapticEventName(HapticEvent event) {
	switch(event) {
		case HapticEvent::HitTaken: return "hit_taken";
		case HapticEvent::WeaponHit: return "weapon_hit";
		case HapticEvent::ItemGrabbed: return "item_grabbed";
		case HapticEvent::WeaponBlocked: return "weapon_blocked";
		case HapticEvent::PointerTick: return "pointer_tick";
		case HapticEvent::RuneStroke: return "rune_stroke";
		case HapticEvent::SpellCast: return "spell_cast";
		case HapticEvent::ItemStashed: return "item_stashed";
		case HapticEvent::Count: break;
	}
	return "unknown";
}

std::optional<HapticEvent> parseHapticEvent(std::string_view name) {
	for(size_t i = 0; i < HapticEventCount; i++) {
		if(name == hapticEventName(HapticEvent(i))) {
			return HapticEvent(i);
		}
	}
	return std::nullopt;
}

HapticHand hapticHand(HapticEvent event) {
	switch(event) {
		case HapticEvent::HitTaken: return HapticHand::Both;
		case HapticEvent::WeaponHit: return HapticHand::Weapon;
		case HapticEvent::ItemGrabbed: return HapticHand::Pointer;
		case HapticEvent::WeaponBlocked: return HapticHand::Weapon;
		case HapticEvent::PointerTick: return HapticHand::Pointer;
		case HapticEvent::RuneStroke: return HapticHand::Pointer;
		case HapticEvent::SpellCast: return HapticHand::Both;
		case HapticEvent::ItemStashed: return HapticHand::Pointer;
		case HapticEvent::Count: break;
	}
	return HapticHand::Both;
}

HapticPulse hapticPulse(HapticEvent event, float intensity) {

	if(!std::isfinite(intensity)) {
		intensity = 0.f;
	}
	intensity = std::clamp(intensity, 0.f, 1.f);

	switch(event) {
		case HapticEvent::HitTaken: {
			float amplitude = std::min(HitTakenBase + HitTakenPerLife * intensity, HitTakenMax);
			return { amplitude, HitTakenSeconds, HitTakenHz };
		}
		case HapticEvent::WeaponHit: return { WeaponHitAmplitude, WeaponHitSeconds, WeaponHitHz };
		case HapticEvent::ItemGrabbed: return { ItemGrabbedAmplitude, ItemGrabbedSeconds, ItemGrabbedHz };
		case HapticEvent::WeaponBlocked: return { WeaponBlockedAmplitude, WeaponBlockedSeconds, 100.f };
		case HapticEvent::PointerTick: return { PointerTickAmplitude, PointerTickSeconds, 200.f };
		case HapticEvent::RuneStroke: return { RuneStrokeAmplitude, RuneStrokeSeconds, 160.f };
		case HapticEvent::SpellCast: return { SpellCastAmplitude, SpellCastSeconds, 100.f };
		case HapticEvent::ItemStashed: return { ItemStashedAmplitude, ItemStashedSeconds, 160.f };
		case HapticEvent::Count: break;
	}

	return HapticPulse();
}

namespace {

//! The stronger of two pulses; the longer one on a tie
bool stronger(const HapticPulse & a, const HapticPulse & b) {
	return a.amplitude > b.amplitude || (a.amplitude == b.amplitude && a.seconds > b.seconds);
}

} // anonymous namespace

bool HapticMixer::request(Hand hand, HapticPulse pulse) {

	if(!(strength > 0.f) || pulse.empty()) {
		return false;
	}
	pulse.amplitude = std::min(pulse.amplitude * strength, 1.f);

	std::optional<HapticPulse> & pending = m_pending[size_t(hand)];
	if(pending) {
		m_merged++;
		if(!stronger(pulse, *pending)) {
			return true;
		}
	}
	pending = pulse;

	return true;
}

std::array<std::optional<HapticPulse>, HandCount> HapticMixer::take(double now) {

	std::array<std::optional<HapticPulse>, HandCount> result;

	for(size_t hand = 0; hand < HandCount; hand++) {
		std::optional<HapticPulse> & pending = m_pending[hand];
		if(!pending) {
			continue;
		}
		if(now < m_playingUntil[hand] && m_playing[hand].amplitude > pending->amplitude) {
			m_masked++;
		} else {
			m_playing[hand] = *pending;
			m_playingUntil[hand] = now + double(pending->seconds);
			result[hand] = pending;
		}
		pending.reset();
	}

	return result;
}

void HapticMixer::clear() {
	m_pending = { };
	m_playing = { };
	m_playingUntil = { };
}

} // namespace vr
