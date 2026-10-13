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

#include "platform/HangWatchdog.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#include "platform/Architecture.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#endif

namespace platform {

namespace {

s64 watchdogClockNs() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr size_t MaxStackFrames = 48;

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

std::string toUtf8(const wchar_t * text) {
	int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if(size <= 1) {
		return std::string();
	}
	std::string result(size_t(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
	result.resize(size_t(size - 1));
	return result;
}

const wchar_t * fileNameOf(const wchar_t * path) {
	const wchar_t * file = path;
	for(const wchar_t * c = path; *c != L'\0'; c++) {
		if(*c == L'\\' || *c == L'/') {
			file = c + 1;
		}
	}
	return file;
}

const char * fileNameOf(const char * path) {
	const char * file = path;
	for(const char * c = path; *c != '\0'; c++) {
		if(*c == '\\' || *c == '/') {
			file = c + 1;
		}
	}
	return file;
}

#endif // ARX_PLATFORM_WIN32

} // anonymous namespace

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

/*
 * DbgHelp, the symbols and the list of loaded modules, all set up while the watched thread still
 * beats - never during a report: a thread stuck inside the loader holds the loader lock, which
 * LoadLibrary, SymInitialize and the module queries wait for (the first version of this watchdog
 * did that at its first report and never wrote it, #440). The game links DbgHelp only with the
 * crash handler and the host test not at all, so it is loaded here. Without it (or without the
 * .pdb) a frame keeps its module+offset, which the build's .pdb resolves later.
 */
class HangWatchdog::Symbols {

	using SymSetOptionsFn = DWORD (WINAPI *)(DWORD);
	using SymInitializeWFn = BOOL (WINAPI *)(HANDLE, PCWSTR, BOOL);
	using SymRefreshModuleListFn = BOOL (WINAPI *)(HANDLE);
	using SymCleanupFn = BOOL (WINAPI *)(HANDLE);
	using SymFromAddrFn = BOOL (WINAPI *)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
	using SymGetLineFromAddr64Fn = BOOL (WINAPI *)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);

	struct Module {
		DWORD64 base = 0;
		DWORD64 size = 0;
		std::string name;
	};

	HMODULE m_dbghelp = nullptr;
	HANDLE m_process = GetCurrentProcess();
	bool m_ready = false;
	SymRefreshModuleListFn m_refresh = nullptr;
	SymCleanupFn m_cleanup = nullptr;
	SymFromAddrFn m_fromAddr = nullptr;
	SymGetLineFromAddr64Fn m_lineFromAddr = nullptr;
	std::vector<Module> m_modules;

	void listModules() {

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if(snapshot == INVALID_HANDLE_VALUE) {
			return;
		}

		std::vector<Module> modules;
		MODULEENTRY32W entry;
		entry.dwSize = sizeof(entry);
		for(BOOL found = Module32FirstW(snapshot, &entry); found; found = Module32NextW(snapshot, &entry)) {
			Module & module = modules.emplace_back();
			module.base = DWORD64(reinterpret_cast<uintptr_t>(entry.modBaseAddr));
			module.size = entry.modBaseSize;
			module.name = toUtf8(entry.szModule);
		}
		CloseHandle(snapshot);

		m_modules = std::move(modules);
	}

public:

	Symbols() {

		listModules();

		m_dbghelp = LoadLibraryW(L"dbghelp.dll");
		if(!m_dbghelp) {
			return;
		}

		auto setOptions = reinterpret_cast<SymSetOptionsFn>(GetProcAddress(m_dbghelp, "SymSetOptions"));
		auto initialize = reinterpret_cast<SymInitializeWFn>(GetProcAddress(m_dbghelp, "SymInitializeW"));
		m_refresh = reinterpret_cast<SymRefreshModuleListFn>(GetProcAddress(m_dbghelp, "SymRefreshModuleList"));
		m_cleanup = reinterpret_cast<SymCleanupFn>(GetProcAddress(m_dbghelp, "SymCleanup"));
		m_fromAddr = reinterpret_cast<SymFromAddrFn>(GetProcAddress(m_dbghelp, "SymFromAddr"));
		m_lineFromAddr = reinterpret_cast<SymGetLineFromAddr64Fn>(GetProcAddress(m_dbghelp, "SymGetLineFromAddr64"));
		if(!setOptions || !initialize || !m_refresh || !m_cleanup || !m_fromAddr || !m_lineFromAddr) {
			return;
		}

		// No deferred loads: every module's symbols are read now, a report only looks them up
		setOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);

		// The executable's folder holds its .pdb; the path recorded in the executable is tried as well
		wchar_t path[MAX_PATH] = {};
		DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
		if(length != 0 && length < MAX_PATH) {
			const_cast<wchar_t *>(fileNameOf(path))[0] = L'\0';
		} else {
			path[0] = L'\0';
		}
		m_ready = initialize(m_process, path[0] != L'\0' ? path : nullptr, TRUE) == TRUE;
	}

