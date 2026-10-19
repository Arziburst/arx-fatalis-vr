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

#ifndef ARX_PLATFORM_CRASHNOTICE_H
#define ARX_PLATFORM_CRASHNOTICE_H

#include <cstddef>
#include <string>

#include "io/fs/FilePath.h"

/*!
 * \file
 * \brief The next-launch crash notice (#545, M6): which report folders are still to be announced
 *
 * With the dump helper on, a crash shows no "Fatal Error" dialog; at the next launch the main menu (flat and on
 * the VR panel) says "The game ended with an error last time. A report was saved." with [Open folder] and [OK],
 * once per new report. A folder is new while it has a report.txt and no marker; OK writes the marker into every
 * such folder. A report of kind=terminated (a TerminateProcess from outside: SteamVR's own quit, the task
 * manager) is never announced - the owner quits SteamVR on purpose. hangs\ is never looked at: the game ran on.
 * Pure file logic, covered by tests/platform/CrashNoticeTest.cpp and CrashNoticeKindTest.cpp.
 */
namespace platform {

//! The file a report folder gets when its notice was dismissed with OK: <user dir>\crashes\<name>\notice-shown.txt
extern const char * const CrashNoticeMarker;

/*!
 * \brief The name of the newest (by folder name) report folder in \a crashesDir that is still to be announced
 *
 * Empty when there is none or the folder does not exist. A folder without report.txt (one the dump helper is
 * still writing, an old crash.txt-only report) is no report; one whose report.txt says kind=terminated is not
 * announced either.
 */
std::string findUnseenCrashReport(const fs::path & crashesDir);

//! OK: every report folder findUnseenCrashReport() would consider gets the marker; returns how many got it
size_t markCrashReportsShown(const fs::path & crashesDir);

} // namespace platform

#endif // ARX_PLATFORM_CRASHNOTICE_H
