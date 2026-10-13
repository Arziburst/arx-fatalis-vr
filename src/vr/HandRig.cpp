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

#include "vr/HandRig.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace vr {

namespace {

// Helpers are prefixed rig: arx is a unity build

Vec3f rigJointPosition(const std::vector<glm::mat4> & world, const HandRig & rig, HandJoint joint) {
	return Vec3f(world[size_t(rig.joint[size_t(joint)])][3]);
}

//! \a direction (mesh space) in the rest frame of the joint whose world matrix is \a world, unit
Vec3f rigToLocal(const glm::mat4 & world, const Vec3f & direction) {
	return glm::normalize(glm::inverse(glm::mat3(world)) * direction);
}

//! A unit axis across \a bone toward \a toward: turning \a bone about it by a positive angle moves it toward \a toward
bool rigAxisAcross(const Vec3f & bone, const Vec3f & toward, Vec3f & axis) {
	const Vec3f across = glm::cross(bone, toward);
	const float length = glm::length(across);
	if(!(length > 1e-6f)) {
		return false;
	}
	axis = across / length;
	return true;
}

glm::mat4 rigTurn(float degrees, const Vec3f & axis) {
	return glm::mat4_cast(glm::angleAxis(glm::radians(degrees), axis));
}

} // anonymous namespace

HandJoint flexingJoint(Finger finger, size_t joint) {
	if(finger == Finger::Thumb) {
		return HandJoint(size_t(HandJoint::ThumbMetacarpal) + joint);
	}
	return HandJoint(size_t(HandJoint::IndexProximal) + 5 * (size_t(finger) - 1) + joint);
}

HandRig buildHandRig(const SkinnedMesh & hand) {

	HandRig rig;
	for(size_t j = 0; j < HandJointCount; j++) {
		rig.joint[j] = findHandJoint(hand, HandJoint(j));
		if(rig.joint[j] < 0) {
			return rig;
		}
	}

	std::vector<glm::mat4> world;
	restWorldMatrices(hand, world);

	// The palm's normal: across the metacarpals' bases and along the middle metacarpal, on the Palm joint's side
	const Vec3f thumbSide = rigJointPosition(world, rig, HandJoint::IndexMetacarpal)
	                        - rigJointPosition(world, rig, HandJoint::LittleMetacarpal);
	const Vec3f along = rigJointPosition(world, rig, HandJoint::MiddleProximal)
	                    - rigJointPosition(world, rig, HandJoint::MiddleMetacarpal);
	Vec3f palmNormal = glm::cross(thumbSide, along);
	if(!(glm::length(palmNormal) > 1e-9f) || !(glm::length(thumbSide) > 1e-6f)) {
		return rig;
	}
	palmNormal = glm::normalize(palmNormal);
	const Vec3f toPalm = rigJointPosition(world, rig, HandJoint::Palm)
	                     - rigJointPosition(world, rig, HandJoint::MiddleMetacarpal);
	if(glm::dot(palmNormal, toPalm) < 0.f) {
		palmNormal = -palmNormal;
	}

	for(size_t finger = 0; finger < FingerCount; finger++) {
		for(size_t k = 0; k < FingerJointCount; k++) {
			const HandJoint joint = flexingJoint(Finger(finger), k);
			const size_t index = size_t(rig.joint[size_t(joint)]);
			const size_t child = size_t(rig.joint[size_t(joint) + 1]);
			// The bone to the next joint is that joint's rest translation, in this joint's frame
			const Vec3f bone = Vec3f(hand.skeleton[child].restLocal[3]);
			if(!(glm::length(bone) > 1e-6f)
			   || !rigAxisAcross(bone, rigToLocal(world[index], palmNormal), rig.flexAxis[finger * FingerJointCount + k])) {
				return rig;
			}
			if(k == 0 && !rigAxisAcross(bone, rigToLocal(world[index], thumbSide), rig.spreadAxis[finger])) {
				return rig;
			}
		}
	}

	rig.valid = true;
	return rig;
}

void poseHand(const SkinnedMesh & hand, const HandRig & rig, const FingerPose & pose, std::vector<glm::mat4> & local) {

	local.resize(hand.skeleton.size());
	for(size_t i = 0; i < hand.skeleton.size(); i++) {
		local[i] = hand.skeleton[i].restLocal;
	}
	if(!rig.valid) {
		return;
	}

	for(size_t finger = 0; finger < FingerCount; finger++) {
		for(size_t k = 0; k < FingerJointCount; k++) {
			const float flex = pose.flex[finger][k];
			const float spread = (k == 0) ? pose.spread[finger] : 0.f;
			if(flex == 0.f && spread == 0.f) {
				continue; // exactly the rest
			}
			glm::mat4 & matrix = local[size_t(rig.joint[size_t(flexingJoint(Finger(finger), k))])];
			if(spread != 0.f) {
				matrix = matrix * rigTurn(spread, rig.spreadAxis[finger]);
			}
			matrix = matrix * rigTurn(flex, rig.flexAxis[finger * FingerJointCount + k]);
		}
	}
}

const char * customFingerSourceName(CustomFingerSource source) {
	switch(source) {
		case CustomFingerSource::None: return "none";
		case CustomFingerSource::Joints: return "joints";
		case CustomFingerSource::Controls: return "controls";
		case CustomFingerSource::Held: return "held";
	}
	return "none";
}

CustomFingerTarget customFingerTarget(const CustomFingerInput & input, const HandJoints & joints,
                                      const FingerPoseLimits & limits) {

	CustomFingerTarget target;
	if(input.holds) {
		target.source = CustomFingerSource::Held;
		target.pose = fingerPoseClosed(limits);
	} else if(joints.valid) {
		// The thumb's first joint is read past its rest angle: its limit there is raised by as much
		FingerPoseLimits read = limits;
		read.flex[size_t(Finger::Thumb)][0] += ThumbRestFlexion;
		target.source = CustomFingerSource::Joints;
		target.pose = fingerPoseFromJoints(joints, read);
		float & thumbRoot = target.pose.flex[size_t(Finger::Thumb)][0];
		thumbRoot = std::clamp(thumbRoot - ThumbRestFlexion, 0.f, limits.flex[size_t(Finger::Thumb)][0]);
	} else if(input.controls) {
		target.source = CustomFingerSource::Controls;
		target.pose = fingerPoseFromControls(input.trigger, input.grip, input.thumbTouch, limits);
	}
	return target;
}

} // namespace vr
