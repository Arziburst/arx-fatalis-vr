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

#include "core/CommandInboxTest.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/CommandInbox.h"

#ifdef _WIN32
#include <windows.h>
#endif

CppUnit::AutoRegisterSuite<CommandInboxTest> g_registerCommandInboxTest;

namespace {

using namespace std::chrono_literals;

using Lines = std::vector<std::string>;

struct InboxFolder {

	std::filesystem::path path;

	InboxFolder() {
		path = std::filesystem::temp_directory_path()
		       / ("arx-command-inbox-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
	}

	~InboxFolder() {
		std::error_code ignored;
		std::filesystem::remove_all(path, ignored);
	}

	std::filesystem::path command() const { return path / "command.txt"; }

};

//! Files written from now on are this run's
std::filesystem::file_time_type runStart() {
	return std::filesystem::file_time_type::clock::now() - 1min;
}

//! As the test runner does (test-lib.ps1 Send-ArxCommand): written next to its name, then renamed into place
bool sendBatch(const std::filesystem::path & file, const std::string & text) {
	std::filesystem::path tmp = file;
	tmp += ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << text;
	}
	for(int i = 0; i < 100; i++) {
		std::error_code error;
		std::filesystem::rename(tmp, file, error);
		if(!error) {
			return true;
		}
		std::this_thread::sleep_for(10ms);
	}
	return false;
}

//! Gone for good: a file being deleted (an access error while its delete is pending) still counts as there
bool isThere(const std::filesystem::path & file) {
	std::error_code error;
	const bool there = std::filesystem::exists(file, error);
	return there || error;
}

bool waitGone(const std::filesystem::path & file, std::chrono::milliseconds timeout = 10s) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while(isThere(file)) {
		if(std::chrono::steady_clock::now() >= deadline) {
			return false;
		}
		std::this_thread::sleep_for(1ms);
	}
	return true;
}

//! The frame, once a millisecond, until a batch is ready
bool takeWithin(CommandInbox & inbox, Lines & lines, std::chrono::milliseconds timeout = 10s) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	do {
		if(inbox.take(lines)) {
			return true;
		}
		std::this_thread::sleep_for(1ms);
	} while(std::chrono::steady_clock::now() < deadline);
	return false;
}

//! No batch for a while: nothing was read twice, nothing is left over
bool nothingFor(CommandInbox & inbox, std::chrono::milliseconds time = 200ms) {
	Lines lines;
	return !takeWithin(inbox, lines, time);
}

bool waitRead(CommandInbox & inbox, unsigned long long count, std::chrono::milliseconds timeout = 10s) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while(inbox.batchesRead() < count) {
		if(std::chrono::steady_clock::now() >= deadline) {
			return false;
		}
		std::this_thread::sleep_for(1ms);
	}
	return true;
}

struct NoteLog {

	std::mutex mutex;
	std::vector<std::pair<bool, std::string>> notes;

	CommandInbox::Note callback() {
		return [this](bool warning, std::string_view message) {
			std::scoped_lock lock(mutex);
			notes.emplace_back(warning, std::string(message));
		};
	}

	size_t count(bool warning) {
		std::scoped_lock lock(mutex);
		size_t n = 0;
		for(const auto & note : notes) {
			n += (note.first == warning) ? 1 : 0;
		}
		return n;
	}

};

std::string numbered(const char * what, int i) {
	return std::string(what) + " " + std::to_string(i);
}

#ifdef _WIN32

/*!
 * command.txt created and held by its writer: \a share is what it lets others do meanwhile. The content is in the
 * file before anyone can see it under its name (written under another name, then renamed through the same handle),
 * so a reader never finds it half-written
 */
class HeldCommandFile {

	HANDLE m_handle = INVALID_HANDLE_VALUE;

public:

	HeldCommandFile(const std::filesystem::path & file, const std::string & text, DWORD share) {
		std::filesystem::path staging = file;
		staging += ".staging";
		m_handle = CreateFileW(staging.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, share, nullptr, CREATE_NEW,
		                       FILE_ATTRIBUTE_NORMAL, nullptr);
		if(m_handle == INVALID_HANDLE_VALUE) {
			return;
		}
		DWORD written = 0;
		WriteFile(m_handle, text.data(), DWORD(text.size()), &written, nullptr);
		FlushFileBuffers(m_handle);
		const std::wstring name = file.wstring();
		std::vector<char> buffer(sizeof(FILE_RENAME_INFO) + (name.size() + 1) * sizeof(wchar_t), 0);
		FILE_RENAME_INFO * info = reinterpret_cast<FILE_RENAME_INFO *>(buffer.data());
		info->ReplaceIfExists = FALSE;
		info->RootDirectory = nullptr;
		info->FileNameLength = DWORD(name.size() * sizeof(wchar_t));
		std::memcpy(info->FileName, name.c_str(), name.size() * sizeof(wchar_t));
		if(written != text.size() || !SetFileInformationByHandle(m_handle, FileRenameInfo, info, DWORD(buffer.size()))) {
			CloseHandle(m_handle);
			m_handle = INVALID_HANDLE_VALUE;
		}
	}

