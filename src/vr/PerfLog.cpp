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

#include "vr/PerfLog.h"

#include <array>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>

#include "core/Version.h"
#include "io/fs/FilePath.h"
#include "io/fs/FileStream.h"
#include "io/fs/SystemPaths.h"
#include "io/log/FileLogger.h"
#include "io/log/Logger.h"
#include "platform/ProgramOptions.h"
#include "platform/Time.h"
#include "vr/PerfWriter.h"

namespace vr::perf {

namespace {

//! Runs whose performance logs are kept, as for arx.log: arx-perf.log plus arx-perf.1.log .. arx-perf.9.log
const size_t LogHistory = 10;

//! Nested phases deeper than this are counted in the outer one
const size_t MaxDepth = 16;

bool g_disabled = false;

void disable() {
	g_disabled = true;
}

//! Scopes time only on the thread that opened the log (the main thread) while it is open
thread_local bool t_timing = false;

struct State {

	PlatformInstant origin = PlatformInstant(0);
	bool haveFrame = false;
	PlatformInstant frameStart = PlatformInstant(0);
	PlatformInstant phaseStart = PlatformInstant(0);
	PerfPhase current = PerfPhase::Other;
	std::array<PerfPhase, MaxDepth> stack = { };
	size_t depth = 0;

	PerfFrame frame;
	//! The previous frame, ended by frameStart() and judged by frameBegun(): the display slots it made
	//! the runtime skip are known only after this frame's xrWaitFrame
	PerfFrame closing;
	bool haveClosing = false;
	PerfMonitor monitor;
	s64 selfUs = 0;     //!< the log's own work in frameStart() and frameBegun() this second
	u32 selfFrames = 0;

	std::unique_ptr<fs::ofstream> file;
	std::unique_ptr<PerfWriter> writer; //!< destroyed before the file it writes to

};

State g;

//! The time since the last phase switch goes to the current phase
void charge(PlatformInstant now) {
	g.frame.phases[size_t(g.current)] += toUs(now - g.phaseStart);
	g.phaseStart = now;
}

void writeSecond() {
	PerfSecond second = g.monitor.second();
	second.selfUs = g.selfFrames ? double(g.selfUs) / double(g.selfFrames) : 0.0;
	g.writer->push(formatSecond(second));
	g.selfUs = 0;
	g.selfFrames = 0;
}

//! Judge the previous frame: its second, its hitch
void commitClosing() {
	if(!g.haveClosing) {
		return;
	}
	g.haveClosing = false;
	PerfHitch hitch;
	if(g.monitor.add(g.closing, &hitch)) {
		writeSecond();
	}
	if(hitch.detected) {
		g.writer->push(formatHitch(hitch));
	}
}

std::string localTime() {
	std::time_t now = std::time(nullptr);
	std::tm * tm = std::localtime(&now);
	char buffer[32];
	if(!tm || !std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm)) {
		return "?";
	}
	return buffer;
}

} // anonymous namespace

ARX_PROGRAM_OPTION("no-perf-log", nullptr,
                   "Do not write arx-perf.log, the per-second frame time and hitch log next to arx.log",
                   &disable)

void initialize() {

	if(g_disabled) {
		LogInfo << "[perf] performance log off (--no-perf-log)";
		return;
	}

	fs::path path = fs::getUserDir() / "arx-perf.log";
	std::string rotation = logger::rotateLogs(path, LogHistory);
	std::ios_base::openmode mode = std::ios_base::out | (rotation.empty() ? std::ios_base::trunc : std::ios_base::app);
	g.file = std::make_unique<fs::ofstream>(path, mode);
	if(!g.file->is_open()) {
		LogWarning << "[perf] could not open the performance log " << path;
		g.file.reset();
		return;
	}
	if(!rotation.empty()) {
		LogWarning << "[perf] " << rotation << " - appending to " << path;
	}

	fs::ofstream * file = g.file.get();
	g.writer = std::make_unique<PerfWriter>([file](std::string_view batch) {
		file->write(batch.data(), std::streamsize(batch.size()));
		file->flush();
	});

	g.writer->push("# arx-perf.log (#327): " + arx_name + " " + arx_version + ", started " + localTime());
	g.writer->push("# S = one second: frame times in ms, the display rate, missed display slots, hitches, then the"
	               " mean ms per frame of each phase (exclusive, they add up to the mean); H = a hitch: a frame over"
	               " 1.5 expected frame times, with its phases; M = a mark. Summary: scripts/dev/perf-summary.ps1");

	g.origin = platform::getTime();
	t_timing = true;

	LogInfo << "[perf] performance log: " << path << " (a line per second and per hitch; --no-perf-log turns it off)";
}

void shutdown() {

	if(!g.writer) {
		return;
	}

	// The frame in progress never ended; the one before it still waits for its verdict
	t_timing = false;
	commitClosing();
	if(g.monitor.flush()) {
		writeSecond();
	}
	g.writer->push("# end: " + std::to_string(g.monitor.frames()) + " frames");
	g.writer->flush();
	size_t dropped = g.writer->dropped();
	g.writer.reset();
	if(dropped) {
		LogWarning << "[perf] " << dropped << " performance log lines dropped: the disk fell behind";
	}
	g.file.reset();
	g = State();
}

void frameStart() {

	if(!g.writer) {
		return;
	}

	PlatformInstant now = platform::getTime();

	// A frame without frameBegun() (cannot happen in doFrame) is judged without its missed slots
	commitClosing();

	if(g.haveFrame) {
		charge(now);
		g.closing = g.frame;
		g.closing.start = toUs(g.frameStart - g.origin);
		g.closing.duration = toUs(now - g.frameStart);
		g.haveClosing = true;
	}

	g.frame = PerfFrame();
	g.haveFrame = true;
	g.frameStart = now;
	g.phaseStart = now;
	g.current = PerfPhase::Other;
	g.depth = 0;

	// What this took is the log's own cost, reported per frame on the second's line (in the new
	// frame it counts as "other")
	g.selfUs += toUs(platform::getTime() - now);
	g.selfFrames++;
}

void frameBegun() {

	if(!g.writer || !g.haveClosing) {
		return;
	}

	PlatformInstant now = platform::getTime();
	commitClosing();
	g.selfUs += toUs(platform::getTime() - now);
}

void setDisplay(s64 periodUs, u32 divisor, bool vr) {
	g.frame.period = periodUs;
	g.frame.divisor = divisor;
	g.frame.vr = vr;
}

void addMissed(u32 slots) {
	// Before frameBegun() the slots were skipped for the previous frame: it came late
	(g.haveClosing ? g.closing : g.frame).missed += slots;
}

void countUpload() {
	if(t_timing) {
		g.frame.uploads++;
	}
}

void mark(std::string_view text) {
	if(!g.writer) {
		return;
	}
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "M t=%.3f ", double(toUs(platform::getTime() - g.origin)) / 1e6);
	g.writer->push(buffer + std::string(text));
}

Scope::Scope(PerfPhase phase) noexcept
	: m_on(t_timing && g.haveFrame && g.depth < MaxDepth)
{
	if(m_on) {
		charge(platform::getTime());
		g.stack[g.depth++] = g.current;
		g.current = phase;
	}
}

Scope::~Scope() {
	// A frame that started inside this scope (cannot happen in doFrame) left nothing to pop
	if(m_on && t_timing && g.depth > 0) {
		charge(platform::getTime());
		g.current = g.stack[--g.depth];
	}
}

} // namespace vr::perf
