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

#include "platform/ProcessResources.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/Architecture.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace platform {

namespace {

const double BytesPerMiB = 1024.0 * 1024.0;

//! A MiB value with exactly one decimal and always a '.', whatever the locale
void appendMiB(std::string & out, double value) {
	long long tenths = std::llround(value * 10.0);
	if(tenths < 0) {
		tenths = 0;
	}
	char buffer[32];
	int n = std::snprintf(buffer, sizeof(buffer), "%lld.%lld", tenths / 10, tenths % 10);
	if(n > 0) {
		out.append(buffer, size_t(n));
	}
}

void appendField(std::string & out, const char * name, u32 value) {
	char buffer[32];
	int n = std::snprintf(buffer, sizeof(buffer), " %s=%u", name, value);
	if(n > 0) {
		out.append(buffer, size_t(n));
	}
}

void appendMiBField(std::string & out, const char * name, double value) {
	out.push_back(' ');
	out += name;
	out.push_back('=');
	appendMiB(out, value);
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

u32 countProcessThreads() {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if(snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	DWORD pid = GetCurrentProcessId();
	u32 threads = 0;
	THREADENTRY32 entry;
	entry.dwSize = sizeof(entry);
	for(BOOL found = Thread32First(snapshot, &entry); found; found = Thread32Next(snapshot, &entry)) {
		if(entry.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(entry.th32OwnerProcessID)
		   && entry.th32OwnerProcessID == pid) {
			threads++;
		}
		entry.dwSize = sizeof(entry);
	}
	CloseHandle(snapshot);
	return threads;
}

#endif // ARX_PLATFORM_WIN32

} // anonymous namespace

ProcessResources sampleProcessResources() {

	ProcessResources resources;

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	HANDLE process = GetCurrentProcess();

	PROCESS_MEMORY_COUNTERS_EX memory;
	std::memset(&memory, 0, sizeof(memory));
	memory.cb = sizeof(memory);
	if(GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
		resources.privateMB = double(memory.PrivateUsage) / BytesPerMiB;
		resources.workingSetMB = double(memory.WorkingSetSize) / BytesPerMiB;
	}

	PERFORMANCE_INFORMATION perf;
	std::memset(&perf, 0, sizeof(perf));
	perf.cb = sizeof(perf);
	if(GetPerformanceInfo(&perf, sizeof(perf))) {
		resources.commitMB = double(perf.CommitTotal) * double(perf.PageSize) / BytesPerMiB;
	}

	DWORD handles = 0;
	if(GetProcessHandleCount(process, &handles)) {
		resources.handles = u32(handles);
	}

	resources.gdiObjects = u32(GetGuiResources(process, GR_GDIOBJECTS));
	resources.userObjects = u32(GetGuiResources(process, GR_USEROBJECTS));
	resources.threads = countProcessThreads();

	#endif // ARX_PLATFORM_WIN32

	return resources;
}

std::string formatResources(const ResourceSample & sample) {

	std::string out = "R t=";
	char second[16];
	int n = std::snprintf(second, sizeof(second), "%u", sample.second);
	if(n > 0) {
		out.append(second, size_t(n));
	}

	appendMiBField(out, "priv", sample.process.privateMB);
	appendMiBField(out, "ws", sample.process.workingSetMB);
	appendMiBField(out, "commit", sample.process.commitMB);
	appendField(out, "handles", sample.process.handles);
	appendField(out, "gdi", sample.process.gdiObjects);
	appendField(out, "user", sample.process.userObjects);
	appendField(out, "threads", sample.process.threads);
	appendField(out, "tex", sample.textures);
	appendField(out, "buf", sample.buffers);
	appendField(out, "xrsc", sample.swapchains);

	return out;
}

} // namespace platform
