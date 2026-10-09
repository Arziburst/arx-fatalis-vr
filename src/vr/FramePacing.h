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

#ifndef ARX_VR_FRAMEPACING_H
#define ARX_VR_FRAMEPACING_H

#include <string>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The rate / cadence verdict of the OpenXR frame loop (#20, research rows F-07, F-26)
 *
 * Pure logic over timestamps: no OpenXR, no engine globals, no clock reads, so it is covered by a
 * host unit test (tests/vr/FramePacingTest.cpp). The frame loop feeds one \ref FrameTiming per
 * submitted frame; every window (3 s) a \ref PacingReport comes out, logged as one \c [vr] \c rate:
 * line and written to status.json (\c vr.pacing).
 *
 * The questions the report answers, with the lessons behind them:
 *  - Does the loop submit one frame per display slot? \c xrWaitFrame blocks until the runtime
 *    wants the next frame, so a healthy loop is MATCHED: as many frames as slots. OVER-SUBMITTING
 *    (more frames than slots) means nothing paces the loop - the session is not synchronized or
 *    the runtime does not block; UNDER-SUBMITTING means slots go unfilled: the stalls are upstream,
 *    in the game's own work.
 *  - Is the cadence even? Frames landing 1.05-1.11 slots apart ghost on head turns (Dishonored VR
 *    at 120 Hz), while exactly 2.00 slots per frame are smooth. The report gives the mean slots
 *    per frame and the standard deviation of the interval between frames.
 *  - \c xrWaitFrame and \c xrEndFrame are one pacing budget (Titanfall 2 VR): the runtime may put
 *    its wait into either call, so the report shows their sum against the display period and the
 *    split only as detail. The game's own time (from \c xrEndFrame to the next \c xrWaitFrame) is
 *    what the game controls.
 *  - A pause the wearer feels is a run of late frames, not one bad frame: the report names the
 *    worst frame and the worst run of the window, never only the mean.
 */
namespace vr {

//! One frame of the OpenXR frame loop as the loop timed it, in microseconds of one monotonic clock
struct FrameTiming {

	s64 waitStart = 0;     //!< just before \c xrWaitFrame
	s64 waitEnd = 0;       //!< just after \c xrWaitFrame returned - the frame's start
	s64 endStart = 0;      //!< just before \c xrEndFrame
	s64 endEnd = 0;        //!< just after \c xrEndFrame returned
	s64 displayPeriod = 0; //!< the runtime's predicted display period, i.e. one display slot
	bool shown = false;    //!< layers were submitted (the runtime wanted the frame rendered)

};

//! Frames against display slots
enum class SubmitVerdict {
	Unknown,         //!< not enough frames in the window
	Matched,         //!< one frame per expected slot, within tolerance
	OverSubmitting,  //!< more frames than display slots: nothing paces the loop
	UnderSubmitting  //!< fewer frames than expected slots: the game's work does not fit
};

//! Spacing of the frames in display slots
enum class CadenceVerdict {
	Unknown, //!< not enough frames in the window
	Even,    //!< a whole number of slots per frame, with little jitter
	Uneven   //!< frames land a fractional number of slots apart, or jitter: ghosting on head turns
};

//! The rate / cadence verdict of one window of frames
struct PacingReport {

	bool valid = false;

	double seconds = 0.0;     //!< the window, from the first frame's start to the last frame's start
	u32 frames = 0;           //!< frames submitted in the window
	u32 shown = 0;            //!< of those, frames with layers
	u32 divisor = 1;          //!< expected display slots per frame (the even-cadence cap)

	double periodMs = 0.0;        //!< one display slot
	double slotsPerSecond = 0.0;  //!< the headset's refresh rate
	double framesPerSecond = 0.0; //!< frames submitted per second

