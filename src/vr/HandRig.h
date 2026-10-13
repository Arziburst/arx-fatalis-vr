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

#ifndef ARX_VR_HANDRIG_H
#define ARX_VR_HANDRIG_H

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/ArmMesh.h"
#include "vr/FingerCurl.h"

/*!
 * \file
 * \brief The five fingers of the custom glove hand (#385.4): pure, host-tested in tests/vr/HandRigTest.cpp
 *
 * No engine globals. The rig is read once from the glove's rest skeleton: the mesh joint of each
 * \c XR_EXT_hand_tracking joint, and per finger joint the axis it flexes about - across its bone,
 * so a positive angle curls the finger toward the palm (research rows F-235, F-282). \ref poseHand turns
 * a FingerPose (vr/FingerCurl.h) into the joints' local matrices, which the body module composes and
 * skins (vr/VRArms.cpp).
 *
 * What the fingers follow (D-041, D-042) is \ref customFingerTarget: a hand holding a weapon - either
 * hand - is closed on the grip, else the runtime's hand joints, else the controller's trigger, grip and
 * thumb rest.
 */
namespace vr {

//! The flexing joints of one finger: FingerPose::flex's three in order
const size_t FingerJointCount = 3;

//! The rig of a glove hand: which mesh joint is which, and the axes its fingers bend about
struct HandRig {
	std::array<int, HandJointCount> joint = { }; //!< the skeleton index per XrHandJointEXT joint
	/*!
	 * Per finger joint, [finger * 3 + joint] (the three joints of FingerPose::flex in order), the flexion
	 * axis in that joint's local rest frame: across the bone to the next joint and the palm's normal,
	 * signed so a positive angle curls toward the palm
	 */
	std::array<Vec3f, FingerCount * FingerJointCount> flexAxis = { };
	//! Per finger, in its first flexing joint's local rest frame: a positive angle turns it toward the thumb's side
	std::array<Vec3f, FingerCount> spreadAxis = { };
	bool valid = false;
};

//! The rig of \a hand; invalid when one of the 26 hand joints is missing or a finger has no usable bone
HandRig buildHandRig(const SkinnedMesh & hand);

//! The joint that FingerPose::flex[finger][joint] turns: the thumb's metacarpal, proximal and distal joint,
//! a finger's proximal (the knuckle), intermediate and distal joint
HandJoint flexingJoint(Finger finger, size_t joint);

/*!
 * \brief The joints' local matrices of \a hand posed by \a pose
 *
 * Every joint at its rest local transform, but a finger joint turned about its flexion axis by its angle
 * (the first one also about its spread axis by the finger's spread, then flexed in the turned frame).
 * An open pose gives the rest exactly. Compose \a local with the parents for world matrices.
 */
void poseHand(const SkinnedMesh & hand, const HandRig & rig, const FingerPose & pose, std::vector<glm::mat4> & local);

/*!
 * The angle the thumb's first joint (wrist, metacarpal, proximal) reads on an open hand, degrees: the bone from
 * the wrist to the metacarpal is not in line with the next one. The glove's thumb bends by what is past it.
 */
const float ThumbRestFlexion = 30.f;

//! What a custom hand's fingers follow (status.json vr.body.<hand>.custom.fingers.source)
enum class CustomFingerSource : u8 {
	None,     //!< the hand is not posed
	Joints,   //!< the runtime's 26 hand joints
	Controls, //!< the controller's trigger, grip and thumb rest (D-041)
	Held      //!< a weapon in the hand: closed on its grip (D-042: either hand)
};

const char * customFingerSourceName(CustomFingerSource source);

//! What one hand gives this frame
struct CustomFingerInput {
	bool holds = false;           //!< the hand holds a weapon
	bool controls = false;        //!< the controller's actions are read for this hand
	float trigger = 0.f;          //!< 0 - 1
	float grip = 0.f;             //!< 0 - 1
	bool thumbTouch = false;
};

//! The pose a custom hand's fingers head for and where it comes from
struct CustomFingerTarget {
	CustomFingerSource source = CustomFingerSource::None;
	FingerPose pose;
};

/*!
 * \brief The held rule and the finger sources of the custom hand, in order (D-041, D-042)
 *
 * A hand holding a weapon is closed (fingerPoseClosed), whatever its joints say; else valid \a joints
 * give their pose, the thumb's first joint less ThumbRestFlexion; else the controls give theirs; else none
 * (an open hand).
 */
CustomFingerTarget customFingerTarget(const CustomFingerInput & input, const HandJoints & joints,
                                      const FingerPoseLimits & limits = FingerPoseLimits());

} // namespace vr

#endif // ARX_VR_HANDRIG_H