	~Symbols() {
		if(m_ready) {
			m_cleanup(m_process);
		}
		if(m_dbghelp) {
			FreeLibrary(m_dbghelp);
		}
	}

	Symbols(const Symbols &) = delete;
	Symbols & operator=(const Symbols &) = delete;

	//! Modules loaded since: only while the watched thread beats
	void refresh() {
		listModules();
		if(m_ready) {
			m_refresh(m_process);
		}
	}

	//! "arx.exe+0x1a2b3c", or the bare address outside every module listed
	std::string moduleOffset(DWORD64 address) const {
		std::ostringstream oss;
		for(const Module & module : m_modules) {
			if(address >= module.base && address < module.base + module.size) {
				oss << module.name << "+0x" << std::hex << (address - module.base);
				return oss.str();
			}
		}
		oss << "0x" << std::hex << address;
		return oss.str();
	}

	//! " function (file.cpp:123)", " function" or nothing
	std::string describe(DWORD64 address) const {

		if(!m_ready) {
			return std::string();
		}

		std::string result;

		alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
		SYMBOL_INFO * symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 256 - 1;
		DWORD64 displacement = 0;
		if(m_fromAddr(m_process, address, &displacement, symbol) == TRUE) {
			result += ' ';
			result.append(symbol->Name, std::min<size_t>(symbol->NameLen, 255));
		}

		IMAGEHLP_LINE64 line;
		std::memset(&line, 0, sizeof(line));
		line.SizeOfStruct = sizeof(line);
		DWORD lineDisplacement = 0;
		if(m_lineFromAddr(m_process, address, &lineDisplacement, &line) == TRUE && line.FileName) {
			result += " (";
			result += fileNameOf(line.FileName);
			result += ':';
			result += std::to_string(line.LineNumber);
			result += ')';
		}

		return result;
	}

};

#else

class HangWatchdog::Symbols { };

#endif // ARX_PLATFORM_WIN32

struct HangWatchdog::Thread {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	HANDLE handle = nullptr;

	Thread() {
		handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
		                    GetCurrentThreadId());
	}

	~Thread() {
		if(handle) {
			CloseHandle(handle);
		}
	}

	#endif

	/*
	 * The return addresses of the watched thread, innermost first. While the thread is suspended
	 * this touches no heap and takes no lock of ours: the thread may hold the heap's lock.
	 */
	size_t captureStack(u64 (&frames)[MaxStackFrames]) {

		size_t count = 0;

		#if ARX_PLATFORM == ARX_PLATFORM_WIN32 && ARX_ARCH == ARX_ARCH_X86_64

		if(!handle || SuspendThread(handle) == DWORD(-1)) {
			return 0;
		}

		CONTEXT context;
		std::memset(&context, 0, sizeof(context));
		context.ContextFlags = CONTEXT_FULL;
		if(GetThreadContext(handle, &context)) {
			while(count < MaxStackFrames && context.Rip != 0) {
				frames[count++] = context.Rip;
				DWORD64 imageBase = 0;
				PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
				if(function) {
					PVOID handlerData = nullptr;
					DWORD64 establisherFrame = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
					                 &handlerData, &establisherFrame, nullptr);
				} else if(count == 1) {
					// A leaf function has no unwind data: its return address is on top of the stack
					context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
					context.Rsp += sizeof(DWORD64);
				} else {
					// Code without unwind data further up (generated code): the walk ends here
					break;
				}
			}
		}

		ResumeThread(handle);

		#else
		ARX_UNUSED(frames);
		#endif

		return count;
	}

};

