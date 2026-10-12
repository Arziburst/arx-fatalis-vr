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

#include "vr/FingerCurl.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

float clamp01(float value) {
	return std::clamp(value, 0.f, 1.f);
}

//! The flexion at a joint: the bones before and after it, from four joints in a row
float jointFlexion(const HandJoints & joints, HandJoint a, HandJoint b, HandJoint c) {
	return flexionDegrees(joints[b] - joints[a], joints[c] - joints[b]);
}

//! A finger's knuckle flexion and the sum of its two outer flexions
void fingerFlexion(const HandJoints & joints, HandJoint metacarpal, float & knuckle, float & outer) {
	const HandJoint proximal = HandJoint(size_t(metacarpal) + 1);
	const HandJoint intermediate = HandJoint(size_t(metacarpal) + 2);
	const HandJoint distal = HandJoint(size_t(metacarpal) + 3);
	const HandJoint tip = HandJoint(size_t(metacarpal) + 4);
	knuckle = jointFlexion(joints, metacarpal, proximal, intermediate);
	outer = jointFlexion(joints, proximal, intermediate, distal) + jointFlexion(joints, intermediate, distal, tip);
}

float approach(float current, float target, float step) {
	if(target > current) {
		return std::min(target, current + step);
	}
	return std::max(target, current - step);
}

} // anonymous namespace

float flexionDegrees(const Vec3f & before, const Vec3f & after) {
	const float lengths = glm::length(before) * glm::length(after);
	if(!(lengths > 1e-12f)) {
		return 0.f;
	}
	const float cosine = std::clamp(glm::dot(before, after) / lengths, -1.f, 1.f);
	return glm::degrees(std::acos(cosine));
}

FingerCurl fingerCurlFromJoints(const HandJoints & joints, const FingerCurlLimits & limits) {

	FingerCurl curl;
	if(!joints.valid) {
		return curl;
	}

	// The thumb: its two joints together
	const float thumbFlexion = jointFlexion(joints, HandJoint::ThumbMetacarpal, HandJoint::ThumbProximal, HandJoint::ThumbDistal)
	                           + jointFlexion(joints, HandJoint::ThumbProximal, HandJoint::ThumbDistal, HandJoint::ThumbTip);
	curl.thumb = limits.thumb * clamp01(thumbFlexion / ThumbFlexionAtFist);

	float knuckle = 0.f;
	float outer = 0.f;
	fingerFlexion(joints, HandJoint::IndexMetacarpal, knuckle, outer);
	curl.indexRoot = limits.indexRoot * clamp01(knuckle / KnuckleFlexionAtFist);
	curl.indexTip = limits.indexTip * clamp01(outer / OuterFlexionAtFist);

	// The three-finger block: the mean of the middle, ring and little finger
	float knuckles = 0.f;
	float outers = 0.f;
	for(HandJoint metacarpal : { HandJoint::MiddleMetacarpal, HandJoint::RingMetacarpal, HandJoint::LittleMetacarpal }) {
		fingerFlexion(joints, metacarpal, knuckle, outer);
		knuckles += knuckle;
		outers += outer;
	}
	curl.fingersRoot = limits.fingersRoot * clamp01(knuckles / 3.f / KnuckleFlexionAtFist);
	curl.fingersTip = limits.fingersTip * clamp01(outers / 3.f / OuterFlexionAtFist);

	return curl;
}

FingerCurl fingerCurlFromControls(float trigger, float grip, bool thumbTouch, const FingerCurlLimits & limits) {
	FingerCurl curl;
	curl.thumb = thumbTouch ? limits.thumb : 0.f;
	curl.indexRoot = limits.indexRoot * clamp01(trigger);
	curl.indexTip = limits.indexTip * clamp01(trigger);
	curl.fingersRoot = limits.fingersRoot * clamp01(grip);
	curl.fingersTip = limits.fingersTip * clamp01(grip);
	return curl;
}

FingerCurl fingerCurlClosed(const FingerCurlLimits & limits) {
	FingerCurl curl;
	curl.thumb = limits.thumb;
	curl.indexRoot = limits.indexRoot;
	curl.indexTip = limits.indexTip;
	curl.fingersRoot = limits.fingersRoot;
	curl.fingersTip = limits.fingersTip;
	return curl;
}

