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

#ifndef ARX_VR_PERFSTATS_H
#define ARX_VR_PERFSTATS_H

#include <stddef.h>
#include <array>
#include <string>
#include <vector>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The performance log's numbers: per-second frame time stats and the hitch detector (#327)
 *
 * Pure logic over durations: no clock reads, no engine globals, so it is covered by a host unit test
 * (tests/vr/PerfStatsTest.cpp). The game side (vr/PerfLog.h) times the phases of every frame and
 * feeds one \ref PerfFrame per frame; every second of the run a \ref PerfSecond comes out, and every
 * frame over \ref HitchFactor expected frame times is a \ref PerfHitch with what ran in it. Both
 * become one line of \c arx-perf.log, read by \c scripts/dev/perf-summary.ps1.
 *
 * The phases are exclusive: a phase entered inside another one pauses the outer one (a level load
 * started by a channel command counts as \c load, not as \c channel), so the phases of a frame add
 * up to the frame and \c other is what no phase claimed.
 */
namespace vr {

//! Where the time of a frame went
enum class PerfPhase : u8 {
	Other,     //!< not inside any timed phase
	Input,     //!< OpenXR events, the controllers' actions, the engine's input
	Channel,   //!< the test channel: its commands and status.json
	Load,      //!< a level or save game load, a new game
	Save,      //!< a save game
	Update,    //!< the game's update (entities, player, spells, damages, ...) not in the phases below
	Script,    //!< the script timers and the script event stack
	Physics,   //!< ARX_PHYSICS_Apply
	Particles, //!< the particle systems and the other short-lived effects
	Render,    //!< drawing: the scene (per eye in VR), the interface, menus, cinematics, the VR layers
	Textures,  //!< texture uploads to the GPU (inside render or load)
	Present,   //!< swapping the window's buffers
	Limit,     //!< the flat mode frame limiter's sleep (cfg.ini fps_limit; never in VR)
	VrWait,   //!< xrWaitFrame and the even-cadence cap's sleep: the headset paces the game here
	VrEnd,     //!< xrEndFrame
	Count
};

const size_t PerfPhaseCount = size_t(PerfPhase::Count);

//! The phase's name in the log: "other", "input", ..., "vrwait", "vrend"
const char * perfPhaseName(PerfPhase phase);

//! One finished frame, from its start to the next frame's start (microseconds)
struct PerfFrame {

	s64 start = 0;    //!< since the log started
	s64 duration = 0; //!< to the next frame's start
	s64 period = 0;   //!< the display period (OpenXR's predicted one in VR, the monitor's in flat mode), 0: unknown
	u32 divisor = 1;  //!< display periods per expected frame (the VR even-cadence cap)
	bool vr = false;  //!< an OpenXR frame loop paced the frame
	u32 missed = 0;   //!< display slots OpenXR's predicted display time skipped beyond the expected ones
	u32 uploads = 0;  //!< textures uploaded
	std::array<s64, PerfPhaseCount> phases = { }; //!< exclusive time per phase; sums to \ref duration

	//! The frame time the display expects: period x divisor (0: unknown)
	s64 expected() const { return period * s64(divisor); }

};

//! One second of the run
struct PerfSecond {

	bool valid = false;
	u32 second = 0;        //!< since the log started
	u32 frames = 0;
	double minMs = 0.0, meanMs = 0.0, p95Ms = 0.0, maxMs = 0.0;
	double periodMs = 0.0; //!< mean display period, 0: unknown
	u32 divisor = 1;
	bool vr = false;       //!< every frame of the second was a VR frame
	u32 missed = 0;
	u32 hitches = 0;
	u32 uploads = 0;
	std::array<double, PerfPhaseCount> phaseMs = { }; //!< mean per frame
	double selfUs = 0.0;   //!< the log's own work per frame (set by the caller), microseconds

};

//! A frame is a hitch when it takes more than this many expected frame times
const double HitchFactor = 1.5;

//! A frame over \ref HitchFactor expected frame times, with what ran in it
struct PerfHitch {

	bool detected = false;
	u64 frame = 0;         //!< frames since the log started
	double atS = 0.0;      //!< the frame's start, seconds since the log started
	double ms = 0.0;
	double periodMs = 0.0;
	double slots = 0.0;    //!< the frame in display periods
	u32 missed = 0;
	u32 uploads = 0;
	PerfPhase cause = PerfPhase::Other; //!< the phase that took the most time
	std::array<double, PerfPhaseCount> phaseMs = { };

};

//! Accumulates frames into seconds and finds the hitches
class PerfMonitor {

public:

	/*!
	 * \brief Feed a finished frame
	 *
	 * \param hitch receives the hitch verdict for this frame (optional)
	 * \return true when the frame started a new second and \ref second() holds the one it closed
	 */
	bool add(const PerfFrame & frame, PerfHitch * hitch = nullptr);

	//! Close the open second (the log ends); false when it had no frames
	bool flush();

	//! The last closed second
	const PerfSecond & second() const { return m_second; }

	u64 frames() const { return m_frames; }

private:

	//! What the open second has seen so far
	struct Open {
		u32 second = 0;
		u32 frames = 0;
		u32 vrFrames = 0;
		s64 periodSum = 0;
		u32 periodFrames = 0;
		u32 divisor = 1;
		u32 missed = 0;
		u32 hitches = 0;
		u32 uploads = 0;
		std::array<s64, PerfPhaseCount> phases = { };
		std::vector<s64> durations;
	};

	void close();

	Open m_open;
	PerfSecond m_second;
	u64 m_frames = 0;

};

//! The second as one log line: "S t=12 frames=90 min=... | other=... vrend=... | uploads=0 self=0.4"
std::string formatSecond(const PerfSecond & second);

//! The hitch as one log line: "H t=12.345 frame=1080 ms=243.1 ... cause=load | other=... vrend=..."
std::string formatHitch(const PerfHitch & hitch);

} // namespace vr

#endif // ARX_VR_PERFSTATS_H
