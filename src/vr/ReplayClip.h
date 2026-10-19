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

#ifndef ARX_VR_REPLAYCLIP_H
#define ARX_VR_REPLAYCLIP_H

#include <stddef.h>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "platform/Platform.h"
#include "vr/SimRig.h"

/*!
 * \file
 * \brief Record and replay (#550): a VR session as a clip in the simulated headset's script dialect
 *
 * Every VR session writes \c arx-replay.log in the user directory: a header, then one line per frame -
 * "@<ms>ms <command>; <command>; ..." with absolute display times since the record start - holding what
 * the runtime gave the game (the head, the grips and whether they are tracked, the aims, five curls per
 * hand, the raw action values, the session state, the interaction profile) and where the engine's player
 * was. A clip loads as a \c --vr-sim script as it is (the dialect is vr/SimRig.h's); a long one is streamed
 * by \ref ClipPlayer (the simulated runtime's \c play command). \ref ReplayRing keeps the last 60 s in
 * memory the dump helper shares (every crash and hang report carries it, \c arx-replay.vrsim), and cuts
 * the clip of a \c replay \c mark.
 *
 * Pure logic, no engine globals: covered by tests/vr/ReplayClipTest.cpp and tests/vr/ReplayRingTest.cpp.
 * The recorder in the game is vr/ReplayRecorder.h; the scripts are scripts/dev/replay.ps1 and
 * scripts/dev/clip-to-xrs.ps1; docs/vr/TESTING.md has the how-to.
 */
namespace vr {

//! A clip's header: comment lines, then the set-up lines, before the first frame line
struct ReplayHeader {
	std::string build;    //!< "# build: <text>"   arx_name + " " + arx_version
	std::string save;     //!< "# save: <text>"    "<save folder> <save name>" of the save made at the record start, or "none"
	u64 seed = 0;         //!< "# seed: <decimal>" the main thread's RNG seed (--seed <n>, else Random::currentSeed())
	float rate = 0.f;     //!< "# rate: <hz>"      the display rate at the record start
	std::string mark;     //!< "# mark: <name> <ms>" only in a clip cut at a `replay mark` ...
	s64 markMs = -1;      //!< ... the mark's display time in ms on the clip's clock
	float ipd = 0.f;      //!< set-up line "ipd <m>"   (0: not written)
	float floor = 0.f;    //!< set-up line "floor <m>" (0: not written)
};

//! "# arx-replay ..." first, then "# build: ", "# save: ", "# seed: ", "# rate: " [, "# mark: "] and the set-up lines
std::string formatReplayHeader(const ReplayHeader & header);

//! One frame of the stream: the rig the game saw at a predicted display time, and the player in the engine
struct ReplayFrame {
	u64 timeUs = 0;       //!< predicted display time since the record start, microseconds
	SimRig rig;           //!< head, grips (hands) + handTracked, aims, fingers (five curls, joints on/off), profile,
	                      //!< actions (the raw values the game read), focused / worn (the session), player
};

/*!
 * \brief A frame's commands as text, one string each, in the clip's order (the formatter's working state)
 *
 * The recorder keeps the previous frame's to write only what changed without formatting it twice
 */
struct ReplayFrameText {
	std::vector<std::string> keys;     //!< what each command sets ("head", "hand left", "action trigger left", ...)
	std::vector<std::string> commands; //!< the command, e.g. "head 0.0000 0.0000 0.0000 0.00 0.00 0.00"
};

//! Every command of \a frame, in the clip's order
void formatReplayFrameText(const ReplayFrame & frame, ReplayFrameText & text);

/*!
 * \brief One line "@<ms>ms <command>; <command>; ...\n" - ms = timeUs rounded to whole milliseconds, absolute (never @+)
 *
 * With \a previous only what changed since it; without, everything (a self-contained first frame). The commands
 * are head / hand / hand aim / player (vr/SimRig.h, #550) and the existing ones: hand left|right on|off, hand
 * left|right fingers <5 curls> | on|off, action <name> left|right <x> [<y>] (a 2D action - thumbstick - always with
 * both), session focused|visible|idle, profile <path>|none. Positions to 0.1 mm, angles to 0.01 degree, curls and
 * action values to 0.01.
 */
std::string formatReplayFrame(const ReplayFrame & frame, const ReplayFrame * previous = nullptr);

//! The same with the previous frame's text already formatted (\a previous may be null): \a text gets this frame's
std::string formatReplayFrame(const ReplayFrame & frame, const ReplayFrameText * previous, ReplayFrameText & text);

//! What the replayed game is doing: for the anchors and the drift
struct ReplayGame {
	bool playable = true; //!< in gameplay (no loading screen, no menu)
	SimPlayer player;     //!< the replayed player
};

//! vr.sim.play.drift: how far the replayed player is from the recorded one
struct ReplayDrift {
	float current = 0.f;  //!< meters, at the last frame compared
	float max = 0.f;      //!< meters, the largest so far
	u64 frames = 0;       //!< frames compared
	//! Compare one frame; only when both are in a level and in the same one (else nothing changes)
	void update(const SimPlayer & recorded, const SimPlayer & replayed);
};

//! The status block vr.sim.play
struct ClipStatus {
	std::string file;          //!< the clip's file name (the caller sets it; only the leaf, never a full path)
	double positionMs = 0.0;   //!< where the play is on the clip's clock
	u64 framesServed = 0;      //!< frame lines applied inside the play's window
	bool done = false;         //!< the window's end, the clip's end, play stop or an error
	u32 anchorsWaiting = 0;    //!< 1 while the play holds at an anchor
	ReplayDrift drift;
	u64 eventsReplayed = 0;    //!< discrete changes applied in the window: action values, session, profile,
	                           //!< hand on/off, fingers on/off - poses and curls are not events
};

//! The streaming clip player: a file cursor, never the whole clip in memory (a 30 minute clip is 50+ MB)
class ClipPlayer {

public:

