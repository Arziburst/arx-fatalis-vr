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

#include "platform/CrashGateTest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "platform/CrashGate.h"
#include "platform/Platform.h"

CppUnit::AutoRegisterSuite<CrashGateTest> g_registerCrashGateTest;

namespace {

using namespace std::chrono_literals;

using platform::CrashGate;

} // anonymous namespace

void CrashGateTest::testFirstCrashReports() {
	CrashGate gate;
	CPPUNIT_ASSERT_EQUAL(u32(0), gate.reporter());
	CPPUNIT_ASSERT(gate.enter(101) == CrashGate::Entry::First);
	CPPUNIT_ASSERT_EQUAL(u32(101), gate.reporter());
}

void CrashGateTest::testSameThreadAgainEndsAtOnce() {
	CrashGate gate;
	CPPUNIT_ASSERT(gate.enter(101) == CrashGate::Entry::First);
	// A fault inside the handler, on the reporting thread
	CPPUNIT_ASSERT(gate.enter(101) == CrashGate::Entry::Again);
	CPPUNIT_ASSERT_EQUAL(u32(101), gate.reporter());
}

void CrashGateTest::testOtherThreadParks() {
	CrashGate gate;
	CPPUNIT_ASSERT(gate.enter(101) == CrashGate::Entry::First);
	CPPUNIT_ASSERT(gate.enter(202) == CrashGate::Entry::Other);
	CPPUNIT_ASSERT(gate.enter(303) == CrashGate::Entry::Other);
	// The second thread crashing again is still not the reporter
	CPPUNIT_ASSERT(gate.enter(202) == CrashGate::Entry::Other);
	CPPUNIT_ASSERT_EQUAL(u32(101), gate.reporter());
}

void CrashGateTest::testOneReporterAmongThreadsCrashingTogether() {
	for(int round = 0; round < 50; round++) {
		CrashGate gate;
		const int count = 8;
		std::atomic<int> ready { 0 };
		std::atomic<bool> go { false };
		std::vector<CrashGate::Entry> entries(count, CrashGate::Entry::Again);
		std::vector<std::thread> threads;
		for(int i = 0; i < count; i++) {
			threads.emplace_back([&, i] {
				ready++;
				while(!go.load()) {
					std::this_thread::yield();
				}
				entries[size_t(i)] = gate.enter(u32(1000 + i));
			});
		}
		while(ready.load() < count) {
			std::this_thread::yield();
		}
		go = true;
		for(std::thread & thread : threads) {
			thread.join();
		}
		int first = -1;
		int firsts = 0;
		int others = 0;
		for(int i = 0; i < count; i++) {
			if(entries[size_t(i)] == CrashGate::Entry::First) {
				first = i;
				firsts++;
			} else if(entries[size_t(i)] == CrashGate::Entry::Other) {
				others++;
			}
		}
		CPPUNIT_ASSERT_EQUAL(1, firsts);
		CPPUNIT_ASSERT_EQUAL(count - 1, others);
		CPPUNIT_ASSERT_EQUAL(u32(1000 + first), gate.reporter());
	}
}

void CrashGateTest::testParkEnds() {
	// The park of a thread that crashed while another reports ends after its limit (it was Sleep(INFINITE), #554).
	// It runs on its own thread: a park that never ends fails this test instead of hanging the suite
	struct Watch {
		std::mutex mutex;
		std::condition_variable changed;
		bool returned = false;
	};
	auto watch = std::make_shared<Watch>();
	const auto limit = 300ms;
	const auto start = std::chrono::steady_clock::now();
	std::thread parked([watch, limit] {
		CrashGate::park(limit);
		std::scoped_lock lock(watch->mutex);
		watch->returned = true;
		watch->changed.notify_all();
	});
	bool returned = false;
	{
		std::unique_lock lock(watch->mutex);
		returned = watch->changed.wait_for(lock, 10s, [&] { return watch->returned; });
	}
	const auto took = std::chrono::steady_clock::now() - start;
	if(!returned) {
		parked.detach(); // parked for good: the bug; the shared state outlives this test
		CPPUNIT_FAIL("CrashGate::park(300 ms) did not return within 10 s");
	}
	parked.join();
	// Not before its limit (a little slack for the timer's resolution), and not much after it
	CPPUNIT_ASSERT(took >= limit - 20ms);
	CPPUNIT_ASSERT(took < limit + 5s);
}

void CrashGateTest::testParkLimitOutlastsTheReportButIsBounded() {
	// The first handler's report may wait 15 s for the dump helper (requestReport in handleCrash): the park must
	// outlast that, so the second thread's TerminateProcess does not cut the first report short - and stay bounded
	constexpr std::chrono::milliseconds limit = CrashGate::ParkLimit;
	CPPUNIT_ASSERT(limit >= 20s);
	CPPUNIT_ASSERT(limit <= 60s);
}
