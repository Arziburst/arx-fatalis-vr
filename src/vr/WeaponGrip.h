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

#include <string_view>
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

/*
 * The weapon in the custom glove (#385.10, research #453 F-485 .. F-489): with a model that draws our glove the held
 * weapon is placed from the glove's hilt frame - a point of the handle on the line the closed fingers wrap, the
 * blade along that line, its edge toward the knuckles - never from the hidden Arx hand. Where along the handle the
 * fingers close depends on the weapon's class (the anchor table, VRWeapon.cpp reads the class from the item).
 */

//! The classes a held weapon is anchored by (F-487)
enum class WeaponClass { Sword, Dagger, Axe, Mace, TwoHanded };

const char * weaponClassName(WeaponClass weaponClass);

/*!
 * \return the class of an item: a dagger or a two-handed weapon by its type, a one-handed one by its weapon material
 *         (the script's setweaponmaterial, any case): axe an axe, club a mace, anything else a sword
 */
WeaponClass weaponClassOf(bool dagger, bool twoHanded, std::string_view material);

/*!
 * \brief Where the closed fingers sit on the handle: the distance from the pommel end of the handle, along the handle
 *        towards the guard (or the head), to the point placed on the glove's grip axis
 *
 * The anchor table (VRWeapon.cpp, research #453's first proposal): sword and dagger at the handle's centre, axe and
 * mace near the handle's end (one fraction of the handle in [0.15, 0.25]), two-handed a hand-width above the pommel
 * (at most the handle). Every result lies in (0, \a handleLength] for a handle longer than 0.
 */
float weaponAnchorOffset(WeaponClass weaponClass, float handleLength, float handWidth);

/*!
 * \return the rotation about \a blade (unit) that turns \a edge (unit, perpendicular to the blade: the sharp side)
 *         towards \a knuckles (any length: the glove's wrist to its knuckles): the turned edge lies on the knuckles'
 *         projection onto the plane perpendicular to the blade (F-489). Knuckles (nearly) along the blade: identity
 */
glm::quat bladeRollToKnuckles(const Vec3f & blade, const Vec3f & edge, const Vec3f & knuckles);

/*!
 * \brief The sword's forward tilt in the hand: the rotation by |\a degrees| about cross(\a handle, \a toward)
 *
 * \a handle and \a toward unit and perpendicular; a negative \a degrees leans the blade's tip towards \a toward
 * (0 is the identity). In the game \a toward is the glove's knuckle direction off the handle (the wrist to the
 * knuckles: forward, where the Index's pointing frame lies ~55 degrees from its handle, F-488), so the tilt turns
 * the blade about the palm's normal, in the plane of its edges.
 */
glm::quat swordTiltRotation(float degrees, const Vec3f & handle, const Vec3f & toward);

//! The glove's hilt frame in the world (vr/VRArms.h CustomArmStatus): what a held weapon is drawn from
struct GloveHilt {
	Vec3f centre = Vec3f(0.f);              //!< the hold point: where the closed fingers wrap the handle
	Vec3f axis = Vec3f(0.f, 0.f, 1.f);      //!< unit: the handle line, toward the thumb side (where the blade goes)
	Vec3f knuckles = Vec3f(0.f, 1.f, 0.f);  //!< unit: the glove's wrist to its middle knuckle
};

/*!
 * \return the rotation that draws a weapon mesh in the glove: its blade \a blade (unit) along \a hilt's axis, its
 *         edge \a edge (unit, perpendicular to the blade) toward the knuckles, then tilted by \a tiltDegrees
 *         (\ref swordTiltRotation) toward the knuckles off the handle - forward, in the plane of the edges
 */
glm::quat weaponInGlove(const Vec3f & blade, const Vec3f & edge, const GloveHilt & hilt, float tiltDegrees);

//! The sword tilt variants the owner picks from (cfg.ini vr_sword_tilt, the channel's `grip tilt <deg>`), degrees
const float SwordTiltVariants[] = { 0.f, -15.f, -30.f };

//! \return \a degrees snapped to the nearest of SwordTiltVariants (0 for a value that is not a number)
float swordTiltVariant(float degrees);

/*!
 * The handle of a weapon mesh along its blade axis (\ref bladeAxisFromMesh), relative to the attach vertex: from the
 * pommel end (the mesh's end behind the attach vertex) to the guard or the head - where the mesh first grows wider
 * than the handle towards the tip - and the edge axis of what lies past it
 */
struct WeaponHandle {
	bool valid = false;
	float pommel = 0.f;          //!< the pommel end's coordinate along the blade axis (<= 0: behind the attach vertex)
	float length = 0.f;          //!< from the pommel end to the guard, units (> 0 when valid)
	Vec3f centre = Vec3f(0.f);   //!< the handle's centre line off the attach vertex, perpendicular to the blade axis
	float radius = 0.f;          //!< the handle's radius (the mean distance of its vertices from its centre line)
	Vec3f edge = Vec3f(1.f, 0.f, 0.f); //!< unit, perpendicular to the blade: the widest side of the blade or the head
	bool guardFound = false;     //!< false: no vertex grows wider, the handle is taken as twice the attach's depth
};

WeaponHandle weaponHandleFromMesh(const std::vector<Vec3f> & vertices, const BladeAxis & blade);

} // namespace vr

#endif // ARX_VR_WEAPONGRIP_H
