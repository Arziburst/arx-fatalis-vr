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

#ifndef ARX_VR_LOCOMOTION_H
#define ARX_VR_LOCOMOTION_H

#include "math/Types.h"
#include "vr/VRControls.h"

/*!
 * \brief Smooth stick locomotion: the left stick walks relative to where the player looks (#31, D-008)
 *
 * Pure math, no engine globals: the move vector of #74 (x right, y forward, past the dead zone) is
 * turned by the look yaw into a direction in the world, and into the engine's movement flags, which
 * pick the animation - and with it the speed: the engine moves the player at the speed of the walk /
 * run / strafe animation and uses the move vector only for the direction (\c ARX_PLAYER_Frame_Update),
 * so the stick keeps the game's own speeds. A deflection past the dead zone is a full step; partial
 * deflection as stealth is #33. Moving relative to the controller instead (F-169) is after the MVP.
 * Host-tested in tests/vr/LocomotionTest.cpp.
 */
namespace vr {

//! One frame of walking with the stick
struct StickWalk {
	Vec3f direction = Vec3f(0.f); //!< unit length in the XZ plane, or zero when the stick rests
	bool forward = false;         //!< the engine's PLAYER_MOVE_WALK_FORWARD ...
	bool backward = false;
	bool left = false;
	bool right = false;

	bool moving() const { return forward || backward || left || right; }
};

//! A stick direction this close to an axis (degrees) counts as that axis alone - eight directions, like the keys
const float StickWalkSectorDegrees = 22.5f;

/*!
 * \brief Where the stick walks
 * \param move the move vector after the dead zone (\ref GameInputState::move)
 * \param yaw the stick's forward, engine degrees (a larger yaw turns left): the look yaw (D-008)
 */
StickWalk stickWalk(Stick move, float yaw);

/*!
 * \brief The look yaw for the stick, from the heading of this frame
 *
 * The look of \ref bodyStatus is the heading plus the head's turn, taken when the camera was placed
 * (the end of the last frame); the heading may have turned since (the turn stick, the mouse). The head's
 * turn - look - (body - offset) - is added to \a heading so the walk follows a turn in the same frame.
 */
float stickWalkYaw(float heading, float lookYaw, float bodyYaw, float bodyOffset);

} // namespace vr

#endif // ARX_VR_LOCOMOTION_H
