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
#include <cctype>
#include <cmath>
#include <string>

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

namespace {

/*!
 * Where the closed fingers sit on the handle, per class (#385.10, research #453 F-487, the first proposal - the owner
 * judges the look in the headset): a share of the handle from the pommel end, or a hand-width above the pommel
 */
struct WeaponAnchor {
	WeaponClass weaponClass;
	const char * name;
	float handleShare;  //!< of the handle's length from the pommel end (when not by the hand's width)
	bool byHandWidth;   //!< a hand-width above the pommel instead (the right hand on a two-handed weapon)
};

const WeaponAnchor WeaponAnchorTable[] = {
	// class                    name         handleShare byHandWidth
	{ WeaponClass::Sword,     "sword",     0.5f,       false },
	{ WeaponClass::Dagger,    "dagger",    0.5f,       false },
	{ WeaponClass::Axe,       "axe",       0.2f,       false }, // 15 - 25 % from the handle's end
	{ WeaponClass::Mace,      "mace",      0.2f,       false },
	{ WeaponClass::TwoHanded, "twoHanded", 0.f,        true },
};

const WeaponAnchor & weaponAnchor(WeaponClass weaponClass) {
	for(const WeaponAnchor & anchor : WeaponAnchorTable) {
		if(anchor.weaponClass == weaponClass) {
			return anchor;
		}
	}
	return WeaponAnchorTable[0];
}

//! The handle's section the handle is measured on reaches at least this share of the mesh from the pommel end
const float HandleSectionShare = 0.1f;
//! A vertex this much wider than the handle (twice its radius, at least this many units more) starts the guard or the head
const float GuardWidening = 1.f;

//! \a v without its part along \a axis (unit)
Vec3f offAxis(const Vec3f & v, const Vec3f & axis) {
	return v - axis * glm::dot(v, axis);
}

} // anonymous namespace

const char * weaponClassName(WeaponClass weaponClass) {
	return weaponAnchor(weaponClass).name;
}

WeaponClass weaponClassOf(bool dagger, bool twoHanded, std::string_view material) {
	if(dagger) {
		return WeaponClass::Dagger;
	}
	if(twoHanded) {
		return WeaponClass::TwoHanded;
	}
	std::string name(material);
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	if(name == "axe") {
		return WeaponClass::Axe;
	}
	if(name == "club") {
		return WeaponClass::Mace;
	}
	return WeaponClass::Sword;
}

float weaponAnchorOffset(WeaponClass weaponClass, float handleLength, float handWidth) {
	if(!(handleLength > 0.f)) {
		return 0.f;
	}
	const WeaponAnchor & anchor = weaponAnchor(weaponClass);
	if(anchor.byHandWidth) {
		return (handWidth > 0.f) ? std::min(handWidth, handleLength) : 0.5f * handleLength;
	}
	return anchor.handleShare * handleLength;
}

glm::quat bladeRollToKnuckles(const Vec3f & blade, const Vec3f & edge, const Vec3f & knuckles) {
	const Vec3f to = offAxis(knuckles, blade);
	const Vec3f from = offAxis(edge, blade);
	if(glm::length(to) < 1e-3f * std::max(glm::length(knuckles), 1e-3f) || glm::length(from) < GripEpsilon) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}
	const float angle = std::atan2(glm::dot(glm::cross(from, to), blade), glm::dot(from, to));
	return glm::angleAxis(angle, blade);
}

glm::quat swordTiltRotation(float degrees, const Vec3f & handle, const Vec3f & toward) {
	const Vec3f axis = glm::cross(handle, toward);
	if(degrees == 0.f || !std::isfinite(degrees) || glm::length(axis) < GripEpsilon) {
		return glm::quat(1.f, 0.f, 0.f, 0.f);
	}
	// About cross(handle, toward) a positive angle turns the handle towards \a toward: a negative tilt leans that way
	return glm::angleAxis(glm::radians(-degrees), glm::normalize(axis));
}

glm::quat weaponInGlove(const Vec3f & blade, const Vec3f & edge, const GloveHilt & hilt, float tiltDegrees) {
	const glm::quat onHandle = rotationBetween(blade, hilt.axis);
	const glm::quat roll = bladeRollToKnuckles(hilt.axis, onHandle * edge, hilt.knuckles);
	const Vec3f off = offAxis(hilt.knuckles, hilt.axis);
	const glm::quat tilt = (glm::length(off) > 1e-3f) ? swordTiltRotation(tiltDegrees, hilt.axis, glm::normalize(off))
	                                                  : glm::quat(1.f, 0.f, 0.f, 0.f);
	return glm::normalize(tilt * roll * onHandle);
}

