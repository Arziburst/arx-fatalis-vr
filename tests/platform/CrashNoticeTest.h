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

#ifndef ARX_TESTS_PLATFORM_CRASHNOTICETEST_H
#define ARX_TESTS_PLATFORM_CRASHNOTICETEST_H

#include <string>

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The next-launch crash notice (#545, M6): with the dump helper on, a crash shows no "Fatal Error" dialog; at the next
 * launch the main menu (flat and on the VR panel) says "The game ended with an error last time. A report was saved."
 * with [Open folder] and [OK] - once per new report, never again after OK.
 *
 * Which reports are new - the interface these tests expect, src/platform/CrashNotice.h and src/platform/CrashNotice.cpp
 * (add the .cpp to arxtest_SOURCES in tests/CMakeLists.txt):
 *
 *   namespace platform {
 *
 *   //! The file a report folder gets when its notice was dismissed with OK: <user dir>\crashes\<name>\notice-shown.txt
 *   extern const char * const CrashNoticeMarker; // "notice-shown.txt"
 *
 *   //! The name of the newest (by folder name) report folder in \a crashesDir that has a report.txt and no marker;
 *   //! an empty string when there is none or the folder does not exist. A folder without report.txt (one the dump
 *   //! helper is still writing, an old crash.txt-only report) is no report. hangs\ is never looked at: the game ran on.
 *   std::string findUnseenCrashReport(const fs::path & crashesDir);
 *
 *   //! OK: every report folder findUnseenCrashReport() would consider gets the marker; returns how many got it
 *   size_t markCrashReportsShown(const fs::path & crashesDir);
 *
 *   }
 *
 * The game (scripts/dev/crash-notice-check.ps1 checks it): at the main menu, status.json "crashNotice": { "state":
 * "shown" | "none", "report": "<folder name>" | "" } names the report the notice is about; the test channel's
 * "crashnotice ok" presses OK (it calls markCrashReportsShown), "crashnotice open" presses Open folder.
 */
class CrashNoticeTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(CrashNoticeTest);
	CPPUNIT_TEST(testNoReports);
	CPPUNIT_TEST(testNewestUnseenReport);
	CPPUNIT_TEST(testOkMarksEveryUnseenReport);
	CPPUNIT_TEST(testNewReportAfterOk);
	CPPUNIT_TEST_SUITE_END();

public:

	void setUp() override;
	void tearDown() override;

	void testNoReports();
	void testNewestUnseenReport();
	void testOkMarksEveryUnseenReport();
	void testNewReportAfterOk();

private:

	std::string m_root;

};

#endif // ARX_TESTS_PLATFORM_CRASHNOTICETEST_H
