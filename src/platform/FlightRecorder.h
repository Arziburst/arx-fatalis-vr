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

#ifndef ARX_PLATFORM_FLIGHTRECORDER_H
#define ARX_PLATFORM_FLIGHTRECORDER_H

#include <cstddef>
#include <string>
#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The flight recorder (#545): per-thread lock-free rings of fixed records
 *
 * The game keeps a short history of what happened - frames, state changes, channel commands, thread
 * births, GL errors, texture and buffer creates, watchdog beats and the cause of an exit - in a block
 * of memory it does not own (the crash handler's shared memory), so the out-of-process dump helper can
 * read it after the game is gone. Each thread writes only its own ring (one producer per ring); the
 * reader (serialize, on the helper) is the only one that reads, and a record is written under a tiny
 * sequence lock so a half-written record is never read.
 *
 * The image holds no pointers and no absolute addresses: a byte copy at another address, or bytes read
 * from another process or a minidump, serialise to the very same text. Serialising never trusts the
 * image - zeros, garbage or a cut-off copy give fewer lines, never a crash or a read past the image.
 *
 * Pure logic, no engine globals, no OpenXR, no clock read on the reader's side; covered by
 * tests/platform/FlightRecorderTest.cpp. The game's recorder is wired in src/core (#545).
 */
namespace platform {

//! The kinds of record, in the order they serialise as recordKindName()
enum class RecordKind : u8 {
	Frame,    //!< one frame of the main loop
	State,    //!< a game / VR / level / scene / panel state change
	Command,  //!< a test channel command
	Thread,   //!< a thread was born, with its start module!export
	GlError,  //!< a GL error where it is checked
	Texture,  //!< a GL texture was created
	Buffer,   //!< a GL buffer was created
	Beat,     //!< a hang watchdog beat
	Exit,     //!< the cause of an exit
	Note,     //!< anything else
	Count     //!< not a kind: the number of kinds
};

//! "frame", "state", "command", "thread", "gl", "texture", "buffer", "beat", "exit", "note"; nullptr for an unknown kind
const char * recordKindName(RecordKind kind) noexcept;

/*!
 * \brief A ring per thread, laid out in memory the recorder does not own
 *
 * \ref record is lock-free, never blocks and never allocates: the calling thread gets its own ring at
 * its first record (or \ref nameThread), keyed by its OS thread id, and keeps the newest
 * \a recordsPerThread records there. A thread that finds no free ring loses its records; they are
 * counted in the serialised text.
 */
class FlightRecorder {

public:

	//! The bytes a recorder of \a maxThreads rings of \a recordsPerThread records needs (header included)
	static size_t memorySize(size_t maxThreads, size_t recordsPerThread) noexcept;

	/*!
	 * \brief Lay a new, empty recorder out in \a memory (\a size >= memorySize()); allocate nothing else
	 *
	 * \a memory must be aligned to at least 8 bytes and stay valid for the recorder's life.
	 */
	FlightRecorder(void * memory, size_t size, size_t maxThreads, size_t recordsPerThread) noexcept;

	FlightRecorder(const FlightRecorder &) = delete;
	FlightRecorder & operator=(const FlightRecorder &) = delete;

	//! One record on the calling thread's ring: lock-free, never blocks, never allocates
	void record(RecordKind kind, std::string_view text, s64 value = 0) noexcept;

	//! The name the serialised header gives the calling thread's ring ("main thread")
	void nameThread(std::string_view name) noexcept;

private:

	void * m_memory;
	size_t m_size;

};

/*!
 * \brief The text of a recorder image: the live memory, a byte copy of it, or bytes read from a dump
 *
 * \code
 * # <free text>                                      comment lines first, then:
 * # thread <tid> [<name>]: <n> events, <k> overwritten   one per ring in use
 * # <d> events dropped ...                           when threads found no free ring
 * <t> <tid> <kind> <value>[ <text>]                  the events, oldest first over all threads
 * \endcode
 *
 * \a t is seconds since the recorder was laid out, 3 decimals; \a tid the OS thread id in decimal;
 * \a kind recordKindName(); \a value a signed decimal; \a text the record's text with every control
 * character shown as a space. Never reads past \a size, never trusts the bytes.
 */
std::string serializeRecorder(const void * image, size_t size);

} // namespace platform

#endif // ARX_PLATFORM_FLIGHTRECORDER_H
