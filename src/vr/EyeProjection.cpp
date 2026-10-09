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

#include "vr/EyeProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vr {

namespace {

//! OpenXR view axes (y up, z back) to engine view axes (y down, z forward): a half turn about x
Vec3f projectionEngineAxes(const Vec3f & v) {
	return Vec3f(v.x, -v.y, -v.z);
}

//! The same half turn applied to a rotation: conjugation by the half turn flips the y and z parts
glm::quat projectionEngineAxes(const glm::quat & q) {
	return glm::quat(q.w, q.x, -q.y, -q.z);
}

} // anonymous namespace

FovTangents tangents(const EyeFov & fov) {
	FovTangents t;
	t.left = std::tan(fov.left);
	t.right = std::tan(fov.right);
	t.up = std::tan(fov.up);
	t.down = std::tan(fov.down);
	return t;
}

glm::mat4x4 eyeProjection(const EyeFov & fov, float nearDist, float farDist) {

	const FovTangents t = tangents(fov);
	const float width = t.right - t.left;
	const float height = t.up - t.down;
	const float Q = farDist / (farDist - nearDist);

	/*
	 * The engine's clip space (createProjectionMatrix in game/Camera.cpp): clip w = view z, depth
	 * Q - Q near / z in [0, 1], and the y axis flipped because the engine's view space has y down.
	 * An eye's frustum at distance z spans x in [left z, right z] in its own view axes, so
	 * ndc x = (2 x / z - (right + left)) / (right - left): the symmetric 2 / (right - left) scale
	 * plus a shift by the view axis's own position, the same shift the engine's centerShift makes
	 * for an off-center projection. Vertically, OpenXR's up is the engine's -y.
	 */
	glm::mat4x4 m(1.f);
	m[0][0] = 2.f / width;
	m[2][0] = -(t.right + t.left) / width;
	m[1][1] = -2.f / height;
	m[2][1] = -(t.up + t.down) / height;
	m[2][2] = Q;
	m[3][2] = -Q * nearDist;
	m[2][3] = 1.f;
	m[3][3] = 0.f;

	return m;
}

float verticalFov(const EyeFov & fov) {
	const FovTangents t = tangents(fov);
	return 2.f * std::atan((t.up - t.down) / 2.f);
}

float horizontalFov(const EyeFov & fov) {
	const FovTangents t = tangents(fov);
	return 2.f * std::atan((t.right - t.left) / 2.f);
}

std::array<EyeRig, 2> eyeRig(const LocatedEye & left, const LocatedEye & right, const EyeRigParams & params) {

	// The head between the eyes, as HeadPose.h's headFromEyes defines it (#24)
	const Vec3f headPosition = (left.position + right.position) * 0.5f;
	const glm::quat headOrientation = glm::normalize(glm::slerp(left.orientation, right.orientation, 0.5f));
	const glm::quat localToHead = glm::inverse(headOrientation);

	std::array<EyeRig, 2> rig;
	const LocatedEye * eyes[2] = { &left, &right };
	for(size_t i = 0; i < rig.size(); i++) {
		const LocatedEye & eye = *eyes[i];
		// The eye in the head's own frame (OpenXR axes), then in engine axes and game units
		const Vec3f offset = localToHead * (eye.position - headPosition);
		const glm::quat rotation = glm::normalize(localToHead * eye.orientation);
		rig[i].offset = projectionEngineAxes(offset) * params.unitsPerMeter;
		rig[i].rotation = projectionEngineAxes(rotation);
		rig[i].fov = eye.fov;
	}

	return rig;
}

float eyeSeparationMeters(const std::array<EyeRig, 2> & eyes, const EyeRigParams & params) {
	return glm::distance(eyes[0].offset, eyes[1].offset) / params.unitsPerMeter;
}

float cantAngle(const std::array<EyeRig, 2> & eyes) {
	const Vec3f forward(0.f, 0.f, 1.f);
	const Vec3f left = eyes[0].rotation * forward;
	const Vec3f right = eyes[1].rotation * forward;
	return std::acos(std::clamp(glm::dot(left, right), -1.f, 1.f));
}

glm::mat4x4 eyeWorldToView(const glm::mat4x4 & headWorldToView, const EyeRig & eye) {
	// A point in the eye's view space is R p + t in the head's, so head to eye is R^T (p - t)
	const glm::mat4x4 headToEye = glm::transpose(glm::mat4_cast(eye.rotation))
	                              * glm::translate(glm::mat4x4(1.f), -eye.offset);
	return headToEye * headWorldToView;
}

Vec3f eyeWorldPosition(const glm::mat4x4 & headWorldToView, const EyeRig & eye) {
	const glm::mat4x4 viewToWorld = glm::affineInverse(headWorldToView);
	return Vec3f(viewToWorld * Vec4f(eye.offset, 1.f));
}

EyeFov unionFov(const std::array<EyeRig, 2> & eyes) {

	float minX = std::numeric_limits<float>::infinity();
	float maxX = -std::numeric_limits<float>::infinity();
	float minY = std::numeric_limits<float>::infinity();
	float maxY = -std::numeric_limits<float>::infinity();

	for(const EyeRig & eye : eyes) {
		// The eye's frustum corners at distance 1 in its own (engine) view axes: y down, so the top
		// edge is at -up; turned into the head's axes by the eye's rotation
		const FovTangents t = tangents(eye.fov);
		const Vec3f corners[4] = {
			Vec3f(t.left, -t.up, 1.f), Vec3f(t.right, -t.up, 1.f),
			Vec3f(t.left, -t.down, 1.f), Vec3f(t.right, -t.down, 1.f),
		};
		for(const Vec3f & corner : corners) {
			const Vec3f d = eye.rotation * corner;
			if(d.z <= 0.f) {
				// A corner turned behind the head: no finite frustum contains it (a cant of 90°
				// or more, which no headset has) - the union keeps the other corners
				continue;
			}
			minX = std::min(minX, d.x / d.z);
			maxX = std::max(maxX, d.x / d.z);
			minY = std::min(minY, d.y / d.z);
			maxY = std::max(maxY, d.y / d.z);
		}
	}

	EyeFov fov;
	if(!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY)) {
		return fov;
	}
	fov.left = std::atan(minX);
	fov.right = std::atan(maxX);
	// Engine y is down: the top of the union is the most negative y, OpenXR's up is positive
	fov.up = std::atan(-minY);
	fov.down = std::atan(-maxY);
	return fov;
}

} // namespace vr
