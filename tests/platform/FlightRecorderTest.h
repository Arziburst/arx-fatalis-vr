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

#ifndef ARX_TESTS_PLATFORM_FLIGHTRECORDERTEST_H
#define ARX_TESTS_PLATFORM_FLIGHTRECORDERTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The flight recorder's rings (#545): per-thread lock-free rings of fixed records in a block of memory the
 * recorder does not own (the game puts it in the crash handler's shared memory, so the dump helper reads it
 * after the game is gone), and the text they serialise to (arx-recorder.txt, the dump's user stream).
 *
 * The interface these tests expect - src/platform/FlightRecorder.h, src/platform/FlightRecorder.cpp (add the
 * .cpp to arxtest_SOURCES in tests/CMakeLists.txt):
 *
 *   namespace platform {
 *
 *   enum class RecordKind : u8 { Frame, State, Command, Thread, GlError, Texture, Buffer, Beat, Exit, Note };
 *
 *   //! "frame", "state", "command", "thread", "gl", "texture", "buffer", "beat", "exit", "note"
 *   const char * recordKindName(RecordKind kind);
 *
 *   class FlightRecorder {
 *   public:
 *     //! The bytes a recorder of \a maxThreads rings of \a recordsPerThread records needs (header included)
 *     static size_t memorySize(size_t maxThreads, size_t recordsPerThread);
 *     //! Lays a new, empty recorder out in \a memory (\a size >= memorySize()); allocates nothing else
 *     FlightRecorder(void * memory, size_t size, size_t maxThreads, size_t recordsPerThread);
 *     //! One record on the calling thread's ring: lock-free, never blocks, never allocates
 *     void record(RecordKind kind, std::string_view text, s64 value = 0) noexcept;
 *     //! The name the serialised header gives the calling thread's ring ("main thread")
 *     void nameThread(std::string_view name) noexcept;
 *   };
 *
 *   //! The text of a recorder image: the live memory, a byte copy of it, or bytes read from another process or a dump
 *   std::string serializeRecorder(const void * image, size_t size);
 *
 *   }
 *
 * The behaviour:
 *   - a thread gets its own ring at its first record, keyed by its OS thread id; a ring keeps exactly the newest
 *     \a recordsPerThread records; a thread that finds no free ring loses its records, and they are counted
 *   - several recorders may exist one after another, or at once, in one process (each test makes its own): a
 *     thread's records go to the recorder it calls, never to another one's memory
 *   - a record keeps at least 32 characters of its text; longer text is cut and never spills into another record
 *   - the image holds no pointers: a byte copy at another address serialises to the same text; the text depends on
 *     the image's bytes only (no clock, no address), so two serialisations of the same bytes are equal
 *   - the memory given is aligned to at least 16 bytes (operator new, a mapped view); a copy read back may be at an
 *     address aligned to 8 only
 *   - serialising never trusts the image: zeros, garbage, a cut-off copy or a concurrent writer give fewer lines,
 *     never a crash, a read outside the image, or a torn record (text of one write, value of another)
 *
 * The text, one line each:
 *   # <free text>                                     comment lines first (any number of them), then:
 *   # thread <tid> [<name>]: <n> events, <k> overwritten   one per ring in use (<n> recorded in all, <k> of them gone)
 *   # <d> events dropped ...                          when threads found no free ring (<d> records lost)
 *   <t> <tid> <kind> <value>[ <text>]                 the events, oldest first over all threads (one thread's events
 *                                                     keep their order); t = seconds since the recorder was laid out
 *                                                     with exactly 3 decimals, tid = the OS thread id in decimal,
 *                                                     kind = recordKindName(), value = signed decimal, text = the
 *                                                     record's text with every control character printed as a space;
 *                                                     a record of an unknown kind is skipped
 *
 * scripts/dev/crash-report.ps1 prints the last 100 of these lines (scripts/dev/test-crash-report.ps1).
 */
class FlightRecorderTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(FlightRecorderTest);
	CPPUNIT_TEST(testKindsAndLineFormat);
	CPPUNIT_TEST(testLongTextIsCut);
	CPPUNIT_TEST(testWrapKeepsTheNewest);
	CPPUNIT_TEST(testTwoProducers);
	CPPUNIT_TEST(testSerialiseWhileWriting);
	CPPUNIT_TEST(testCopyOfTheImage);
	CPPUNIT_TEST(testBrokenImages);
	CPPUNIT_TEST(testThreadWithoutARing);
	CPPUNIT_TEST(testSeveralRecorders);
	CPPUNIT_TEST_SUITE_END();

public:

	void testKindsAndLineFormat();
	void testLongTextIsCut();
	void testWrapKeepsTheNewest();
	void testTwoProducers();
	void testSerialiseWhileWriting();
	void testCopyOfTheImage();
	void testBrokenImages();
	void testThreadWithoutARing();
	void testSeveralRecorders();

};

#endif // ARX_TESTS_PLATFORM_FLIGHTRECORDERTEST_H
