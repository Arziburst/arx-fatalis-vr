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

#ifndef ARX_VR_VRBODY_H
#define ARX_VR_VRBODY_H

#include <array>
#include <string>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/HeadPose.h"
#include "vr/VRControls.h"

class Entity;
struct EERIE_3DOBJ;
struct Skeleton;

/*!
 * \brief The full body in VR (#36, D-009): the player model under the tracked head, arms from the controllers
 *
 * The body is the engine's own player model on its own skeleton: legs, torso and head keep the
 * engine's animations, the head stays hidden by the engine's first-person selection. Once per
 * frame, after the engine has animated the skeleton and before the vertices are transformed
 * (\ref posePlayerBody, called from the animation code for the player's own skeleton), this module
 *  - draws the model under the tracked head: the head's horizontal movement since the reference
 *    (the lean, as \ref headToCamera turns and clamps it) shifts every bone, so looking down shows
 *    the body under you (#26 later moves the real player there; the camera takes the shift off
 *    again, \ref bodyShift);
 *  - poses both arms with a two-bone IK (vr/ArmIK.h) towards the controllers' grip poses, located
 *    for this frame in the same space as the eyes (F-21) and placed rigid with the head camera
 *    (\ref handToWorld). The model's palm attach point lands on the grip, the hand bone takes the
 *    controller's orientation through the hand frame read from the mesh (\ref handFrameFromMesh);
 *    a target beyond the arm slides the shoulder a little and then stretches the arm at it (F-52).
 * The arm chains are found from the model itself - the group holding the palm attach vertex, its
 * father, and its father's father - so no group names are assumed; a model without them keeps
 * the engine's arms and says why in the log. A hand without a tracked controller keeps the engine's
 * animation for its arm.
 *
 * Two poses per hand (F-185): the controller (where the player's hand is) and the posed wrist
 * (where the model's hand is); both are in status.json with the error between them.
 */
namespace vr {

struct HandFrame;

/*!
 * \brief Shift the body under the head and pose the arms - after the engine's animation of the
 *        player's own skeleton, before its vertices are transformed
 *
 * Does nothing without a head (flat mode without a simulated one), before the head's reference
 * is taken, or when the model has no arm chains. Binds to \a eobj on first sight (and again when
 * the model changes - an armor tweak makes a new object).
 */
void posePlayerBody(EERIE_3DOBJ * eobj, Skeleton & skeleton, Entity * io);

//! The horizontal shift the body was drawn with this frame (units); the engine camera takes it off
Vec3f bodyShift();

/*!
 * \return true while a head drives the camera: the first-person view then hides only the head and
 *         the neck of the player model (D-009 - the torso stays, the engine's own first-person
 *         selection hides the chest too), see \ref ARX_INTERACTIVE_Show_Hide_1st
 */
bool hidesHeadOnly();

//! \return true for a vertex of the player model's head (the head group and everything under it) or neck
bool isHeadVertex(EERIE_3DOBJ * obj, VertexId vertex);

/*!
 * \return the index of the first face the body added to the player model to close the neck opening the
 *         hidden head leaves (a fan over the boundary of the hidden faces, drawn only under a tracked
 *         head, where the model is drawn two-sided), or the face count of a model without one
 */
size_t bodyCapStart(const EERIE_3DOBJ * obj);

/*!
 * \brief Where the posed hand ended up, after the vertices were transformed - for status.json
 * \param palm the palm attach vertex in the world (on the controller's grip when the arm reached it)
 * \param tip the vertex farthest along the hand from the wrist: the fingertips
 * \return false without a bound model or a chain for the hand
 */
bool handLandmarks(Hand hand, Vec3f & palm, Vec3f & tip);

/*!
 * \return the hand's axes read from the bound model's mesh at rest (the frame the IK puts on the grip), or
 *         nullptr without a bound chain or a usable frame for that hand - for the held weapon (#75)
 */
const HandFrame * handRestFrame(Hand hand);

//! A simulated controller for checks without one (the test channel's \c handpose); a located controller wins over it
void setSimulatedHand(Hand hand, const HandSample & sample);
void clearSimulatedHand(Hand hand);

//! One arm as status.json reports it
struct PlayerArmStatus {
	const char * source = "none"; //!< xr (a located controller), sim (handpose), none
	bool tracked = false;         //!< a controller pose was available this frame
	bool posed = false;           //!< the arm was posed by the IK this frame (else the engine's animation)
	HandSample sample;            //!< the controller in the OpenXR LOCAL space (meters)
	Vec3f controller = Vec3f(0.f); //!< the grip in the world (units) - where the player's hand is
	Vec3f target = Vec3f(0.f);    //!< where the wrist should be for the palm to sit on the grip
	Vec3f shoulder = Vec3f(0.f);  //!< the posed joints (units) - where the model's arm is
	Vec3f elbow = Vec3f(0.f);
	Vec3f wrist = Vec3f(0.f);
	float reach = 0.f;            //!< shoulder to target
	float armLength = 0.f;        //!< upper arm + forearm of the model
	float error = 0.f;            //!< wrist to target: 0 when the hand is where the controller is
	float slide = 0.f;            //!< how far the shoulder slid towards an out-of-reach target
	bool stretched = false;
};

//! The body as status.json reports it (the look and the body yaw of #25 are vr::BodyStatus in HeadTracking.h)
struct PlayerBodyStatus {
	bool bound = false;           //!< arm chains were found on the player model
	std::string model;            //!< the model file bound to
	std::string chains;           //!< the group names of both chains, e.g. "left: left_arm>left_forearm>left_hand; right: ..."
	bool active = false;          //!< the body was shifted and at least one arm posed this frame
	const char * inactive = "";   //!< why not, when not
	u64 posedFrames = 0;          //!< frames with at least one posed arm since the game started
	Vec3f shift = Vec3f(0.f);     //!< this frame's horizontal shift of the body (units)
	float facingYaw = 0.f;        //!< engine degrees [0, 360): where the model was turned to face (#25's body yaw)
	std::array<PlayerArmStatus, HandCount> hands; //!< left, right
};

const PlayerBodyStatus & playerBodyStatus();

} // namespace vr

#endif // ARX_VR_VRBODY_H
