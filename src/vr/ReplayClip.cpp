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

#include "vr/ReplayClip.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace vr {

namespace {

//! A line this close below the position counts as reached: a clip's times are whole milliseconds of display
//! times that were not (90 Hz: 55555 us is written as 56 ms), so a frame's line is due at its own frame, never
//! the one after; the next frame's line is a whole period further
const u64 ClipSlackUs = 500;

const char * const ClipHandNames[2] = { "left", "right" };

//! A number with \a decimals places; a negative zero is written as zero, so it reads back the same
void formatNumber(std::string & out, float value, int decimals) {
	char buffer[48];
	int n = std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, double(value));
	if(n <= 0 || size_t(n) >= sizeof(buffer)) {
		out += '0';
		return;
	}
	bool zero = buffer[0] == '-';
	for(int i = 1; zero && i < n; i++) {
		zero = buffer[i] == '0' || buffer[i] == '.';
	}
	out.append(buffer + (zero ? 1 : 0), size_t(n - (zero ? 1 : 0)));
}

void formatPose(std::string & out, const SimPose & pose) {
	formatNumber(out, pose.position.x, 4);
	out += ' ';
	formatNumber(out, pose.position.y, 4);
	out += ' ';
	formatNumber(out, pose.position.z, 4);
	out += ' ';
	formatNumber(out, pose.angles.x, 2);
	out += ' ';
	formatNumber(out, pose.angles.y, 2);
	out += ' ';
	formatNumber(out, pose.angles.z, 2);
}

//! The game's two-axis actions: both values are always written (vr/VRActions.cpp has one, the thumbstick)
bool vectorAction(std::string_view name) {
	return name == "thumbstick" || name == "trackpad" || name == "joystick";
}

void appendMs(std::string & out, u64 timeUs) {
	char buffer[32];
	const int n = std::snprintf(buffer, sizeof(buffer), "@%llums ", static_cast<unsigned long long>((timeUs + 500) / 1000));
	out.append(buffer, n > 0 ? size_t(n) : 0);
}

} // anonymous namespace

// --- The text form ------------------------------------------------------------------------------

std::string formatReplayHeader(const ReplayHeader & header) {
	std::string text = "# arx-replay 1: a VR session as a .vrsim clip (#550) - scripts/dev/replay.ps1 plays it\n";
	text += "# build: " + header.build + "\n";
	text += "# save: " + (header.save.empty() ? std::string("none") : header.save) + "\n";
	text += "# seed: " + std::to_string(header.seed) + "\n";
	char rate[32];
	std::snprintf(rate, sizeof(rate), "%g", double(header.rate));
	text += std::string("# rate: ") + rate + "\n";
	if(!header.mark.empty()) {
		text += "# mark: " + header.mark + " " + std::to_string(header.markMs) + "\n";
	}
	if(header.ipd != 0.f) {
		text += "ipd ";
		formatNumber(text, header.ipd, 4);
		text += '\n';
	}
	if(header.floor != 0.f) {
		text += "floor ";
		formatNumber(text, header.floor, 4);
		text += '\n';
	}
	return text;
}

