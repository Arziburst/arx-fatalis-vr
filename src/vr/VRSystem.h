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

#ifndef ARX_VR_VRSYSTEM_H
#define ARX_VR_VRSYSTEM_H

/*!
 * \brief VR runtime integration (OpenXR)
 *
 * The game starts in VR automatically when an OpenXR runtime with a head-mounted display is
 * available and \c --no-vr was not given; otherwise it runs in flat mode exactly as before.
 * Every function here is safe to call in a build without OpenXR support - it then does nothing
 * and reports that VR is unavailable.
 *
 * Startup order (see \ref ArxGame::initialize):
 *  1. \ref initialize - create the OpenXR instance and find the headset (no OpenGL needed yet)
 *  2. the window and the OpenGL context are created
 *  3. \ref createSession - bind the OpenXR session to that OpenGL context
 *  4. \ref pollEvents every frame, \ref shutdown at exit
 */
namespace vr {

//! \return true if the game was built with OpenXR support
bool isSupported();

/*!
 * \brief Create the OpenXR instance and find the head-mounted display
 *
 * Does nothing and returns false when VR was disabled with \c --no-vr, when no OpenXR runtime or
 * headset is available, or when the active runtime is SteamVR and SteamVR is not running (creating
 * an instance would start SteamVR behind the player's back). The reason is logged.
 *
 * \return true if the game will run in VR
 */
bool initialize();

/*!
 * \brief Create the OpenXR session bound to the current OpenGL context
 *
 * Must be called from the thread that owns the window's OpenGL context, after the renderer has
 * been initialized. On failure VR is shut down and the game continues in flat mode.
 *
 * \return true if the session was created
 */
bool createSession();

//! Poll OpenXR events (session state changes, instance loss) - call once per frame
void pollEvents();

//! \return true while the game runs in VR, i.e. an OpenXR session exists
bool isEnabled();

//! Destroy the OpenXR session and instance (if any)
void shutdown();

} // namespace vr

#endif // ARX_VR_VRSYSTEM_H
