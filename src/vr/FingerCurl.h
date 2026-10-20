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

#ifndef ARX_VR_FINGERCURL_H
#define ARX_VR_FINGERCURL_H

#include <array>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief The finger curl of the VR body (#305, D-041): pure math, host-tested in tests/vr/FingerCurlTest.cpp
 *
 * No engine globals, no OpenXR, no clock. The body module (vr/VRBody.cpp) feeds it either the 26
 * hand joints of \c XR_EXT_hand_tracking (located by vr/HandTracking.cpp) or the controller's action
 * values, and bends the player model's finger groups by what comes back. The Arx hand has five
 * finger groups - the thumb (one segment), the index finger (two segments) and the middle, ring
 * and little finger as one block (two segments) - so a hand is three curls, each a bend in degrees
 * per segment about the knuckle it hangs on (research row F-218, section 16b).
 *
 * The joints give angles: the knuckle's flexion bends the root segment, the two outer joints
 * together bend the tip segment, the thumb's two joints together bend the thumb; each real angle
 * is scaled so the model's segment reaches its limit when the real finger reaches a fist. The
 * action values give fractions: the trigger is the index finger, the grip the three-finger block,
 * a thumb resting on a control a closed thumb. A hand on a weapon's grip is the closed hand.
 *
 * The custom VR hand (#385) has five fingers of three joints each: its FingerPose is read the same
 * way, one angle per joint, with the grip closing the middle, ring and little finger.
 */
namespace vr {

//! The joints of \c XR_EXT_hand_tracking in the spec's order (XrHandJointEXT), without the OpenXR header
enum class HandJoint : u8 {
	Palm,
	Wrist,
	ThumbMetacarpal,
	ThumbProximal,
	ThumbDistal,
	ThumbTip,
	IndexMetacarpal,
	IndexProximal,
	IndexIntermediate,
	IndexDistal,
	IndexTip,
	MiddleMetacarpal,
	MiddleProximal,
	MiddleIntermediate,
	MiddleDistal,
	MiddleTip,
	RingMetacarpal,
	RingProximal,
	RingIntermediate,
	RingDistal,
	RingTip,
	LittleMetacarpal,
	LittleProximal,
	LittleIntermediate,
	LittleDistal,
	LittleTip,
	Count
};

const size_t HandJointCount = size_t(HandJoint::Count); //!< 26, XR_HAND_JOINT_COUNT_EXT

//! Every joint's orientation the identity
inline std::array<glm::quat, HandJointCount> identityJointOrientations() {
	std::array<glm::quat, HandJointCount> orientations;
	orientations.fill(glm::quat(1.f, 0.f, 0.f, 0.f));
	return orientations;
}

/*!
 * \brief One hand's joints as located for a frame: positions and orientations
 *
 * The curl is read from the angles between the bones (the positions); the custom glove is posed by the
 * orientations (#385.7, vr/HandRig.h poseHandFromJoints). An orientation is the OpenXR joint frame
 * (XR_EXT_hand_tracking's "Conventions of hand joints"): -Z along the bone toward the fingertip, +Y on
 * the back of the hand (the thumb's: toward the thumb's side, away from the fingers), X completing a
 * right-handed frame; the palm and the wrist point -Z along the middle finger. A tip has its distal
 * bone's frame.
 */
struct HandJoints {
	bool valid = false; //!< every joint was located this frame
	std::array<Vec3f, HandJointCount> position = { }; //!< meters, all in one space
	//! the OpenXR joint frames, in the space of the positions (the identity until located)
	std::array<glm::quat, HandJointCount> orientation = identityJointOrientations();
	const Vec3f & operator[](HandJoint joint) const { return position[size_t(joint)]; }
	Vec3f & operator[](HandJoint joint) { return position[size_t(joint)]; }
};

//! How far each finger group of the model bends: degrees about the segment's base, 0 = straight
struct FingerCurl {
	float thumb = 0.f;       //!< the thumb's one segment
	float indexRoot = 0.f;   //!< the index finger on its knuckle
	float indexTip = 0.f;    //!< the index finger's outer segment on the root
	float fingersRoot = 0.f; //!< the three-finger block on its knuckles
	float fingersTip = 0.f;  //!< its outer segment on the root
};

/*!
 * \brief The bend of each segment at a closed fist, degrees: the model's limits
 *
 * Named here so they can be tuned from the headset in one place: the model's segments are short
 * (3.5 units), so a real fist's 160 degrees of outer flexion would fold the tip into the palm.
 */
struct FingerCurlLimits {
	float thumb = 60.f;
	float indexRoot = 90.f;
	float indexTip = 95.f;
	float fingersRoot = 90.f;
	float fingersTip = 95.f;
};

//! A real hand's flexion at a closed fist: the joint angles at which the model's segments reach their limits (degrees)
const float KnuckleFlexionAtFist = 90.f;  //!< the knuckle (metacarpophalangeal joint) of a finger
const float OuterFlexionAtFist = 160.f;   //!< a finger's two outer joints together
const float ThumbFlexionAtFist = 120.f;   //!< the thumb's two joints together

//! How fast the model's fingers may move towards what the tracking says (degrees per second): a fist in about 0.15 s
const float CurlDegreesPerSecond = 720.f;

/*!
 * \brief The curl of the model's finger groups from a located hand
 *
 * The flexion at a joint is the angle between the bone before it and the bone after it, so the
 * joints may be in any space. Invalid joints give an open hand. The three-finger block takes the
 * mean of the middle, ring and little finger.
 */
FingerCurl fingerCurlFromJoints(const HandJoints & joints, const FingerCurlLimits & limits = FingerCurlLimits());

/*!
 * \brief The curl from the controller's action values, for a runtime without hand joints
 *
 * \param trigger    0 - 1: how far the trigger is pulled - the index finger
 * \param grip       0 - 1: how far the hand is closed on the handle - the three-finger block
 * \param thumbTouch the thumb rests on a thumbstick, trackpad or button - a closed thumb
 */
FingerCurl fingerCurlFromControls(float trigger, float grip, bool thumbTouch,
                                  const FingerCurlLimits & limits = FingerCurlLimits());

//! The closed hand: every segment at its limit (a hand on a weapon's grip, D-041)
FingerCurl fingerCurlClosed(const FingerCurlLimits & limits = FingerCurlLimits());

//! \return \a current moved towards \a target by at most \a stepDegrees per segment
FingerCurl approachCurl(const FingerCurl & current, const FingerCurl & target, float stepDegrees);

//! The angle between two bone directions in degrees, 0 for a degenerate one
float flexionDegrees(const Vec3f & before, const Vec3f & after);

//! The five fingers of a real hand, in the order of \c XR_EXT_hand_tracking's joints
enum class Finger : u8 {
	Thumb,
	Index,
	Middle,
	Ring,
	Little,
	Count
};

const size_t FingerCount = size_t(Finger::Count); //!< 5

/*!
 * \brief How far each joint of a five-finger hand bends (#385): the custom VR hand's fingers
 *
 * Degrees, 0 = straight; \c flex[finger][joint] from the root: the thumb's metacarpal (carpometacarpal),
 * proximal and distal joint, a finger's knuckle (metacarpophalangeal), middle and last joint.
 * \c spread is the knuckle's sideways turn, signed (+ towards the thumb's side), 0 = in line with
 * its metacarpal; the thumb's spread stays 0 (its sideways motion is in its three flexions).
 */
struct FingerPose {
	std::array<std::array<float, 3>, FingerCount> flex = { };
	std::array<float, FingerCount> spread = { };
};

/*!
 * \brief The bend of each joint at a closed fist, degrees: the custom hand's limits (#385)
 *
 * A first proposal, tuned in the headset: a finger bends 90 at the knuckle, 100 at the middle
 * joint and 70 at the last one, the thumb 40, 50 and 70. A real angle past its limit is held there.
 */
struct FingerPoseLimits {
	std::array<std::array<float, 3>, FingerCount> flex = { {
		{ 40.f, 50.f, 70.f },
		{ 90.f, 100.f, 70.f },
		{ 90.f, 100.f, 70.f },
		{ 90.f, 100.f, 70.f },
		{ 90.f, 100.f, 70.f },
	} };
};

/*!
 * \brief The five-finger pose from a located hand
 *
 * The flexion at each joint is the angle between the bone before it and the bone after it (as
 * fingerCurlFromJoints reads it), so the joints may be in any space and either hand; each is held
 * within 0 and its limit. Invalid joints give an open hand.
 */
FingerPose fingerPoseFromJoints(const HandJoints & joints, const FingerPoseLimits & limits = FingerPoseLimits());

/*!
 * \brief The five-finger pose from the controller's action values, for a runtime without hand joints
 *
 * \param trigger    0 - 1: the index finger
 * \param grip       0 - 1: the middle, ring and little finger
 * \param thumbTouch the thumb rests on a control - a closed thumb
 */
FingerPose fingerPoseFromControls(float trigger, float grip, bool thumbTouch,
                                  const FingerPoseLimits & limits = FingerPoseLimits());

//! The closed hand: every joint of all five fingers at its limit (a hand on a weapon's grip)
FingerPose fingerPoseClosed(const FingerPoseLimits & limits = FingerPoseLimits());

//! \return \a current moved towards \a target by at most \a stepDegrees per joint and spread
FingerPose approachPose(const FingerPose & current, const FingerPose & target, float stepDegrees);

//! How far a hand is closed, per finger: 0 open, 1 a fist (the simulated runtime's script)
struct HandShape {
	float thumb = 0.f;
	float index = 0.f;
	float middle = 0.f;
	float ring = 0.f;
	float little = 0.f;
};

/*!
 * \brief A synthetic hand for the simulated runtime and the tests: the joints of a hand closed by \a shape
 *
 * Built in the OpenXR grip frame of \a hand (the palm centroid at the origin, the fingers along -Y,
 * the thumb towards -Z, the palm facing -X for the right hand and +X for the left, as the spec's
 * grip pose says) and placed by \a gripPosition / \a gripOrientation. A fully closed finger bends
 * 90 degrees at the knuckle, 100 at the middle joint and 70 at the last one; the thumb 40 at its
 * first joint (the metacarpal on the wrist: a real thumb closing into a fist flexes there too, #385.4), 50 and 70.
 * The orientations are the joints' OpenXR frames (orientHandJoints, #385.7).
 */
HandJoints synthesizeHandJoints(Hand hand, const HandShape & shape, const Vec3f & gripPosition = Vec3f(0.f),
                                const glm::quat & gripOrientation = glm::quat(1.f, 0.f, 0.f, 0.f));

/*!
 * \brief How far each finger of a located hand is closed, 0 - 1: the inverse of \ref synthesizeHandJoints (#550)
 *
 * Each joint's flexion (as fingerPoseFromJoints reads it) is placed between the synthetic open hand's and the
 * synthetic fist's at that joint, and a finger is the mean of its three joints, held within 0 and 1 - so the
 * joints a shape synthesizes read back as that shape, and a real hand's joints give the shape the simulated
 * runtime would synthesize them from (the replay recorder's five curls per hand). Invalid joints: an open hand.
 */
HandShape handShapeFromJoints(const HandJoints & joints);

/*!
 * \brief Every joint's OpenXR frame from the positions of \a joints (#385.7)
 *
 * \param along     the hand's direction from the wrist to the fingers (the palm's and the wrist's -Z)
 * \param back      out of the back of the hand (the palm's +Y)
 * \param thumbSide across the hand toward the thumb (the thumb joints' +Y)
 *
 * A finger joint's -Z runs along its bone to the next joint (a tip takes its distal's frame) and its X is
 * the hand's sideways axis (\a back x -\a along) made square to the bone - the axis a finger flexes about,
 * so a curled finger keeps its frame's roll; a thumb joint's +Y is \a thumbSide made square to its bone.
 * The three directions are in the space of the positions and need not be unit.
 */
void orientHandJoints(HandJoints & joints, const Vec3f & along, const Vec3f & back, const Vec3f & thumbSide);

} // namespace vr

#endif // ARX_VR_FINGERCURL_H
