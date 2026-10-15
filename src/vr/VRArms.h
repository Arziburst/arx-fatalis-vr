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

#ifndef ARX_VR_VRARMS_H
#define ARX_VR_VRARMS_H

#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/ArmRoll.h"
#include "vr/HandRig.h"
#include "vr/VRControls.h"

class TextureContainer;

/*!
 * \file
 * \brief Our own arms on the player in VR (#385, D-049): a sleeve from the shoulder to the wrist and a glove hand
 *
 * The Arx arm stays the logical arm: vr/VRBody.cpp poses it as before (#313's roll-aware IK), and the
 * weapon, the reticle, the wrist bars and the melee keep reading it. With \c vr_arms_model = custom (forearm, hands) its
 * faces are not drawn in VR (\ref vr::drawsBodyFace), and this module draws in its place:
 *  - the procedural sleeve of vr/ArmMesh.h on the custom arm's own solve (#385.6): segment lengths fitted to the
 *    player's reach (vr/ArmFit.h), from the Arx shoulder to the glove's cuff, stretched by at most MaxArmStretch
 *    instead of the shoulder slide, the elbow dropped while the hand holds a weapon (F-300); its joints: the shoulder
 *    (sunk a little into the torso), the upper twist joint with the upper arm's roll, the elbow in the
 *    bend plane with the drawn bend clamped (\c MaxDrawnBend), the two forearm twist joints with a third
 *    and two thirds of the forearm-to-hand roll, and the wrist on the glove hand's cuff, so the two
 *    openings meet; the elbow corrective is applied from the bend, the normals are recomputed from the
 *    skinned positions every frame (research rows F-233, F-310 .. F-312);
 *  - the Godot XR Tools glove hand (data/core/vr/arms/Hand_Glove_low_L/R.gltf) with its palm behind the
 *    controller's grip by the handle's and the palm's radius (short of it past the stretch limit) and the
 *    controller's orientation, scaled by \c vr_hand_scale; its joints turn as the joints' orientations say
 *    (vr/HandRig.h poseHandFromJoints, #385.7): closed on the hilt pose by a held weapon on either hand (D-042),
 *    else following the runtime's hand joints (conforming to the controller where the runtime can), else
 *    closing each finger toward MRTK3's Grab by the controller's trigger, grip and thumb rest (D-041).
 * The forearm and hands models (#385.8) pose the same arm and draw less of it: the sleeve cut ForearmKept along the
 * forearm from the wrist, or only at the wrist, its cut closed by a cuff (vr/ArmMesh.h SleeveParams::startAt).
 * Skinned once per frame after the body is posed; the animation code lights and projects the result per eye
 * into the model batch, two-sided like the VR body (research row F-231). Flat mode and the inventory book
 * always draw the Arx arms.
 *
 * With \c vr_arms_model = library (#467.1) a ready-made hand takes the glove's place and no arm is drawn: the WebXR
 * generic hand (data/core/vr/hands/webxr, MIT), its wrist ending clean, placed on the controller's handle and
 * posed joint by joint exactly as the glove is, in a plain grey (it has no texture) lit like the player model.
 */
