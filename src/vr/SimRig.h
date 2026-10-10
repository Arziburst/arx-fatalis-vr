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

#ifndef ARX_VR_SIMRIG_H
#define ARX_VR_SIMRIG_H

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/MirrorGeometry.h"

/*!
 * \file
 * \brief The simulated headset of \c --vr-sim (#112): its script, its rig and the eye poses
 *
 * Pure logic: no OpenXR, no OpenGL, no engine globals, no clock - covered by a host unit test
 * (tests/vr/SimRigTest.cpp). The simulated runtime (vr/VRSimulator.cpp) owns one
 * \ref Simulation, advances it once per frame and answers the game's OpenXR calls from it.
 *
 * Coordinates are OpenXR's: the room is the LOCAL space - x right, y up, -z forward, meters,
 * the origin where the head starts. Angles in a script are degrees.
 *
 * A script is text, one command per line, \c # starts a comment:
 *  - a line without \c @ is set-up: it runs when the script is loaded;
 *  - \c @<frame> \c <command> runs at that frame of the timeline;
 *  - \c @<from>-<to> \c <command> moves the numbers of the command there linearly, from the
 *    values at frame \c from to the command's values at frame \c to (a ramp);
 *  - \c @+<n> is n frames after the previous timed line's last frame.
 * Frame 0 of the timeline is the first frame the game is playable (\c timeline \c gameplay, the
 * default) or the first frame of the session (\c timeline \c session).
 *
 * Commands (the channel's \c vr-sim takes the same):
 *  - \c rate \c <hz>: display rate, 0 = no pacing; \c eye-size \c <w> \c <h> (set-up only);
 *    \c ipd \c <meters>; \c fov \c left|right \c <left> \c <right> \c <up> \c <down> (degrees,
 *    OpenXR signs: left and down negative); \c timeline \c gameplay|session (set-up only)
 *  - \c head \c pos \c <x> \c <y> \c <z>; \c head \c rot \c <yaw> \c <pitch> \c <roll> - yaw + turns
 *    left, pitch + looks up, roll + tilts the head to the left
 *  - \c hand \c left|right \c pos|rot \c ...; \c hand \c left|right \c on|off (tracked or not)
 *  - \c profile \c index|touch|simple|none|<path>: the interaction profile of both hands
 *  - \c action \c <name> \c left|right|both \c <value> \c [<y>]: an OpenXR action of the game
 *    (\c trigger, \c grip, \c button_a, \c thumbstick, ... - vr/VRActions.cpp); booleans are 0 / 1
 *  - \c session \c focused|visible: the runtime gives or takes the input focus; \c session \c idle:
 *    the headset is not worn (lies on the desk) - the session falls back to synchronized, frames are
 *    timed and the eyes located but nothing is displayed, as on an idle Index (#27); \c focused or
 *    \c visible puts it on again
 *  - \c shot \c [name]: capture both eyes and the flat frame quad at the next submitted frame
 *  - \c mark \c <text>: a line in arx.log; \c quit: ask the game to quit
 *  - \c fault \c swap-eyes \c on|off: the compositor shows each eye the other eye's image - the
 *    bug class the two-eyes check (F-06) must catch
 *  - \c layers \c on|off: shots also save each eye's projection layer without the quad
 *    (\c <name>-left-layer.bmp, \c -right-layer.bmp): what the game drew into the eyes themselves (#72)
 */
