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

#ifndef ARX_VR_WEAPONGRIP_H
#define ARX_VR_WEAPONGRIP_H

#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"

/*!
 * \file
 * \brief How the weapon sits in the IK hand (#75, F-48): pure math, host-tested in tests/vr/WeaponGripTest.cpp
 *
 * No engine globals, no OpenXR: mesh vertices and rotations in, axes and rotations out. The held
 * weapon module (vr/VRWeapon.cpp) reads the weapon's mesh once per weapon and turns it in the hand.
 *
 * The rule (D-033, the coordinator for the owner): the controller's handle is the hilt - the blade
 * lies along the grip's -Z (OpenXR: little finger to thumb, out of the thumb side of the fist), the
 * hilt's attach point on the model's palm. The engine draws a weapon in the hand bone's rest frame,
 * so its own angle in the model's fist is whatever the asset gives; the correction turns the blade
 * onto the hand's thumb axis (which the IK puts on grip -Z) about the attach point.
 */
namespace vr {

//! The blade of a weapon mesh, relative to its attach vertex (the point that sits in the palm)
struct BladeAxis {
	bool valid = false;
	Vec3f axis = Vec3f(0.f, 0.f, 1.f); //!< unit: from the hilt towards the tip
	float length = 0.f;                //!< how far the mesh reaches along \c axis from the attach vertex
	size_t tip = 0;                    //!< the index of the vertex farthest along \c axis
};

/*!
 * \brief The blade's axis from the weapon mesh: its principal (longest) axis, pointing to the side the
 *        mesh reaches further on from the attach vertex - the tip, not the pommel
 * \param vertices the weapon's vertices relative to its attach vertex
 * \c valid is false for fewer than two distinct vertices.
 */
BladeAxis bladeAxisFromMesh(const std::vector<Vec3f> & vertices);

/*!
 * \return the rotation, in the hand bone's rest frame, that turns the weapon's blade \a blade onto the
 *         hand's thumb axis \a thumb (both unit, rest frame) by the shortest arc - applied before the
 *         hand bone's world rotation, about the attach point
 */
glm::quat bladeOntoHandle(const Vec3f & blade, const Vec3f & thumb);

//! \return the angle in degrees between two directions (any length; 0 for a zero vector)
float angleDegrees(const Vec3f & a, const Vec3f & b);

/*!
 * \return a world direction (engine axes) as the controller's grip sees it, in OpenXR grip axes
 *         (+X normal to the palm, -Z little finger to thumb): the hand bone's world rotation
 *         \a handWorld is the grip's world rotation (engine axes) times \a handToGrip (vr/ArmIK.h)
 */
Vec3f directionInGrip(const Vec3f & worldDirection, const glm::quat & handWorld, const glm::quat & handToGrip);

} // namespace vr

#endif // ARX_VR_WEAPONGRIP_H
