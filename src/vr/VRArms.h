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
#include "vr/VRControls.h"

class TextureContainer;

/*!
 * \file
 * \brief Our own arms on the player in VR (#385, D-NEW-arms): a sleeve from the shoulder to the wrist and a glove hand
 *
 * The Arx arm stays the logical arm: vr/VRBody.cpp poses it as before (#313's roll-aware IK), and the
 * weapon, the reticle, the wrist bars and the melee keep reading it. With \c vr_arms_model = custom its
 * faces are not drawn in VR (\ref vr::drawsBodyFace), and this module draws in its place:
 *  - the procedural sleeve of vr/ArmMesh.h on the custom arm's own solve (#385.6): segment lengths fitted to the
 *    player's reach (vr/ArmFit.h), from the Arx shoulder to the glove's cuff, stretched by at most MaxArmStretch
 *    instead of the shoulder slide, the elbow dropped while the hand holds a weapon (F-300); its joints: the shoulder
 *    (sunk a little into the torso), the upper twist joint with the upper arm's roll, the elbow in the
 *    bend plane with the drawn bend clamped (\c MaxDrawnBend), the two forearm twist joints with a third
 *    and two thirds of the forearm-to-hand roll, and the wrist on the glove hand's cuff, so the two
 *    openings meet; the elbow corrective is applied from the bend, the normals are recomputed from the
 *    skinned positions every frame (research rows F-233, F-310 .. F-312);
 *  - the Godot XR Tools glove hand (data/core/vr/arms/Hand_Glove_low_L/R.gltf) with its palm on the
 *    controller's grip (short of it past the stretch limit) and the controller's orientation, scaled by
 *    \c vr_hand_scale, at rest pose (the
 *    fingers are 385.4's).
 * Skinned once per frame after the body is posed; the animation code lights and projects the result per eye
 * into the model batch, two-sided like the VR body (research row F-231). Flat mode and the inventory book
 * always draw the Arx arms.
 */
namespace vr {

enum class ArmsModel : u8 {
	Custom, //!< our own arms (the default)
	Arx     //!< the player model's own arms
};

//! The custom hands against the glove's real size: bigger than the Arx hands by default (the owner: fingers too small)
constexpr float DefaultHandScale = 1.15f;
constexpr float MinHandScale = 0.8f;
constexpr float MaxHandScale = 1.5f;

//! \return the model a cfg.ini value names: "arx" is the Arx arms, anything else our own
ArmsModel parseArmsModel(std::string_view text);
//! \return false unless \a text is "custom" or "arx" (the channel's \c arms command)
bool parseArmsModelStrict(std::string_view text, ArmsModel & model);
const char * armsModelName(ArmsModel model);
float clampHandScale(float scale);

//! The model cfg.ini asks for (vr_arms_model)
ArmsModel armsModel();
//! Switch the model live and save it to cfg.ini; \a by says who asked (the channel, the playground lever)
void setArmsModel(ArmsModel model, const char * by);
//! Set vr_hand_scale live (clamped) and save it to cfg.ini
void setHandScale(float scale, const char * by);

//! The playground's arms lever (#385.3, role lever_arms): each pull flips the model; call once per frame
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
};

//! Forget last frame's custom arms (before the body is posed)
void resetCustomArms();

/*!
 * \brief Skin the custom arm of \a hand on the logical arm, when the custom model is on
 * \return true when the custom arm is drawn this frame in place of the Arx arm
 */
bool poseCustomArm(Hand hand, const LogicalArm & arm);

//! The custom arm of \a hand is drawn this frame (its Arx arm faces are not)
bool customArmDrawn(Hand hand);

/*!
 * Whether the glove meshes loaded (status.json vr.body.customArms). A glove is read the first time its arm is
 * posed with the custom model on; a missing or broken data/core/vr/arms file is logged as an error.
 */
struct CustomArmsLoad {
	bool tried = false;   //!< at least one glove was read
	bool loaded = false;  //!< every glove read so far loaded
	std::string reason;   //!< why not, when not
};

CustomArmsLoad customArmsLoad();

//! One custom arm as status.json reports it (vr.body.<hand>.custom)
struct CustomArmStatus {
	bool drawn = false;
	float handScale = DefaultHandScale;
	float tipLength = 0.f;       //!< the hand from the palm joint to the middle fingertip (units)
	Vec3f palm = Vec3f(0.f);     //!< the glove's palm joint in the world (on the grip)
	Vec3f wrist = Vec3f(0.f);    //!< where the sleeve meets the glove's cuff
	// The fit (#385.6, vr/ArmFit.h)
	float upperLength = 0.f;     //!< the fitted segments, units
	float forearmLength = 0.f;
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
	size_t vertices = 0;         //!< sleeve + hand
	size_t triangles = 0;
};

const CustomArmStatus & customArmStatus(Hand hand);

//! One mesh to draw, in the world: the sleeve or the hand of an arm drawn this frame
struct CustomArmMesh {
	const std::vector<Vec3f> * positions = nullptr;
	const std::vector<Vec3f> * normals = nullptr;
	const std::vector<Vec2f> * uvs = nullptr;
	const std::vector<u16> * indices = nullptr;
	TextureContainer * texture = nullptr;
};

//! The meshes drawn this frame (up to four: each arm's sleeve and hand); \return how many
size_t customArmMeshes(CustomArmMesh (&meshes)[4]);

} // namespace vr

#endif // ARX_VR_VRARMS_H
