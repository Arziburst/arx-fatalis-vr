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

#ifndef ARX_VR_EYEPROJECTION_H
#define ARX_VR_EYEPROJECTION_H

#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"

/*!
 * \file
 * \brief The per-eye matrices of the stereo render (#19)
 *
 * Pure math, no engine globals and no OpenXR: the eye poses and fields of view a frame was located
 * with become the engine's per-eye world-to-view and view-to-clip matrices, plus the union frustum
 * both eyes are culled with (research row F-163). Host-tested in tests/vr/EyeProjectionTest.cpp.
 *
 * Conventions (the same as HeadPose.h, #24):
 *  - OpenXR LOCAL and view spaces: meters, x right, y up, z back (an eye looks along -z). An
 *    \c XrFovf holds four half-angles in radians, left and down negative.
 *  - Engine view space: game units, x right, y \b down, z forward. Clip space as
 *    \c createProjectionMatrix() in game/Camera.cpp makes it: depth 0 at the near plane and 1 at
 *    the far plane, clip w = view z.
 */
namespace vr {

//! Field of view half-angles in radians, OpenXR convention (left and down are negative)
struct EyeFov {
	float left = 0.f;
	float right = 0.f;
	float up = 0.f;
	float down = 0.f;
};

//! The tangents of the four half-angles: the frustum's extent at distance 1
struct FovTangents {
	float left = 0.f;
	float right = 0.f;
	float up = 0.f;
	float down = 0.f;
};

FovTangents tangents(const EyeFov & fov);

/*!
 * \brief The engine's projection for one eye, straight from its four FOV angles (F-115)
 *
 * Off-axis when the angles are asymmetric (a headset's eyes see more to the outside than to the
 * nose); for symmetric angles it is exactly the engine's own matrix for that vertical FOV and
 * aspect. The runtime's angles go into the matrix unchanged, so the picture matches the FOV the
 * frame is submitted with - no symmetric cover, no cropped lens.
 *
 * \param nearDist near clip distance in game units (\ref NearClipMeters in VR, F-39)
 * \param farDist  far clip distance in game units (the camera's \c cdepth)
 */
glm::mat4x4 eyeProjection(const EyeFov & fov, float nearDist, float farDist);

/*!
 * \return the symmetric vertical field of view (radians) with the same vertical extent as \a fov -
 *         what \c Camera::setFov() needs so the engine's sprite and fog scaling, which read the
 *         focal length, match the eye's projection
 */
float verticalFov(const EyeFov & fov);

//! \return the symmetric horizontal field of view (radians) with the same horizontal extent as \a fov
float horizontalFov(const EyeFov & fov);

//! One eye as located for a frame in the OpenXR LOCAL space
struct LocatedEye {
	Vec3f position = Vec3f(0.f);                            //!< meters
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< eye to LOCAL
	EyeFov fov;
};

/*!
 * \brief One eye relative to the head, in engine view axes
 *
 * The head is the point between the eyes with the orientation halfway between them - the same head
 * the camera takes (#24, HeadPose.h \c headFromEyes). Relative to it an eye is an offset (half the
 * eye separation, in game units) and a rotation: identity for parallel displays, the cant for
 * canted ones (the Index, the Quest 3 - F-115).
 */
struct EyeRig {
	Vec3f offset = Vec3f(0.f);                            //!< the eye in the head's view space, game units
	glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);  //!< the eye's view axes in the head's view axes
	EyeFov fov;
};

//! How the located eyes become game-unit offsets
struct EyeRigParams {
	float unitsPerMeter = 100.f; //!< world scale, interim until #27 calibrates it (HeadCameraParams agrees)
};

//! \return the rig of both eyes (left, right) for one located frame
std::array<EyeRig, 2> eyeRig(const LocatedEye & left, const LocatedEye & right,
                             const EyeRigParams & params = EyeRigParams());

//! \return the distance between the two eyes of a rig in meters
float eyeSeparationMeters(const std::array<EyeRig, 2> & eyes, const EyeRigParams & params = EyeRigParams());

//! \return the angle in radians between the two eyes' view directions: 0 for parallel displays
float cantAngle(const std::array<EyeRig, 2> & eyes);

/*!
 * \return the eye's world-to-view matrix from the head camera's world-to-view (the engine's
 *         \c toRotationMatrix(angle) translated by \c -position, see \c cameraWorldToView())
 */
glm::mat4x4 eyeWorldToView(const glm::mat4x4 & headWorldToView, const EyeRig & eye);

//! \return the eye's position in the world, from the head camera's world-to-view and the rig
Vec3f eyeWorldPosition(const glm::mat4x4 & headWorldToView, const EyeRig & eye);

/*!
 * \brief The frustum that contains both eyes' frusta, in the head's view axes (F-163)
 *
 * The corners of each eye's frustum at distance 1, turned by the eye's rotation into the head's
 * axes, bound the union; for parallel eyes with the same FOV it is that FOV. Culling once with it
 * (rooms, portals, entities) sees everything either eye sees, so the visibility work is done once
 * per frame and each eye only draws.
 */
EyeFov unionFov(const std::array<EyeRig, 2> & eyes);

//! The near clip plane in VR: hands and a raised weapon must not clip at the face (F-39)
constexpr float NearClipMeters = 0.09f;

} // namespace vr

#endif // ARX_VR_EYEPROJECTION_H
