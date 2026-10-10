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

#ifndef ARX_VR_PHYSICALCROUCH_H
#define ARX_VR_PHYSICALCROUCH_H

/*!
 * \brief Physical crouch (#100, D-018): the player's head going down crouches the character
 *
 * Pure math, no engine globals - host-tested in tests/vr/PhysicalCrouchTest.cpp.
 *
 * The head's depth is how far it is below the calibrated reference (the character's eye, D-018). The
 * character crouches once the head is \c enterMeters below it and stands again once it is back above
 * \c exitMeters (hysteresis, the owner's numbers: 30 and 20 cm). A seated player leaning forward that far
 * crouches too - accepted by the owner. The crouch button (D-019) works alongside: either crouches.
 */
namespace vr {

constexpr bool DefaultPhysicalCrouch = true;
constexpr float DefaultCrouchEnter = 0.30f;
constexpr float DefaultCrouchExit = 0.20f;

//! \c vr_physical_crouch, \c vr_crouch_enter and \c vr_crouch_exit in cfg.ini (the settings of #178)
struct CrouchParams {
	bool enabled = DefaultPhysicalCrouch;
	float enterMeters = DefaultCrouchEnter; //!< the head this far below the reference crouches
	float exitMeters = DefaultCrouchExit;   //!< and stands up again above this depth (<= enterMeters)
};

/*!
 * \param crouched whether the head crouched the character in the last frame
 * \param depthMeters the head below its reference, meters (negative above it)
 * \return whether the head crouches the character now
 */
bool updatePhysicalCrouch(bool crouched, float depthMeters, const CrouchParams & params = CrouchParams());

/*!
 * \brief The head's drop that the camera still has to show while the character's eye is lowered
 *
 * The camera is the head (D-018), but the character's crouch already lowers the eye the camera starts
 * from: counting both would put the view twice as low. The character's own drop covers the head's drop
 * as far as it goes - the view is the head, but never above the character's eye.
 *
 * \param headDrop the head below its reference (game units, down positive)
 * \param characterDrop the character's eye below its standing eye (game units, 0 standing)
 * \return the vertical camera offset from the character's eye (game units, down positive)
 */
float headDropOnCamera(float headDrop, float characterDrop);

} // namespace vr

#endif // ARX_VR_PHYSICALCROUCH_H
