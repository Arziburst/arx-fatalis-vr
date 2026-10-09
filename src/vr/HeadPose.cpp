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

#include "vr/HeadPose.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

//! OpenXR axes (y up, z back) to engine axes (y down, z forward): a half turn about x
Vec3f toEngineAxes(const Vec3f & v) {
	return Vec3f(v.x, -v.y, -v.z);
}

} // anonymous namespace

HeadSample headFromEyes(const Vec3f & leftPosition, const glm::quat & leftOrientation,
                        const Vec3f & rightPosition, const glm::quat & rightOrientation) {
	HeadSample head;
	head.position = (leftPosition + rightPosition) * 0.5f;
	head.orientation = glm::normalize(glm::slerp(leftOrientation, rightOrientation, 0.5f));
	return head;
}

Anglef headAngles(const glm::quat & orientation) {

	// In double precision: near the poles yaw and roll come from components of a few thousandths, where
	// float rounding alone moves them by thousandths of a degree
	const glm::dquat q = glm::normalize(glm::dquat(orientation));

	// The head's view axes in engine terms (forward +z, right +x, down +y), expressed in the LOCAL
	// space converted to engine axes (a half turn about x: y and z change sign)
	const glm::dvec3 toEngine(1.0, -1.0, -1.0);
	const glm::dvec3 forward = (q * glm::dvec3(0.0, 0.0, -1.0)) * toEngine;
	const glm::dvec3 right = (q * glm::dvec3(1.0, 0.0, 0.0)) * toEngine;
	const glm::dvec3 down = (q * glm::dvec3(0.0, -1.0, 0.0)) * toEngine;

	// The view-to-world rotation of Anglef(p, y, r) is Ry(-y) Rx(-p) Rz(r), so
	// forward = (-sin y cos p, sin p, cos y cos p), right.y = cos p sin r, down.y = cos p cos r
	// (atan2 of the horizontal length rather than asin, which loses precision near the poles)
	const double sinPitch = forward.y;
	const double cosPitch = std::sqrt(forward.x * forward.x + forward.z * forward.z);
	const double pitch = std::atan2(sinPitch, cosPitch);

	double yaw;
	double roll;
	if(cosPitch > 1e-9) {
		yaw = std::atan2(-forward.x, forward.z);
		roll = std::atan2(right.y, down.y);
	} else {
		// Straight up or down: yaw and roll turn about the same axis; with roll 0,
		// down = (sin y sin p, cos p, -cos y sin p)
		const double sign = (sinPitch > 0.0) ? 1.0 : -1.0;
		yaw = std::atan2(down.x * sign, -down.z * sign);
		roll = 0.0;
	}

	return Anglef(float(glm::degrees(pitch)), float(glm::degrees(yaw)), float(glm::degrees(roll)));
}

HeadReference makeHeadReference(const HeadSample & head) {
	HeadReference reference;
	reference.yaw = headAngles(head.orientation).getYaw();
	reference.position = head.position;
	return reference;
}

HeadCamera headToCamera(const HeadSample & head, const HeadReference & reference, float bodyYaw,
                        const Vec2f & anchor, const HeadCameraParams & params) {

	HeadCamera camera;

	// The LOCAL space turned about the vertical by turn degrees puts the reference heading on the body's
	// yaw; a turn about the vertical adds to the yaw because the engine applies Ry(yaw) first
	const float turn = bodyYaw - reference.yaw;
	const Anglef own = headAngles(head.orientation);
	camera.angle = Anglef(wrapDegrees(own.getPitch()), wrapDegrees(own.getYaw() + turn),
	                      wrapDegrees(own.getRoll()));

	// The same turn for the movement since the reference (view-to-world rotation Ry(-turn)), less what the
	// body already walked of it (#26)
	camera.moved = toEngineAxes(head.position - reference.position);
	const Vec3f lean = Vec3f(camera.moved.x - anchor.x, camera.moved.y, camera.moved.z - anchor.y) * params.unitsPerMeter;
	const float t = glm::radians(turn);
	const float c = std::cos(t);
	const float s = std::sin(t);
	camera.offset = Vec3f(c * lean.x - s * lean.z, lean.y, s * lean.x + c * lean.z);

	return camera;
}

float headLockedQuadFov(float quadWidth, float distance, const Vec2i & frameSize) {
	const float height = quadWidth * float(frameSize.y) / std::max(float(frameSize.x), 1.f);
	return 2.f * std::atan(height / 2.f / distance);
}

float wrapDegrees(float degrees) {
	float wrapped = std::fmod(degrees, 360.f);
	if(wrapped < 0.f) {
		wrapped += 360.f;
	}
	return (wrapped >= 360.f) ? 0.f : wrapped;
}

glm::quat headOrientationFromDegrees(float yaw, float pitch, float roll) {
	return glm::angleAxis(glm::radians(yaw), Vec3f(0.f, 1.f, 0.f))
	       * glm::angleAxis(glm::radians(pitch), Vec3f(1.f, 0.f, 0.f))
	       * glm::angleAxis(glm::radians(roll), Vec3f(0.f, 0.f, 1.f));
}

} // namespace vr
