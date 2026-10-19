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

#include "platform/CrashNotice.h"

#include <algorithm>
#include <vector>

#include "io/fs/Filesystem.h"

namespace platform {

const char * const CrashNoticeMarker = "notice-shown.txt";

namespace {

//! The report.txt says kind=terminated: a kill from outside, not an error of the game
bool reportIsTerminated(const fs::path & folder) {
	std::string text = fs::read(folder / "report.txt");
	// The kind= line is the first; a report without one is announced (better once too often than never)
	return text.compare(0, 16, "kind=terminated\n") == 0 || text.compare(0, 17, "kind=terminated\r\n") == 0
	       || text == "kind=terminated";
}

//! The report folders still to be announced: a report.txt, no marker, not a kill from outside
std::vector<std::string> unseenReports(const fs::path & crashesDir) {
	std::vector<std::string> names;
	if(!fs::is_directory(crashesDir)) {
		return names;
	}
	for(fs::directory_iterator it(crashesDir); !it.end(); ++it) {
		if(!it.is_directory()) {
			continue;
		}
		fs::path folder = crashesDir / it.name();
		if(!fs::is_regular_file(folder / "report.txt") || fs::exists(folder / CrashNoticeMarker)) {
			continue;
		}
		if(reportIsTerminated(folder)) {
			continue;
		}
		names.push_back(it.name());
	}
	std::sort(names.begin(), names.end());
	return names;
}

} // anonymous namespace

std::string findUnseenCrashReport(const fs::path & crashesDir) {
	std::vector<std::string> names = unseenReports(crashesDir);
	return names.empty() ? std::string() : names.back();
}

size_t markCrashReportsShown(const fs::path & crashesDir) {
	size_t marked = 0;
	for(const std::string & name : unseenReports(crashesDir)) {
		if(fs::write(crashesDir / name / CrashNoticeMarker, "shown\n")) {
			marked++;
		}
	}
	return marked;
}

} // namespace platform