	//! Gives the clip's next line (without the '\n'); false at the end of the clip
	using LineSource = std::function<bool(std::string & line)>;

	static constexpr u64 NoEnd = std::numeric_limits<u64>::max();

	//! Reads the header at once (up to the first frame line); \a fromUs / \a toUs: the window on the clip's clock
	explicit ClipPlayer(LineSource source, u64 fromUs = 0, u64 toUs = NoEnd);

	const ReplayHeader & header() const { return m_header; }

	/*!
	 * \brief The play at \a nowUs (the replay's display time since the play started, not decreasing)
	 *
	 * The clip's position is max(fromUs, the first frame's time) + nowUs minus the time held at anchors. Applies to
	 * \a sim, in order, the header's set-up lines (first call) and every frame line up to the position - the frames
	 * before fromUs at the first call, without counting them - reading no further than one line ahead. At an
	 * "anchor level <area>" the play holds (anchorsWaiting 1, the position stays at the anchor) until \a game is
	 * playable in that area; at "anchor save" until it is playable. The drift compares the clip's player with
	 * game.player after every call that applied a frame.
	 */
	void advance(u64 nowUs, const ReplayGame & game, Simulation & sim);

	//! End the play now (\c play \c stop)
	void stop();

	const ClipStatus & status() const { return m_status; } //!< file left empty
	ClipStatus & status() { return m_status; }

	//! The clip time of the last frame line applied
	u64 frameTimeUs() const { return m_frameTimeUs; }

	//! Empty, or "line <n>: <what>" (n counts every line of the file from 1) - a broken line ends the play
	const std::string & error() const { return m_error; }

private:

	//! The line read ahead, parsed
	struct Line {
		bool have = false;
		size_t number = 0;    //!< 1-based, every line of the file
		u64 timeUs = 0;
		bool anchor = false;  //!< "anchor level <area>" (area >= 0) or "anchor save" (area < 0)
		s32 anchorArea = -1;
		std::string commands; //!< the frame line's commands after the time
	};

	bool readLine(std::string & line);
	//! Parse a "@<ms>ms ..." line into \a line (its number set by the caller); false ends the play with the error
	bool parseFrameLine(std::string_view text, Line & line, std::string_view & rest);
	//! Read and parse the next frame line (false at the end or on an error, which ends the play)
	bool fetch();
	void fail(const std::string & what);
	bool anchorSatisfied(const ReplayGame & game) const;

	LineSource m_source;
	u64 m_fromUs;
	u64 m_toUs;
	ReplayHeader m_header;
	std::vector<std::string> m_setup;
	bool m_setupApplied = false;
	size_t m_lineNumber = 0;
	Line m_next;
	bool m_haveBase = false;
	u64 m_baseUs = 0;
	u64 m_heldUs = 0;       //!< the time held at anchors so far
	bool m_holding = false;
	u64 m_frameTimeUs = 0;
	bool m_framed = false;  //!< a frame line was applied
	ClipStatus m_status;
	std::string m_error;

};

/*!
 * \brief The last 60 s of the recorded stream in a block of memory the ring does not own (#550)
 *
 * The game lays it out in the dump helper's shared memory (#545), so every crash and hang report folder gets the
 * clip \c arx-replay.vrsim, written by the helper from the image even when the game is gone; \c replay \c mark
 * cuts the last 30 s of the same ring into a clip of its own. Plain data, no pointers, every frame with its own
 * checksum: a copy of the image at any address, torn by a frame being written, reads back as a clip of whole
 * frames.
 */
class ReplayRing {

public:

	//! The bytes a ring of \a seconds of frames at \a rate Hz needs (its header included)
	static size_t memorySize(u32 seconds, u32 rate);

	//! Lays a new, empty ring out in \a memory (\a size >= memorySize(), aligned to 16); allocates nothing else
	ReplayRing(void * memory, size_t size, u32 seconds, u32 rate);

	//! The clip header every clip cut from the ring carries (the session's: build, save, seed, rate, ipd, floor)
	void setHeader(const ReplayHeader & header);

	//! One frame, newest last (the frame thread); the oldest go when the ring is full
	void push(const ReplayFrame & frame);

	//! Frames the ring holds now
	u32 count() const;

private:

	void * m_memory;
	size_t m_size;

};

/*!
 * \brief Everything a ring image holds as a self-contained clip
 *
 * The header, the oldest frame in full, the rest as formatReplayFrame writes them; no frame line when it holds
 * none. \a image is the live memory, a byte copy of it at any address, or bytes read from another process; never
 * trusted: zeros, garbage or a cut-off copy give "" or fewer frames, never a crash or a read outside
 * [image, image + size).
 */
std::string replayRingClip(const void * image, size_t size);

/*!
 * \brief The clip of a mark
 *
 * The frames from the last one at or before markUs - windowUs (or the oldest kept) to the last one at or before
 * markUs, the first in full; the header adds "# mark: <mark> <markUs in ms>". Frame times stay on the session's clock.
 */
std::string replayRingClip(const void * image, size_t size, std::string_view mark, u64 markUs, u64 windowUs);

} // namespace vr

#endif // ARX_VR_REPLAYCLIP_H
