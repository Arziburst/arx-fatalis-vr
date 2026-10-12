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

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

#include "vr/HeadPose.h"

namespace vr {

const std::vector<float> & snapAngleChoices() {
	static const std::vector<float> choices = [] {
		std::vector<float> result;
		for(int angle = 30; angle <= 90; angle += 5) {
			result.push_back(float(angle));
		}
		return result;
	}();
	return choices;
}

const std::vector<float> & smoothSpeedChoices() {
	static const std::vector<float> choices = { 60.f, 90.f, 120.f, 180.f, 240.f };
	return choices;
}

const char * smoothSpeedName(size_t index) {
	static const char * const names[] = { "very slow", "slow", "medium", "fast", "very fast" };
	return index < std::size(names) ? names[index] : "";
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

size_t notchAt(float fraction, size_t count) {
	if(count < 2 || !std::isfinite(fraction)) {
		return 0;
	}
	const float clamped = std::min(std::max(fraction, 0.f), 1.f);
	return size_t(std::lround(clamped * float(count - 1)));
}

float notchFraction(size_t index, size_t count) {
	if(count < 2) {
		return 0.f;
	}
	return float(std::min(index, count - 1)) / float(count - 1);
}

} // namespace vr
