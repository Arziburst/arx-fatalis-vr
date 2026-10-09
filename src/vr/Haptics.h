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

#ifndef ARX_VR_HAPTICS_H
#define ARX_VR_HAPTICS_H

#include <array>
#include <optional>
#include <string_view>

#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief Haptic feedback: the event table and the per-hand mixer (#42, F-44, F-193, D-034)
 *
 * The game asks for a pulse by event (\ref HapticEvent); the table turns it into a strength, a
 * duration and a frequency, and \ref HapticMixer decides per hand what is actually sent: the
 * strongest request of a frame wins, and a weaker pulse never cuts a stronger one that is still
 * playing. vr/VRInput.cpp drains the mixer on the frame thread right after the actions are synced.
 *
 * The numbers are the MVP defaults the coordinator approved (D-034) from VHVR's and HIGGS' tables;
 * the owner judges them in the headset. Pure logic - no OpenXR, no engine globals, no clock reads -
 * covered by tests/vr/HapticsTest.
 */
namespace vr {

//! What a pulse is for. Rows past ItemGrabbed are in the table but not wired yet (hooks for later tasks)
enum class HapticEvent : u8 {
	HitTaken,      //!< the player took damage - both hands, stronger with the damage
	WeaponHit,     //!< the player's weapon hit a creature - the weapon hand
	ItemGrabbed,   //!< an item was picked up to drag (world or inventory) - the hand that clicked
	WeaponBlocked, //!< the weapon hit a wall or a parry (#39)
	PointerTick,   //!< the menu pointer moved onto another control (#49)
	RuneStroke,    //!< a rune was recognised while drawing (#43)
	SpellCast,     //!< a spell was cast (#43)
	ItemStashed,   //!< an item went into the inventory
	Count
};

const size_t HapticEventCount = size_t(HapticEvent::Count);

//! "hit_taken", "weapon_hit", ... - for the log, status.json and the test channel
const char * hapticEventName(HapticEvent event);

//! \return the event named by \ref hapticEventName, nothing for an unknown name
std::optional<HapticEvent> parseHapticEvent(std::string_view name);

//! Which hand an event goes to; Pointer and Weapon are resolved by the caller
enum class HapticHand : u8 {
	Left,
	Right,
	Both,
	Pointer, //!< the hand whose click / use / stash was the last one
	Weapon   //!< the hand that holds the weapon - the right one until the left-handed setting (#178)
};

//! One vibration
struct HapticPulse {
	float amplitude = 0.f; //!< 0 to 1
	float seconds = 0.f;
	float frequency = 0.f; //!< Hz; 0 lets the runtime choose

	bool empty() const { return amplitude <= 0.f || seconds <= 0.f; }
};

// The MVP table (D-034). HitTaken: amplitude = base + perLife x (damage / max life), clamped
constexpr float HitTakenBase = 0.3f;
constexpr float HitTakenPerLife = 1.5f;
constexpr float HitTakenMax = 0.9f;
constexpr float HitTakenSeconds = 0.2f;
constexpr float HitTakenHz = 100.f;
constexpr float WeaponHitAmplitude = 0.5f;
constexpr float WeaponHitSeconds = 0.15f;
constexpr float WeaponHitHz = 100.f;
constexpr float ItemGrabbedAmplitude = 0.25f;
constexpr float ItemGrabbedSeconds = 0.05f;
constexpr float ItemGrabbedHz = 160.f;
constexpr float WeaponBlockedAmplitude = 0.3f;
constexpr float WeaponBlockedSeconds = 0.1f;
constexpr float PointerTickAmplitude = 0.1f;
constexpr float PointerTickSeconds = 0.02f;
constexpr float RuneStrokeAmplitude = 0.2f;
constexpr float RuneStrokeSeconds = 0.04f;
constexpr float SpellCastAmplitude = 0.5f;
constexpr float SpellCastSeconds = 0.15f;
constexpr float ItemStashedAmplitude = 0.15f;
constexpr float ItemStashedSeconds = 0.03f;

//! The master strength (cfg \c vr_haptic_strength, the setting comes with #178): 0 is off
constexpr float DefaultHapticStrength = 1.f;

//! The hand the table sends an event to
HapticHand hapticHand(HapticEvent event);

/*!
 * \brief The table: the pulse for an event at master strength 1
 *
 * \param intensity the event's own strength term in 0 to 1 - for HitTaken the damage over the
 *                  player's max life; the other events ignore it
 */
HapticPulse hapticPulse(HapticEvent event, float intensity = 0.f);

//! Per hand: what to send in one frame
class HapticMixer {

public:

	//! The master strength: amplitudes are multiplied by it and cut at 1; 0 or less sends nothing
	float strength = DefaultHapticStrength;

	/*!
	 * Queue a pulse for a hand. Of the requests of one frame the strongest wins (the longer one on a tie).
	 * \return false if it was refused at once: master strength off or an empty pulse
	 */
	bool request(Hand hand, HapticPulse pulse);

	/*!
	 * \brief The pulses to send now, once per frame
	 *
	 * A queued pulse weaker than the one still playing on that hand is dropped; anything else is
	 * sent and plays until \a now + its duration.
	 * \param now seconds on any steady clock
	 */
	std::array<std::optional<HapticPulse>, HandCount> take(double now);

	//! Forget the queue and what is playing (the session lost focus; the runtime stops the pulses)
	void clear();

	//! Requests dropped because a stronger pulse was still playing
	u64 masked() const { return m_masked; }

	//! Requests merged into a stronger one of the same frame
	u64 merged() const { return m_merged; }

private:

	std::array<std::optional<HapticPulse>, HandCount> m_pending;
	std::array<HapticPulse, HandCount> m_playing;
	std::array<double, HandCount> m_playingUntil = { };
	u64 m_masked = 0;
	u64 m_merged = 0;

};

} // namespace vr

#endif // ARX_VR_HAPTICS_H
