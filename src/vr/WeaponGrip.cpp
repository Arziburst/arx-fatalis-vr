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

#include "vr/WeaponGrip.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "vr/ArmIK.h"

namespace vr {

namespace {

const float GripEpsilon = 1e-6f;

} // anonymous namespace

BladeAxis bladeAxisFromMesh(const std::vector<Vec3f> & vertices) {

	BladeAxis blade;
	if(vertices.size() < 2) {
		return blade;
	}

	Vec3f centroid(0.f);
	for(const Vec3f & v : vertices) {
		centroid += v;
	}
	centroid /= float(vertices.size());

	glm::mat3 covariance(0.f);
	for(const Vec3f & v : vertices) {
		const Vec3f p = v - centroid;
		covariance += glm::outerProduct(p, p);
	}

	// The principal axis by power iteration, started from the vertex farthest from the attach point
	// (never orthogonal to the long axis of a long mesh)
	Vec3f axis(0.f);
	for(const Vec3f & v : vertices) {
		if(glm::dot(v - centroid, v - centroid) > glm::dot(axis, axis)) {
			axis = v - centroid;
		}
	}
	if(glm::dot(axis, axis) < GripEpsilon) {
		return blade;
	}
	axis = glm::normalize(axis);
	for(int i = 0; i < 64; i++) {
		const Vec3f next = covariance * axis;
		if(glm::dot(next, next) < GripEpsilon) {
			return blade;
		}
		axis = glm::normalize(next);
	}

	// Towards the tip: the side the mesh reaches further on from the attach vertex
	float plus = 0.f;
	float minus = 0.f;
	for(const Vec3f & v : vertices) {
		plus = std::max(plus, glm::dot(v, axis));
		minus = std::max(minus, -glm::dot(v, axis));
	}
	if(minus > plus) {
		axis = -axis;
	}

	blade.axis = axis;
	blade.length = 0.f;
	for(size_t i = 0; i < vertices.size(); i++) {
		const float along = glm::dot(vertices[i], axis);
		if(along > blade.length) {
			blade.length = along;
			blade.tip = i;
		}
	}
	blade.valid = blade.length > GripEpsilon;

	return blade;
}

glm::quat bladeOntoHandle(const Vec3f & blade, const Vec3f & thumb) {
	return rotationBetween(blade, thumb);
}

float angleDegrees(const Vec3f & a, const Vec3f & b) {
	const float la = glm::length(a);
	const float lb = glm::length(b);
	if(la < GripEpsilon || lb < GripEpsilon) {
		return 0.f;
	}
	return glm::degrees(std::acos(glm::clamp(glm::dot(a, b) / (la * lb), -1.f, 1.f)));
}

Vec3f directionInGrip(const Vec3f & worldDirection, const glm::quat & handWorld, const glm::quat & handToGrip) {
	// The grip's world rotation (engine axes) is handWorld * inverse(handToGrip); a world direction in
	// the grip's own frame is its inverse applied - in engine axes, where OpenXR's +X, +Y, +Z are +x, -y, -z
	const Vec3f local = handToGrip * (glm::inverse(handWorld) * worldDirection);
	return Vec3f(local.x, -local.y, -local.z);
}

} // namespace vr
