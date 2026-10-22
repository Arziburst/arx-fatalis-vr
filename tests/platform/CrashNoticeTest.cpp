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

#include "platform/CrashNoticeTest.h"

#include <chrono>
#include <filesystem>
#include <string>

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "platform/CrashNotice.h"

CppUnit::AutoRegisterSuite<CrashNoticeTest> g_registerCrashNoticeTest;

namespace ARX_ANONYMOUS_NAMESPACE {

//! A report folder as the dump helper leaves it, optionally already dismissed, optionally still without its report.txt
void plantCrashReport(const fs::path & crashes, const std::string & name, bool report, bool shown) {
	const fs::path dir = crashes / name;
	CPPUNIT_ASSERT(fs::create_directories(dir));
	CPPUNIT_ASSERT(fs::write(dir / "crash.dmp", "MDMP"));
	if(report) {
		CPPUNIT_ASSERT(fs::write(dir / "report.txt", "kind=crash\nexit=-1073741819 (0xc0000005)\nstack:\n"));
	}
	if(shown) {
		CPPUNIT_ASSERT(fs::write(dir / "notice-shown.txt", "shown\n"));
	}
}

bool noticeShown(const fs::path & crashes, const std::string & name) {
	return fs::is_regular_file(crashes / name / "notice-shown.txt");
}

} ARX_END_ANONYMOUS_NAMESPACE

void CrashNoticeTest::setUp() {
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	m_root = (std::filesystem::temp_directory_path() / ("arx-crash-notice-test-" + std::to_string(stamp))).string();
	CPPUNIT_ASSERT(fs::create_directories(fs::path(m_root)));
}

void CrashNoticeTest::tearDown() {
	fs::remove_all(fs::path(m_root));
}

void CrashNoticeTest::testNoReports() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	// No crashes folder at all: a first launch
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT_EQUAL(size_t(0), platform::markCrashReportsShown(crashes));
	// An empty one
	CPPUNIT_ASSERT(fs::create_directories(crashes));
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	// Only reports that were dismissed, and a folder without its report.txt
	plantCrashReport(crashes, "2026-10-01-10-00-00-00", true, true);
	plantCrashReport(crashes, "2026-10-01-11-00-00-00", false, false);
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT_EQUAL(size_t(0), platform::markCrashReportsShown(crashes));
	CPPUNIT_ASSERT(!noticeShown(crashes, "2026-10-01-11-00-00-00"));
}

void CrashNoticeTest::testNewestUnseenReport() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	plantCrashReport(crashes, "2026-10-01-10-00-00-00", true, false);
	plantCrashReport(crashes, "2026-10-01-11-00-00-00", true, false);
	// Newer, but dismissed already
	plantCrashReport(crashes, "2026-10-01-12-00-00-00", true, true);
	// Newest, but no report.txt yet
	plantCrashReport(crashes, "2026-10-01-13-00-00-00", false, false);
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-01-11-00-00-00"), platform::findUnseenCrashReport(crashes));
	// Looking does not dismiss: the notice stays until OK
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-01-11-00-00-00"), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT(!noticeShown(crashes, "2026-10-01-11-00-00-00"));
}

void CrashNoticeTest::testOkMarksEveryUnseenReport() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	plantCrashReport(crashes, "2026-10-01-10-00-00-00", true, false);
	plantCrashReport(crashes, "2026-10-01-11-00-00-00", true, false);
	plantCrashReport(crashes, "2026-10-01-12-00-00-00", true, true);
	plantCrashReport(crashes, "2026-10-01-13-00-00-00", false, false);

	CPPUNIT_ASSERT_EQUAL(size_t(2), platform::markCrashReportsShown(crashes));
	// The marker is a file in each report folder it was about
	CPPUNIT_ASSERT(noticeShown(crashes, "2026-10-01-10-00-00-00"));
	CPPUNIT_ASSERT(noticeShown(crashes, "2026-10-01-11-00-00-00"));
	CPPUNIT_ASSERT(noticeShown(crashes, "2026-10-01-12-00-00-00"));
	CPPUNIT_ASSERT(!noticeShown(crashes, "2026-10-01-13-00-00-00"));
	// Never again: not at the next launch, not after a second OK
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT_EQUAL(size_t(0), platform::markCrashReportsShown(crashes));
	// The reports themselves are kept
	CPPUNIT_ASSERT(fs::is_regular_file(crashes / "2026-10-01-11-00-00-00" / "report.txt"));
	CPPUNIT_ASSERT(fs::is_regular_file(crashes / "2026-10-01-11-00-00-00" / "crash.dmp"));
}

void CrashNoticeTest::testNewReportAfterOk() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	plantCrashReport(crashes, "2026-10-01-10-00-00-00", true, false);
	CPPUNIT_ASSERT_EQUAL(size_t(1), platform::markCrashReportsShown(crashes));
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	// The next crash gets its own notice
	plantCrashReport(crashes, "2026-10-02-09-00-00-00", true, false);
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-02-09-00-00-00"), platform::findUnseenCrashReport(crashes));
	// ... and so does the report a helper finishes later (report.txt written after the folder)
	plantCrashReport(crashes, "2026-10-03-09-00-00-00", false, false);
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-02-09-00-00-00"), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT(fs::write(crashes / "2026-10-03-09-00-00-00" / "report.txt", "kind=abort\n"));
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-03-09-00-00-00"), platform::findUnseenCrashReport(crashes));
}
