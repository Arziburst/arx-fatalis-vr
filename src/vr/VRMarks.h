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
 * Both Index trackpads pressed in VR (vr::ControlResolver::markGesture) write <user dir>\c \\marks\\<name>\\
 * (vr::markFolderName) at once: left.png and right.png (both eyes of the press frame, full size), recorder.txt (the
 * flight recorder's tail, scrubbed like arx-recorder.txt) and, once the release decides what the mark is, meta.txt
 * (vr::formatMarkMeta: kind, press and release on the clip's clock, the press frame's pose). Let go before
 * vr::HoldThresholdMs it is a SHORT mark and that is all. Held longer it is a HOLD, up to vr::HoldMaxMs (then it
 * ends by itself with a notice): snap-+NN.Ns.jpg (the left eye at half size, every vr::SnapshotIntervalMs and at the
 * release), clip.vrsim (the replay ring from vr::HoldContextMs before the press to the release, with a "# hold:"
 * line) and, with the voice note on, note.wav: push-to-talk, the default microphone while both trackpads stay
 * pressed (vr/VoiceNote.h; a short press keeps no note). The hero-dialogue panel (vr/SceneSubtitles.h) shows
 * "Moment marked (#N)" for 2 s; while a hold's note records, the indicator "<mic> m:ss" counting up instead; once a
 * note.wav is written "<mic> Note saved (m:ss)" for 1.5 s.
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
	u32 count = 0;          //!< marks made since the start
	std::string last;       //!< the last mark's folder name, "" before the first
	std::string lastFiles;  //!< the non-empty files in it, sorted by name, joined by ","; "" until they are written
	std::string lastKind;   //!< "" before the first mark; "pending" until its release (or the cap); "press" | "hold"
	u32 lastSnapshots = 0;  //!< the snap-*.jpg files written for the last mark so far; 0 for a press
	s64 lastClipMs = 0;     //!< the last mark's clip.vrsim length in ms once written; 0 for a press and until then
	bool recording = false; //!< a voice note records now (only while both trackpads are held)
	s64 noteMs = 0;         //!< how long it records so far, ms (the indicator); 0 when none records
	bool note = true;       //!< the voice note is on
};

const MarksStatus & status();

//! Every frame in VR, after the controls were resolved (vr/VRInput.cpp): the gesture, the files, the note, the panel
void update();

//! The game quits: the note stops (saved), the files still being written are finished
void shutdown();

//! The channel's `marks ...` command (note on|off): "" or why it was refused
std::string command(std::string_view rest);

} // namespace marks
} // namespace vr

#endif // ARX_VR_VRMARKS_H
