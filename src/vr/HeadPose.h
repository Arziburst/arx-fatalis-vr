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
#include "vr/WorldScale.h"

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

/*!
 * \brief Auto-raise (the owner's calibration model, D-018): the head never sees from above the
 *        character's eye - a head higher than the reference lifts the reference to it
 *
 * The reference height is the character's eye; a player who recentered seated or crouched and then
 * stands up, or whose reference came from a headset still being lifted, would otherwise look from
 * above the character's head. Only the height moves; the heading and the horizontal position stay.
 *
 * \return how far the reference was raised, meters (0 when the head is not above it)
 */
float raiseHeadReference(HeadReference & reference, const HeadSample & head);

//! The first-person camera driven by the head
struct HeadCamera {
	Anglef angle;              //!< engine degrees, each wrapped to [0, 360) like the engine's angles
	Vec3f offset = Vec3f(0.f); //!< game units from the character's eye to the head
	Vec3f moved = Vec3f(0.f);  //!< meters, the head's movement since the reference, reference frame (engine axes)
};

//! How the head moves the camera
struct HeadCameraParams {
	float unitsPerMeter = UnitsPerMeter; //!< world scale (WorldScale.h)
	//! The player's character height setting (#260): the head at the reference sees from this far above
	//! the character's eye, meters, up positive; already clamped (\ref clampHeightOffset)
	float heightOffsetMeters = 0.f;
};

//! The character height setting's range and step (#260): +-20 cm in 2 cm steps, 0 = the character's own eye
constexpr float MaxHeightOffset = 0.2f;
constexpr float HeightOffsetStep = 0.02f;

//! \return \a meters within +-\ref MaxHeightOffset (0 for a value that is not a number)
float clampHeightOffset(float meters);

/*!
 * \brief The camera for a head pose
 *
 * The LOCAL space is turned about the vertical so that the reference heading is the body's yaw:
 * camera yaw = body yaw + head yaw - reference yaw, pitch and roll are the head's own. The offset is
 * the head's movement since the reference, turned the same way, in game units: head 20 cm lower -
 * camera 20 cm lower (D-018). The part of the horizontal movement the body already took (real steps,
 * #26, HeadLean.h) is the body's, not the camera's: \a anchor is taken off before the turn. The
 * character height setting lifts the whole offset by \c heightOffsetMeters (#260).
 *
 * \param bodyYaw the character's yaw in engine degrees (what the stick and the mouse turn)
 * \param anchor the body's movement since the reference, meters, reference frame (x right, z forward)
 */
HeadCamera headToCamera(const HeadSample & head, const HeadReference & reference, float bodyYaw,
                        const Vec2f & anchor = Vec2f(0.f),
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

//! A controller's grip pose in the OpenXR LOCAL space (#36)
struct HandSample {
	Vec3f position = Vec3f(0.f); //!< meters
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< grip to LOCAL
};

//! A hand in the engine world, rigid with the head camera
struct HandPlacement {
	Vec3f offset = Vec3f(0.f); //!< game units from the head camera to the grip
	//! the grip's rotation in engine axes: turns a grip-frame vector given in engine axes (OpenXR
	//! +X, +Y, +Z = engine +X, -Y, -Z) into the world
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

/*!
 * \brief Where a hand is, rigid with the head (#36)
 *
 * The hand's position relative to the head is turned the way \ref headToCamera turns the head's
 * movement (the LOCAL space put on the body's yaw by the reference) and scaled to game units; its
 * orientation gets the same turn. So a hand 30 cm right of the head is 30 units right of the camera
 * whatever the body walked after the head (#26), and a hand oriented like the head has the camera's
 * rotation. Hands at the character's eye height look right as soon as the head does (#27).
 */
HandPlacement handToWorld(const HandSample & hand, const HeadSample & head, const HeadReference & reference,
                          float bodyYaw, const HeadCameraParams & params = HeadCameraParams());

//! \return a vector in OpenXR axes (y up, z back) as the same vector in engine axes (y down, z forward)
Vec3f toEngineAxes(const Vec3f & v);

//! \return a vector in engine axes turned about the vertical by \a turnDegrees the way \ref headToCamera turns the head's movement
Vec3f turnAboutVertical(const Vec3f & v, float turnDegrees);

/*!
 * \return a rotation given in OpenXR axes as the same rotation in engine axes, turned about the
 *         vertical by \a turnDegrees: the quaternion \c toRotationMatrix(headAngles(q) + turn) is the
 *         inverse of
 */
glm::quat orientationToEngine(const glm::quat & q, float turnDegrees);

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
