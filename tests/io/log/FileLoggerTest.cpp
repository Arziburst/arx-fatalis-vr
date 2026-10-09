/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Libertatis.
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

#include "io/log/FileLoggerTest.h"

#include <chrono>
#include <thread>

#include "io/fs/FileStream.h"
#include "io/fs/Filesystem.h"
#include "io/log/FileLogger.h"
#include "io/log/LogBackend.h"

#include "platform/Platform.h"

CppUnit::AutoRegisterSuite<FileLoggerTest> g_registerFileLoggerTest;

namespace {

const logger::Source g_source = { "tests/io/log/FileLoggerTest.cpp", "FileLoggerTest", Logger::Debug };

const std::ios_base::openmode g_truncate = std::ios_base::out | std::ios_base::trunc;

} // anonymous namespace

void FileLoggerTest::setUp() {
	m_root = "filelogger-test-tmp";
	fs::remove_all(m_root);
	CPPUNIT_ASSERT(fs::create_directories(m_root));
}

void FileLoggerTest::tearDown() {
	fs::remove_all(m_root);
}

fs::path FileLoggerTest::numbered(size_t n) const {
	fs::path p = m_root / "arx.log";
	return p.set_basename("arx." + std::to_string(n));
}

std::string FileLoggerTest::contents(const fs::path & file) const {
	return fs::is_regular_file(file) ? fs::read(file) : std::string();
}

long FileLoggerTest::waitForText(const fs::path & file, const std::string & text, long timeoutMs) const {
	auto start = std::chrono::steady_clock::now();
	while(true) {
		auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
		if(contents(file).find(text) != std::string::npos) {
			return long(waited.count());
		}
		if(waited.count() >= timeoutMs) {
			return -1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void FileLoggerTest::testWarningIsOnDiskAtOnce() {

	fs::path file = m_root / "arx.log";

	// A flush interval far longer than the test: only the warning / error path can put a line on disk
	logger::File log(file, g_truncate, std::chrono::seconds(60));

	log.log(g_source, 1, Logger::Info, "info before the warning");
	log.log(g_source, 2, Logger::Warning, "the warning");
	std::string text = contents(file);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("info before the warning") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("[W] FileLoggerTest:2") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("the warning") != std::string::npos);

	log.log(g_source, 3, Logger::Info, "info before the error");
	log.log(g_source, 4, Logger::Error, "the error");
	text = contents(file);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("info before the error") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("[E] FileLoggerTest:4") != std::string::npos);

	log.log(g_source, 5, Logger::Critical, "the critical error");
	text = contents(file);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("[CRITICAL] FileLoggerTest:5") != std::string::npos);

}

void FileLoggerTest::testInfoIsOnDiskWithinTheInterval() {

	fs::path file = m_root / "arx.log";

	logger::File log(file, g_truncate, std::chrono::milliseconds(50));
	log.log(g_source, 1, Logger::Info, "an info line nobody flushes");

	// Nothing else is logged: only the flush thread can put the line on disk
	long ms = waitForText(file, "an info line nobody flushes", 2000);
	CPPUNIT_ASSERT_MESSAGE("the info line never reached the disk", ms >= 0);
	CPPUNIT_ASSERT_MESSAGE("the info line took " + std::to_string(ms) + " ms to reach the disk", ms < 1000);

}

void FileLoggerTest::testExplicitFlush() {

	fs::path file = m_root / "arx.log";

	logger::File log(file, g_truncate, std::chrono::seconds(60));
	log.log(g_source, 1, Logger::Info, "an info line flushed by hand");
	log.flush();

	std::string text = contents(file);
	CPPUNIT_ASSERT_MESSAGE(text, text.find("an info line flushed by hand") != std::string::npos);

}

