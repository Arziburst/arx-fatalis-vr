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

#include "platform/DumpHelper.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#include "platform/Architecture.h"
#include "platform/ProgramOptions.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#include <wincrypt.h>
#include <dbghelp.h>
#endif

namespace platform {

namespace {
bool g_noRecorder = false;
void setNoRecorder() { g_noRecorder = true; }
} // anonymous namespace

ARX_PROGRAM_OPTION("no-recorder", nullptr,
                   "Turn off the flight recorder and its out-of-process dump helper (#545, the overhead baseline)",
                   &setNoRecorder)

bool flightRecorderRequested() noexcept {
	return !g_noRecorder;
}

const char * dumpKindName(DumpKind kind) noexcept {
	switch(kind) {
		case DumpKind::Crash:       return "crash";
		case DumpKind::Abort:       return "abort";
		case DumpKind::Terminate:   return "terminate";
		case DumpKind::ExitOutside: return "exit from outside";
		case DumpKind::Hang:        return "hang";
		case DumpKind::Terminated:  return "terminated";
		case DumpKind::ExitInProcess: return "exit (in process)";
	}
	return "crash";
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

namespace {

// The recorder: a ring for every thread the game has (about 20 at a time, plus the short-lived ones a kill
// from outside makes - a thread without a ring loses its birth record), each long enough to keep ~90 s of
// frames at 90 Hz so a long run still has its early state changes. 24 * 8192 * 64 B = 12 MiB of a pagefile
// section: pages the game touches land in its working set, not in its private bytes (the 5 MiB budget).
const size_t RecorderThreads = 24;
const size_t RecorderRecords = 8192;

const u32 SharedMagic = 0x46524431; // "FRD1"
const u32 SharedVersion = 1;

// The control area the game and the helper share, followed by the recorder's memory in the same mapping.
// POD, fixed size, no pointers: the helper maps it at its own address.
#pragma pack(push, 8)
struct DumpShared {
	u32 magic;
	u32 version;
	u32 gamePid;
	u32 recorderOffset;
	u32 recorderSize;
	volatile LONG requestSeq;   // the game bumps this to ask for a report
	volatile LONG doneSeq;      // the helper sets this to the requestSeq it has handled
	volatile LONG cleanExit;    // the game sets this at its own clean shutdown
	// The request payload, filled before requestSeq is bumped
	u32 kind;
	s32 exitCode;
	u32 wantDump;
	u32 final;                  // 1 = the game is ending (report after it exits); 0 = a hang (report now)
	u32 faultThreadId;
	u64 exceptionPointers;
	char killer[160];
	char faultModule[160];
	char exceptionName[96];
	char stack[8192];
	char exeVersion[96];
	char lastReportFolder[64]; // the helper writes the leaf name of the folder it just made, for the game's log line
	wchar_t exePath[520];      // the helper hashes it for report.txt's exehash= (the pdb archive's key)
	wchar_t reportRoot[520];
	wchar_t homePath[260];
	wchar_t userName[128];
	wchar_t machineName[128];
};
#pragma pack(pop)

// --- The game's side -------------------------------------------------------------------------------------

HANDLE g_mapping = nullptr;
DumpShared * g_shared = nullptr;
unsigned char * g_recorderMem = nullptr;
FlightRecorder * g_recorder = nullptr;
HANDLE g_reqEvent = nullptr;
HANDLE g_doneEvent = nullptr;
HANDLE g_helperProcess = nullptr;
bool g_enabled = false;
u32 g_mainThreadId = 0;
std::atomic<bool> g_requestInFlight(false);

// In-process symbols for the crash stack (#545): a handle of our own (DbgHelp keys its state by the handle
// value, and the hang watchdog already uses the pseudo-handle), deferred loads so the start costs nothing -
// arx.pdb is read at the first lookup, inside the crash handler
HANDLE g_symProcess = nullptr;
bool g_symReady = false;

void initializeSymbols() {
	g_symProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, GetCurrentProcessId());
	if(!g_symProcess) {
		return;
	}
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS
	              | SYMOPT_NO_PROMPTS);
	// The executable's folder holds its .pdb
	wchar_t path[MAX_PATH] = {};
	DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if(length != 0 && length < MAX_PATH) {
		for(wchar_t * c = path + length; c > path; c--) {
			if(c[-1] == L'\\' || c[-1] == L'/') {
				*c = L'\0';
				break;
			}
		}
	} else {
		path[0] = L'\0';
	}
	g_symReady = SymInitializeW(g_symProcess, path[0] ? path : nullptr, TRUE) == TRUE;
}

std::wstring widen(const std::string & text) {
	if(text.empty()) {
		return std::wstring();
	}
	int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
	std::wstring result(size_t(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
	return result;
}

void copyNarrow(char * dst, size_t cap, std::string_view text) {
	size_t n = std::min(text.size(), cap - 1);
	std::memcpy(dst, text.data(), n);
	dst[n] = '\0';
}

void copyWide(wchar_t * dst, size_t cap, const std::wstring & text) {
	size_t n = std::min(text.size(), cap - 1);
	std::memcpy(dst, text.data(), n * sizeof(wchar_t));
	dst[n] = L'\0';
}

// The account and machine names, read once; "" when too short or generic (as the privacy scan treats them)
std::wstring envValue(const wchar_t * name) {
	wchar_t buffer[256];
	DWORD n = GetEnvironmentVariableW(name, buffer, DWORD(std::size(buffer)));
	if(n == 0 || n >= std::size(buffer)) {
		return std::wstring();
	}
	return std::wstring(buffer, n);
}

bool genericName(const std::wstring & name) {
	static const wchar_t * generic[] = { L"SYSTEM", L"Administrator", L"Admin", L"User", L"Public",
		L"Default", L"Guest", L"WORKGROUP" };
	for(const wchar_t * g : generic) {
		if(_wcsicmp(name.c_str(), g) == 0) {
			return true;
		}
	}
	return false;
}

} // anonymous namespace

bool initializeDumpHelper(const std::string & exePath, const std::string & userDir,
                          const std::string & version, bool enabled) {

	if(!enabled) {
		g_enabled = false;
		return false;
	}

	// A name unique to this game process
	wchar_t idText[32];
	std::swprintf(idText, std::size(idText), L"%lu-%lu", GetCurrentProcessId(), GetTickCount());
	std::wstring id = idText;

	size_t recorderSize = FlightRecorder::memorySize(RecorderThreads, RecorderRecords);
	size_t total = sizeof(DumpShared) + recorderSize;

	std::wstring mapName = L"Local\\arxfr-map-" + id;
	g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
	                               DWORD(u64(total) >> 32), DWORD(total & 0xffffffff), mapName.c_str());
	if(!g_mapping) {
		return false;
	}
	g_shared = static_cast<DumpShared *>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, total));
	if(!g_shared) {
		CloseHandle(g_mapping);
		g_mapping = nullptr;
		return false;
	}

	std::memset(g_shared, 0, sizeof(DumpShared));
	g_shared->magic = SharedMagic;
	g_shared->version = SharedVersion;
	g_shared->gamePid = GetCurrentProcessId();
	g_shared->recorderOffset = u32(sizeof(DumpShared));
	g_shared->recorderSize = u32(recorderSize);
	copyNarrow(g_shared->exeVersion, sizeof(g_shared->exeVersion), version);
	copyWide(g_shared->exePath, std::size(g_shared->exePath), widen(exePath));
	copyWide(g_shared->reportRoot, std::size(g_shared->reportRoot), widen(userDir));

	// What the helper masks out of the dump (the public repo must carry no personal data, #387)
	std::wstring profile = envValue(L"USERPROFILE");
	copyWide(g_shared->homePath, std::size(g_shared->homePath), profile);
	std::wstring user = envValue(L"USERNAME");
	if(user.size() >= 4 && !genericName(user)) {
		copyWide(g_shared->userName, std::size(g_shared->userName), user);
	}
	std::wstring machine = envValue(L"COMPUTERNAME");
	if(machine.size() >= 4 && !genericName(machine)) {
		copyWide(g_shared->machineName, std::size(g_shared->machineName), machine);
	}

	g_recorderMem = reinterpret_cast<unsigned char *>(g_shared) + g_shared->recorderOffset;
	static FlightRecorder recorder(g_recorderMem, recorderSize, RecorderThreads, RecorderRecords);
	g_recorder = &recorder;
	g_mainThreadId = GetCurrentThreadId();
	g_recorder->nameThread("main thread");
	initializeSymbols();

	std::wstring reqName = L"Local\\arxfr-req-" + id;
	std::wstring doneName = L"Local\\arxfr-done-" + id;
	g_reqEvent = CreateEventW(nullptr, FALSE, FALSE, reqName.c_str());  // auto-reset
	g_doneEvent = CreateEventW(nullptr, FALSE, FALSE, doneName.c_str()); // auto-reset

	// Launch the helper: arx.exe --dump-helper --id <id> --pid <pid>. It inherits nothing; it opens the
	// game by pid and the named objects by name.
	std::wstring exe = widen(exePath);
	std::wstring cmd = L"\"" + exe + L"\" --dump-helper --id " + id + L" --pid "
	                   + std::to_wstring(GetCurrentProcessId());
	std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
	cmdline.push_back(L'\0');
	STARTUPINFOW si;
	std::memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	std::memset(&pi, 0, sizeof(pi));
	// CREATE_BREAKAWAY_FROM_JOB: a launcher's kill-on-close job must not take the helper before it has
	// written its report (#545); harmless when the process is in no such job.
	DWORD flags = CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB;
	if(!CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
		// Try again without the breakaway (the job may forbid it)
		if(!CreateProcessW(exe.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
		                   nullptr, nullptr, &si, &pi)) {
			g_enabled = true; // the recorder still works; there is just no out-of-process helper
			g_recorder->nameThread("main thread");
			g_mainThreadId = GetCurrentThreadId();
			return true;
		}
	}
	g_helperProcess = pi.hProcess;
	if(pi.hThread) {
		CloseHandle(pi.hThread);
	}

	g_enabled = true;
	g_mainThreadId = GetCurrentThreadId();
	return true;
}

