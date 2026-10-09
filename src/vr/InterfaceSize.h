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

#ifndef ARX_VR_INTERFACESIZE_H
#define ARX_VR_INTERFACESIZE_H

#include "math/Types.h"
#include "vr/EyeProjection.h"

/*!
 * \file
 * \brief The interface's own resolution in VR (#73)
 *
 * The 2D interface (HUD, menus, inventory, loading screens) is laid out on a virtual screen -
 * \c g_size and \c g_sizeRatio. In flat mode that is the window. While an OpenXR session runs the
 * interface is drawn into the head-locked quad's image, and the window's size says nothing about
 * how many pixels the headset spends on the quad: a small window made the quad blurry. Here the
 * quad gets the resolution the headset's displays have at that angle, with the window's aspect
 * ratio, so the quad looks exactly as before (size, distance, shape) - only sharper.
 *
 * Pure math, no engine globals and no OpenXR; host-tested in tests/vr/InterfaceSizeTest.cpp.
 */
namespace vr {

//! The narrowest interface ever chosen in VR: the game's own layout size
const s32 MinInterfaceWidth = 640;

//! The widest interface: beyond this the quad costs fill rate without a visible gain
const s32 MaxInterfaceWidth = 4096;

/*!
 * \brief The quad's resolution that matches the headset's pixel density
 *
 * An eye image spreads \a eyeSize.x pixels over the tangent span of its field of view; the quad,
 * \a quadWidth meters wide at \a quadDistance straight ahead, spans quadWidth / quadDistance of
 * that. The height follows the window's aspect ratio (the quad has always had the window's shape).
 * The result is clamped to [\ref MinInterfaceWidth, \ref MaxInterfaceWidth] and to \a maxSize
 * (the largest swapchain), aspect kept.
 *
 * \return the window size when the input says nothing usable (no window, no eye image, an empty
 *         field of view, no quad)
 */
Vec2i interfaceResolution(const Vec2i & eyeSize, const EyeFov & fov, float quadWidth, float quadDistance,
                          const Vec2i & windowSize, const Vec2i & maxSize);

/*!
 * \return the pixels per degree the eye image has straight ahead, for the log (0 for an empty FOV)
 */
float eyePixelsPerDegree(s32 eyeWidth, const EyeFov & fov);

/*!
 * \brief A mouse position in the window -> the same place on the interface's virtual screen
 *
 * Pixel centers are mapped, so equal sizes are the identity and a window position survives the
 * way there and back (\ref interfaceToWindow) whenever the interface is at least as large.
 * The result is inside the interface. Sizes that are not positive leave the position unchanged.
 */
Vec2i windowToInterface(const Vec2i & pos, const Vec2i & windowSize, const Vec2i & interfaceSize);

//! \brief The inverse of \ref windowToInterface: where the window shows an interface position
Vec2i interfaceToWindow(const Vec2i & pos, const Vec2i & windowSize, const Vec2i & interfaceSize);

} // namespace vr

#endif // ARX_VR_INTERFACESIZE_H
