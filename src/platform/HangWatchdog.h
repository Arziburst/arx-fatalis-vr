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

#ifndef ARX_PLATFORM_HANGWATCHDOG_H
#define ARX_PLATFORM_HANGWATCHDOG_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "platform/Platform.h"

/*!
 * \file
 * \brief A stalled thread reported with its stack (#440)
 *
 * The watched thread beats (every frame, every step of the loading screen); a thread of the
 * watchdog checks the beats. When none came for the threshold it suspends the watched thread just
 * long enough to unwind its stack (no heap, no locks while it is suspended), resumes it, and hands
 * the report to its reporter: how long the thread stalled, where it last beat, and the stack as
 * module+offset with the function and source line where the build's .pdb is found. A stall that
 * goes on is reported again every threshold (up to three times, so a stack that moves shows it);
 * the first beat after a reported stall is reported too.
 *
 * The symbols and the list of modules are read when the watchdog starts (and refreshed while the
 * thread beats), never during a report: a thread stuck inside the loader holds the lock that loading
 * DbgHelp or querying a module waits for.
 *
 * Without a stack walker (not Windows x64) the report has no frames. Covered by
 * tests/platform/HangWatchdogTest.cpp; the game's watchdog is started by the test channel.
 */
namespace platform {

class HangWatchdog {

public:

	//! Receives one report (several lines, each ending in '\n') on the watchdog's own thread
	using Reporter = std::function<void(std::string_view report)>;

	//! Reports of one stall at most: the first, and two more if it goes on
	static constexpr int MaxReportsPerStall = 3;

	/*!
	 * \brief Watch the thread that constructs it, from now on
	 *
	 * \param name what the reports call the thread ("main thread")
	 */
	HangWatchdog(std::string name, std::chrono::milliseconds threshold, Reporter reporter);
	~HangWatchdog();

	HangWatchdog(const HangWatchdog &) = delete;
	HangWatchdog & operator=(const HangWatchdog &) = delete;

	/*!
	 * \brief The watched thread is alive
	 *
	 * \param where a string literal naming the place (the report says where the thread last beat)
	 */
	void beat(const char * where) noexcept;

	void setThreshold(std::chrono::milliseconds threshold) noexcept;
	std::chrono::milliseconds threshold() const noexcept;

	//! Stalls reported so far
	u64 stalls() const noexcept { return m_stalls.load(); }

	//! The watchdog has set up its symbols and checks the beats (a stall before that is reported late)
	bool watching() const noexcept { return m_watching.load(); }

private:

	struct Thread;
	class Symbols;

	void run();
	void report(s64 stalledMs, int index);

	std::string m_name;
	Reporter m_reporter;
	std::unique_ptr<Thread> m_watched;
	std::unique_ptr<Symbols> m_symbols; //!< Set up and used by the watchdog's thread only
	std::atomic<s64> m_thresholdMs;
	std::atomic<s64> m_lastBeatNs;
	std::atomic<u64> m_beats;
	std::atomic<const char *> m_where;
	std::atomic<u64> m_stalls;
	std::atomic<bool> m_watching;

	std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_stop = false;
	std::thread m_thread;

};

/*!
 * \brief Sleep on this thread in a frame of its own
 *
 * The stall the hang checks force (the test channel's \c stall command, the host test): never
 * inlined, so the report's stack names it.
 */
void stallThisThread(std::chrono::milliseconds duration);

} // namespace platform

#endif // ARX_PLATFORM_HANGWATCHDOG_H
