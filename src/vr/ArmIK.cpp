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

#include "vr/ArmIK.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace vr {

namespace {

const float IkEpsilon = 1e-5f;

//! Any unit vector perpendicular to \a v (a unit vector)
Vec3f anyPerpendicular(const Vec3f & v) {
	Vec3f axis = glm::cross(v, Vec3f(1.f, 0.f, 0.f));
	if(glm::dot(axis, axis) < 1e-6f) {
		axis = glm::cross(v, Vec3f(0.f, 1.f, 0.f));
	}
	return glm::normalize(axis);
}

} // anonymous namespace

glm::quat rotationBetween(const Vec3f & from, const Vec3f & to) {

	const float lengthFrom = glm::length(from);
	const float lengthTo = glm::length(to);
	if(lengthFrom < IkEpsilon || lengthTo < IkEpsilon) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}

	const Vec3f f = from / lengthFrom;
	const Vec3f t = to / lengthTo;
	const float d = glm::dot(f, t);
	if(d >= 1.f - 1e-6f) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}
	if(d <= -1.f + 1e-6f) {
		const Vec3f axis = anyPerpendicular(f);
		return glm::quat(0.f, axis.x, axis.y, axis.z);
	}

	// The half-angle quaternion: w = 1 + cos, xyz = sin * axis, normalized
	const Vec3f axis = glm::cross(f, t);
	return glm::normalize(glm::quat(1.f + d, axis.x, axis.y, axis.z));
}

ArmPose solveArm(const Vec3f & shoulder, const Vec3f & target, float upperLength, float forearmLength,
                 const Vec3f & elbowHint, const ArmIKParams & params) {

	ArmPose pose;
	pose.shoulder = shoulder;

	const float l1 = std::max(upperLength, IkEpsilon);
	const float l2 = std::max(forearmLength, IkEpsilon);
	const float armLength = l1 + l2;

	const Vec3f toTarget = target - shoulder;
	float distance = glm::length(toTarget);
	pose.reach = distance;

	Vec3f direction;
	if(distance > 1e-4f) {
		direction = toTarget / distance;
	} else {
		// The target is at the shoulder: the arm folds across the hint, so the elbow can go along it
		const float hintLength = glm::length(elbowHint);
		direction = (hintLength > IkEpsilon) ? anyPerpendicular(elbowHint / hintLength) : Vec3f(0.f, 0.f, 1.f);
	}

	// Out of reach: the shoulder slides towards the target first (F-52)
	if(distance > armLength && params.maxShoulderSlide > 0.f) {
		pose.slide = std::min(distance - armLength, params.maxShoulderSlide);
		pose.shoulder = shoulder + direction * pose.slide;
		distance -= pose.slide;
	}
	pose.stretched = distance > armLength + 1e-4f;

	// The wrist: on the line towards the target, as far as the chain can reach or fold
	const float fold = std::abs(l1 - l2);
	const float r = glm::clamp(distance, fold, armLength);
	pose.wrist = pose.shoulder + direction * r;

	// The elbow: in the plane of the arm and the hint, on the hint's side
	Vec3f side = elbowHint - glm::dot(elbowHint, direction) * direction;
	if(glm::dot(side, side) < 1e-8f) {
		side = anyPerpendicular(direction);
	} else {
		side = glm::normalize(side);
	}
	if(r < 1e-4f) {
		// Folded flat (equal bones): the elbow sticks out towards the hint
		pose.elbow = pose.shoulder + side * l1;
	} else {
		const float along = (l1 * l1 - l2 * l2 + r * r) / (2.f * r);
		const float height2 = l1 * l1 - along * along;
		const float height = (height2 > 0.f) ? std::sqrt(height2) : 0.f;
		pose.elbow = pose.shoulder + direction * along + side * height;
	}

	pose.error = glm::distance(pose.wrist, target);

	return pose;
}

