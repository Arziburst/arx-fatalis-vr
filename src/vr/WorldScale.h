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

#ifndef ARX_VR_WORLDSCALE_H
#define ARX_VR_WORLDSCALE_H

namespace vr {

/*!
 * \brief The world scale of VR: game units per meter of the room (#27)
 *
 * One number for everything that turns meters from the runtime into game units - the head's
 * movement (HeadPose.h), the eye offsets and the near plane of the eye rig. The engine itself works
 * in centimeters: the audio listener is set up with 0.01 m per unit (\c audio::setUnitFactor in
 * GameSound.cpp) and the player's collision cylinder is 170 units tall (\c player.baseHeight()), the
 * height of a person. So 1 m = 100 units: doors, tables and goblins keep their size, and the stereo
 * depth (the distance between the eyes in units) matches it.
 */
constexpr float UnitsPerMeter = 100.f;

} // namespace vr

#endif // ARX_VR_WORLDSCALE_H