void formatReplayFrameText(const ReplayFrame & frame, ReplayFrameText & text) {

	// The slots of the last call are written over (their strings keep their room): no allocation in steady state
	const SimRig & rig = frame.rig;
	size_t slot = 0;
	auto add = [&](std::string_view key, std::string_view command) {
		if(slot < text.keys.size()) {
			text.keys[slot].assign(key);
			text.commands[slot].assign(command);
		} else {
			text.keys.emplace_back(key);
			text.commands.emplace_back(command);
		}
		slot++;
	};

	// Two strings reused for every command: their room grows once (the recorder keeps the text between frames)
	std::string & key = text.scratchKey;
	std::string & command = text.scratchCommand;
	command = "head ";
	formatPose(command, rig.head);
	add("head", command);

	for(size_t hand = 0; hand < 2; hand++) {
		key = "hand ";
		key += ClipHandNames[hand];
		command = key;
		command += ' ';
		formatPose(command, rig.hands[hand]);
		add(key, command);
	}
	for(size_t hand = 0; hand < 2; hand++) {
		key = "hand ";
		key += ClipHandNames[hand];
		key += " aim";
		command = key;
		command += ' ';
		formatPose(command, rig.aims[hand]);
		add(key, command);
	}
	for(size_t hand = 0; hand < 2; hand++) {
		key = "hand ";
		key += ClipHandNames[hand];
		command = key;
		command += (rig.handTracked[hand] ? " on" : " off");
		key += " tracked";
		add(key, command);
	}
	for(size_t hand = 0; hand < 2; hand++) {
		key = "hand ";
		key += ClipHandNames[hand];
		key += " fingers";
		command = key;
		command += (rig.fingers[hand].joints ? " on" : " off");
		key += " joints";
		add(key, command);
		key.resize(key.size() - 7); // "hand <side> fingers" again
		command = key;
		command += ' ';
		const SimFingers & fingers = rig.fingers[hand];
		const float curls[5] = { fingers.curl.x, fingers.curl.y, fingers.curl.z, fingers.outer.x, fingers.outer.y };
		for(size_t i = 0; i < 5; i++) {
			if(i) {
				command += ' ';
			}
			formatNumber(command, curls[i], 2);
		}
		add(key, command);
	}

	command = "profile ";
	command += rig.profile.empty() ? std::string_view("none") : std::string_view(rig.profile);
	add("profile", command);
	command = "session ";
	command += (!rig.worn ? "idle" : rig.focused ? "focused" : "visible");
	add("session", command);

	for(const auto & [name, values] : rig.actions) {
		const bool vector = vectorAction(name);
		for(size_t hand = 0; hand < 2; hand++) {
			key = "action ";
			key += name;
			key += ' ';
			key += ClipHandNames[hand];
			command = key;
			command += ' ';
			formatNumber(command, values[hand].x, 2);
			if(vector) {
				command += ' ';
				formatNumber(command, values[hand].y, 2);
			}
			add(key, command);
		}
	}

	command = "player ";
	formatNumber(command, rig.player.position.x, 2);
	command += ' ';
	formatNumber(command, rig.player.position.y, 2);
	command += ' ';
	formatNumber(command, rig.player.position.z, 2);
	command += ' ';
	formatNumber(command, rig.player.yaw, 2);
	command += ' ';
	command += std::to_string(rig.player.area);
	add("player", command);
	text.keys.resize(slot);
	text.commands.resize(slot);
}

std::string formatReplayFrame(const ReplayFrame & frame, const ReplayFrameText * previous, ReplayFrameText & text) {
	std::string line;
	formatReplayFrame(frame, previous, text, line);
	return line;
}

void formatReplayFrame(const ReplayFrame & frame, const ReplayFrameText * previous, ReplayFrameText & text, std::string & line) {

	formatReplayFrameText(frame, text);

	line.clear();
	appendMs(line, frame.timeUs);
	bool first = true;
	size_t cursor = 0;
	for(size_t i = 0; i < text.keys.size(); i++) {
		if(previous) {
			// The same key in the previous frame, usually at the same place
			size_t found = previous->keys.size();
			for(size_t k = 0; k < previous->keys.size(); k++) {
				size_t j = (cursor + k) % previous->keys.size();
				if(previous->keys[j] == text.keys[i]) {
					found = j;
					break;
				}
			}
			if(found < previous->keys.size()) {
				cursor = found + 1;
				if(previous->commands[found] == text.commands[i]) {
					continue;
				}
			}
		}
		if(!first) {
			line += "; ";
		}
		first = false;
		line += text.commands[i];
	}
	if(first) {
		// Nothing changed: the line still marks the frame (its time), with the head as it is
		line += text.commands[0];
	}
	line += '\n';
}

// --- The recorder's gate ------------------------------------------------------------------------

RecordGate::Action RecordGate::step(bool session, bool views, bool playable) {
	if(!session) {
		if(m_recording) {
			m_recording = false;
			return Action::Finish;
		}
		return Action::Skip;
	}
	if(!views) {
		return Action::Skip; // tracking lost for a frame: nothing to record, the recording goes on
	}
	if(m_recording) {
		return Action::Record;
	}
	if(!playable) {
		return Action::Skip;
	}
	m_recording = true;
	return Action::Start;
}

std::string formatReplayFrame(const ReplayFrame & frame, const ReplayFrame * previous) {
	ReplayFrameText text;
	if(!previous) {
		return formatReplayFrame(frame, nullptr, text);
	}
	ReplayFrameText before;
	formatReplayFrameText(*previous, before);
	return formatReplayFrame(frame, &before, text);
}

// --- The drift ----------------------------------------------------------------------------------

void ReplayDrift::update(const SimPlayer & recorded, const SimPlayer & replayed) {
	if(recorded.area <= 0 || replayed.area <= 0 || recorded.area != replayed.area) {
		return;
	}
	frames++;
	current = glm::length(recorded.position - replayed.position) / 100.f; // 1 m = 100 engine units
	max = std::max(max, current);
}

