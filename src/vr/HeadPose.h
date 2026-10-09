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

#ifndef ARX_VR_HEADPOSE_H
#define ARX_VR_HEADPOSE_H

#include <glm/gtc/quaternion.hpp>

#include "math/Angle.h"
#include "math/Types.h"

/*!
 * \brief The head pose → the engine camera (#24, D-018)
 *
 * Pure math, no engine globals and no OpenXR: a head pose in the OpenXR LOCAL space becomes the
 * first-person camera's angles and an offset from the character's eye. Host-tested in
 * tests/vr/HeadPoseTest.cpp.
 *
 * Conventions:
 *  - OpenXR LOCAL: meters, gravity-aligned, x right, y up, z back (the head looks along -z).
 *  - Engine: game units, y \b down; a camera with \c Anglef(pitch, yaw, roll) in degrees has the
 *    world-to-view rotation \c toRotationMatrix(angle) = Rz(-roll) Rx(pitch) Ry(yaw), its view
 *    looks along +z with y down; positive pitch looks down, a larger yaw turns left.
 *
 * Rules from the reference mods: the camera is the tracked head with no neck model (F-37); pitch
 * and roll are the head's absolute ones because LOCAL is gravity-aligned, only the yaw needs a
 * reference (F-120 - subtracting a reference pitch or roll tilts the world); the pose is applied
 * after the engine's own camera motion, which the comfort policy turns off in VR (F-31, #76).
 */
namespace vr {

//! A head pose in the OpenXR LOCAL space
struct HeadSample {
	Vec3f position = Vec3f(0.f); //!< meters
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< head to LOCAL
};

/*!
 * \return the head between the two eyes of one located frame: the midpoint of the eye positions,
 *         the orientation halfway between them - the same sample both eyes render with (F-21)
 */
HeadSample headFromEyes(const Vec3f & leftPosition, const glm::quat & leftOrientation,
                        const Vec3f & rightPosition, const glm::quat & rightOrientation);

/*!
 * \brief A head orientation in the OpenXR LOCAL space as engine camera angles
 *
 * Exact: \c toRotationMatrix() of the result is the head's rotation in engine axes. Pitch is in
 * [-90, 90], yaw and roll in (-180, 180]. Looking straight up or down (where yaw and roll are the
 * same turn) the roll is 0 and the yaw carries the whole turn.
 */
Anglef headAngles(const glm::quat & orientation);

//! What the head pose is measured against: the head's yaw and position when the reference was taken
struct HeadReference {
	float yaw = 0.f;             //!< engine degrees, the head's heading only (F-120)
	Vec3f position = Vec3f(0.f); //!< meters, OpenXR LOCAL
};

//! \return the reference for the head as it is now: looking this way = the character's facing
HeadReference makeHeadReference(const HeadSample & head);

//! The first-person camera driven by the head
struct HeadCamera {
	Anglef angle;              //!< engine degrees, each wrapped to [0, 360) like the engine's angles
	Vec3f offset = Vec3f(0.f); //!< game units from the character's eye to the head
	bool clamped = false;      //!< the horizontal offset was longer than the lean limit and was shortened
};

//! How the head moves the camera
struct HeadCameraParams {
	float unitsPerMeter = 100.f; //!< world scale (#27 calibrates it)
	float maxLeanMeters = 0.6f;  //!< interim horizontal limit until the body follows the head (#26)
};

/*!
 * \brief The camera for a head pose
 *
 * The LOCAL space is turned about the vertical so that the reference heading is the body's yaw:
 * camera yaw = body yaw + head yaw - reference yaw, pitch and roll are the head's own. The offset is
 * the head's movement since the reference, turned the same way, in game units: head 20 cm lower -
 * camera 20 cm lower (D-018); a horizontal offset beyond \c maxLeanMeters is shortened to it.
 *
 * \param bodyYaw the character's yaw in engine degrees (what the stick and the mouse turn)
 */
HeadCamera headToCamera(const HeadSample & head, const HeadReference & reference, float bodyYaw,
                        const HeadCameraParams & params = HeadCameraParams());

/*!
 * \brief The camera field of view that fills a head-locked quad one to one
 *
 * While the flat frame is shown on a quad in front of the eyes (until #19 renders per eye), the
 * world seen through the quad stays where the head-tracked camera put it only if the camera sees
 * exactly the angle the quad covers. The quad has the frame's aspect ratio, so matching the
 * vertical angle matches the horizontal one too (the engine's projection keeps the vertical FOV).
 *
 * \param quadWidth the quad's width in meters
 * \param distance the quad's distance from the eyes in meters
 * \param frameSize the size of the frame shown on it (pixels)
 * \return the vertical field of view in radians
 */
float headLockedQuadFov(float quadWidth, float distance, const Vec2i & frameSize);

//! \return \a degrees wrapped to [0, 360)
float wrapDegrees(float degrees);

/*!
 * \return the head orientation for yaw, pitch and roll in degrees as a person would say them - yaw
 *         left, pitch up and roll left (counter-clockwise as seen from behind) positive, applied
 *         in that order - for the test channel's simulated head and the tests
 */
glm::quat headOrientationFromDegrees(float yaw, float pitch, float roll);

} // namespace vr

#endif // ARX_VR_HEADPOSE_H
