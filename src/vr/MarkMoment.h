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

#ifndef ARX_VR_MARKMOMENT_H
#define ARX_VR_MARKMOMENT_H

#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/SimRig.h"

/*!
 * \file
 * \brief Mark the moment in the headset (#551, marks v2 #574): the marker folder's name, its meta.txt, the short
 *        press against the hold, the hold's snapshots and the panel's lines
 *
 * During a headset test both Index trackpads pressed (vr::ControlResolver::markGesture) start a recording into
 * <user dir>\c \\marks\\<name>\\ and the same press again stops it (a toggle since #632; at \ref HoldMaxMs it stops
 * by itself): the eye images, the flight recorder's tail, the voice note (vr/VoiceNote.h), the left eye as a
 * snapshot every \ref SnapshotIntervalMs and at the stop, the replay clip from \ref HoldContextMs before the start
 * to the stop and this meta.txt - #574's HOLD, so the folder reads as before. #574's SHORT press (let go before
 * \ref HoldThresholdMs: the eyes only) is gone from the game; its helpers stay for the folders older builds wrote
 * and for the panel's first moments. The game's side is vr/VRMarks.h. Pure: no engine globals, no file system -
 * covered by tests/vr/MarkMomentTest.cpp and tests/vr/MarkHoldTest.cpp.
 */
