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

#ifndef ARX_VR_VRSIMULATOR_H
#define ARX_VR_VRSIMULATOR_H

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/Platform.h"
#include "vr/EyeImage.h"
#include "vr/SimRig.h"

/*!
 * \brief The simulated VR runtime (\c --vr-sim \c <script>, #112, research rows F-04 / F-06 / F-165)
 *
 * VR without a headset and without a runtime: the simulator answers every OpenXR call of the VR
 * module through the runtime interface (vr/XRFunctions.h) - session states, the frame loop,
 * swapchains of real OpenGL textures, eye poses and controllers from the script - so the game
 * runs its real VR code path, offscreen, and a worker can look at what each eye would see.
 *
 * Its compositor counts how the eye images pair up per frame and, on \c shot, composes what each
 * eye sees (the projection layer with the head-locked quad on top), saves it and judges it:
 * \ref ImageVerdict per eye and for the flat frame, the left/right difference and parallax shift,
 * and the difference to every earlier shot. scripts/dev/eye-check.ps1 turns those numbers into the
 * two-eyes check. The script language is in vr/SimRig.h; docs/vr/TESTING.md has the how-to.
 */
namespace vr {

//! \return true if \c --vr-sim was given
bool simRequested();

/*!
 * \brief Load the \c --vr-sim script and put the simulator behind the runtime interface
 *
 * After this every OpenXR call of the VR module reaches the simulator, never the OpenXR loader.
 * \return false if the script could not be read or has an error (logged with its line)
 */
bool installSimulator();

//! \return true while the simulator answers the OpenXR calls
bool isSimulated();

/*!
 * \brief Run a script command now (the channel's \c vr-sim, see vr/SimRig.h)
 * \return an empty string, or why the command was refused
 */
std::string simCommand(std::string_view line);

//! Where shots are saved (the channel's capture directory); without it, \c vrsim in the user directory
void setSimCaptureDir(const std::string & dir);

//! One eye of a shot
struct SimShotEye {
	std::string file;    //!< what the eye sees, composed
	PixelStats scene;    //!< the composed picture around where the eye looks
	PixelStats layer;    //!< the projection layer alone, same region
	Vec3f position = Vec3f(0.f);  //!< the eye as submitted, in the room
	Vec3f angles = Vec3f(0.f);    //!< yaw, pitch, roll in degrees
	u64 imageFrame = 0;  //!< the frame the eye image was released in
};

//! A shot: both eyes and the flat frame quad, judged
struct SimShot {
	std::string name;
	u64 frame = 0;          //!< simulator frame
	u64 timelineFrame = 0;
	bool timelineRunning = false;
	std::array<SimShotEye, 2> eyes;
	bool quadShown = false;
	std::string quadFile;
	PixelStats quad;        //!< the flat frame, its middle
	size_t quadPixels[2] = { 0, 0 }; //!< eye pixels the quad covers
	//! eye pixels the quad layers after the first cover - the menu pointer's beam above the panel (#34)
	size_t overlayPixels[2] = { 0, 0 };
	float lrDiff = -1.f;    //!< mean absolute luma difference of the two composed eyes
	ShiftEstimate lrShift;  //!< left eye picture shifted right of the right eye's (> 0 is right)
	bool swapEyes = false;  //!< the swap-eyes fault was on
	//! Per earlier shot: the mean absolute luma difference of each eye to that shot
	std::vector<std::pair<std::string, std::array<float, 2>>> versus;
};

//! The simulator for status.json
struct SimStatus {
	bool active = false;
	std::string script;
	std::string timeline;  //!< "waiting for gameplay", "running", "done"
	u64 frame = 0;         //!< frames the simulator paced
	u64 timelineFrame = 0;
	size_t pending = 0;    //!< timeline lines not yet run
	SimView view;
	SimRig rig;
	bool located = false;
	std::array<SimEye, 2> eyes; //!< as last located in the room
	//! The compositor: frames ended, with a projection layer, both eye images from one frame,
	//! from different frames (a stale eye), released in the frame they were shown in, with the quad
	u64 endedFrames = 0;
	u64 projectionFrames = 0;
	u64 pairedFrames = 0;
	u64 staleFrames = 0;
	u64 freshFrames = 0;
	u64 quadFrames = 0;
	u64 syncs = 0;         //!< xrSyncActions while focused
	std::vector<SimShot> shots;
	size_t pendingShots = 0;
};

SimStatus simStatus();

} // namespace vr

#endif // ARX_VR_VRSIMULATOR_H
