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

#include "vr/MarkMoment.h"

#include <cstdio>

#include "vr/VoiceNote.h"

namespace vr {

namespace {

//! A UTC time split into its calendar fields
struct UtcTime {
	s64 year = 1970;
	int month = 1;
	int day = 1;
	int hour = 0;
	int minute = 0;
	int second = 0;
};

/*!
 * \a unixSeconds as a UTC calendar date and time, whatever the machine's time zone (the days-to-civil algorithm of
 * the proleptic Gregorian calendar: eras of 400 years starting on March 1st)
 */
UtcTime utcTime(s64 unixSeconds) {

	s64 days = unixSeconds / 86400;
	s64 seconds = unixSeconds % 86400;
	if(seconds < 0) {
		seconds += 86400;
		days--;
	}

	const s64 z = days + 719468;
	const s64 era = (z >= 0 ? z : z - 146096) / 146097;
	const s64 dayOfEra = z - era * 146097;
	const s64 yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
	const s64 dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
	const s64 monthIndex = (5 * dayOfYear + 2) / 153;

	UtcTime time;
	time.day = int(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
	time.month = int(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
	time.year = yearOfEra + era * 400 + (time.month <= 2 ? 1 : 0);
	time.hour = int(seconds / 3600);
	time.minute = int(seconds / 60 % 60);
	time.second = int(seconds % 60);
	return time;
}

//! \a text with every control character as a space: one line whatever it holds
std::string oneLine(const std::string & text) {
	std::string result = text;
	for(char & c : result) {
		if(static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
			c = ' ';
		}
	}
	return result;
}

//! \a ms as <minutes>:<seconds> - minutes without padding, whole seconds rounded down; a negative time is 0:00
std::string minutesSeconds(s64 ms) {
	const s64 seconds = ms > 0 ? ms / 1000 : 0;
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%lld:%02d", static_cast<long long>(seconds / 60), int(seconds % 60));
	return buffer;
}

} // anonymous namespace

bool markAllowed(bool replayPlaying) {
	return !replayPlaying;
}

bool noteMicrophoneAllowed(bool simulatedHeadset) {
	return !simulatedHeadset;
}

MarkNotice markNotice(const MarkNoticeState & state, std::string & text) {
	if(state.recording && state.recordingMs >= VoiceNote::minMs) {
		text = noteIndicatorText(state.recordingMs);
		return MarkNotice::Recording;
	}
	if(state.saving) {
		text = noteIndicatorText(state.savingLengthMs);
		return MarkNotice::Recording;
	}
	if(state.sinceSavedMs >= 0 && state.sinceSavedMs < NoteSavedMs) {
		text = noteSavedText(state.savedLengthMs);
		return MarkNotice::Saved;
	}
	if(state.sinceMarkedMs >= 0 && state.sinceMarkedMs < MarkNoticeMs) {
		text = markNoticeText(state.markedNumber);
		return MarkNotice::Marked;
	}
	text.clear();
	return MarkNotice::None;
}

std::string markNoticeText(u32 number) {
	return "Moment marked (#" + std::to_string(number) + ")";
}

std::string noteIndicatorText(s64 elapsedMs) {
	return std::string(MicrophoneMark) + " " + minutesSeconds(elapsedMs);
}

std::string noteSavedText(s64 lengthMs) {
	return std::string(MicrophoneMark) + " Note saved (" + minutesSeconds(lengthMs) + ")";
}

std::string markFolderName(s64 unixSeconds, u32 number) {
	const UtcTime time = utcTime(unixSeconds);
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%04lld%02d%02dT%02d%02d%02dZ-%u", static_cast<long long>(time.year),
	              time.month, time.day, time.hour, time.minute, time.second, unsigned(number));
	return buffer;
}

std::string formatMarkMeta(const MarkMeta & meta) {

	const UtcTime time = utcTime(meta.unixSeconds);
	char when[32];
	std::snprintf(when, sizeof(when), "%04lld-%02d-%02dT%02d:%02d:%02dZ", static_cast<long long>(time.year),
	              time.month, time.day, time.hour, time.minute, time.second);
	char position[96];
	std::snprintf(position, sizeof(position), "%.1f %.1f %.1f", double(meta.position.x), double(meta.position.y),
	              double(meta.position.z));
	char yaw[32];
	std::snprintf(yaw, sizeof(yaw), "%.1f", double(meta.yaw));

	std::string text = "# moment mark (#551)\n";
	text += "mark=" + std::to_string(meta.number) + "\n";
	text += "time=" + std::string(when) + "\n";
	text += "area=" + std::to_string(meta.area) + "\n";
	text += "position=" + std::string(position) + "\n";
	text += "yaw=" + std::string(yaw) + "\n";
	text += "build=" + oneLine(meta.build) + "\n";
	text += "state=" + oneLine(meta.state) + "\n";
	text += "session=" + oneLine(meta.session) + "\n";
	return text;
}

} // namespace vr
