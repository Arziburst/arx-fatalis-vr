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

#ifndef ARX_VR_VRRUNES_H
#define ARX_VR_VRRUNES_H

#include <array>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/RuneTrail.h"
#include "vr/VRControls.h"

/*!
 * \brief The rune trail at the hand (#43, D-010, D-NEW)
 *
 * In flat mode the engine draws the magic flares where the mouse cursor is, projected 75 units in
 * front of the camera. In VR the hand draws: while the casting stance is on (the magic button held,
 * D-019) the trigger of a hand draws a stroke (vr/RuneTrail.h), and the engine's magic
 * (\c ARX_SPELLS_ManageMagic) puts its own flares - look, colours, lifetime unchanged - at the point
 * ahead of that hand's grip instead of at the cursor, filling the path between frames. No cursor, no
 * ray, no projection onto a plane is shown (D-010). What the engine recognises is not changed here:
 * its points still come from the cursor (#44 feeds the stroke into the recogniser).
 */
namespace vr {

//! The size of a trail flare in VR, relative to the engine's flare at the cursor (D-NEW)
const float RuneFlareScale = 0.5f;

//! The spacing of the flares that fill the trail between two frames (units = cm)
const float RuneTrailStep = 1.2f;

/*!
 * \return true while the hands draw the runes: a VR session runs, or a controller is simulated (the
 *         test channel's \c handpose) - the engine's trail at the cursor is then not drawn (D-010)
 */
bool handsDrawRunes();

/*!
 * \brief Update the stroke - once per frame from the engine's magic, after the camera was placed
 *
 * \param stance     the engine's casting stance: the magic action held and casting allowed
 * \param headCamera the head camera in the world this frame (units)
 * \param bodyYaw    the character's yaw (engine degrees) the head is placed on
 */
const RuneStroke & updateRuneTrail(bool stance, const Vec3f & headCamera, float bodyYaw);

//! The stroke of the last update
const RuneStroke & runeStroke();

//! The engine drew \a count flares of the trail, the last one at \a point - for status.json
void noteRuneFlares(size_t count, const Vec3f & point);

//! The rune trail as status.json reports it
struct RuneTrailStatus {
	bool handsDraw = false;   //!< the hands draw the runes (\ref handsDrawRunes)
	bool stance = false;      //!< the casting stance was on in the last update
	bool drawing = false;     //!< a stroke is drawn
	const char * hand = "none"; //!< the hand of the current or the last stroke
	u32 strokes = 0;          //!< strokes started since the game started
	float length = 0.f;       //!< the current or the last stroke's path (units)
	s64 durationMs = 0;       //!< and how long it was drawn
	const char * lastEnd = "none"; //!< how the last stroke ended: trigger, stance, lost
	Vec3f point = Vec3f(0.f); //!< where the trail is drawn now, or where the last stroke ended (units)
	u64 flares = 0;           //!< trail flares the engine drew at the hands since the game started
	Vec3f lastFlare = Vec3f(0.f); //!< where the last of them was put (units)
	std::array<bool, HandCount> located = { { false, false } }; //!< the hand's point was known in the last update
	std::array<Vec3f, HandCount> points = { };  //!< where each hand would draw (units)
	std::array<bool, HandCount> ignored = { { false, false } }; //!< a trigger waiting for its release
};

const RuneTrailStatus & runeTrailStatus();

} // namespace vr

#endif // ARX_VR_VRRUNES_H