namespace vr {

//! A pose in the simulated room
struct SimPose {
	Vec3f position = Vec3f(0.f);
	Vec3f angles = Vec3f(0.f); //!< yaw, pitch, roll in degrees
	glm::quat orientation() const;
};

//! \return yaw, pitch, roll in degrees of an orientation - the inverse of \ref SimPose::orientation
Vec3f simAngles(const glm::quat & orientation);

//! One simulated eye as the runtime locates it
struct SimEye {
	Vec3f position = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	MirrorFov fov; //!< radians, OpenXR convention
};

/*!
 * The simulated display: eye image size, eye distance, the four field of view angles per eye,
 * the refresh rate. The angles default to research row F-165 (OpenMW VR's custom stereo view,
 * an HP Reverb G2): asymmetric - more to the outside than to the nose - and mirrored between the
 * eyes, as on every real headset; the Index's own measured angles go in the same place.
 */
struct SimView {
	Vec2i eyeSize = Vec2i(1440, 1600);
	float ipd = 0.064f;
	std::array<MirrorFov, 2> fov;
	float rate = 90.f;
	SimView();
};

//! \return both eyes (left, right) in the room for a head pose
std::array<SimEye, 2> simEyes(const SimView & view, const SimPose & head);

//! \return both eyes (left, right) in the head's own (VIEW) space
std::array<SimEye, 2> simEyesInHead(const SimView & view);

/*!
 * \brief Express a pose in the space of a base pose (both given in the same space)
 *
 * \a position and \a orientation are replaced by the pose as seen from the base - what
 * \c xrLocateSpace answers for a space located in a base space.
 */
void poseInSpace(const Vec3f & basePosition, const glm::quat & baseOrientation, Vec3f & position,
                 glm::quat & orientation);

enum class SimTimeline {
	Gameplay, //!< frame 0 = the first frame the game is playable
	Session,  //!< frame 0 = the first frame of the session
};

//! The state of the simulated headset, controllers and runtime that commands change
struct SimRig {
	SimPose head;
	std::array<SimPose, 2> hands;
	std::array<bool, 2> handTracked = { true, true };
	std::string profile; //!< interaction profile path of both hands, empty = none
	//! Action values by OpenXR action name, per hand (x, and y for 2D actions)
	std::map<std::string, std::array<Vec2f, 2>> actions;
	bool focused = true;
	bool worn = true;     //!< false: the headset lies idle, the session stays synchronized (#27)
	bool swapEyes = false;
	bool saveLayers = false; //!< shots also save each eye's projection layer alone (#72)
	SimRig();
};

//! What a command asks of the simulated runtime beyond the rig
struct SimRequests {
	std::vector<std::string> shots;
	std::vector<std::string> marks;
	bool quit = false;
	bool focusChanged = false;
	bool profileChanged = false;
	bool empty() const {
		return shots.empty() && marks.empty() && !quit && !focusChanged && !profileChanged;
	}
};

//! \return the interaction profile path for a short name (index, touch, simple), or the name itself
std::string simProfilePath(std::string_view name);

class Simulation {

public:

	/*!
	 * \brief Load a script: set-up lines run now, timed lines are checked and put on the timeline
	 * \return an empty string, or what is wrong as "line N: ..." (nothing is applied then)
	 */
	std::string load(std::string_view text);

	/*!
	 * \brief Run one command now (set-up lines, the channel's \c vr-sim)
	 * \return an empty string, or what is wrong with it
	 */
	std::string execute(std::string_view line);

	/*!
	 * \brief Run the timeline up to \a frame: due commands, ramps in progress
	 *
	 * Call once per frame with increasing frames (0 = the first frame of the timeline).
	 */
	void advance(u64 frame);

	//! The session started: set-up-only commands are refused from now on
	void lockSetup() { m_setupLocked = true; }

	//! \return what the commands run since the last call asked for, and forget it
	SimRequests takeRequests();

	//! \return timeline lines not yet run (ramps in progress count until they end)
	size_t pending() const;

	//! \return the last timeline frame advanced to
	u64 frame() const { return m_frame; }

	SimView view;
	SimRig rig;
	SimTimeline timeline = SimTimeline::Gameplay;

private:

	struct Command;
	struct Entry {
		u64 from = 0;
		u64 to = 0;
		size_t line = 0;
		std::string text;
		bool started = false;
		bool done = false;
		std::vector<float> start; //!< a ramp's values at its first frame
	};

	std::string parse(std::string_view line, Command & command) const;
	//! Put "@<when> <command>" on the timeline; @+n counts from \a base; \a last = its last frame
	std::string addTimed(std::string_view line, u64 base, u64 & last);
	void apply(const Command & command);
	std::vector<float *> targets(const Command & command);

	std::vector<Entry> m_timeline;
	SimRequests m_requests;
	u64 m_frame = 0;
	bool m_advanced = false;
	bool m_setupLocked = false;
	u32 m_shots = 0;

};

} // namespace vr

#endif // ARX_VR_SIMRIG_H
