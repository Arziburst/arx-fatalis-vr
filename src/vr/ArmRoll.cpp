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

/*
 * The roll rules in this file are ported from the limb solver of RENIK (V-Sekai/renik, commit
 * 1d4028a65b21a05c86714baa48a459a3a7aaaeca, renik/renik_limb.h and renik.cpp solve_trig_ik_redux):
 * the twist shares (lower_limb_twist, upper_limb_twist), the twist steering the joint
 * (target_rotation_influence) and the inflection point with its overflow hysteresis
 * (twist_inflection_point_offset, twist_overflow, overflow_state). RENIK is MIT licensed:
 * Copyright (c) 2020 MMMaellon, Copyright (c) 2014-present Godot Engine contributors - see
 * LICENSE.renik at the root of this tree. The geometry around them (bend-plane frames from the
 * model's own rest pose, the swing-twist split) is ours.
 */

#include "vr/ArmRoll.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vr {

namespace {

const float RollEpsilon = 1e-5f;

//! Any unit vector perpendicular to \a v (a unit vector)
Vec3f perpendicularTo(const Vec3f & v) {
	Vec3f axis = glm::cross(v, Vec3f(1.f, 0.f, 0.f));
	if(glm::dot(axis, axis) < 1e-6f) {
		axis = glm::cross(v, Vec3f(0.f, 1.f, 0.f));
	}
	return glm::normalize(axis);
}

//! The part of \a v across \a axis (a unit vector)
Vec3f across(const Vec3f & v, const Vec3f & axis) {
	return v - glm::dot(v, axis) * axis;
}

//! \a v normalized, or \a fallback when it is too short to have a direction
Vec3f unitOr(const Vec3f & v, const Vec3f & fallback) {
	const float length = glm::length(v);
	return (length > RollEpsilon) ? v / length : fallback;
}

/*!
 * A bone's frame: the bone's axis, the reference put across it, and their cross product. A
 * reference along the axis gets replaced by some perpendicular, so the frame is always a rotation.
 */
glm::mat3 boneFrame(const Vec3f & axis, const Vec3f & reference) {
	Vec3f r = across(reference, axis);
	if(glm::dot(r, r) < 1e-8f) {
		r = perpendicularTo(axis);
	} else {
		r = glm::normalize(r);
	}
	return glm::mat3(axis, r, glm::cross(axis, r));
}

//! The rotation that takes the \a rest frame onto the \a world frame
glm::quat frameRotation(const glm::mat3 & rest, const glm::mat3 & world) {
	return glm::normalize(glm::quat_cast(world * glm::transpose(rest)));
}

glm::quat rollAbout(const Vec3f & axis, float degrees) {
	return glm::angleAxis(glm::radians(degrees), axis);
}

//! The direction the posed elbow sticks out across the shoulder-wrist line; the hint's when the arm is straight
Vec3f elbowDirection(const ArmPose & pose, const Vec3f & hint, const Vec3f & upperAxis) {
	const Vec3f chord = pose.wrist - pose.shoulder;
	const Vec3f line = unitOr(chord, upperAxis);
	Vec3f e = across(pose.elbow - pose.shoulder, line);
	if(glm::dot(e, e) < 1e-4f) {
		e = across(hint, line);
	}
	if(glm::dot(e, e) < 1e-8f) {
		e = perpendicularTo(line);
	}
	return glm::normalize(e);
}

//! Both bones with their rest bend references put into the posed bend plane: no roll of their own
struct NeutralFrames {
	Vec3f upperAxis = Vec3f(0.f, 1.f, 0.f);
	Vec3f forearmAxis = Vec3f(0.f, 1.f, 0.f);
	glm::quat upper = quat_identity();
	glm::quat forearm = quat_identity();
};

NeutralFrames neutralFrames(const ArmPose & pose, const Vec3f & hint, const ArmRest & rest, const Vec3f & restElbow) {
	NeutralFrames frames;
	const Vec3f u0 = unitOr(rest.upper, Vec3f(0.f, 1.f, 0.f));
	const Vec3f f0 = unitOr(rest.forearm, u0);
	frames.upperAxis = unitOr(pose.elbow - pose.shoulder, u0);
	frames.forearmAxis = unitOr(pose.wrist - pose.elbow, frames.upperAxis);
	// The inside of the bend is opposite the elbow, at rest and now
	const Vec3f e = elbowDirection(pose, hint, frames.upperAxis);
	frames.upper = frameRotation(boneFrame(u0, -restElbow), boneFrame(frames.upperAxis, -e));
	frames.forearm = frameRotation(boneFrame(f0, -restElbow), boneFrame(frames.forearmAxis, -e));
	return frames;
}

float share(float value) {
	return glm::clamp(value, 0.f, 1.f);
}

} // anonymous namespace

