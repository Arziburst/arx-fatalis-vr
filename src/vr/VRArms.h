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
#include "vr/HandRig.h"
#include "vr/VRControls.h"

class TextureContainer;

/*!
 * \file
 * \brief The arms on the player in VR (D-047, D-051): the classic Arx arm, with the Arx hand or our glove at its wrist
 *
 * The Arx arm is the drawn and the logical arm: vr/VRBody.cpp poses it (#313's roll-aware IK) and re-skins it with one
 * elbow (vr/ArxArmFix.h), and the weapon, the reticle, the wrist bars and the melee read it. With
 * \c vr_classic_hand = glove (#466.2) the Arx hand's faces are not drawn in VR (\ref vr::drawsBodyFace) and this module
 * draws the Godot XR Tools glove hand (data/core/vr/arms/Hand_Glove_low_L/R.gltf) in its place: its cuff on the Arx
 * wrist joint, turned by the controller's grip like the Arx hand (D-032), scaled by \c vr_hand_scale; its joints turn as
 * the joints' orientations say (vr/HandRig.h poseHandFromJoints, #385.7): closed on the hilt pose by a held weapon on
 * either hand (D-042), else following the runtime's hand joints (conforming to the controller where the runtime can),
 * else closing each finger toward MRTK3's Grab by the controller's trigger, grip and thumb rest (D-041).
 * Skinned once per frame after the body is posed; the animation code lights and projects it per eye into the model
 * batch, two-sided like the VR body (research row F-231). Flat mode and the inventory book always draw the Arx arms.
 * The other arm models (custom, forearm, hands, arx) were removed with their own arm solve and sleeve (#486).
 */
