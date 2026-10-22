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

#include "platform/CrashNoticeKindTest.h"

#include <chrono>
#include <filesystem>
#include <string>

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "platform/CrashNotice.h"

CppUnit::AutoRegisterSuite<CrashNoticeKindTest> g_registerCrashNoticeKindTest;

namespace ARX_ANONYMOUS_NAMESPACE {

void plantReportOfKind(const fs::path & crashes, const std::string & name, const std::string & kind) {
	const fs::path dir = crashes / name;
	CPPUNIT_ASSERT(fs::create_directories(dir));
	CPPUNIT_ASSERT(fs::write(dir / "report.txt", "kind=" + kind + "\nexit=-1 (0xffffffff)\nkiller=x\nmodule=none\ndump=none\nstack:\n"));
}

} ARX_END_ANONYMOUS_NAMESPACE

void CrashNoticeKindTest::setUp() {
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	m_root = (std::filesystem::temp_directory_path() / ("arx-crash-notice-kind-test-" + std::to_string(stamp))).string();
	CPPUNIT_ASSERT(fs::create_directories(fs::path(m_root)));
}

void CrashNoticeKindTest::tearDown() {
	fs::remove_all(fs::path(m_root));
}

void CrashNoticeKindTest::testTerminatedIsNotAnnounced() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	// The newest report is a kill from outside: the one before it is announced instead
	plantReportOfKind(crashes, "2026-10-01-10-00-00-00", "crash");
	plantReportOfKind(crashes, "2026-10-01-11-00-00-00", "terminated");
	CPPUNIT_ASSERT_EQUAL(std::string("2026-10-01-10-00-00-00"), platform::findUnseenCrashReport(crashes));
	// OK marks the crash, not the kill (it was never announced, and need not be)
	CPPUNIT_ASSERT_EQUAL(size_t(1), platform::markCrashReportsShown(crashes));
	CPPUNIT_ASSERT(fs::is_regular_file(crashes / "2026-10-01-10-00-00-00" / platform::CrashNoticeMarker));
	CPPUNIT_ASSERT(!fs::exists(crashes / "2026-10-01-11-00-00-00" / platform::CrashNoticeMarker));
	// Only kills left: nothing to announce
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	plantReportOfKind(crashes, "2026-10-01-12-00-00-00", "terminated");
	CPPUNIT_ASSERT_EQUAL(std::string(), platform::findUnseenCrashReport(crashes));
	CPPUNIT_ASSERT_EQUAL(size_t(0), platform::markCrashReportsShown(crashes));
}

void CrashNoticeKindTest::testEveryOtherKindIs() {
	const fs::path crashes = fs::path(m_root) / "crashes";
	const char * kinds[] = { "crash", "abort", "terminate", "exit from outside", "exit (in process)", "hang" };
	int hour = 10;
	for(const char * kind : kinds) {
		const std::string name = "2026-10-02-" + std::to_string(hour++) + "-00-00-00";
		plantReportOfKind(crashes, name, kind);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(kind, name, platform::findUnseenCrashReport(crashes));
		CPPUNIT_ASSERT_EQUAL_MESSAGE(kind, size_t(1), platform::markCrashReportsShown(crashes));
	}
}