// --- The streaming clip player ------------------------------------------------------------------

namespace {

std::string_view trimView(std::string_view text) {
	while(!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
		text.remove_prefix(1);
	}
	while(!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
		text.remove_suffix(1);
	}
	return text;
}

//! "# key: value" of a clip header; false for any other comment
bool headerField(std::string_view line, std::string_view & key, std::string_view & value) {
	if(line.size() < 2 || line[0] != '#') {
		return false;
	}
	std::string_view rest = trimView(line.substr(1));
	size_t colon = rest.find(':');
	if(colon == std::string_view::npos) {
		return false;
	}
	key = trimView(rest.substr(0, colon));
	value = trimView(rest.substr(colon + 1));
	return !key.empty() && key.find(' ') == std::string_view::npos;
}

bool parseU64(std::string_view text, u64 & value) {
	if(text.empty() || text.size() > 20) {
		return false;
	}
	u64 result = 0;
	for(char c : text) {
		if(c < '0' || c > '9') {
			return false;
		}
		const u64 digit = u64(c - '0');
		if(result > (std::numeric_limits<u64>::max() - digit) / 10) {
			return false;
		}
		result = result * 10 + digit;
	}
	value = result;
	return true;
}

bool parseFloatValue(std::string_view text, float & value) {
	std::string copy(text);
	char * end = nullptr;
	errno = 0;
	double parsed = std::strtod(copy.c_str(), &end);
	if(copy.empty() || end != copy.c_str() + copy.size() || errno != 0 || !std::isfinite(parsed)) {
		return false;
	}
	value = float(parsed);
	return true;
}

} // anonymous namespace

ClipPlayer::ClipPlayer(LineSource source, u64 fromUs, u64 toUs)
	: m_source(std::move(source))
	, m_fromUs(fromUs)
	, m_toUs(toUs)
{
	// The header: comments and set-up lines up to the first frame line, which is read ahead
	std::string line;
	while(readLine(line)) {
		std::string_view text = trimView(line);
		if(text.empty()) {
			continue;
		}
		if(text.front() == '@') {
			m_next.have = true;
			m_next.number = m_lineNumber;
			std::string_view rest;
			if(!parseFrameLine(text, m_next, rest)) {
				return;
			}
			return;
		}
		std::string_view key, value;
		if(headerField(text, key, value)) {
			if(key == "build") {
				m_header.build = std::string(value);
			} else if(key == "save") {
				m_header.save = std::string(value);
			} else if(key == "seed") {
				parseU64(value, m_header.seed);
			} else if(key == "rate") {
				parseFloatValue(value, m_header.rate);
			} else if(key == "mark") {
				size_t space = value.find_last_of(' ');
				u64 ms = 0;
				if(space != std::string_view::npos && parseU64(trimView(value.substr(space + 1)), ms)) {
					m_header.mark = std::string(trimView(value.substr(0, space)));
					m_header.markMs = s64(ms);
				} else {
					m_header.mark = std::string(value);
				}
			}
			continue;
		}
		if(text.front() == '#') {
			continue;
		}
		// A set-up line: applied with the first frame; the eye distance and the floor are known at once
		if(text.size() > 4 && text.substr(0, 4) == "ipd ") {
			parseFloatValue(trimView(text.substr(4)), m_header.ipd);
		} else if(text.size() > 6 && text.substr(0, 6) == "floor ") {
			parseFloatValue(trimView(text.substr(6)), m_header.floor);
		}
		m_setup.push_back(std::string(text));
	}
}

bool ClipPlayer::readLine(std::string & line) {
	if(!m_source || !m_source(line)) {
		return false;
	}
	m_lineNumber++;
	return true;
}

