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

#ifndef ARX_TESTS_VR_MARKHOLDTEST_H
#define ARX_TESTS_VR_MARKHOLDTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Marks v2 (#574; written before the implementation): a SHORT press of both Index trackpads records what the owner
 * sees; a HOLD (up to 1 minute) records what he says AND does during it. The gesture itself (both trackpads, at once,
 * ControlResolver::markGesture / markHeld) is #551's and unchanged (tests/vr/MarkMomentTest.h); the JPEG writer is in
 * tests/vr/SnapshotImageTest.h; the game's side is asserted by scripts/dev/xrs/mark-moment.xrs; the listing and the
 * transcription by scripts/dev/test-marks.ps1 and scripts/dev/test-transcribe.ps1.
 *
 * The owner's decisions (2026-10-21): a short press is a snapshot only - both eyes, meta and the recorder, NO clip;
 * the clip belongs to the hold. A hold is capped at 1 minute. Periodic snapshots every 2 s plus one at the release:
 * the left eye at half resolution as JPEG; both eyes at full resolution (PNG) at the press. The short/hold threshold
 * is about 0.5 s, tuned in the headset (the contract's do_not_decide: its final value).
 *
 * The interface these tests expect:
 *
 * 1. In src/vr/MarkMoment.h (pure, next to what #551 put there):
 *
 *   namespace vr {
 *
 *   //! A press of both trackpads let go before this is a SHORT mark; held this long or longer it is a HOLD
 *   //! (about 500 ms; the owner tunes it in the headset - the tests accept 400-800 and use the constant)
 *   const s64 HoldThresholdMs = 500;
 *   //! The longest hold (the owner: 1 minute): at this length it ends by itself, as if released, with a notice in
 *   //! the hero-dialogue panel; VoiceNote::maxMs is the same value (the note never outlasts the hold)
 *   const s64 HoldMaxMs = 60000;
 *   //! A hold's periodic snapshots: one every 2 s after the press
 *   const s64 SnapshotIntervalMs = 2000;
 *   //! At most this many snapshot JPEGs in one hold (every 2 s of a minute, plus the release)
 *   const size_t SnapshotMax = 31;
 *   //! A hold's clip.vrsim starts this long before the press
 *   const s64 HoldContextMs = 10000;
 *
 *   enum class MarkKind { Press, Hold };
 *   //! "press" | "hold": meta.txt's kind= and status.json marks.lastKind
 *   const char * markKindName(MarkKind kind);
 *   //! What a mark held \a heldMs (from the press to the release of either trackpad) is: Press below
 *   //! HoldThresholdMs (a negative time too), Hold from it on
 *   MarkKind markKind(s64 heldMs);
 *   //! A hold held \a heldMs has reached HoldMaxMs: it ends now, by itself
 *   bool holdCapped(s64 heldMs);
 *   //! The hold's length: \a heldMs within [0, HoldMaxMs]
 *   s64 holdLengthMs(s64 heldMs);
 *
 *   //! The snapshot JPEGs of a mark held \a heldMs, as ms after the press, ascending (the press's own pictures -
 *   //! left.png and right.png - are not in it). A short press (markKind Press): none. A hold of length
 *   //! T = holdLengthMs(heldMs): the periodic ones, k * SnapshotIntervalMs for every k >= 1 with k * 2000 < T, then
 *   //! the release T - except when T has the same snapshotName as the last periodic one (they are the same picture,
 *   //! the periodic one stays). So 7000 -> 2000 4000 6000 7000; 6050 -> 2000 4000 6000; 60000 (and any longer)
 *   //! -> 2000 ... 58000 60000 (30). Never more than SnapshotMax. The game takes the periodic one on the first frame
 *   //! at or after its time, named by that time; the release one on the release frame
 *   std::vector<s64> snapshotOffsets(s64 heldMs);
 *   //! A snapshot's file name on the press-relative timeline: "snap-+" then the offset in seconds rounded DOWN to
 *   //! 0.1 s, at least two digits before the point, one after, then "s.jpg" - 2000 -> "snap-+02.0s.jpg",
 *   //! 7049 -> "snap-+07.0s.jpg", 6999 -> "snap-+06.9s.jpg", 500 -> "snap-+00.5s.jpg", 60000 -> "snap-+60.0s.jpg"
 *   std::string snapshotName(s64 offsetMs);
 *
 *   struct MarkMeta {
 *     ...                                    // #551's fields, unchanged
 *     MarkKind kind = MarkKind::Press;       //!< what the mark became at its release (or at the cap)
 *     s64 pressMs = -1;                      //!< the press on the replay clip's clock (replay::clipClockUs / 1000,
 *                                            //!< the "@<ms>ms" times of clip.vrsim); -1: the clock had not started
 *     s64 releaseMs = -1;                    //!< the release (or the cap) on the same clock; -1 likewise
 *     bool posed = false;                    //!< the press frame's pose is known (the frame had the views)
 *     SimRig pose;                           //!< ... the rig the game saw on the press frame (vr/SimRig.h: head,
 *                                            //!< hands = the grips, handTracked, fingers) - the owner, 2026-10-21
 *   };
 *   //! formatMarkMeta: #551's nine lines, then, each ending in "\n":
 *   //!   kind=<press|hold>
 *   //!   pressMs=<pressMs>
 *   //!   releaseMs=<releaseMs>
 *   //! and with \c posed (a short press's and a hold's alike: the press frame - a hold's clip has the rest), in the
 *   //! units, order and number format of the .vrsim commands (vr/ReplayClip.h formatReplayFrameText: metres to 4
 *   //! decimals, yaw pitch roll in degrees to 2, curls to 2, never "-0"):
 *   //!   head=<x> <y> <z> <yaw> <pitch> <roll>                  the "head" command's numbers
 *   //!   left=<x> <y> <z> <yaw> <pitch> <roll> <on|off>         the "hand left" grip pose, then tracked (handTracked)
 *   //!   right=<x> <y> <z> <yaw> <pitch> <roll> <on|off>        the same for the right
 *   //!   curlsLeft=<thumb> <index> <middle> <ring> <little>     the "hand left fingers" command's five curls
 *   //!   curlsRight=<thumb> <index> <middle> <ring> <little>
 *
 *   //! markNotice: the recording indicator only from HoldThresholdMs of recording on (it was VoiceNote::minMs): a
 *   //! short press shows the confirmation alone, never the microphone
 *
 *   }
 *
 * 2. In src/vr/ReplayClip.h (pure):
 *
 *   //! The replay ring's length and the display rate it is sized for (ReplayRecorder.cpp's RingSeconds /
 *   //! RingMaxRate, made public): 75 s at 144 Hz, so a 60 s hold and its 10 s before it fit on the Index at 144 Hz.
 *   //! Crash and hang reports' arx-replay.vrsim grow to 75 s with it
 *   const u32 ReplayRingSeconds = 75;
 *   const u32 ReplayRingMaxRate = 144;
 *
 *   struct ReplayHeader {
 *     ...
 *     s64 holdPressMs = -1;    //!< "# hold: <pressMs> <releaseMs>" only in a hold's clip, the line right after
 *     s64 holdReleaseMs = -1;  //!< "# mark: "; on the clip's clock; ClipPlayer::header() reads it back
 *   };
 *
 *   //! A hold mark's clip, cut at the release: replayRingClip(image, size, mark, releaseUs, releaseUs - pressUs +
 *   //! contextUs) - the frames from the last one at or before pressUs - contextUs (or the oldest kept) to the last one
 *   //! at or before releaseUs, the first in full, "# mark: <mark> <release ms>" - plus the header line
 *   //! "# hold: <pressUs in ms> <releaseUs in ms>" (rounded like the mark's ms). The game copies the ring's bytes on
 *   //! the release frame and calls this on the writer thread (the ring's image is a byte copy at any address).
 *   //! Any display rate works (the Index's 80/90/120/144 Hz, the Quest 3's; fixed for a session - it changes only with
 *   //! a SteamVR restart, the owner): the window is by display time, the "# rate:" line is the session's rate (80 in
 *   //! an 80 Hz session), and a ring sized for 75 s at 144 Hz keeps longer at a lower rate
 *   std::string replayRingHoldClip(const void * image, size_t size, std::string_view mark, u64 pressUs, u64 releaseUs,
 *                                  u64 contextUs);
 *
 * 3. The game (scripts/dev/xrs/mark-moment.xrs; docs/vr/TESTING.md "Marks of the moment"):
 *    - every mark: left.png + right.png (both eyes, full resolution, PNG) of the press frame, recorder.txt, meta.txt
 *      (kind, pressMs, releaseMs - written once the kind is known - and the press frame's pose)
 *    - a short press: nothing else - no clip.vrsim, no note.wav, no snapshot, no recording indicator
 *    - a hold, in addition: snap-+NN.Ns.jpg (the LEFT eye at half resolution, JPEG quality SnapshotJpegQuality) at
 *      every snapshotOffsets time; clip.vrsim = replayRingHoldClip(press, release, HoldContextMs); note.wav when the
 *      voice note is on and a microphone recorded (never under the simulated headset); at HoldMaxMs it ends by itself
 *      with a notice
 *    - status.json marks (src/core/CommandChannel.cpp), next to #551's fields:
 *        marks.lastKind      (text)   "" before the first mark; "pending" from the mark until its release (or the
 *                                     cap) decides; then "press" or "hold" (markKindName)
 *        marks.lastSnapshots (number) the snap-*.jpg files written for the last mark so far; 0 for a press
 *        marks.lastClipMs    (number) the last mark's clip.vrsim length in ms (its last frame line's time less its
 *                                     first's) once the file is written; 0 for a press, and until then
 *    - the log line "[vr] mark #<n>: marks\<folder> files=<lastFiles>" once every file of the mark is written (a
 *      hold: after its release), lastFiles sorted by name as in #551
 */
class MarkHoldTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MarkHoldTest);
	CPPUNIT_TEST(classifierTest);
	CPPUNIT_TEST(capTest);
	CPPUNIT_TEST(scheduleTest);
	CPPUNIT_TEST(scheduleBoundsTest);
	CPPUNIT_TEST(snapshotNameTest);
	CPPUNIT_TEST(indicatorFromThresholdTest);
	CPPUNIT_TEST(metaKindTest);
	CPPUNIT_TEST(metaPoseTest);
	CPPUNIT_TEST(ringSizeTest);
	CPPUNIT_TEST(holdClipMinuteTest);
	CPPUNIT_TEST(holdClipEightyHzTest);
	CPPUNIT_TEST(holdClipShortHoldTest);
	CPPUNIT_TEST(holdClipLittleHistoryTest);
	CPPUNIT_TEST(holdHeaderTest);
	CPPUNIT_TEST_SUITE_END();

public:

	//! Short against hold at HoldThresholdMs
	void classifierTest();
	//! The 60 s cap: the hold's length, the voice note's
	void capTest();
	//! Every 2 s plus the release; a short press gets none
	void scheduleTest();
	//! At most SnapshotMax, ascending, distinct names, the release last - for every hold length
	void scheduleBoundsTest();
	//! snap-+NN.Ns.jpg on the press-relative timeline
	void snapshotNameTest();
	//! No microphone indicator before the threshold: a short press shows the confirmation only
	void indicatorFromThresholdTest();
	//! meta.txt: kind, pressMs, releaseMs
	void metaKindTest();
	//! meta.txt: the press frame's pose, as the .vrsim commands write it
	void metaPoseTest();
	//! An 80 Hz session: the window by display time, the header's rate 80, the ring keeps longer
	void holdClipEightyHzTest();
	//! The ring holds 75 s at 144 Hz: a 60 s hold plus its 10 s before fit
	void ringSizeTest();
	//! A full minute's hold at 144 Hz, cut at the release: press - 10 s .. release, the first frame in full
	void holdClipMinuteTest();
	//! A 7 s hold: 17 s of clip
	void holdClipShortHoldTest();
	//! A hold soon after the record start: from the oldest frame kept
	void holdClipLittleHistoryTest();
	//! The "# hold:" line written and read back; none in a clip that is no hold's
	void holdHeaderTest();

};

#endif // ARX_TESTS_VR_MARKHOLDTEST_H