FlightRecorder * flightRecorder() noexcept {
	return g_recorder;
}

void flightRecord(RecordKind kind, std::string_view text, s64 value) noexcept {
	if(g_recorder) {
		g_recorder->record(kind, text, value);
	}
}

void flightNameThread(std::string_view name) noexcept {
	if(g_recorder) {
		g_recorder->nameThread(name);
	}
}

u32 mainThreadId() noexcept {
	return g_mainThreadId;
}

bool dumpHelperEnabled() noexcept {
	return g_enabled;
}

void requestReport(DumpKind kind, bool wantDump, u32 faultThreadId, u64 exceptionPointers,
                   std::string_view killer, std::string_view faultModule, std::string_view exceptionName,
                   std::string_view stack, u32 timeoutMs) noexcept {

	// The cause always goes into the recorder, helper or not
	flightRecord(RecordKind::Exit, std::string(dumpKindName(kind)));

	if(!g_shared || !g_reqEvent || !g_helperProcess) {
		return;
	}

	// One request at a time: a crash during a hang report keeps the first
	bool expected = false;
	if(!g_requestInFlight.compare_exchange_strong(expected, true)) {
		return;
	}

	g_shared->kind = u32(kind);
	g_shared->wantDump = wantDump ? 1u : 0u;
	g_shared->final = (kind == DumpKind::Hang) ? 0u : 1u;
	g_shared->faultThreadId = faultThreadId ? faultThreadId : GetCurrentThreadId();
	g_shared->exceptionPointers = exceptionPointers;
	g_shared->exitCode = 0;
	copyNarrow(g_shared->killer, sizeof(g_shared->killer), killer);
	copyNarrow(g_shared->faultModule, sizeof(g_shared->faultModule), faultModule);
	copyNarrow(g_shared->exceptionName, sizeof(g_shared->exceptionName), exceptionName);
	copyNarrow(g_shared->stack, sizeof(g_shared->stack), stack);

	LONG seq = InterlockedIncrement(&g_shared->requestSeq);
	SetEvent(g_reqEvent);

	// Wait a bounded time for the helper to take the dump (never refuse the kill); a helper that is gone is
	// not waited for
	bool done = false;
	DWORD deadline = GetTickCount() + timeoutMs;
	while(true) {
		if(g_shared->doneSeq >= seq) {
			done = true;
			break;
		}
		if(WaitForSingleObject(g_helperProcess, 0) == WAIT_OBJECT_0) {
			break;
		}
		DWORD now = GetTickCount();
		if(now >= deadline) {
			break;
		}
		if(g_doneEvent) {
			WaitForSingleObject(g_doneEvent, std::min<DWORD>(deadline - now, 100));
		} else {
			Sleep(10);
		}
	}

	// A hang is one-shot (the game runs on and may report again); a request the helper never answered must
	// not block a later, real one either
	if(kind == DumpKind::Hang || !done) {
		g_requestInFlight.store(false);
	}
}

