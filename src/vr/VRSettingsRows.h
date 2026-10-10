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

#ifndef ARX_VR_VRSETTINGSROWS_H
#define ARX_VR_VRSETTINGSROWS_H

#include <cstddef>
#include <string>
#include <vector>

/*!
 * \brief The choices of the VR settings' turning and height rows (#260, D-028)
 *
 * The rows edit the cfg.ini keys of #32 (\c vr_smooth_turn, \c vr_snap_turn_angle, \c vr_smooth_turn_speed,
 * \c vr_snap_turn_repeat) and \c vr_height_offset; cfg.ini stays the source of truth. A row shows the
 * choice nearest to the stored value, so a hand-edited value outside the list shows as its neighbour
 * until the player picks one.
 *
 * Pure logic - covered by tests/vr/VRSettingsRowsTest.
 */
namespace vr {

//! Snap turn angles, degrees: 30 to 90 in 5 degree steps (owner, 2026-10-10; D-028: 45 is the default)
const std::vector<float> & snapAngleChoices();

/*!
 * Smooth turn speeds, degrees per second at full deflection, slowest first - the player sees only their names
 * (\ref smoothSpeedName: "very slow" to "very fast", owner 2026-10-10); "medium" is #32's 120
 */
const std::vector<float> & smoothSpeedChoices();

//! The name of smooth speed preset \a index ("very slow", "slow", "medium", "fast", "very fast")
const char * smoothSpeedName(size_t index);

//! The pause between snaps while the stick is held, seconds; 0 = one snap per push (0.4 is the default)
const std::vector<float> & snapRepeatChoices();

//! Character height offsets, meters: -MaxHeightOffset to +MaxHeightOffset in HeightOffsetStep steps (HeadPose.h)
const std::vector<float> & heightOffsetChoices();

//! \return the index of the choice nearest to \a value (the first of two equally near; 0 for an empty list)
size_t nearestChoice(const std::vector<float> & choices, float value);

//! A height offset as the row shows it: "+4 cm", "-10 cm", "0 cm"
std::string formatHeightOffset(float meters);

//! A snap repeat as the row shows it: "off", "0.4 s"
std::string formatSnapRepeat(float seconds);

/*!
 * The slider's notches (NotchSliderWidget): \a count notches evenly along the bar, the first at its left end
 * (fraction 0), the last at its right end (1)
 * \return the notch nearest to \a fraction, clamped to the bar; 0 for fewer than two notches
 */
size_t notchAt(float fraction, size_t count);

//! \return where notch \a index of \a count is along the bar, 0 to 1 (0 for fewer than two notches)
float notchFraction(size_t index, size_t count);

} // namespace vr

#endif // ARX_VR_VRSETTINGSROWS_H
