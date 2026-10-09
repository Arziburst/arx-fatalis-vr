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

#include "vr/FramePacing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vr {

namespace {

//! More frames than this share of the display slots: nothing paces the loop
const double OverSubmitFactor = 1.05;

//! Fewer frames than this share of the expected slots: the game's work does not fit
const double UnderSubmitFactor = 0.95;

//! Even cadence: the mean slots per frame within this of a whole number ...
const double EvenMaxFraction = 0.1;

//! ... and the interval jitter (standard deviation) within this share of a display slot
const double EvenMaxSdSlots = 0.2;

//! A frame is late when its interval exceeds this many expected slots
const double LateSlots = 1.5;

//! A frame gap: an interval of at least this ...
const double GapMinMs = 80.0;

//! ... and at least this times the mean interval
const double GapMeanFactor = 2.5;

//! The open window's mean interval is trusted for the gap test from this many intervals on
const u32 GapMinIntervals = 10;

double ms(s64 us) {
	return double(us) / 1000.0;
}

} // anonymous namespace

PacingWindow::PacingWindow(s64 windowUs)
	: m_windowUs(windowUs)
{ }

void PacingWindow::setDivisor(u32 divisor) {
	m_divisor = std::max(divisor, u32(1));
}

void PacingWindow::reset() {
	m_window = Window();
	m_havePrevious = false;
}

bool PacingWindow::add(const FrameTiming & frame, FrameGap * gap) {

	Window & w = m_window;

	if(w.frames == 0) {
		w.start = frame.waitEnd;
	}
	w.last = frame.waitEnd;
	w.frames++;
	if(frame.shown) {
		w.shown++;
	}
	w.periodSum += ms(frame.displayPeriod);

	double wait = ms(frame.waitEnd - frame.waitStart);
	double end = ms(frame.endEnd - frame.endStart);
	w.waitSum += wait;
	w.waitMax = std::max(w.waitMax, wait);
	w.endSum += end;
	w.endMax = std::max(w.endMax, end);
	w.budgetSum += wait + end;
	w.budgetMax = std::max(w.budgetMax, wait + end);

	if(gap) {
		*gap = FrameGap();
	}

	if(m_havePrevious) {

		double game = ms(frame.waitStart - m_previous.endEnd);
		w.gameSum += game;
		w.gameMax = std::max(w.gameMax, game);
		w.gameSamples++;

		double interval = ms(frame.waitEnd - m_previous.waitEnd);

		// The gap test judges against what the window has seen, or the last report, or the slot
		double mean;
		if(w.intervals >= GapMinIntervals) {
			mean = w.intervalSum / double(w.intervals);
		} else if(m_report.valid) {
			mean = m_report.intervalMeanMs;
		} else {
			mean = ms(frame.displayPeriod) * double(m_divisor);
		}
		if(gap && mean > 0.0 && interval >= std::max(GapMinMs, GapMeanFactor * mean)) {
			gap->detected = true;
			gap->ms = interval;
			gap->meanMs = mean;
			double previousEnd = ms(m_previous.endEnd - m_previous.endStart);
			if(wait >= game && wait >= previousEnd) {
				gap->phase = "wait";
			} else if(game >= previousEnd) {
				gap->phase = "game";
			} else {
				gap->phase = "end";
			}
		}

		w.intervals++;
		w.intervalSum += interval;
		w.intervalSquares += interval * interval;
		w.intervalMax = std::max(w.intervalMax, interval);

		// A run of late frames: consecutive intervals over the late threshold
		double late = LateSlots * double(m_divisor) * ms(frame.displayPeriod);
		if(frame.displayPeriod > 0 && interval > late) {
			w.runFrames++;
			w.runMs += interval;
			if(w.runFrames > w.worstRunFrames || (w.runFrames == w.worstRunFrames && w.runMs > w.worstRunMs)) {
				w.worstRunFrames = w.runFrames;
				w.worstRunMs = w.runMs;
			}
		} else {
			w.runFrames = 0;
			w.runMs = 0.0;
		}

	}

	m_previous = frame;
	m_havePrevious = true;

	if(frame.waitEnd - w.start >= m_windowUs) {
		close();
		return true;
	}

	return false;
}

bool PacingWindow::flush() {
	if(m_window.frames < 2) {
		m_window = Window();
		return false;
	}
	close();
	return true;
}

