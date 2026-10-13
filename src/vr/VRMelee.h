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

#ifndef ARX_VR_VRMELEE_H
#define ARX_VR_VRMELEE_H

#include <array>
#include <string>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/MeleeSwing.h"

/*!
 * \brief Physical melee (#39, D-038): the weapon in the IK hand hits what it is swung into
 *
 * While the IK hand holds a melee weapon (vr/VRWeapon.h), once per frame after the body was posed:
 *  - the right trigger charges the strike with the game's own charge - the time held against the
 *    player's \c Full_AimTime, shown on the HUD's hit strength gauge like the engine's (\c player.m_aimTime);
 *  - the right controller's motion is fed to the swing detector (vr/MeleeSwing.h): relative to the head,
 *    from the controller's LOCAL pose;
 *  - while a swing is in progress the weapon is placed where the hand holds it this frame and the engine's
 *    own strike check runs on it (\c ARX_EQUIPMENT_Strike_Check: the weapon's hit spheres, the to-hit roll,
 *    damage, armour, knockback, blood, sounds, durability, and #42's haptics) with the aim ratio of
 *    \ref strikeAim; targets hit by this swing or within the cooldown are left out (F-62);
 *  - the first contact of a charged swing spends the charge: the trigger must be released and pressed
 *    again to charge another;
 *  - after a contact the hand recovers for the weapon type's \ref recoveryTime (the game's own strike: the
 *    player's strike start and strike animations): a swing started within it hits nothing (#323);
 *  - a swing start is the engine's strike: the player becomes visible, the \c strike script event, and the
 *    strike speech of a nearly full charge.
 * The engine's own strike (the attack button's charge-and-release in combat mode, which hit without any
 * animation in VR) does not start while the IK hand holds the weapon (\ref meleeOwnsStrikes).
 *
 * Gates (F-66): a weapon in the IK hand, the right controller tracked, a head, no menu, inventory or book,
 * nothing dragged, the controls not blocked - the caller names what refuses (\ref updateMelee).
 * Thresholds: cfg.ini \c vr_melee_swing_speed, \c vr_melee_full_speed, \c vr_melee_weak_aim,
 * \c vr_melee_target_cooldown, \c vr_melee_recovery_dagger, \c vr_melee_recovery_1h, \c vr_melee_recovery_2h.
 */
namespace vr {

/*!
 * \brief Once per frame after \ref updateHeldWeapon
 * \param refusal why the game does not allow a swing now (a menu's name, "controls blocked", ...), or nullptr
 */
void updateMelee(const char * refusal);

/*!
 * \return true while the IK hand holds the weapon: the engine's combat mode must not start its own strike
 *         from the attack button (the physical swing is the strike)
 */
bool meleeOwnsStrikes();

//! Physical melee as status.json reports it
struct MeleeStatus {
	bool active = false;            //!< swings count now
	std::string refused;            //!< why not, when not
	SwingParams params;             //!< the thresholds in use
	// The swing detector
	bool swinging = false;
	const char * kind = "none";     //!< of the swing in progress, or of the last one
	float speed = 0.f;              //!< m/s, the lever point's filtered speed now
	float peak = 0.f;               //!< m/s, of the swing in progress or the last one
	float factor = 0.f;             //!< its speed factor
	u64 swings = 0;
	u64 gaps = 0;
	u64 jumps = 0;
	u64 nearMisses = 0;             //!< motions that peaked within 20 % below the swing speed (F-60)
	float lastNearMiss = 0.f;       //!< the peak of the last one
	// The charge (the game's own: the trigger held against Full_AimTime)
	bool chargeHeld = false;
	bool charging = false;
	bool chargeSpent = false;       //!< a charged hit used it; the trigger is still held
	float chargeSeconds = 0.f;
	float chargeRatio = 0.f;        //!< 0.1 .. 1 while charging, else 0
	float fullAimSeconds = 0.f;     //!< the player's Full_AimTime
	// Hits
	u64 hits = 0;                   //!< NPCs touched by a swing (an armour class miss included)
	u64 contacts = 0;               //!< anything touched (NPCs, objects)
	u64 chargedHits = 0;            //!< swings whose charge was spent on a hit
	std::string lastTarget;
	float lastAim = 0.f;            //!< the aim ratio of the last NPC hit
	float lastDamage = 0.f;
	bool lastCharged = false;
	float lastSpeed = 0.f;          //!< the swing's peak speed then
	u64 lastSwing = 0;              //!< which swing
	// The recovery after a hit (#323)
	const char * weapon = "none";   //!< the weapon type in the hand: dagger, 1h, 2h
	float recovery = 0.f;           //!< s, its recovery after a hit
	const char * recoverySource = "fallback"; //!< game, config, fallback
	float recoveryGame = 0.f;       //!< s, the game's own strike for the type (0: not known)
	std::array<float, 3> gameStrike = { }; //!< s, the game's own strike per \ref MeleeWeapon (0: not known)
	float recoveryLeft = 0.f;       //!< s until a new swing may hit
	bool recovering = false;        //!< the swing in progress (or the last one) started within the recovery: no hits
	u64 recoverySwings = 0;         //!< swings that started within the recovery
	// The weapon placed for the check
	bool placed = false;
	Vec3f hilt = Vec3f(0.f);
	Vec3f tip = Vec3f(0.f);
};

const MeleeStatus & meleeStatus();

} // namespace vr

#endif // ARX_VR_VRMELEE_H
