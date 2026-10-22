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

#ifndef ARX_PLATFORM_CRASHGATE_H
#define ARX_PLATFORM_CRASHGATE_H

#include <atomic>
#include <chrono>

#include "platform/Platform.h"

namespace platform {

/*!
 * \brief Who reports a crash, and how long a second crashing thread waits (#545, #554)
 *
 * The first crashing thread owns the report. The same thread crashing again (a fault inside the handler) ends the
 * process at once. Another thread parks for a bounded time, long enough for the first report to finish, and then ends
 * the process: it parked for good before (Sleep(INFINITE)), which hung the game when that thread held a lock the
 * first handler needs - the heap lock while the first thread symbolises. Covered by tests/platform/CrashGateTest.cpp.
 */
class CrashGate {

public:

	enum class Entry {
		First, //!< this thread reports the crash
		Again, //!< the reporting thread crashed again (inside the handler): end the process now
		Other, //!< another thread reports: park, then end the process
	};

	//! How long another crashing thread parks: longer than the first report takes (the dump helper's request waits up
	//! to 15 s), short enough that a deadlocked report does not keep a dead game around
	static constexpr std::chrono::milliseconds ParkLimit = std::chrono::seconds(30);

	constexpr CrashGate() noexcept = default;

	//! Lock-free and allocation-free: safe in a crash handler. \a threadId is never 0
	Entry enter(u32 threadId) noexcept;

	//! The thread that reports; 0 before any crash
	u32 reporter() const noexcept { return m_reporter.load(); }

	//! Parks the calling thread for \a limit, then returns (no heap, no lock taken)
	static void park(std::chrono::milliseconds limit) noexcept;

private:

	std::atomic<u32> m_reporter { 0 };

};

} // namespace platform

#endif // ARX_PLATFORM_CRASHGATE_H