void setReportStack(std::string_view stack) noexcept {
	if(g_shared) {
		copyNarrow(g_shared->stack, sizeof(g_shared->stack), stack);
	}
}

void dumpHelperMarkClean() noexcept {
	if(g_shared) {
		InterlockedExchange(&g_shared->cleanExit, 1);
	}
	if(g_reqEvent) {
		SetEvent(g_reqEvent);
	}
}

std::string dumpLastReportFolder() {
	if(!g_shared) {
		return std::string();
	}
	return std::string(g_shared->lastReportFolder,
	                   strnlen(g_shared->lastReportFolder, sizeof(g_shared->lastReportFolder)));
}

std::string moduleLabel(u64 address) {
	char buffer[64];
	HMODULE module = nullptr;
	DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
	if(GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(address)), &module) && module) {
		wchar_t path[MAX_PATH];
		if(GetModuleFileNameW(module, path, MAX_PATH) != 0) {
			const wchar_t * file = path;
			for(const wchar_t * c = path; *c != L'\0'; c++) {
				if(*c == L'\\' || *c == L'/') {
					file = c + 1;
				}
			}
			char name[MAX_PATH];
			int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, name, int(sizeof(name)), nullptr, nullptr);
			std::snprintf(buffer, sizeof(buffer), "+0x%llx",
			              static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(module)));
			return (n > 1 ? std::string(name, size_t(n - 1)) : std::string("?")) + buffer;
		}
	}
	std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(address));
	return buffer;
}

