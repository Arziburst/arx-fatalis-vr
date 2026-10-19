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

#include "platform/ExitTrace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

#include "io/log/Logger.h"
#include "platform/DumpHelper.h"
#include "platform/ExitReport.h"
#include "platform/Platform.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#include "platform/WindowsUtils.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace platform {

namespace {

// Everything the hooks read is fixed-size and set up front: they run while the process is torn
// down, where another thread may have died holding the heap's or the logger's lock

std::atomic<bool> g_exitTraceInstalled(false);
std::atomic<bool> g_cleanExit(false);
std::atomic<bool> g_exitReported(false);
void (*g_exitFlushLog)() = nullptr;

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
wchar_t g_exitLogPath[1024] = {};
#else
char g_exitLogPath[4096] = {};
#endif

/*
 * The last cause noted: two slots, the newest one published by its index. Two threads noting at
 * the same moment may garble one text - a rare race that costs a line, never a crash.
 */
struct NotedExitCause {
	char text[256] = {};
	std::atomic<s64> ns{0};
};
NotedExitCause g_exitCauses[2];
std::atomic<int> g_exitCause(-1);

s64 exitClockNs() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr size_t MaxExitFrames = 24;

struct ExitStack {
	ExitFrame frames[MaxExitFrames];
	char names[MaxExitFrames][96];
	size_t count = 0;
};

void captureExitStack(ExitStack & stack) {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	void * addresses[MaxExitFrames];
	// Frame 0 is this function, 1 the report; the rest is the way the end was called
	USHORT count = RtlCaptureStackBackTrace(2, MaxExitFrames, addresses, nullptr);
	for(USHORT i = 0; i < count; i++) {
		ExitFrame & frame = stack.frames[stack.count];
		frame.offset = u64(reinterpret_cast<uintptr_t>(addresses[i]));
		HMODULE module = nullptr;
		DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
		wchar_t path[MAX_PATH];
		if(GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(addresses[i]), &module)
		   && GetModuleFileNameW(module, path, MAX_PATH) != 0) {
			// Only the file name: a full build path does not fit the name buffer
			const wchar_t * file = path;
			for(const wchar_t * c = path; *c != L'\0'; c++) {
				if(*c == L'\\' || *c == L'/') {
					file = c + 1;
				}
			}
			char * name = stack.names[stack.count];
			int size = WideCharToMultiByte(CP_UTF8, 0, file, -1, name, int(sizeof(stack.names[0])), nullptr, nullptr);
			if(size > 0) {
				frame.module = std::string_view(name, size_t(size - 1));
			}
			frame.offset -= u64(reinterpret_cast<uintptr_t>(module));
		}
		stack.count++;
	}
	#else
	ARX_UNUSED(stack);
	#endif

}

void writeExitReport(std::string_view text) {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	DWORD written = 0;
	if(g_exitLogPath[0] != L'\0') {
		HANDLE file = CreateFileW(g_exitLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(file != INVALID_HANDLE_VALUE) {
			WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr);
			CloseHandle(file);
		}
	}
	HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
	if(error != nullptr && error != INVALID_HANDLE_VALUE) {
		WriteFile(error, text.data(), DWORD(text.size()), &written, nullptr);
	}
	#else
	if(g_exitLogPath[0] != '\0') {
		int file = open(g_exitLogPath, O_WRONLY | O_APPEND | O_CREAT, 0644);
		if(file >= 0) {
			ARX_UNUSED(write(file, text.data(), text.size()));
			close(file);
		}
	}
	ARX_UNUSED(write(STDERR_FILENO, text.data(), text.size()));
	#endif

}

void reportExit(std::string_view via, bool exitCodeKnown, s64 exitCode) {

	if(!g_exitTraceInstalled.load() || g_cleanExit.load()) {
		return;
	}

	if(g_exitReported.exchange(true)) {
		// exit() was reported by the atexit handler; its ExitProcess adds the code it ends with
		if(exitCodeKnown) {
			ExitReport line;
			line.append("[E] ExitTrace:0  The process ended with exit code ");
			line.appendNumber(exitCode);
			line.append("\n");
			writeExitReport(line.text());
		}
		return;
	}

	// The lines the game wrote just before the end go first. Logger::quickShutdown may wait up to ~200 ms for
	// the file logger's mutex when a dead or busy thread holds it - only on an abnormal end, then it gives up
	if(g_exitFlushLog) {
		g_exitFlushLog();
	}

	ExitFacts facts;
	facts.via = via;
	facts.exitCodeKnown = exitCodeKnown;
	facts.exitCode = exitCode;
	int slot = g_exitCause.load(std::memory_order_acquire);
	if(slot >= 0) {
		const NotedExitCause & cause = g_exitCauses[slot];
		facts.cause = std::string_view(cause.text, strnlen(cause.text, sizeof(cause.text)));
		facts.causeAgoMs = (exitClockNs() - cause.ns.load()) / 1000000;
	}
	ExitStack stack;
	captureExitStack(stack);
	facts.frames = stack.frames;
	facts.frameCount = stack.count;

	ExitReport text;
	describeExit(text, facts);
	writeExitReport(text.text());

}

