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
 * \brief Mark the moment in the headset (#551): the game's side
 *
 * Both thumbstick clicks held 3 s in VR (vr::ControlResolver::markGesture) write <user dir>\c \\marks\\<name>\\
 * (vr::markFolderName): left.png and right.png (the eye images of the next drawn frame), recorder.txt (the flight
 * recorder's tail, scrubbed like arx-recorder.txt), clip.vrsim (the replay ring's last 30 s, the clip of
 * `replay mark`), meta.txt (vr::formatMarkMeta) and, with the voice note on, note.wav (up to 10 s of the default
 * microphone from the mark on, vr/VoiceNote.h). The hero-dialogue panel shows "Moment marked (#N)" for 2 s, then
 * "Recording note... (N s)" counting the note's seconds left down (vr/SceneSubtitles.h).
 *
 * The files are written off the frame thread; the mark's frame reads both eye images back from the GPU (one hitch).
 * cfg.ini \c vr_mark_note turns the voice note off, the channel's \c marks \c note \c on|off for the session.
 */
namespace vr {
namespace marks {

//! status.json \c marks
struct MarksStatus {
	u32 count = 0;          //!< marks made since the start
	std::string last;       //!< the last mark's folder name, "" before the first
	std::string lastFiles;  //!< the non-empty files in it, sorted by name, joined by ","; "" until they are written
	bool recording = false; //!< a voice note records now
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