bool ClipPlayer::parseFrameLine(std::string_view text, Line & line, std::string_view & rest) {
	// "@<ms>ms <command>; ..." or "@<ms>ms anchor level <area> | anchor save"
	size_t pos = 1;
	while(pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
		pos++;
	}
	u64 ms = 0;
	if(pos == 1 || text.compare(pos, 2, "ms") != 0 || !parseU64(text.substr(1, pos - 1), ms) || ms > (u64(1) << 50)) {
		fail("line " + std::to_string(line.number) + ": expected @<ms>ms <command>; ..., got \""
		     + std::string(text.substr(0, std::min<size_t>(text.size(), 40))) + "\"");
		return false;
	}
	line.timeUs = ms * 1000;
	rest = trimView(text.substr(pos + 2));
	if(!rest.empty() && pos + 2 < text.size() && text[pos + 2] != ' ' && text[pos + 2] != '\t') {
		fail("line " + std::to_string(line.number) + ": expected a space after the time");
		return false;
	}
	line.anchor = false;
	line.anchorArea = -1;
	line.commands.clear();
	if(rest.size() >= 6 && rest.substr(0, 6) == "anchor" && (rest.size() == 6 || rest[6] == ' ')) {
		std::string_view what = trimView(rest.substr(6));
		if(what == "save") {
			line.anchor = true;
		} else if(what.size() > 6 && what.substr(0, 6) == "level ") {
			u64 area = 0;
			if(!parseU64(trimView(what.substr(6)), area) || area > 100000) {
				fail("line " + std::to_string(line.number) + ": anchor: expected level <area> or save");
				return false;
			}
			line.anchor = true;
			line.anchorArea = s32(area);
		} else {
			fail("line " + std::to_string(line.number) + ": anchor: expected level <area> or save");
			return false;
		}
	} else {
		line.commands = std::string(rest);
	}
	return true;
}

bool ClipPlayer::fetch() {
	const u64 previousUs = m_next.timeUs;
	m_next.have = false;
	std::string line;
	while(readLine(line)) {
		std::string_view text = trimView(line);
		if(text.empty() || text.front() == '#') {
			continue;
		}
		m_next.number = m_lineNumber;
		if(text.front() != '@') {
			fail("line " + std::to_string(m_lineNumber) + ": expected a frame line (@<ms>ms ...), got \""
			     + std::string(text.substr(0, std::min<size_t>(text.size(), 40))) + "\"");
			return false;
		}
		std::string_view rest;
		if(!parseFrameLine(text, m_next, rest)) {
			return false;
		}
		if(m_next.timeUs < previousUs) {
			fail("line " + std::to_string(m_lineNumber) + ": the time goes back");
			return false;
		}
		m_next.have = true;
		return true;
	}
	return false;
}

void ClipPlayer::fail(const std::string & what) {
	if(m_error.empty()) {
		m_error = what;
	}
	m_next.have = false;
	m_status.done = true;
	m_status.anchorsWaiting = 0;
	m_holding = false;
}

bool ClipPlayer::anchorSatisfied(const ReplayGame & game) const {
	if(!game.playable) {
		return false;
	}
	return m_next.anchorArea < 0 || game.player.area == m_next.anchorArea;
}

void ClipPlayer::stop() {
	m_status.done = true;
	m_status.anchorsWaiting = 0;
	m_holding = false;
}

void ClipPlayer::advance(u64 nowUs, const ReplayGame & game, Simulation & sim) {

	if(m_status.done) {
		return;
	}

	m_framed = false;

	if(!m_setupApplied) {
		m_setupApplied = true;
		for(const std::string & line : m_setup) {
			std::string error = sim.execute(line);
			if(!error.empty()) {
				fail("set-up: " + error);
				return;
			}
		}
	}

	if(!m_haveBase) {
		if(!m_next.have) {
			m_status.done = true;
			return;
		}
		m_haveBase = true;
		m_baseUs = std::max(m_fromUs, m_next.timeUs);
	}

	u64 position = 0;
	while(true) {

		if(!m_next.have) {
			// The clip's end (or an error, which said so already)
			m_status.done = true;
			position = m_baseUs + nowUs - std::min(m_heldUs, m_baseUs + nowUs);
			break;
		}

		if(m_holding) {
			// The position stays at the anchor: the hold grows with the clock, until the game is there - then the
			// play goes on from the anchor, the time held fixed
			m_heldUs = m_baseUs + nowUs - std::min(m_next.timeUs, m_baseUs + nowUs);
			if(!anchorSatisfied(game)) {
				m_status.anchorsWaiting = 1;
				position = m_next.timeUs;
				break;
			}
			m_holding = false;
			m_status.anchorsWaiting = 0;
			if(!fetch()) {
				continue;
			}
		}

		position = m_baseUs + nowUs - std::min(m_heldUs, m_baseUs + nowUs);
		if(m_next.timeUs > m_toUs) {
			// Nothing of the clip left inside the window
			m_status.done = true;
			position = m_toUs;
			break;
		}
		if(m_next.timeUs > position + ClipSlackUs) {
			break;
		}

		if(m_next.anchor) {
			if(m_next.timeUs < m_fromUs || anchorSatisfied(game)) {
				// Before the window, or the replayed game is already there: no hold
				if(!fetch()) {
					continue;
				}
				continue;
			}
			m_holding = true;
			m_status.anchorsWaiting = 1;
			m_heldUs = m_baseUs + nowUs - std::min(m_next.timeUs, m_baseUs + nowUs);
			position = m_next.timeUs;
			break;
		}

		// A frame line: its commands, in order
		const bool counted = m_next.timeUs >= m_fromUs;
		std::string error = sim.execute(m_next.commands);
		if(!error.empty()) {
			fail("line " + std::to_string(m_next.number) + ": " + error);
			break;
		}
		if(counted) {
			m_status.framesServed++;
			// The discrete changes among them
			size_t start = 0;
			const std::string & commands = m_next.commands;
			while(start <= commands.size()) {
				size_t end = commands.find(';', start);
				if(end == std::string::npos) {
					end = commands.size();
				}
				if(Simulation::isEventCommand(std::string_view(commands).substr(start, end - start))) {
					m_status.eventsReplayed++;
				}
				start = end + 1;
			}
		}
		m_frameTimeUs = m_next.timeUs;
		m_framed = true;
		if(!fetch()) {
			continue;
		}
	}

	if(!m_status.done && !m_holding && position >= m_toUs) {
		m_status.done = true;
		position = m_toUs;
	}
	m_status.positionMs = double(std::min(position, m_toUs)) / 1000.0;

	if(m_framed) {
		m_status.drift.update(sim.rig.player, game.player);
	}
}