namespace vr {

//! The arms drawn in VR (#385.8: a choice of models, the owner picks in the headset); what each draws is its ArmsModelInfo
enum class ArmsModel : u8 {
	Custom,  //!< our own arms: the glove and the whole sleeve (the default)
	Forearm, //!< the glove and a short forearm ending in a closed cuff before the elbow (research F-305, F-467)
	Hands,   //!< the glove alone, closed at the wrist (Half-Life: Alyx, research F-303)
	Arx,     //!< the player model's own arms
	// Classic, //!< #466: the fixed Arx arm
	Library, //!< a ready-made hand from a library, no arm (#467.1: the WebXR generic hand)
};

/*!
 * What one arms model draws (#385.8). The models are one table (VRArms.cpp ArmsModelTable), in the order the
 * playground's arms lever cycles them; the code reads these properties, never the model itself, so a new model is
 * one row
 */
struct ArmsModelInfo {
	ArmsModel model;
	const char * name;  //!< cfg.ini vr_arms_model, the channel's `arms <name>`, status.json vr.body.armsModel
	bool glove;         //!< our glove hand is drawn (with a closed cuff when no sleeve meets it)
	bool library;       //!< the library hand is drawn instead of the glove, alone: no cuff, no sleeve (#467)
	bool sleeve;       //!< the sleeve is drawn, from the wrist along the forearm
	bool upperArm;      //!< the sleeve goes on to the shoulder; without it, it ends forearmKept along the forearm
	float forearmKept;  //!< the share of the forearm the sleeve covers from the wrist without the upper arm
	bool arxArms;       //!< the Arx arm faces are drawn (the logical arm then stays on the drawn, pushed shoulder)
};

/*!
 * The forearm variant's sleeve ends this share of the forearm from the wrist (#385.8, the owner's first proposal): before
 * the elbow, so the elbow no solver places where the player's is (F-461) is never drawn
 */
constexpr float ForearmKept = 0.6f;

//! The custom hands against the glove's real size: bigger than the Arx hands by default (the owner: fingers too small)
constexpr float DefaultHandScale = 1.15f;
constexpr float MinHandScale = 0.8f;
constexpr float MaxHandScale = 1.5f;

/*!
 * A hand holding a weapon drops its elbow (#385.6, research row F-300: VHVR's equipped palm-to-thumb axis (-+1, -3, 0)
 * puts three parts down to one sideways): the elbow turns about the shoulder-target line towards its lowest position
 * by this share of its angle off it. The default of cfg.ini vr_arm_weapon_elbow_drop, which the owner tunes in the headset
 */
constexpr float DefaultWeaponElbowDrop = 0.75f;

//! \return \a share within [0, 1] (DefaultWeaponElbowDrop for a value that is not a number)
float clampWeaponElbowDrop(float share);

//! \return the model a cfg.ini value names; a name no model has is our own arms (custom)
ArmsModel parseArmsModel(std::string_view text);
//! \return false unless \a text names a model (the channel's \c arms command)
bool parseArmsModelStrict(std::string_view text, ArmsModel & model);
const char * armsModelName(ArmsModel model);
//! The models' names for a message: "custom, forearm, hands, arx or library"
std::string armsModelNames();
const ArmsModelInfo & armsModelInfo(ArmsModel model);
//! The model after \a model in the lever's cycle (the last one is followed by the first)
ArmsModel nextArmsModel(ArmsModel model);
float clampHandScale(float scale);

//! The model cfg.ini asks for (vr_arms_model)
ArmsModel armsModel();
//! What it draws
const ArmsModelInfo & armsModelInfo();
//! Switch the model live and save it to cfg.ini; \a by says who asked (the channel, the playground lever)
void setArmsModel(ArmsModel model, const char * by);
//! Set vr_hand_scale live (clamped) and save it to cfg.ini
void setHandScale(float scale, const char * by);

//! The playground's arms lever (#385.3, role lever_arms): each pull steps to the next model (#385.8); call once per frame
void followArmsLever();

/*!
 * The logical Arx arm of one hand this frame: what the custom arm is solved from (#385.6) - its shoulder, the Arx
 * hand frame the roll is measured in, and the elbow poles; the custom arm then solves its own chain with its own
 * lengths from that shoulder to the glove's cuff
 */
struct LogicalArm {
	Vec3f shoulder = Vec3f(0.f);        //!< the Arx shoulder joint before the logical arm's shoulder slide
	glm::quat hand = quat_identity();   //!< the Arx hand bone's world rotation (rest -> world), from the controller
	ArmRest rest;                       //!< the Arx chain at rest, in the model's mesh space
	Vec3f restElbow = Vec3f(0.f);       //!< where the Arx elbow sticks out at rest, in the model's mesh space
	Vec3f elbowHint = Vec3f(0.f);       //!< the base pole of the elbow (the body's down, out, back)
	bool holdsWeapon = false;           //!< the hand holds the equipped weapon this frame: its elbow drops (F-300)
	ArmRollParams roll;                 //!< the shares of the hand's roll, as the logical arm uses them
	Vec3f grip = Vec3f(0.f);            //!< the controller's grip in the world
	glm::quat gripOrientation = quat_identity(); //!< the grip's rotation in engine axes
	float upperLength = 0.f;            //!< the Arx bones, units: the fit when the character's height is unknown
	float forearmLength = 0.f;
	// The glove's five fingers (#385.4, D-041, D-042): the held rule's inputs (holds = holdsWeapon) and the hand joints
	CustomFingerInput fingers;
	HandJoints joints;                  //!< this frame's hand joints (invalid without them)
	float seconds = 0.f;                //!< this frame's duration: the fingers move at most CurlDegreesPerSecond
};

//! Forget last frame's custom arms (before the body is posed)
void resetCustomArms();

/*!
 * \brief Skin the custom arm of \a hand on the logical arm, when the model draws our glove (custom, forearm, hands) -
 *        or only the library hand, when the model draws that (library)
 * \return true when the custom arm - the glove and as much of the sleeve as the model draws - or the library hand is
 *         drawn this frame in place of the Arx arm
 */
bool poseCustomArm(Hand hand, const LogicalArm & arm);

//! The custom arm of \a hand, whole or in part, or its library hand is drawn this frame (its Arx arm faces are not)
bool customArmDrawn(Hand hand);

/*!
 * Whether the hand meshes loaded (status.json vr.body.customArms). A glove is read the first time its arm is
 * posed with the custom model on, a library hand with the library model on; a missing or broken data/core/vr/arms
 * or data/core/vr/hands/webxr file is logged as an error.
 */
struct CustomArmsLoad {
	bool tried = false;   //!< at least one glove was read
	bool loaded = false;  //!< every glove read so far loaded
	std::string reason;   //!< why not, when not
};

CustomArmsLoad customArmsLoad();

/*!
 * One custom arm as status.json reports it (vr.body.<hand>.custom). With the library model (#467.1) the hand's fields
 * are the library hand's, \c wrist is its wrist joint and the arm's (the fit to \c wristGap) are zero: no arm is drawn
 */
struct CustomArmStatus {
	bool drawn = false;          //!< the glove (with what of the sleeve the model draws) or the library hand is drawn
	bool library = false;        //!< the drawn hand is the library hand (vr.body.<hand>.library.drawn)
	bool sleeveDrawn = false;    //!< the sleeve, whole or the forearm's part (#385.8)
	bool upperArmDrawn = false;  //!< the sleeve's upper arm and elbow (#385.8)
	float handScale = DefaultHandScale;
	float tipLength = 0.f;       //!< the hand from the palm joint to the middle fingertip (units)
	Vec3f palm = Vec3f(0.f);     //!< the glove's palm joint in the world (behind the grip, #385.7)
	Vec3f wrist = Vec3f(0.f);    //!< where the sleeve meets the glove's cuff
	// The fit (#385.6, vr/ArmFit.h)
	float upperLength = 0.f;     //!< the fitted segments, units
	float forearmLength = 0.f;   //!< the forearm drawn: the fitted one, ForearmKept of it in the forearm model, 0 for hands
	const char * fitSource = "none"; //!< reach (measured), height (the character height setting) or arx (the Arx bones)
	float reach = 0.f;           //!< the measured reach so far (shoulder to wrist, units; 0 before a sample)
	float stretch = 0.f;         //!< the stretch fraction in use, 0 .. MaxArmStretch
	float error = 0.f;           //!< the solved wrist to its target on the controller (units; > 0 past the stretch limit)
	float elbowDrop = 0.f;       //!< how far the elbow is below the elbow without a weapon (units, 0 without one)
	float bend = 0.f;            //!< the custom elbow's bend (degrees, 0 straight)
	float drawnBend = 0.f;       //!< the bend the sleeve is drawn with (clamped)
	float upperRoll = 0.f;       //!< the roll on the upper twist joint
	float forearmTwist = 0.f;    //!< the roll from the elbow to the wrist; the twist joints take a third and two thirds
	float wristGap = 0.f;        //!< the sleeve's last ring centre to the glove's cuff centre (units, 0 = they meet)
	// The five fingers (#385.4): what they follow (none while the arm is not posed) and each joint's bend this frame
	CustomFingerSource fingerSource = CustomFingerSource::None;
	FingerPose fingers;          //!< degrees, approaching what the source asks for
	// The glove driven joint by joint (#385.7)
	HiltPoseSource hiltPose = HiltPoseSource::None; //!< what a held weapon closes the hand on (none: no weapon)
	float palmToGrip = 0.f;      //!< the glove palm's centre (the middle metacarpal's midpoint) from the grip (units)
	Vec3f grip = Vec3f(0.f);     //!< the controller's grip in the world: a point of the handle's line
	Vec3f handle = Vec3f(0.f, 0.f, 1.f); //!< the handle's direction in the world (the grip's Z), unit
	float curlToHandle = 0.f;    //!< the hilt pose's curl centre from the handle's line, as drawn (units)
	float curlAngle = 0.f;       //!< the hilt pose's curl axis off the handle, as drawn (degrees)
	// The glove's hilt frame (#385.10): a held weapon is drawn from it with every model that draws the glove
	bool hiltFrame = false;      //!< set this frame (the glove drawn)
	Vec3f hiltCentre = Vec3f(0.f); //!< the hold point: the hilt pose's curl centre as drawn (the grip without a curl)
	Vec3f hiltAxis = Vec3f(0.f, 0.f, 1.f); //!< unit: the line the closed fingers wrap, toward the thumb side (the blade's way)
	Vec3f knuckles = Vec3f(0.f, 1.f, 0.f); //!< unit: the glove's wrist to its middle knuckle
	glm::quat hiltGrip = quat_identity(); //!< the controller grip's rotation (engine axes): vr.weapon.bladeGrip's frame
	float handWidth = 0.f;       //!< the glove's breadth across its four knuckles (units)
	size_t vertices = 0;         //!< sleeve + hand
	size_t triangles = 0;
};

const CustomArmStatus & customArmStatus(Hand hand);

/*!
 * \brief Store \a hand's runtime joints of the last frame as its hilt pose (#385.7, the channel's `grip capture`)
 *
 * Written to <user dir>/vr-hilt-pose-<left|right>.json (vr/HandRig.h handShapeJson, palm-relative) and used at once
 * for a held weapon; read again on the next start. \a result says what was done or why not (no joints this frame,
 * the file could not be written). In the headset with SteamVR this is the Index's conforming grip (research F-486).
 */
bool captureHiltPose(Hand hand, std::string & result);

//! Forget \a hand's captured hilt pose and delete its file: a held weapon closes the hand on the default hilt pose
void clearHiltPose(Hand hand);

//! One mesh to draw, in the world: the sleeve or the hand of an arm drawn this frame
struct CustomArmMesh {
	const std::vector<Vec3f> * positions = nullptr;
	const std::vector<Vec3f> * normals = nullptr;
	const std::vector<Vec2f> * uvs = nullptr;
	const std::vector<u16> * indices = nullptr;
	TextureContainer * texture = nullptr;
};

//! The meshes drawn this frame (up to four: each arm's sleeve and hand, a library hand alone); \return how many
size_t customArmMeshes(CustomArmMesh (&meshes)[4]);

} // namespace vr

#endif // ARX_VR_VRARMS_H