namespace vr {

//! A press of both trackpads let go before this is a SHORT mark; held this long or longer it is a HOLD (about
//! 500 ms; the owner tunes it in the headset)
const s64 HoldThresholdMs = 500;
//! The longest hold (the owner: 1 minute): at this length it ends by itself, as if released, with a notice in the
//! hero-dialogue panel; VoiceNote::maxMs is the same value (the note never outlasts the hold)
const s64 HoldMaxMs = 60000;
//! A hold's periodic snapshots: one every 2 s after the press
const s64 SnapshotIntervalMs = 2000;
//! At most this many snapshot JPEGs in one hold (every 2 s of a minute, plus the release)
const size_t SnapshotMax = 31;
//! A hold's clip.vrsim starts this long before the press
const s64 HoldContextMs = 10000;
//! How long "Hold ended at the 1:00 limit" shows once a hold reached \ref HoldMaxMs
const s64 HoldCappedNoticeMs = 3000;
//! The shortest cap the channel's `marks cap <ms>` sets for a session (#632's test hook; HoldMaxMs the longest)
const s64 RecordingCapMinMs = 1000;

enum class MarkKind { Press, Hold };

//! "press" | "hold": meta.txt's kind= and status.json marks.lastKind
const char * markKindName(MarkKind kind);

//! What a mark held \a heldMs (from the press to the release of either trackpad) is: Press below HoldThresholdMs
//! (a negative time too), Hold from it on
MarkKind markKind(s64 heldMs);

//! A hold held \a heldMs has reached HoldMaxMs: it ends now, by itself
bool holdCapped(s64 heldMs);

//! The hold's length: \a heldMs within [0, HoldMaxMs]
s64 holdLengthMs(s64 heldMs);

/*!
 * The snapshot JPEGs of a mark held \a heldMs, as ms after the press, ascending (the press's own pictures - left.png
 * and right.png - are not in it). A short press: none. A hold of length T = holdLengthMs(heldMs): k *
 * SnapshotIntervalMs for every k >= 1 with k * SnapshotIntervalMs < T, then the release T - unless T has the same
 * \ref snapshotName as the last periodic one (the same picture: the periodic one stays). Never more than SnapshotMax.
 * The game takes a periodic one on the first frame at or after its time, named by that time; the release one on the
 * release frame
 */
std::vector<s64> snapshotOffsets(s64 heldMs);

//! A snapshot's file name on the press-relative timeline: "snap-+" then the offset in seconds rounded down to 0.1 s,
//! at least two digits before the point - 2000 -> "snap-+02.0s.jpg", 6999 -> "snap-+06.9s.jpg"
std::string snapshotName(s64 offsetMs);

/*!
 * Whether the mark gesture writes a mark: not while a recorded clip plays into the simulated headset (#550's replay
 * replays the owner's trackpad presses - his marks are his session's, not the replay's)
 */
bool markAllowed(bool replayPlaying);

/*!
 * Whether the voice note may open the real microphone: never under the simulated headset (\c --vr-sim: CI, a
 * replay, a worker's run - a microphone on the PC must never record the room); there the note behaves as without a
 * microphone: no note, no error
 */
bool noteMicrophoneAllowed(bool simulatedHeadset);

//! The confirmation in the hero-dialogue panel: "Moment marked (#<number>)"
std::string markNoticeText(u32 number);

/*!
 * The microphone mark that leads the voice note's lines: U+1F3A4 (MICROPHONE) in UTF-8. The status text carries the
 * character; the panel draws an icon in its place (the game's fonts have no glyph for it)
 */
inline constexpr std::string_view MicrophoneMark = "\xF0\x9F\x8E\xA4";

/*!
 * The recording indicator while a push-to-talk note records, counting up: the mark, a space, the elapsed time as
 * <minutes>:<seconds> (whole seconds rounded down, a negative time is 0:00) - "<mic> 0:23"
 */
std::string noteIndicatorText(s64 elapsedMs);

//! The brief confirmation after a note was saved: "<mic> Note saved (<m>:<ss>)", the note's length in the same form
std::string noteSavedText(s64 lengthMs);

//! ... after a recording without a note (no microphone, the note off) was saved: "<mic> Recording saved (<m>:<ss>)"
std::string recordingSavedText(s64 lengthMs);

//! How long "Moment marked (#N)" shows (the owner: about 2 s)
const s64 MarkNoticeMs = 2000;
//! How long "<mic> Note saved (m:ss)" shows after the note.wav was written (the owner: about 1.5 s)
const s64 NoteSavedMs = 1500;

//! What the hero-dialogue panel shows for the marks
enum class MarkNotice { None, Marked, Recording, Saved };

//! The marks' state the panel's notice follows
struct MarkNoticeState {
	bool recording = false;     //!< a recording is on (#632: from the start press to the stop or the cap)
	s64 recordingMs = 0;        //!< ... for this long
	bool saving = false;        //!< a stopped recording's files are being written
	s64 savingLengthMs = 0;     //!< ... its length: the indicator's final time
	s64 sinceSavedMs = -1;      //!< how long ago the last recording's files were written, -1 for none
	s64 savedLengthMs = 0;      //!< ... its length
	bool savedNote = true;      //!< ... with a note.wav: "Note saved", else "Recording saved"
	s64 sinceMarkedMs = -1;     //!< how long ago the last mark was made, -1 for none
	u32 markedNumber = 0;
	s64 sinceCappedMs = -1;     //!< how long ago the last hold ended by itself at HoldMaxMs, -1 for none
};

//! The notice after a hold reached \ref HoldMaxMs: "Hold ended at the 1:00 limit"
std::string holdCappedText();

/*!
 * The notice and its line: the indicator while a note records (from HoldThresholdMs on: a short press shows the
 * confirmation only), and frozen at the note's final time while its note.wav is written - so the panel never goes
 * blank between the release and "Note saved"; then "Note saved" for \ref NoteSavedMs; else, after a hold that
 * reached its cap, \ref holdCappedText for \ref HoldCappedNoticeMs (as the confirmation's kind, Marked); else the
 * confirmation for \ref MarkNoticeMs; else none (an empty line)
 */
MarkNotice markNotice(const MarkNoticeState & state, std::string & text);

/*!
 * The marker folder's name: the mark's UTC time (ISO 8601 basic) and its number in the session -
 * "20261020T153012Z-1"; sorts by time, valid on every file system
 */
std::string markFolderName(s64 unixSeconds, u32 number);

//! What meta.txt says about a mark
struct MarkMeta {
	u32 number = 0;               //!< the mark's number in this session, from 1 (the "#N" of the confirmation)
	s64 unixSeconds = 0;          //!< when it was made: UTC seconds since 1970
	u32 area = 0;                 //!< the level (status.json "area"), 0 for none
	Vec3f position = Vec3f(0.f);  //!< the player's position (status.json player.x / y / z), game units
	float yaw = 0.f;              //!< the player's heading (status.json player.yaw), degrees
	std::string build;            //!< the build: version, commit and config
	std::string state;            //!< the game's state (status.json "state": GAMEPLAY, MENU, ...)
	std::string session;          //!< the VR session's state (focused, visible, synchronized, ...)
	MarkKind kind = MarkKind::Press; //!< what the mark became at its release (or at the cap)
	s64 pressMs = -1;             //!< the press on the replay clip's clock (the "@<ms>ms" times of clip.vrsim); -1:
	                              //!< the clock had not started
	s64 releaseMs = -1;           //!< the release (or the cap) on the same clock; -1 likewise
	bool posed = false;           //!< the press frame's pose is known (the frame had the views)
	SimRig pose;                  //!< ... the rig the game saw on the press frame: head, grips, tracked, fingers
};

/*!
 * meta.txt: "# moment mark (#551)", then one key=value line per field - mark, time (YYYY-MM-DDTHH:MM:SSZ, UTC),
 * area, position ("<x> <y> <z>", one decimal each), yaw (one decimal), build, state, session, kind (press|hold),
 * pressMs, releaseMs - each ending in "\n". With \c posed the press frame's pose follows in the numbers of the .vrsim
 * commands (vr/ReplayClip.h formatReplayFrameText): head=<the "head" command's six>, left= / right=<the grip's six>
 * <on|off>, curlsLeft= / curlsRight=<the five curls>. A control character in a text field is written as a space, so
 * a field is always one line. No path of any kind.
 */
std::string formatMarkMeta(const MarkMeta & meta);

} // namespace vr

#endif // ARX_VR_MARKMOMENT_H
