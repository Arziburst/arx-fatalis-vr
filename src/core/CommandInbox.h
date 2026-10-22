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

#ifndef ARX_CORE_COMMANDINBOX_H
#define ARX_CORE_COMMANDINBOX_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/*!
 * \brief Reads the test channel's command.txt off the frame path (#561)
 *
 * The frame looked for command.txt every frame - a stat, and when it was there a read and a remove - and a busy file
 * system made that take 42-62 ms (PR #559's diagnostic, `slow frame part: commands` before any command had run). A
 * reader thread now looks for the file, reads it, removes it and splits it into command lines; the frame only takes a
 * ready batch (\ref take), oldest first, each one once.
 *
 * What the reader keeps from the frame's old poll:
 *  - a file last written before this run started is a leftover: removed, one warning, never run
 *  - the file is read whole, sharing it with its writer: the test's rename still holds it open with DELETE access for
 *    a moment after its name is visible (#527); a file that cannot be opened yet stays and is read on a later look,
 *    with an info note, and is given up on (removed, one warning) after \ref GiveUpAfter of failed opens
 *  - after the read the file is removed; one that cannot be removed is emptied, so the batch never runs twice
 *  - lines are trimmed of whitespace (a CR included); empty lines and lines starting with '#' are dropped; a file
 *    without command lines is consumed and gives no batch
 *
 * The reader holds its lock only to hand a batch over; the frame never waits for it (a starved CPU may deschedule the
 * reader holding it, as in ChannelWriter): \ref take finds nothing then, and the batch on a later call. Covered by
 * tests/core/CommandInboxTest.cpp.
 */
class CommandInbox {

public:

	//! A line for arx.log, from the reader thread: warning = true for LogWarning, false for LogInfo
	using Note = std::function<void(bool warning, std::string_view message)>;

	//! How long a file that cannot be opened is tried before it is removed (the old poll's 900 frames at 90 fps)
	static constexpr std::chrono::seconds GiveUpAfter = std::chrono::seconds(10);

	/*!
	 * Starts the reader thread
	 * \param file command.txt
	 * \param notBefore a file last written before this is a leftover of an earlier run
	 * \param interval how often the reader looks for the file
	 */
	CommandInbox(std::filesystem::path file, std::filesystem::file_time_type notBefore, Note note = Note(),
	             std::chrono::milliseconds interval = std::chrono::milliseconds(5));

	//! Stops the reader thread (within one look: a read, a remove and the interval)
	~CommandInbox();

	CommandInbox(const CommandInbox &) = delete;
	CommandInbox & operator=(const CommandInbox &) = delete;

	//! The oldest batch read and not taken yet, into \a lines (replaced); false when none is ready. Never touches the
	//! file system, never waits for the reader: when the reader holds its lock, false now and the batch on a later call
	bool take(std::vector<std::string> & lines);

	//! Batches with command lines read so far (taken or not)
	unsigned long long batchesRead() { return m_batchesRead.load(); }

	//! For the host test: hold the lock the reader takes to hand a batch over, as a descheduled reader would
	std::unique_lock<std::mutex> holdLockForTest() { return std::unique_lock<std::mutex>(m_mutex); }

private:

	void run();

	//! One look for the file; a batch with command lines into \a lines
	bool look(std::vector<std::string> & lines);

	void note(bool warning, const std::string & message);

	std::filesystem::path m_file;
	std::filesystem::file_time_type m_notBefore;
	Note m_note;
	std::chrono::milliseconds m_interval;

	std::mutex m_mutex; //!< the batches read and not taken yet
	std::deque<std::vector<std::string>> m_batches;
	std::atomic<unsigned long long> m_batchesRead { 0 };

	std::mutex m_stopMutex; //!< the reader's sleep between looks
	std::condition_variable m_stopped;
	bool m_stop = false;

	// The reader thread's side
	unsigned m_openFailures = 0; //!< failed opens of the file there now
	std::chrono::steady_clock::time_point m_firstOpenFailure;
	std::chrono::steady_clock::time_point m_lastOpenNote;

	std::thread m_thread;

};

#endif // ARX_CORE_COMMANDINBOX_H
