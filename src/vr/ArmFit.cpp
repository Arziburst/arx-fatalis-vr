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

#include "vr/ArmFit.h"

#include <algorithm>
#include <cmath>

namespace vr {

float clampArmMaxStretch(float stretch) {
	return std::isfinite(stretch) ? std::min(std::max(stretch, 0.f), MaxArmStretchLimit) : MaxArmStretch;
}

bool isExtendedReach(float shoulderToWrist, float heightReach) {
	return std::isfinite(shoulderToWrist) && heightReach > 0.f && shoulderToWrist >= heightReach * MinExtendedReach
	       && shoulderToWrist <= heightReach * MaxExtendedReach;
}

ArmLengths armLengthsFromHeight(float heightUnits, float upperRatio) {
	return armLengthsFromReach(heightUnits * ArmToStature, upperRatio);
}

ArmLengths armLengthsFromReach(float reach, float upperRatio) {
	ArmLengths lengths;
	lengths.upper = reach * upperRatio;
	lengths.forearm = reach - lengths.upper;
	return lengths;
}

void ReachTracker::add(float shoulderToWrist) {
	if(!std::isfinite(shoulderToWrist)) {
		return;
	}
	if(shoulderToWrist > m_largest) {
		m_second = m_largest;
		m_largest = shoulderToWrist;
	} else if(shoulderToWrist > m_second) {
		m_second = shoulderToWrist;
	}
	m_samples = std::min(m_samples + 1, MinSamples);
}

void ReachTracker::reset() {
	*this = ReachTracker();
}

float armStretch(float shoulderToTarget, const ArmLengths & lengths, float maxStretch) {
	const float length = lengths.upper + lengths.forearm;
	if(length <= 0.f || !(shoulderToTarget > length)) {
		return 0.f;
	}
	return std::min((shoulderToTarget - length) / length, std::max(maxStretch, 0.f));
}

} // namespace vr
