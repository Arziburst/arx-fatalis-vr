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

#include "vr/PerfWriterTest.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "vr/PerfWriter.h"

CppUnit::AutoRegisterSuite<PerfWriterTest> g_registerPerfWriterTest;

namespace {

/*!
 * A sink that collects what it is given and, when \c holdFirst, stops in its first batch until the
 * test releases it - a disk that stalls (or gives up after 10 s, which fails the test)
 */
struct Gate {

	std::mutex mutex;
	std::condition_variable changed;
	std::string received;
	bool holdFirst = false;
	bool entered = false;
	bool released = false;
	bool timedOut = false;

	PerfWriter::Sink sink() {
		return [this](std::string_view batch) {
			std::unique_lock lock(mutex);
			received.append(batch);
			if(holdFirst && !entered) {
				entered = true;
				changed.notify_all();
				timedOut = !changed.wait_for(lock, std::chrono::seconds(10), [this] { return released; });
			}
		};
	}

	bool waitEntered() {
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, std::chrono::seconds(10), [this] { return entered; });
	}

	void release() {
		std::scoped_lock lock(mutex);
		released = true;
		changed.notify_all();
	}

	std::string text() {
		std::scoped_lock lock(mutex);
		return received;
	}

};

std::string numbered(int i) {
	return "line " + std::to_string(i);
}

std::string expectedLines(int from, int to) {
	std::string text;
	for(int i = from; i < to; i++) {
		text += numbered(i) + '\n';
	}
	return text;
}

} // anonymous namespace

void PerfWriterTest::testLinesReachTheSinkInOrder() {

	Gate gate;
	PerfWriter writer(gate.sink(), std::chrono::milliseconds(1));

	for(int i = 0; i < 1000; i++) {
		writer.push(numbered(i));
	}
	writer.flush();

	CPPUNIT_ASSERT_EQUAL(expectedLines(0, 1000), gate.text());
	CPPUNIT_ASSERT_EQUAL(size_t(0), writer.dropped());
}

void PerfWriterTest::testPushNeverWaitsForTheSink() {

	Gate gate;
	gate.holdFirst = true;
	PerfWriter writer(gate.sink(), std::chrono::milliseconds(1));

	writer.push(numbered(0));
	CPPUNIT_ASSERT(gate.waitEntered());

	// The sink is stuck in its first batch: the frame thread's pushes must still go through, and
	// cheaply - a 90 Hz frame is 11.1 ms, 1 % of it 111 us
	const int count = 10000;
	auto start = std::chrono::steady_clock::now();
	for(int i = 1; i <= count; i++) {
		writer.push(numbered(i));
	}
	auto took = std::chrono::steady_clock::now() - start;
	double meanUs = std::chrono::duration<double, std::micro>(took).count() / count;

	gate.release();
	writer.flush();

	// Had a push waited for the sink, the sink would have given up waiting for the release
	CPPUNIT_ASSERT(!gate.timedOut);
	CPPUNIT_ASSERT(meanUs < 20.0);
	CPPUNIT_ASSERT_EQUAL(expectedLines(0, count + 1), gate.text());
}

void PerfWriterTest::testPushNeverWaitsForTheLock() {

	Gate gate;
	PerfWriter writer(gate.sink(), std::chrono::milliseconds(1));
	writer.push(numbered(0));

	// Another thread holds the writer's lock - a writer thread descheduled while swapping buffers
	std::mutex mutex;
	std::condition_variable changed;
	bool holding = false;
	bool release = false;
	bool timedOut = false;
	std::thread holder([&] {
		std::unique_lock<std::mutex> held = writer.holdLockForTest();
		std::unique_lock lock(mutex);
		holding = true;
		changed.notify_all();
		timedOut = !changed.wait_for(lock, std::chrono::seconds(10), [&] { return release; });
	});
	{
		std::unique_lock lock(mutex);
		CPPUNIT_ASSERT(changed.wait_for(lock, std::chrono::seconds(10), [&] { return holding; }));
	}

	for(int i = 1; i <= 100; i++) {
		writer.push(numbered(i));
	}

	{
		std::scoped_lock lock(mutex);
		release = true;
		changed.notify_all();
	}
	holder.join();
	CPPUNIT_ASSERT(!timedOut);

	// The lines kept meanwhile are handed over in order, with the next push or the flush
	writer.push(numbered(101));
	writer.flush();
	CPPUNIT_ASSERT_EQUAL(expectedLines(0, 102), gate.text());
	CPPUNIT_ASSERT_EQUAL(size_t(0), writer.dropped());
}

void PerfWriterTest::testDropsWhenTheSinkFallsBehind() {

	Gate gate;
	gate.holdFirst = true;
	// Room for 10 lines of "line N\n" (7 bytes) while the sink is stuck
	PerfWriter writer(gate.sink(), std::chrono::milliseconds(1), 70);

	writer.push(numbered(0));
	CPPUNIT_ASSERT(gate.waitEntered());

	for(int i = 1; i <= 50; i++) {
		writer.push(numbered(i));
	}
	CPPUNIT_ASSERT(writer.dropped() >= 40);

	gate.release();
	writer.flush();
	CPPUNIT_ASSERT(!gate.timedOut);

	// The first lines are kept whole and in order, the rest dropped
	CPPUNIT_ASSERT_EQUAL(expectedLines(0, 51 - int(writer.dropped())), gate.text());

	// Room again once the sink caught up
	writer.push("after");
	writer.flush();
	CPPUNIT_ASSERT(gate.text().size() >= 6 && gate.text().compare(gate.text().size() - 6, 6, "after\n") == 0);
}

void PerfWriterTest::testDestructorWritesTheRest() {

	Gate gate;
	{
		// The writer thread would not wake on its own for an hour
		PerfWriter writer(gate.sink(), std::chrono::hours(1));
		for(int i = 0; i < 100; i++) {
			writer.push(numbered(i));
		}
	}
	CPPUNIT_ASSERT_EQUAL(expectedLines(0, 100), gate.text());
}
