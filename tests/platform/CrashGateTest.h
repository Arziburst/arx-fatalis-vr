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

#ifndef ARX_TESTS_PLATFORM_CRASHGATETEST_H
#define ARX_TESTS_PLATFORM_CRASHGATETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Who reports a crash, and how long a second crashing thread waits (#545, #554): the first crashing thread owns the
 * report; the same thread crashing again (a fault inside the handler) ends the process at once; another thread parks
 * for a bounded time - long enough for the first report to finish, never forever (it may hold a lock the first
 * handler needs: the heap lock while the first thread symbolises) - and then ends the process.
 *
 * The interface these tests are written against (written before the code, contract #561/#554). The worker implements
 * it as written and CrashHandlerWindows::handleCrash uses it in place of its s_handlingThread and Sleep(INFINITE):
 * First -> report as today; Again -> TerminateProcess at once; Other -> CrashGate::park(CrashGate::ParkLimit), then
 * TerminateProcess. A .cpp, if it has one, goes into arxtest_SOURCES (tests/CMakeLists.txt).
 *
 * \code
 * // src/platform/CrashGate.h
 * namespace platform {
 * class CrashGate { // default-constructible; the crash handler keeps one static instance
 * public:
 *     enum class Entry {
 *         First, //!< this thread reports the crash
 *         Again, //!< the reporting thread crashed again (inside the handler): end the process now
 *         Other, //!< another thread reports: park, then end the process
 *     };
 *     //! How long another crashing thread parks: longer than the first report takes (the dump helper's request
 *     //! waits up to 15 s), short enough that a deadlocked report does not keep a dead game around
 *     static constexpr std::chrono::milliseconds ParkLimit = std::chrono::seconds(30);
 *
 *     //! Lock-free and allocation-free: safe in a crash handler. \a threadId is never 0
 *     Entry enter(u32 threadId) noexcept;
 *     //! The thread that reports; 0 before any crash
 *     u32 reporter() const noexcept;
 *     //! Parks the calling thread for \a limit, then returns (no heap, no lock taken)
 *     static void park(std::chrono::milliseconds limit) noexcept;
 * };
 * }
 * \endcode
 *
 * The first handler's report itself (one crashes\ folder, report.txt) is checked by the existing
 * scripts/dev/flight-recorder-check.ps1 cases (crash, abort, terminate-thread).
 */
class CrashGateTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(CrashGateTest);
	CPPUNIT_TEST(testFirstCrashReports);
	CPPUNIT_TEST(testSameThreadAgainEndsAtOnce);
	CPPUNIT_TEST(testOtherThreadParks);
	CPPUNIT_TEST(testOneReporterAmongThreadsCrashingTogether);
	CPPUNIT_TEST(testParkEnds);
	CPPUNIT_TEST(testParkLimitOutlastsTheReportButIsBounded);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFirstCrashReports();
	void testSameThreadAgainEndsAtOnce();
	void testOtherThreadParks();
	void testOneReporterAmongThreadsCrashingTogether();
	void testParkEnds();
	void testParkLimitOutlastsTheReportButIsBounded();

};

#endif // ARX_TESTS_PLATFORM_CRASHGATETEST_H
