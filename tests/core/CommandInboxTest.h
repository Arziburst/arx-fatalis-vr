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

#ifndef ARX_TESTS_CORE_COMMANDINBOXTEST_H
#define ARX_TESTS_CORE_COMMANDINBOXTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The test channel's command.txt polled and read off the frame (#561): a reader thread looks for the file, reads it,
 * removes it and splits it into command lines; the frame only takes a ready batch - in order, each exactly once,
 * never waiting for the reader or the file system.
 *
 * The interface these tests are written against (written before the code, contract #561). The worker implements it
 * as written and moves pollCommands' file work (src/core/CommandChannel.cpp) into it; the frame's pollCommands then
 * only calls take(). src/core/CommandInbox.cpp goes into arxtest_SOURCES (tests/CMakeLists.txt).
 *
 * \code
 * // src/core/CommandInbox.h
 * class CommandInbox {
 * public:
 *     //! A line for arx.log, from the reader thread: warning = true for LogWarning, false for LogInfo
 *     using Note = std::function<void(bool warning, std::string_view message)>;
 *
 *     //! Starts the reader thread. It looks for \a file every \a interval (or watches its folder: then at least
 *     //! that often). A file last written before \a notBefore is a leftover of an earlier run: removed, one
 *     //! warning note, no batch (pollCommands' "written before this process started")
 *     CommandInbox(std::filesystem::path file, std::filesystem::file_time_type notBefore, Note note = Note(),
 *                  std::chrono::milliseconds interval = std::chrono::milliseconds(5));
 *
 *     //! Stops the reader thread (promptly: within a second, whatever the file does)
 *     ~CommandInbox();
 *
 *     //! The oldest batch read and not taken yet, into \a lines (replaced); false when none is ready.
 *     //! Never touches the file system, never waits for the reader: when the reader holds its lock, false now
 *     //! and the batch on a later call
 *     bool take(std::vector<std::string> & lines);
 *
 *     //! Batches with command lines read so far (taken or not)
 *     unsigned long long batchesRead();
 *
 *     //! For the host test: hold the lock the reader takes to hand a batch over, as a descheduled reader would
 *     std::unique_lock<std::mutex> holdLockForTest();
 * };
 * \endcode
 *
 * What the reader keeps from pollCommands (the channel's semantics stay):
 *  - the file is read whole, sharing it with its writer (FILE_SHARE_READ | WRITE | DELETE); a file that cannot be
 *    opened yet (its renamer still holds it, #527) stays and is read on a later look, with an info note
 *  - after the read the file is removed; one it cannot remove is emptied, so the batch never runs twice
 *  - lines are trimmed of whitespace (a CR included); empty lines and lines starting with '#' are dropped; a file
 *    without command lines is consumed and gives no batch
 */
class CommandInboxTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(CommandInboxTest);
	CPPUNIT_TEST(testBatchesInOrderEachTakenOnce);
	CPPUNIT_TEST(testFrameTakesWhileTheRunnerWrites);
	CPPUNIT_TEST(testTakeNeverWaitsForTheReader);
	CPPUNIT_TEST(testLinesTrimmedAndCommentsDropped);
	CPPUNIT_TEST(testLeftoverOfAnEarlierRunIsDropped);
	CPPUNIT_TEST(testFileItsWriterStillHoldsIsReadLater);
	CPPUNIT_TEST(testFileThatCannotBeRemovedRunsOnce);
	CPPUNIT_TEST(testStopIsPrompt);
	CPPUNIT_TEST_SUITE_END();

public:

	void testBatchesInOrderEachTakenOnce();
	void testFrameTakesWhileTheRunnerWrites();
	void testTakeNeverWaitsForTheReader();
	void testLinesTrimmedAndCommentsDropped();
	void testLeftoverOfAnEarlierRunIsDropped();
	void testFileItsWriterStillHoldsIsReadLater();
	void testFileThatCannotBeRemovedRunsOnce();
	void testStopIsPrompt();

};

#endif // ARX_TESTS_CORE_COMMANDINBOXTEST_H
