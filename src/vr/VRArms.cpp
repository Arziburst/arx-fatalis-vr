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
#include "io/log/Logger.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourcePath.h"
#include "script/Script.h"
#include "util/String.h"
#include "vr/ArmIK.h"
#include "vr/ArmMesh.h"
#include "vr/ArmRoll.h"

namespace vr {

namespace {

//! How far the sleeve's top ring sits inside the torso, back along the upper arm from the shoulder joint (units)
const float ArmsShoulderSink = 1.5f;
//! The sleeve's elbow is drawn bent at most this much (degrees, research row F-310); the forearm's twist joints
//! still point at the hand, so a tighter bend folds over the first third of the forearm instead of the elbow ring
const float ArmsMaxDrawnBend = 150.f;
//! The elbow corrective's strength (units at a full fold, vr/ArmMesh.h applyElbowCorrective)
const float ArmsElbowCorrective = 1.5f;
//! The glove's cuff opening is this far past its Wrist joint towards the fingers (meters, 385.1's measure)
const float GloveCuffPastWrist = 0.0014f;
//! The glove meshes are in meters
const float GloveUnitsPerMeter = 100.f;
//! The colour of a missing texture: plain leather brown
const u8 ArmsLeather[3] = { 92, 60, 38 };

//! The playground's arms lever counts its pulls in this global script variable (scripts/dev/playground/build.mjs)
const char * const ArmsLeverPulls = "#pg_arms_pulls";

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
	Vec3f cuff = Vec3f(0.f);     //!< the centre of the cuff opening
	glm::quat toGrip = quat_identity(); //!< the mesh's rest frame to the grip frame (vr/ArmIK.h handToGrip)
	glm::quat sleeveAxes = quat_identity(); //!< the sleeve's wrist axes (along, thickness, width) in mesh space
};

struct CustomArm {
	GloveRest glove;
	SkinnedMesh sleeve;            //!< at rest, built for the logical arm's bone lengths
	SkinnedMesh sleeveWork;        //!< the same with this frame's elbow corrective
	float builtUpper = 0.f;
	float builtForearm = 0.f;
	std::vector<glm::mat4> sleeveJoints;
	std::vector<glm::mat4> gloveJoints;
	std::vector<Vec3f> sleevePositions;
	std::vector<Vec3f> sleeveNormals;
	std::vector<Vec3f> handPositions;
	std::vector<Vec3f> handNormals;
	std::vector<size_t> lastRing;  //!< the sleeve's vertices on its last ring
	bool drawn = false;
	CustomArmStatus status;
};

std::array<CustomArm, HandCount> g_customArms;
long g_armsLeverPulls = -1; //!< the pulls seen last, -1 outside the playground

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
	restWorldMatrices(glove.mesh, glove.restWorld);
	glove.wrist = jointPosition(glove.restWorld, wrist);
	glove.palm = jointPosition(glove.restWorld, palm);
	glove.tip = jointPosition(glove.restWorld, tip);

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

void buildArmSleeve(CustomArm & arm, float upperLength, float forearmLength) {
	SleeveParams params;
	params.upperLength = upperLength;
	params.forearmLength = forearmLength;
	arm.sleeve = buildSleeve(params);
	arm.sleeveWork = arm.sleeve;
	arm.builtUpper = upperLength;
	arm.builtForearm = forearmLength;
	arm.lastRing.clear();
	const float end = upperLength + forearmLength;
	for(size_t v = 0; v < arm.sleeve.positions.size(); v++) {
		if(arm.sleeve.positions[v].x > end - 1e-3f) {
			arm.lastRing.push_back(v);
		}
	}
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

} // anonymous namespace

ArmsModel parseArmsModel(std::string_view text) {
	return (util::toLowercase(std::string(text)) == "arx") ? ArmsModel::Arx : ArmsModel::Custom;
}

bool parseArmsModelStrict(std::string_view text, ArmsModel & model) {
	const std::string name = util::toLowercase(std::string(text));
	if(name == "custom") {
		model = ArmsModel::Custom;
	} else if(name == "arx") {
		model = ArmsModel::Arx;
	} else {
		return false;
	}
	return true;
}

const char * armsModelName(ArmsModel model) {
	return model == ArmsModel::Arx ? "arx" : "custom";
}

float clampHandScale(float scale) {
	return std::isfinite(scale) ? glm::clamp(scale, MinHandScale, MaxHandScale) : DefaultHandScale;
}

ArmsModel armsModel() {
	return parseArmsModel(config.input.vrArmsModel);
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

void setHandScale(float scale, const char * by) {
	const float clamped = clampHandScale(scale);
	if(config.input.vrHandScale != clamped) {
		config.input.vrHandScale = clamped;
		config.save();
		LogInfo << "[vr] arms: hand scale " << clamped << " (" << by << ")";
		gameevent::Event("vr.arms.handScale")("scale", clamped)("by", by);
	}
}

void followArmsLever() {
	if(!isPlaygroundArea(g_currentArea)) {
		g_armsLeverPulls = -1;
		return;
	}
	const long pulls = GETVarValueLong(svar, ArmsLeverPulls);
	if(g_armsLeverPulls >= 0 && pulls > g_armsLeverPulls && (pulls - g_armsLeverPulls) % 2 == 1) {
		setArmsModel(armsModel() == ArmsModel::Custom ? ArmsModel::Arx : ArmsModel::Custom, "the playground lever");
	}
	g_armsLeverPulls = pulls; // first sight (a level just loaded) or fewer (a new game): nothing pulled yet
}

void resetCustomArms() {
	for(CustomArm & arm : g_customArms) {
		arm.drawn = false;
		arm.status.drawn = false;
	}
}

bool poseCustomArm(Hand hand, const LogicalArm & logical) {

	CustomArm & arm = g_customArms[size_t(hand)];
	CustomArmStatus & status = arm.status;
	status.handScale = clampHandScale(config.input.vrHandScale);
	arm.drawn = false;
	status.drawn = false;
	if(armsModel() != ArmsModel::Custom) {
		return false;
	}
	if(!arm.glove.tried) {
		loadGlove(hand, arm.glove);
	}
	if(!arm.glove.loaded || logical.upperLength <= 0.f || logical.forearmLength <= 0.f) {
		return false;
	}
	if(arm.sleeve.positions.empty() || std::abs(arm.builtUpper - logical.upperLength) > 0.1f
	   || std::abs(arm.builtForearm - logical.forearmLength) > 0.1f) {
		buildArmSleeve(arm, logical.upperLength, logical.forearmLength);
	}
	const GloveRest & glove = arm.glove;
	const float scale = status.handScale;
	const float gloveScale = GloveUnitsPerMeter * scale;

	// The glove: its palm joint on the grip, the controller's orientation through the hand frame
	const glm::quat handQuat = glm::normalize(logical.gripOrientation * glove.toGrip);
	const Vec3f gloveWrist = logical.grip - handQuat * ((glove.palm - glove.wrist) * gloveScale);
	glm::mat4 gloveToWorld = glm::translate(glm::mat4(1.f), gloveWrist) * glm::mat4_cast(handQuat);
	gloveToWorld = glm::scale(gloveToWorld, Vec3f(gloveScale));
	gloveToWorld = glm::translate(gloveToWorld, -glove.wrist);
	const Vec3f cuff = Vec3f(gloveToWorld * glm::vec4(glove.cuff, 1.f));

	// The upper arm's bend frame: along the logical upper arm, the inner side towards the drawn forearm
	// (the elbow to the glove's cuff), the bend axis completing it
	const Vec3f shoulder = logical.shoulder;
	const Vec3f elbow = logical.elbow;
	const Vec3f upperAxis = (glm::length(elbow - shoulder) > 1e-4f) ? glm::normalize(elbow - shoulder) : Vec3f(0.f, 1.f, 0.f);
	const Vec3f forearmVector = cuff - elbow;
	const float forearmDrawn = glm::length(forearmVector);
	const Vec3f forearmAxis = forearmDrawn > 1e-4f ? forearmVector / forearmDrawn : upperAxis;
	Vec3f inner = forearmAxis - glm::dot(forearmAxis, upperAxis) * upperAxis;
	if(glm::length(inner) < 1e-3f) {
		// Straight: the inside of the bend is opposite where the logical elbow sticks out
		inner = logical.upper * -logical.restElbow;
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
	// elbow's frame to it, on the same turn as the logical arm's unwrapped roll (a half turn and past keeps its side)
	const glm::quat wristQuat = glm::normalize(handQuat * glove.sleeveAxes);
	const float measured = twistAboutAxis(glm::inverse(glm::quat_cast(forearmFrame)) * wristQuat, Vec3f(1.f, 0.f, 0.f));
	const float forearmTwist = logical.residualRoll + wrapHalfTurn(measured - logical.residualRoll);

	// The sleeve's joints (vr/ArmMesh.h SleeveJoint)
	const float upperTwistAt = SleeveParams().upperTwistAt * glm::distance(shoulder, elbow);
	arm.sleeveJoints.resize(SleeveJointCount);
	arm.sleeveJoints[SleeveShoulder] = jointMatrix(shoulder - upperAxis * ArmsShoulderSink, upperFrame);
	arm.sleeveJoints[SleeveUpperTwist] = jointMatrix(shoulder + upperAxis * upperTwistAt, rolled(upperFrame, logical.upperRoll));
	arm.sleeveJoints[SleeveElbow] = jointMatrix(elbow, elbowFrame);
	arm.sleeveJoints[SleeveForearmTwist1] = jointMatrix(elbow + forearmVector / 3.f, rolled(forearmFrame, forearmTwist / 3.f));
	arm.sleeveJoints[SleeveForearmTwist2] = jointMatrix(elbow + forearmVector * (2.f / 3.f),
	                                                    rolled(forearmFrame, forearmTwist * (2.f / 3.f)));
	arm.sleeveJoints[SleeveWrist] = jointMatrix(cuff, glm::mat3_cast(wristQuat), scale);

	arm.sleeveWork.positions = arm.sleeve.positions;
	applyElbowCorrective(arm.sleeve, drawnBend, ArmsElbowCorrective, arm.sleeveWork.positions);
	skinMesh(arm.sleeveWork, arm.sleeveJoints, arm.sleevePositions, arm.sleeveNormals);
	recomputeNormals(arm.sleeve, arm.sleevePositions, arm.sleeveNormals);

	// The hand, rigid at its rest pose
	arm.gloveJoints.resize(glove.restWorld.size());
	for(size_t j = 0; j < glove.restWorld.size(); j++) {
		arm.gloveJoints[j] = gloveToWorld * glove.restWorld[j];
	}
	skinMesh(glove.mesh, arm.gloveJoints, arm.handPositions, arm.handNormals);

	Vec3f ring(0.f);
	for(size_t v : arm.lastRing) {
		ring += arm.sleevePositions[v];
	}
	ring /= float(std::max<size_t>(arm.lastRing.size(), 1));

	const Vec3f logicalForearm = logical.wrist - logical.elbow;
	const Vec3f logicalUpper = logical.elbow - logical.shoulder;
	status.drawn = true;
	status.palm = Vec3f(gloveToWorld * glm::vec4(glove.palm, 1.f));
	status.tipLength = glm::distance(status.palm, Vec3f(gloveToWorld * glm::vec4(glove.tip, 1.f)));
	status.wrist = cuff;
	status.bend = (glm::length(logicalForearm) > 1e-4f && glm::length(logicalUpper) > 1e-4f)
	              ? glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(logicalUpper), glm::normalize(logicalForearm)), -1.f, 1.f)))
	              : 0.f;
	status.drawnBend = drawnBend;
	status.upperRoll = logical.upperRoll;
	status.forearmTwist = forearmTwist;
	status.wristGap = glm::distance(ring, cuff);
	status.stretch = forearmDrawn / logical.forearmLength;
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

bool customArmDrawn(Hand hand) {
	return g_customArms[size_t(hand)].drawn;
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
		CustomArmMesh & sleeve = meshes[count++];
		sleeve.positions = &arm.sleevePositions;
		sleeve.normals = &arm.sleeveNormals;
		sleeve.uvs = &arm.sleeve.uvs;
		sleeve.indices = &arm.sleeve.indices;
		sleeve.texture = armsTexture("vr/arms/sleeve");
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
