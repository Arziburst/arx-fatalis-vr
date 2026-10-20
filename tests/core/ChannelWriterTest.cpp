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

#include "core/ChannelWriterTest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "core/ChannelWriter.h"

#ifdef _WIN32
#include <windows.h>
#endif

CppUnit::AutoRegisterSuite<ChannelWriterTest> g_registerChannelWriterTest;

namespace {

using namespace std::chrono_literals;

struct WriterFolder {

	std::filesystem::path path;

	WriterFolder() {
		path = std::filesystem::temp_directory_path() / ("arx-channel-writer-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
	}

	~WriterFolder() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}

	ChannelWriter::Files files() const {
		return ChannelWriter::Files{ path / "status.json", path / "status.json.tmp", path / "ack.txt" };
	}

};

void writeText(const std::filesystem::path & file, const std::string & text) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out << text;
}

std::string readText(const std::filesystem::path & file) {
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#ifdef _WIN32

/*!
 * A reader that holds a file open for a time, as the test runner or a scanner does with status.json: no sharing, so
 * a rename over the file fails until it lets go
 */
class HoldingReader {

	HANDLE m_handle;
	std::thread m_thread;
	std::atomic<bool> m_release { false };

public:

	HoldingReader(const std::filesystem::path & file, std::chrono::milliseconds holdFor)
		: m_handle(CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
	{
		m_thread = std::thread([this, holdFor]() {
			const auto until = std::chrono::steady_clock::now() + holdFor;
			while(!m_release.load() && std::chrono::steady_clock::now() < until) {
				std::this_thread::sleep_for(1ms);
			}
			if(m_handle != INVALID_HANDLE_VALUE) {
				CloseHandle(m_handle);
				m_handle = INVALID_HANDLE_VALUE;
			}
		});
	}

	bool holds() const { return m_handle != INVALID_HANDLE_VALUE; }

	~HoldingReader() {
		m_release = true;
		if(m_thread.joinable()) {
			m_thread.join();
		}
	}

};

#endif // _WIN32

} // anonymous namespace


void ChannelWriterTest::testStatusThenAck() {
	WriterFolder folder;
	std::atomic<bool> ackBeforeStatus { false };
	const std::filesystem::path ack = folder.path / "ack.txt";
	ChannelWriter writer(folder.files(), [&]() { ackBeforeStatus = ackBeforeStatus || std::filesystem::exists(ack); });
	writer.status("{\"cmdSeq\": 1}\n");
	writer.ack("seq=1\n");
	writer.flush();
	CPPUNIT_ASSERT_EQUAL(std::string("{\"cmdSeq\": 1}\n"), readText(folder.path / "status.json"));
	CPPUNIT_ASSERT_EQUAL(std::string("seq=1\n"), readText(ack));
	CPPUNIT_ASSERT(!std::filesystem::exists(folder.path / "status.json.tmp"));
	CPPUNIT_ASSERT(!ackBeforeStatus);
	CPPUNIT_ASSERT_EQUAL(1ull, writer.statusesWritten());
}

void ChannelWriterTest::testHeldStatusDelaysTheAckNotTheCaller() {
#ifdef _WIN32
	WriterFolder folder;
	writeText(folder.path / "status.json", "old");
	const std::filesystem::path ack = folder.path / "ack.txt";
	std::atomic<bool> ackBeforeStatus { false };
	ChannelWriter writer(folder.files(), [&]() { ackBeforeStatus = ackBeforeStatus || std::filesystem::exists(ack); });
	{
		// The reader lets go after 500 ms: the rename waits for it on the writer thread, not in the caller
		HoldingReader reader(folder.path / "status.json", 500ms);
		CPPUNIT_ASSERT(reader.holds());
		const auto start = std::chrono::steady_clock::now();
		writer.status("new");
		writer.ack("seq=2\n");
		const auto took = std::chrono::steady_clock::now() - start;
		CPPUNIT_ASSERT(took < 250ms);
		std::this_thread::sleep_for(100ms);
		// The status still waits for the reader (which shares the file with nobody, so it is not read here): its ack
		// must not be there yet
		CPPUNIT_ASSERT_EQUAL(0ull, writer.statusesWritten());
		CPPUNIT_ASSERT(!std::filesystem::exists(ack));
	}
	writer.flush();
	CPPUNIT_ASSERT_EQUAL(std::string("new"), readText(folder.path / "status.json"));
	CPPUNIT_ASSERT_EQUAL(std::string("seq=2\n"), readText(ack));
	CPPUNIT_ASSERT(!ackBeforeStatus);
#endif
}

void ChannelWriterTest::testNewestStatusWins() {
#ifdef _WIN32
	WriterFolder folder;
	writeText(folder.path / "status.json", "old");
	ChannelWriter writer(folder.files());
	{
		HoldingReader reader(folder.path / "status.json", 200ms);
		CPPUNIT_ASSERT(reader.holds());
		writer.status("1");
		writer.status("2");
		writer.status("3");
		writer.ack("seq=3\n");
	}
	writer.flush();
	CPPUNIT_ASSERT_EQUAL(std::string("3"), readText(folder.path / "status.json"));
	CPPUNIT_ASSERT_EQUAL(std::string("seq=3\n"), readText(folder.path / "ack.txt"));
	// "1" may have been taken before the others came: at most it and the last one were written
	CPPUNIT_ASSERT(writer.statusesWritten() >= 1 && writer.statusesWritten() <= 2);
#endif
}

void ChannelWriterTest::testPatienceRunsOutAndTheStatusIsTriedAgain() {
#ifdef _WIN32
	WriterFolder folder;
	writeText(folder.path / "status.json", "old");
	std::atomic<int> warnings { 0 };
	ChannelWriter writer(folder.files(), ChannelWriter::Landed(), [&](std::string_view) { warnings++; }, 50ms);
	{
		// Held for six times the patience: the write gives up, warns once, and tries again until it lands
		HoldingReader reader(folder.path / "status.json", 300ms);
		CPPUNIT_ASSERT(reader.holds());
		writer.status("new");
		writer.ack("seq=4\n");
		std::this_thread::sleep_for(150ms);
		CPPUNIT_ASSERT(!std::filesystem::exists(folder.path / "ack.txt"));
	}
	writer.flush();
	CPPUNIT_ASSERT_EQUAL(std::string("new"), readText(folder.path / "status.json"));
	CPPUNIT_ASSERT_EQUAL(std::string("seq=4\n"), readText(folder.path / "ack.txt"));
	CPPUNIT_ASSERT_EQUAL(1, warnings.load());
#endif
}

void ChannelWriterTest::testKeptStatusIsHandedOverByPoll() {
	WriterFolder folder;
	ChannelWriter writer(folder.files());

	// Hold the writer's lock on another thread, as a descheduled writer thread would
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

	// A timer status does not wait for the lock: it is kept
	writer.status("kept", true);

	{
		std::scoped_lock lock(mutex);
		release = true;
		changed.notify_all();
	}
	holder.join();
	CPPUNIT_ASSERT(!timedOut);
	CPPUNIT_ASSERT(!std::filesystem::exists(folder.path / "status.json"));

	// The next frame's poll hands it over (flush would do so itself: the writer thread is waited for without it)
	writer.poll();
	const auto deadline = std::chrono::steady_clock::now() + 10s;
	while(writer.statusesWritten() == 0 && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(1ms);
	}
	CPPUNIT_ASSERT_EQUAL(1ull, writer.statusesWritten());
	CPPUNIT_ASSERT_EQUAL(std::string("kept"), readText(folder.path / "status.json"));
}

void ChannelWriterTest::testStopWritesWhatIsLeft() {
	WriterFolder folder;
	{
		ChannelWriter writer(folder.files());
		writer.status("last");
		writer.ack("seq=5\n");
	}
	CPPUNIT_ASSERT_EQUAL(std::string("last"), readText(folder.path / "status.json"));
	CPPUNIT_ASSERT_EQUAL(std::string("seq=5\n"), readText(folder.path / "ack.txt"));
}
