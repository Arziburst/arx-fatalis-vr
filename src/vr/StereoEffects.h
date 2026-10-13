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

#ifndef ARX_VR_STEREOEFFECTS_H
#define ARX_VR_STEREOEFFECTS_H

#include <array>

#include "math/Types.h"
#include "vr/EyeProjection.h"

/*!
 * \file
 * \brief Camera-facing sprites and full-view overlays that both eyes see as one thing (#22)
 *
 * The engine builds its sprites (particles, fog, light and magic flares) in screen space: a square
 * of so many pixels around the point's projection, parallel to the image. Drawn once per eye that is
 * a different square in the world for each eye as soon as the eyes are not parallel - a canted
 * headset (the Index: 5° between the eyes) turns each eye's square by half the cant, and away from
 * the middle of the view the two squares then differ in size by more than 10 %: the edges of a
 * smoke puff or a flare sit at different depths. Its full-view overlays (Magic Sight, the Improve
 * Vision tint) are stretched over each eye's own image, whose middle is a different direction in
 * each eye on an asymmetric headset - the pattern lands at different places, the eyes cannot fuse it.
 *
 * In a stereo frame both get one shape in the head's space instead: a sprite is a square in the
 * world facing the point between the eyes, sized by the engine's own formula at the head's depth, and
 * each eye projects its corners; an overlay hangs at infinity in front of the head over the union of
 * both eyes' frusta, so each texel is one direction for both eyes.
 *
 * Pure math, no engine globals: host-tested in tests/vr/StereoEffectsTest.cpp. Axes are the
 * engine's view axes (EyeProjection.h): x right, y down, z forward, game units.
 */
namespace vr {

/*!
 * \brief The world size of an engine sprite, half its side, at a depth (graphics/Draw.cpp)
 *
 * The engine sizes a sprite in pixels: \c ppu * size * (3000 / depth - 1) * 350 / 310 / 1000 for a
 * size of 0 or more - it shrinks faster than the perspective and is gone at depth 3000 - or, for a
 * negative size, \c ppu * -size / focal: a fixed share of the image at any distance. \c ppu (pixels
 * per unit at depth 1) cancels out of the world size, so both eyes get the same square.
 *
 * \param size  the sprite's size argument
 * \param depth the point's depth in front of the head (view z), above 0
 * \param focal the head camera's focal length (the scale of a fixed-share sprite)
 */
float spriteHalfSize(float size, float depth, float focal);

//! The plane of a sprite facing the head: its right and down axes, unit length, in the head's view space
struct FacingAxes {
	Vec3f right = Vec3f(1.f, 0.f, 0.f);
	Vec3f down = Vec3f(0.f, 1.f, 0.f);
};

/*!
 * \brief The axes of a sprite at \a center (head view space) that faces the head's origin
 *
 * The plane is perpendicular to the line from the point between the eyes to the sprite; its right
 * axis is level in the head's axes and its down axis as close to the head's down as the facing
 * allows - straight ahead it is the screen's own square. A sprite exactly above or below the head
 * keeps the head's right axis.
 */
FacingAxes facingAxes(const Vec3f & center);

/*!
 * \return the directions (head view space, at distance 1) of the corners of an overlay that covers
 *         \a fov: top left, top right, bottom right, bottom left - the order of the engine's
 *         full-screen bitmap. Seen from either eye it covers that eye's whole image when \a fov is
 *         the union of both eyes (\ref unionFov)
 */
std::array<Vec3f, 4> overlayCornerDirections(const EyeFov & fov);

} // namespace vr

#endif // ARX_VR_STEREOEFFECTS_H
