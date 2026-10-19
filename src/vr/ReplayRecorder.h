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

#ifndef ARX_VR_REPLAYRECORDER_H
#define ARX_VR_REPLAYRECORDER_H

#include <stddef.h>
#include <string>
#include <string_view>

/*!
 * \file
 * \brief The replay recorder (#550): every VR session written to arx-replay.log, the last 60 s in a ring
 *
 * From the first frame the game is playable in VR, every frame goes into <user dir>\c \\arx-replay.log as a line
 * of the clip dialect (vr/ReplayClip.h): what the runtime gave the game and where the player was. A save is made
 * at the record start (a replay slot the load menu does not list, core/SaveGame.cpp) and named in the header
 * with the RNG seed and the display rate. At the session's end the file is gzipped (arx-replay.log.gz) and the
 * next run rotates the old ones like arx-perf.log. The same frames go into a ring of the last 60 s that lives in
 * the dump helper's shared memory (platform/DumpHelper.h), so every crash and hang report carries arx-replay.vrsim;
 * the channel's \c replay \c mark \c <name> \c [<seconds>] cuts the last 30 s of it into
 * replay-marks\c \\<time>-<name>.vrsim.
 *
 * \c --no-replay-log turns the recorder off; it is off in flat mode. \c --seed \c <n> seeds the main thread's RNG
 * (the clip records the seed used either way). The lines are written off the frame path (vr/PerfWriter.h).
 */
namespace vr {
namespace replay {

//! Once per frame, after the input was read and the channel ran (ArxGame::doFrame): record it, start the record first
void frame();

//! Close the session's clip (gzip): the game shuts down
void shutdown();

//! The channel's `replay ...` command (mark <name> [<seconds>]): "" or why it was refused
std::string command(std::string_view rest);

//! The player saved the game: an "anchor save" line goes into the clip
void noteSave();

//! True while a session is recorded
bool recording();

//! The bytes the ring wants in the dump helper's shared memory (60 s at the fastest display)
size_t ringBytes();

} // namespace replay
} // namespace vr

#endif // ARX_VR_REPLAYRECORDER_H
