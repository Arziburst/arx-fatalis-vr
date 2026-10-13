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

#ifndef ARX_PLATFORM_EXITREPORT_H
#define ARX_PLATFORM_EXITREPORT_H

#include <cstddef>
#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The lines the exit hook (platform/ExitTrace.h, #146) writes when the process ends by surprise
 *
 * Pure text: no heap, no locks, no clock - the hook runs while the process is being torn down,
 * where other threads may have died holding a lock. Covered by tests/platform/ExitReportTest.cpp.
 */
namespace platform {

//! One return address of the stack that ended the process: a module and the offset inside it
struct ExitFrame {
	std::string_view module; //!< file name of the module, empty when the address is in none
	u64 offset = 0;
};

//! What the hook knows about the end of the process
struct ExitFacts {
	std::string_view via;      //!< how the process ends: exit(), ExitProcess, ...
	bool exitCodeKnown = false;
	s64 exitCode = 0;
	std::string_view cause;    //!< the last cause noted before (a quit request, the VR session ending), may be empty
	s64 causeAgoMs = -1;       //!< how long before the end the cause was noted, < 0 = unknown
	const ExitFrame * frames = nullptr; //!< innermost first
	size_t frameCount = 0;
};

//! A fixed-size text buffer: appending past its end drops the rest and marks the cut with "..."
class ExitReport {

public:

	static constexpr size_t Capacity = 4096;

	void append(std::string_view text);
	void appendNumber(s64 value);
	void appendHex(u64 value);

	std::string_view text() const { return std::string_view(m_text, m_size); }

private:

	char m_text[Capacity];
	size_t m_size = 0;
	bool m_cut = false;

};

/*!
 * \brief The report of an unexpected end, in arx.log's own line format (\c [E] lines)
 *
 * Three lines: how the process ends and its exit code, the last cause noted before it, and the
 * stack that called the end as module+offset (resolved later with the build's .pdb).
 */
void describeExit(ExitReport & out, const ExitFacts & facts);

} // namespace platform

#endif // ARX_PLATFORM_EXITREPORT_H
