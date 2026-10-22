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

#include "core/CommandInbox.h"

#include <fstream>
#include <iterator>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

enum class Read {
	Done,    //!< the file's contents are read
	Gone,    //!< the file is not there (any more)
	Blocked, //!< the file is there but cannot be opened yet
};

/*
 * Read the file whole, sharing it with whoever still has it open. The test writes command.txt.tmp and renames it into
 * place; on Windows the renamer still holds the file open with DELETE access for a moment after the new name is
 * visible, and an open without FILE_SHARE_DELETE (std::ifstream) fails then with a sharing violation. The old code
 * read "" in that moment, deleted the file and acknowledged nothing: the batch was lost without a line in the log
 * (CI run 39120032077 attempt 1, vr-remap's 'waitframes 5' after 'vr-input down left.grip': the ack of the quit came
 * back as seq 15, the lost batch's number; #527). The moment is microseconds when the PC is idle and grows with every
 * preemption of the writer's thread, so it hit under three games. A file that cannot be opened is left alone and read
 * on a later look.
 */
Read readWhole(const std::filesystem::path & file, std::string & contents) {

#ifdef _WIN32
	HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(handle == INVALID_HANDLE_VALUE) {
		const DWORD error = GetLastError();
		return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? Read::Gone : Read::Blocked;
	}
	contents.clear();
	char buffer[4096];
	DWORD read = 0;
	while(ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
		contents.append(buffer, read);
	}
	CloseHandle(handle);
	return Read::Done;
#else
	std::ifstream in(file, std::ios::binary);
	if(!in) {
		std::error_code error;
		return std::filesystem::exists(file, error) ? Read::Blocked : Read::Gone;
	}
	contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return Read::Done;
#endif

}

//! Empty a file that cannot be removed, sharing it as the read does
bool empty(const std::filesystem::path & file) {
#ifdef _WIN32
	HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                            nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	CloseHandle(handle);
	return true;
#else
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	return bool(out);
#endif
}

//! Removed, or else emptied: either way it is not read again as it was
bool removeOrEmpty(const std::filesystem::path & file) {
	std::error_code error;
	return std::filesystem::remove(file, error) || empty(file);
}

std::string_view trimmed(std::string_view line) {
	const char * const space = " \t\r\n\v\f";
	const size_t begin = line.find_first_not_of(space);
	if(begin == std::string_view::npos) {
		return std::string_view();
	}
	return line.substr(begin, line.find_last_not_of(space) - begin + 1);
}

} // anonymous namespace

CommandInbox::CommandInbox(std::filesystem::path file, std::filesystem::file_time_type notBefore, Note note,
                           std::chrono::milliseconds interval)
	: m_file(std::move(file))
	, m_notBefore(notBefore)
	, m_note(std::move(note))
	, m_interval(interval)
	, m_thread(&CommandInbox::run, this)
{ }

CommandInbox::~CommandInbox() {
	{
		std::scoped_lock lock(m_stopMutex);
		m_stop = true;
	}
	m_stopped.notify_all();
	m_thread.join();
}

bool CommandInbox::take(std::vector<std::string> & lines) {
	std::unique_lock lock(m_mutex, std::try_to_lock);
	if(!lock.owns_lock() || m_batches.empty()) {
		return false;
	}
	lines = std::move(m_batches.front());
	m_batches.pop_front();
	return true;
}

void CommandInbox::note(bool warning, const std::string & message) {
	if(m_note) {
		m_note(warning, message);
	}
}

bool CommandInbox::look(std::vector<std::string> & lines) {

	// An error (a file being deleted can say "access denied") is no answer: the open below tells
	std::error_code error;
	if(!std::filesystem::exists(m_file, error) && !error) {
		m_openFailures = 0;
		return false;
	}

	const std::filesystem::file_time_type written = std::filesystem::last_write_time(m_file, error);
	if(!error && written < m_notBefore) {
		note(true, "ignoring " + m_file.string() + ": written before this process started");
		// Emptied, it counts as written now: the next look consumes it without a batch
		removeOrEmpty(m_file);
		return false;
	}

	std::string contents;
	const Read read = readWhole(m_file, contents);
	if(read == Read::Gone) {
		m_openFailures = 0;
		return false;
	}
	if(read == Read::Blocked) {
		// Still open by the test's rename (see readWhole): the file stays, a later look reads it
		const auto now = std::chrono::steady_clock::now();
		m_openFailures++;
		if(m_openFailures == 1) {
			m_firstOpenFailure = now;
			m_lastOpenNote = now;
			note(false, m_file.string() + " cannot be opened yet (its writer still has it open), reading it later");
		} else if(now - m_lastOpenNote >= std::chrono::seconds(1)) {
			m_lastOpenNote = now;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_firstOpenFailure).count();
			note(false, m_file.string() + " cannot be opened yet (its writer still has it open), reading it later ("
			            + std::to_string(m_openFailures) + " tries in " + std::to_string(ms) + " ms so far)");
		}
		if(now - m_firstOpenFailure >= GiveUpAfter) {
			note(true, "giving up on " + m_file.string() + " after " + std::to_string(m_openFailures)
			           + " tries: it stays open");
			m_openFailures = 0;
			std::filesystem::remove(m_file, error);
		}
		return false;
	}
	m_openFailures = 0;

	std::error_code removeError;
	if(!std::filesystem::remove(m_file, removeError)) {
		// Do not run the same batch again on the next look
		const bool emptied = empty(m_file);
		if(!contents.empty()) {
			note(true, "could not delete " + m_file.string() + " after reading it"
			           + (emptied ? ", emptied it" : ", and could not empty it"));
		}
	}

	lines.clear();
	for(size_t pos = 0; pos < contents.size(); ) {
		size_t end = contents.find('\n', pos);
		if(end == std::string::npos) {
			end = contents.size();
		}
		std::string_view line = trimmed(std::string_view(contents).substr(pos, end - pos));
		if(!line.empty() && line.front() != '#') {
			lines.emplace_back(line);
		}
		pos = end + 1;
	}
	return !lines.empty();
}

void CommandInbox::run() {
	while(true) {
		std::vector<std::string> lines;
		if(look(lines)) {
			std::scoped_lock lock(m_mutex);
			m_batches.push_back(std::move(lines));
			m_batchesRead++;
		}
		std::unique_lock lock(m_stopMutex);
		if(m_stopped.wait_for(lock, m_interval, [this] { return m_stop; })) {
			break;
		}
	}
}