void FileLoggerTest::testRotationShiftsEveryRun() {

	fs::path file = m_root / "arx.log";

	CPPUNIT_ASSERT(fs::write(file, "run 0"));
	for(size_t n = 1; n <= 9; n++) {
		CPPUNIT_ASSERT(fs::write(numbered(n), "run " + std::to_string(n)));
	}

	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 10));

	CPPUNIT_ASSERT(!fs::exists(file));
	for(size_t n = 1; n <= 9; n++) {
		CPPUNIT_ASSERT_EQUAL_MESSAGE(numbered(n).string(), "run " + std::to_string(n - 1), contents(numbered(n)));
	}
	// The oldest run is dropped, not pushed beyond the history
	CPPUNIT_ASSERT(!fs::exists(numbered(10)));

	// The next run shifts everything once more
	CPPUNIT_ASSERT(fs::write(file, "run 10"));
	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 10));
	CPPUNIT_ASSERT(!fs::exists(file));
	CPPUNIT_ASSERT_EQUAL(std::string("run 10"), contents(numbered(1)));
	CPPUNIT_ASSERT_EQUAL(std::string("run 0"), contents(numbered(2)));
	CPPUNIT_ASSERT_EQUAL(std::string("run 7"), contents(numbered(9)));
	CPPUNIT_ASSERT(!fs::exists(numbered(10)));

}

void FileLoggerTest::testRotationWithoutHistory() {

	fs::path file = m_root / "arx.log";

	// Nothing at all: nothing to do, nothing created
	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 10));
	CPPUNIT_ASSERT(!fs::exists(file));
	CPPUNIT_ASSERT(!fs::exists(numbered(1)));

	// Old runs but no current log (the previous run never wrote one): the history stays as it is
	CPPUNIT_ASSERT(fs::write(numbered(1), "previous"));
	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 10));
	CPPUNIT_ASSERT_EQUAL(std::string("previous"), contents(numbered(1)));
	CPPUNIT_ASSERT(!fs::exists(numbered(2)));

	// A history of one keeps nothing
	CPPUNIT_ASSERT(fs::write(file, "current"));
	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 1));
	CPPUNIT_ASSERT_EQUAL(std::string("current"), contents(file));
	CPPUNIT_ASSERT_EQUAL(std::string("previous"), contents(numbered(1)));

	// A gap in the numbering is shifted like everything else
	CPPUNIT_ASSERT(fs::remove(numbered(1)));
	CPPUNIT_ASSERT(fs::write(numbered(3), "much older"));
	CPPUNIT_ASSERT_EQUAL(std::string(), logger::rotateLogs(file, 10));
	CPPUNIT_ASSERT(!fs::exists(file));
	CPPUNIT_ASSERT_EQUAL(std::string("current"), contents(numbered(1)));
	CPPUNIT_ASSERT(!fs::exists(numbered(2)));
	CPPUNIT_ASSERT(!fs::exists(numbered(3)));
	CPPUNIT_ASSERT_EQUAL(std::string("much older"), contents(numbered(4)));

}

void FileLoggerTest::testRotationStopsAtAFileItCannotMove() {

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	fs::path file = m_root / "arx.log";

	CPPUNIT_ASSERT(fs::write(file, "current"));
	CPPUNIT_ASSERT(fs::write(numbered(1), "previous"));
	CPPUNIT_ASSERT(fs::write(numbered(2), "older"));

	std::string error;
	{
		// The C runtime opens without FILE_SHARE_DELETE, so the file cannot be moved while this
		// stream is open - the same lock an editor or another instance holding the log would cause
		fs::ofstream hold(numbered(1), std::ios_base::out | std::ios_base::app);
		CPPUNIT_ASSERT(hold.is_open());
		error = logger::rotateLogs(file, 10);
	}

	CPPUNIT_ASSERT_MESSAGE("a file that cannot be moved must be reported", !error.empty());
	CPPUNIT_ASSERT_MESSAGE(error, error.find("arx.1.log") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(error, error.find("arx.2.log") != std::string::npos);

	// Nothing truncated or deleted: the current log and the locked file keep their contents
	CPPUNIT_ASSERT_EQUAL(std::string("current"), contents(file));
	CPPUNIT_ASSERT_EQUAL(std::string("previous"), contents(numbered(1)));
	// The runs behind the locked file were already shifted when the rotation stopped
	CPPUNIT_ASSERT(!fs::exists(numbered(2)));
	CPPUNIT_ASSERT_EQUAL(std::string("older"), contents(numbered(3)));

	#endif

}