namespace vr {

//! The arms drawn in VR: the owner kept Classic (2026-10-16, #466), the other models are gone (#486)
enum class ArmsModel : u8 {
	Classic, //!< the player model's own arms with one elbow and a rigid forearm (#466.1, vr/ArxArmFix.h)
};

//! The glove against its real size: bigger than the Arx hands by default (the owner: fingers too small)
constexpr float DefaultHandScale = 1.15f;
constexpr float MinHandScale = 0.8f;
constexpr float MaxHandScale = 1.5f;

//! \return the model cfg.ini selects: always classic (#466) - a value an earlier build saved reads as it
ArmsModel parseArmsModel(std::string_view text);
//! \return false unless \a text names a model (the test channel's \c arms command)
bool parseArmsModelStrict(std::string_view text, ArmsModel & model);
const char * armsModelName(ArmsModel model);
//! The models' names for a message
std::string armsModelNames();

float clampHandScale(float scale);

//! The model cfg.ini asks for (vr_arms_model)
ArmsModel armsModel();
//! Switch the model live and save it to cfg.ini; \a by says who asked (the test channel)
void setArmsModel(ArmsModel model, const char * by);

//! The hand at the end of the classic arm (#466.2, research F-281, F-282): the owner compares them in the headset
enum class ClassicHand : u8 {
	Arx,   //!< the Arx hand of the player model (the default)
	Glove  //!< our five-finger glove (#385.7) at the Arx wrist, the Arx hand faces left out of the draw
};

//! \return the hand a cfg.ini value names: "glove" our glove, anything else the Arx hand
ClassicHand parseClassicHand(std::string_view text);
//! \return false unless \a text is "arx" or "glove" (the channel's \c arms \c classic-hand)
bool parseClassicHandStrict(std::string_view text, ClassicHand & hand);
const char * classicHandName(ClassicHand hand);
//! The classic arm's hand cfg.ini asks for (vr_classic_hand)
ClassicHand classicHand();
//! Switch the classic arm's hand live and save it to cfg.ini; \a by says who asked
void setClassicHand(ClassicHand hand, const char * by);
//! Set vr_hand_scale live (clamped) and save it to cfg.ini
void setHandScale(float scale, const char * by);

//! The posed Arx arm of one hand this frame: what the glove at its wrist is placed and closed by (#466.2)
struct LogicalArm {
	Vec3f grip = Vec3f(0.f);            //!< the controller's grip in the world
	glm::quat gripOrientation = quat_identity(); //!< the grip's rotation in engine axes
	Vec3f arxWrist = Vec3f(0.f);        //!< the Arx wrist joint as posed: the glove's cuff sits there
	bool holdsWeapon = false;           //!< the hand holds the equipped weapon this frame
	// The glove's five fingers (#385.4, D-041, D-042): the held rule's inputs (holds = holdsWeapon) and the hand joints
	CustomFingerInput fingers;
	HandJoints joints;                  //!< this frame's hand joints (invalid without them)
	float seconds = 0.f;                //!< this frame's duration: the fingers move at most CurlDegreesPerSecond
};

//! Forget last frame's gloves (before the body is posed)
void resetGloves();

/*!
 * \brief Skin the glove of \a hand at the classic arm's Arx wrist (vr_classic_hand = glove, #466.2)
 * \return true when the glove is drawn this frame in place of the Arx hand
 */
bool poseGlove(Hand hand, const LogicalArm & arm);

//! The glove of \a hand is drawn at the classic arm's Arx wrist this frame (#466.2): its Arx hand faces are not
bool arxHandHidden(Hand hand);
//! The centre of the Arx wrist ring the glove's cuff meets, as the classic arm skinned it this frame: its wristGap (#466.2)
void noteArxWristRing(Hand hand, const Vec3f & centre);

/*!
 * Whether the glove meshes loaded (status.json vr.body.customArms). A glove is read the first time its arm is
 * posed with vr_classic_hand = glove; a missing or broken data/core/vr/arms file is logged as an error.
 */
struct CustomArmsLoad {
	bool tried = false;   //!< at least one glove was read
	bool loaded = false;  //!< every glove read so far loaded
	std::string reason;   //!< why not, when not
};

CustomArmsLoad customArmsLoad();

//! One glove as status.json reports it (vr.body.<hand>.custom)
struct CustomArmStatus {
	bool drawn = false;          //!< the glove is drawn at the Arx wrist
	float handScale = DefaultHandScale;
	float tipLength = 0.f;       //!< the hand from the palm joint to the middle fingertip (units)
	Vec3f palm = Vec3f(0.f);     //!< the glove's palm joint in the world
	Vec3f wrist = Vec3f(0.f);    //!< the Arx wrist joint the glove's cuff sits on
	float wristGap = 0.f;        //!< the Arx wrist ring as skinned to the glove's cuff centre (units, 0 = they meet),
	                             //!< -1 unmeasured
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
	// The glove's hilt frame (#385.10): unused while the weapon is placed from the Arx hand, kept for #548
	bool hiltFrame = false;      //!< set this frame (the glove drawn)
	Vec3f hiltCentre = Vec3f(0.f); //!< the hold point: the hilt pose's curl centre as drawn (the grip without a curl)
	Vec3f hiltAxis = Vec3f(0.f, 0.f, 1.f); //!< unit: the line the closed fingers wrap, toward the thumb side (the blade's way)
	Vec3f knuckles = Vec3f(0.f, 1.f, 0.f); //!< unit: the glove's wrist to its middle knuckle
	glm::quat hiltGrip = quat_identity(); //!< the controller grip's rotation (engine axes)
	float handWidth = 0.f;       //!< the glove's breadth across its four knuckles (units)
	size_t vertices = 0;
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

//! One mesh to draw, in the world: the glove of an arm drawn this frame
struct CustomArmMesh {
	const std::vector<Vec3f> * positions = nullptr;
	const std::vector<Vec3f> * normals = nullptr;
	const std::vector<Vec2f> * uvs = nullptr;
	const std::vector<u16> * indices = nullptr;
	TextureContainer * texture = nullptr;
};

//! The meshes drawn this frame (one glove per hand at most); \return how many
size_t customArmMeshes(CustomArmMesh (&meshes)[4]);

} // namespace vr

#endif // ARX_VR_VRARMS_H