Vec3f restElbowDirection(const ArmRest & rest, const char ** source) {

	const Vec3f upperAxis = unitOr(rest.upper, Vec3f(0.f, 1.f, 0.f));
	const Vec3f line = unitOr(rest.upper + rest.forearm, upperAxis);
	const char * name = "any";

	// The rest triangle: under about two degrees of bend it says nothing about the elbow's side
	Vec3f e = across(rest.upper, line);
	if(glm::length(e) >= glm::length(rest.upper) * 0.035f) {
		name = "the rest triangle";
	} else {
		e = across(-rest.thumb, line);
		if(glm::dot(e, e) > 1e-8f) {
			name = "opposite the thumb";
		} else {
			e = perpendicularTo(line);
		}
	}
	if(source) {
		*source = name;
	}

	return glm::normalize(e);
}

float twistAboutAxis(const glm::quat & q, const Vec3f & axis) {
	// The swing-twist split: the twist is the rotation's projection onto the axis. With w kept
	// non-negative (q and -q are one rotation) the half angle lies in [-90, 90] degrees
	glm::quat r = q;
	if(r.w < 0.f) {
		r = glm::quat(-r.w, -r.x, -r.y, -r.z);
	}
	const float p = r.x * axis.x + r.y * axis.y + r.z * axis.z;
	return glm::degrees(2.f * std::atan2(p, r.w));
}

float unwrapRoll(float rollDegrees, float overflowDegrees, ArmRollState & state) {

	const float overflow = glm::clamp(overflowDegrees, MinTwistOverflow, MaxTwistOverflow);

	// RENIK's overflow rule: near a half turn the roll remembers which side it came from, and
	// stays on that side (past 180) until it is more than the overflow beyond it
	const float inflection = (rollDegrees > 0.f) ? 180.f : -180.f;
	const float distance = rollDegrees - inflection;
	if(std::abs(distance) < overflow) {
		if(state.overflow == 0) {
			state.overflow = (distance < 0.f) ? 1 : -1;
		}
	} else {
		state.overflow = 0;
	}

	const float limit = inflection + float(state.overflow) * overflow;
	if(rollDegrees > 0.f && rollDegrees > limit) {
		return rollDegrees - 360.f;
	}
	if(rollDegrees < 0.f && rollDegrees < limit) {
		return rollDegrees + 360.f;
	}
	return rollDegrees;
}

ArmRollPose solveArmRoll(const Vec3f & shoulder, const Vec3f & target, const glm::quat & hand,
                         float upperLength, float forearmLength, const Vec3f & elbowHint, const ArmRest & rest,
                         const ArmIKParams & ik, const ArmRollParams & params, ArmRollState & state) {

	ArmRollPose out;
	out.hand = hand;

	const Vec3f restElbow = restElbowDirection(rest, &out.restElbowSource);
	const Vec3f f0 = unitOr(rest.forearm, unitOr(rest.upper, Vec3f(0.f, 1.f, 0.f)));

	// The arm on the base hint, and the hand's roll against that bend plane
	const ArmPose base = solveArm(shoulder, target, upperLength, forearmLength, elbowHint, ik);
	const NeutralFrames baseFrames = neutralFrames(base, elbowHint, rest, restElbow);
	const float measured = twistAboutAxis(glm::inverse(baseFrames.forearm) * hand, f0);
	out.handRoll = unwrapRoll(measured, params.overflowDegrees, state);
	out.overflow = state.overflow;

	// The elbow swings about the shoulder-target line by its share of the roll: the bend plane
	// follows the hand, so the mesh does not have to
	out.elbowSwing = share(params.elbowShare) * out.handRoll;
	ArmPose pose = base;
	Vec3f hint = elbowHint;
	NeutralFrames frames = baseFrames;
	if(std::abs(out.elbowSwing) > 1e-3f) {
		const Vec3f line = unitOr(target - shoulder, baseFrames.upperAxis);
		hint = rollAbout(line, out.elbowSwing) * elbowHint;
		pose = solveArm(shoulder, target, upperLength, forearmLength, hint, ik);
		frames = neutralFrames(pose, hint, rest, restElbow);
	}
	out.pose = pose;

	// What is left against the final bend plane, on the same turn as the unwrapped roll
	float residual = twistAboutAxis(glm::inverse(frames.forearm) * hand, f0);
	const float expected = out.handRoll - out.elbowSwing;
	residual += 360.f * std::round((expected - residual) / 360.f);
	out.residualRoll = residual;

	// The forearm takes its share, the upper arm a share of the forearm's; the bands show the rest
	out.forearmRoll = share(params.forearmShare) * residual;
	out.upperRoll = share(params.upperShare) * out.forearmRoll;
	out.wristTwist = residual - out.forearmRoll;
	out.elbowTwist = out.forearmRoll - out.upperRoll;
	out.forearm = glm::normalize(rollAbout(frames.forearmAxis, out.forearmRoll) * frames.forearm);
	out.upper = glm::normalize(rollAbout(frames.upperAxis, out.upperRoll) * frames.upper);

	return out;
}

} // namespace vr
