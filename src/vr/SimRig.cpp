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

#include "vr/SimRig.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <utility>

#include <glm/gtc/constants.hpp>

namespace vr {

namespace {

const glm::vec3 AxisX(1.f, 0.f, 0.f);
const glm::vec3 AxisY(0.f, 1.f, 0.f);
const glm::vec3 AxisZ(0.f, 0.f, 1.f);

std::string lowercase(std::string_view text) {
	std::string result(text);
	for(char & c : result) {
		if(c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
	}
	return result;
}

std::vector<std::string_view> splitWords(std::string_view line) {
	std::vector<std::string_view> words;
	size_t pos = 0;
	while(pos < line.size()) {
		while(pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\r')) {
			pos++;
		}
		size_t end = pos;
		while(end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') {
			end++;
		}
		if(end > pos) {
			words.push_back(line.substr(pos, end - pos));
		}
		pos = end;
	}
	return words;
}

bool parseFloat(std::string_view text, float & value) {
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

bool parseExitCode(std::string_view text, int & value) {
	std::string copy(text);
	char * end = nullptr;
	errno = 0;
	long parsed = std::strtol(copy.c_str(), &end, 0);
	if(copy.empty() || end != copy.c_str() + copy.size() || errno != 0 || parsed < -2147483647L - 1 || parsed > 2147483647L) {
		return false;
	}
	value = int(parsed);
	return true;
}

bool parseFrame(std::string_view text, u64 & value) {
	if(text.empty() || text.size() > 12) {
		return false;
	}
	u64 result = 0;
	for(char c : text) {
		if(c < '0' || c > '9') {
			return false;
		}
		result = result * 10 + u64(c - '0');
	}
	value = result;
	return true;
}

std::string_view stripComment(std::string_view line) {
	size_t hash = line.find('#');
	if(hash != std::string_view::npos) {
		line = line.substr(0, hash);
	}
	while(!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
		line.remove_suffix(1);
	}
	while(!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
		line.remove_prefix(1);
	}
	return line;
}

bool validShotName(std::string_view name) {
	if(name.empty() || name.size() > 64) {
		return false;
	}
	for(char c : name) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		if(!ok) {
			return false;
		}
	}
	return true;
}

} // anonymous namespace

glm::quat SimPose::orientation() const {
	return glm::angleAxis(glm::radians(angles.x), AxisY) * glm::angleAxis(glm::radians(angles.y), AxisX)
	       * glm::angleAxis(glm::radians(angles.z), AxisZ);
}

Vec3f simAngles(const glm::quat & orientation) {
	// Yaw from where the head looks, pitch from how far up, roll from where "up" went around the view axis
	const Vec3f forward = orientation * Vec3f(0.f, 0.f, -1.f);
	const Vec3f up = orientation * Vec3f(0.f, 1.f, 0.f);
	const float yaw = std::atan2(-forward.x, -forward.z);
	const float pitch = std::asin(std::clamp(forward.y, -1.f, 1.f));
	const glm::quat level = glm::angleAxis(yaw, AxisY) * glm::angleAxis(pitch, AxisX);
	const Vec3f localUp = glm::inverse(level) * up;
	const float roll = std::atan2(-localUp.x, localUp.y);
	return Vec3f(glm::degrees(yaw), glm::degrees(pitch), glm::degrees(roll));
}

SimView::SimView() {
	// Research row F-165 (OpenMW VR's [Stereo View] defaults): the left eye sees more to the left
	fov[0].left = -0.86f;
	fov[0].right = 0.78f;
	fov[0].up = 0.8f;
	fov[0].down = -0.8f;
	fov[1].left = -0.78f;
	fov[1].right = 0.86f;
	fov[1].up = 0.8f;
	fov[1].down = -0.8f;
}

SimRig::SimRig()
	: profile("/interaction_profiles/valve/index_controller")
{
	// Hands held in front of the body, below the head
	hands[0].position = Vec3f(-0.2f, -0.45f, -0.35f);
	hands[1].position = Vec3f(0.2f, -0.45f, -0.35f);
}

std::array<SimEye, 2> simEyes(const SimView & view, const SimPose & head) {
	std::array<SimEye, 2> eyes;
	const glm::quat orientation = head.orientation();
	for(size_t i = 0; i < eyes.size(); i++) {
		const Vec3f offset((i == 0 ? -0.5f : 0.5f) * view.ipd, 0.f, 0.f);
		eyes[i].position = head.position + orientation * offset;
		eyes[i].orientation = orientation;
		eyes[i].fov = view.fov[i];
	}
	return eyes;
}

std::array<SimEye, 2> simEyesInHead(const SimView & view) {
	return simEyes(view, SimPose());
}

void poseInSpace(const Vec3f & basePosition, const glm::quat & baseOrientation, Vec3f & position,
                 glm::quat & orientation) {
	const glm::quat inverse = glm::inverse(baseOrientation);
	position = inverse * (position - basePosition);
	orientation = inverse * orientation;
}

std::string simProfilePath(std::string_view name) {
	const std::string lower = lowercase(name);
	if(lower == "index") {
		return "/interaction_profiles/valve/index_controller";
	} else if(lower == "touch") {
		return "/interaction_profiles/oculus/touch_controller";
	} else if(lower == "simple") {
		return "/interaction_profiles/khr/simple_controller";
	} else if(lower == "none") {
		return std::string();
	}
	return std::string(name);
}

struct Simulation::Command {
	enum Op {
		Rate, EyeSize, Ipd, Fov, Timeline, HeadPos, HeadRot, HandPos, HandRot, HandTracked, HandFingers, HandJoints,
		Profile, Action, Session, Shot, Mark, Record, Quit, Fault, Layers, SessionEnd, FaultExit,
	};
	Op op = Rate;
	size_t index = 0; //!< eye or hand; 2 = both hands
	std::string text;
	std::array<float, 5> values = { };
	size_t count = 0;
	bool flag = false;
	SimSessionEnd sessionEnd = SimSessionEnd::None;
	int exitCode = 0;
	bool rampable() const {
		return op == Ipd || op == HeadPos || op == HeadRot || op == HandPos || op == HandRot || op == HandFingers
		       || op == Action;
	}
};

std::string Simulation::parse(std::string_view line, Command & command) const {

	std::vector<std::string_view> words = splitWords(line);
	if(words.empty()) {
		return "empty command";
	}

	const std::string word = lowercase(words[0]);
	auto numbers = [&](size_t first, size_t count, const char * what) -> std::string {
		if(words.size() != first + count) {
			return word + ": expected " + what;
		}
		for(size_t i = 0; i < count; i++) {
			if(!parseFloat(words[first + i], command.values[i])) {
				return word + ": \"" + std::string(words[first + i]) + "\" is not a number";
			}
		}
		command.count = count;
		return std::string();
	};
	auto side = [&](size_t i, bool both) -> bool {
		if(i >= words.size()) {
			return false;
		}
		const std::string name = lowercase(words[i]);
		if(name == "left") {
			command.index = 0;
		} else if(name == "right") {
			command.index = 1;
		} else if(both && name == "both") {
			command.index = 2;
		} else {
			return false;
		}
		return true;
	};

	if(word == "rate") {
		command.op = Command::Rate;
		std::string error = numbers(1, 1, "a rate in Hz (0 = no pacing)");
		if(error.empty() && (command.values[0] < 0.f || command.values[0] > 1000.f)) {
			return "rate: expected 0-1000 Hz";
		}
		return error;
	}

	if(word == "eye-size") {
		command.op = Command::EyeSize;
		std::string error = numbers(1, 2, "<width> <height>");
		if(error.empty() && (command.values[0] < 16.f || command.values[1] < 16.f
		                     || command.values[0] > 4096.f || command.values[1] > 4096.f)) {
			return "eye-size: expected 16-4096 pixels";
		}
		if(error.empty() && m_setupLocked) {
			return "eye-size: set-up only - the swapchains exist already";
		}
		return error;
	}

	if(word == "ipd") {
		command.op = Command::Ipd;
		std::string error = numbers(1, 1, "a distance in meters");
		if(error.empty() && std::abs(command.values[0]) > 0.5f) {
			return "ipd: expected at most 0.5 m";
		}
		return error;
	}

	if(word == "fov") {
		command.op = Command::Fov;
		if(!side(1, false)) {
			return "fov: expected left|right <left> <right> <up> <down>";
		}
		std::string error = numbers(2, 4, "left|right <left> <right> <up> <down> in degrees");
		if(error.empty()) {
			for(size_t i = 0; i < 4; i++) {
				if(std::abs(command.values[i]) >= 89.f) {
					return "fov: each angle must be below 89 degrees";
				}
			}
			if(command.values[0] >= command.values[1] || command.values[3] >= command.values[2]) {
				return "fov: left must be below right and down below up";
			}
		}
		return error;
	}

	if(word == "timeline") {
		command.op = Command::Timeline;
		const std::string mode = words.size() == 2 ? lowercase(words[1]) : std::string();
		if(mode != "gameplay" && mode != "session") {
			return "timeline: expected gameplay or session";
		}
		if(m_setupLocked) {
			return "timeline: set-up only";
		}
		command.flag = (mode == "session");
		return std::string();
	}

	if(word == "head") {
		const std::string what = words.size() > 1 ? lowercase(words[1]) : std::string();
		if(what == "pos") {
			command.op = Command::HeadPos;
			return numbers(2, 3, "head pos <x> <y> <z> in meters");
		} else if(what == "rot") {
			command.op = Command::HeadRot;
			return numbers(2, 3, "head rot <yaw> <pitch> <roll> in degrees");
		}
		return "head: expected pos or rot";
	}

	if(word == "hand") {
		if(!side(1, false)) {
			return "hand: expected left|right pos|rot|fingers|on|off";
		}
		const std::string what = words.size() > 2 ? lowercase(words[2]) : std::string();
		if(what == "pos") {
			command.op = Command::HandPos;
			return numbers(3, 3, "hand left|right pos <x> <y> <z> in meters");
		} else if(what == "rot") {
			command.op = Command::HandRot;
			return numbers(3, 3, "hand left|right rot <yaw> <pitch> <roll> in degrees");
		} else if((what == "on" || what == "off") && words.size() == 3) {
			command.op = Command::HandTracked;
			command.flag = (what == "on");
			return std::string();
		} else if(what == "fingers") {
			const std::string mode = words.size() == 4 ? lowercase(words[3]) : std::string();
			if(mode == "on" || mode == "off") {
				command.op = Command::HandJoints;
				command.flag = (mode == "on");
				return std::string();
			}
			command.op = Command::HandFingers;
			// Five fingers, or the thumb, the index finger and the other three together (#305's form)
			const size_t count = (words.size() == 3 + 5) ? 5 : 3;
			std::string error = numbers(3, count, "hand left|right fingers <thumb> <index> <middle> <ring> <little> "
			                                      "or <thumb> <index> <rest> (0 open - 1 closed) or on|off");
			for(size_t i = 0; error.empty() && i < count; i++) {
				if(command.values[i] < 0.f || command.values[i] > 1.f) {
					return "hand: fingers are closed from 0 to 1";
				}
			}
			if(error.empty() && count == 3) {
				command.values[3] = command.values[4] = command.values[2];
				command.count = 5;
			}
			return error;
		}
		return "hand: expected left|right pos|rot|fingers|on|off";
	}

	if(word == "profile") {
		command.op = Command::Profile;
		if(words.size() != 2) {
			return "profile: expected index, touch, simple, none or a profile path";
		}
		command.text = simProfilePath(words[1]);
		if(!command.text.empty() && command.text.rfind("/interaction_profiles/", 0) != 0) {
			return "profile: \"" + std::string(words[1]) + "\" is not index, touch, simple, none or /interaction_profiles/...";
		}
		return std::string();
	}

	if(word == "action") {
		command.op = Command::Action;
		if(words.size() < 4 || !side(2, true)) {
			return "action: expected <name> left|right|both <value> [<y>]";
		}
		command.text = std::string(words[1]);
		return numbers(3, words.size() - 3 == 2 ? 2 : 1, "<name> left|right|both <value> [<y>]");
	}

	if(word == "session") {
		command.op = Command::Session;
		const std::string state = words.size() == 2 ? lowercase(words[1]) : std::string();
		if(state == "exiting" || state == "loss-pending") {
			command.op = Command::SessionEnd;
			command.sessionEnd = (state == "exiting") ? SimSessionEnd::Exiting : SimSessionEnd::LossPending;
			return std::string();
		}
		if(state != "focused" && state != "visible" && state != "idle") {
			return "session: expected focused, visible, idle, exiting or loss-pending";
		}
		command.flag = (state == "focused");
		command.index = (state == "idle") ? 0 : 1; // worn
		return std::string();
	}

	if(word == "shot") {
		command.op = Command::Shot;
		if(words.size() > 2 || (words.size() == 2 && !validShotName(words[1]))) {
			return "shot: expected an optional name of letters, digits, - and _";
		}
		if(words.size() == 2) {
			command.text = std::string(words[1]);
		}
		return std::string();
	}

	if(word == "mark") {
		command.op = Command::Mark;
		size_t start = line.find(words[0]) + words[0].size();
		command.text = std::string(stripComment(line.substr(start)));
		return std::string();
	}

	if(word == "record") {
		command.op = Command::Record;
		if(words.size() != 2 || !validShotName(words[1])) {
			return "record: expected a name of letters, digits, - and _";
		}
		command.text = std::string(words[1]);
		return std::string();
	}

	if(word == "quit") {
		command.op = Command::Quit;
		return words.size() == 1 ? std::string() : "quit: takes no arguments";
	}

	if(word == "runtime") {
		command.op = Command::SessionEnd;
		command.sessionEnd = SimSessionEnd::RuntimeLost;
		const bool lost = words.size() == 2 && lowercase(words[1]) == "lost";
		return lost ? std::string() : "runtime: expected lost";
	}

	if(word == "fault") {
		command.op = Command::Fault;
		const std::string what = words.size() == 3 ? lowercase(words[1]) : std::string();
		const std::string state = words.size() == 3 ? lowercase(words[2]) : std::string();
		if(what == "exit" || what == "exit-process") {
			command.op = Command::FaultExit;
			command.flag = (what == "exit-process");
			return parseExitCode(words[2], command.exitCode) ? std::string() : "fault: " + what + " takes an exit code";
		}
		if(what != "swap-eyes" || (state != "on" && state != "off")) {
			return "fault: expected swap-eyes on|off or exit|exit-process <code>";
		}
		command.flag = (state == "on");
		return std::string();
	}

	if(word == "layers") {
		command.op = Command::Layers;
		const std::string state = words.size() == 2 ? lowercase(words[1]) : std::string();
		if(state != "on" && state != "off") {
			return "layers: expected on or off";
		}
		command.flag = (state == "on");
		return std::string();
	}

	return "unknown command \"" + std::string(words[0]) + "\"";
}

std::vector<float *> Simulation::targets(const Command & command) {

	std::vector<float *> result;
	auto add = [&](Vec3f & v) {
		result.push_back(&v.x);
		result.push_back(&v.y);
		result.push_back(&v.z);
	};

	switch(command.op) {
		case Command::Ipd: result.push_back(&view.ipd); break;
		case Command::HeadPos: add(rig.head.position); break;
		case Command::HeadRot: add(rig.head.angles); break;
		case Command::HandPos: add(rig.hands[command.index].position); break;
		case Command::HandRot: add(rig.hands[command.index].angles); break;
		case Command::HandFingers:
			add(rig.fingers[command.index].curl);
			result.push_back(&rig.fingers[command.index].outer.x);
			result.push_back(&rig.fingers[command.index].outer.y);
			break;
		case Command::Action: {
			std::array<Vec2f, 2> & values = rig.actions[command.text];
			for(size_t hand = 0; hand < 2; hand++) {
				if(command.index == hand || command.index == 2) {
					result.push_back(&values[hand].x);
					if(command.count == 2) {
						result.push_back(&values[hand].y);
					}
				}
			}
			break;
		}
		default: break;
	}

	return result;
}

void Simulation::apply(const Command & command) {

	switch(command.op) {
		case Command::Rate: view.rate = command.values[0]; return;
		case Command::EyeSize: view.eyeSize = Vec2i(s32(command.values[0]), s32(command.values[1])); return;
		case Command::Fov: {
			MirrorFov & fov = view.fov[command.index];
			fov.left = glm::radians(command.values[0]);
			fov.right = glm::radians(command.values[1]);
			fov.up = glm::radians(command.values[2]);
			fov.down = glm::radians(command.values[3]);
			return;
		}
		case Command::Timeline: timeline = command.flag ? SimTimeline::Session : SimTimeline::Gameplay; return;
		case Command::HandTracked:
			rig.handTracked[command.index] = command.flag;
			m_requests.profileChanged = true;
			return;
		case Command::HandJoints:
			rig.fingers[command.index].joints = command.flag;
			return;
		case Command::Profile:
			rig.profile = command.text;
			m_requests.profileChanged = true;
			return;
		case Command::Session: {
			const bool worn = (command.index != 0);
			if(!worn) {
				// The headset idle on the desk: the session falls back to synchronized; the focus stays as it was
				if(rig.worn) {
					rig.worn = false;
					m_requests.focusChanged = true;
				}
			} else if(!rig.worn || rig.focused != command.flag) {
				rig.worn = true;
				rig.focused = command.flag;
				m_requests.focusChanged = true;
			}
			return;
		}
		case Command::Shot:
			m_shots++;
			m_requests.shots.push_back(command.text.empty() ? "sim" + std::to_string(m_shots) : command.text);
			return;
		case Command::Mark: m_requests.marks.push_back(command.text); return;
		case Command::Record: m_requests.records.push_back(command.text); return;
		case Command::Quit: m_requests.quit = true; return;
		case Command::Fault: rig.swapEyes = command.flag; return;
		case Command::SessionEnd: m_requests.sessionEnd = command.sessionEnd; return;
		case Command::FaultExit:
			m_requests.exit = command.flag ? SimExit::ExitProcess : SimExit::Exit;
			m_requests.exitCode = command.exitCode;
			return;
		case Command::Layers: rig.saveLayers = command.flag; return;
		default: break;
	}

	std::vector<float *> values = targets(command);
	for(size_t i = 0; i < values.size(); i++) {
		*values[i] = command.values[i % command.count];
	}
}

std::string Simulation::execute(std::string_view text) {

	std::string_view line = stripComment(text);
	if(line.empty()) {
		return std::string();
	}

	if(line.front() == '@') {
		// A timed line from the channel: absolute frames, or @+n from the next frame on; @+<t>ms from the
		// display time of the frame advanced last
		u64 last = 0, lastUs = 0;
		return addTimed(line, m_advanced ? m_frame + 1 : 0, last, m_advanced ? u64(std::max<s64>(m_timeUs, 0)) : 0, lastUs);
	}

	Command command;
	std::string error = parse(line, command);
	if(!error.empty()) {
		return error;
	}
	apply(command);
	return std::string();
}

std::string Simulation::load(std::string_view text) {

	Simulation copy = *this;
	u64 previous = 0, previousUs = 0;
	size_t number = 0;

	for(size_t pos = 0; pos <= text.size(); ) {

		size_t end = text.find('\n', pos);
		if(end == std::string_view::npos) {
			end = text.size();
		}
		std::string_view line = stripComment(text.substr(pos, end - pos));
		pos = end + 1;
		number++;

		if(line.empty()) {
			continue;
		}

		// @+n in a script counts from the previous timed line's last frame, @+<t>ms from the previous ms line's end
		std::string error = line.front() == '@' ? copy.addTimed(line, previous, previous, previousUs, previousUs)
		                                        : copy.execute(line);
		if(!error.empty()) {
			return "line " + std::to_string(number) + ": " + error;
		}
	}

	*this = std::move(copy);
	return std::string();
}

std::string Simulation::addTimed(std::string_view line, u64 base, u64 & last, u64 baseUs, u64 & lastUs) {

	size_t space = line.find_first_of(" \t");
	if(space == std::string_view::npos) {
		return "@: expected @<frame> <command>";
	}

	std::string_view when = line.substr(1, space - 1);
	const std::string written(when);
	const bool relative = !when.empty() && when.front() == '+';
	if(relative) {
		when.remove_prefix(1);
	}
	// Milliseconds of display time instead of frames (#23)
	Entry entry;
	if(when.size() > 2 && when.substr(when.size() - 2) == "ms") {
		entry.timed = true;
		when.remove_suffix(2);
	}

	size_t dash = when.find('-');
	if(!parseFrame(when.substr(0, dash), entry.from)
	   || (dash != std::string_view::npos && !parseFrame(when.substr(dash + 1), entry.to))) {
		return "@" + written + ": expected @<frame>, @<from>-<to>, @+<n> or @+<n>-<m>, frames or with ms";
	}
	if(dash == std::string_view::npos) {
		entry.to = entry.from;
	}
	if(entry.timed) {
		entry.from *= 1000;
		entry.to *= 1000;
	}
	if(relative) {
		entry.from += entry.timed ? baseUs : base;
		entry.to += entry.timed ? baseUs : base;
	}
	if(entry.to < entry.from) {
		return "@" + written + ": a ramp must end after it starts";
	}

	entry.text = std::string(stripComment(line.substr(space + 1)));
	Command command;
	std::string error = parse(entry.text, command);
	if(!error.empty()) {
		return error;
	}
	if(entry.to != entry.from && !command.rampable()) {
		return "@" + written + ": " + lowercase(splitWords(entry.text)[0]) + " cannot be a ramp";
	}
	entry.action = (command.op == Command::Action);

	(entry.timed ? lastUs : last) = entry.to;
	m_timeline.push_back(std::move(entry));
	// Frame lines before ms lines, each in the order of their start (stable: the order written for equal starts)
	std::stable_sort(m_timeline.begin(), m_timeline.end(), [](const Entry & a, const Entry & b) {
		return a.timed != b.timed ? !a.timed : a.from < b.from;
	});

	return std::string();
}

namespace {

/*!
 * A display time this close below a line's time counts as reached: the runtime's period is whole
 * nanoseconds (72 Hz: 13888888 ns), so 36 slots end 32 ns short of 500 ms
 */
const u64 TimeSlackUs = 50;

} // anonymous namespace

void Simulation::advance(u64 frame, s64 timeUs) {

	// An action is what the controller held when the frame's interval began: the previous frame's display time
	const u64 previousUs = u64(std::max<s64>(m_advanced ? m_timeUs : timeUs, 0));
	const u64 nowUs = u64(std::max<s64>(timeUs, 0));
	m_frame = frame;
	m_timeUs = timeUs;
	m_advanced = true;

	for(Entry & entry : m_timeline) {

		const u64 at = !entry.timed ? frame : (entry.action ? previousUs : nowUs);
		const u64 reached = entry.timed ? at + TimeSlackUs : at;
		if(entry.done || entry.from > reached) {
			continue;
		}

		Command command;
		if(!parse(entry.text, command).empty()) {
			entry.done = true; // checked when it was added - only set-up-only commands can fail now
			continue;
		}

		if(entry.to == entry.from) {
			apply(command);
			entry.started = entry.done = true;
			continue;
		}

		std::vector<float *> values = targets(command);
		if(!entry.started) {
			entry.started = true;
			entry.start.clear();
			for(float * value : values) {
				entry.start.push_back(*value);
			}
		}
		const bool end = reached >= entry.to;
		const u64 into = at > entry.from ? at - entry.from : 0;
		const float t = end ? 1.f : float(double(into) / double(entry.to - entry.from));
		for(size_t i = 0; i < values.size() && i < entry.start.size(); i++) {
			float target = command.values[i % command.count];
			*values[i] = entry.start[i] + (target - entry.start[i]) * t;
		}
		if(end) {
			entry.done = true;
		}
	}
}

SimRequests Simulation::takeRequests() {
	SimRequests requests = std::move(m_requests);
	m_requests = SimRequests();
	return requests;
}

size_t Simulation::pending() const {
	return size_t(std::count_if(m_timeline.begin(), m_timeline.end(), [](const Entry & entry) { return !entry.done; }));
}

} // namespace vr
