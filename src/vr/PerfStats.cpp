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

#include "vr/PerfStats.h"

#include <algorithm>
#include <cstdio>

namespace vr {

namespace {

const s64 SecondUs = 1000000;

double usToMs(s64 us) {
	return double(us) / 1000.0;
}

//! " | other=0.12 input=0.03 ..." with the given precision
void appendPhases(std::string & line, const std::array<double, PerfPhaseCount> & phaseMs, const char * format) {
	line += " |";
	char buffer[48];
	for(size_t i = 0; i < PerfPhaseCount; i++) {
		std::snprintf(buffer, sizeof(buffer), format, perfPhaseName(PerfPhase(i)), phaseMs[i]);
		line += buffer;
	}
}

} // anonymous namespace

const char * perfPhaseName(PerfPhase phase) {
	switch(phase) {
		case PerfPhase::Other:     return "other";
		case PerfPhase::Input:     return "input";
		case PerfPhase::Channel:   return "channel";
		case PerfPhase::Load:      return "load";
		case PerfPhase::Save:      return "save";
		case PerfPhase::Update:    return "update";
		case PerfPhase::Script:    return "script";
		case PerfPhase::Physics:   return "physics";
		case PerfPhase::Particles: return "particles";
		case PerfPhase::Render:    return "render";
		case PerfPhase::Textures:  return "textures";
		case PerfPhase::Present:   return "present";
		case PerfPhase::Limit:     return "limit";
		case PerfPhase::VrWait:    return "vrwait";
		case PerfPhase::VrEnd:     return "vrend";
		case PerfPhase::Count:     break;
	}
	return "?";
}

bool PerfMonitor::add(const PerfFrame & frame, PerfHitch * hitch) {

	u32 second = u32(std::max(frame.start, s64(0)) / SecondUs);
	bool closed = false;
	if(m_open.frames != 0 && second != m_open.second) {
		close();
		closed = true;
	}

	Open & o = m_open;
	if(o.frames == 0) {
		o.second = second;
		o.durations.reserve(256);
	}
	o.frames++;
	if(frame.vr) {
		o.vrFrames++;
	}
	if(frame.period > 0) {
		o.periodSum += frame.period;
		o.periodFrames++;
	}
	o.divisor = std::max(frame.divisor, u32(1));
	o.missed += frame.missed;
	o.uploads += frame.uploads;
	for(size_t i = 0; i < PerfPhaseCount; i++) {
		o.phases[i] += frame.phases[i];
	}
	o.durations.push_back(frame.duration);

	if(hitch) {
		*hitch = PerfHitch();
	}
	s64 expected = frame.expected();
	if(expected > 0 && double(frame.duration) > HitchFactor * double(expected)) {
		o.hitches++;
		if(hitch) {
			hitch->detected = true;
			hitch->frame = m_frames;
			hitch->atS = double(frame.start) / double(SecondUs);
			hitch->ms = usToMs(frame.duration);
			hitch->periodMs = usToMs(frame.period);
			hitch->slots = double(frame.duration) / double(frame.period);
			hitch->missed = frame.missed;
			hitch->uploads = frame.uploads;
			size_t worst = 0;
			for(size_t i = 0; i < PerfPhaseCount; i++) {
				hitch->phaseMs[i] = usToMs(frame.phases[i]);
				if(frame.phases[i] > frame.phases[worst]) {
					worst = i;
				}
			}
			hitch->cause = PerfPhase(worst);
		}
	}

	m_frames++;

	return closed;
}

bool PerfMonitor::flush() {
	if(m_open.frames == 0) {
		return false;
	}
	close();
	return true;
}

void PerfMonitor::close() {

	Open & o = m_open;
	PerfSecond s;

	s.valid = true;
	s.second = o.second;
	s.frames = o.frames;
	s.vr = o.vrFrames == o.frames;
	s.divisor = o.divisor;
	s.missed = o.missed;
	s.hitches = o.hitches;
	s.uploads = o.uploads;
	s.periodMs = o.periodFrames ? usToMs(o.periodSum) / double(o.periodFrames) : 0.0;

	std::vector<s64> & d = o.durations;
	s64 sum = 0;
	for(s64 duration : d) {
		sum += duration;
	}
	s.meanMs = usToMs(sum) / double(d.size());
	s.minMs = usToMs(*std::min_element(d.begin(), d.end()));
	s.maxMs = usToMs(*std::max_element(d.begin(), d.end()));
	// Nearest rank: the smallest frame time that at least 95 % of the frames do not exceed
	size_t rank = (d.size() * 95 + 99) / 100;
	std::nth_element(d.begin(), d.begin() + (rank - 1), d.end());
	s.p95Ms = usToMs(d[rank - 1]);

	for(size_t i = 0; i < PerfPhaseCount; i++) {
		s.phaseMs[i] = usToMs(o.phases[i]) / double(o.frames);
	}

	m_second = s;

	// Keep the buffer's capacity for the next second
	d.clear();
	o.second = 0;
	o.frames = 0;
	o.vrFrames = 0;
	o.periodSum = 0;
	o.periodFrames = 0;
	o.divisor = 1;
	o.missed = 0;
	o.hitches = 0;
	o.uploads = 0;
	o.phases = { };
}

std::string formatSecond(const PerfSecond & s) {

	char buffer[256];
	std::snprintf(buffer, sizeof(buffer),
	              "S t=%u frames=%u min=%.2f mean=%.2f p95=%.2f max=%.2f hz=%.1f period=%.2f div=%u vr=%d"
	              " missed=%u hitches=%u",
	              s.second, s.frames, s.minMs, s.meanMs, s.p95Ms, s.maxMs,
	              s.periodMs > 0.0 ? 1000.0 / s.periodMs : 0.0, s.periodMs, s.divisor, s.vr ? 1 : 0,
	              s.missed, s.hitches);

	std::string line = buffer;
	appendPhases(line, s.phaseMs, " %s=%.2f");
	std::snprintf(buffer, sizeof(buffer), " | uploads=%u self=%.1f selfmax=%.0f", s.uploads, s.selfUs, s.selfMaxUs);
	line += buffer;

	return line;
}

std::string formatHitch(const PerfHitch & h) {

	char buffer[256];
	std::snprintf(buffer, sizeof(buffer),
	              "H t=%.3f frame=%llu ms=%.1f slots=%.1f period=%.2f missed=%u uploads=%u cause=%s",
	              h.atS, static_cast<unsigned long long>(h.frame), h.ms, h.slots, h.periodMs, h.missed,
	              h.uploads, perfPhaseName(h.cause));

	std::string line = buffer;
	appendPhases(line, h.phaseMs, " %s=%.1f");

	return line;
}

} // namespace vr
