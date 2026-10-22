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

#ifndef ARX_TESTS_VR_VOICENOTETEST_H
#define ARX_TESTS_VR_VOICENOTETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The voice note of a mark (#551; written before the implementation, the gesture and the marker are in
 * tests/vr/MarkMomentTest.h). PUSH-TO-TALK (the owner, after his headset test of 2026-10-20): both trackpads pressed
 * mark at once; KEEPING both pressed records the default microphone, up to 1 minute (#574, the owner, 2026-10-21: a hold
 * is capped at 1 minute - MarkHoldTest.h HoldMaxMs; it was 5 minutes, before that 10 s), saved as note.wav in the
 * marker folder, with a recording indicator counting up in the hero-dialogue panel
 * (MarkMomentTest.h: noteIndicatorText) only while held, then "Note saved" briefly (noteSavedText); releasing either
 * trackpad stops it at once; a release before the note has VoiceNote::minMs (0.3 s) of audio leaves no note.wav and no
 * "Note saved". Nothing ever records without both trackpads held. cfg.ini vr_mark_note 0 turns it off; no microphone
 * means no note and no error.
 * The real microphone (OpenAL capture, src/audio) is the owner's headset test; here the capture's state machine runs
 * on a fake device, and the WAV writer is checked byte by byte.
 *
 * The interface these tests expect, in src/vr/VoiceNote.h (pure: no OpenAL, no engine globals, no file system):
 *
 *   namespace vr {
 *
 *   //! A microphone: the default OpenAL capture device in the game, a fake in the tests
 *   class NoteDevice {
 *   public:
 *     virtual ~NoteDevice() = default;
 *     //! Open the default capture device for mono 16-bit samples at \a sampleRate and start capturing.
 *     //! \return false when there is none (no microphone): nothing is recorded, nothing is an error
 *     virtual bool open(u32 sampleRate) = 0;
 *     //! Append the samples captured since the last call to \a out, at most \a max of them. \return how many
 *     virtual size_t read(std::vector<s16> & out, size_t max) = 0;
 *     //! Stop capturing and close the device
 *     virtual void close() = 0;
 *   };
 *
 *   //! The capture of one note: started on the mark, updated every frame with whether both trackpads are still
 *   //! held (ControlResolver::markHeld), at most maxMs long
 *   class VoiceNote {
 *   public:
 *     static constexpr u32 sampleRate = 16000;  //!< mono, 16 bit
 *     static constexpr s64 maxMs = 60000;       //!< the longest note: 1 minute = HoldMaxMs (16 kHz mono 16 bit: 1.92 MB)
 *     static constexpr s64 minMs = 300;         //!< the shortest note kept: fewer samples than this give no WAV
 *     enum class State {
 *       Idle,       //!< never started
 *       Recording,  //!< the device is open, samples come in
 *       Finished,   //!< released, 1 minute of time or of samples, or stop(): the device is closed (once)
 *       NoDevice    //!< start() found no microphone: no note, no error
 *     };
 *     //! Start a new note at \a timeMs (the samples of an earlier note are dropped): Recording, or NoDevice when
 *     //! \a device does not open. The device must outlive the recording. The game calls it only on the mark, with
 *     //! both trackpads down
 *     State start(NoteDevice & device, s64 timeMs);
 *     //! Every frame. Recording and \a held (both trackpads still down): read what the device has; at \a timeMs >=
 *     //! the start + maxMs, or with maxMs of samples (sampleRate * maxMs / 1000) read, it stops - the device closed,
 *     //! never more than that many samples kept - and is Finished. Recording and not \a held (either trackpad let
 *     //! go): it stops at once - what the device already has is read (within the cap), the device closed, Finished.
 *     //! Does nothing in any other state (no open, no read, no close) - a note never starts or resumes by itself
 *     State update(s64 timeMs, bool held);
 *     //! Stop early (the game quits, a new mark): what the device still has is read, then Finished, the device
 *     //! closed; nothing in any other state
 *     State stop();
 *     State state() const;
 *     bool recording() const;  //!< state() == Recording
 *     const std::vector<s16> & samples() const;
 *     //! The note as note.wav's bytes (formatWav of the samples at sampleRate); "" when fewer than
 *     //! sampleRate * minMs / 1000 samples were recorded (a release too quick to say anything, or no sample at
 *     //! all) - then no note.wav is written
 *     std::string wav() const;
 *   };
 *
 *   //! A mono 16-bit PCM WAV file: the 44-byte header ("RIFF", 36 + data bytes, "WAVE", "fmt ", 16, format 1,
 *   //! 1 channel, \a sampleRate, sampleRate * 2 bytes a second, block 2, 16 bits, "data", data bytes), then the
 *   //! \a count samples, little-endian
 *   std::string formatWav(const s16 * samples, size_t count, u32 sampleRate);
 *
 *   }
 */
class VoiceNoteTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(VoiceNoteTest);
	CPPUNIT_TEST(wavHeaderTest);
	CPPUNIT_TEST(wavEmptyTest);
	CPPUNIT_TEST(wavLengthTest);
	CPPUNIT_TEST(noDeviceTest);
	CPPUNIT_TEST(recordsOneMinuteTest);
	CPPUNIT_TEST(sampleCapTest);
	CPPUNIT_TEST(silentDeviceTest);
	CPPUNIT_TEST(stopEarlyTest);
	CPPUNIT_TEST(secondNoteTest);
	CPPUNIT_TEST(releaseStopsTest);
	CPPUNIT_TEST(shortHoldTest);
	CPPUNIT_TEST(minLengthTest);
	CPPUNIT_TEST(neverWithoutHoldTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void wavHeaderTest();
	void wavEmptyTest();
	void wavLengthTest();
	void noDeviceTest();
	void recordsOneMinuteTest();
	void sampleCapTest();
	void silentDeviceTest();
	void stopEarlyTest();
	void secondNoteTest();
	void releaseStopsTest();
	void shortHoldTest();
	void minLengthTest();
	void neverWithoutHoldTest();

};

#endif // ARX_TESTS_VR_VOICENOTETEST_H