void onCrtExit() {
	reportExit("exit() was called before the game's shutdown finished", false, 0);
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32 && ARX_COMPILER_MSVC

// The ExitGuard (#545): every thread, as it is born, is logged with its start routine; a thread that
// starts straight at ExitProcess / ExitThread / TerminateProcess / RtlExitUserProcess is an exit from
// outside the game's own shutdown (the #544 shape: a foreign CreateThread / CreateRemoteThread), so we ask
// the dump helper for a report of every thread before the exit proceeds - record only, never refuse the kill.
namespace {

void * threadStartAddress() {
	using NtQueryInformationThreadFn = LONG (NTAPI *)(HANDLE, int, PVOID, ULONG, PULONG);
	static NtQueryInformationThreadFn query = reinterpret_cast<NtQueryInformationThreadFn>(
		reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
	if(!query) {
		return nullptr;
	}
	void * start = nullptr;
	// ThreadQuerySetWin32StartAddress = 9
	query(GetCurrentThread(), 9, &start, sizeof(start), nullptr);
	return start;
}

struct ExitRoutines {
	void * exitProcess = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess"));
	void * exitThread = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitThread"));
	void * terminateProcess = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TerminateProcess"));
	void * rtlExit = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlExitUserProcess"));
};

//! "KERNEL32!ExitProcess" for an exit routine, else "<module>+0x<offset>"; \a isExit set for an exit routine
std::string describeStart(void * address, bool & isExit) {
	static const ExitRoutines routines;
	isExit = false;
	if(address == routines.exitProcess) { isExit = true; return "KERNEL32!ExitProcess"; }
	if(address == routines.exitThread) { isExit = true; return "KERNEL32!ExitThread"; }
	if(address == routines.terminateProcess) { isExit = true; return "KERNEL32!TerminateProcess"; }
	if(address == routines.rtlExit) { isExit = true; return "ntdll!RtlExitUserProcess"; }
	HMODULE module = nullptr;
	DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if(GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address), &module) && module) {
		wchar_t path[MAX_PATH];
		if(GetModuleFileNameW(module, path, MAX_PATH) != 0) {
			const wchar_t * file = path;
			for(const wchar_t * c = path; *c != L'\0'; c++) {
				if(*c == L'\\' || *c == L'/') {
					file = c + 1;
				}
			}
			char nameBuf[MAX_PATH];
			int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nameBuf, int(sizeof(nameBuf)), nullptr, nullptr);
			std::string name = (n > 1) ? std::string(nameBuf, size_t(n - 1)) : std::string("?");
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "+0x%llx",
			              static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)
			                                              - reinterpret_cast<uintptr_t>(module)));
			return name + buffer;
		}
	}
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
	return buffer;
}

void exitGuardOnThreadStart() {
	void * start = threadStartAddress();
	bool isExit = false;
	std::string label = describeStart(start, isExit);
	flightRecord(RecordKind::Thread, "start " + label);
	if(isExit) {
		std::string killer = std::to_string(GetCurrentThreadId()) + " start " + label;
		// A bounded wait for the dump, then the thread returns and the exit proceeds (never refused)
		requestReport(DumpKind::ExitOutside, true, GetCurrentThreadId(), 0, killer, label,
		              std::string_view(), std::string_view(), 15000);
	}
}

} // anonymous namespace

void NTAPI exitTraceTlsCallback(PVOID /* module */, DWORD reason, PVOID /* reserved */) {

	// Every thread birth: log its start routine, and catch an exit from outside before it runs (#545)
	if(reason == DLL_THREAD_ATTACH) {
		exitGuardOnThreadStart();
		return;
	}

	// The executable is never unloaded: its process detach is the end of the process. Unlike DllMain,
	// a TLS callback gets no "terminating" flag - the loader passes a null reserved here
	if(reason != DLL_PROCESS_DETACH) {
		return;
	}

	// The code is set once the process is fully gone; read it in case this runtime has it already
	DWORD code = STILL_ACTIVE;
	bool known = GetExitCodeProcess(GetCurrentProcess(), &code) && code != STILL_ACTIVE;
	reportExit("ExitProcess (or _exit / abort) was called inside the process, not exit() of the game's"
	           " C runtime - the stack names the module that called it", known, s64(s32(code)));

}

#endif

} // anonymous namespace

void initializeExitTrace(const fs::path & logFile, void (*flushLog)()) {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	WinPath path(logFile);
	if(path.size() < std::size(g_exitLogPath)) {
		std::copy(path.c_str(), path.c_str() + path.size() + 1, g_exitLogPath);
	}
	#else
	if(logFile.string().size() < sizeof(g_exitLogPath)) {
		std::memcpy(g_exitLogPath, logFile.string().c_str(), logFile.string().size() + 1);
	}
	#endif

	g_exitFlushLog = flushLog;

	if(!g_exitTraceInstalled.exchange(true)) {
		std::atexit(onCrtExit);
	}

}

void noteExitCause(std::string_view cause) {

	const int slot = g_exitCause.load(std::memory_order_relaxed) == 0 ? 1 : 0;
	NotedExitCause & noted = g_exitCauses[slot];
	const size_t size = std::min(cause.size(), sizeof(noted.text) - 1);
	std::memcpy(noted.text, cause.data(), size);
	noted.text[size] = '\0';
	noted.ns.store(exitClockNs());
	g_exitCause.store(slot, std::memory_order_release);

}

void logQuitRequest(std::string_view who) {
	LogInfo << "Quit requested: " << who;
	noteExitCause(std::string("quit requested: ") + std::string(who));
}

void markCleanExit() {
	g_cleanExit.store(true);
}

} // namespace platform

#if ARX_PLATFORM == ARX_PLATFORM_WIN32 && ARX_COMPILER_MSVC
// The loader calls the executable's TLS callbacks on process detach as well: the one place that
// sees every ExitProcess, whichever module calls it
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:arx_exitTraceTlsCallback")
#pragma const_seg(".CRT$XLY")
extern "C" const PIMAGE_TLS_CALLBACK arx_exitTraceTlsCallback = platform::exitTraceTlsCallback;
#pragma const_seg()
#endif