HandFrame handFrameFromMesh(const std::vector<Vec3f> & vertices, const Vec3f * palmHint, const Vec3f * thumbHint) {

	HandFrame frame;
	if(vertices.size() < 4) {
		return frame;
	}

	// Both hints known: the axes come from the model's own landmarks, the mesh only measures extents
	if(palmHint && thumbHint && glm::dot(*palmHint, *palmHint) > IkEpsilon) {
		const Vec3f knuckles = glm::normalize(*palmHint);
		Vec3f lateral = *thumbHint - glm::dot(*thumbHint, knuckles) * knuckles;
		if(glm::dot(lateral, lateral) > IkEpsilon) {
			lateral = glm::normalize(lateral);
			frame.knuckles = knuckles;
			frame.thumb = lateral;
			frame.palm = glm::normalize(glm::cross(knuckles, lateral));
			for(const Vec3f & vertex : vertices) {
				const float side = glm::dot(vertex, lateral);
				frame.thumbExtent = std::max(frame.thumbExtent, side);
				frame.otherExtent = std::max(frame.otherExtent, -side);
				frame.length = std::max(frame.length, glm::dot(vertex, knuckles));
			}
			frame.valid = frame.length > IkEpsilon;
			return frame;
		}
	}

	Vec3f centroid(0.f);
	for(const Vec3f & v : vertices) {
		centroid += v;
	}
	centroid /= float(vertices.size());

	// The long axis: from the wrist to the palm point when one is known, else the mesh's
	// principal axis (power iteration on the covariance), pointing away from the wrist
	Vec3f knuckles;
	if(palmHint && glm::dot(*palmHint, *palmHint) > IkEpsilon) {
		knuckles = glm::normalize(*palmHint);
	} else {
		if(glm::dot(centroid, centroid) < IkEpsilon) {
			return frame;
		}
		glm::mat3 covariance(0.f);
		for(const Vec3f & v : vertices) {
			const Vec3f p = v - centroid;
			covariance += glm::outerProduct(p, p);
		}
		knuckles = glm::normalize(centroid);
		for(int i = 0; i < 32; i++) {
			const Vec3f next = covariance * knuckles;
			if(glm::dot(next, next) < IkEpsilon) {
				return frame;
			}
			knuckles = glm::normalize(next);
		}
		if(glm::dot(knuckles, centroid) < 0.f) {
			knuckles = -knuckles;
		}
	}

	// The lateral axis: the direction the mesh is widest in across the long axis (2D principal axis
	// in the plane perpendicular to it)
	const Vec3f u = anyPerpendicular(knuckles);
	const Vec3f v = glm::cross(knuckles, u);
	float a = 0.f;
	float b = 0.f;
	float c = 0.f;
	for(const Vec3f & vertex : vertices) {
		const Vec3f p = vertex - centroid;
		const float x = glm::dot(p, u);
		const float y = glm::dot(p, v);
		a += x * x;
		b += x * y;
		c += y * y;
	}
	const float theta = 0.5f * std::atan2(2.f * b, a - c);
	const Vec3f lateral = std::cos(theta) * u + std::sin(theta) * v;
	// The eigenvalues: too alike means the mesh is round across the long axis - no thumb side to tell
	const float mean = 0.5f * (a + c);
	const float diff = std::sqrt(0.25f * (a - c) * (a - c) + b * b);
	const float wide = mean + diff;
	const float thin = mean - diff;

	// The thumb side: the mesh reaches further from the long axis on it
	float plus = 0.f;
	float minus = 0.f;
	float length = 0.f;
	for(const Vec3f & vertex : vertices) {
		const float side = glm::dot(vertex, lateral);
		plus = std::max(plus, side);
		minus = std::max(minus, -side);
		length = std::max(length, glm::dot(vertex, knuckles));
	}

	frame.knuckles = knuckles;
	frame.thumb = (plus >= minus) ? lateral : -lateral;
	frame.palm = glm::normalize(glm::cross(knuckles, frame.thumb));
	frame.length = length;
	frame.thumbExtent = std::max(plus, minus);
	frame.otherExtent = std::min(plus, minus);
	frame.valid = length > IkEpsilon && wide > 1.5f * std::max(thin, IkEpsilon)
	              && frame.thumbExtent > frame.otherExtent * 1.05f;

	return frame;
}

glm::quat handToGrip(const HandFrame & frame) {
	// The hand's axes as columns (palm, knuckles, thumb) make a rotation B from the grip's axes they
	// lie on - OpenXR +X, -Y, -Z (the spec's grip pose: +X normal to the palm, -Z through the fingers'
	// tube from the little finger to the thumb, +Y by the right-hand rule) - to the bone's rest frame.
	// In engine terms those three grip axes are +x, +y, +z, so bone -> grip (engine) is B^T
	const glm::mat3 bone(frame.palm, frame.knuckles, frame.thumb);
	return glm::normalize(glm::quat_cast(glm::transpose(bone)));
}

} // namespace vr
