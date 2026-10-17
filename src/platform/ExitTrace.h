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

#ifndef ARX_PLATFORM_EXITTRACE_H
#define ARX_PLATFORM_EXITTRACE_H

#include <string_view>

#include "io/fs/FilePath.h"

/*!
 * \file
 * \brief Every way the process ends names itself in arx.log (#146)
 *
 * The game's own shutdown logs "Clean shutdown" and a crash writes a crash report. The rest ended
 * #146 without a trace: exit() or ExitProcess called inside the process - by our code or by a
 * library (an OpenXR runtime's client DLL, a driver). The hooks report such an end in arx.log and
 * on stderr: how the process ends, its exit code where known, the last quit request or VR session
 * end noted before it, and the stack that called it as module+offset.
 *
 *  - an atexit handler sees exit() of this module's C runtime (and names its caller);
 *  - on Windows (MSVC) a TLS callback of the executable sees every ExitProcess, whichever module
 *    calls it: the loader calls it while the process is torn down.
 *
 * A TerminateProcess from outside (or the crash handler's own) runs no code in the process: the
 * hooks cannot see it, and its absence from arx.log says that is what happened.
 *
 * The hooks only log and flush: they never change how or with which code the process ends.
 */
namespace platform {

/*!
 * \brief Install the exit hooks
 *
 * \param logFile  the log the report is appended to directly (the logger may be gone by then)
 * \param flushLog writes the logger's buffered lines without waiting for its locks
 *                 (Logger::quickShutdown), so the last lines before the end reach the file first
 */
void initializeExitTrace(const fs::path & logFile, void (*flushLog)());

/*!
 * \brief Remember what may end the game next - a quit request, the VR session ending
 *
 * The exit report names the last cause noted and how long before the end it came. Cheap: a copy
 * into a fixed buffer, no lock, no log line.
 */
void noteExitCause(std::string_view cause);

//! Log a quit request ("Quit requested: <who>") and note it as the cause of the coming exit
void logQuitRequest(std::string_view who);

//! The game's own shutdown is done: the hooks stay quiet from now on
void markCleanExit();

} // namespace platform

#endif // ARX_PLATFORM_EXITTRACE_H