	double waitMeanMs = 0.0, waitMaxMs = 0.0;     //!< \c xrWaitFrame
	double endMeanMs = 0.0, endMaxMs = 0.0;       //!< \c xrEndFrame
	double budgetMeanMs = 0.0, budgetMaxMs = 0.0; //!< \c xrWaitFrame + \c xrEndFrame of one frame: the pacing budget
	double gameMeanMs = 0.0, gameMaxMs = 0.0;     //!< from \c xrEndFrame to the next \c xrWaitFrame: the game's own work

	double intervalMeanMs = 0.0; //!< between the starts of consecutive frames
	double slotsPerFrame = 0.0;  //!< \ref intervalMeanMs over \ref periodMs
	double cadenceSdMs = 0.0;    //!< standard deviation of the interval
	double worstFrameMs = 0.0;   //!< the longest interval
	u32 worstRunFrames = 0;      //!< the longest run of consecutive late frames (over 1.5 expected slots)
	double worstRunMs = 0.0;     //!< the time that run took

	SubmitVerdict verdict = SubmitVerdict::Unknown;
	CadenceVerdict cadence = CadenceVerdict::Unknown;

};

//! A frame that came far later than the others - fires on its own, between the reports
struct FrameGap {
	bool detected = false;
	double ms = 0.0;        //!< the interval from the previous frame's start
	double meanMs = 0.0;    //!< the mean interval it was judged against
	const char * phase = ""; //!< where the time went: "wait" (xrWaitFrame), "end" (xrEndFrame) or "game" (the game's work)
};

//! Accumulates frame timings and closes a report every window
class PacingWindow {

public:

	explicit PacingWindow(s64 windowUs = 3000000);

	//! Expected display slots per frame: 1 for every slot, 2 for every second slot, ...
	void setDivisor(u32 divisor);
	u32 divisor() const { return m_divisor; }

	/*!
	 * \brief Feed a finished frame
	 *
	 * \param gap receives the frame-gap verdict for this frame (optional)
	 * \return true when the window closed with this frame and \ref report() is new
	 */
	bool add(const FrameTiming & frame, FrameGap * gap = nullptr);

	/*!
	 * \brief Close the window now (the session ends)
	 *
	 * \return true when a report came out (at least two frames in the window)
	 */
	bool flush();

	//! The last closed window; \c valid is false before the first one
	const PacingReport & report() const { return m_report; }

	//! Windows closed so far
	u32 reports() const { return m_reports; }

	//! Forget the previous frame and the open window (a new session)
	void reset();

private:

	//! What the open window has seen so far
	struct Window {
		s64 start = 0;             //!< waitEnd of the first frame
		s64 last = 0;              //!< waitEnd of the last frame
		u32 frames = 0;
		u32 shown = 0;
		double periodSum = 0.0;
		double waitSum = 0.0, waitMax = 0.0;
		double endSum = 0.0, endMax = 0.0;
		double budgetSum = 0.0, budgetMax = 0.0;
		double gameSum = 0.0, gameMax = 0.0;
		u32 gameSamples = 0;
		u32 intervals = 0;
		double intervalSum = 0.0, intervalSquares = 0.0, intervalMax = 0.0;
		u32 runFrames = 0, worstRunFrames = 0;
		double runMs = 0.0, worstRunMs = 0.0;
	};

	void close();

	s64 m_windowUs;
	u32 m_divisor = 1;
	Window m_window;
	bool m_havePrevious = false;
	FrameTiming m_previous;
	PacingReport m_report;
	u32 m_reports = 0;

};

//! "MATCHED", "OVER-SUBMITTING", "UNDER-SUBMITTING" or "unknown"
const char * verdictName(SubmitVerdict verdict);

//! "EVEN", "UNEVEN" or "unknown"
const char * cadenceName(CadenceVerdict cadence);

//! The report as one log line (without the "[vr] rate:" prefix)
std::string formatReport(const PacingReport & report);

} // namespace vr

#endif // ARX_VR_FRAMEPACING_H
