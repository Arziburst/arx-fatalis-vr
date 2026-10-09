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

#include "core/CommandChannel.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cinematic/CinematicController.h"
#include "core/Application.h"
#include "core/ArxGame.h"
#include "core/Core.h"
#include "core/GameTime.h"
#include "core/SaveGame.h"
#include "core/TimeTypes.h"
#include "core/Version.h"
#include "game/Player.h"
#include "graphics/Renderer.h"
#include "graphics/image/Image.h"
#include "gui/Console.h"
#include "gui/Menu.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/log/LogBackend.h"
#include "io/log/Logger.h"
#include "platform/Environment.h"
#include "platform/Process.h"
#include "platform/ProgramOptions.h"
#include "platform/Time.h"
#include "scene/ChangeLevel.h"
#include "util/String.h"
#include "vr/VRSystem.h"
#include "window/RenderWindow.h"

namespace channel {

namespace {

enum class GameState {
	Startup,   //!< Before the first level or menu
	Menu,      //!< Main menu, pause menu, credits, character creation
	Loading,   //!< A level is being loaded (set by \ref notifyLoading, synchronous within a frame)
	Gameplay,
	Cinematic,
	Exited     //!< Clean shutdown reached
};

const char * gameStateName(GameState state) {
	switch(state) {
		case GameState::Startup:   return "STARTUP";
		case GameState::Menu:      return "MENU";
		case GameState::Loading:   return "LOADING";
		case GameState::Gameplay:  return "GAMEPLAY";
		case GameState::Cinematic: return "CINEMATIC";
		case GameState::Exited:    return "EXITED";
	}
	return "UNKNOWN";
}

const char * menuModeName(MenuMode mode) {
	switch(mode) {
		case Mode_InGame:            return "ingame";
		case Mode_MainMenu:          return "mainmenu";
		case Mode_Credits:           return "credits";
		case Mode_CharacterCreation: return "charactercreation";
	}
	return "unknown";
}

//! Counts logged errors and warnings so a test can assert on numbers without parsing the log
class LogCounter final : public logger::Backend {

public:

	std::atomic<unsigned> errors { 0 };
	std::atomic<unsigned> warnings { 0 };

	void log(const logger::Source & /* file */, int /* line */, Logger::LogLevel level,
	         std::string_view /* str */) override {
		if(level >= Logger::Error) {
			errors++;
		} else if(level == Logger::Warning) {
			warnings++;
		}
	}

};

//! Minimal JSON object writer - enough for status.json, no dependencies
class JsonWriter {

	std::ostringstream m_out;
	std::vector<bool> m_first;

	void comma() {
		if(!m_first.back()) {
			m_out << ',';
		}
		m_first.back() = false;
	}

	void writeString(std::string_view str) {
		m_out << '"';
		for(unsigned char c : str) {
			switch(c) {
				case '"':  m_out << "\\\""; break;
				case '\\': m_out << "\\\\"; break;
				case '\n': m_out << "\\n"; break;
				case '\r': m_out << "\\r"; break;
				case '\t': m_out << "\\t"; break;
				default: {
					if(c < 0x20) {
						m_out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
					} else {
						m_out << char(c);
					}
				}
			}
		}
		m_out << '"';
	}

	void key(std::string_view name) {
		comma();
		writeString(name);
		m_out << ':';
	}

public:

	JsonWriter() {
		m_out << '{';
		m_first.push_back(true);
	}

	JsonWriter & object(std::string_view name) {
		key(name);
		m_out << '{';
		m_first.push_back(true);
		return *this;
	}

	JsonWriter & end() {
		m_out << '}';
		m_first.pop_back();
		return *this;
	}

	JsonWriter & value(std::string_view name, std::string_view str) {
		key(name);
		writeString(str);
		return *this;
	}

	JsonWriter & value(std::string_view name, const char * str) {
		return value(name, std::string_view(str));
	}

	JsonWriter & value(std::string_view name, bool b) {
		key(name);
		m_out << (b ? "true" : "false");
		return *this;
	}

	JsonWriter & value(std::string_view name, float f) {
		key(name);
		if(std::isfinite(f)) {
			m_out << std::fixed << std::setprecision(3) << f << std::defaultfloat;
		} else {
			m_out << "null";
		}
		return *this;
	}

	template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
	JsonWriter & value(std::string_view name, T n) {
		key(name);
		m_out << n;
		return *this;
	}

