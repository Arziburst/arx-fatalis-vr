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
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
 *
 * Since #385.7 the game poses the glove joint by joint from the joints' OpenXR frames instead
 * (\ref poseHandFromJoints, chosen by \ref customHandTarget: the hilt pose, the runtime's joints, MRTK3's
 * Grab, host-tested in tests/vr/HandConformingTest.cpp and tests/vr/HandShapeTest.cpp); \ref poseHand and
 * \ref customFingerTarget remain the flexion model of #385.4.
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
	/*!
	 * Per XrHandJointEXT joint (#385.7): the glove's rest frame in the OpenXR joint convention, in mesh space -
	 * the orientation a runtime would report for a hand in the glove's rest (orientHandJoints on the rest
	 * positions: along the middle finger, the back of the hand opposite the palm, the thumb's side)
	 */
	std::array<glm::quat, HandJointCount> restFrame = identityJointOrientations();
	//! Per joint: the OpenXR frame to the glove joint's own rest rotation (restFrame^-1 x rest rotation)
	std::array<glm::quat, HandJointCount> frameToJoint = identityJointOrientations();
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

// The glove driven joint by joint (#385.7): the joints' orientations, the hilt pose, MRTK3's Grab

/*!
 * The Valve Index controller's handle about its grip pose, meters: the OpenXR grip is the handle's centre,
 * outside the hand (research #453 F-483), so the palm lies this far plus GlovePalmRadius behind it
 */
const float IndexHandleRadius = 0.018f;
//! The glove's palm joint to the skin of its palm, meters (with IndexHandleRadius: 3.2 cm, the first proposal)
const float GlovePalmRadius = 0.014f;
//! The hilt a held weapon closes the hand on, meters (no weapon has its own yet: research #453, 385.10)
const float DefaultHiltRadius = IndexHandleRadius;

/*!
 * \brief How far a hand on a hilt is closed, degrees (cfg.ini vr_hilt_knuckle / middle / last / thumb, live
 *        through the channel's `grip angles`)
 *
 * The four fingers bend at the knuckle, the middle and the last joint by these at DefaultHiltRadius (a
 * thicker hilt opens them in proportion, a thinner one never closes them further); the thumb's metacarpal
 * runs over the hilt and its two outer joints bend by \c thumb, laying its tip across the fingers. The
 * defaults keep finger-five.xrs' held asserts (knuckle > 60, middle > 70, last > 45); research #453 F-484
 * proposed 55 / 75 / 35. The owner tunes them in the headset.
 */
struct HiltAngles {
	float knuckle = 65.f;
	float middle = 78.f;
	float last = 48.f;
	float thumb = 45.f;
};

//! Each angle held within 0 and the fist limits (FingerPoseLimits: 90 / 100 / 70; the thumb 90)
HiltAngles clampHiltAngles(const HiltAngles & angles);

/*!
 * \brief The joint set a runtime would report for a hand in the glove's rest: the rest positions and
 *        \ref HandRig::restFrame (mesh space); invalid for an invalid rig
 *
 * \ref poseHandFromJoints gives the glove's rest locals for it.
 */
HandJoints restHandJoints(const SkinnedMesh & hand, const HandRig & rig);

/*!
 * \brief The glove's local joint matrices retargeted from the joints' orientations
 *
 * Every finger joint (the metacarpals to the tips) turns as its OpenXR frame relative to the joint set's palm
 * says: its world rotation is the glove palm's rest frame x (the joints' palm^-1 x the joint) x the joint's
 * rest correction (\ref HandRig::frameToJoint); the palm and the wrist stay at rest (they are placed on the
 * grip), and every joint keeps its rest translation (the glove's own bone lengths). So the joints may be in
 * any space; \ref restHandJoints reproduces the rest locals, and invalid joints or an invalid rig give the rest.
 */
void poseHandFromJoints(const SkinnedMesh & hand, const HandRig & rig, const HandJoints & joints,
                        std::vector<glm::mat4> & local);

/*!
 * \brief Read an MRTK3 hand-shape JSON into \a out (meters, valid)
 *
 * The layout of MRTK3's articulated hand poses (data/core/vr/hands/README.md): { "items": [ { "joint", "pose":
 * { "position": { x, y, z }, "rotation": { x, y, z, w } } } ... ] }, a joint named as MRTK3 names it ("Wrist",
 * "ThumbMetacarpal", ...) or by its XrHandJointEXT index; all 26, each once. The values are Unity's (left-handed,
 * y up): converted to OpenXR's right-handed space (z negated, a rotation's x and y negated). MRTK3 records its
 * poses for the right hand: \ref mirrorHandJoints makes the left one.
 *
 * \return false with a reason in \a error for anything else; \a out is then invalid
 */
bool loadHandShape(const char * json, size_t size, HandJoints & out, std::string & error);

//! \a joints as a hand-shape JSON that \ref loadHandShape reads back (joints by name, Unity's space)
std::string handShapeJson(const HandJoints & joints);

//! \a joints mirrored across their space's YZ plane: a right hand's joints become a left hand's (and back)
HandJoints mirrorHandJoints(const HandJoints & joints);