HangWatchdog::HangWatchdog(std::string name, std::chrono::milliseconds threshold, Reporter reporter)
	: m_name(std::move(name))
	, m_reporter(std::move(reporter))
	, m_watched(std::make_unique<Thread>())
	, m_thresholdMs(std::max<s64>(threshold.count(), 1))
	, m_lastBeatNs(watchdogClockNs())
	, m_beats(0)
	, m_where("start")
	, m_stalls(0)
	, m_watching(false)
{
	m_thread = std::thread(&HangWatchdog::run, this);
}

HangWatchdog::~HangWatchdog() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

void HangWatchdog::beat(const char * where) noexcept {
	m_where.store(where, std::memory_order_relaxed);
	m_lastBeatNs.store(watchdogClockNs(), std::memory_order_relaxed);
	m_beats.fetch_add(1, std::memory_order_release);
}

void HangWatchdog::setThreshold(std::chrono::milliseconds threshold) noexcept {
	m_thresholdMs.store(std::max<s64>(threshold.count(), 1));
}

std::chrono::milliseconds HangWatchdog::threshold() const noexcept {
	return std::chrono::milliseconds(m_thresholdMs.load());
}

void HangWatchdog::run() {

	m_symbols = std::make_unique<Symbols>();
	s64 refreshedNs = watchdogClockNs();
	m_watching = true;

	u64 seenBeats = m_beats.load(std::memory_order_acquire);
	int reports = 0;
	s64 stallBeatNs = 0;

	std::unique_lock<std::mutex> lock(m_mutex);
	while(!m_stop) {

		const s64 thresholdMs = m_thresholdMs.load();
		m_wake.wait_for(lock, std::chrono::milliseconds(std::clamp<s64>(thresholdMs / 10, 10, 100)));
		if(m_stop) {
			break;
		}

		u64 beats = m_beats.load(std::memory_order_acquire);
		if(beats != seenBeats) {
			seenBeats = beats;
			if(reports != 0) {
				s64 stalledMs = (m_lastBeatNs.load(std::memory_order_relaxed) - stallBeatNs) / 1000000;
				std::string text = m_name + " responsive again after " + std::to_string(stalledMs) + " ms\n";
				lock.unlock();
				m_reporter(text);
				lock.lock();
			}
			reports = 0;
			#if ARX_PLATFORM == ARX_PLATFORM_WIN32
			// The thread just beat, so it is not stuck in the loader: modules loaded since are listed now
			if(watchdogClockNs() - refreshedNs >= s64(10) * 1000000000) {
				lock.unlock();
				m_symbols->refresh();
				lock.lock();
				refreshedNs = watchdogClockNs();
			}
			#else
			ARX_UNUSED(refreshedNs);
			#endif
			continue;
		}

		const s64 lastBeatNs = m_lastBeatNs.load(std::memory_order_relaxed);
		const s64 sinceBeatMs = (watchdogClockNs() - lastBeatNs) / 1000000;
		if(reports < MaxReportsPerStall && sinceBeatMs >= thresholdMs * (reports + 1)) {
			if(reports == 0) {
				stallBeatNs = lastBeatNs;
				m_stalls++;
			}
			reports++;
			lock.unlock();
			report(sinceBeatMs, reports);
			lock.lock();
		}

	}

}

void HangWatchdog::report(s64 stalledMs, int index) {

	u64 frames[MaxStackFrames];
	size_t count = m_watched->captureStack(frames);

	std::ostringstream oss;
	oss << m_name << " stalled for " << stalledMs << " ms (report " << index << " of at most "
	    << MaxReportsPerStall << "), last beat at " << m_where.load(std::memory_order_relaxed) << '\n';

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	for(size_t i = 0; i < count; i++) {
		oss << "  #" << i << ' ' << m_symbols->moduleOffset(frames[i]) << m_symbols->describe(frames[i]) << '\n';
	}
	#endif

	if(count == 0) {
		oss << "  (no stack: the thread could not be walked on this platform)\n";
	}

	m_reporter(oss.str());
}

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void stallThisThread(std::chrono::milliseconds duration) {
	static std::atomic<u64> stalls(0);
	std::this_thread::sleep_for(duration);
	// Work after the sleep: the call is no tail call, so this frame stays on the stack while it sleeps
	stalls++;
}

} // namespace platform
