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

#ifndef ARX_CORE_BUGREPORT_H
#define ARX_CORE_BUGREPORT_H

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "io/fs/FilePath.h"

/*!
 * \brief A save exported for a bug report (#417): one zip a player sends with the report
 *
 * The zip holds the chosen save (\c <slot>/gsave.sav), \c arx.log, \c arx-perf.log and \c cfg.ini. The three text
 * files are scrubbed of personal data first, with the rules of the bug filing (#387, \c Protect-ArxText in
 * scripts/dev/log-digest-lib.ps1): home folders, Steam ids, network paths, e-mails, GUIDs, Windows SIDs, MAC
 * and IPv4 addresses, and the account and machine names where they stand alone. The save is binary and holds none
 * of them.
 * \c scripts/dev/repro-save.ps1 loads such a zip in one command.
 *
 * The scrubber and the zip writer are pure and covered by tests/core/BugReportTest, the export into a folder too.
 */
namespace bugreport {

//! The account and machine names to scrub: only names of four characters or more that are not generic
struct PersonalNames {
	std::vector<std::string> users;
	std::vector<std::string> machines;
};

//! \a users and \a machines without the names that are too short or generic to replace ("Admin", "User", ...)
PersonalNames usableNames(const std::vector<std::string> & users, const std::vector<std::string> & machines);

//! The names of this machine's account and computer (USERNAME, COMPUTERNAME, USERDOMAIN)
PersonalNames systemNames();

//! \a text with its personal data replaced by \c <home>, \c <steam-id>, \c <path>, \c <email>, \c <user>, ...
std::string scrub(std::string_view text, const PersonalNames & names);

struct ZipEntry {
	std::string name; //!< the path inside the zip, with forward slashes
	std::string data;
};

//! A zip archive of \a entries, stored without compression, every entry dated \a time (local time)
std::string makeZip(const std::vector<ZipEntry> & entries, std::time_t time);

//! The zip's file name for a report made at \a time: arxvr-report-YYYYMMDD-HHMMSS.zip
std::string reportFileName(std::time_t time);

//! What \ref exportReport did
struct ExportResult {
	bool ok = false;
	fs::path file;                   //!< the zip that was written
	std::vector<std::string> files;  //!< the entries in it
	std::string why;                 //!< why nothing was written, or what is missing from the zip
};

/*!
 * Write the zip of the save \a savefile (its folder becomes the entry's folder) with \a userDir's arx.log and
 * arx-perf.log and \a configDir's cfg.ini into \a targetDir, scrubbed with \a names. A missing log or config is left
 * out and named in \c why; a missing save writes nothing.
 */
ExportResult exportReport(const fs::path & savefile, const fs::path & userDir, const fs::path & configDir,
                          const fs::path & targetDir, std::time_t time, const PersonalNames & names);

} // namespace bugreport

#endif // ARX_CORE_BUGREPORT_H
