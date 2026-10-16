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

#include "vr/VRArms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/Config.h"
#include "core/Core.h"
#include "core/GameEvent.h"
#include "game/Levels.h"
#include "graphics/Renderer.h"
#include "graphics/data/TextureContainer.h"
#include "graphics/image/Image.h"
#include "graphics/texture/Texture.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "platform/Time.h"
#include "script/Script.h"
#include "util/String.h"
#include "vr/ArmFit.h"
#include "vr/ArmIK.h"
#include "vr/ArmMesh.h"
#include "vr/ArmRoll.h"
#include "vr/HeadTracking.h"
#include "vr/WorldScale.h"

namespace vr {

namespace {

//! How far the sleeve's top ring sits inside the torso, back along the upper arm from the shoulder joint (units)
const float ArmsShoulderSink = 1.5f;
//! The sleeve's elbow is drawn bent at most this much (degrees, research row F-310); the forearm's twist joints
//! still point at the hand, so a tighter bend folds over the first third of the forearm instead of the elbow ring
const float ArmsMaxDrawnBend = 150.f;
//! The elbow corrective's strength (units at a full fold, vr/ArmMesh.h applyElbowCorrective)
const float ArmsElbowCorrective = 1.5f;
//! A new fit (the reach measured, the height setting changed) is taken over about this long (seconds, 95 %)
const float ArmsFitEaseSeconds = 0.5f;
//! The glove's cuff opening is this far past its Wrist joint towards the fingers (meters, 385.1's measure)
const float GloveCuffPastWrist = 0.0014f;
//! The glove meshes are in meters
const float GloveUnitsPerMeter = 100.f;
//! The colour of a missing texture: plain leather brown
const u8 ArmsLeather[3] = { 92, 60, 38 };

/*!
 * The arms models (#385.8). The first is the default and the only one a player can choose: the owner kept classic
 * (2026-10-16, #466), and cfg.ini reads any other name as it. The others are reachable only through the test
 * channel's `arms <model>` until they are deleted with their tests (#486)
 */
const ArmsModelInfo ArmsModelTable[] = {
	// model             name       glove  sleeve upperArm forearmKept  arxArms classicSkin
	{ ArmsModel::Classic, "classic", false, false, false,   0.f,         true,   true },
	{ ArmsModel::Custom,  "custom",  true,  true,  true,    1.f,         false,  false },
	{ ArmsModel::Forearm, "forearm", true,  true,  false,   ForearmKept, false,  false },
	{ ArmsModel::Hands,   "hands",   true,  false, false,   0.f,         false,  false },
	{ ArmsModel::Arx,     "arx",     false, false, false,   0.f,         true,   false },
};
const size_t ArmsModelCount = sizeof(ArmsModelTable) / sizeof(ArmsModelTable[0]);

size_t armsModelIndex(ArmsModel model) {
	for(size_t i = 0; i < ArmsModelCount; i++) {
		if(ArmsModelTable[i].model == model) {
			return i;
		}
	}
	return 0;
}

//! Where the sleeve of \a info starts along the arm built with \a upper and \a forearm (vr/ArmMesh.h SleeveParams)
float sleeveStartAt(const ArmsModelInfo & info, float upper, float forearm) {
	if(info.upperArm) {
		return 0.f;
	}
	const float kept = info.sleeve ? glm::clamp(info.forearmKept, 0.f, 1.f) : 0.f;
	return upper + forearm * (1.f - kept);
}

//! A glove hand at rest, read once from data/core/vr/arms
struct GloveRest {
	bool tried = false;
	bool loaded = false;
	std::string error;           //!< why it is not loaded, after a try
	SkinnedMesh mesh;
	std::vector<glm::mat4> restWorld;
	Vec3f wrist = Vec3f(0.f);    //!< mesh space, meters
	Vec3f palm = Vec3f(0.f);
	Vec3f tip = Vec3f(0.f);      //!< the middle fingertip
	Vec3f palmCentre = Vec3f(0.f); //!< the middle metacarpal's midpoint: the palm's centre (the Palm joint lies off it)
	Vec3f cuff = Vec3f(0.f);     //!< the centre of the cuff opening
	Vec3f knuckle = Vec3f(0.f);  //!< the middle finger's knuckle (#385.10: the wrist to it is the knuckle direction)
	Vec3f indexKnuckle = Vec3f(0.f);  //!< the index and the little finger's knuckles: the hand's breadth
	Vec3f littleKnuckle = Vec3f(0.f);
	glm::quat toGrip = quat_identity(); //!< the mesh's rest frame to the grip frame (vr/ArmIK.h handToGrip)
	glm::quat sleeveAxes = quat_identity(); //!< the sleeve's wrist axes (along, thickness, width) in mesh space
	HandRig rig;                 //!< the five fingers' joints and axes (#385.4)
};

struct CustomArm {
	GloveRest glove;
	SkinnedMesh sleeve;            //!< at rest, built for the logical arm's bone lengths
	SkinnedMesh sleeveWork;        //!< the same with this frame's elbow corrective
	float builtUpper = 0.f;
	float builtForearm = 0.f;
	float builtStart = 0.f;        //!< where it starts along the arm: 0 whole, else cut by the model (#385.8)
	std::vector<glm::mat4> sleeveJoints;
	std::vector<glm::mat4> gloveLocal;  //!< the glove's joints posed by the fingers, on their parents (#385.4)
	std::vector<glm::mat4> gloveJoints;
	std::vector<Vec3f> sleevePositions;
	std::vector<Vec3f> sleeveNormals;
	std::vector<Vec3f> handPositions;
	std::vector<Vec3f> handNormals;
	std::vector<size_t> lastRing;  //!< the sleeve's vertices on its last ring
	ReachTracker reach;            //!< the player's reach with this hand (#385.6)
	ArmLengths lengths;            //!< the segments in use: easing towards the fit (0 before the first)
	ArmRollState roll;             //!< the overflow side of this arm's roll, between frames
	// The glove driven joint by joint (#385.7)
	bool shapesLoaded = false;     //!< open, Grab and the captured hilt pose were read
	CustomHandShapes shapes;       //!< palm-relative
	HiltAngles hiltBuiltFor;       //!< the configured angles shapes.hilt was built with
	bool hiltBuilt = false;
	HandJoints lastJoints;         //!< the runtime's joints of the last frame posed (for `grip capture`)
	HandJoints current;            //!< what the glove is posed by: the target approached at a bounded speed
	GloveCurl hiltCurl;            //!< the axis the glove's fingers wrap in the default hilt pose (mesh space)
	bool drawn = false;
	bool atArxWrist = false;       //!< drawn as the classic arm's hand: the glove alone, at the Arx wrist (#466.2)
	CustomArmStatus status;
};

std::array<CustomArm, HandCount> g_customArms;
float g_armsBodyHeight = -1.f; //!< the character's height as the setting makes it, units; -1 before it was known

/*!
 * \return the body height the character height setting makes (#260), units: the character's standing eye
 *         lifted by the setting, against the eye's share of the stature (vr/ArmFit.h); -1 while unknown
 */
float armsBodyHeight() {
	const HeadStatus & head = headStatus();
	if(head.eyeAboveFeet >= 0.f) {
		const float standingEye = head.eyeAboveFeet + head.characterDrop;
		g_armsBodyHeight = (standingEye + head.heightOffset * UnitsPerMeter) / EyeToStature;
	}
	return g_armsBodyHeight;
}

float wrapHalfTurn(float degrees) {
	return degrees - 360.f * std::round(degrees / 360.f);
}

Vec3f armsPerpendicular(const Vec3f & axis) {
	const Vec3f other = (std::abs(axis.x) < 0.9f) ? Vec3f(1.f, 0.f, 0.f) : Vec3f(0.f, 1.f, 0.f);
	return glm::normalize(glm::cross(axis, other));
}

Vec3f jointPosition(const std::vector<glm::mat4> & world, int joint) {
	return (joint >= 0 && size_t(joint) < world.size()) ? Vec3f(world[size_t(joint)][3]) : Vec3f(0.f);
}

/*!
 * A glove that cannot be used: the shipped data is broken, so it is an error (a check's errors count goes red),
 * and status.json vr.body.customArms says why
 */
void gloveFailed(Hand hand, GloveRest & glove, const std::string & reason) {
	glove.error = reason;
	LogError << "[vr] arms: " << reason << " - the " << handName(hand) << " arm stays the Arx arm";
}

void loadGlove(Hand hand, GloveRest & glove) {

	glove.tried = true;
	const std::string file = std::string("vr/arms/Hand_Glove_low_") + (hand == Hand::Right ? "R" : "L") + ".gltf";
	// The resource system keeps every name in lower case (PakReader::addFiles)
	const std::string data = g_resources->read(util::toLowercase(file));
	if(data.empty()) {
		gloveFailed(hand, glove, file + " not found");
		return;
	}
	std::string error;
	if(!loadGltfSkinnedMesh(data.data(), data.size(), glove.mesh, error)) {
		gloveFailed(hand, glove, file + ": " + error);
		return;
	}
	const int wrist = findHandJoint(glove.mesh, HandJoint::Wrist);
	const int palm = findHandJoint(glove.mesh, HandJoint::Palm);
	const int tip = findHandJoint(glove.mesh, HandJoint::MiddleTip);
	const int thumb = findHandJoint(glove.mesh, HandJoint::ThumbProximal);
	if(wrist < 0 || palm < 0 || tip < 0 || thumb < 0) {
		gloveFailed(hand, glove, file + " lacks the Wrist, Palm, Middle_Tip or Thumb_Proximal joint");
		return;
	}
	glove.rig = buildHandRig(glove.mesh);
	if(!glove.rig.valid) {
		gloveFailed(hand, glove, file + " lacks a hand joint or a usable finger bone for the five fingers");
		return;
	}
	restWorldMatrices(glove.mesh, glove.restWorld);
	glove.wrist = jointPosition(glove.restWorld, wrist);
	glove.palm = jointPosition(glove.restWorld, palm);
	glove.tip = jointPosition(glove.restWorld, tip);
	glove.palmCentre = 0.5f * (jointPosition(glove.restWorld, glove.rig.joint[size_t(HandJoint::MiddleMetacarpal)])
	                           + jointPosition(glove.restWorld, glove.rig.joint[size_t(HandJoint::MiddleProximal)]));
	glove.knuckle = jointPosition(glove.restWorld, glove.rig.joint[size_t(HandJoint::MiddleProximal)]);
	glove.indexKnuckle = jointPosition(glove.restWorld, glove.rig.joint[size_t(HandJoint::IndexProximal)]);
	glove.littleKnuckle = jointPosition(glove.restWorld, glove.rig.joint[size_t(HandJoint::LittleProximal)]);

	// The hand's frame as the IK reads it from a model (long axis to the palm, lateral axis to the thumb)
	std::vector<Vec3f> vertices;
	vertices.reserve(glove.mesh.positions.size());
	for(const Vec3f & position : glove.mesh.positions) {
		vertices.push_back(position - glove.wrist);
	}
	const Vec3f palmLocal = glove.palm - glove.wrist;
	const Vec3f thumbLocal = jointPosition(glove.restWorld, thumb) - glove.wrist;
	const HandFrame frame = handFrameFromMesh(vertices, &palmLocal, &thumbLocal);
	if(!frame.valid) {
		gloveFailed(hand, glove, file + ": no usable hand frame");
		return;
	}
	glove.toGrip = handToGrip(frame);

	// The sleeve's wrist axes in the glove's mesh space: along the hand (the glove points along mesh Z), the
	// cuff's thickness (mesh X) and its width (mesh Y, completing the frame)
	const Vec3f along(0.f, 0.f, palmLocal.z < 0.f ? -1.f : 1.f);
	const Vec3f thickness(1.f, 0.f, 0.f);
	const glm::mat3 axes(along, thickness, glm::cross(along, thickness));
	glove.sleeveAxes = glm::normalize(glm::quat_cast(axes));
	glove.cuff = glove.wrist + along * GloveCuffPastWrist;

	glove.loaded = true;
	LogInfo << "[vr] arms: " << file << ": " << glove.mesh.positions.size() << " vertices, "
	        << glove.mesh.indices.size() / 3 << " triangles, " << glove.mesh.skeleton.size() << " joints; wrist to palm "
	        << glm::length(palmLocal) * GloveUnitsPerMeter << " cm, palm to middle tip "
	        << glm::distance(glove.palm, glove.tip) * GloveUnitsPerMeter << " cm, the hand along mesh "
	        << (along.z < 0.f ? "-Z" : "+Z");
}

//! MRTK3's Grab (data/core/vr/hands, BSD-3): the controls' closed end; the resource system's names are lower case
const char * const ArmsGrabShape = "vr/hands/articulatedhandpose_grab.json";

//! Where `grip capture` keeps \a hand's hilt pose
fs::path hiltPosePath(Hand hand) {
	return fs::getUserDir() / (std::string("vr-hilt-pose-") + handName(hand) + ".json");
}

//! The configured hilt angles (cfg.ini vr_hilt_*)
HiltAngles configuredHiltAngles() {
	HiltAngles angles;
	angles.knuckle = config.input.vrHiltKnuckle;
	angles.middle = config.input.vrHiltMiddle;
	angles.last = config.input.vrHiltLast;
	angles.thumb = config.input.vrHiltThumb;
	return clampHiltAngles(angles);
}

/*!
 * The hand shapes of \a hand (#385.7), read once: the open hand, MRTK3's Grab (recorded for the right hand: the left
 * is its mirror) and the captured hilt pose when <user dir> has one. A missing Grab is the shipped data broken: an
 * error, and the controls close nothing
 */
void loadHandShapes(Hand hand, CustomArm & arm) {

	arm.shapesLoaded = true;
	arm.shapes.open = palmRelativeJoints(synthesizeHandJoints(hand, HandShape()));

	std::string error;
	const std::string grab = g_resources->read(ArmsGrabShape);
	HandJoints joints;
	if(grab.empty()) {
		LogError << "[vr] arms: " << ArmsGrabShape << " not found - the " << handName(hand)
		         << " hand's controls close no finger";
	} else if(!loadHandShape(grab.data(), grab.size(), joints, error)) {
		LogError << "[vr] arms: " << ArmsGrabShape << ": " << error << " - the " << handName(hand)
		         << " hand's controls close no finger";
	} else {
		arm.shapes.grab = palmRelativeJoints(hand == Hand::Right ? joints : mirrorHandJoints(joints));
	}

	const fs::path captured = hiltPosePath(hand);
	if(fs::exists(captured)) {
		const std::string data = fs::read(captured);
		if(loadHandShape(data.data(), data.size(), joints, error)) {
			arm.shapes.captured = palmRelativeJoints(joints);
			LogInfo << "[vr] arms: the " << handName(hand) << " hand closes on the hilt pose captured in " << captured;
		} else {
			LogWarning << "[vr] arms: " << captured << ": " << error << " - the " << handName(hand)
			           << " hand closes on the default hilt pose";
		}
	}
}

//! \a current turned toward \a target by at most \a step radians per joint (all palm-relative)
HandJoints approachJoints(const HandJoints & current, const HandJoints & target, float step) {
	if(!current.valid || !target.valid) {
		return target;
	}
	HandJoints joints = target;
	for(size_t j = 0; j < HandJointCount; j++) {
		const glm::quat & from = current.orientation[j];
		const glm::quat & to = target.orientation[j];
		const float angle = 2.f * std::acos(std::min(std::abs(glm::dot(from, to)), 1.f));
		if(angle > step && angle > 1e-6f) {
			joints.orientation[j] = glm::normalize(glm::slerp(from, to, step / angle));
		}
	}
	return joints;
}

/*!
 * The glove's joints in the world: each joint's \a local composed with its parents' (a parent may come after its
 * child in the file), placed by \a toWorld
 */
void composeGloveJoints(const SkinnedMesh & mesh, const std::vector<glm::mat4> & local, const glm::mat4 & toWorld,
                        std::vector<glm::mat4> & world) {
	world.resize(mesh.skeleton.size());
	for(size_t j = 0; j < mesh.skeleton.size(); j++) {
		glm::mat4 matrix = local[j];
		for(int parent = mesh.skeleton[j].parent; parent >= 0; parent = mesh.skeleton[size_t(parent)].parent) {
			matrix = local[size_t(parent)] * matrix;
		}
		world[j] = toWorld * matrix;
	}
}

void buildArmSleeve(CustomArm & arm, float upperLength, float forearmLength, float startAt) {
	SleeveParams params;
	params.upperLength = upperLength;
	params.forearmLength = forearmLength;
	params.startAt = startAt;
	arm.sleeve = buildSleeve(params);
	arm.sleeveWork = arm.sleeve;
	arm.builtUpper = upperLength;
	arm.builtForearm = forearmLength;
	arm.builtStart = startAt;
	arm.lastRing.clear();
	const float end = upperLength + forearmLength;
	for(size_t v = 0; v < arm.sleeve.positions.size(); v++) {
		if(arm.sleeve.positions[v].x > end - 1e-3f) {
			arm.lastRing.push_back(v);
		}
	}
}

/*!
 * The pole that puts the elbow of \a unarmed (the solve on the base pole) vr_arm_weapon_elbow_drop of its angle
 * nearer its lowest position about the shoulder-target line. The solve swings the elbow from its pole by a share of the hand's
 * roll measured against that pole, so turning the pole by an angle moves the elbow by (1 - elbowShare) of it: the
 * pole turns by the wanted move over that
 */
Vec3f weaponElbowHint(const ArmRollPose & unarmed, const Vec3f & shoulder, const Vec3f & target,
                      const LogicalArm & logical) {
	const Vec3f line = target - shoulder;
	if(glm::length(line) < 1e-4f) {
		return logical.elbowHint;
	}
	const Vec3f axis = glm::normalize(line);
	const Vec3f down(0.f, 1.f, 0.f); // engine y is down
	const Vec3f lowest = down - glm::dot(down, axis) * axis;
	const Vec3f elbow = (unarmed.pose.elbow - shoulder) - glm::dot(unarmed.pose.elbow - shoulder, axis) * axis;
	if(glm::length(lowest) < 1e-3f || glm::length(elbow) < 1e-3f) {
		return logical.elbowHint; // the arm along the vertical, or straight: no lower side to turn to
	}
	const float offLowest = std::atan2(glm::dot(glm::cross(lowest, elbow), axis), glm::dot(lowest, elbow));
	const float kept = std::max(1.f - glm::clamp(logical.roll.elbowShare, 0.f, 1.f), 0.1f);
	const float drop = clampWeaponElbowDrop(config.input.vrArmWeaponElbowDrop);
	return glm::angleAxis(-drop * offLowest / kept, axis) * logical.elbowHint;
}

//! A joint's world matrix: at \a position, turned by the frame \a axes (columns: along, inner side, bend axis)
glm::mat4 jointMatrix(const Vec3f & position, const glm::mat3 & axes, float scale = 1.f) {
	glm::mat4 matrix(axes * scale);
	matrix[3] = glm::vec4(position, 1.f);
	return matrix;
}

glm::mat3 rolled(const glm::mat3 & axes, float degrees) {
	return glm::mat3(glm::angleAxis(glm::radians(degrees), axes[0])) * axes;
}

/*!
 * A texture of data/core/vr/arms (385.5), or a plain leather-brown one when the file is missing. Looked up by
 * name every frame: the engine deletes every texture container when it exits or reloads them.
 */
TextureContainer * armsTexture(const std::string & name) {
	if(TextureContainer * texture = TextureContainer::Find(name)) {
		return texture;
	}
	if(g_resources->getFile(res::path(name + ".png"))) {
		if(TextureContainer * texture = TextureContainer::Load(name)) {
			return texture;
		}
	}
	const std::string flatName = name + "_flat";
	if(TextureContainer * texture = TextureContainer::Find(flatName)) {
		return texture;
	}
	Image image;
	image.create(4, 4, Image::Format_R8G8B8);
	for(size_t i = 0; i < 16; i++) {
		std::copy(ArmsLeather, ArmsLeather + 3, image.getData() + i * 3);
	}
	TextureContainer * flat = new TextureContainer(flatName, TextureContainer::NoMipmap);
	flat->m_pTexture = GRenderer->createTexture();
	if(!flat->m_pTexture || !flat->m_pTexture->create(image, 0)) {
		return nullptr;
	}
	flat->m_size = flat->m_pTexture->getSize();
	flat->uv = Vec2f(1.f);
	flat->hd = Vec2f(0.5f / flat->m_size.x, 0.5f / flat->m_size.y);
	LogInfo << "[vr] arms: no " << name << ".png - drawn plain leather brown";
	return flat;
}

/*!
 * The glove's hand, joint by joint (#385.7), placed by \a gloveToWorld: a held weapon closes it on the hilt pose on
 * either hand (the captured one, else the default), else it takes the runtime's joints (conforming to the controller
 * where the runtime can), else the controls close each finger toward MRTK3's Grab. The glove's joints turn toward
 * that at a bounded speed, so a weapon taken or a source lost never snaps them. Needs status.grip and status.handle.
 * Also sets the glove's hilt frame (#385.10): \a fallbackHilt is the hilt's centre without a hilt curl
 */
void poseGloveHand(CustomArm & arm, const LogicalArm & logical, const glm::mat4 & gloveToWorld, const glm::quat & handQuat,
                   const Vec3f & fallbackHilt) {

	CustomArmStatus & status = arm.status;
	const GloveRest & glove = arm.glove;
	arm.lastJoints = logical.joints;
	CustomFingerInput fingerInput = logical.fingers;
	fingerInput.holds = logical.holdsWeapon;
	const CustomHandTarget handTarget = customHandTarget(fingerInput, logical.joints, arm.shapes);
	const float step = CurlDegreesPerSecond * logical.seconds;
	arm.current = approachJoints(arm.current, handTarget.joints, glm::radians(step));
	status.fingers = approachPose(status.fingers, handTarget.pose, step);
	status.fingerSource = handTarget.source;
	status.hiltPose = handTarget.hilt;
	poseHandFromJoints(glove.mesh, glove.rig, arm.current, arm.gloveLocal);
	composeGloveJoints(glove.mesh, arm.gloveLocal, gloveToWorld, arm.gloveJoints);
	skinMesh(glove.mesh, arm.gloveJoints, arm.handPositions, arm.handNormals);

	if(arm.hiltCurl.valid) {
		const Vec3f centre(gloveToWorld * glm::vec4(arm.hiltCurl.centre, 1.f));
		status.curlToHandle = glm::length(glm::cross(centre - status.grip, status.handle));
		status.curlAngle = glm::degrees(std::acos(std::min(std::abs(glm::dot(handQuat * arm.hiltCurl.axis, status.handle)), 1.f)));
	}
	// The glove's hilt frame (#385.10, research #453 F-485): what a held weapon is drawn from - the curl the closed
	// fingers wrap, where the glove is drawn (moved with it past the stretch limit, never to the hidden Arx hand)
	auto gloveWorld = [&](const Vec3f & point) { return Vec3f(gloveToWorld * glm::vec4(point, 1.f)); };
	status.hiltCentre = arm.hiltCurl.valid ? gloveWorld(arm.hiltCurl.centre) : fallbackHilt;
	status.hiltAxis = arm.hiltCurl.valid ? glm::normalize(handQuat * arm.hiltCurl.axis) : status.handle;
	const Vec3f knuckles = gloveWorld(glove.knuckle) - gloveWorld(glove.wrist);
	status.knuckles = glm::length(knuckles) > 1e-4f ? glm::normalize(knuckles) : armsPerpendicular(status.hiltAxis);
	status.hiltGrip = logical.gripOrientation;
	// Four fingers side by side: the index to the little knuckle spans three of their widths
	status.handWidth = glm::distance(gloveWorld(glove.indexKnuckle), gloveWorld(glove.littleKnuckle)) * (4.f / 3.f);
	status.hiltFrame = true;
	status.palm = gloveWorld(glove.palm);
	status.tipLength = glm::distance(status.palm, gloveWorld(glove.tip));
}

} // anonymous namespace

ArmsModel parseArmsModel(std::string_view /* text */) {
	// Only the default is a player's choice (#466): a model the arms lever or the channel saved before reads as it
	return ArmsModelTable[0].model;
}

bool parseArmsModelStrict(std::string_view text, ArmsModel & model) {
	const std::string name = util::toLowercase(std::string(text));
	for(const ArmsModelInfo & info : ArmsModelTable) {
		if(name == info.name) {
			model = info.model;
			return true;
		}
	}
	return false;
}

const char * armsModelName(ArmsModel model) {
	return armsModelInfo(model).name;
}

std::string armsModelNames() {
	std::string names;
	for(size_t i = 0; i < ArmsModelCount; i++) {
		names += (i == 0) ? "" : (i + 1 == ArmsModelCount) ? " or " : ", ";
		names += ArmsModelTable[i].name;
	}
	return names;
}

const ArmsModelInfo & armsModelInfo(ArmsModel model) {
	return ArmsModelTable[armsModelIndex(model)];
}

float clampWeaponElbowDrop(float share) {
	return std::isfinite(share) ? glm::clamp(share, 0.f, 1.f) : DefaultWeaponElbowDrop;
}

float clampHandScale(float scale) {
	return std::isfinite(scale) ? glm::clamp(scale, MinHandScale, MaxHandScale) : DefaultHandScale;
}

ArmsModel armsModel() {
	// What is set now: cfg.ini's choice, or a model the test channel switched to
	ArmsModel model = ArmsModelTable[0].model;
	parseArmsModelStrict(config.input.vrArmsModel, model);
	return model;
}

const ArmsModelInfo & armsModelInfo() {
	return armsModelInfo(armsModel());
}

void setArmsModel(ArmsModel model, const char * by) {
	const char * name = armsModelName(model);
	if(config.input.vrArmsModel != name) {
		config.input.vrArmsModel = name;
		config.save();
		LogInfo << "[vr] arms: model " << name << " (" << by << ")";
		gameevent::Event("vr.arms.model")("model", name)("by", by);
	}
}

ClassicHand parseClassicHand(std::string_view text) {
	ClassicHand hand = ClassicHand::Arx;
	parseClassicHandStrict(text, hand);
	return hand;
}

bool parseClassicHandStrict(std::string_view text, ClassicHand & hand) {
	const std::string name = util::toLowercase(std::string(text));
	if(name == "arx") {
		hand = ClassicHand::Arx;
	} else if(name == "glove") {
		hand = ClassicHand::Glove;
	} else {
		return false;
	}
	return true;
}

const char * classicHandName(ClassicHand hand) {
	return (hand == ClassicHand::Glove) ? "glove" : "arx";
}

ClassicHand classicHand() {
	return parseClassicHand(config.input.vrClassicHand);
}

void setClassicHand(ClassicHand hand, const char * by) {
	const char * name = classicHandName(hand);
	if(config.input.vrClassicHand != name) {
		config.input.vrClassicHand = name;
		config.save();
		LogInfo << "[vr] arms: classic hand " << name << " (" << by << ")";
		gameevent::Event("vr.arms.classicHand")("hand", name)("by", by);
	}
}

void setHandScale(float scale, const char * by) {
	const float clamped = clampHandScale(scale);
	if(config.input.vrHandScale != clamped) {
		config.input.vrHandScale = clamped;
		config.save();
		LogInfo << "[vr] arms: hand scale " << clamped << " (" << by << ")";
		gameevent::Event("vr.arms.handScale")("scale", clamped)("by", by);
	}
}

void resetCustomArms() {
	for(CustomArm & arm : g_customArms) {
		arm.drawn = false;
		arm.atArxWrist = false;
		arm.status.drawn = false;
		arm.status.sleeveDrawn = false;
		arm.status.upperArmDrawn = false;
		arm.status.fingerSource = CustomFingerSource::None;
		arm.status.hiltPose = HiltPoseSource::None;
		arm.status.hiltFrame = false;
	}
}

bool poseCustomArm(Hand hand, const LogicalArm & logical) {

	CustomArm & arm = g_customArms[size_t(hand)];
	CustomArmStatus & status = arm.status;
	status.handScale = clampHandScale(config.input.vrHandScale);
	arm.drawn = false;
	status.drawn = false;
	status.sleeveDrawn = false;
	status.upperArmDrawn = false;
	status.hiltPose = HiltPoseSource::None;
	status.hiltFrame = false;
	const ArmsModelInfo & info = armsModelInfo();
	const bool atArxWrist = info.classicSkin && classicHand() == ClassicHand::Glove;
	if(!info.glove && !atArxWrist) {
		return false;
	}
	if(!arm.glove.tried) {
		loadGlove(hand, arm.glove);
	}
	if(!arm.glove.loaded || logical.upperLength <= 0.f || logical.forearmLength <= 0.f) {
		return false;
	}
	const GloveRest & glove = arm.glove;
	const float scale = status.handScale;
	const float gloveScale = GloveUnitsPerMeter * scale;

	// The hand's shapes (#385.7): read once; the hilt pose rebuilt when its configured angles change, with the axis
	// its closed fingers wrap on the glove
	if(!arm.shapesLoaded) {
		loadHandShapes(hand, arm);
	}
	const HiltAngles angles = configuredHiltAngles();
	if(!arm.hiltBuilt || angles.knuckle != arm.hiltBuiltFor.knuckle || angles.middle != arm.hiltBuiltFor.middle
	   || angles.last != arm.hiltBuiltFor.last || angles.thumb != arm.hiltBuiltFor.thumb) {
		arm.shapes.hilt = palmRelativeJoints(hiltPose(hand, DefaultHiltRadius, angles));
		std::vector<glm::mat4> hiltLocal;
		poseHandFromJoints(glove.mesh, glove.rig, arm.shapes.hilt, hiltLocal);
		arm.hiltCurl = gloveCurlAxis(glove.mesh, glove.rig, hiltLocal);
		arm.hiltBuiltFor = angles;
		arm.hiltBuilt = true;
	}

	// The glove on the controller's handle (#385.7, research #453 F-483: the grip is the handle's centre, outside the
	// hand): turned by the least rotation that lays its hilt pose's curl along the handle and placed with that curl's
	// centre on the grip, so the handle - and a held hilt - runs through the closed fingers, not through the palm.
	// Without a curl the palm's centre lies the handle's radius plus the palm's radius behind the grip
	const glm::quat handQuat = glm::normalize(logical.gripOrientation * gloveOnHandle(arm.hiltCurl, glove.toGrip));
	Vec3f gloveWrist(0.f);
	if(arm.hiltCurl.valid) {
		gloveWrist = logical.grip - handQuat * ((arm.hiltCurl.centre - glove.wrist) * gloveScale);
	} else {
		const Vec3f palmAt = palmBehindGrip(hand, logical.grip, logical.gripOrientation, IndexHandleRadius * UnitsPerMeter,
		                                    GlovePalmRadius * gloveScale);
		gloveWrist = palmAt - handQuat * ((glove.palmCentre - glove.wrist) * gloveScale);
	}
	status.palmToGrip = glm::distance(gloveWrist + handQuat * ((glove.palmCentre - glove.wrist) * gloveScale), logical.grip);
	status.grip = logical.grip;
	status.handle = logical.gripOrientation * Vec3f(0.f, 0.f, 1.f); // the grip's Z in engine axes

	if(atArxWrist) {
		// The classic arm's glove (#466.2, research F-281): the Arx arm is drawn to its wrist, so the glove takes the
		// Arx hand's place - turned by the same grip frame as the Arx hand (D-032, without the curl's turn onto the
		// handle, which tilts it some 30 degrees off the Arx wrist ring) and with its cuff on the Arx wrist joint. The
		// Arx forearm ends in its cuff, with no sleeve and no solve of its own
		const glm::quat wristQuat = glm::normalize(logical.gripOrientation * glove.toGrip);
		glm::mat4 atWrist = glm::translate(glm::mat4(1.f), logical.arxWrist) * glm::mat4_cast(wristQuat);
		atWrist = glm::scale(atWrist, Vec3f(gloveScale));
		atWrist = glm::translate(atWrist, -glove.cuff);
		status.palmToGrip = glm::distance(Vec3f(atWrist * glm::vec4(glove.palmCentre, 1.f)), logical.grip);
		poseGloveHand(arm, logical, atWrist, wristQuat, logical.grip);
		status.drawn = true;
		status.wrist = logical.arxWrist;
		status.wristGap = -1.f; // measured on the Arx wrist ring once the classic arm is skinned (noteArxWristRing)
		status.vertices = arm.handPositions.size();
		status.triangles = glove.mesh.indices.size() / 3;
		arm.atArxWrist = true;
		arm.drawn = true;
		return true;
	}

	glm::mat4 gloveToWorld = glm::translate(glm::mat4(1.f), gloveWrist) * glm::mat4_cast(handQuat);
	gloveToWorld = glm::scale(gloveToWorld, Vec3f(gloveScale));
	gloveToWorld = glm::translate(gloveToWorld, -glove.wrist);
	const Vec3f target = Vec3f(gloveToWorld * glm::vec4(glove.cuff, 1.f));

	// The fit (#385.6): the arm the character height setting makes, until this hand's reach is measured -
	// sampled while the wrist target is about as far from the shoulder as that arm is long (the arm held out).
	// Samples count up to MaxExtendedReach (1.3) of the height's arm while the arm stretches only to 1 +
	// vr_arm_max_stretch of its fit: a long reach held for a while lengthens the fit - intended, for a player
	// taller than the height setting says
	const Vec3f shoulder = logical.shoulder;
	const float toTarget = glm::distance(shoulder, target);
	const float height = armsBodyHeight();
	ArmLengths fit;
	if(height > 0.f) {
		const ArmLengths fromHeight = armLengthsFromHeight(height);
		if(isExtendedReach(toTarget, fromHeight.upper + fromHeight.forearm)) {
			arm.reach.add(toTarget);
		}
		fit = fromHeight;
		status.fitSource = "height";
	} else {
		fit.upper = logical.upperLength;
		fit.forearm = logical.forearmLength;
		status.fitSource = "arx";
	}
	if(arm.reach.measured()) {
		fit = armLengthsFromReach(arm.reach.reach());
		status.fitSource = "reach";
	}
	// The arm grows or shrinks to a new fit over a moment instead of popping (the first fit is taken at once)
	if(arm.lengths.upper <= 0.f || arm.lengths.forearm <= 0.f) {
		arm.lengths = fit;
	} else {
		const float seconds = std::min(toMsf(g_platformTime.lastFrameDuration()) / 1000.f, 0.25f);
		const float approach = 1.f - std::exp(-3.f * seconds / ArmsFitEaseSeconds);
		arm.lengths.upper += (fit.upper - arm.lengths.upper) * approach;
		arm.lengths.forearm += (fit.forearm - arm.lengths.forearm) * approach;
	}
	const ArmLengths lengths = arm.lengths;
	// The model draws the whole sleeve, the forearm's part or only the cuff at the wrist (#385.8): the same arm is
	// posed, the sleeve's mesh starts further along it
	const float startAt = sleeveStartAt(info, lengths.upper, lengths.forearm);
	if(arm.sleeve.positions.empty() || std::abs(arm.builtUpper - lengths.upper) > 0.1f
	   || std::abs(arm.builtForearm - lengths.forearm) > 0.1f || std::abs(arm.builtStart - startAt) > 0.1f) {
		buildArmSleeve(arm, lengths.upper, lengths.forearm, startAt);
	}

	// The custom arm's own solve: from the Arx shoulder (never slid) to the glove's cuff, both segments stretched
	// by at most vr_arm_max_stretch; a hand holding a weapon drops its elbow (F-300)
	const float stretch = armStretch(toTarget, lengths, clampArmMaxStretch(config.input.vrArmMaxStretch));
	const float stretchedUpper = lengths.upper * (1.f + stretch);
	const float stretchedForearm = lengths.forearm * (1.f + stretch);
	const ArmIKParams noSlide;
	ArmRollState unarmedState = arm.roll;
	const ArmRollPose unarmed = solveArmRoll(shoulder, target, logical.hand, stretchedUpper, stretchedForearm,
	                                         logical.elbowHint, logical.rest, noSlide, logical.roll,
	                                         logical.holdsWeapon ? unarmedState : arm.roll);
	ArmRollPose solved = unarmed;
	float elbowDrop = 0.f;
	if(logical.holdsWeapon) {
		solved = solveArmRoll(shoulder, target, logical.hand, stretchedUpper, stretchedForearm,
		                      weaponElbowHint(unarmed, shoulder, target, logical), logical.rest, noSlide, logical.roll,
		                      arm.roll);
		elbowDrop = solved.pose.elbow.y - unarmed.pose.elbow.y; // engine y is down
	}

	// Past the stretch limit the hand stops short of the controller, on the line from the shoulder
	Vec3f cuff = target;
	if(solved.pose.stretched) {
		const Vec3f back = solved.pose.wrist - target;
		gloveToWorld = glm::translate(glm::mat4(1.f), back) * gloveToWorld;
		cuff = solved.pose.wrist;
	}

	// The upper arm's bend frame: along the custom upper arm, the inner side towards the drawn forearm
	// (the elbow to the glove's cuff), the bend axis completing it
	const Vec3f elbow = solved.pose.elbow;
	const Vec3f upperAxis = (glm::length(elbow - shoulder) > 1e-4f) ? glm::normalize(elbow - shoulder) : Vec3f(0.f, 1.f, 0.f);
	const Vec3f forearmVector = cuff - elbow;
	const float forearmDrawn = glm::length(forearmVector);
	const Vec3f forearmAxis = forearmDrawn > 1e-4f ? forearmVector / forearmDrawn : upperAxis;
	Vec3f inner = forearmAxis - glm::dot(forearmAxis, upperAxis) * upperAxis;
	if(glm::length(inner) < 1e-3f) {
		// Straight: the inside of the bend is opposite where the Arx elbow sticks out at rest
		inner = solved.upper * -logical.restElbow;
		inner -= glm::dot(inner, upperAxis) * upperAxis;
		inner = (glm::length(inner) > 1e-4f) ? inner : armsPerpendicular(upperAxis);
	}
	inner = glm::normalize(inner);
	const Vec3f bendAxis = glm::cross(upperAxis, inner);
	const float bend = glm::degrees(std::atan2(glm::dot(forearmAxis, inner), glm::dot(forearmAxis, upperAxis)));
	const float drawnBend = std::min(bend, ArmsMaxDrawnBend);
	const glm::mat3 upperFrame(upperAxis, inner, bendAxis);
	auto bent = [&](float degrees) {
		const float r = glm::radians(degrees);
		return glm::mat3(std::cos(r) * upperAxis + std::sin(r) * inner, -std::sin(r) * upperAxis + std::cos(r) * inner,
		                 bendAxis);
	};
	const glm::mat3 elbowFrame = bent(drawnBend);
	const glm::mat3 forearmFrame = bent(bend); // its first axis is forearmAxis

	// The wrist: the glove's cuff with the cuff's axes and the hand's scale; the forearm's twist is the roll from the
	// elbow's frame to it, on the same turn as the solve's unwrapped roll (a half turn and past keeps its side)
	const glm::quat wristQuat = glm::normalize(handQuat * glove.sleeveAxes);
	const float measured = twistAboutAxis(glm::inverse(glm::quat_cast(forearmFrame)) * wristQuat, Vec3f(1.f, 0.f, 0.f));
	const float forearmTwist = solved.residualRoll + wrapHalfTurn(measured - solved.residualRoll);

	// The sleeve's joints (vr/ArmMesh.h SleeveJoint)
	const float upperTwistAt = SleeveParams().upperTwistAt * glm::distance(shoulder, elbow);
	arm.sleeveJoints.resize(SleeveJointCount);
	arm.sleeveJoints[SleeveShoulder] = jointMatrix(shoulder - upperAxis * ArmsShoulderSink, upperFrame);
	arm.sleeveJoints[SleeveUpperTwist] = jointMatrix(shoulder + upperAxis * upperTwistAt, rolled(upperFrame, solved.upperRoll));
	arm.sleeveJoints[SleeveElbow] = jointMatrix(elbow, elbowFrame);
	arm.sleeveJoints[SleeveForearmTwist1] = jointMatrix(elbow + forearmVector / 3.f, rolled(forearmFrame, forearmTwist / 3.f));
	arm.sleeveJoints[SleeveForearmTwist2] = jointMatrix(elbow + forearmVector * (2.f / 3.f),
	                                                    rolled(forearmFrame, forearmTwist * (2.f / 3.f)));
	arm.sleeveJoints[SleeveWrist] = jointMatrix(cuff, glm::mat3_cast(wristQuat), scale);

	arm.sleeveWork.positions = arm.sleeve.positions;
	applyElbowCorrective(arm.sleeve, drawnBend, ArmsElbowCorrective, arm.sleeveWork.positions);
	skinMesh(arm.sleeveWork, arm.sleeveJoints, arm.sleevePositions, arm.sleeveNormals);
	recomputeNormals(arm.sleeve, arm.sleevePositions, arm.sleeveNormals);

	poseGloveHand(arm, logical, gloveToWorld, handQuat, logical.grip + (cuff - target));

	Vec3f ring(0.f);
	for(size_t v : arm.lastRing) {
		ring += arm.sleevePositions[v];
	}
	ring /= float(std::max<size_t>(arm.lastRing.size(), 1));

	status.drawn = true;
	status.wrist = cuff;
	status.sleeveDrawn = info.sleeve;
	status.upperArmDrawn = info.sleeve && info.upperArm;
	status.upperLength = lengths.upper;
	status.forearmLength = glm::clamp(lengths.upper + lengths.forearm - startAt, 0.f, lengths.forearm); // as drawn
	status.reach = arm.reach.reach();
	status.stretch = stretch;
	status.error = solved.pose.error;
	status.elbowDrop = elbowDrop;
	status.bend = bend;
	status.drawnBend = drawnBend;
	status.upperRoll = solved.upperRoll;
	status.forearmTwist = forearmTwist;
	status.wristGap = glm::distance(ring, cuff);
	status.vertices = arm.sleevePositions.size() + arm.handPositions.size();
	status.triangles = (arm.sleeve.indices.size() + glove.mesh.indices.size()) / 3;
	arm.drawn = true;
	return true;
}

CustomArmsLoad customArmsLoad() {
	CustomArmsLoad load;
	bool all = true;
	for(const CustomArm & arm : g_customArms) {
		if(!arm.glove.tried) {
			continue;
		}
		load.tried = true;
		all = all && arm.glove.loaded;
		if(!arm.glove.loaded && load.reason.empty()) {
			load.reason = arm.glove.error;
		}
	}
	load.loaded = load.tried && all;
	if(!load.tried) {
		load.reason = "not tried yet: no arm was posed with the custom model on";
	}
	return load;
}

bool captureHiltPose(Hand hand, std::string & result) {
	CustomArm & arm = g_customArms[size_t(hand)];
	if(!arm.lastJoints.valid) {
		result = std::string("the ") + handName(hand) + " hand has no runtime joints this frame (custom arms off, hand"
		         " not tracked or the runtime gives no hand joints)";
		return false;
	}
	const HandJoints joints = palmRelativeJoints(arm.lastJoints);
	const fs::path path = hiltPosePath(hand);
	if(!fs::write(path, handShapeJson(joints))) {
		result = "could not write " + path.string();
		LogWarning << "[vr] arms: grip capture: " << result;
		return false;
	}
	arm.shapes.captured = joints;
	result = path.string();
	LogInfo << "[vr] arms: grip capture: the " << handName(hand) << " hand's joints are its hilt pose from now on ("
	        << path << ")";
	gameevent::Event("vr.hands.hiltPose")("hand", handName(hand))("state", "captured");
	return true;
}

void clearHiltPose(Hand hand) {
	CustomArm & arm = g_customArms[size_t(hand)];
	arm.shapes.captured = HandJoints();
	const fs::path path = hiltPosePath(hand);
	if(fs::exists(path)) {
		fs::remove(path);
	}
	LogInfo << "[vr] arms: grip capture clear: the " << handName(hand) << " hand closes on the default hilt pose";
	gameevent::Event("vr.hands.hiltPose")("hand", handName(hand))("state", "default");
}

bool customArmDrawn(Hand hand) {
	const CustomArm & arm = g_customArms[size_t(hand)];
	return arm.drawn && !arm.atArxWrist;
}

bool arxHandHidden(Hand hand) {
	const CustomArm & arm = g_customArms[size_t(hand)];
	return arm.drawn && arm.atArxWrist;
}

void noteArxWristRing(Hand hand, const Vec3f & centre) {
	CustomArm & arm = g_customArms[size_t(hand)];
	if(arm.drawn && arm.atArxWrist) {
		arm.status.wristGap = glm::distance(centre, arm.status.wrist);
	}
}

const CustomArmStatus & customArmStatus(Hand hand) {
	CustomArmStatus & status = g_customArms[size_t(hand)].status;
	status.handScale = clampHandScale(config.input.vrHandScale);
	return status;
}

size_t customArmMeshes(CustomArmMesh (&meshes)[4]) {
	size_t count = 0;
	for(CustomArm & arm : g_customArms) {
		if(!arm.drawn) {
			continue;
		}
		// The sleeve as the model built it: whole, the forearm's part, or only the cuff closing the glove (#385.8); none
		// for the glove at the classic arm's wrist, the Arx forearm ends in its cuff (#466.2)
		if(!arm.atArxWrist) {
			CustomArmMesh & sleeve = meshes[count++];
			sleeve.positions = &arm.sleevePositions;
			sleeve.normals = &arm.sleeveNormals;
			sleeve.uvs = &arm.sleeve.uvs;
			sleeve.indices = &arm.sleeve.indices;
			sleeve.texture = armsTexture("vr/arms/sleeve");
		}
		CustomArmMesh & hand = meshes[count++];
		hand.positions = &arm.handPositions;
		hand.normals = &arm.handNormals;
		hand.uvs = &arm.glove.mesh.uvs;
		hand.indices = &arm.glove.mesh.indices;
		hand.texture = armsTexture("vr/arms/glove");
	}
	return count;
}

} // namespace vr
