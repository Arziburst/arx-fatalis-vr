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

#include "vr/MeleeChargeCue.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace vr {

namespace {

const float CueMinLength = 1e-4f;

//! Up in the engine's world (y points down)
const Vec3f CueWorldUp = Vec3f(0.f, -1.f, 0.f);

//! \a v without its part along the unit vector \a axis, normalized; zero when nothing is left
Vec3f cueOrthogonalTo(const Vec3f & v, const Vec3f & axis) {
	const Vec3f rest = v - axis * glm::dot(v, axis);
	const float length = glm::length(rest);
	return (length > 0.01f * glm::length(v) && length > CueMinLength) ? rest / length : Vec3f(0.f);
}

//! \a value clamped to [0, 1], NaN as empty
float cueFraction(float value) {
	return (value > 0.f) ? std::min(value, 1.f) : 0.f;
}

} // anonymous namespace

const char * chargeCueName(ChargeCue cue) {
	switch(cue) {
		case ChargeCue::Off: return "off";
		case ChargeCue::Glow: return "glow";
		case ChargeCue::Wrist: return "wrist";
		case ChargeCue::Hud: return "hud";
		case ChargeCue::Blade: return "blade";
		case ChargeCue::All: return "all";
	}
	return "off";
}

bool parseChargeCue(std::string_view name, ChargeCue & cue) {
	for(ChargeCue candidate : { ChargeCue::Off, ChargeCue::Glow, ChargeCue::Wrist, ChargeCue::Hud, ChargeCue::Blade,
	                            ChargeCue::All }) {
		if(name == chargeCueName(candidate)) {
			cue = candidate;
			return true;
		}
	}
	return false;
}

bool chargeCueShows(ChargeCue mode, ChargeCue variant) {
	if(variant == ChargeCue::Off || variant == ChargeCue::All) {
		return false;
	}
	return mode == ChargeCue::All || mode == variant;
}

float chargeFill(float seconds, float fullSeconds) {
	if(!(fullSeconds > 0.f)) {
		return 0.f;
	}
	return cueFraction(seconds / fullSeconds);
}

ChargeRingLayout layoutChargeRing(const WristFrame & frame, const Vec3f & eye, float fill,
                                  const ChargeCueParams & params) {

	ChargeRingLayout layout;
	layout.fill = cueFraction(fill);

	const Vec3f forearm = frame.wrist - frame.elbow;
	const float forearmLength = glm::length(forearm);
	if(!(forearmLength > CueMinLength)) {
		return layout;
	}
	const Vec3f along = forearm / forearmLength;
	const Vec3f back = cueOrthogonalTo(-frame.palmNormal, along);
	if(back == Vec3f(0.f)) {
		return layout;
	}

	const float u = params.unitsPerMeter;
	const Vec3f center = frame.wrist - along * (params.ringBackM * u) + back * (params.ringLiftM * u);
	const Vec3f toEye = eye - center;
	const float eyeDistance = glm::length(toEye);
	if(!(eyeDistance > CueMinLength)) {
		return layout;
	}
	const Vec3f normal = toEye / eyeDistance;

	// 12 o'clock is up in the world; seen straight from above or below, it is towards the hand
	Vec3f up = cueOrthogonalTo(CueWorldUp, normal);
	if(up == Vec3f(0.f)) {
		up = cueOrthogonalTo(along, normal);
	}
	if(up == Vec3f(0.f)) {
		return layout;
	}
	// Right as the eye sees it: the eye looks along -normal
	const Vec3f right = glm::cross(-normal, up);

	layout.valid = true;
	layout.center = center;
	layout.normal = normal;
	layout.up = up;
	layout.right = right;
	layout.filledDeg = layout.fill * 360.f;

	const float inner = params.ringInnerM * u;
	const float outer = params.ringOuterM * u;
	const size_t segments = std::max<size_t>(params.ringSegments, 3);
	auto point = [&](float degrees, float radius) {
		const float a = glm::radians(degrees);
		return center + (right * std::sin(a) + up * std::cos(a)) * radius;
	};
	auto quad = [&](float from, float to) {
		return CueQuad{ point(from, inner), point(from, outer), point(to, outer), point(to, inner) };
	};

	const float step = 360.f / float(segments);
	layout.track.reserve(segments);
	for(size_t i = 0; i < segments; i++) {
		layout.track.push_back(quad(step * float(i), step * float(i + 1)));
	}
	for(float from = 0.f; from < layout.filledDeg; from += step) {
		layout.filled.push_back(quad(from, std::min(from + step, layout.filledDeg)));
	}

	return layout;
}

BladeStripLayout layoutBladeStrip(const Vec3f & hilt, const Vec3f & tip, const Vec3f & eye, float fill,
                                  const ChargeCueParams & params) {

	BladeStripLayout layout;
	layout.fill = cueFraction(fill);
	layout.end = hilt;

	const Vec3f blade = tip - hilt;
	const float length = glm::length(blade);
	if(!(length > CueMinLength)) {
		return layout;
	}
	const Vec3f axis = blade / length;
	const Vec3f middle = hilt + blade * 0.5f;
	const Vec3f toEye = eye - middle;
	const float eyeDistance = glm::length(toEye);
	if(!(eyeDistance > CueMinLength)) {
		return layout;
	}
	// Turned to the eye: across the blade in the plane the eye sees it in
	const Vec3f side = cueOrthogonalTo(glm::cross(axis, toEye / eyeDistance), axis);
	if(side == Vec3f(0.f)) {
		return layout;
	}

	const float u = params.unitsPerMeter;
	const Vec3f lift = (toEye / eyeDistance) * (params.stripTowardEyeM * u);
	const Vec3f halfWidth = side * (params.stripWidthM * u * 0.5f);
	layout.valid = true;
	layout.end = hilt + blade * layout.fill;
	layout.strip = { hilt - halfWidth + lift, hilt + halfWidth + lift, layout.end + halfWidth + lift,
	                 layout.end - halfWidth + lift };

	return layout;
}

Vec3f chargeGlow(float fill, const ChargeCueParams & params) {
	return params.glowFull * cueFraction(fill);
}

} // namespace vr
