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

#include "platform/ExitReport.h"

#include <algorithm>
#include <cstring>

namespace platform {

namespace {

constexpr std::string_view ExitReportCut = "...";
constexpr std::string_view ExitLinePrefix = "[E] ExitTrace:0  ";

} // anonymous namespace

void ExitReport::append(std::string_view text) {

	if(m_cut) {
		return;
	}

	// Room for the cut mark stays free until the text is known to fit
	const size_t room = Capacity - m_size;
	if(text.size() + ExitReportCut.size() <= room) {
		std::memcpy(m_text + m_size, text.data(), text.size());
		m_size += text.size();
		return;
	}

	const size_t keep = room > ExitReportCut.size() ? room - ExitReportCut.size() : 0;
	std::memcpy(m_text + m_size, text.data(), keep);
	m_size += keep;
	std::memcpy(m_text + m_size, ExitReportCut.data(), std::min(ExitReportCut.size(), Capacity - m_size));
	m_size += std::min(ExitReportCut.size(), Capacity - m_size);
	m_cut = true;
}

void ExitReport::appendNumber(s64 value) {

	char digits[24];
	size_t count = 0;
	// Through the unsigned magnitude: -INT64_MIN does not fit an s64
	u64 magnitude = value < 0 ? u64(0) - u64(value) : u64(value);
	do {
		digits[sizeof(digits) - 1 - count++] = char('0' + magnitude % 10);
		magnitude /= 10;
	} while(magnitude != 0);
	if(value < 0) {
		digits[sizeof(digits) - 1 - count++] = '-';
	}

	append(std::string_view(digits + sizeof(digits) - count, count));
}

void ExitReport::appendHex(u64 value) {

	char digits[18];
	size_t count = 0;
	do {
		digits[sizeof(digits) - 1 - count++] = "0123456789abcdef"[value & 0xf];
		value >>= 4;
	} while(value != 0);
	digits[sizeof(digits) - 1 - count++] = 'x';
	digits[sizeof(digits) - 1 - count++] = '0';

	append(std::string_view(digits + sizeof(digits) - count, count));
}

void describeExit(ExitReport & out, const ExitFacts & facts) {

	out.append(ExitLinePrefix);
	out.append("The process is ending outside the game's own shutdown (#146): ");
	out.append(facts.via);
	if(facts.exitCodeKnown) {
		out.append(", exit code ");
		out.appendNumber(facts.exitCode);
		out.append(" (");
		out.appendHex(u64(u32(facts.exitCode)));
		out.append(")");
	} else {
		out.append(", exit code not known yet");
	}
	out.append("\n");

	out.append(ExitLinePrefix);
	out.append("Last cause noted before it: ");
	if(facts.cause.empty()) {
		out.append("none (no quit request, no VR session end)");
	} else {
		out.append(facts.cause);
		if(facts.causeAgoMs >= 0) {
			out.append(", ");
			out.appendNumber(facts.causeAgoMs);
			out.append(" ms earlier");
		}
	}
	out.append("\n");

	out.append(ExitLinePrefix);
	out.append("Called from (innermost first):");
	if(facts.frameCount == 0) {
		out.append(" no stack");
	}
	for(size_t i = 0; i < facts.frameCount; i++) {
		out.append(i == 0 ? " " : " < ");
		const ExitFrame & frame = facts.frames[i];
		out.append(frame.module.empty() ? std::string_view("?") : frame.module);
		out.append("+");
		out.appendHex(frame.offset);
	}
	out.append("\n");
}

} // namespace platform
