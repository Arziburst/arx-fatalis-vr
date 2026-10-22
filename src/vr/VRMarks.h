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

#ifndef ARX_VR_VRMARKS_H
#define ARX_VR_VRMARKS_H

#include <string>
#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief Mark the moment in the headset (#551, marks v2 #574): the game's side
 *
 * A toggle (#632, the owner, 2026-10-22): both Index trackpads pressed in VR (vr::ControlResolver::markGesture)
 * START a recording, the same press again STOPS it; at vr::HoldMaxMs (the channel's `marks cap` for a test) it
 * stops by itself. The thumbs stay free in between: a trackpad or a button alone acts on the game. A recording is
 * #574's hold, its folder <user dir>\c \\marks\\<name>\\ (vr::markFolderName): at the start left.png and right.png
 * (both eyes of the start frame, full size) and recorder.txt (the flight recorder's tail, scrubbed like
 * arx-recorder.txt); snap-+NN.Ns.jpg (the left eye at half size, every vr::SnapshotIntervalMs and at the stop);
 * after the stop clip.vrsim (the replay ring from vr::HoldContextMs before the start to the stop, with a "# hold:"
 * line), with the voice note on note.wav (the default microphone for as long as the recording is on,
 * vr/VoiceNote.h) and meta.txt last (vr::formatMarkMeta: kind=hold, the start and the stop on the clip's clock, the
 * start frame's pose). The hero-dialogue panel (vr/SceneSubtitles.h) shows "Moment marked (#N)" for the first
 * vr::HoldThresholdMs, then the indicator "<mic> m:ss" counting up while it records, frozen until the files are
 * written, then "<mic> Note saved (m:ss)" (without a note "Recording saved") for 1.5 s.
 *
 * Nothing waits for the GPU: the eyes are copied on the GPU and read back later (vr/EyeShots.h); the images, the
 * clip and the note are formatted and written on the mark writer thread. The microphone opens on the press frame
 * (one OpenAL call, logged with its time). cfg.ini \c vr_mark_note turns the voice note off, the channel's
 * \c marks \c note \c on|off for the session.
 */
namespace vr {
namespace marks {

//! status.json \c marks
struct MarksStatus {
	u32 count = 0;          //!< recordings started since the start (a stop press is no new one)
	std::string last;       //!< the last mark's folder name, "" before the first
	std::string lastFiles;  //!< the non-empty files in it, sorted by name, joined by ","; "" until they are written
	std::string lastKind;   //!< "" before the first mark; "pending" until its stop; then "hold" (#574's "press"
	                        //!< is gone since #632)
	u32 lastSnapshots = 0;  //!< the snap-*.jpg files written for the last mark so far
	s64 lastClipMs = 0;     //!< the last mark's clip.vrsim length in ms once written; 0 until then
	bool recording = false; //!< a recording is on: from the start press to the stop press or the cap (#632)
	s64 recordingMs = 0;    //!< how long it has run, ms; 0 when none is on
	s64 capMs = 60000;      //!< the longest recording, ms (vr::HoldMaxMs; the channel's `marks cap <ms>`)
	s64 noteMs = 0;         //!< how long the voice note records so far, ms; 0 when none records
	bool note = true;       //!< the voice note is on
};

const MarksStatus & status();

//! Every frame in VR, after the controls were resolved (vr/VRInput.cpp): the gesture, the files, the note, the panel
void update();

//! The game quits: the note stops (saved), the files still being written are finished
void shutdown();

//! The channel's `marks ...` command (note on|off; cap <ms>: the longest recording for the session, a whole number
//! from vr::RecordingCapMinMs to vr::HoldMaxMs): "" or why it was refused
std::string command(std::string_view rest);

} // namespace marks
} // namespace vr

#endif // ARX_VR_VRMARKS_H