float swordTiltVariant(float degrees) {
	if(!std::isfinite(degrees)) {
		return SwordTiltVariants[0];
	}
	float best = SwordTiltVariants[0];
	for(float variant : SwordTiltVariants) {
		if(std::abs(degrees - variant) < std::abs(degrees - best)) {
			best = variant;
		}
	}
	return best;
}

WeaponHandle weaponHandleFromMesh(const std::vector<Vec3f> & vertices, const BladeAxis & blade) {

	WeaponHandle handle;
	if(!blade.valid || vertices.size() < 2) {
		return handle;
	}
	const Vec3f & axis = blade.axis;
	float pommel = 0.f;
	for(const Vec3f & v : vertices) {
		pommel = std::min(pommel, glm::dot(v, axis));
	}
	const float span = blade.length - pommel;
	if(span < GripEpsilon) {
		return handle;
	}

	// The handle's section: from the pommel end to the attach vertex (which Arx authors in the hand), at least a
	// share of the mesh - its centre line and its radius
	const float sectionEnd = std::max(0.f, pommel + HandleSectionShare * span);
	Vec3f centre(0.f);
	size_t count = 0;
	for(const Vec3f & v : vertices) {
		if(glm::dot(v, axis) <= sectionEnd) {
			centre += offAxis(v, axis);
			count++;
		}
	}
	centre /= float(std::max<size_t>(count, 1));
	float radius = 0.f;
	for(const Vec3f & v : vertices) {
		if(glm::dot(v, axis) <= sectionEnd) {
			radius += glm::length(offAxis(v, axis) - centre);
		}
	}
	radius /= float(std::max<size_t>(count, 1));

	// The guard (or the head): the nearest vertex past the section that is much wider than the handle
	const float wider = std::max(2.f * radius, radius + GuardWidening);
	float guard = blade.length;
	for(const Vec3f & v : vertices) {
		const float along = glm::dot(v, axis);
		if(along > sectionEnd && along < guard && glm::length(offAxis(v, axis) - centre) > wider) {
			guard = along;
			handle.guardFound = true;
		}
	}
	if(handle.guardFound) {
		handle.length = guard - pommel;
	} else {
		const float depth = -pommel;
		handle.length = (depth > HandleSectionShare * span) ? std::min(2.f * depth, span) : 0.25f * span;
		guard = pommel + handle.length;
	}

	// The edge: the principal axis, across the blade, of what lies past the guard (the blade, the axe's head)
	glm::mat3 covariance(0.f);
	Vec3f start(0.f);
	for(const Vec3f & v : vertices) {
		if(glm::dot(v, axis) > guard || !handle.guardFound) {
			const Vec3f p = offAxis(v, axis) - centre;
			covariance += glm::outerProduct(p, p);
			if(glm::dot(p, p) > glm::dot(start, start)) {
				start = p;
			}
		}
	}
	Vec3f edge = (glm::dot(start, start) > GripEpsilon) ? glm::normalize(start) : Vec3f(0.f);
	for(int i = 0; i < 64 && glm::dot(edge, edge) > 0.f; i++) {
		const Vec3f next = offAxis(covariance * edge, axis);
		if(glm::dot(next, next) < GripEpsilon) {
			break;
		}
		edge = glm::normalize(next);
	}
	if(glm::dot(edge, edge) < 0.5f) {
		const Vec3f other = (std::abs(axis.x) < 0.9f) ? Vec3f(1.f, 0.f, 0.f) : Vec3f(0.f, 1.f, 0.f);
		edge = glm::normalize(glm::cross(axis, other));
	}
	// Towards the side the blade or the head reaches further on (an axe's bit, not its back)
	float plus = 0.f;
	float minus = 0.f;
	for(const Vec3f & v : vertices) {
		if(glm::dot(v, axis) > guard || !handle.guardFound) {
			const float across = glm::dot(offAxis(v, axis) - centre, edge);
			plus = std::max(plus, across);
			minus = std::max(minus, -across);
		}
	}
	if(minus > plus) {
		edge = -edge;
	}

	handle.pommel = pommel;
	handle.centre = centre;
	handle.radius = radius;
	handle.edge = edge;
	handle.valid = handle.length > GripEpsilon;
	return handle;
}

} // namespace vr
