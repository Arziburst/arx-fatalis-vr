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

#ifndef ARX_VR_HEADLEAN_H
#define ARX_VR_HEADLEAN_H

#include "math/Types.h"

/*!
 * \brief Leaning and real steps: the body follows the head (#26, D-018)
 *
 * Pure math, no engine globals: host-tested in tests/vr/HeadLeanTest.cpp.
 *
 * The head's horizontal movement since the reference (meters, in the reference frame: engine axes
 * x right, z forward of the play space before the body's yaw turns it, see \ref headToCamera) is
 * split in two: the part the body (the collision cylinder) already took - the \e anchor - and the
 * rest, the \e lean, which the camera adds to the character's eye. While the head is within the
 * follow radius of the body, the body stays where it is (a lean, a glance); beyond it the body is
 * asked to step so the head is back at the radius. The engine moves the body through its collision
 * and reports how far it really got (\ref anchorAfterStep): blocked by a wall the body stops and the
 * head goes on - the lean grows, and the wall check fades the screen once the head is in the wall.
 *
 * The anchor lives in the reference frame, so turning the body's yaw (the stick) turns only the lean
 * about the body, never the distance walked in the room.
 */
namespace vr {

//! How the body follows the head and when a head too far from it fades the screen (cfg.ini, #26)
struct LeanParams {
	float followRadius = 0.10f; //!< meters: the head may be this far from the body before it steps (vr_body_follow_radius)
	float farStart = 0.7f;      //!< meters: a head this far from the body starts to fade the screen (vr_head_far_start)
	float farFull = 1.0f;       //!< meters: ... and is black from here on (vr_head_far_full)
};

struct HeadLean {
	Vec2f lean = Vec2f(0.f); //!< meters, the head from the body (reference frame): what the camera adds
	Vec2f step = Vec2f(0.f); //!< meters, the step the body is asked to take (reference frame); 0 inside the radius
	float distance = 0.f;    //!< meters, the length of \c lean
	float farFade = 0.f;     //!< 0..1: how much a head this far from the body fades the screen
};

/*!
 * \brief Where the head is against the body, and the step that brings the body after it
 *
 * \param moved the head's horizontal movement since the reference, meters (reference frame)
 * \param anchor the body's movement since the reference, meters (reference frame)
 */
HeadLean leanHead(const Vec2f & moved, const Vec2f & anchor, const LeanParams & params = LeanParams());

/*!
 * \return \a v (x right, z forward) of the reference frame turned into the world by \a turnDegrees
 *         about the vertical - the same turn \ref headToCamera applies to the head's movement
 */
Vec2f turnToWorld(const Vec2f & v, float turnDegrees);

//! \return the inverse of \ref turnToWorld
Vec2f turnFromWorld(const Vec2f & v, float turnDegrees);

/*!
 * \return the anchor after the body moved by \a movedWorld (game units, the world's x and z) - what
 *         the engine's collision really let it do, which can be less than the step, or a slide
 * \param turnDegrees the turn the step was asked with
 */
Vec2f anchorAfterStep(const Vec2f & anchor, const Vec2f & movedWorld, float turnDegrees, float unitsPerMeter);

/*!
 * \return the distance from \a p to the triangle \a a, \a b, \a c (its closest point, edges and
 *         corners included) - the wall check's near-plane test: the engine's own sphere test samples
 *         a polygon's corners and edge midpoints only and misses the middle of a large wall
 */
float distanceToTriangle(const Vec3f & p, const Vec3f & a, const Vec3f & b, const Vec3f & c);

//! How fast the wall fade goes to black and comes back (cfg.ini, #26)
struct FadeParams {
	float outSeconds = 0.15f; //!< 0 to black (vr_wall_fade_out); 0 = at once
	float inSeconds = 0.25f;  //!< black to clear (vr_wall_fade_in); 0 = at once
};

/*!
 * \return the fade after \a seconds moving from \a current towards \a target (both 0..1) at the
 *         rates of \a params: darker at the fade-out rate, lighter at the fade-in rate
 */
float stepFade(float current, float target, float seconds, const FadeParams & params = FadeParams());

} // namespace vr

#endif // ARX_VR_HEADLEAN_H
