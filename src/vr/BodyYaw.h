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

#ifndef ARX_VR_BODYYAW_H
#define ARX_VR_BODYYAW_H

/*!
 * \brief The body turns after the head (#25, D-009 / F-52)
 *
 * Pure math, no engine globals: the yaw the player model faces follows the head's yaw with a dead zone
 * and a relaxation, so a glance does not swing the whole body and looking somewhere for a while turns
 * it there (the opposite of locking the body to the head, F-168). Host-tested in tests/vr/BodyYawTest.cpp.
 *
 * Both yaws are engine degrees (a larger yaw turns left) measured from the play-space heading - the yaw
 * the stick and the mouse turn (\c player.angle) - so a snap or smooth turn turns the head and the body
 * together and only the head's own turn ever separates them.
 */
namespace vr {

//! How the body follows the head - the F-52 defaults; the owner judges them in the headset once the
//! body is visible (#36), the VR settings may expose them (#178)
struct BodyFollowParams {
	float deadZoneDegrees = 25.f; //!< the head never gets further than this from the body
	float settleSeconds = 1.5f;   //!< inside the dead zone the gap closes to 5% in this time
};

struct BodyFollow {
	float yaw = 0.f;      //!< the body's new yaw, in (-180, 180]
	bool dragged = false; //!< the head was beyond the dead zone and pulled the body along
};

//! \return \a degrees wrapped to (-180, 180]
float wrapSignedDegrees(float degrees);

/*!
 * \brief One step of the body after the head
 *
 * The gap between head and body shrinks exponentially (a time constant of a third of
 * \c settleSeconds); a gap still wider than the dead zone after that is cut to it at once.
 *
 * \param body the body's yaw before the step
 * \param head the head's yaw now
 * \param seconds the time since the last step (a negative time is none)
 */
BodyFollow followHead(float body, float head, float seconds,
                      const BodyFollowParams & params = BodyFollowParams());

} // namespace vr

#endif // ARX_VR_BODYYAW_H