std::string exceptionCodeName(u32 code) {
	const char * name = nullptr;
	switch(code) {
		case 0xC0000005: name = "EXCEPTION_ACCESS_VIOLATION"; break;
		case 0xC0000006: name = "EXCEPTION_IN_PAGE_ERROR"; break;
		case 0x80000003: name = "EXCEPTION_BREAKPOINT"; break;
		case 0x80000004: name = "EXCEPTION_SINGLE_STEP"; break;
		case 0xC000008C: name = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"; break;
		case 0xC000008E: name = "EXCEPTION_FLT_DIVIDE_BY_ZERO"; break;
		case 0xC0000090: name = "EXCEPTION_FLT_INVALID_OPERATION"; break;
		case 0xC0000094: name = "EXCEPTION_INT_DIVIDE_BY_ZERO"; break;
		case 0xC0000095: name = "EXCEPTION_INT_OVERFLOW"; break;
		case 0xC0000096: name = "EXCEPTION_PRIV_INSTRUCTION"; break;
		case 0xC000001D: name = "EXCEPTION_ILLEGAL_INSTRUCTION"; break;
		case 0xC0000025: name = "EXCEPTION_NONCONTINUABLE_EXCEPTION"; break;
		case 0xC00000FD: name = "EXCEPTION_STACK_OVERFLOW"; break;
		case 0xC0000374: name = "STATUS_HEAP_CORRUPTION"; break;
		case 0xC0000409: name = "STATUS_STACK_BUFFER_OVERRUN"; break;
		case 0xC0000417: name = "STATUS_INVALID_CRUNTIME_PARAMETER"; break;
		case 0xE06D7363: name = "CPP_EXCEPTION"; break;
		default: break;
	}
	char buffer[80];
	if(name) {
		std::snprintf(buffer, sizeof(buffer), "0x%08x %s", unsigned(code), name);
	} else {
		std::snprintf(buffer, sizeof(buffer), "0x%08x", unsigned(code));
	}
	return buffer;
}