	bool holds() const { return m_handle != INVALID_HANDLE_VALUE; }

	void release() {
		if(m_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(m_handle);
			m_handle = INVALID_HANDLE_VALUE;
		}
	}

	~HeldCommandFile() { release(); }

};

#endif // _WIN32

} // anonymous namespace


void CommandInboxTest::testBatchesInOrderEachTakenOnce() {
	InboxFolder folder;
	CommandInbox inbox(folder.command(), runStart());

	// The runner sends 20 batches; the frame takes none meanwhile (a long level load): the reader keeps them all
	const int count = 20;
	for(int i = 1; i <= count; i++) {
		CPPUNIT_ASSERT(sendBatch(folder.command(), numbered("first", i) + "\n" + numbered("second", i) + "\n"));
		CPPUNIT_ASSERT(waitGone(folder.command()));
	}
	CPPUNIT_ASSERT(waitRead(inbox, count));

	// One batch per take, oldest first, each one once
	for(int i = 1; i <= count; i++) {
		Lines lines;
		CPPUNIT_ASSERT(takeWithin(inbox, lines, 1s));
		CPPUNIT_ASSERT_EQUAL(size_t(2), lines.size());
		CPPUNIT_ASSERT_EQUAL(numbered("first", i), lines[0]);
		CPPUNIT_ASSERT_EQUAL(numbered("second", i), lines[1]);
	}
	CPPUNIT_ASSERT(nothingFor(inbox));
	CPPUNIT_ASSERT_EQUAL(static_cast<unsigned long long>(count), inbox.batchesRead());
}

void CommandInboxTest::testFrameTakesWhileTheRunnerWrites() {
	InboxFolder folder;
	CommandInbox inbox(folder.command(), runStart());

	const int count = 50;
	std::atomic<bool> runnerFailed { false };
	std::thread runner([&] {
		for(int i = 1; i <= count; i++) {
			if(!sendBatch(folder.command(), numbered("batch", i) + "\n") || !waitGone(folder.command())) {
				runnerFailed = true;
				return;
			}
		}
	});

	// The frame, once a millisecond: every batch arrives, in the order sent, once
	std::vector<std::string> taken;
	bool oneLineEach = true;
	const auto deadline = std::chrono::steady_clock::now() + 30s;
	while(taken.size() < size_t(count) && std::chrono::steady_clock::now() < deadline && !runnerFailed) {
		Lines lines;
		if(inbox.take(lines)) {
			oneLineEach = oneLineEach && lines.size() == 1;
			taken.push_back(lines.empty() ? std::string() : lines[0]);
		}
		std::this_thread::sleep_for(1ms);
	}
	runner.join();
	CPPUNIT_ASSERT(!runnerFailed);
	CPPUNIT_ASSERT(oneLineEach);
	CPPUNIT_ASSERT_EQUAL(size_t(count), taken.size());
	for(int i = 1; i <= count; i++) {
		CPPUNIT_ASSERT_EQUAL(numbered("batch", i), taken[size_t(i - 1)]);
	}
	CPPUNIT_ASSERT(nothingFor(inbox));
}

void CommandInboxTest::testTakeNeverWaitsForTheReader() {
	InboxFolder folder;
	CommandInbox inbox(folder.command(), runStart());

	// Hold the reader's lock on another thread, as a reader descheduled while it hands a batch over would
	std::mutex mutex;
	std::condition_variable changed;
	bool holding = false;
	bool release = false;
	bool timedOut = false;
	std::thread holder([&] {
		std::unique_lock<std::mutex> held = inbox.holdLockForTest();
		std::unique_lock lock(mutex);
		holding = true;
		changed.notify_all();
		timedOut = !changed.wait_for(lock, 10s, [&] { return release; });
	});
	{
		std::unique_lock lock(mutex);
		CPPUNIT_ASSERT(changed.wait_for(lock, 10s, [&] { return holding; }));
	}

	// The batch is on disk; the reader cannot hand it over. The frame neither waits for the reader nor reads the file
	// itself: every take is quick and finds nothing
	const bool sent = sendBatch(folder.command(), "held back\n");
	std::this_thread::sleep_for(200ms);
	bool tookSomething = false;
	const auto start = std::chrono::steady_clock::now();
	for(int i = 0; i < 20; i++) {
		Lines lines;
		tookSomething = inbox.take(lines) || tookSomething;
	}
	const auto took = std::chrono::steady_clock::now() - start;

	{
		std::scoped_lock lock(mutex);
		release = true;
		changed.notify_all();
	}
	holder.join();
	CPPUNIT_ASSERT(sent);
	CPPUNIT_ASSERT(!timedOut);
	CPPUNIT_ASSERT(took < 500ms);
	CPPUNIT_ASSERT(!tookSomething);

	// Once the reader has its lock again the batch comes, once
	Lines lines;
	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(1), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("held back"), lines[0]);
	CPPUNIT_ASSERT(waitGone(folder.command()));
	CPPUNIT_ASSERT(nothingFor(inbox));
}

