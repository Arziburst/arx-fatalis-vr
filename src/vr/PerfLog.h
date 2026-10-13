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

#ifndef ARX_VR_PERFLOG_H
#define ARX_VR_PERFLOG_H

#include <string_view>

#include "platform/Platform.h"
#include "vr/PerfStats.h"

/*!
 * \file
 * \brief The always-on performance log, \c arx-perf.log next to \c arx.log (#327)
 *
 * Every run writes one line per second (frame time min / mean / p95 / max, the display rate,
 * the display slots OpenXR skipped, the mean time per frame of every phase) and one line per hitch
 * (a frame over 1.5 expected frame times, with the phases that ran in it), so a headset test leaves
 * the evidence without anyone asking for it. \c --no-perf-log turns it off. The numbers are
 * \ref PerfMonitor's; the file is written by \ref PerfWriter's thread, never on the frame path.
 * \c scripts/dev/perf-summary.ps1 reads it; docs/vr/TESTING.md explains the lines.
 *
 * Phases are timed by \ref Scope on the main thread only (another thread's scope does nothing).
 */
namespace vr::perf {

//! Open the log (after the logger, before the first frame); a no-op with --no-perf-log
void initialize();

//! Write the open second and close the log
void shutdown();

//! The top of a frame: closes the previous one (its second, its hitch) and starts timing this one
void frameStart();

/*!
 * \brief What the current frame is paced by
 *
 * \param periodUs the display period in microseconds, 0 when unknown
 * \param divisor  display periods per frame (the VR even-cadence cap)
 * \param vr       an OpenXR frame loop paces the game
 */
void setDisplay(s64 periodUs, u32 divisor, bool vr);

//! Display slots the runtime's predicted display time skipped in this frame, beyond the expected ones
void addMissed(u32 slots);

//! A texture was uploaded to the GPU in this frame
void countUpload();

//! A line "M t=<s> <text>" between the seconds, e.g. a test channel mark
void mark(std::string_view text);

//! Times a phase of the frame until it goes out of scope; nested phases pause the outer one
class Scope {

	bool m_on;

public:

	explicit Scope(PerfPhase phase) noexcept;
	~Scope();

	Scope(const Scope &) = delete;
	Scope & operator=(const Scope &) = delete;

};

} // namespace vr::perf

#endif // ARX_VR_PERFLOG_H