std::string threadStartLabel(bool & isExit) {
	isExit = false;
	using NtQueryInformationThreadFn = LONG (NTAPI *)(HANDLE, int, PVOID, ULONG, PULONG);
	static NtQueryInformationThreadFn query = reinterpret_cast<NtQueryInformationThreadFn>(
		reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
	void * start = nullptr;
	if(query) {
		query(GetCurrentThread(), 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), nullptr);
	}
	// ExitThread ends only the thread: named, but no exit of the process
	struct Routine { const char * name; void * address; bool exitsProcess; };
	static const Routine routines[] = {
		{ "KERNEL32!ExitProcess", reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess")), true },
		{ "KERNEL32!TerminateProcess", reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TerminateProcess")), true },
		{ "ntdll!RtlExitUserProcess", reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlExitUserProcess")), true },
		{ "KERNEL32!ExitThread", reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitThread")), false },
	};
	for(const Routine & routine : routines) {
		if(start && start == routine.address) {
			isExit = routine.exitsProcess;
			return routine.name;
		}
	}
	return moduleLabel(u64(reinterpret_cast<uintptr_t>(start)));
}

std::string killerLabel() {
	DWORD tid = GetCurrentThreadId();
	if(tid == g_mainThreadId) {
		return std::to_string(tid) + " main thread";
	}
	bool isExit = false;
	return std::to_string(tid) + " start " + threadStartLabel(isExit);
}

std::string symbolizedStack(const void * contextIn) {

	std::string out;
	#if ARX_ARCH == ARX_ARCH_X86_64
	if(!contextIn) {
		return out;
	}
	CONTEXT context;
	std::memcpy(&context, contextIn, sizeof(context));

	STACKFRAME64 frame;
	std::memset(&frame, 0, sizeof(frame));
	frame.AddrPC.Offset = context.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = context.Rsp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = context.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	HANDLE process = g_symProcess ? g_symProcess : GetCurrentProcess();
	if(g_symReady) {
		// Modules loaded since the start (the OpenXR runtime, a driver): named too
		SymRefreshModuleList(process);
	}
	for(int i = 0; i < 64; i++) {
		BOOL ok = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
		                      SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
		if(ok != TRUE || frame.AddrPC.Offset == 0) {
			break;
		}
		DWORD64 address = frame.AddrPC.Offset;
		std::string line = "  #" + std::to_string(i) + " " + moduleLabel(u64(address));
		if(g_symReady) {
			alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
			SYMBOL_INFO * symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 511;
			DWORD64 displacement = 0;
			if(SymFromAddr(process, address, &displacement, symbol) == TRUE) {
				line.push_back(' ');
				line.append(symbol->Name, std::min<size_t>(symbol->NameLen, 511));
			}
			IMAGEHLP_LINE64 lineInfo;
			std::memset(&lineInfo, 0, sizeof(lineInfo));
			lineInfo.SizeOfStruct = sizeof(lineInfo);
			DWORD lineDisplacement = 0;
			if(SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo) == TRUE && lineInfo.FileName) {
				const char * file = lineInfo.FileName;
				for(const char * c = lineInfo.FileName; *c != '\0'; c++) {
					if(*c == '\\' || *c == '/') {
						file = c + 1;
					}
				}
				line += " (";
				line += file;
				line += ':';
				line += std::to_string(lineInfo.LineNumber);
				line += ')';
			}
		}
		out += line;
		out.push_back('\n');
	}
	#else
	ARX_UNUSED(contextIn);
	#endif
	return out;
}

// =====================================================================================================
// The helper process (arx.exe --dump-helper --id <id> --pid <pid>)
// =====================================================================================================

namespace {

std::wstring helperArg(int argc, char ** argv, const char * name) {
	for(int i = 1; i + 1 < argc; i++) {
		if(std::strcmp(argv[i], name) == 0) {
			std::string value = argv[i + 1];
			int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0);
			std::wstring result(size_t(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.data(), int(value.size()), result.data(), size);
			return result;
		}
	}
	return std::wstring();
}

// yyyy-MM-dd-HH-mm-ss-NN under <root>, the first that does not exist (crashes\ have always been named so)
std::wstring makeReportFolder(const std::wstring & root, const std::wstring & sub) {
	std::wstring base = root + L"\\" + sub;
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW(base.c_str(), nullptr);
	SYSTEMTIME t;
	GetSystemTime(&t);
	for(int count = 0; count < 100; count++) {
		wchar_t name[64];
		std::swprintf(name, std::size(name), L"%04u-%02u-%02u-%02u-%02u-%02u-%02u",
		              unsigned(t.wYear), unsigned(t.wMonth), unsigned(t.wDay), unsigned(t.wHour),
		              unsigned(t.wMinute), unsigned(t.wSecond), unsigned(count));
		std::wstring folder = base + L"\\" + name;
		if(GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES) {
			if(CreateDirectoryW(folder.c_str(), nullptr)) {
				return folder;
			}
		}
	}
	return std::wstring();
}

void writeFileBytes(const std::wstring & path, const void * data, size_t size) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file == INVALID_HANDLE_VALUE) {
		return;
	}
	DWORD written = 0;
	WriteFile(file, data, DWORD(size), &written, nullptr);
	CloseHandle(file);
}

void writeTextFile(const std::wstring & path, const std::string & text) {
	writeFileBytes(path, text.data(), text.size());
}

unsigned char lowerAscii(unsigned char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

// Mask every occurrence of a byte pattern in place with the same-length replacement; ASCII letters match
// whatever their case (a path is spelled c:\users\ as often as C:\Users\), which holds for the UTF-16 form
// too, since its high bytes are zero for these characters
void maskBytes(std::vector<unsigned char> & bytes, const std::string & pattern, const std::string & replacement) {
	if(pattern.empty() || pattern.size() != replacement.size() || bytes.size() < pattern.size()) {
		return;
	}
	for(size_t i = 0; i + pattern.size() <= bytes.size(); ) {
		bool match = true;
		for(size_t k = 0; k < pattern.size(); k++) {
			if(lowerAscii(bytes[i + k]) != lowerAscii(static_cast<unsigned char>(pattern[k]))) {
				match = false;
				break;
			}
		}
		if(match) {
			std::memcpy(bytes.data() + i, replacement.data(), replacement.size());
			i += pattern.size();
		} else {
			i++;
		}
	}
}

// The SHA-256 of a file as lower-case hex (the pdb archive's key, scripts/launch-lib.ps1), or ""
std::string fileSha256(const std::wstring & path) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file == INVALID_HANDLE_VALUE) {
		return std::string();
	}
	std::string result;
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	if(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
	   && CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
		std::vector<unsigned char> chunk(1 << 16);
		DWORD read = 0;
		bool ok = true;
		while(ReadFile(file, chunk.data(), DWORD(chunk.size()), &read, nullptr) && read) {
			if(!CryptHashData(hash, chunk.data(), read, 0)) {
				ok = false;
				break;
			}
		}
		unsigned char digest[32];
		DWORD length = sizeof(digest);
		if(ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0)) {
			char hex[65];
			for(DWORD i = 0; i < length && i < 32; i++) {
				std::snprintf(hex + i * 2, 3, "%02x", unsigned(digest[i]));
			}
			result.assign(hex, size_t(length) * 2);
		}
	}
	if(hash) {
		CryptDestroyHash(hash);
	}
	if(provider) {
		CryptReleaseContext(provider, 0);
	}
	CloseHandle(file);
	return result;
}

// The UTF-16LE bytes of an ASCII-ish wide string
std::string utf16Bytes(const std::wstring & text) {
	std::string out;
	out.reserve(text.size() * 2);
	for(wchar_t c : text) {
		out.push_back(char(c & 0xff));
		out.push_back(char((c >> 8) & 0xff));
	}
	return out;
}

