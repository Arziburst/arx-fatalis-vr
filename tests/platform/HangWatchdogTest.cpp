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

#include "platform/HangWatchdogTest.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/Architecture.h"
#include "platform/HangWatchdog.h"
#include "platform/Platform.h"

CppUnit::AutoRegisterSuite<HangWatchdogTest> g_registerHangWatchdogTest;

namespace {

struct HangReports {

	std::mutex mutex;
	std::vector<std::string> texts;

	platform::HangWatchdog::Reporter reporter() {
		return [this](std::string_view text) {
			std::lock_guard<std::mutex> lock(mutex);
			texts.emplace_back(text);
		};
	}

	std::vector<std::string> get() {
		std::lock_guard<std::mutex> lock(mutex);
		return texts;
	}

};

bool hangReportHas(const std::string & text, const std::string & part) {
	return text.find(part) != std::string::npos;
}

} // anonymous namespace

void HangWatchdogTest::testStallIsReportedWithStack() {

	HangReports reports;
	platform::HangWatchdog watchdog("test thread", std::chrono::milliseconds(200), reports.reporter());
	// The symbols are read first: beat until the watchdog checks the beats
	for(int i = 0; i < 1000 && !watchdog.watching(); i++) {
		watchdog.beat("set-up");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	CPPUNIT_ASSERT(watchdog.watching());
	watchdog.beat("before the stall");

	// Reported at 200, 400 and 600 ms, then no more for this stall
	platform::stallThisThread(std::chrono::milliseconds(1000));
	watchdog.beat("after the stall");

	std::vector<std::string> texts;
	for(int i = 0; i < 100; i++) {
		texts = reports.get();
		if(texts.size() >= size_t(platform::HangWatchdog::MaxReportsPerStall + 1)) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	CPPUNIT_ASSERT_EQUAL(size_t(platform::HangWatchdog::MaxReportsPerStall + 1), texts.size());
	CPPUNIT_ASSERT_EQUAL(u64(1), watchdog.stalls());

	const std::string & first = texts[0];
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "test thread stalled for "));
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "(report 1 of at most 3)"));
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "last beat at before the stall\n"));
	CPPUNIT_ASSERT_MESSAGE(texts[2], hangReportHas(texts[2], "(report 3 of at most 3)"));

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32 && ARX_ARCH == ARX_ARCH_X86_64
	// The stack the thread sleeps in: the sleeping function by name, under it this test
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "  #0 "));
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "arxtest.exe+0x"));
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "platform::stallThisThread"));
	CPPUNIT_ASSERT_MESSAGE(first, hangReportHas(first, "HangWatchdogTest::testStallIsReportedWithStack"));
	#endif

	const std::string & last = texts.back();
	CPPUNIT_ASSERT_MESSAGE(last, hangReportHas(last, "test thread responsive again after "));

}

void HangWatchdogTest::testBeatingThreadIsQuiet() {

	HangReports reports;
	platform::HangWatchdog watchdog("test thread", std::chrono::milliseconds(1000), reports.reporter());

	for(int i = 0; i < 40; i++) {
		watchdog.beat("loop");
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	CPPUNIT_ASSERT_EQUAL(size_t(0), reports.get().size());
	CPPUNIT_ASSERT_EQUAL(u64(0), watchdog.stalls());

}
