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

#ifndef ARX_TESTS_VR_REPLAYRINGTEST_H
#define ARX_TESTS_VR_REPLAYRINGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The replay ring (#550): the last 60 s of the recorded stream in a block of memory the ring does not own. The game
 * lays it out in the dump helper's shared memory (#545), so every crash and hang report folder gets the clip
 * arx-replay.vrsim, written by the helper from the image even when the game is gone; `replay mark <name>` cuts the
 * last 30 s of the same ring into a clip of its own.
 *
 * The interface these tests expect, in src/vr/ReplayClip.h (tests/vr/ReplayClipTest.h has the rest of it):
 *
 *   namespace vr {
 *
 *   class ReplayRing {
 *   public:
 *     //! The bytes a ring of \a seconds of frames at \a rate Hz needs (its header included)
 *     static size_t memorySize(u32 seconds, u32 rate);
 *     //! Lays a new, empty ring out in \a memory (\a size >= memorySize(), aligned to 16); allocates nothing else
 *     ReplayRing(void * memory, size_t size, u32 seconds, u32 rate);
 *     //! The clip header every clip cut from the ring carries (the session's: build, save, seed, rate, ipd, floor)
 *     void setHeader(const ReplayHeader & header);
 *     //! One frame, newest last (the frame thread); the oldest go when the ring is full
 *     void push(const ReplayFrame & frame);
 *   };
 *
 *   //! Everything a ring image holds as a self-contained clip: the header, the oldest frame in full, the rest as
 *   //! formatReplayFrame writes them; no frame line when it holds none. \a image is the live memory, a byte copy of it
 *   //! at any address, or bytes read from another process; never trusted: zeros, garbage or a cut-off copy give ""
 *   //! or fewer frames, never a crash or a read outside [image, image + size)
 *   std::string replayRingClip(const void * image, size_t size);
 *
 *   //! The clip of a mark: the frames from the last one at or before markUs - windowUs (or the oldest kept) to the
 *   //! last one at or before markUs, the first in full; the header adds "# mark: <mark> <markUs in ms>". Frame times
 *   //! stay on the session's clock.
 *   std::string replayRingClip(const void * image, size_t size, std::string_view mark, u64 markUs, u64 windowUs);
 *
 *   }
 *
 * In the game (scripts/dev/replay-check.ps1, scripts/dev/flight-recorder-check.ps1 -Case replay-crash):
 *   - the channel command `replay mark <name> [<seconds>]` (name: letters, digits, - and _; seconds 1-60, default 30)
 *     writes <user dir>\replay-marks\<yyyy-MM-dd-HH-mm-ss>-<name>.vrsim; refused without a recording
 *   - every report folder of crashes\ and hangs\ holds arx-replay.vrsim when the game recorded (VR, no --no-replay-log)
 */
class ReplayRingTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ReplayRingTest);
	CPPUNIT_TEST(testKeepsTheLastSixtySeconds);
	CPPUNIT_TEST(testMarkCutsTheLastThirtySeconds);
	CPPUNIT_TEST(testMarkBeforeTheNewestFrame);
	CPPUNIT_TEST(testWindowIsConfigurable);
	CPPUNIT_TEST(testWindowLongerThanTheRing);
	CPPUNIT_TEST(testCopyOfTheImage);
	CPPUNIT_TEST(testBrokenImages);
	CPPUNIT_TEST(testEmptyRing);
	CPPUNIT_TEST_SUITE_END();

public:

	void testKeepsTheLastSixtySeconds();
	void testMarkCutsTheLastThirtySeconds();
	void testMarkBeforeTheNewestFrame();
	void testWindowIsConfigurable();
	void testWindowLongerThanTheRing();
	void testCopyOfTheImage();
	void testBrokenImages();
	void testEmptyRing();

};

#endif // ARX_TESTS_VR_REPLAYRINGTEST_H