std::string narrowBytes(const std::wstring & text) {
	int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(size_t(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
	return out;
}

// Rewrite any personal data in a buffer with same-length placeholders, as 8-bit text and as UTF-16
void scrubBuffer(std::vector<unsigned char> & bytes, const DumpShared & shared) {

	auto maskBoth = [&](const std::wstring & text) {
		if(text.empty()) {
			return;
		}
		std::string narrow = narrowBytes(text);
		maskBytes(bytes, narrow, std::string(narrow.size(), '#'));
		std::string wide = utf16Bytes(text);
		// Keep the length even: replace every byte, high byte kept 0 so it stays valid UTF-16
		std::string wideMask(wide.size(), '\0');
		for(size_t i = 0; i < wide.size(); i += 2) {
			wideMask[i] = '#';
		}
		maskBytes(bytes, wide, wideMask);
	};

	// The account, the machine and the home path - the distinctive personal strings
	maskBoth(shared.userName);
	maskBoth(shared.machineName);
	maskBoth(shared.homePath);

	// The path prefix the privacy scan flags whatever the account: "\Users" / "/Users" and the old
	// "Documents and Settings". Break the directory token so [drive]:[\/]+Users no longer matches.
	for(const wchar_t * sep : { L"\\Users", L"/Users" }) {
		std::wstring token = sep;
		std::wstring broken = token;
		broken[1] = L'#'; // "\Users" -> "\#sers"
		maskBytes(bytes, narrowBytes(token), narrowBytes(broken));
		std::string wide = utf16Bytes(token), wideBroken = utf16Bytes(broken);
		maskBytes(bytes, wide, wideBroken);
	}
	for(const wchar_t * sep : { L"\\Documents and Settings", L"/Documents and Settings" }) {
		std::wstring token = sep;
		std::wstring broken = token;
		broken[1] = L'#';
		maskBytes(bytes, narrowBytes(token), narrowBytes(broken));
		maskBytes(bytes, utf16Bytes(token), utf16Bytes(broken));
	}
}

void scrubFileInPlace(const std::wstring & path, const DumpShared & shared) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
	                          FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file == INVALID_HANDLE_VALUE) {
		return;
	}
	LARGE_INTEGER size;
	if(!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (256ll << 20)) {
		CloseHandle(file);
		return;
	}
	std::vector<unsigned char> bytes(size_t(size.QuadPart));
	DWORD read = 0;
	if(ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size()) {
		scrubBuffer(bytes, shared);
		SetFilePointer(file, 0, nullptr, FILE_BEGIN);
		DWORD written = 0;
		WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr);
	}
	CloseHandle(file);
}

// A minidump of the game, written while it is still alive (every thread before the exit). The recorder is
// a user stream; the scrub masks the paths afterwards. Returns true when the file was written.
bool writeMinidump(HANDLE game, DumpShared * shared, const std::wstring & path) {

	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
	                          FILE_ATTRIBUTE_NORMAL, nullptr);
	if(file == INVALID_HANDLE_VALUE) {
		return false;
	}

	// The recorder serialised, as a user stream (a type above 0xffff)
	std::string recorderText = serializeRecorder(reinterpret_cast<unsigned char *>(shared) + shared->recorderOffset,
	                                              shared->recorderSize);
	MINIDUMP_USER_STREAM userStream;
	userStream.Type = 0x46520001; // >= 0x10000: the flight recorder
	userStream.BufferSize = ULONG(recorderText.size());
	userStream.Buffer = const_cast<char *>(recorderText.data());
	MINIDUMP_USER_STREAM_INFORMATION userInfo;
	userInfo.UserStreamCount = 1;
	userInfo.UserStreamArray = &userStream;

	MINIDUMP_EXCEPTION_INFORMATION exInfo;
	std::memset(&exInfo, 0, sizeof(exInfo));
	bool haveException = shared->exceptionPointers != 0;
	if(haveException) {
		exInfo.ThreadId = shared->faultThreadId;
		exInfo.ExceptionPointers = reinterpret_cast<PEXCEPTION_POINTERS>(shared->exceptionPointers);
		exInfo.ClientPointers = TRUE;
	}

	MINIDUMP_TYPE type = MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules
	                                   | MiniDumpWithHandleData | MiniDumpWithIndirectlyReferencedMemory);

	BOOL ok = MiniDumpWriteDump(game, shared->gamePid, file, type, haveException ? &exInfo : nullptr,
	                            &userInfo, nullptr);
	CloseHandle(file);
	if(!ok) {
		DeleteFileW(path.c_str());
		return false;
	}
	return true;
}

std::string exitText(DWORD code) {
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%d (0x%x)", int(s32(code)), unsigned(code));
	return buffer;
}

