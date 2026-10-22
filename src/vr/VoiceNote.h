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

#ifndef ARX_VR_VOICENOTE_H
#define ARX_VR_VOICENOTE_H

#include <stddef.h>
#include <string>
#include <vector>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The voice note of a mark (#551): the default microphone for as long as the recording is on (#632: from
 *        one press of both trackpads to the next; push-to-talk before), up to 1 minute (the cap, #574), saved as
 *        note.wav
 *
 * The capture's state machine and the WAV writer; the microphone is a \ref NoteDevice - the default OpenAL capture
 * device in the game (vr/NoteMicrophone.h), a fake in the tests. Pure: no OpenAL, no engine globals, no
 * file system - covered by tests/vr/VoiceNoteTest.cpp.
 */
namespace vr {

//! A microphone: the default OpenAL capture device in the game, a fake in the tests
class NoteDevice {

public:

	virtual ~NoteDevice() = default;

	/*!
	 * Open the default capture device for mono 16-bit samples at \a sampleRate and start capturing.
	 * \return false when there is none (no microphone): nothing is recorded, nothing is an error
	 */
	virtual bool open(u32 sampleRate) = 0;

	//! Append the samples captured since the last call to \a out, at most \a max of them. \return how many
	virtual size_t read(std::vector<s16> & out, size_t max) = 0;

	//! Stop capturing and close the device
	virtual void close() = 0;

};

/*!
 * The capture of one note: started on a recording's start press, updated every frame with whether it goes on (since
 * #632 always while the recording is on - it is stopped with it; #574 passed ControlResolver::markHeld), at most
 * \ref maxMs long
 */
class VoiceNote {

public:

	static constexpr u32 sampleRate = 16000;  //!< mono, 16 bit
	static constexpr s64 maxMs = 60000;       //!< the longest note: 1 minute = HoldMaxMs (16 kHz mono 16 bit: 1.92 MB)
	static constexpr s64 minMs = 300;         //!< the shortest note kept: fewer samples give no WAV

	enum class State {
		Idle,       //!< never started
		Recording,  //!< the device is open, samples come in
		Finished,   //!< released, 1 minute of time or of samples, or stop(): the device is closed (once)
		NoDevice    //!< start() found no microphone: no note, no error
	};

	/*!
	 * Start a new note at \a timeMs (the samples of an earlier note are dropped): Recording, or NoDevice when
	 * \a device does not open. The device must outlive the recording. The game calls it only on the mark, with
	 * both trackpads down
	 */
	State start(NoteDevice & device, s64 timeMs);

	/*!
	 * Every frame. Recording and \a held (both trackpads still down): read what the device has; at \a timeMs >= the
	 * start + \ref maxMs, or with maxMs of samples read, it stops - the device closed, never more than that many
	 * samples kept - and is Finished. Recording and not \a held: it stops at once, what the device already has read
	 * first. Does nothing in any other state: a note never starts or resumes by itself
	 */
	State update(s64 timeMs, bool held);

	//! Stop early (the game quits, a new mark): what the device still has is read, then Finished, the device closed;
	//! nothing in any other state
	State stop();

	State state() const { return m_state; }
	bool recording() const { return m_state == State::Recording; }
	const std::vector<s16> & samples() const { return m_samples; }
	//! When the note started, in the clock of \ref start
	s64 startMs() const { return m_startMs; }

	//! The note as note.wav's bytes; "" when fewer than \ref minMs of samples were recorded (a release too quick to
	//! say anything) - then no note.wav is written
	std::string wav() const;

	//! At least \ref minMs of samples were recorded: \ref wav gives a note
	bool kept() const;

	//! The samples moved out (the note keeps none), to build the WAV off the frame thread
	std::vector<s16> takeSamples();

private:

	State m_state = State::Idle;
	NoteDevice * m_device = nullptr;
	s64 m_startMs = 0;
	std::vector<s16> m_samples;

};

/*!
 * A mono 16-bit PCM WAV file: the 44-byte header ("RIFF", 36 + data bytes, "WAVE", "fmt ", 16, format 1, 1 channel,
 * \a sampleRate, sampleRate * 2 bytes a second, block 2, 16 bits, "data", data bytes), then the \a count samples,
 * little-endian
 */
std::string formatWav(const s16 * samples, size_t count, u32 sampleRate);

} // namespace vr

#endif // ARX_VR_VOICENOTE_H
