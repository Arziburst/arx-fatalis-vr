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

#include <string>

#include "math/Types.h"
#include "platform/Platform.h"

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
 *  3. \ref createSession - bind the OpenXR session to that OpenGL context, create the swapchains
 *  4. \ref pollEvents every frame (begins and ends the session as the runtime asks),
 *     \ref submitFrame before every buffer swap, \ref shutdown at exit
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

/*!
 * \brief Poll OpenXR events (session state changes, instance loss) - call once per frame
 *
 * Begins the session when the runtime reports it ready and ends it when the runtime stops it.
 */
void pollEvents();

/*!
 * \brief Show the finished flat frame in the headset
 *
 * Call after the frame was rendered into the window's back buffer and before the buffers are
 * swapped. Submits one OpenXR frame: a uniform dark background from the per-eye swapchains and,
 * in front of it, a copy of the back buffer on a head-locked quad (F-20 in
 * docs/vr/RESEARCH-reference-mods.md). Waits for the headset's frame timing (\c xrWaitFrame).
 * Does nothing while no OpenXR session is running.
 *
 * \param windowSize size of the window's back buffer
 */
void submitFrame(const Vec2i & windowSize);

//! \return true while the game runs in VR, i.e. an OpenXR session exists
bool isEnabled();

//! \return the current OpenXR session state as text ("none" without a session) - for status.json
const char * sessionStateString();

//! OpenXR frame counters since the game started - for status.json
struct FrameCounters {
	u64 submitted = 0; //!< frames ended with \c xrEndFrame
	u64 shown = 0;     //!< of those, frames the runtime wanted rendered and that carried layers
};

FrameCounters frameCounters();

/*!
 * \return the OpenXR runtime that answered this run, as "<name> <version>" - empty while no
 *         instance was created (for status.json; stays set after \ref shutdown)
 */
std::string runtimeName();

/*!
 * \return the active OpenXR runtime manifest the loader uses, found the way the loader finds it
 *         (\c XR_RUNTIME_JSON, then the registry on Windows) - empty when no runtime is installed
 */
std::string runtimeManifest();

//! Destroy the OpenXR session and instance (if any)
void shutdown();

} // namespace vr

#endif // ARX_VR_VRSYSTEM_H
