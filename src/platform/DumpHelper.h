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

#ifndef ARX_PLATFORM_DUMPHELPER_H
#define ARX_PLATFORM_DUMPHELPER_H

#include <string>
#include <string_view>

#include "platform/FlightRecorder.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief The out-of-process dump helper and the game's side of it (#545)
 *
 * At launch the game starts a second copy of itself, \c arx.exe \c --dump-helper, that shares a block of
 * memory with it (the flight recorder plus a small control area) and watches the game's process. When the
 * game dies, stalls or is killed, the helper writes one report folder - \c report.txt, \c arx-recorder.txt,
 * the run's \c arx-perf.log and, when the game was still there to dump, a minidump of every thread - and,
 * because it is a separate process, it does so even after a \c TerminateProcess from outside. The helper
 * writes the dump while the game is still alive (so every thread is captured before the exit proceeds) and
 * the report files after the game has exited (so the exit code is the real one); a hang is reported at once,
 * the game runs on.
 *
 * \c --no-recorder turns the recorder and the helper off (the overhead baseline). Windows only; a no-op
 * elsewhere. The recorder itself is \ref platform::FlightRecorder (host-tested); crash-report.ps1 reads the
 * report folders; scripts/dev/flight-recorder-check.ps1 exercises every way the game can end.
 */
