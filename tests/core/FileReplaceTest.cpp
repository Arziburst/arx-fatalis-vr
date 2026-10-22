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

#include "core/FileReplaceTest.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "core/FileReplace.h"

#ifdef _WIN32
#include <windows.h>
#endif

CppUnit::AutoRegisterSuite<FileReplaceTest> g_registerFileReplaceTest;

namespace {

using namespace std::chrono_literals;

struct TempFolder {

	std::filesystem::path path;

	TempFolder() {
		path = std::filesystem::temp_directory_path() / ("arx-file-replace-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
	}

	~TempFolder() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}

};

void writeFile(const std::filesystem::path & file, const std::string & text) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out << text;
}

std::string readFile(const std::filesystem::path & file) {
	std::ifstream in(file, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

#ifdef _WIN32

/*!
 * A reader that holds a file open for a time, as the test runner does while it reads status.json: no sharing, so a
 * rename over the file fails like it does for the game
 */
class Reader {

	HANDLE m_handle;
	std::thread m_thread;
	std::atomic<bool> m_release { false };

public:

	Reader(const std::filesystem::path & file, std::chrono::milliseconds holdFor, bool untilReleased = false)
		: m_handle(CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
	{
		m_thread = std::thread([this, holdFor, untilReleased]() {
			const auto until = std::chrono::steady_clock::now() + holdFor;
			while(untilReleased ? !m_release.load() : std::chrono::steady_clock::now() < until) {
				std::this_thread::sleep_for(1ms);
			}
			if(m_handle != INVALID_HANDLE_VALUE) {
				CloseHandle(m_handle);
				m_handle = INVALID_HANDLE_VALUE;
			}
		});
	}

	bool holds() const { return m_handle != INVALID_HANDLE_VALUE; }

	void release() {
		m_release = true;
		if(m_thread.joinable()) {
			m_thread.join();
		}
	}

	~Reader() {
		release();
	}

};

#endif // _WIN32

} // anonymous namespace

void FileReplaceTest::testFreeTargetIsReplaced() {
	TempFolder folder;
	writeFile(folder.path / "status.json.tmp", "new");
	writeFile(folder.path / "status.json", "old");
	CPPUNIT_ASSERT(core::replaceFile(folder.path / "status.json.tmp", folder.path / "status.json", core::ReplaceMode::Once));
	CPPUNIT_ASSERT_EQUAL(std::string("new"), readFile(folder.path / "status.json"));
	CPPUNIT_ASSERT(!std::filesystem::exists(folder.path / "status.json.tmp"));
}

void FileReplaceTest::testMissingTargetIsCreated() {
	TempFolder folder;
	writeFile(folder.path / "status.json.tmp", "first");
	CPPUNIT_ASSERT(core::replaceFile(folder.path / "status.json.tmp", folder.path / "status.json", core::ReplaceMode::Patient));
	CPPUNIT_ASSERT_EQUAL(std::string("first"), readFile(folder.path / "status.json"));
}

void FileReplaceTest::testHeldTargetDelaysAPatientWriteButNeverLosesIt() {
#ifdef _WIN32
	TempFolder folder;
	writeFile(folder.path / "status.json.tmp", "new");
	writeFile(folder.path / "status.json", "old");
	const auto start = std::chrono::steady_clock::now();
	{
		Reader reader(folder.path / "status.json", 150ms);
		CPPUNIT_ASSERT(reader.holds());
		// The reader lets go after 150 ms: the write waits for it, and lands
		CPPUNIT_ASSERT(core::replaceFile(folder.path / "status.json.tmp", folder.path / "status.json", core::ReplaceMode::Patient, 2000ms));
	}
	const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	CPPUNIT_ASSERT_EQUAL(std::string("new"), readFile(folder.path / "status.json"));
	// Delayed by the reader, not by a fixed sleep: after it let go, the write landed within a few ms (the engine's own
	// rename slept 100 ms per try)
	CPPUNIT_ASSERT(took >= 140ms);
	CPPUNIT_ASSERT(took < 400ms);
#endif
}

void FileReplaceTest::testHeldTargetMakesAOnceWriteGiveUpAtOnce() {
#ifdef _WIN32
	TempFolder folder;
	writeFile(folder.path / "status.json.tmp", "new");
	writeFile(folder.path / "status.json", "old");
	Reader reader(folder.path / "status.json", 400ms);
	CPPUNIT_ASSERT(reader.holds());
	const auto start = std::chrono::steady_clock::now();
	CPPUNIT_ASSERT(!core::replaceFile(folder.path / "status.json.tmp", folder.path / "status.json", core::ReplaceMode::Once));
	CPPUNIT_ASSERT(std::chrono::steady_clock::now() - start < 50ms);
	reader.release();
#endif
}

void FileReplaceTest::testPatienceEndsWhenTheReaderNeverLetsGo() {
#ifdef _WIN32
	TempFolder folder;
	writeFile(folder.path / "status.json.tmp", "new");
	writeFile(folder.path / "status.json", "old");
	Reader reader(folder.path / "status.json", 0ms, true);
	CPPUNIT_ASSERT(reader.holds());
	const auto start = std::chrono::steady_clock::now();
	// Gives up when the patience ran out - the caller keeps the write pending - and the target is untouched
	CPPUNIT_ASSERT(!core::replaceFile(folder.path / "status.json.tmp", folder.path / "status.json", core::ReplaceMode::Patient, 60ms));
	const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
	CPPUNIT_ASSERT(took >= 55ms);
	CPPUNIT_ASSERT(took < 300ms);
	reader.release();
	CPPUNIT_ASSERT_EQUAL(std::string("old"), readFile(folder.path / "status.json"));
	CPPUNIT_ASSERT(std::filesystem::exists(folder.path / "status.json.tmp"));
#endif
}
