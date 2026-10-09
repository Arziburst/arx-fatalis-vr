/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
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

#include "io/log/FileLogger.h"

#include <sstream>

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"

namespace logger {

File::File(const fs::path & path, std::ios_base::openmode mode, std::chrono::milliseconds flushInterval)
	: ofs(path, mode)
	, m_interval(flushInterval)
	, m_dirty(false)
	, m_stop(false)
{
	m_flusher = std::thread(&File::flushLoop, this);
}

File::~File() {
	{
		std::scoped_lock lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	if(m_flusher.joinable()) {
		m_flusher.join();
	}
	// ofs flushes and closes itself
}

void File::flushLoop() {
	std::unique_lock lock(m_mutex);
	while(!m_stop) {
		m_wake.wait_for(lock, m_interval);
		if(m_dirty) {
			ofs.flush();
			m_dirty = false;
		}
	}
}

void File::log(const Source & file, int line, Logger::LogLevel level, std::string_view str) {
	std::scoped_lock lock(m_mutex);
	format(ofs, file, line, level, str);
	if(level >= Logger::Warning) {
		// The line that matters most must not be the one still in the buffer when the game dies
		ofs.flush();
		m_dirty = false;
	} else {
		m_dirty = true;
	}
}

void File::flush() {
	std::scoped_lock lock(m_mutex);
	ofs.flush();
	m_dirty = false;
}

void File::quickShutdown() {
	// Called by the crash handler on the crashing thread, outside the logger's locks: the flush
	// thread may be inside ofs.flush() right now, and closing the stream under it would risk a
	// second fault inside the crash handler. Wait a moment for the lock instead; if it stays
	// busy (the crash happened inside the logger itself) give up the tail of the log - everything
	// older than one flush interval is on disk already, and the process ends either way.
	for(int tries = 0; tries < 200; tries++) {
		if(m_mutex.try_lock()) {
			m_stop = true;
			ofs.close();
			m_mutex.unlock();
			m_wake.notify_all();
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

std::string rotateLogs(const fs::path & file, size_t keep) {

	if(keep < 2 || !fs::is_regular_file(file)) {
		// Nothing to keep - a first run, or the previous run never wrote its log
		return std::string();
	}

	// arx.log -> arx.1.log -> arx.2.log -> ... : the number sits between the basename and the extension
	auto numbered = [&file](size_t n) {
		fs::path p = file;
		return p.set_basename(std::string(file.basename()) + '.' + std::to_string(n));
	};

	// Oldest first, so that every move lands on a name that was just freed (or on the one dropped)
	for(size_t i = keep - 1; i >= 1; i--) {
		fs::path from = (i == 1) ? file : numbered(i - 1);
		fs::path to = numbered(i);
		if(!fs::is_regular_file(from)) {
			continue;
		}
		if(!fs::rename(from, to, true)) {
			std::ostringstream oss;
			oss << "could not move " << from << " to " << to;
			return oss.str();
		}
	}

	return std::string();
}

} // namespace logger
