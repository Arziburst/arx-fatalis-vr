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

#include "platform/CrashGate.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#else
#include <cerrno>
#include <time.h>
#endif

namespace platform {

CrashGate::Entry CrashGate::enter(u32 threadId) noexcept {
	u32 expected = 0;
	if(m_reporter.compare_exchange_strong(expected, threadId)) {
		return Entry::First;
	}
	return (expected == threadId) ? Entry::Again : Entry::Other;
}

void CrashGate::park(std::chrono::milliseconds limit) noexcept {
	// A plain sleep of the OS: no heap, no lock - the thread may have crashed holding the heap's
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	Sleep(DWORD(limit.count()));
#else
	timespec left;
	left.tv_sec = time_t(limit.count() / 1000);
	left.tv_nsec = long(limit.count() % 1000) * 1000000l;
	while(nanosleep(&left, &left) != 0 && errno == EINTR) { }
#endif
}

} // namespace platform
