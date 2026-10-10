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

#include "vr/WristVitals.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

const float MinLength = 1e-4f;

//! \a v without its part along the unit vector \a axis, normalized; zero when nothing is left
Vec3f orthogonalTo(const Vec3f & v, const Vec3f & axis) {
	const Vec3f rest = v - axis * glm::dot(v, axis);
	const float length = glm::length(rest);
	return (length > MinLength) ? rest / length : Vec3f(0.f);
}

WristBar makeBar(const Vec3f & middle, const Vec3f & along, const Vec3f & across, float width, float length,
                 float fraction) {
	WristBar bar;
	bar.fraction = fraction;
	const Vec3f elbowSide = -along * (width * 0.5f);
	const Vec3f handSide = along * (width * 0.5f);
	const Vec3f empty = middle - across * (length * 0.5f);
	const Vec3f full = empty + across * length;
	const Vec3f value = empty + across * (length * fraction);
	bar.back = { empty + elbowSide, empty + handSide, full + handSide, full + elbowSide };
	bar.fill = { empty + elbowSide, empty + handSide, value + handSide, value + elbowSide };
	return bar;
}

//!  value clamped to [0, 1], NaN as empty
float toFraction(float value) {
	return (value > 0.f) ? std::min(value, 1.f) : 0.f;
}

} // anonymous namespace

float wristVitalsAlpha(float facingDeg, const WristVitalsParams & params) {
	if(!(facingDeg > params.fullAngleDeg)) {
		return (facingDeg == facingDeg) ? 1.f : 0.f; // NaN: nothing to show
	}
	if(facingDeg >= params.zeroAngleDeg) {
		return 0.f;
	}
	return (params.zeroAngleDeg - facingDeg) / (params.zeroAngleDeg - params.fullAngleDeg);
}

WristVitalsLayout layoutWristVitals(const WristFrame & frame, const Vec3f & eye, float health, float mana,
                                    const WristVitalsParams & params) {

	WristVitalsLayout layout;

	const Vec3f forearm = frame.wrist - frame.elbow;
	const float forearmLength = glm::length(forearm);
	if(!(forearmLength > MinLength)) {
		return layout;
	}
	const Vec3f along = forearm / forearmLength;

	const Vec3f normal = orthogonalTo(frame.palmNormal, along);
	if(normal == Vec3f(0.f)) {
		return layout;
	}

	// Across the wrist towards the thumb; a thumb along the normal or the forearm falls back to where
	// the thumb of a hand in line with the forearm is (palm = knuckles x thumb, HandFrame in ArmIK.h)
	Vec3f across = orthogonalTo(orthogonalTo(frame.thumb, along), normal);
	if(across == Vec3f(0.f)) {
		across = glm::normalize(glm::cross(normal, along));
	}

	const float u = params.unitsPerMeter;
	layout.valid = true;
	layout.along = along;
	layout.normal = normal;
	layout.across = across;
	layout.center = frame.wrist - along * (params.backM * u) + normal * (params.liftM * u);

	const Vec3f toEye = eye - layout.center;
	const float eyeDistance = glm::length(toEye);
	if(eyeDistance > MinLength) {
		const float c = std::clamp(glm::dot(normal, toEye / eyeDistance), -1.f, 1.f);
		layout.facingDeg = glm::degrees(std::acos(c));
	}
	layout.alpha = wristVitalsAlpha(layout.facingDeg, params);

	const float width = params.barWidthM * u;
	const float length = params.barLengthM * u;
	const float offset = (params.gapM * u + width) * 0.5f;
	layout.health = makeBar(layout.center - along * offset, along, across, width, length,
	                        toFraction(health));
	layout.mana = makeBar(layout.center + along * offset, along, across, width, length,
	                      toFraction(mana));

	return layout;
}

} // namespace vr
