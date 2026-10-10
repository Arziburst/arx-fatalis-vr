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

#ifndef ARX_VR_VRWEAPON_H
#define ARX_VR_VRWEAPON_H

#include <string>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"

class Entity;

/*!
 * \brief The weapon in the IK hand of the full body (#75, D-009, D-NEW)
 *
 *  - While the IK drives the right arm (vr/VRBody.h), an equipped dagger, one- or two-handed weapon is
 *    linked to the model's right hand - outside the engine's combat mode too: VR has no draw or sheathe
 *    control (D-019: the weapon stays in the hand), the trigger stays a click and #39's swings hit. When
 *    the right arm goes back to the engine (no controller, flat mode) the weapon goes back to where the
 *    engine keeps it (the back, or the hand in combat mode). Bows are left to the engine.
 *  - The hilt sits on the palm attach point and the blade lies along the controller's handle (grip -Z,
 *    vr/WeaponGrip.h): the blade's axis is read from the weapon's mesh once per weapon and turned onto
 *    the hand's thumb axis about the attach point.
 *  - While the IK drives the arms, the engine's arm layer (animation layer 1: ready, unready, strike and
 *    cast clips) is not applied to the player's skeleton (F-67); its timing and game logic still run.
 */
namespace vr {

//! Once per frame after the player's animation and the combat mode's (ArxGame::updateLevel): the weapon's link
void updateHeldWeapon();

/*!
 * \brief The rotation the engine draws a linked object of the player with (AnimationRender.cpp)
 * \param linked     the linked entity
 * \param rotation   the hand bone's world rotation in, the held weapon's rotation out (bone * correction)
 * \return true when \a linked is the weapon held by the IK hand and \a rotation was changed
 */
bool heldWeaponRotation(const Entity * linked, glm::quat & rotation);

/*!
 * \return true when the player's arm animation layer must not be applied this frame: the IK drove the
 *         arms in the last frame (F-67). \a armAnimation is the clip the layer plays (or nullptr): counted
 *         and logged once per change
 */
bool suppressArmAnimation(const Entity * io, const std::string & armAnimation);

//! \return the weapon the IK hand held at the last \ref updateHeldWeapon, or nullptr
Entity * weaponInHand();

/*!
 * \brief Put the held weapon's vertices where the hand holds it now (#39)
 *
 * The engine places a linked object's vertices only when it draws it; a hit check in the update (melee,
 * #39) needs this frame's pose - after the body was posed - also for a weapon out of the eyes' view.
 * The same transform as the draw (the hand bone, the blade correction, the attach points).
 *
 * \param hilt, tip the weapon's attach vertex and blade tip in the world, when placed
 * \return false when no weapon is held by the IK hand or the link is gone
 */
bool placeHeldWeapon(Vec3f & hilt, Vec3f & tip);

//! The held weapon as status.json reports it
struct HeldWeaponStatus {
	std::string item;              //!< the equipped weapon's id, empty without one
	const char * kind = "none";    //!< dagger, 1h, 2h, bow, none
	const char * place = "none";   //!< hand: held by the IK hand; engine: where the engine keeps it; none
	bool inHand = false;           //!< linked to the right hand by this module
	u64 handFrames = 0;            //!< frames held by the IK hand since the start
	u64 relinks = 0;               //!< links changed by this module
	bool measured = false;         //!< the blade was read from the weapon's mesh
	float bladeLength = 0.f;       //!< units, from the attach vertex to the tip along the blade
	float assetAngle = 0.f;        //!< degrees: the asset's blade against the hand's thumb axis (the handle) in the model's fist
	float correction = 0.f;        //!< degrees the blade is turned by (= assetAngle)
	// After the weapon was drawn (its vertices in the world):
	bool drawn = false;            //!< the weapon's world vertices are from this frame's pose
	Vec3f hilt = Vec3f(0.f);       //!< the weapon's attach vertex (on the palm)
	Vec3f tip = Vec3f(0.f);        //!< the vertex farthest along the blade
	Vec3f bladeGrip = Vec3f(0.f);  //!< the drawn blade's direction in the controller's grip axes (OpenXR)
	float bladeAngle = 0.f;        //!< degrees between the drawn blade and the grip's -Z (the handle)
	// The engine's arm layer (F-67)
	bool armAnimSuppressed = false; //!< the layer was not applied in the last animated frame
	std::string armAnimation;       //!< the clip it plays (suppressed or not)
	u64 suppressedFrames = 0;       //!< frames the layer had a clip that was not applied
};

//! The status with the drawn weapon's landmarks read now
const HeldWeaponStatus & heldWeaponStatus();

} // namespace vr

#endif // ARX_VR_VRWEAPON_H