namespace platform {

//! How the game ended, as the report's kind= line names it
enum class DumpKind : u32 {
	Crash,        //!< an unhandled exception / fail-fast the handler caught (kind=crash)
	Abort,        //!< abort() / SIGABRT (kind=abort)
	Terminate,    //!< std::terminate() (kind=terminate)
	ExitOutside,  //!< a thread that started at ExitProcess / TerminateProcess (kind=exit from outside)
	Hang,         //!< the hang watchdog reported a stall (kind=hang); the game runs on
	Terminated,   //!< the game was killed from outside and ran no code on the way out (kind=terminated)
	ExitInProcess //!< exit() / ExitProcess called by the game's or a library's code before the shutdown (kind=exit (in process))
};

//! "crash", "abort", "terminate", "exit from outside", "hang", "terminated", "exit (in process)"
const char * dumpKindName(DumpKind kind) noexcept;

/*!
 * \brief Start the dump helper and the flight recorder (the game's side)
 *
 * Creates the shared memory, lays the recorder out in it, and launches \c arx.exe \c --dump-helper. Call it
 * once, early, after the user directory is known (that is where the reports go). With \a enabled false
 * (\c --no-recorder) it does nothing and the recorder stays off. Returns true when the recorder is live.
 *
 * \param exePath   the path to this executable (to launch the helper)
 * \param userDir   the user directory; the helper writes userDir\\crashes\\ and userDir\\hangs\\
 * \param version   arx_name + " " + arx_version, for the report
 * \param enabled   false with --no-recorder
 * \param regionBytes  bytes of a region after the recorder the game fills itself (\ref dumpHelperRegion: the
 *                  replay ring, #550); the helper writes it into every report through \ref setDumpRegionWriter
 */
bool initializeDumpHelper(const std::string & exePath, const std::string & userDir,
                          const std::string & version, bool enabled, size_t regionBytes = 0);

//! The region of the shared memory reserved by \a regionBytes (\a size gets its bytes), 16-aligned; null without one
void * dumpHelperRegion(size_t & size) noexcept;

/*!
 * \brief How the helper turns the region into a report file (#550: vr::replayRingClip -> arx-replay.vrsim)
 *
 * Called in the helper process (utf8_main, before runDumpHelper): \a writer gets the region's bytes (a copy of the
 * game's, untrusted) and returns the file's text, "" for none; \a fileName is the file's name in the report folder
 */
void setDumpRegionWriter(std::string (*writer)(const void * image, size_t size), const char * fileName) noexcept;

//! The game's flight recorder, or nullptr when it is off; name the calling thread's ring once
FlightRecorder * flightRecorder() noexcept;

/*!
 * The game's flight recorder as text now (serializeRecorder of the live memory), scrubbed of personal data like
 * arx-recorder.txt - the tail of a mark of the moment (#551); "" when the recorder is off. Any thread
 */
std::string flightRecorderText();

//! Record on the game's recorder (no-op when it is off); the cheap call the engine uses everywhere
void flightRecord(RecordKind kind, std::string_view text, s64 value = 0) noexcept;

//! Name the calling thread's ring ("main thread", a worker's name); remembers the main thread for the killer line
void flightNameThread(std::string_view name) noexcept;

/*!
 * \brief Ask the helper for a report now, and wait a bounded time for it (the ExitGuard, the crash handler)
 *
 * Fills the request in shared memory and signals the helper, then waits up to \a timeoutMs for it to write
 * the dump (the report files follow when the game exits, except a hang which is written at once). Never
 * refuses the kill: it returns when the helper is done or the wait runs out.
 *
 * \param kind            how the game is ending
 * \param wantDump        write a minidump (the game is still alive); false when it is already gone
 * \param faultThreadId   the thread to mark as the exception thread in the dump (0 = the caller)
 * \param exceptionPointers  the address of this process's EXCEPTION_POINTERS, or 0
 * \param killer          "<tid> main thread" / "<tid> start KERNEL32!ExitProcess"
 * \param faultModule     "arx.exe+0x1234", the faulting or killing module
 * \param exceptionName   "0xc0000005 EXCEPTION_ACCESS_VIOLATION" or empty
 * \param stack           the symbolised stack, one "  #N module+0xoff func (file:line)" per line, or empty
 * \param timeoutMs       how long to wait for the helper (the bounded exit wait, 15 s)
 */
void requestReport(DumpKind kind, bool wantDump, u32 faultThreadId, u64 exceptionPointers,
                   std::string_view killer, std::string_view faultModule, std::string_view exceptionName,
                   std::string_view stack, u32 timeoutMs) noexcept;

//! The game's own shutdown is done: tell the helper no report is wanted, and let it exit
void dumpHelperMarkClean() noexcept;

/*!
 * \brief The stack for the pending report, set after the dump was taken
 *
 * A crash asks for the dump first (the process is in an unknown state, symbols load afterwards) and fills the
 * stack in once it is symbolised: the helper writes report.txt after the game has exited and reads it then.
 */
void setReportStack(std::string_view stack) noexcept;

//! The leaf name of the report folder the helper last wrote (for the game's "[hang] dump written" line), or ""
std::string dumpLastReportFolder();

//! "arx.exe+0x1234" for an address inside a loaded module, else "0x<address>" - the faulting module of a report
std::string moduleLabel(u64 address);
//! The same into \a out (\a size bytes, truncated), allocating nothing: the crash handler's request (#610)
void moduleLabel(u64 address, char * out, size_t size) noexcept;

//! "0xc0000005 EXCEPTION_ACCESS_VIOLATION": a Windows exception code with its name (the code alone when unknown)
std::string exceptionCodeName(u32 code);
//! The same into \a out, allocating nothing (#610)
void exceptionCodeName(u32 code, char * out, size_t size) noexcept;

/*!
 * \brief The calling thread's start routine: "KERNEL32!ExitProcess" or "<module>+0x<offset>"
 *
 * \a isExit is set when the thread starts straight at ExitProcess / ExitThread / TerminateProcess /
 * RtlExitUserProcess - a thread made to end the process from outside (the ExitGuard, #544).
 */
std::string threadStartLabel(bool & isExit);
//! The same into \a out, allocating nothing (#610)
void threadStartLabel(bool & isExit, char * out, size_t size) noexcept;

//! The report's killer line for the calling thread: "<tid> main thread", or "<tid> start <threadStartLabel>"
std::string killerLabel();
//! The same into \a out, allocating nothing (#610)
void killerLabel(char * out, size_t size) noexcept;

/*!
 * \brief The calling thread's stack from a context, symbolised in process
 *
 * One "  #<n> <module>+0x<offset> <function> (<file>:<line>)" line per frame (the function and the line where
 * the build's arx.pdb or a module's exports name them). \a context is a CONTEXT of the calling thread (copied,
 * not changed). The symbols were set up by initializeDumpHelper with deferred loads: the first lookup loads them.
 */
std::string symbolizedStack(const void * context);

/*!
 * \brief The pending report has its stack: the helper walked it out of process while the dump was taken (#610)
 *
 * A crash with exception pointers gets its stack from the helper, which reads the context from the game's memory
 * and symbolises it with a heap of its own; the crash handler walks the stack in process only when this is false.
 */
bool reportHasStack() noexcept;

//! "crash ntdll" (#610): the in-process stack walk faults from now on, as it did on a corrupt heap
void failInProcessStackForTest() noexcept;

//! The id of the main thread (the one that called flightNameThread("main thread")), for the killer line
u32 mainThreadId() noexcept;

//! True while the recorder and helper are live
bool dumpHelperEnabled() noexcept;

//! False when --no-recorder was given (the overhead baseline); passed to initializeDumpHelper as \a enabled
bool flightRecorderRequested() noexcept;

/*!
 * \brief The dump helper's entry point (\c arx.exe \c --dump-helper ...)
 *
 * Called from the very top of main when the first argument is \c --dump-helper, before the engine is
 * touched (so the helper stays tiny). Maps the shared memory, watches the game, writes the reports, and
 * returns an exit code. A no-op returning 0 off Windows.
 */
int runDumpHelper(int argc, char ** argv);

} // namespace platform

#endif // ARX_PLATFORM_DUMPHELPER_H
