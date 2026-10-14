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
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/ArxArmFix.h"
#include "vr/FingerCurl.h"
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
 *
 * The fingers (#305, D-041): the model's finger groups under each hand - the thumb, the index
 * finger and the three-finger block, found as the hand group's children by name - bend about the
 * knuckle line by what the real fingers do: the hand joints of XR_EXT_hand_tracking when the
 * runtime gives them, the trigger, grip and thumb-rest values when it does not (vr/FingerCurl.h),
 * and closed on the grip while the hand holds a weapon (#75). The engine's animation no longer
 * moves the finger groups of a posed arm.
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

//! The horizontal shift the body was drawn with this frame (units): the lean and the push back behind the eyes
//! (#385.9); the engine camera takes it off
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
 * \brief Skin the classic Arx arm (#466.1, vr/ArxArmFix.h) - after the engine transformed the vertices of the
 *        player's own skeleton posed by \ref posePlayerBody
 *
 * With \c vr_arms_model = classic, the vertices of each posed arm whose ring the classic arm gives another bone than
 * their group's are placed again on that bone. Nothing else changes: flat mode (no posed arm), the inventory book
 * (its own update, never through here) and the other models keep the engine's skin.
 */
void skinPlayerArms(EERIE_3DOBJ * obj);

//! A vertex the classic arm skinned on another bone than its group's this frame, and the rotation it turned with
struct ReskinnedVertex {
	VertexId vertex;
	glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

//! The vertices of \a obj \ref skinPlayerArms placed this frame, for their lighting (empty for any other model)
const std::vector<ReskinnedVertex> & reskinnedArmVertices(const EERIE_3DOBJ * obj);

/*!
 * \return false for a face of the player model not drawn under a tracked head this frame (#385.3): an Arx arm's
 *         face while the custom arm (vr/VRArms.h) is drawn in its place, or a face of the cap that closes that
 *         shoulder while it is not; with vr_body = hidden (#385.11) every face of the body, and the cap of a shoulder
 *         then closes the drawn Arx arm's open end instead. Asked only for the two-sided view of the VR body (never
 *         the book, never flat)
 */
bool drawsBodyFace(const EERIE_3DOBJ * obj, size_t face);

/*!
 * What of the player model is drawn under a tracked head (#385.11, cfg.ini vr_body): the whole body pushed back
 * behind the eyes (#385.9), or the arms and hands only. Flat mode and the inventory book always draw the whole model.
 */
enum class BodyMode : u8 {
	Full,  //!< the body, pushed back behind the eyes (the default)
	Hidden //!< no torso, legs, neck or head: only the arms - the custom arms, or the Arx arms with their shoulder caps
};

//! \return the mode a cfg.ini value names: "hidden" hides the body, anything else draws it
BodyMode parseBodyMode(std::string_view text);
//! \return false unless \a text is "full" or "hidden" (the channel's \c body command)
bool parseBodyModeStrict(std::string_view text, BodyMode & mode);
const char * bodyModeName(BodyMode mode);

//! The mode cfg.ini asks for (vr_body)
BodyMode bodyMode();
//! Switch the mode live and save it to cfg.ini; \a by says who asked (the channel)
void setBodyMode(BodyMode mode, const char * by);

//! A face of the player model went into the two-sided view of the VR body this frame - for status.json (#385.11)
void noteBodyFaceDrawn(const EERIE_3DOBJ * obj, size_t face);

/*!
 * \return true for a face of the player model too near an eye to draw under a tracked head this frame (#385.9,
 *         vr/BodyView.h): a face of the torso, the shoulders or the caps with a corner within the clearance of an
 *         eye - never the legs, never an Arx arm. Worked out once per frame for both eyes, after the vertices were
 *         transformed; asked only for the two-sided view of the VR body (never the book, never flat)
 */
bool hidesBodyFaceNearEye(const EERIE_3DOBJ * obj, size_t face);

//! Where the posed hand ended up, after the vertices were transformed - for status.json
struct HandLandmarks {
	Vec3f palm = Vec3f(0.f);  //!< the palm attach vertex in the world (on the controller's grip when the arm reached it)
	Vec3f tip = Vec3f(0.f);   //!< the vertex farthest along the hand from the wrist at rest: a fingertip
	float tipAlong = 0.f;     //!< the fingertip from the wrist along the hand's long axis (units): shrinks as the fingers curl (#305)
	float tipOut = 0.f;       //!< the fingertip from the wrist towards the palm's face (units): grows as they curl
};

//! \return false without a bound model, a chain for the hand, or a pose this frame
bool handLandmarks(Hand hand, HandLandmarks & landmarks);

/*!
 * \return the hand's axes read from the bound model's mesh at rest (the frame the IK puts on the grip), or
 *         nullptr without a bound chain or a usable frame for that hand - for the held weapon (#75)
 */
const HandFrame * handRestFrame(Hand hand);

//! A simulated controller for checks without one (the test channel's \c handpose); a located controller wins over it
void setSimulatedHand(Hand hand, const HandSample & sample);
void clearSimulatedHand(Hand hand);

/*!
 * \brief A hand's controller for this frame: located with the eyes (F-21), else the simulated one
 * \return the source - "xr" or "sim" - or nullptr when the hand has neither
 */
const char * controllerSample(Hand hand, HandSample & sample);

//! One arm as status.json reports it
struct PlayerArmStatus {
	const char * source = "none"; //!< xr (a located controller), sim (handpose), none
	bool tracked = false;         //!< a controller pose was available this frame
	bool posed = false;           //!< the arm was posed by the IK this frame (else the engine's animation)
	HandSample sample;            //!< the controller in the OpenXR LOCAL space (meters)
	Vec3f controller = Vec3f(0.f); //!< the grip in the world (units) - where the player's hand is
	glm::quat grip = glm::quat(1.f, 0.f, 0.f, 0.f); //!< the grip's rotation in engine axes (HandPlacement::orientation)
	Vec3f target = Vec3f(0.f);    //!< where the wrist should be for the palm to sit on the grip
	Vec3f shoulder = Vec3f(0.f);  //!< the posed joints (units) - where the model's arm is
	Vec3f elbow = Vec3f(0.f);
	Vec3f wrist = Vec3f(0.f);
	float reach = 0.f;            //!< shoulder to target
	float armLength = 0.f;        //!< upper arm + forearm of the model
	float error = 0.f;            //!< wrist to target: 0 when the hand is where the controller is
	float slide = 0.f;            //!< how far the shoulder slid towards an out-of-reach target
	bool stretched = false;
	//! Where the hand's roll went (degrees, D-NEW, vr/ArmRoll.h): the hand's roll about the forearm against
	//! the base bend plane (unwrapped past a half turn), how far it swung the elbow, what was left against the
	//! final bend plane, the forearm's and the upper arm's roll off that plane, and what the bands between the
	//! bones show - the wrist band (hand vs forearm) and the elbow band (forearm vs upper arm)
	float handRoll = 0.f;
	float elbowSwing = 0.f;
	float residualRoll = 0.f;
	float forearmRoll = 0.f;
	float upperRoll = 0.f;
	float wristTwist = 0.f;
	float elbowTwist = 0.f;
	int overflow = 0;             //!< the side the roll continues on past a half turn: 0 none, +1, -1
	// The fingers (#305)
	const char * fingerSource = "none"; //!< joints (XR_EXT_hand_tracking), controls (trigger, grip, thumb rest), held (a weapon in the hand), none
	bool fingerGroups = false;    //!< the model has finger groups under this hand
	bool joints = false;          //!< the runtime located this hand's joints this frame
	bool holds = false;           //!< the hand holds a weapon: the fingers are closed on its grip
	FingerCurl curl;              //!< the segments' bend this frame (degrees), approaching \c curlTarget
	FingerCurl curlTarget;        //!< what the source asked for
	// The Arx arm along its rings (#466.1, vr/ArxArmFix.h)
	const char * armSkin = "none"; //!< the skin it was measured on: classic, arx (the engine's), none (not posed)
	ArmBend bend;                 //!< the creases, the elbow's bend and the forearm's twist spread on that skin
	bool classicDrawn = false;    //!< the classic arm re-skinned this arm's vertices this frame
};

//! The body as status.json reports it (the look and the body yaw of #25 are vr::BodyStatus in HeadTracking.h)
struct PlayerBodyStatus {
	bool bound = false;           //!< arm chains were found on the player model
	std::string model;            //!< the model file bound to
	std::string chains;           //!< the group names of both chains, e.g. "left: left_arm>left_forearm>left_hand; right: ..."
	std::string fingers;          //!< the finger groups per hand, e.g. "right: thumb right_hand_thumb, index right_hand_index_root>..., block ..."
	bool active = false;          //!< the body was shifted and at least one arm posed this frame
	const char * inactive = "";   //!< why not, when not
	u64 posedFrames = 0;          //!< frames with at least one posed arm since the game started
	u64 poseCalls = 0;            //!< frames the player's skeleton was posed at all (a still count: the model was not animated)
	Vec3f shift = Vec3f(0.f);     //!< this frame's horizontal shift of the body (units)
	float facingYaw = 0.f;        //!< engine degrees [0, 360): where the model was turned to face (#25's body yaw)
	//! the shares of the hand's roll in use (cfg.ini vr_arm_*, D-NEW): forearm, upper arm, elbow swing, overflow degrees
	float forearmShare = 0.f;
	float upperShare = 0.f;
	float elbowShare = 0.f;
	float twistOverflow = 0.f;
	const char * armsModel = "custom"; //!< cfg.ini vr_arms_model: the arms drawn in VR, custom or arx (#385.3)
	bool arxArmsHidden = false;   //!< an Arx arm's faces are not drawn this frame: a custom arm is drawn in their place
	// The body variant (#385.11): cfg.ini vr_body, and what of the player model the last frame drew (both eyes counted)
	const char * bodyMode = "full";
	size_t drawnBodyFaces = 0;    //!< the body's faces: torso, legs, the neck cap, the head - not the arms
	size_t drawnArmFaces = 0;     //!< the Arx arms' faces and the caps that close their shoulders
	// The own body out of the eyes (#385.9, vr/BodyView.h)
	float push = 0.f;             //!< how far the body was pushed back behind the eyes along its facing this frame (units)
	Vec3f neckPivot = Vec3f(0.f); //!< the neck pivot below and behind the eyes the body hangs from
	float clearance = 0.f;        //!< faces within this distance of an eye are not drawn (units)
	size_t hiddenNearEye = 0;     //!< faces not drawn this frame because they were within the clearance
	float nearestBodyToEye = -1.f; //!< the nearest drawn body vertex to either eye (units; Arx arms aside), -1 none drawn
	bool legsDrawn = false;       //!< faces wholly below the keep line (the legs) were drawn this frame
	std::array<PlayerArmStatus, HandCount> hands; //!< left, right
};

const PlayerBodyStatus & playerBodyStatus();

} // namespace vr

#endif // ARX_VR_VRBODY_H