// Keep only the newest \a keep report folders of a directory (by name, which is the UTC time)
void pruneReports(const std::wstring & dir, size_t keep) {
	std::vector<std::wstring> names;
	WIN32_FIND_DATAW find;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &find);
	if(h == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		if((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && find.cFileName[0] != L'.') {
			names.push_back(find.cFileName);
		}
	} while(FindNextFileW(h, &find));
	FindClose(h);
	std::sort(names.begin(), names.end());
	if(names.size() <= keep) {
		return;
	}
	for(size_t i = 0; i + keep < names.size(); i++) {
		std::wstring folder = dir + L"\\" + names[i];
		// Remove the folder and its files
		WIN32_FIND_DATAW inner;
		HANDLE ih = FindFirstFileW((folder + L"\\*").c_str(), &inner);
		if(ih != INVALID_HANDLE_VALUE) {
			do {
				if(!(inner.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					DeleteFileW((folder + L"\\" + inner.cFileName).c_str());
				}
			} while(FindNextFileW(ih, &inner));
			FindClose(ih);
		}
		RemoveDirectoryW(folder.c_str());
	}
}

// Copy a file the game wrote (arx-perf.log) into the report folder, shared-read (the game may hold it)
void copyLogFile(const std::wstring & src, const std::wstring & dst) {
	HANDLE in = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(in == INVALID_HANDLE_VALUE) {
		return;
	}
	std::vector<unsigned char> bytes;
	unsigned char chunk[65536];
	DWORD read = 0;
	while(ReadFile(in, chunk, sizeof(chunk), &read, nullptr) && read) {
		bytes.insert(bytes.end(), chunk, chunk + read);
	}
	CloseHandle(in);
	writeFileBytes(dst, bytes.data(), bytes.size());
}

// Write the whole report folder (report.txt, arx-recorder.txt, a copy of arx-perf.log, and crash.txt for a
// crash so the run digest still reads it). The dump, when any, was already written into \a folder.
void writeReportFolder(const std::wstring & folder, DumpShared * shared, DumpKind kind, DWORD exitCode,
                       bool exitKnown, bool haveDump, const std::wstring & dumpName) {

	std::string recorderText = serializeRecorder(reinterpret_cast<unsigned char *>(shared) + shared->recorderOffset,
	                                              shared->recorderSize);

	// report.txt
	std::string report;
	report += "kind=";
	report += dumpKindName(kind);
	report += "\n";
	report += "exit=";
	report += (kind == DumpKind::Hang) ? std::string("running") : (exitKnown ? exitText(exitCode) : std::string("unknown"));
	report += "\n";
	if(shared->exceptionName[0]) {
		report += "exception=";
		report += shared->exceptionName;
		report += "\n";
	}
	report += "killer=";
	report += shared->killer;
	report += "\n";
	report += "module=";
	report += (shared->faultModule[0] ? shared->faultModule : "none");
	report += "\n";
	// The build: its version and the exe's hash, the key of the archived arx.pdb that matches this report
	report += "exe=";
	report += shared->exeVersion;
	report += "\n";
	report += "exehash=";
	report += fileSha256(shared->exePath);
	report += "\n";
	report += "dump=";
	report += (haveDump ? narrowBytes(dumpName) : std::string("none"));
	report += "\n";
	report += "stack:\n";
	report += shared->stack;
	if(shared->stack[0] && report.back() != '\n') {
		report += "\n";
	}

	writeTextFile(folder + L"\\report.txt", report);
	writeTextFile(folder + L"\\arx-recorder.txt", recorderText);

	// The run's performance log up to the report: its R lines are the resource trend crash-report prints
	copyLogFile(std::wstring(shared->reportRoot) + L"\\arx-perf.log", folder + L"\\arx-perf.log");

	// A crash.txt the run digest (scripts/dev/log-digest-lib.ps1) and smoke-test can read: title, the
	// exception code, the callstack
	if(kind == DumpKind::Crash || kind == DumpKind::Abort || kind == DumpKind::Terminate) {
		std::string crash;
		crash += (shared->faultModule[0] ? shared->faultModule : dumpKindName(kind));
		crash += "\n";
		if(shared->exceptionName[0]) {
			crash += "Exception code: ";
			crash += shared->exceptionName;
			crash += "\n";
		}
		crash += "Callstack:\n";
		crash += shared->stack;
		writeTextFile(folder + L"\\crash.txt", crash);
	}

	// Scrub every file of the folder (the dump too)
	WIN32_FIND_DATAW find;
	HANDLE h = FindFirstFileW((folder + L"\\*").c_str(), &find);
	if(h != INVALID_HANDLE_VALUE) {
		do {
			if(!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				scrubFileInPlace(folder + L"\\" + find.cFileName, *shared);
			}
		} while(FindNextFileW(h, &find));
		FindClose(h);
	}
}

} // anonymous namespace

#else // not Windows: the recorder and the out-of-process helper are a Windows feature; the API is a no-op

bool initializeDumpHelper(const std::string &, const std::string &, const std::string &, bool) { return false; }
FlightRecorder * flightRecorder() noexcept { return nullptr; }
void flightRecord(RecordKind, std::string_view, s64) noexcept { }
void flightNameThread(std::string_view) noexcept { }
u32 mainThreadId() noexcept { return 0; }
bool dumpHelperEnabled() noexcept { return false; }
void requestReport(DumpKind, bool, u32, u64, std::string_view, std::string_view, std::string_view,
                   std::string_view, u32) noexcept { }
void dumpHelperMarkClean() noexcept { }
void setReportStack(std::string_view) noexcept { }
std::string dumpLastReportFolder() { return std::string(); }
std::string moduleLabel(u64) { return std::string(); }
std::string exceptionCodeName(u32) { return std::string(); }
std::string threadStartLabel(bool & isExit) { isExit = false; return std::string(); }
std::string killerLabel() { return std::string(); }
std::string symbolizedStack(const void *) { return std::string(); }

#endif // ARX_PLATFORM_WIN32

