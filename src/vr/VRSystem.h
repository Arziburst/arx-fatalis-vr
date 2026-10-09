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

#include <array>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/FramePacing.h"

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
 *  4. every frame: \ref pollEvents (begins and ends the session as the runtime asks),
 *     \ref beginFrame at the start of the frame, \ref submitFrame before the buffer swap;
 *     \ref shutdown at exit
 *
 * The frame loop (#20): while the session runs, \ref beginFrame waits for the headset's next
 * display slot (\c xrWaitFrame - the only clock that paces the game in VR, D-024), begins the
 * frame and locates the eye poses for its predicted display time; the game simulates and renders;
 * \ref submitFrame hands the frame to the compositor with exactly those poses (pose-matched
 * submission, research rows F-21 and F-116). A frame shown from outside the game loop (the
 * loading screen) begins its own frame inside \ref submitFrame.
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
 * \brief Start a game frame in VR: wait for the headset's next display slot and locate the eyes
 *
 * Blocks in \c xrWaitFrame until the runtime wants the next frame rendered - this is what paces
 * the game in VR, so the caller skips its own frame limiter when this returns true. With the
 * even-cadence cap (\c vr_frame_divisor, F-26) the call first sleeps past the slots to skip, so
 * the runtime hands out every n-th slot. Then \c xrBeginFrame and \c xrLocateViews for the frame's
 * predicted display time: the poses in \ref frameViews are the ones this frame must render with.
 *
 * \return true while an OpenXR session runs and the frame was begun; false in flat mode, while
 *         the session is not running, or when the runtime refused (logged)
 */
bool beginFrame();

/*!
 * \brief Show the finished flat frame in the headset
 *
 * Call after the frame was rendered into the window's back buffer and before the buffers are
 * swapped. Ends the OpenXR frame begun by \ref beginFrame (or begins one first, for frames shown
 * from outside the game loop): a uniform dark background in the per-eye swapchains as a projection
 * layer with the poses located for this frame, and in front of it a copy of the back buffer on a
 * head-locked quad (F-20). Frames the runtime does not want rendered are ended without layers.
 * Then the desktop mirror (#21, F-24) replaces the back buffer with what the left eye sees: the
 * left eye image cropped to the window, the flat frame where the quad appears in that eye.
 * Does nothing while no OpenXR session is running.
 *
 * \param windowSize size of the window's back buffer
 */
void submitFrame(const Vec2i & windowSize);

//! One eye as located for the current frame, in the world-locked reference space (meters)
struct EyeView {
	Vec3f position = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	float fovLeft = 0.f;  //!< field of view half-angles in radians, OpenXR convention
	float fovRight = 0.f; //!< (left and down are negative)
	float fovUp = 0.f;
	float fovDown = 0.f;
};

//! The eye poses the current frame renders with and is submitted with (F-21, F-116)
struct FrameViews {
	bool valid = false;         //!< the runtime located both eyes with a valid pose
	s64 displayTime = 0;        //!< the frame's predicted display time, OpenXR nanoseconds
	std::array<EyeView, 2> eyes; //!< left, right
};

/*!
 * \return the eye poses located by \ref beginFrame for the current frame - the sample the camera
 *         (#24) and the per-eye render (#19) take; \c valid is false in flat mode and before the
 *         first frame
 */
const FrameViews & frameViews();

//! \return true while the game runs in VR, i.e. an OpenXR session exists
bool isEnabled();

//! \return the current OpenXR session state as text ("none" without a session) - for status.json
const char * sessionStateString();

//! OpenXR frame counters since the game started - for status.json
struct FrameCounters {
	u64 submitted = 0; //!< frames ended with \c xrEndFrame
	u64 shown = 0;     //!< of those, frames the runtime wanted rendered and that carried layers
	u64 resubmitted = 0; //!< of those, frames ended with the previous picture because no new one was shown
	u64 discarded = 0; //!< frames the runtime discarded (\c xrBeginFrame called twice)
	u64 gaps = 0;      //!< frame gaps detected (see \ref FrameGap)
};

FrameCounters frameCounters();

/*!
 * \return the last rate / cadence report of the frame loop (F-07): one per 3 s window while the
 *         session runs, kept after it ended; \c valid is false before the first window closed
 */
const PacingReport & lastPacingReport();

//! \return how many rate / cadence reports were produced since the game started
u32 pacingReports();

//! \return the expected display slots per frame: 1 for every slot, 2 for every second slot, ...
u32 frameDivisor();

//! The desktop mirror (#21): the window shows what the left eye sees - for status.json
struct MirrorStatus {
	std::string state = "flat mode"; //!< "on", or why the window shows the flat frame ("off: ...")
	u64 frames = 0;                  //!< frames mirrored into the window since the game started
};

MirrorStatus mirrorStatus();

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

/*!
 * \brief End the session in order and destroy the OpenXR session and instance (if any)
 *
 * A running session is asked to exit (\c xrRequestExitSession) and given a moment to reach
 * \c STOPPING (then \c xrEndSession) and \c EXITING, so the runtime sees an orderly exit instead
 * of a session destroyed while running.
 */
void shutdown();

} // namespace vr

#endif // ARX_VR_VRSYSTEM_H