void CommandInboxTest::testLinesTrimmedAndCommentsDropped() {
	InboxFolder folder;
	CommandInbox inbox(folder.command(), runStart());

	// pollCommands' split: trimmed (a CR too), blank lines and comments dropped, the last line without its newline kept
	CPPUNIT_ASSERT(sendBatch(folder.command(), "  first  \r\n\r\n# a comment\n\t second\t\n   # an indented comment\n\nthird"));
	Lines lines;
	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(3), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("first"), lines[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("second"), lines[1]);
	CPPUNIT_ASSERT_EQUAL(std::string("third"), lines[2]);
	CPPUNIT_ASSERT_EQUAL(1ull, inbox.batchesRead());

	// A file without a command is consumed and gives no batch
	CPPUNIT_ASSERT(sendBatch(folder.command(), "# only a comment\n\n   \r\n"));
	CPPUNIT_ASSERT(waitGone(folder.command()));
	CPPUNIT_ASSERT(nothingFor(inbox));
	CPPUNIT_ASSERT_EQUAL(1ull, inbox.batchesRead());

	CPPUNIT_ASSERT(sendBatch(folder.command(), "after\n"));
	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(1), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("after"), lines[0]);
	CPPUNIT_ASSERT_EQUAL(2ull, inbox.batchesRead());
}

void CommandInboxTest::testLeftoverOfAnEarlierRunIsDropped() {
	InboxFolder folder;

	// A command.txt an earlier run never read: removed with one warning, never run
	{
		std::ofstream out(folder.command(), std::ios::binary | std::ios::trunc);
		out << "quit\n";
	}
	std::filesystem::last_write_time(folder.command(), std::filesystem::file_time_type::clock::now() - 1h);
	NoteLog notes;
	CommandInbox inbox(folder.command(), runStart(), notes.callback());
	CPPUNIT_ASSERT(waitGone(folder.command()));
	CPPUNIT_ASSERT(nothingFor(inbox));
	CPPUNIT_ASSERT_EQUAL(0ull, inbox.batchesRead());
	CPPUNIT_ASSERT_EQUAL(size_t(1), notes.count(true));

	// This run's batches still come
	CPPUNIT_ASSERT(sendBatch(folder.command(), "fresh\n"));
	Lines lines;
	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(1), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("fresh"), lines[0]);
}

void CommandInboxTest::testFileItsWriterStillHoldsIsReadLater() {
#ifdef _WIN32
	InboxFolder folder;
	NoteLog notes;
	CommandInbox inbox(folder.command(), runStart(), notes.callback());

	// #527: the renamer still has the file open - nobody can open it; it stays and is read once let go, not lost
	HeldCommandFile held(folder.command(), "late\n", 0);
	CPPUNIT_ASSERT(held.holds());
	std::this_thread::sleep_for(300ms);
	Lines lines;
	CPPUNIT_ASSERT(!inbox.take(lines));
	CPPUNIT_ASSERT(isThere(folder.command()));
	CPPUNIT_ASSERT_EQUAL(0ull, inbox.batchesRead());
	CPPUNIT_ASSERT(notes.count(false) >= 1); // "cannot be opened yet", in the log as before
	held.release();

	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(1), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("late"), lines[0]);
	CPPUNIT_ASSERT(waitGone(folder.command()));
	CPPUNIT_ASSERT(nothingFor(inbox));
	CPPUNIT_ASSERT_EQUAL(1ull, inbox.batchesRead());
	CPPUNIT_ASSERT_EQUAL(size_t(0), notes.count(true));
#endif
}

void CommandInboxTest::testFileThatCannotBeRemovedRunsOnce() {
#ifdef _WIN32
	InboxFolder folder;
	CommandInbox inbox(folder.command(), runStart());

	// Its writer lets others read and write it but not delete it: the batch is read, the remove fails, and the
	// batch must still run once only (pollCommands empties such a file)
	HeldCommandFile held(folder.command(), "once\n", FILE_SHARE_READ | FILE_SHARE_WRITE);
	CPPUNIT_ASSERT(held.holds());
	std::this_thread::sleep_for(500ms);
	held.release();

	Lines lines;
	CPPUNIT_ASSERT(takeWithin(inbox, lines));
	CPPUNIT_ASSERT_EQUAL(size_t(1), lines.size());
	CPPUNIT_ASSERT_EQUAL(std::string("once"), lines[0]);
	CPPUNIT_ASSERT(waitGone(folder.command()));
	CPPUNIT_ASSERT(nothingFor(inbox, 300ms));
	CPPUNIT_ASSERT_EQUAL(1ull, inbox.batchesRead());
#endif
}

void CommandInboxTest::testStopIsPrompt() {
	InboxFolder folder;
#ifdef _WIN32
	// Even while the reader keeps trying a file nobody can open
	HeldCommandFile held(folder.command(), "never\n", 0);
	CPPUNIT_ASSERT(held.holds());
#endif
	auto inbox = std::make_unique<CommandInbox>(folder.command(), runStart());
	std::this_thread::sleep_for(100ms);
	const auto start = std::chrono::steady_clock::now();
	inbox.reset();
	const auto took = std::chrono::steady_clock::now() - start;
	CPPUNIT_ASSERT(took < 1s);
}