int runDumpHelper(int argc, char ** argv) {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	std::wstring id = helperArg(argc, argv, "--id");
	std::wstring pidText = helperArg(argc, argv, "--pid");
	if(id.empty() || pidText.empty()) {
		return 0;
	}
	DWORD gamePid = DWORD(_wtoi(pidText.c_str()));

	HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, (L"Local\\arxfr-map-" + id).c_str());
	if(!mapping) {
		return 0;
	}
	DumpShared * shared = static_cast<DumpShared *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if(!shared || shared->magic != SharedMagic) {
		return 0;
	}

	HANDLE reqEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (L"Local\\arxfr-req-" + id).c_str());
	HANDLE doneEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, (L"Local\\arxfr-done-" + id).c_str());
	HANDLE game = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE | PROCESS_DUP_HANDLE,
	                          FALSE, gamePid);
	if(!game) {
		return 0;
	}

	std::wstring root = shared->reportRoot;

	// A pending final report: a crash/exit took its dump while the game was alive; the report files are
	// written once the game exits (so the exit code is the real one)
	bool pendingFinal = false;
	DumpKind pendingKind = DumpKind::Crash;
	std::wstring pendingFolder;
	bool pendingDump = false;
	std::wstring pendingDumpName = L"arx.dmp";

	LONG handled = 0;

	HANDLE waits[2] = { reqEvent, game };
	while(true) {

		DWORD which = WaitForMultipleObjects(reqEvent ? 2 : 1, reqEvent ? waits : &game, FALSE, INFINITE);
		if(which == WAIT_FAILED) {
			break;
		}

		bool gameExited = (which == WAIT_OBJECT_0 + (reqEvent ? 1 : 0));
		bool request = reqEvent && which == WAIT_OBJECT_0;

		if(request) {
			// A clean shutdown only wakes us (no new request); a crash during the exit (a static destructor, a
			// runtime unloading) after markClean is still a request and still gets its report
			LONG seq = shared->requestSeq;
			if(seq <= handled) {
				continue;
			}
			handled = seq;
			DumpKind kind = DumpKind(shared->kind);
			bool wantDump = shared->wantDump != 0;
			bool final = shared->final != 0;

			std::wstring sub = (kind == DumpKind::Hang) ? L"hangs" : L"crashes";
			std::wstring folder = makeReportFolder(root, sub);
			// Tell the game the folder's leaf name, so it can name the dump in its log ([hang] dump written)
			std::memset(shared->lastReportFolder, 0, sizeof(shared->lastReportFolder));
			if(!folder.empty()) {
				size_t slash = folder.find_last_of(L"\\/");
				std::string leaf = narrowBytes(slash == std::wstring::npos ? folder : folder.substr(slash + 1));
				std::memcpy(shared->lastReportFolder, leaf.data(),
				            std::min(leaf.size(), sizeof(shared->lastReportFolder) - 1));
			}
			std::wstring dumpName = L"arx.dmp";
			bool haveDump = false;
			if(!folder.empty() && wantDump) {
				haveDump = writeMinidump(game, shared, folder + L"\\" + dumpName);
			}

			if(final) {
				// Defer the report files to the game's exit (for the real exit code)
				pendingFinal = true;
				pendingKind = kind;
				pendingFolder = folder;
				pendingDump = haveDump;
				pendingDumpName = dumpName;
			} else {
				// A hang: write it all now, the game runs on (exit = "running")
				if(!folder.empty()) {
					writeReportFolder(folder, shared, kind, 0, false, haveDump, dumpName);
					pruneReports(root + L"\\hangs", 5);
				}
			}

			// Tell the game it may proceed
			shared->doneSeq = seq;
			if(doneEvent) {
				SetEvent(doneEvent);
			}
			continue;
		}

		if(gameExited) {
			DWORD exitCode = 0;
			GetExitCodeProcess(game, &exitCode);
			bool clean = InterlockedCompareExchange(&shared->cleanExit, 1, 1) == 1;

			// arx-recorder.txt in the user directory is written on every exit, clean or not (the game
			// could not when it was killed) - the last events of the run, scrubbed of personal data
			{
				std::string recorderText = serializeRecorder(
					reinterpret_cast<unsigned char *>(shared) + shared->recorderOffset, shared->recorderSize);
				std::wstring userRecorder = root + L"\\arx-recorder.txt";
				writeTextFile(userRecorder, recorderText);
				scrubFileInPlace(userRecorder, *shared);
			}

			if(pendingFinal) {
				if(!pendingFolder.empty()) {
					writeReportFolder(pendingFolder, shared, pendingKind, exitCode, true, pendingDump, pendingDumpName);
					pruneReports(root + L"\\crashes", 5);
				}
			} else if(!clean) {
				// The game was killed from outside and ran no code on the way out: no dump, kind=terminated
				std::wstring folder = makeReportFolder(root, L"crashes");
				if(!folder.empty()) {
					shared->kind = u32(DumpKind::Terminated);
					copyNarrow(shared->killer, sizeof(shared->killer), "outside the process (TerminateProcess)");
					shared->faultModule[0] = '\0';
					shared->exceptionName[0] = '\0';
					shared->stack[0] = '\0';
					writeReportFolder(folder, shared, DumpKind::Terminated, exitCode, true, false, L"");
					pruneReports(root + L"\\crashes", 5);
				}
			}
			break;
		}
	}

	return 0;

	#else
	ARX_UNUSED(argc), ARX_UNUSED(argv);
	return 0;
	#endif
}

} // namespace platform
