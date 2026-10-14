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

#include "vr/ArxArmFix.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <glm/glm.hpp>

#include "math/Angle.h"
#include "vr/ArmRoll.h"

namespace vr {

namespace {

//! The arm's two bones at rest: unit directions and lengths
struct ArmAxes {
	Vec3f upper = Vec3f(0.f);
	Vec3f forearm = Vec3f(0.f);
	float upperLength = 0.f;
	float forearmLength = 0.f;
};

bool armAxesOf(const ArxArmRest & rest, ArmAxes & axes) {
	const Vec3f upper = rest.elbow - rest.shoulder;
	const Vec3f forearm = rest.wrist - rest.elbow;
	axes.upperLength = glm::length(upper);
	axes.forearmLength = glm::length(forearm);
	if(axes.upperLength < 1e-3f || axes.forearmLength < 1e-3f) {
		return false;
	}
	axes.upper = upper / axes.upperLength;
	axes.forearm = forearm / axes.forearmLength;
	return true;
}

//! The distance of \a p along the arm from the shoulder joint: on the bone it is nearest to
float alongArm(const ArxArmRest & rest, const ArmAxes & axes, const Vec3f & p) {
	const float onUpper = glm::dot(p - rest.shoulder, axes.upper);
	const float onForearm = glm::dot(p - rest.elbow, axes.forearm);
	const Vec3f nearestUpper = rest.shoulder + glm::clamp(onUpper, 0.f, axes.upperLength) * axes.upper;
	const Vec3f nearestForearm = rest.elbow + glm::clamp(onForearm, 0.f, axes.forearmLength) * axes.forearm;
	if(glm::length(p - nearestUpper) < glm::length(p - nearestForearm)) {
		return onUpper;
	}
	return axes.upperLength + onForearm;
}

//! A ring's cross-section across \a axis: its extent the wide way and the thin way
void crossSection(const std::vector<Vec3f> & points, const Vec3f & centre, const Vec3f & axis, float & wide, float & thin) {
	// Two axes across the arm
	const Vec3f helper = (std::abs(axis.x) < 0.9f) ? Vec3f(1.f, 0.f, 0.f) : Vec3f(0.f, 1.f, 0.f);
	const Vec3f a = glm::normalize(glm::cross(axis, helper));
	const Vec3f b = glm::cross(axis, a);
	float xx = 0.f;
	float xy = 0.f;
	float yy = 0.f;
	for(const Vec3f & p : points) {
		const float x = glm::dot(p - centre, a);
		const float y = glm::dot(p - centre, b);
		xx += x * x;
		xy += x * y;
		yy += y * y;
	}
	// The principal direction of the 2x2 covariance
	const float angle = 0.5f * std::atan2(2.f * xy, xx - yy);
	const Vec3f major = std::cos(angle) * a + std::sin(angle) * b;
	const Vec3f minor = glm::cross(axis, major);
	float majorMin = 0.f, majorMax = 0.f, minorMin = 0.f, minorMax = 0.f;
	for(const Vec3f & p : points) {
		const float x = glm::dot(p - centre, major);
		const float y = glm::dot(p - centre, minor);
		majorMin = std::min(majorMin, x);
		majorMax = std::max(majorMax, x);
		minorMin = std::min(minorMin, y);
		minorMax = std::max(minorMax, y);
	}
	wide = majorMax - majorMin;
	thin = minorMax - minorMin;
	if(thin > wide) {
		std::swap(thin, wide);
	}
}

} // anonymous namespace

ArxArmDiagnosis diagnoseArxArm(const ArxArmRest & rest) {

	ArxArmDiagnosis diagnosis;
	ArmAxes axes;
	if(!armAxesOf(rest, axes) || rest.parts.size() != rest.positions.size()) {
		return diagnosis;
	}
	diagnosis.upperLength = axes.upperLength;
	diagnosis.forearmLength = axes.forearmLength;
	const float wristAlong = axes.upperLength + axes.forearmLength;

	std::vector<float> along(rest.positions.size(), 0.f);
	std::vector<size_t> armVertices;
	for(size_t v = 0; v < rest.positions.size(); v++) {
		const ArmPart part = rest.parts[v];
		if(part == ArmPart::Body) {
			continue;
		}
		const Vec3f & p = rest.positions[v];
		along[v] = alongArm(rest, axes, p);
		ArmPartSpan & span = diagnosis.parts[size_t(part)];
		span.alongMin = span.vertices ? std::min(span.alongMin, along[v]) : along[v];
		span.alongMax = span.vertices ? std::max(span.alongMax, along[v]) : along[v];
		span.vertices++;
		const float onUpper = glm::dot(p - rest.shoulder, axes.upper);
		const float onForearm = glm::dot(p - rest.elbow, axes.forearm);
		if(part == ArmPart::Upper && onUpper > axes.upperLength) {
			diagnosis.upperPastElbow++;
			diagnosis.upperPastElbowMax = std::max(diagnosis.upperPastElbowMax, onUpper - axes.upperLength);
		}
		if(part == ArmPart::Forearm) {
			const bool first = diagnosis.parts[size_t(ArmPart::Forearm)].vertices == 1;
			diagnosis.forearmStart = first ? onForearm : std::min(diagnosis.forearmStart, onForearm);
			diagnosis.forearmEnd = first ? onForearm : std::max(diagnosis.forearmEnd, onForearm);
		}
		if(part == ArmPart::Hand && onForearm < axes.forearmLength) {
			diagnosis.handBeforeWrist++;
			diagnosis.handBeforeWristMax = std::max(diagnosis.handBeforeWristMax, axes.forearmLength - onForearm);
		}
		if(along[v] <= wristAlong + WristRingReach) {
			armVertices.push_back(v);
		}
	}
	if(armVertices.empty()) {
		return diagnosis;
	}

	// The faces by the parts of the arm their corners are in
	for(const std::array<size_t, 3> & face : rest.faces) {
		std::array<bool, 4> has = { };
		for(size_t vertex : face) {
			if(vertex < rest.parts.size()) {
				has[size_t(rest.parts[vertex])] = true;
			}
		}
		const bool upper = has[size_t(ArmPart::Upper)];
		const bool forearm = has[size_t(ArmPart::Forearm)];
		const bool hand = has[size_t(ArmPart::Hand)];
		if(upper && hand) {
			diagnosis.facesUpperHand++;
		} else if(upper && forearm) {
			diagnosis.facesUpperForearm++;
		} else if(forearm && hand) {
			diagnosis.facesForearmHand++;
		}
		if(has[size_t(ArmPart::Body)] && (forearm || hand)) {
			diagnosis.facesBodyForearm++;
		}
	}

	// The rings: the arm's vertices sorted along it, a new ring after a gap
	std::sort(armVertices.begin(), armVertices.end(), [&](size_t a, size_t b) { return along[a] < along[b]; });
	std::vector<std::vector<size_t>> groups;
	for(size_t v : armVertices) {
		if(groups.empty() || along[v] - along[groups.back().back()] > RingGap) {
			groups.emplace_back();
		}
		groups.back().push_back(v);
	}
	for(const std::vector<size_t> & group : groups) {
		ArmRing ring;
		ring.vertices = group;
		ring.alongMin = along[group.front()];
		ring.alongMax = along[group.back()];
		std::vector<Vec3f> points;
		for(size_t v : group) {
			ring.along += along[v];
			ring.centre += rest.positions[v];
			ring.parts[size_t(rest.parts[v])]++;
			points.push_back(rest.positions[v]);
		}
		ring.along /= float(group.size());
		ring.centre /= float(group.size());
		const Vec3f axis = (ring.along < axes.upperLength) ? axes.upper : axes.forearm;
		crossSection(points, ring.centre, axis, ring.wide, ring.thin);
		size_t partsInRing = 0;
		for(size_t part = size_t(ArmPart::Upper); part <= size_t(ArmPart::Hand); part++) {
			partsInRing += ring.parts[part] ? 1 : 0;
		}
		if(partsInRing > 1) {
			diagnosis.mixedRings++;
		}
		if(ring.parts[size_t(ArmPart::Upper)] == group.size() && ring.wide > 0.f && ring.thin < FlatRingRatio * ring.wide) {
			diagnosis.flatUpperRings++;
		}
		diagnosis.rings.push_back(ring);
	}
	for(size_t i = 0; i < diagnosis.rings.size(); i++) {
		const float along0 = diagnosis.rings[i].along;
		if(std::abs(along0 - axes.upperLength) < std::abs(diagnosis.rings[diagnosis.elbowRing].along - axes.upperLength)) {
			diagnosis.elbowRing = i;
		}
		if(std::abs(along0 - wristAlong) < std::abs(diagnosis.rings[diagnosis.wristRing].along - wristAlong)) {
			diagnosis.wristRing = i;
		}
	}

	diagnosis.valid = true;
	return diagnosis;
}

namespace {

//! The swing of a rotation off an axis: its angle with the twist about the axis taken out (degrees)
float swingOffAxis(const glm::quat & q, const Vec3f & axis) {
	const float p = q.x * axis.x + q.y * axis.y + q.z * axis.z;
	return glm::degrees(2.f * std::acos(glm::clamp(std::sqrt(q.w * q.w + p * p), 0.f, 1.f)));
}

//! The mean of the rotations a ring's vertices are skinned with
glm::quat ringRotation(const ArmRing & ring, const std::vector<ArmPart> & parts, const std::array<glm::quat, 3> & bones) {
	glm::quat sum(0.f, 0.f, 0.f, 0.f);
	bool first = true;
	glm::quat reference = quat_identity();
	for(size_t vertex : ring.vertices) {
		const ArmPart part = (vertex < parts.size()) ? parts[vertex] : ArmPart::Body;
		if(part == ArmPart::Body) {
			continue;
		}
		glm::quat q = bones[size_t(part) - size_t(ArmPart::Upper)];
		if(first) {
			reference = q;
			first = false;
		} else if(glm::dot(q, reference) < 0.f) {
			q = -q;
		}
		sum = glm::quat(sum.w + q.w, sum.x + q.x, sum.y + q.y, sum.z + q.z);
	}
	return first ? quat_identity() : glm::normalize(sum);
}

} // anonymous namespace

std::vector<ArmPart> classicArmParts(const ArxArmRest & rest, const ArxArmDiagnosis & diagnosis) {

	std::vector<ArmPart> parts = rest.parts;
	if(!diagnosis.valid || diagnosis.elbowRing >= diagnosis.wristRing) {
		return parts;
	}
	for(size_t i = 0; i < diagnosis.wristRing; i++) {
		const ArmPart part = (i < diagnosis.elbowRing) ? ArmPart::Upper : ArmPart::Forearm;
		for(size_t vertex : diagnosis.rings[i].vertices) {
			parts[vertex] = part;
		}
	}
	return parts;
}

ArmBend measureArm(const ArxArmRest & rest, const ArxArmDiagnosis & diagnosis, const std::vector<ArmPart> & parts,
                   const std::array<glm::quat, 3> & bones, float threshold) {

	ArmBend bend;
	ArmAxes axes;
	if(!diagnosis.valid || !armAxesOf(rest, axes)) {
		return bend;
	}
	const glm::quat & upper = bones[0];
	const glm::quat & forearm = bones[1];
	bend.elbowBend = swingOffAxis(glm::inverse(upper) * forearm, axes.forearm);

	const size_t rings = std::min(diagnosis.wristRing, diagnosis.rings.size());
	std::vector<glm::quat> turns;
	for(size_t i = 0; i < rings; i++) {
		turns.push_back(ringRotation(diagnosis.rings[i], parts, bones));
	}

	std::ostringstream text;
	text << std::fixed << std::setprecision(1);
	for(size_t i = 0; i + 1 < turns.size(); i++) {
		const float middle = 0.5f * (diagnosis.rings[i].along + diagnosis.rings[i + 1].along);
		const Vec3f & axis = (middle < axes.upperLength) ? axes.upper : axes.forearm;
		const float angle = swingOffAxis(glm::inverse(turns[i]) * turns[i + 1], axis);
		bend.maxBend = std::max(bend.maxBend, angle);
		if(angle > threshold) {
			text << (bend.creases ? ", " : "") << i << '>' << (i + 1) << ' ' << angle;
			bend.creases++;
		}
	}
	bend.bands = text.str();

	// The forearm's rings: past the elbow joint, before the wrist ring - their roll about the forearm
	bool any = false;
	float least = 0.f;
	float most = 0.f;
	for(size_t i = 0; i < turns.size(); i++) {
		if(diagnosis.rings[i].along <= axes.upperLength) {
			continue;
		}
		const float roll = twistAboutAxis(glm::inverse(forearm) * turns[i], axes.forearm);
		least = any ? std::min(least, roll) : roll;
		most = any ? std::max(most, roll) : roll;
		any = true;
	}
	bend.forearmTwistSpread = most - least;

	return bend;
}

Vec3f skinOnBone(const glm::quat & rotation, const Vec3f & joint, const Vec3f & scale, const Vec3f & local) {
	return rotation * (scale * local) + joint;
}

std::string diagnosisText(const ArxArmDiagnosis & diagnosis) {

	std::ostringstream text;
	text << std::fixed << std::setprecision(1);
	if(!diagnosis.valid) {
		text << "no usable arm";
		return text.str();
	}
	const char * names[4] = { "body", "upper", "forearm", "hand" };
	text << "bones " << diagnosis.upperLength << " + " << diagnosis.forearmLength
	     << "; vertices along the arm from the shoulder:";
	for(size_t part = size_t(ArmPart::Upper); part <= size_t(ArmPart::Hand); part++) {
		const ArmPartSpan & span = diagnosis.parts[part];
		text << ' ' << names[part] << ' ' << span.vertices << " at " << span.alongMin << ".." << span.alongMax;
	}
	text << "; upper past the elbow joint " << diagnosis.upperPastElbow << " (up to " << diagnosis.upperPastElbowMax
	     << "), forearm from " << diagnosis.forearmStart << " to " << diagnosis.forearmEnd << " past it, hand before the wrist "
	     << diagnosis.handBeforeWrist << " (up to " << diagnosis.handBeforeWristMax << "); faces upper-forearm "
	     << diagnosis.facesUpperForearm << ", forearm-hand " << diagnosis.facesForearmHand << ", upper-hand "
	     << diagnosis.facesUpperHand << ", body-forearm/hand " << diagnosis.facesBodyForearm << "; " << diagnosis.rings.size()
	     << " rings (elbow ring " << diagnosis.elbowRing << ", wrist ring " << diagnosis.wristRing << ", flat upper "
	     << diagnosis.flatUpperRings << ", mixed " << diagnosis.mixedRings << "):";
	for(size_t i = 0; i < diagnosis.rings.size(); i++) {
		const ArmRing & ring = diagnosis.rings[i];
		text << (i ? ", " : " ") << '#' << i << ' ' << ring.along << " (" << ring.alongMin << ".." << ring.alongMax << ") ";
		bool first = true;
		for(size_t part = size_t(ArmPart::Upper); part <= size_t(ArmPart::Hand); part++) {
			if(ring.parts[part]) {
				text << (first ? "" : "+") << names[part][0] << ring.parts[part];
				first = false;
			}
		}
		text << ' ' << ring.thin << 'x' << ring.wide;
	}
	return text.str();
}

} // namespace vr