// --- The ring -----------------------------------------------------------------------------------

namespace {

const u32 RingMagic = 0x31525241; // "ARR1"
const u32 RingVersion = 1;
const size_t RingActions = 16;
const size_t RingActionName = 32;
const size_t RingProfiles = 8;
const size_t RingProfileName = 128;

#pragma pack(push, 8)
struct RingHeader {
	u32 magic;
	u32 version;
	u32 headerSize;
	u32 frameSize;
	u32 capacity;
	u32 count;       //!< frames held
	u32 next;        //!< the slot the next frame goes into
	u32 seconds;
	u32 rate;
	u32 actionCount;
	u32 profileCount;
	u32 reserved;
	u64 seed;
	float rateHz;
	float ipd;
	float floor;
	u32 reserved2;
	char build[96];
	char save[160];
	char actions[RingActions][RingActionName];
	char profiles[RingProfiles][RingProfileName];
	u64 reserved3; //!< pads the header to a multiple of 16
};

struct RingFrame {
	u64 timeUs;
	float head[6];
	float hands[2][6];
	float aims[2][6];
	float curls[2][5];
	float player[4];
	s32 area;
	u8 tracked[2];
	u8 joints[2];
	u8 focused;
	u8 worn;
	u8 profile;      //!< 0 = none, else 1 + the index in the header's table
	u8 reserved;
	u32 actionMask;  //!< the actions present, bit i = actions[i]
	float actions[RingActions][2][2];
	u32 reserved2;
	u32 hash;        //!< FNV-1a of everything before it
};
#pragma pack(pop)

static_assert(sizeof(RingHeader) % 16 == 0, "the frames follow the header 16-aligned");

u32 fnv1a(const void * data, size_t size) {
	const unsigned char * bytes = static_cast<const unsigned char *>(data);
	u32 hash = 2166136261u;
	for(size_t i = 0; i < size; i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

void copyString(char * dst, size_t cap, std::string_view text) {
	size_t n = std::min(text.size(), cap - 1);
	std::memcpy(dst, text.data(), n);
	std::memset(dst + n, 0, cap - n);
}

//! A fixed buffer's text; false when it is not terminated or holds a control character
bool readString(const char * src, size_t cap, std::string & out) {
	size_t n = 0;
	while(n < cap && src[n] != '\0') {
		if((static_cast<unsigned char>(src[n]) < 0x20) || src[n] == '\x7f') {
			return false;
		}
		n++;
	}
	if(n == cap) {
		return false;
	}
	out.assign(src, n);
	return true;
}

void storePose(float * out, const SimPose & pose) {
	out[0] = pose.position.x;
	out[1] = pose.position.y;
	out[2] = pose.position.z;
	out[3] = pose.angles.x;
	out[4] = pose.angles.y;
	out[5] = pose.angles.z;
}

void loadPose(const float * in, SimPose & pose) {
	pose.position = Vec3f(in[0], in[1], in[2]);
	pose.angles = Vec3f(in[3], in[4], in[5]);
}

bool finite(const float * values, size_t count) {
	for(size_t i = 0; i < count; i++) {
		if(!std::isfinite(values[i])) {
			return false;
		}
	}
	return true;
}

//! The ring's header and frames of an image, validated; false when the image is no ring
struct RingView {
	const RingHeader * header = nullptr;
	const unsigned char * frames = nullptr;
	u32 capacity = 0;
	u32 count = 0;
	u32 next = 0;
	std::vector<std::string> actions;
	std::vector<std::string> profiles;
	ReplayHeader clipHeader;
};

bool openRing(const void * image, size_t size, RingView & view) {

	if(!image || size < sizeof(RingHeader)) {
		return false;
	}
	RingHeader header;
	std::memcpy(&header, image, sizeof(header)); // the image may be unaligned
	if(header.magic != RingMagic || header.version != RingVersion || header.headerSize != sizeof(RingHeader)
	   || header.frameSize != sizeof(RingFrame) || header.capacity == 0 || header.capacity > (1u << 20)
	   || header.actionCount > RingActions || header.profileCount > RingProfiles) {
		return false;
	}
	const size_t room = (size - sizeof(RingHeader)) / sizeof(RingFrame);
	view.header = static_cast<const RingHeader *>(image);
	view.frames = static_cast<const unsigned char *>(image) + sizeof(RingHeader);
	// A cut-off copy holds fewer slots than the header says: only the ones inside the image are read
	view.capacity = u32(std::min<size_t>(header.capacity, room));
	view.count = std::min(header.count, header.capacity);
	view.next = header.next % header.capacity;
	for(size_t i = 0; i < header.actionCount; i++) {
		std::string name;
		if(!readString(header.actions[i], RingActionName, name) || name.empty()) {
			return false;
		}
		view.actions.push_back(name);
	}
	for(size_t i = 0; i < header.profileCount; i++) {
		std::string name;
		if(!readString(header.profiles[i], RingProfileName, name)) {
			return false;
		}
		view.profiles.push_back(name);
	}
	if(!readString(header.build, sizeof(header.build), view.clipHeader.build)
	   || !readString(header.save, sizeof(header.save), view.clipHeader.save)) {
		return false;
	}
	view.clipHeader.seed = header.seed;
	view.clipHeader.rate = std::isfinite(header.rateHz) ? header.rateHz : 0.f;
	view.clipHeader.ipd = std::isfinite(header.ipd) ? header.ipd : 0.f;
	view.clipHeader.floor = std::isfinite(header.floor) ? header.floor : 0.f;
	return true;
}

//! The frame in slot \a slot as a ReplayFrame; false when its checksum or contents are off (torn, garbage, cut)
bool readFrame(const RingView & view, u32 slot, ReplayFrame & out) {

	if(slot >= view.capacity) {
		return false;
	}
	RingFrame frame;
	std::memcpy(&frame, view.frames + size_t(slot) * sizeof(RingFrame), sizeof(frame));
	if(fnv1a(&frame, offsetof(RingFrame, hash)) != frame.hash) {
		return false;
	}
	if(!finite(frame.head, 6) || !finite(&frame.hands[0][0], 12) || !finite(&frame.aims[0][0], 12)
	   || !finite(&frame.curls[0][0], 10) || !finite(frame.player, 4) || !finite(&frame.actions[0][0][0], RingActions * 4)
	   || frame.tracked[0] > 1 || frame.tracked[1] > 1 || frame.joints[0] > 1 || frame.joints[1] > 1
	   || frame.focused > 1 || frame.worn > 1 || frame.profile > view.profiles.size()
	   || (view.actions.size() < 32 && (frame.actionMask >> view.actions.size()) != 0)) {
		return false;
	}

	out.timeUs = frame.timeUs;
	SimRig & rig = out.rig;
	loadPose(frame.head, rig.head);
	for(size_t hand = 0; hand < 2; hand++) {
		loadPose(frame.hands[hand], rig.hands[hand]);
		loadPose(frame.aims[hand], rig.aims[hand]);
		rig.handTracked[hand] = frame.tracked[hand] != 0;
		rig.fingers[hand].joints = frame.joints[hand] != 0;
		rig.fingers[hand].curl = Vec3f(frame.curls[hand][0], frame.curls[hand][1], frame.curls[hand][2]);
		rig.fingers[hand].outer = Vec2f(frame.curls[hand][3], frame.curls[hand][4]);
	}
	rig.focused = frame.focused != 0;
	rig.worn = frame.worn != 0;
	rig.profile = frame.profile == 0 ? std::string() : view.profiles[frame.profile - 1];
	rig.actions.clear();
	for(size_t i = 0; i < view.actions.size(); i++) {
		if(frame.actionMask & (1u << i)) {
			rig.actions[view.actions[i]] = { Vec2f(frame.actions[i][0][0], frame.actions[i][0][1]),
			                                 Vec2f(frame.actions[i][1][0], frame.actions[i][1][1]) };
		}
	}
	rig.player.position = Vec3f(frame.player[0], frame.player[1], frame.player[2]);
	rig.player.yaw = frame.player[3];
	rig.player.area = frame.area;
	return true;
}

/*!
 * The frames of an image, oldest first, as far as they are whole: the leading ones a push may be tearing (the
 * oldest slot is the one being overwritten) are skipped, the first broken one after a good one ends the list
 */
std::vector<ReplayFrame> ringFrames(const RingView & view) {
	std::vector<ReplayFrame> frames;
	const u32 count = std::min(view.count, view.capacity);
	const u32 oldest = (view.next + view.capacity - count % view.capacity) % view.capacity;
	bool started = false;
	for(u32 i = 0; i < count; i++) {
		ReplayFrame frame;
		if(!readFrame(view, (oldest + i) % view.capacity, frame)) {
			if(started) {
				break;
			}
			continue;
		}
		if(started && frame.timeUs <= frames.back().timeUs) {
			break;
		}
		started = true;
		frames.push_back(std::move(frame));
	}
	return frames;
}

std::string clipOf(const ReplayHeader & header, const std::vector<ReplayFrame> & frames, size_t first, size_t last) {
	std::string text = formatReplayHeader(header);
	ReplayFrameText previous, current;
	for(size_t i = first; i < frames.size() && i <= last; i++) {
		text += formatReplayFrame(frames[i], i == first ? nullptr : &previous, current);
		std::swap(previous, current);
	}
	return text;
}

} // anonymous namespace

size_t ReplayRing::memorySize(u32 seconds, u32 rate) {
	const size_t capacity = size_t(std::max<u32>(seconds, 1)) * size_t(std::max<u32>(rate, 1)) + 1;
	return sizeof(RingHeader) + capacity * sizeof(RingFrame);
}

ReplayRing::ReplayRing(void * memory, size_t size, u32 seconds, u32 rate)
	: m_memory(memory)
	, m_size(size)
{
	if(!memory || size < sizeof(RingHeader) + sizeof(RingFrame)) {
		m_memory = nullptr;
		return;
	}
	RingHeader * header = static_cast<RingHeader *>(memory);
	std::memset(header, 0, sizeof(RingHeader));
	header->version = RingVersion;
	header->headerSize = sizeof(RingHeader);
	header->frameSize = sizeof(RingFrame);
	const size_t wanted = size_t(std::max<u32>(seconds, 1)) * size_t(std::max<u32>(rate, 1)) + 1;
	const size_t room = (size - sizeof(RingHeader)) / sizeof(RingFrame);
	header->capacity = u32(std::min(wanted, room));
	header->seconds = seconds;
	header->rate = rate;
	header->magic = RingMagic;
}

u32 ReplayRing::count() const {
	return m_memory ? static_cast<const RingHeader *>(m_memory)->count : 0;
}

void ReplayRing::setHeader(const ReplayHeader & clip) {
	if(!m_memory) {
		return;
	}
	RingHeader * header = static_cast<RingHeader *>(m_memory);
	copyString(header->build, sizeof(header->build), clip.build);
	copyString(header->save, sizeof(header->save), clip.save.empty() ? std::string_view("none") : std::string_view(clip.save));
	header->seed = clip.seed;
	header->rateHz = clip.rate;
	header->ipd = clip.ipd;
	header->floor = clip.floor;
}

void ReplayRing::push(const ReplayFrame & in) {

	if(!m_memory) {
		return;
	}
	RingHeader * header = static_cast<RingHeader *>(m_memory);
	const SimRig & rig = in.rig;

	RingFrame frame;
	std::memset(&frame, 0, sizeof(frame));
	frame.timeUs = in.timeUs;
	storePose(frame.head, rig.head);
	for(size_t hand = 0; hand < 2; hand++) {
		storePose(frame.hands[hand], rig.hands[hand]);
		storePose(frame.aims[hand], rig.aims[hand]);
		frame.tracked[hand] = rig.handTracked[hand] ? 1 : 0;
		frame.joints[hand] = rig.fingers[hand].joints ? 1 : 0;
		frame.curls[hand][0] = rig.fingers[hand].curl.x;
		frame.curls[hand][1] = rig.fingers[hand].curl.y;
		frame.curls[hand][2] = rig.fingers[hand].curl.z;
		frame.curls[hand][3] = rig.fingers[hand].outer.x;
		frame.curls[hand][4] = rig.fingers[hand].outer.y;
	}
	frame.focused = rig.focused ? 1 : 0;
	frame.worn = rig.worn ? 1 : 0;

	// The profile and the action names: tables in the header, grown as new ones come
	if(!rig.profile.empty()) {
		size_t index = header->profileCount;
		for(size_t i = 0; i < header->profileCount; i++) {
			if(std::strncmp(header->profiles[i], rig.profile.c_str(), RingProfileName) == 0) {
				index = i;
				break;
			}
		}
		if(index == header->profileCount && index < RingProfiles && rig.profile.size() < RingProfileName) {
			copyString(header->profiles[index], RingProfileName, rig.profile);
			header->profileCount++;
		}
		frame.profile = index < header->profileCount ? u8(index + 1) : 0;
	}
	for(const auto & [name, values] : rig.actions) {
		size_t index = header->actionCount;
		for(size_t i = 0; i < header->actionCount; i++) {
			if(std::strncmp(header->actions[i], name.c_str(), RingActionName) == 0) {
				index = i;
				break;
			}
		}
		if(index == header->actionCount) {
			if(index >= RingActions || name.empty() || name.size() >= RingActionName) {
				continue; // no room for another name: the action is left out of the ring
			}
			copyString(header->actions[index], RingActionName, name);
			header->actionCount++;
		}
		frame.actionMask |= 1u << index;
		frame.actions[index][0][0] = values[0].x;
		frame.actions[index][0][1] = values[0].y;
		frame.actions[index][1][0] = values[1].x;
		frame.actions[index][1][1] = values[1].y;
	}

	frame.player[0] = rig.player.position.x;
	frame.player[1] = rig.player.position.y;
	frame.player[2] = rig.player.position.z;
	frame.player[3] = rig.player.yaw;
	frame.area = rig.player.area;
	frame.hash = fnv1a(&frame, offsetof(RingFrame, hash));

	// The frame first, the counters after it: a reader sees a whole frame or a checksum that fails
	unsigned char * frames = static_cast<unsigned char *>(m_memory) + sizeof(RingHeader);
	const u32 slot = header->next % header->capacity;
	std::memcpy(frames + size_t(slot) * sizeof(RingFrame), &frame, sizeof(frame));
	header->next = (slot + 1) % header->capacity;
	header->count = std::min(header->count + 1, header->capacity);
}

std::string replayRingClip(const void * image, size_t size) {
	RingView view;
	if(!openRing(image, size, view)) {
		return std::string();
	}
	const std::vector<ReplayFrame> frames = ringFrames(view);
	return clipOf(view.clipHeader, frames, 0, frames.empty() ? 0 : frames.size() - 1);
}

std::string replayRingClip(const void * image, size_t size, std::string_view mark, u64 markUs, u64 windowUs) {
	RingView view;
	if(!openRing(image, size, view)) {
		return std::string();
	}
	ReplayHeader header = view.clipHeader;
	header.mark = std::string(mark);
	header.markMs = s64((markUs + 500) / 1000);
	const std::vector<ReplayFrame> frames = ringFrames(view);
	if(frames.empty()) {
		return clipOf(header, frames, 0, 0);
	}
	// The last frame at or before the mark; none: the clip holds no frame
	size_t last = frames.size();
	for(size_t i = frames.size(); i > 0; i--) {
		if(frames[i - 1].timeUs <= markUs) {
			last = i - 1;
			break;
		}
	}
	if(last == frames.size()) {
		return formatReplayHeader(header);
	}
	// The last frame at or before the window's start, or the oldest kept
	const u64 startUs = markUs > windowUs ? markUs - windowUs : 0;
	size_t first = 0;
	for(size_t i = last + 1; i > 0; i--) {
		if(frames[i - 1].timeUs <= startUs) {
			first = i - 1;
			break;
		}
	}
	return clipOf(header, frames, first, last);
}

} // namespace vr