void PacingWindow::close() {

	const Window & w = m_window;
	PacingReport r;

	r.valid = w.frames >= 2;
	r.frames = w.frames;
	r.shown = w.shown;
	r.divisor = m_divisor;
	r.periodMs = w.frames ? w.periodSum / double(w.frames) : 0.0;
	r.slotsPerSecond = r.periodMs > 0.0 ? 1000.0 / r.periodMs : 0.0;

	r.waitMeanMs = w.frames ? w.waitSum / double(w.frames) : 0.0;
	r.waitMaxMs = w.waitMax;
	r.endMeanMs = w.frames ? w.endSum / double(w.frames) : 0.0;
	r.endMaxMs = w.endMax;
	r.budgetMeanMs = w.frames ? w.budgetSum / double(w.frames) : 0.0;
	r.budgetMaxMs = w.budgetMax;
	r.gameMeanMs = w.gameSamples ? w.gameSum / double(w.gameSamples) : 0.0;
	r.gameMaxMs = w.gameMax;

	if(w.intervals) {
		r.seconds = w.intervalSum / 1000.0;
		r.intervalMeanMs = w.intervalSum / double(w.intervals);
		r.framesPerSecond = r.intervalMeanMs > 0.0 ? 1000.0 / r.intervalMeanMs : 0.0;
		r.slotsPerFrame = r.periodMs > 0.0 ? r.intervalMeanMs / r.periodMs : 0.0;
		double variance = w.intervalSquares / double(w.intervals) - r.intervalMeanMs * r.intervalMeanMs;
		r.cadenceSdMs = variance > 0.0 ? std::sqrt(variance) : 0.0;
		r.worstFrameMs = w.intervalMax;
		r.worstRunFrames = w.worstRunFrames;
		r.worstRunMs = w.worstRunMs;
	}

	if(w.intervals && r.periodMs > 0.0) {
		double expected = r.slotsPerSecond / double(m_divisor);
		if(r.framesPerSecond > r.slotsPerSecond * OverSubmitFactor) {
			r.verdict = SubmitVerdict::OverSubmitting;
		} else if(r.framesPerSecond < expected * UnderSubmitFactor) {
			r.verdict = SubmitVerdict::UnderSubmitting;
		} else {
			r.verdict = SubmitVerdict::Matched;
		}
		double nearest = std::round(r.slotsPerFrame);
		bool wholeSlots = nearest >= 1.0 && std::abs(r.slotsPerFrame - nearest) <= EvenMaxFraction;
		bool steady = r.cadenceSdMs <= EvenMaxSdSlots * r.periodMs;
		r.cadence = (wholeSlots && steady) ? CadenceVerdict::Even : CadenceVerdict::Uneven;
	}

	if(r.valid) {
		m_report = r;
		m_reports++;
	}

	m_window = Window();
}

const char * verdictName(SubmitVerdict verdict) {
	switch(verdict) {
		case SubmitVerdict::Matched:         return "MATCHED";
		case SubmitVerdict::OverSubmitting:  return "OVER-SUBMITTING";
		case SubmitVerdict::UnderSubmitting: return "UNDER-SUBMITTING";
		default:                             return "unknown";
	}
}

const char * cadenceName(CadenceVerdict cadence) {
	switch(cadence) {
		case CadenceVerdict::Even:   return "EVEN";
		case CadenceVerdict::Uneven: return "UNEVEN";
		default:                     return "unknown";
	}
}

std::string formatReport(const PacingReport & r) {

	char buffer[512];
	std::snprintf(buffer, sizeof(buffer),
	              "%u frames in %.2f s = %.1f/s on %.1f slots/s (hmd %.2f ms), %u shown"
	              " | wait+end %.1f mean / %.1f max ms (wait %.1f, end %.1f; game %.1f mean / %.1f max)"
	              " | %.2f slots/frame, sd %.2f ms: %s CADENCE | %s"
	              " | worst frame %.1f ms, worst run %u frames %.0f ms",
	              r.frames, r.seconds, r.framesPerSecond, r.slotsPerSecond, r.periodMs, r.shown,
	              r.budgetMeanMs, r.budgetMaxMs, r.waitMeanMs, r.endMeanMs, r.gameMeanMs, r.gameMaxMs,
	              r.slotsPerFrame, r.cadenceSdMs, cadenceName(r.cadence), verdictName(r.verdict),
	              r.worstFrameMs, r.worstRunFrames, r.worstRunMs);

	std::string line = buffer;
	if(r.divisor > 1) {
		line += " (cap: every " + std::to_string(r.divisor) + ". slot)";
	}

	return line;
}

} // namespace vr