//! \a joints in their palm's frame: the palm at the origin with the identity orientation
HandJoints palmRelativeJoints(const HandJoints & joints);

/*!
 * \brief A closed hand around a hilt: \a hand's joints in its OpenXR grip frame, the hilt along the frame's Z
 *        through the origin, meters
 *
 * The palm lies behind the hilt (at +X for the right hand, -X for the left: the palm faces the hilt), the
 * fingers wrap it bent by \a angles at DefaultHiltRadius - opened in proportion for a thicker hilt, and opened
 * further where a fingertip would end inside it - and the thumb's tip lies across the index finger.
 * The hands mirror each other.
 */
HandJoints hiltPose(Hand hand, float hiltRadius, const HiltAngles & angles = HiltAngles());

/*!
 * The palm joint's position for a grip pose: the grip moved along the grip's X axis toward the palm by
 * \a handleRadius + \a palmRadius (OpenXR 1.0 12.38.6: the right palm faces -X, so its palm lies at +X from the
 * handle; the left one's at -X)
 */
Vec3f palmBehindGrip(Hand hand, const Vec3f & gripPosition, const glm::quat & gripOrientation, float handleRadius,
                     float palmRadius);

/*!
 * \brief Each finger closed from \a open toward \a closed by its own amount (0 - 1), the joints' frames slerped
 *        relative to the palm (both sets palm-relative, \ref palmRelativeJoints)
 *
 * \a amounts in Finger order. The positions follow the frames along \a open's bone lengths.
 */
HandJoints blendHandJoints(const HandJoints & open, const HandJoints & closed, const std::array<float, FingerCount> & amounts);

//! The line a closed glove's fingers wrap (#385.7): where the hilt runs through the hand
struct GloveCurl {
	bool valid = false;
	Vec3f centre = Vec3f(0.f); //!< mesh space: the mean centre of the index .. little finger's curls
	Vec3f axis = Vec3f(0.f);   //!< unit, from the little finger's side toward the index finger's
};

/*!
 * \brief The curl axis of \a hand posed by \a local (\ref poseHandFromJoints): for each of the index .. little finger
 *        the circle through its knuckle, its last joint and its tip, the centres and the circles' normals averaged
 *
 * Invalid when a finger is straight (its three points in a line) or \a rig is invalid.
 */
GloveCurl gloveCurlAxis(const SkinnedMesh & hand, const HandRig & rig, const std::vector<glm::mat4> & local);

/*!
 * \brief The glove's rest frame to the grip frame (\a toGrip, vr/ArmIK.h handToGrip) turned by the least rotation
 *        that lays \a curl's axis along the handle (the grip's Z: engine +z)
 *
 * The hand frame read from the glove's mesh leans off the handle (its long axis runs from the wrist to the glove's
 * off-centre Palm joint); a real hand on the handle wraps it, so the closed glove must too. \a toGrip itself for an
 * invalid \a curl.
 */
glm::quat gloveOnHandle(const GloveCurl & curl, const glm::quat & toGrip);

//! Where a held weapon's hilt pose comes from (status.json vr.hands.<hand>.hiltPose)
enum class HiltPoseSource : u8 {
	None,     //!< no weapon in the hand
	Default,  //!< \ref hiltPose with the configured angles
	Captured  //!< the conforming pose stored by the channel's `grip capture` (SteamVR's own grip, F-486)
};

const char * hiltPoseSourceName(HiltPoseSource source);

//! The poses one custom hand chooses from, palm-relative (\ref palmRelativeJoints)
struct CustomHandShapes {
	HandJoints open;     //!< the open hand (the synthetic one, vr/FingerCurl.h)
	HandJoints grab;     //!< MRTK3's Grab: the controls' closed end (invalid when its file did not load)
	HandJoints hilt;     //!< \ref hiltPose with the configured angles at DefaultHiltRadius
	HandJoints captured; //!< the captured hilt pose (invalid without one)
};

//! What a custom hand's glove is posed by this frame and where it comes from
struct CustomHandTarget {
	CustomFingerSource source = CustomFingerSource::None;
	HiltPoseSource hilt = HiltPoseSource::None;
	HandJoints joints;   //!< palm-relative; drives \ref poseHandFromJoints
	FingerPose pose;     //!< each joint's bend, for status.json (degrees)
};

/*!
 * \brief The custom hand's sources in order (D-041, D-042, #385.7)
 *
 * A held weapon: the captured hilt pose, else the default \ref hiltPose - the runtime's joints never open a hand
 * on a hilt; else valid \a runtime joints; else the controls (open toward Grab per finger: the index by the
 * trigger, the middle, ring and little finger by the grip, the thumb by its rest; open without Grab); else the
 * open hand. \c pose is read off the chosen joints - what the glove is posed by - as \ref customFingerTarget
 * reads joints (the thumb's first joint less ThumbRestFlexion).
 */
CustomHandTarget customHandTarget(const CustomFingerInput & input, const HandJoints & runtime,
                                  const CustomHandShapes & shapes, const FingerPoseLimits & limits = FingerPoseLimits());

} // namespace vr

#endif // ARX_VR_HANDRIG_H
