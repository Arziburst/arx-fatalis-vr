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

#ifndef ARX_CORE_COMMANDCHANNEL_H
#define ARX_CORE_COMMANDCHANNEL_H

#include "graphics/BaseGraphicsTypes.h"

/*!
 * \brief File-based command channel and status file for automated tests
 *
 * Enabled with \c --test-dir DIR. While enabled, the game
 *  - polls \c DIR/command.txt (one command per line, consumed on read, ignored if it is older
 *    than the process),
 *  - answers every batch in \c DIR/ack.txt (\c seq=N \c frame=F \c errors=E \c last=... \c error=...),
 *  - rewrites \c DIR/status.json a few times per second and after every batch (atomically:
 *    \c status.json.tmp then rename),
 *  - flushes \c arx.log at the same cadence (the file logger flushes on its own every second and
 *    after every warning or error, #129; a test wants the log fresher than that),
 *  - logs every game state change as \c [game] \c state: \c MENU|LOADING|GAMEPLAY|CINEMATIC.
 *
 * Channel commands: \c status, \c shot \c [name], \c quit, \c mark \c <text>, \c loadslot \c <n>,
 * \c loadlevel \c <n>, \c newgame, \c savegame \c <name>, \c entities \c [filter],
 * \c setpos \c <x> \c <y> \c <z> \c [yaw], \c lookat \c <entity>, \c drawcheck \c [frames], \c recenter (refused until #28). Any other line goes
 * to the script console exactly as if typed there: \c [entity.]command \c args.
 *
 * See docs/vr/TESTING.md for the protocol and scripts/dev/smoke-test.ps1 for the first client.
 */
namespace channel {

//! \return true if \c --test-dir was given
bool isEnabled();

//! Create the directory, remember the process start, start counting log errors - call after the log file exists
void initialize();

//! Poll the command file, run commands, write status and ack, flush the log - once per frame
void update();

//! Take a pending screenshot - call after the frame was rendered and before it is shown
void afterRender();

/*!
 * How many times to draw the scene view in this level frame: 2 while a \c drawcheck runs, else 1.
 * The two views use the same camera; afterSceneView() compares their pictures.
 */
size_t sceneViewCount();

//! Scene view \a view of this frame is in the back buffer - the draw check reads it back
void afterSceneView(size_t view);

//! The game started loading a level - logs \c [game] \c state: \c LOADING and writes the status now
void notifyLoading(AreaId area);

//! Write the final status (\c state: \c EXITED) and stop - call after "Clean shutdown" was logged
void shutdown();

} // namespace channel

#endif // ARX_CORE_COMMANDCHANNEL_H
