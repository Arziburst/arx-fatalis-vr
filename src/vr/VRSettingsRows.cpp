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

#include "vr/VRSettingsRows.h"

#include <cmath>
#include <sstream>

#include "vr/HeadPose.h"

namespace vr {

const std::vector<float> & snapAngleChoices() {
	static const std::vector<float> choices = { 30.f, 45.f, 90.f };
	return choices;
}

const std::vector<float> & smoothSpeedChoices() {
	static const std::vector<float> choices = { 60.f, 90.f, 120.f, 150.f, 180.f, 240.f };
	return choices;
}

const std::vector<float> & snapRepeatChoices() {
	static const std::vector<float> choices = { 0.f, 0.25f, 0.4f, 0.6f, 0.8f, 1.f };
	return choices;
}

const std::vector<float> & heightOffsetChoices() {
	static const std::vector<float> choices = [] {
		std::vector<float> result;
		const long steps = std::lround(MaxHeightOffset / HeightOffsetStep);
		for(long i = -steps; i <= steps; i++) {
			result.push_back(float(i) * HeightOffsetStep);
		}
		return result;
	}();
	return choices;
}

size_t nearestChoice(const std::vector<float> & choices, float value) {
	size_t best = 0;
	for(size_t i = 1; i < choices.size(); i++) {
		if(std::abs(choices[i] - value) < std::abs(choices[best] - value)) {
			best = i;
		}
	}
	return best;
}

std::string formatHeightOffset(float meters) {
	const long centimeters = std::lround(meters * 100.f);
	std::ostringstream oss;
	if(centimeters > 0) {
		oss << '+';
	}
	oss << centimeters << " cm";
	return oss.str();
}

std::string formatSnapRepeat(float seconds) {
	if(seconds <= 0.f) {
		return "off";
	}
	std::ostringstream oss;
	oss << seconds << " s";
	return oss.str();
}

} // namespace vr
