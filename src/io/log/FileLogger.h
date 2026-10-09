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

#ifndef ARX_IO_LOG_FILELOGGER_H
#define ARX_IO_LOG_FILELOGGER_H

#include <stddef.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "io/fs/FileStream.h"
#include "io/log/LogBackend.h"

namespace fs { class path; }

namespace logger {

/*!
 * Logger that writes plain text to a file and keeps that file current on disk while the game
 * runs (#129, research row F-92), so a hang, a kill or a crash loses at most \a flushInterval
 * of the log instead of everything since the last 4 KB:
 *  - a warning, error or critical line is flushed as soon as it is written;
 *  - everything else is flushed by a flush thread at least every \a flushInterval - also while
 *    the main thread is stuck in a level load or a hang and never reaches the frame loop;
 *  - info lines are never flushed one by one: a flush per line was a measured stutter source
 *    in Dishonored VR (F-92), and the engine logs in bursts.
 */
class File final : public Backend {

	fs::ofstream ofs;

	std::mutex m_mutex; //!< Guards \c ofs and \c m_dirty between the logging threads and the flush thread
	std::condition_variable m_wake;
	std::chrono::milliseconds m_interval;
	bool m_dirty; //!< Something was written since the last flush
	bool m_stop; //!< The flush thread is asked to finish (written under \c m_mutex)
	std::thread m_flusher;

	void flushLoop();

public:

	/*!
	 * \param path          The file to write; created or truncated / appended to as \a mode says.
	 * \param mode          Open mode: truncate for a fresh log, append when the previous run's
	 *                      log must be preserved (a failed rotation, see rotateLogs()).
	 * \param flushInterval How long an unflushed line may wait for the flush thread.
	 */
	explicit File(const fs::path & path,
	              std::ios_base::openmode mode = std::ios_base::out | std::ios_base::trunc,
	              std::chrono::milliseconds flushInterval = std::chrono::seconds(1));

	~File() override;

	void quickShutdown() override;

	void log(const Source & file, int line, Logger::LogLevel level, std::string_view str) override;

	void flush() override;

};

/*!
 * Keep the logs of the previous runs (#129, research row F-92): \c name.ext becomes
 * \c name.1.ext, which becomes \c name.2.ext, and so on up to \c name.<keep-1>.ext - the oldest
 * one is dropped. Nothing is deleted or truncated otherwise: a file that cannot be moved stops
 * the rotation where it is, so a tester's earlier evidence survives a locked archive.
 *
 * \param file The current log, e.g. \c arx.log; nothing happens when it does not exist.
 * \param keep How many runs to keep in total, the new one included (\c arx.log and
 *             \c arx.1.log .. \c arx.<keep-1>.log).
 * \return An empty string when every file was moved (or there was nothing to move), otherwise
 *         what went wrong - for the log of the run that then appends to \a file instead.
 */
std::string rotateLogs(const fs::path & file, size_t keep);

} // namespace logger

#endif // ARX_IO_LOG_FILELOGGER_H
