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

#ifndef ARX_VR_ARMIK_H
#define ARX_VR_ARMIK_H

#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"

/*!
 * \file
 * \brief The arm IK of the full body (#36, D-009): pure math, host-tested in tests/vr/ArmIKTest.cpp
 *
 * No engine globals, no OpenXR, no clock: joints in and joints out. The body module (vr/VRBody.cpp)
 * feeds it the player model's shoulder and the controller's wrist target each frame and writes the
 * result back into the engine's skeleton.
 *
 * The rules come from Dishonored VR's arm IK (research row F-52, MIT): a two-bone chain from the
 * shoulder, the elbow bending towards a hint direction, and the shoulder sliding towards a target the
 * arm cannot reach instead of the hand stopping short in the air.
 */
namespace vr {

/*!
 * \return the rotation that turns direction \a from onto direction \a to by the shortest arc; the
 *         identity for parallel directions, a half turn about a perpendicular axis for opposite ones
 */
glm::quat rotationBetween(const Vec3f & from, const Vec3f & to);

//! What the solver may do beyond bending the elbow
struct ArmIKParams {
	//! How far (units) the shoulder may slide towards a target the straight arm cannot reach (F-52)
	float maxShoulderSlide = 0.f;
};

//! The posed arm
struct ArmPose {
	Vec3f shoulder = Vec3f(0.f); //!< the shoulder joint used: the given one, slid when the target was out of reach
	Vec3f elbow = Vec3f(0.f);
	Vec3f wrist = Vec3f(0.f);
	float reach = 0.f;           //!< distance from the given shoulder to the target
	float slide = 0.f;           //!< how far the shoulder slid towards the target
	bool stretched = false;      //!< the target is beyond the straight arm: the arm points at it, the wrist short of it
	float error = 0.f;           //!< distance from the wrist to the target (0 when the target is reached)
};

/*!
 * \brief Two-bone IK: put the wrist on the target with the elbow on the hint's side
 *
 * The elbow lies in the plane of the shoulder, the target and \a elbowHint, on the hint's side of
 * the shoulder-target line (law of cosines on the two bone lengths). A target beyond the straight
 * arm first slides the shoulder towards it by up to \c params.maxShoulderSlide, then stretches the
 * arm straight at it; a target closer than the chain can fold is pushed out to the fold.
 *
 * \param shoulder      the shoulder joint
 * \param target        where the wrist should be
 * \param upperLength   shoulder to elbow (> 0)
 * \param forearmLength elbow to wrist (> 0)
 * \param elbowHint     a direction (any length, not along the arm) the elbow bends towards
 */
ArmPose solveArm(const Vec3f & shoulder, const Vec3f & target, float upperLength, float forearmLength,
                 const Vec3f & elbowHint, const ArmIKParams & params = ArmIKParams());

/*!
 * \brief The hand's own axes, read from its mesh, in the OpenXR grip convention
 *
 * The grip pose of a controller (the OpenXR spec: the palm centroid; +X normal to the palm - into it
 * for the right hand, away from it for the left; -Z through the tube of the non-thumb fingers from
 * the little finger towards the thumb; +Y by the right-hand rule, so the knuckles lie on -Y) is
 * matched to the model's hand by the same three directions of the hand mesh at rest, relative to
 * the wrist (the hand bone's origin):
 *  - \c knuckles: the hand's long axis, from the wrist towards the fingers (grip -Y)
 *  - \c thumb: the lateral axis, towards the side the mesh extends further on - the thumb (grip -Z)
 *  - \c palm: \c knuckles x \c thumb (grip +X)
 * \c valid is false when the vertices are too few or too flat to tell the axes apart.
 */
struct HandFrame {
	bool valid = false;
	Vec3f knuckles = Vec3f(0.f, 0.f, 1.f);
	Vec3f thumb = Vec3f(0.f, 1.f, 0.f);
	Vec3f palm = Vec3f(1.f, 0.f, 0.f);
	float length = 0.f;      //!< the hand's extent along \c knuckles (units)
	float thumbExtent = 0.f; //!< how far the mesh reaches on the thumb side of the long axis
	float otherExtent = 0.f; //!< and on the little finger side
};

/*!
 * \param vertices  the hand mesh's vertices relative to the wrist, at rest
 * \param palmHint  the palm point relative to the wrist when the model names one (its attach vertex):
 *                  the long axis is then wrist -> palm instead of the mesh's principal axis
 * \param thumbHint a point of the thumb relative to the wrist when the model has a thumb group: the
 *                  lateral axis then points at it instead of at the wider side of the mesh
 */
HandFrame handFrameFromMesh(const std::vector<Vec3f> & vertices, const Vec3f * palmHint = nullptr,
                            const Vec3f * thumbHint = nullptr);

/*!
 * \return the rotation from the hand bone's rest frame to the controller's grip frame in engine axes
 *         (OpenXR's +X, +Y, +Z are the engine's +X, -Y, -Z: the palm lands on +x, the knuckles on
 *         grip -Y = engine +y, the thumb on grip -Z = engine +z), so that a vector of the hand mesh
 *         at rest turned by it lies as the grip frame says - the hand bone's world rotation is the
 *         grip's world rotation times this
 */
glm::quat handToGrip(const HandFrame & frame);

} // namespace vr

#endif // ARX_VR_ARMIK_H
