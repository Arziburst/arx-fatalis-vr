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

#ifndef ARX_CORE_CHANNELWRITER_H
#define ARX_CORE_CHANNELWRITER_H

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

/*!
 * \brief Writes the test channel's status.json and ack.txt off the frame path (#553)
 *
 * The frame thread only hands the finished status text and ack line over; the writer thread writes status.json.tmp,
 * renames it over status.json - waiting, if it must, for a reader that holds the file (core::replaceFile) - and then
 * writes ack.txt. A file system that makes that rename wait (a scanner's oplock on status.json, CI run 39359033513:
 * one 159.6 ms channel phase, 11 display slots missed) delays the files, never a frame.
 *
 * The order a test depends on holds: an ack is written only after a status handed over no later than the ack itself
 * landed (#23), so a test that reads status.json right after the ack sees its batch. Only the newest status is kept:
 * a status handed over while an older one still waits for the file replaces it, as the newer one shows everything
 * the older one did.
 *
 * The writer thread holds its lock only to take the texts over. A status the next one makes obsolete anyway (the
 * 100 ms timer's) does not even wait for that (a starved CPU may deschedule the writer holding it, as in PerfWriter):
 * it is kept on the frame thread's side and handed over by the next call, \ref poll included. A status a test waits
 * for, and every ack, waits for the lock - a level load or a stall right after must not keep them back. One
 * producer: everything but the writer thread's own work is called from one thread.
 */
class ChannelWriter {

public:

	struct Files {
		std::filesystem::path status; //!< status.json
		std::filesystem::path tmp;    //!< status.json.tmp: written whole, then renamed over \c status
		std::filesystem::path ack;    //!< ack.txt
	};

	//! Called on the writer thread after each status that landed (the test channel flushes arx.log there)
	using Landed = std::function<void()>;
	//! Called on the writer thread once per kind of trouble (a file that could not be written, a rename that waited
	//! its whole patience)
	using Warn = std::function<void(std::string_view)>;

	explicit ChannelWriter(Files files, Landed landed = Landed(), Warn warn = Warn(),
	                       std::chrono::milliseconds patience = std::chrono::milliseconds(2000));

	//! Writes what is still to be written (one more try of a status a reader holds up) and stops the writer thread
	~ChannelWriter();

	ChannelWriter(const ChannelWriter &) = delete;
	ChannelWriter & operator=(const ChannelWriter &) = delete;

	/*!
	 * The new content of status.json; replaces a status not written yet. Never waits for a file
	 * \param mayKeep a status the next one makes obsolete: kept for the next call while the writer's lock is taken
	 */
	void status(std::string text, bool mayKeep = false);

	//! The new content of ack.txt, written once the status handed over before it landed. Never waits for a file
	void ack(std::string text);

	//! Hands over what an earlier call had to keep (the writer thread's lock was taken); once a frame
	void poll();

	//! Wait until everything handed over so far is written (or given up on, at stop)
	void flush();

	//! Statuses that landed so far
	unsigned long long statusesWritten();

	//! For the host test: hold the lock the writer thread takes, as if it were descheduled holding it
	std::unique_lock<std::mutex> holdLockForTest() { return std::unique_lock<std::mutex>(m_mutex); }

private:

	void run();

	//! The producer's kept status and ack go to the writer thread (under the lock)
	void handOver();

	//! Hand over the kept items when the lock is free; never waits for it
	void tryHandOver();

	void warnOnce(bool & warned, std::string_view message);

	Files m_files;
	Landed m_landed;
	Warn m_warn;
	std::chrono::milliseconds m_patience;

	std::mutex m_mutex;
	std::condition_variable m_wake;    //!< the writer thread: something to write, or stop
	std::condition_variable m_written; //!< flush(): the writer thread caught up
	std::string m_status;
	bool m_hasStatus = false;
	std::string m_ack;
	bool m_hasAck = false;
	unsigned long long m_queued = 0;   //!< items handed over so far
	unsigned long long m_done = 0;     //!< items written (or replaced by newer ones) so far
	unsigned long long m_landedCount = 0;
	bool m_stop = false;

	// The producer's side, never touched by the writer thread
	std::string m_heldStatus;
	bool m_holdsStatus = false;
	std::string m_heldAck;
	bool m_holdsAck = false;

	// The writer thread's side
	bool m_warnedTmp = false;
	bool m_warnedHeld = false;
	bool m_warnedAck = false;

	std::thread m_thread;

};

#endif // ARX_CORE_CHANNELWRITER_H