FingerCurl approachCurl(const FingerCurl & current, const FingerCurl & target, float stepDegrees) {
	const float step = std::max(stepDegrees, 0.f);
	FingerCurl curl;
	curl.thumb = approach(current.thumb, target.thumb, step);
	curl.indexRoot = approach(current.indexRoot, target.indexRoot, step);
	curl.indexTip = approach(current.indexTip, target.indexTip, step);
	curl.fingersRoot = approach(current.fingersRoot, target.fingersRoot, step);
	curl.fingersTip = approach(current.fingersTip, target.fingersTip, step);
	return curl;
}

HandJoints synthesizeHandJoints(Hand hand, const HandShape & shape, const Vec3f & gripPosition,
                                const glm::quat & gripOrientation) {

	// The grip frame: the fingers run along -Y, the thumb side is -Z, the palm faces -X on the right
	// hand (+X points into the palm) and +X on the left; a finger closes towards the palm's face
	const Vec3f along(0.f, -1.f, 0.f);
	const Vec3f thumbSide(0.f, 0.f, -1.f);
	const Vec3f palmOut = (hand == Hand::Right) ? Vec3f(-1.f, 0.f, 0.f) : Vec3f(1.f, 0.f, 0.f);

	HandJoints joints;
	joints.valid = true;
	joints[HandJoint::Palm] = Vec3f(0.f);
	const Vec3f wrist = -along * 0.045f;
	joints[HandJoint::Wrist] = wrist;

	//! A chain of bones from \a base in direction \a direction, each bent further by its angle towards \a bend
	auto chain = [&](HandJoint first, const Vec3f & base, const Vec3f & direction, const Vec3f & bend,
	                 const float * lengths, const float * angles, size_t count) {
		Vec3f at = base;
		joints[first] = at;
		float total = 0.f;
		for(size_t i = 0; i < count; i++) {
			total += glm::radians(angles[i]);
			const Vec3f bone = direction * std::cos(total) + bend * std::sin(total);
			at += bone * lengths[i];
			joints[HandJoint(size_t(first) + i + 1)] = at;
		}
	};

	// The four fingers, side by side from the thumb's side: a straight metacarpal from near the
	// wrist to the knuckle, then three phalanges bending at the knuckle, the middle and the last joint
	const struct { HandJoint metacarpal; float side; float scale; } fingers[] = {
		{ HandJoint::IndexMetacarpal, -0.025f, 1.f },
		{ HandJoint::MiddleMetacarpal, -0.008f, 1.05f },
		{ HandJoint::RingMetacarpal, 0.008f, 0.95f },
		{ HandJoint::LittleMetacarpal, 0.025f, 0.8f },
	};
	for(const auto & finger : fingers) {
		const float closed = (finger.metacarpal == HandJoint::IndexMetacarpal) ? shape.index : shape.fingers;
		const Vec3f base = wrist + Vec3f(0.f, 0.f, finger.side) + along * 0.01f;
		joints[finger.metacarpal] = base;
		const float lengths[] = { 0.04f * finger.scale, 0.025f * finger.scale, 0.02f * finger.scale };
		const float angles[] = { 90.f * closed, 100.f * closed, 70.f * closed };
		chain(HandJoint(size_t(finger.metacarpal) + 1), base + along * 0.06f, along, palmOut, lengths, angles, 3);
	}

	// The thumb: from the wrist's thumb side, out and forward, closing towards the fingers and the palm's face
	{
		const Vec3f direction = glm::normalize(thumbSide * 0.75f + along * 0.55f + palmOut * 0.35f);
		Vec3f bend = glm::normalize(along * 0.7f + palmOut * 0.7f);
		bend = glm::normalize(bend - direction * glm::dot(bend, direction));
		const Vec3f base = wrist + thumbSide * 0.02f + along * 0.005f;
		const float lengths[] = { 0.045f, 0.035f, 0.03f };
		const float angles[] = { 0.f, 50.f * shape.thumb, 70.f * shape.thumb };
		chain(HandJoint::ThumbMetacarpal, base, direction, bend, lengths, angles, 3);
	}

	for(Vec3f & position : joints.position) {
		position = gripPosition + gripOrientation * position;
	}

	return joints;
}

} // namespace vr