	std::string str() {
		while(!m_first.empty()) {
			end();
		}
		m_out << '\n';
		return m_out.str();
	}

};

struct State {

	bool enabled = false;
	fs::path dir;
	std::time_t startTime = 0;
	PlatformInstant startInstant = 0;
	LogCounter * counter = nullptr; //!< Owned by the logger once added

	u64 frame = 0;
	u64 lastStatusFrame = 0;
	PlatformInstant lastStatusTime = 0;
	PlatformInstant lastPollTime = 0;
	float fps = 0.f;

	GameState state = GameState::Startup;
	bool cleanShutdown = false;

	unsigned cmdSeq = 0; //!< Command batches executed so far
	std::string lastCmd;
	std::string lastCmdError;

	unsigned captureSeq = 0;
	std::string lastCapture;
	std::string pendingCapture;

	//! drawcheck: the scene view drawn twice per frame with the same camera must give one picture
	struct DrawCheck {
		unsigned seq = 0;            //!< Checks started so far
		unsigned framesLeft = 0;     //!< Frames still to check; 0 when no check runs
		unsigned frames = 0;         //!< Frames checked by the current or last check
		unsigned framesDiffering = 0;
		size_t pixels = 0;           //!< Pixels per picture
		size_t maxPixelsDiffering = 0;
		unsigned maxDelta = 0;       //!< Largest difference of one color channel, 0-255
		std::string capture;         //!< First differing pair, saved as <capture>_a.bmp / _b.bmp
		Image first;                 //!< The first view of the current frame
	} drawCheck;

};

State g;

const s64 PollIntervalMs = 100;
const s64 StatusIntervalMs = 250;

void setTestDir(const std::string & dir) {
	g.dir = dir;
	g.enabled = true;
}
ARX_PROGRAM_OPTION_ARG("test-dir", nullptr,
                       "Enable the test command channel and status file in DIR (see docs/vr/TESTING.md)",
                       &setTestDir, "DIR")

GameState currentState() {
	if(ARXmenu.mode() != Mode_InGame) {
		return GameState::Menu;
	}
	if(!g_currentArea) {
		return GameState::Startup;
	}
	if(isInCinematic()) {
		return GameState::Cinematic;
	}
	return GameState::Gameplay;
}

std::string buildStatus(PlatformInstant now) {

	JsonWriter json;

	json.value("pid", u64(platform::getProcessId()));
	json.value("version", arx_version);
	// The same build identification as the log banner (#115) - assert on these, not on the log
	json.object("build");
	json.value("commit", arx_git_commit);
	json.value("branch", arx_git_branch);
	json.value("config", arx_build_type);
	json.end();
	json.value("state", gameStateName(g.state));
	json.value("cleanShutdown", g.cleanShutdown);
	json.value("frame", g.frame);
	json.value("uptimeMs", toMsi(now - g.startInstant));
	json.value("fps", g.fps);
	json.value("errors", g.counter ? g.counter->errors.load() : 0u);
	json.value("warnings", g.counter ? g.counter->warnings.load() : 0u);

	json.value("menuMode", menuModeName(ARXmenu.mode()));
	json.value("area", g_currentArea ? u32(g_currentArea) : 0u);
	json.value("paused", g_gameTime.isPaused() != 0);
	json.value("cinematic", isInCinematic());

	if(g_currentArea) {
		json.object("player");
		json.value("x", player.pos.x);
		json.value("y", player.pos.y);
		json.value("z", player.pos.z);
		json.value("yaw", player.angle.getYaw());
		json.value("life", player.lifePool.current);
		json.value("lifeMax", player.lifePool.max);
		json.value("mana", player.manaPool.current);
		json.value("manaMax", player.manaPool.max);
		json.end();
	}

	if(mainApp && mainApp->getWindow()) {
		const RenderWindow & window = *mainApp->getWindow();
		json.object("window");
		json.value("width", window.getSize().x);
		json.value("height", window.getSize().y);
		json.value("focused", window.hasFocus());
		json.value("minimized", window.isMinimized());
		json.value("fullscreen", window.isFullScreen());
		json.end();
	}

	json.object("vr");
	json.value("supported", vr::isSupported());
	json.value("enabled", vr::isEnabled());
	json.value("sessionState", vr::sessionStateString());
	json.value("runtime", vr::runtimeName());
	json.value("manifest", vr::runtimeManifest());
	json.end();

	json.value("cmdSeq", g.cmdSeq);
	json.value("lastCmd", g.lastCmd);
	json.value("lastCmdError", g.lastCmdError);
	json.value("captureSeq", g.captureSeq);
	json.value("lastCapture", g.lastCapture);

	if(g.drawCheck.seq != 0) {
		const State::DrawCheck & check = g.drawCheck;
		json.object("drawCheck");
		json.value("seq", check.seq);
		json.value("running", check.framesLeft != 0);
		json.value("frames", check.frames);
		json.value("framesDiffering", check.framesDiffering);
		json.value("pixels", check.pixels);
		json.value("maxPixelsDiffering", check.maxPixelsDiffering);
		json.value("maxDelta", check.maxDelta);
		json.value("capture", check.capture);
		json.end();
	}

	return json.str();
}

void writeStatus(PlatformInstant now) {

	if(g.lastStatusTime != PlatformInstant(0)) {
		s64 ms = toMsi(now - g.lastStatusTime);
		if(ms > 0) {
			g.fps = float(g.frame - g.lastStatusFrame) * 1000.f / float(ms);
		}
	}
	g.lastStatusTime = now;
	g.lastStatusFrame = g.frame;

	// Written to a temporary file and renamed, so a reader never sees a half-written status
	fs::path file = g.dir / "status.json";
	fs::path tmp = g.dir / "status.json.tmp";
	static bool warned = false;
	if((!fs::write(tmp, buildStatus(now)) || !fs::rename(tmp, file, true)) && !warned) {
		warned = true;
		LogWarning << "[channel] could not write " << file;
	}

	// The file logger flushes on its own every second (#129); a test reading arx.log wants it fresher
	Logger::flush();
}

void writeAck(unsigned errors) {
	std::ostringstream oss;
	oss << "seq=" << g.cmdSeq << " frame=" << g.frame << " errors=" << errors
	    << " last=" << g.lastCmd << " error=" << (g.lastCmdError.empty() ? "-" : g.lastCmdError) << '\n';
	if(!fs::write(g.dir / "ack.txt", oss.str())) {
		LogWarning << "[channel] could not write " << (g.dir / "ack.txt");
	}
}

void setState(GameState state) {
	g.state = state;
	LogInfo << "[game] state: " << gameStateName(state);
	writeStatus(platform::getTime());
}

std::string refuse(const std::string & reason) {
	LogWarning << "[channel] " << reason;
	return reason;
}

std::string_view trim(std::string_view str) {
	while(!str.empty() && std::isspace(static_cast<unsigned char>(str.front()))) {
		str.remove_prefix(1);
	}
	while(!str.empty() && std::isspace(static_cast<unsigned char>(str.back()))) {
		str.remove_suffix(1);
	}
	return str;
}

std::string_view firstWord(std::string_view line, std::string_view & rest) {
	size_t end = 0;
	while(end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
		end++;
	}
	rest = trim(line.substr(end));
	return line.substr(0, end);
}

bool parseIndex(std::string_view text, long & value) {
	if(text.empty()) {
		return false;
	}
	char * end = nullptr;
	std::string copy(text);
	value = std::strtol(copy.c_str(), &end, 10);
	return end && *end == '\0' && value >= 0;
}

/*!
 * Run one command line.
 * \return an empty string on success, otherwise the reason it was refused
 */
std::string execute(std::string_view line) {

	std::string_view rest;
	std::string word(firstWord(line, rest));
	util::makeLowercase(word);

	if(word == "status") {
		// Nothing to do: the status is written after every batch
		return std::string();
	}

	if(word == "quit") {
		LogInfo << "[channel] quit requested";
		mainApp->quit();
		return std::string();
	}

	if(word == "shot") {
		g.pendingCapture = rest.empty() ? "shot" + std::to_string(g.captureSeq + 1) : std::string(rest);
		return std::string();
	}

	if(word == "mark") {
		LogInfo << "[mark] " << rest;
		return std::string();
	}

	if(word == "recenter") {
		return refuse("recenter: not implemented yet (#28)");
	}

	if(word == "drawcheck") {
		long frames = 1;
		if(!rest.empty() && (!parseIndex(rest, frames) || frames < 1 || frames > 100000)) {
			return refuse("drawcheck: expected a frame count 1-100000, got \"" + std::string(rest) + "\"");
		}
		if(g.drawCheck.framesLeft != 0) {
			return refuse("drawcheck: check " + std::to_string(g.drawCheck.seq) + " still runs ("
			              + std::to_string(g.drawCheck.framesLeft) + " frames left)");
		}
		if(currentState() != GameState::Gameplay) {
			return refuse(std::string("drawcheck: needs a level on screen (state ")
			              + gameStateName(currentState()) + ")");
		}
		State::DrawCheck & check = g.drawCheck;
		check.seq++;
		check.framesLeft = unsigned(frames);
		check.frames = 0;
		check.framesDiffering = 0;
		check.pixels = 0;
		check.maxPixelsDiffering = 0;
		check.maxDelta = 0;
		check.capture.clear();
		LogInfo << "[render] drawcheck " << check.seq << ": drawing the scene twice per frame for "
		        << frames << " frames";
		return std::string();
	}

	if(word == "loadslot") {
		long slot = 0;
		if(!parseIndex(rest, slot)) {
			return refuse("loadslot: expected a slot number, got \"" + std::string(rest) + "\"");
		}
		// The list is read at startup and by the load menu; a save made since then must count too
		savegames.update();
		if(size_t(slot) >= savegames.size()) {
			return refuse("loadslot: no save in slot " + std::to_string(slot) + " (have "
			              + std::to_string(savegames.size()) + ")");
		}
		LogInfo << "[channel] loading save slot " << slot;
		LOADQUEST_SLOT = SavegameHandle(slot);
		return std::string();
	}

	if(word == "loadlevel") {
		std::string_view target;
		std::string_view level = firstWord(rest, target);
		long area = 0;
		if(!parseIndex(level, area)) {
			return refuse("loadlevel: expected a level number, got \"" + std::string(level) + "\"");
		}
		if(ARXmenu.mode() != Mode_InGame || !g_currentArea) {
			return refuse("loadlevel: needs a running game (start with --loadlevel N or use loadslot)");
		}
		LogInfo << "[channel] changing to level " << area << (target.empty() ? "" : " at ") << target;
		ARX_CHANGELEVEL_Change(AreaId(u32(area)), target, 0.f);
		return std::string();
	}

	// Everything else is a script console command: [entity.]command args
	LogInfo << "[channel] > " << line;
	std::string error = executeConsoleLine(line);
	if(!error.empty()) {
		return refuse("script command failed: " + error);
	}

	return std::string();
}

void pollCommands() {

	fs::path file = g.dir / "command.txt";
	if(!fs::exists(file)) {
		return;
	}

	if(fs::last_write_time(file) < g.startTime) {
		LogWarning << "[channel] ignoring " << file << ": written before this process started";
		fs::remove(file);
		return;
	}

	std::string contents = fs::read(file);
	if(!fs::remove(file)) {
		// Do not run the same batch again next frame
		LogWarning << "[channel] could not delete " << file << " after reading it";
		fs::write(file, std::string_view());
	}

	std::vector<std::string_view> lines;
	for(size_t pos = 0; pos < contents.size(); ) {
		size_t end = contents.find('\n', pos);
		if(end == std::string::npos) {
			end = contents.size();
		}
		std::string_view line = trim(std::string_view(contents).substr(pos, end - pos));
		if(!line.empty() && line.front() != '#') {
			lines.push_back(line);
		}
		pos = end + 1;
	}
	if(lines.empty()) {
		return;
	}

	g.cmdSeq++;
	unsigned errors = 0;
	g.lastCmdError.clear();
	for(std::string_view line : lines) {
		g.lastCmd = std::string(line);
		std::string error = execute(line);
		if(!error.empty()) {
			errors++;
			g.lastCmdError = error;
		}
	}

	writeAck(errors);
	writeStatus(platform::getTime());
}

} // anonymous namespace

bool isEnabled() {
	return g.enabled;
}

void initialize() {

	if(!g.enabled) {
		return;
	}

	if(!fs::create_directories(g.dir)) {
		LogError << "[channel] could not create the test directory " << g.dir << ", channel disabled";
		g.enabled = false;
		return;
	}

	g.startTime = std::time(nullptr);
	g.startInstant = platform::getTime();

	// Leftovers of a previous run must not be mistaken for this one
	fs::remove(g.dir / "status.json");
	fs::remove(g.dir / "ack.txt");

	g.counter = new LogCounter;
	Logger::add(g.counter);

	// Inherited by the crash processor: an automated run has nobody to click the crash dialog
	platform::setEnvironmentVariable("ARX_UNATTENDED", "1");

	LogInfo << "[channel] test channel enabled in " << g.dir << " (pid " << platform::getProcessId() << ")";

	writeStatus(g.startInstant);
}

void update() {

	if(!g.enabled) {
		return;
	}

	g.frame++;
	PlatformInstant now = platform::getTime();

	GameState state = currentState();
	if(state != g.state) {
		setState(state);
	}

	if(toMsi(now - g.lastPollTime) >= PollIntervalMs) {
		g.lastPollTime = now;
		pollCommands();
	}

	if(toMsi(now - g.lastStatusTime) >= StatusIntervalMs) {
		writeStatus(now);
	}

}

void afterRender() {

	if(!g.enabled || g.pendingCapture.empty()) {
		return;
	}

	std::string name = g.pendingCapture;
	g.pendingCapture.clear();

	fs::path dir = g.dir / "capture";
	fs::create_directories(dir);
	fs::path file = dir / (name + ".bmp");

	Image image;
	if(!GRenderer || !GRenderer->getSnapshot(image)) {
		g.lastCmdError = refuse("shot: could not read the frame");
		return;
	}
	if(!image.save(file)) {
		g.lastCmdError = refuse("shot: could not write " + file.string());
		return;
	}

	g.captureSeq++;
	g.lastCapture = file.string();
	LogInfo << "[channel] shot " << file << " (" << image.getWidth() << 'x' << image.getHeight() << ")";

	writeStatus(platform::getTime());
}

size_t sceneViewCount() {
	return (g.enabled && g.drawCheck.framesLeft != 0) ? 2 : 1;
}

void afterSceneView(size_t view) {

	State::DrawCheck & check = g.drawCheck;
	if(!g.enabled || check.framesLeft == 0 || !GRenderer) {
		return;
	}

	if(view == 0) {
		GRenderer->getSnapshot(check.first);
		return;
	}

	Image second;
	GRenderer->getSnapshot(second);

	const Image & first = check.first;
	arx_assert(first.getFormat() == second.getFormat() && first.getSize() == second.getSize());
	size_t channels = first.getNumChannels();
	size_t pixels = first.getWidth() * first.getHeight();
	size_t differing = 0;
	unsigned delta = 0;
	const unsigned char * a = first.getData();
	const unsigned char * b = second.getData();
	for(size_t i = 0; i < pixels; i++, a += channels, b += channels) {
		bool differs = false;
		for(size_t c = 0; c < channels; c++) {
			unsigned d = unsigned(std::abs(int(a[c]) - int(b[c])));
			if(d != 0) {
				differs = true;
				delta = std::max(delta, d);
			}
		}
		if(differs) {
			differing++;
		}
	}

	check.frames++;
	check.framesLeft--;
	check.pixels = pixels;
	check.maxDelta = std::max(check.maxDelta, delta);
	if(differing != 0) {
		check.framesDiffering++;
		if(check.capture.empty()) {
			// Only the first differing frame is logged: a broken view differs every frame
			LogInfo << "[render] drawcheck " << check.seq << " frame " << check.frames << ": " << differing
			        << " of " << pixels << " pixels differ between the two views (max delta " << delta << ")";
		}
		if(differing > check.maxPixelsDiffering) {
			// Keep the pair that differs most
			fs::path dir = g.dir / "capture";
			fs::create_directories(dir);
			fs::path base = dir / ("drawcheck" + std::to_string(check.seq));
			check.capture = base.string();
			first.save(base.string() + "_a.bmp");
			second.save(base.string() + "_b.bmp");
		}
	}
	check.maxPixelsDiffering = std::max(check.maxPixelsDiffering, differing);

	if(check.framesLeft == 0) {
		LogInfo << "[render] drawcheck " << check.seq << " done: " << check.framesDiffering << " of "
		        << check.frames << " frames differ (max " << check.maxPixelsDiffering << " of " << pixels
		        << " pixels, max delta " << check.maxDelta << ")"
		        << (check.capture.empty() ? "" : ", most differing pair: " + check.capture + "_a.bmp / _b.bmp");
		check.first.reset();
		writeStatus(platform::getTime());
	}

}

void notifyLoading(AreaId area) {

	if(!g.enabled) {
		return;
	}

	g.state = GameState::Loading;
	LogInfo << "[game] state: LOADING level " << u32(area);
	writeStatus(platform::getTime());
}

void shutdown() {

	if(!g.enabled) {
		return;
	}

	g.state = GameState::Exited;
	g.cleanShutdown = true;
	LogInfo << "[game] state: EXITED";
	writeStatus(platform::getTime());

	g.enabled = false;
}

} // namespace channel
