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
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "ai/Paths.h"
#include "animation/Animation.h"
#include "cinematic/CinematicController.h"
#include "core/Application.h"
#include "core/ArxGame.h"
#include "core/ChannelWriter.h"
#include "core/Config.h"
#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "core/SaveGame.h"
#include "core/TimeTypes.h"
#include "core/Version.h"
#include "game/Camera.h"
#include "game/Damage.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Inventory.h"
#include "game/Levels.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/effect/Quake.h"
#include "graphics/Math.h"
#include "graphics/Raycast.h"
#include "graphics/Renderer.h"
#include "graphics/data/Mesh.h"
#include "graphics/image/Image.h"
#include "gui/Console.h"
#include "gui/DamageNumbers.h"
#include "gui/DevVariants.h"
#include "gui/Dragging.h"
#include "gui/CharacterCreation.h"
#include "gui/Interface.h"
#include "gui/LoadLevelScreen.h"
#include "gui/MainMenu.h"
#include "gui/Menu.h"
#include "gui/MenuPublic.h"
#include "gui/MenuWidgets.h"
#include "gui/Notification.h"
#include "gui/book/Book.h"
#include "gui/book/DevPanel.h"
#include "gui/hud/PlayerInventory.h"
#include "gui/hud/SecondaryInventory.h"
#include "input/Input.h"
#include "io/fs/FilePath.h"
#include "io/fs/FileStream.h"
#include "io/fs/Filesystem.h"
#include "io/log/LogBackend.h"
#include "io/log/Logger.h"
#include "physics/Collisions.h"
#include "platform/DumpHelper.h"
#include "platform/Environment.h"
#include "platform/ExitTrace.h"
#include "platform/HangWatchdog.h"
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include "platform/WindowsUtils.h"
#endif
#include "platform/Process.h"
#include "platform/ProgramOptions.h"
#include "platform/Time.h"
#include "scene/ChangeLevel.h"
#include "scene/Interactive.h"
#include "scene/Tiles.h"
#include "script/Script.h"
#include "util/Number.h"
#include "util/String.h"
#include "vr/BodyYaw.h"
#include "vr/EyeImage.h"
#include "vr/HeadTracking.h"
#include "vr/MotionProbe.h"
#include "vr/PerfLog.h"
#include "vr/ReplayRecorder.h"
#include "vr/SceneSubtitles.h"
#include "vr/SceneView.h"
#include "vr/VRArms.h"
#include "vr/VRBody.h"
#include "vr/VRComfort.h"
#include "vr/VRHud.h"
#include "vr/VRInput.h"
#include "vr/VRMarks.h"
#include "vr/VRMelee.h"
#include "vr/VRRadial.h"
#include "vr/VRReticle.h"
#include "vr/VRRunes.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"
#include "vr/VRWeapon.h"
#include "vr/WeaponGrip.h"
#include "vr/WorldScale.h"
#include "vr/WristHud.h"
#include "window/RenderWindow.h"

extern bool START_NEW_QUEST;
extern bool bGCroucheToggle;
extern long COLLIDED_CLIMB_POLY;

namespace channel {

namespace {

// The game state is the event log's (#130): it logs every change as a game.state event
using gameevent::GameState;
using gameevent::gameStateName;

const char * menuModeName(MenuMode mode) {
	switch(mode) {
		case Mode_InGame:            return "ingame";
		case Mode_MainMenu:          return "mainmenu";
		case Mode_Credits:           return "credits";
		case Mode_CharacterCreation: return "charactercreation";
	}
	return "unknown";
}

//! The main menu's pages a test can open and read back (#178)
const struct { MENUSTATE page; const char * name; } MenuPages[] = {
	{ Page_None, "none" },
	{ Page_Options, "options" },
	{ Page_OptionsInput, "input" },
	{ Page_OptionsVR, "vr" },
	{ Page_OptionsVRTurning, "vr-turning" },
	// The pages the main menu's entries and the Interface options open (#394)
	{ Page_LoadOrSave, "loadsave" },
	{ Page_OptionsInterface, "interface" },
	{ Page_QuitConfirm, "quit" },
	{ Page_Localization, "language" },
	{ Page_CrashNotice, "crashnotice" },
};

const char * menuPageName(MENUSTATE page) {
	for(const auto & entry : MenuPages) {
		if(entry.page == page) {
			return entry.name;
		}
	}
	return "other";
}

MENUSTATE currentMenuPage() {
	return (g_mainMenu && g_mainMenu->m_window) ? g_mainMenu->m_window->currentPageId() : Page_None;
}

/*!
 * vr-point <hand> menu <name> (#394): where a widget of the main menu or of its open page (findMenuWidget), or a
 * button, an attribute or a skill of the character creation (CharacterCreation::targetRect), is on the interface
 */
bool menuTargetRect(std::string_view name, Rectf & rect, std::string & why) {
	if(ARXmenu.mode() == Mode_CharacterCreation) {
		if(!g_characterCreation.targetRect(name, rect)) {
			why = "the character creation has no \"" + std::string(name) + "\" (quickgen, skin, done, an attribute"
			      " or a skill: strength, mind, dexterity, constitution, stealth, ...)";
			return false;
		}
		return true;
	}
	if(ARXmenu.mode() == Mode_MainMenu) {
		const Widget * widget = findMenuWidget(name, why);
		if(!widget) {
			return false;
		}
		rect = widget->m_rect;
		return true;
	}
	if(ARXmenu.mode() == Mode_InGame && (player.Interface & INTER_PLAYERBOOK)) {
		// The dev variants panel's buttons on the map page (#588)
		if(name.substr(0, 9) == "devpanel.") {
			if(!devvariants::panelTargetRect(name, rect)) {
				why = "the book shows no dev panel button \"" + std::string(name) + "\" (devPanel.visible, its page)";
				return false;
			}
			return true;
		}
		// The open book's bookmarks (#377): book_stats, book_spells, book_map, book_quests
		const struct { std::string_view name; ARX_INTERFACE_BOOK_MODE page; } tabs[] = {
			{ "book_stats", BOOKMODE_STATS }, { "book_spells", BOOKMODE_SPELLS },
			{ "book_map", BOOKMODE_MINIMAP }, { "book_quests", BOOKMODE_QUESTS },
		};
		for(const auto & tab : tabs) {
			if(name == tab.name) {
				if(!g_playerBook.tabRect(tab.page, rect)) {
					why = "the book shows " + std::string(name) + " or has no bookmark for it";
					return false;
				}
				return true;
			}
		}
		why = "the open book has book_stats, book_spells, book_map and book_quests, not \"" + std::string(name) + "\"";
		return false;
	}
	why = std::string("needs the main menu, the character creation or the open book, mode is ")
	      + menuModeName(ARXmenu.mode());
	return false;
}

//! Counts logged errors and warnings so a test can assert on numbers without parsing the log
class LogCounter final : public logger::Backend {

public:

	std::atomic<unsigned> errors { 0 };
	std::atomic<unsigned> warnings { 0 };
	//! Every line, counted under the logger's lock like its flush: what a status was built after (#553)
	std::atomic<unsigned long long> lines { 0 };

	void log(const logger::Source & /* file */, int /* line */, Logger::LogLevel level,
	         std::string_view /* str */) override {
		lines++;
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

	//! A value that is JSON already (a record kept from an earlier frame)
	JsonWriter & raw(std::string_view name, std::string_view json) {
		key(name);
		m_out << json;
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
	float fps = 0.f;

	GameState state = GameState::Startup;
	bool cleanShutdown = false;

	unsigned cmdSeq = 0; //!< Command batches started so far (the running batch included)
	unsigned ackedSeq = 0; //!< Command batches finished; this is what status.json reports (#142), written just before their ack.txt (#23)
	unsigned commandReadRetries = 0; //!< Frames command.txt could not be opened (its writer still had it, #527)
	std::string lastCmd;
	std::string lastCmdError;

	unsigned captureSeq = 0;
	std::string lastCapture;
	std::string pendingCapture;

	//! The last shot's four quarters (#334): a picture that fills the window has detail in each of them
	struct CaptureQuarters {
		std::string name;
		Vec2i size = Vec2i(0);
		std::array<vr::PixelStats, 4> quarters; //!< top left, top right, bottom left, bottom right
	} capture;

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

	//! pick: what mouse picking finds on the window (#72)
	struct Pick {
		unsigned seq = 0;            //!< Picks done so far
		std::string center;          //!< The entity at the interface's center; empty when nothing is there
		//! The whole window on a grid: "<entity> <hits> <center x> <center y>" per entity found, most hits
		//! first, comma separated
		std::string scan;
		unsigned scanPoints = 0;
		std::string target;          //!< The entity asked for, if any
		unsigned targetHits = 0;     //!< Grid points where picking finds it
		Vec2f targetHit = Vec2f(-1.f);   //!< The middle of those points, interface pixels
		Vec2f targetSeen = Vec2f(-1.f);  //!< Where the camera sees the middle of its bounding box
		float targetOffset = -1.f;   //!< Distance between the two; -1 when either is missing
		std::string targetBox;       //!< Its bbox2D, "x0 y0 x1 y1": what picking tests first
		bool targetSeenInBox = false; //!< Where the camera sees it is inside that box
	} pick;

	//! vr-point: the widget a simulated hand was last aimed at (#34)
	struct PointTarget {
		std::string id = "none";     //!< the item's id, "pixel" for a bare pixel, the name of a menu target
		Vec2f pixel = Vec2f(0.f);    //!< in the 2D interface's pixels
		bool menu = false;           //!< a widget of the main menu or a target of the character creation (#394)
	} pointTarget;

	//! watch: the entity whose place status.json reports (#37), empty for none
	std::string watch;

	//! stick-for: a stick held until the player has walked a distance, let go by the game (#370)
	struct StickFor {
		unsigned seq = 0;            //!< Holds started so far
		bool active = false;
		std::string hand;
		Vec2f start = Vec2f(0.f);    //!< The player's ground position (x, z) when the hold started
		float units = 0.f;           //!< The distance that ends the hold
		float moved = 0.f;           //!< The distance walked so far (the last one, once ended)
		GameInstant deadline = 0;    //!< When the hold ends anyway: a blocked player never walks the distance
		bool timedOut = false;       //!< The hold ended at its deadline, not at its distance
		vr::WalkStatus walk;         //!< The stick walk of the hold's last frame that walked
	} stickFor;

	//! motion: the player's walk, turn and jumps since `motion reset`, fed after every frame (#23)
	vr::MotionProbe motion;
	u32 motionResets = 0;
	//! Writes status.json and ack.txt on its own thread (#553); deleted in shutdown only, never at static
	//! destruction (its thread flushes the logger)
	ChannelWriter * writer = nullptr;
	//! The writer did not flush arx.log after a status (lines came after it was built): the next frame builds one
	std::atomic<bool> freshStatus { false };

	//! The simulated headset's `record <name>`: the motion values of the frame it named, as JSON, in order
	std::vector<std::pair<std::string, std::string>> records;
	//! The free (left) hand's index finger: its bend and target last frame, where and when its approach started
	float curlLast = 0.f, curlTargetLast = 0.f, curlFrom = 0.f;
	double curlLastSeconds = 0.0, curlFromSeconds = 0.0;
	float curlSpeed = 0.f; //!< degrees per second since the target changed
	//! The hang watchdog of the main thread (#440), started at the first frame
	std::unique_ptr<platform::HangWatchdog> watchdog;
	bool watchdogLogged = false;     //!< Its "watching" line is in the log
	long watchdogThresholdMs = 5000; //!< Below the 10 s a test waits for an ack: the report comes first
	long stallLoadMs = 0;            //!< stall load: how long the next level load sleeps, 0 = it does not

	//! What the setters of `devvariants demo` got (#588): status.json devPanel.demo.*
	struct DevDemo {
		std::string choice = "none";
		float number = -1.f;
		u32 calls = 0;
	} devDemo;

	//! The running batch (#522): kept while a waitframes in it holds the ack until the frames are rendered
	struct Batch {
		std::vector<std::string> lines;
		size_t next = 0;             //!< The first line not run yet
		unsigned errors = 0;
		u64 resumeFrame = 0;         //!< The frame the rest of the batch runs in; 0 = not waiting
	} batch;

	//! A simulated headset's shot asked for through the channel: status.json is written once it is saved (#522)
	struct SimShotWatch {
		bool active = false;
		size_t shots = 0;            //!< The simulator's shots when it was asked for
		u64 lastFrame = 0;           //!< Watched up to this frame: a shot that never comes has the 100 ms status
	} simShotWatch;

	//! camera.stillMs (#522): when the first-person camera last moved more than a hair from where it was then
	struct CameraWatch {
		Vec3f pos = Vec3f(0.f);
		Vec3f angles = Vec3f(0.f);   //!< pitch, yaw, roll in degrees
		PlatformInstant movedAt = 0;
	} cameraWatch;

	//! levelstats (#596): the frame times since `levelstats start`, and what the last report found
	struct LevelStats {
		bool measuring = false;
		PlatformInstant lastFrame = 0; //!< The previous frame's top while measuring; 0 before the first one
		std::vector<s64> frameUs;      //!< Frame to frame, microseconds
		u32 reports = 0;
		u32 frames = 0;
		double meanMs = 0.0, p95Ms = 0.0, maxMs = 0.0;
		size_t exits = 0, ladders = 0;
		std::string listing;           //!< Every report of the run: <dir>\level-tour.txt
	} levelStats;

};

State g;

/*!
 * ui.menuHover (#394): the menu target a vr-point aimed at while the widget under the mouse is that one -
 * what a click lands on; "other" for another widget of the main menu, "none" for nothing
 */
std::string menuHover(const Vec2f & mouse) {
	const State::PointTarget & target = g.pointTarget;
	if(ARXmenu.mode() == Mode_MainMenu) {
		const Widget * under = menuWidgetAt(mouse);
		if(!under) {
			return "none";
		}
		std::string why;
		return (target.menu && findMenuWidget(target.id, why) == under) ? target.id : "other";
	}
	Rectf rect;
	if(ARXmenu.mode() == Mode_CharacterCreation && target.menu && g_characterCreation.targetRect(target.id, rect)
	   && rect.contains(mouse)) {
		return target.id;
	}
	// The open book's bookmarks (#377)
	std::string why;
	if(ARXmenu.mode() == Mode_InGame && target.menu && menuTargetRect(target.id, rect, why) && rect.contains(mouse)) {
		return target.id;
	}
	return "none";
}

//! When the delayed deliberate crash of "crash steal" fires (#132); zero = none pending
PlatformInstant g_crashAt = PlatformInstant(0);

// command.txt is looked for every frame (a stat; it is read only when it is there), status.json is
// replaced every 100 ms and after every batch, shot and state change (#522)
const s64 StatusIntervalMs = 100;

/*!
 * A part of the channel's work on the frame that took 20 ms or more is named in the log (#553): the perf log's hitch
 * line only says `cause=channel`, and what is left of the channel on the frame (reading command.txt, a batch's
 * commands, building the status, the records) is then told apart by this line
 */
void noteSlowPart(const char * part, PlatformInstant start) {
	const s64 ms = toMsi(platform::getTime() - start);
	if(ms >= 20) {
		LogInfo << "[channel] slow frame part: " << part << " took " << ms << " ms (frame " << g.frame << ", last command \""
		        << g.lastCmd << "\")";
	}
}

void setTestDir(const std::string & dir) {
	g.dir = dir;
	g.enabled = true;
}
ARX_PROGRAM_OPTION_ARG("test-dir", nullptr,
                       "Enable the test command channel and status file in DIR (see docs/vr/TESTING.md)",
                       &setTestDir, "DIR")

GameState currentState() {
	return gameevent::detectGameState();
}

void writeVec(JsonWriter & json, std::string_view name, const Vec3f & v) {
	json.object(name);
	json.value("x", v.x);
	json.value("y", v.y);
	json.value("z", v.z);
	json.end();
}

void writeAngles(JsonWriter & json, std::string_view name, const Vec3f & angles) {
	json.object(name);
	json.value("yaw", angles.x);
	json.value("pitch", angles.y);
	json.value("roll", angles.z);
	json.end();
}

void writePixelStats(JsonWriter & json, std::string_view name, const vr::PixelStats & stats) {
	json.object(name);
	json.value("verdict", vr::imageVerdictName(stats.verdict));
	json.value("mean", stats.mean);
	json.value("stddev", stats.stddev);
	json.value("black", stats.black);
	json.value("background", stats.background);
	json.end();
}

void writeSimStatus(JsonWriter & json, const vr::SimStatus & sim) {

	json.object("sim");
	json.value("script", sim.script);
	json.value("timeline", sim.timeline);
	json.value("frame", sim.frame);
	json.value("timelineFrame", sim.timelineFrame);
	json.value("timelineMs", float(sim.timelineMs));
	json.value("pending", u64(sim.pending));

	json.object("view");
	json.value("eyeWidth", sim.view.eyeSize.x);
	json.value("eyeHeight", sim.view.eyeSize.y);
	json.value("ipd", sim.view.ipd);
	json.value("rate", sim.view.rate);
	json.value("pacing", sim.view.rate <= 0.f ? "none" : (sim.view.cap ? "cap" : "clock")); // #522
	json.end();

	json.object("rig");
	writeVec(json, "head", sim.rig.head.position);
	writeAngles(json, "headRot", sim.rig.head.angles);
	writeVec(json, "leftHand", sim.rig.hands[0].position);
	writeVec(json, "rightHand", sim.rig.hands[1].position);
	json.value("profile", sim.rig.profile);
	json.value("focused", sim.rig.focused);
	json.value("swapEyes", sim.rig.swapEyes);
	// The real fingers the simulated hand trackers show (#305): joints on or off, how far closed (0 - 1)
	for(size_t hand = 0; hand < 2; hand++) {
		json.object(hand ? "rightFingers" : "leftFingers");
		json.value("joints", sim.rig.fingers[hand].joints);
		json.value("thumb", sim.rig.fingers[hand].curl.x);
		json.value("index", sim.rig.fingers[hand].curl.y);
		json.value("rest", sim.rig.fingers[hand].curl.z);
		json.end();
	}
	json.object("actions");
	for(const auto & [name, values] : sim.rig.actions) {
		json.object(name);
		json.value("left", values[0].x);
		json.value("right", values[1].x);
		json.end();
	}
	json.end();
	json.end();

	// A recorded clip streamed into the rig (#550, the `play` command): where it is, what it served, the drift
	if(sim.playing) {
		json.object("play");
		json.value("file", sim.play.file);
		json.value("positionMs", float(sim.play.positionMs));
		json.value("framesServed", sim.play.framesServed);
		json.value("done", sim.play.done);
		json.value("anchorsWaiting", u64(sim.play.anchorsWaiting));
		json.object("drift");
		json.value("current", sim.play.drift.current);
		json.value("max", sim.play.drift.max);
		json.value("frames", sim.play.drift.frames);
		json.end();
		json.value("eventsReplayed", sim.play.eventsReplayed);
		json.value("error", sim.playError);
		json.end();
	}

	// The eyes as the runtime located them last, in the room (meters; yaw / pitch in degrees)
	json.object("eyes");
	json.value("located", sim.located);
	writeVec(json, "left", sim.eyes[0].position);
	writeVec(json, "right", sim.eyes[1].position);
	writeAngles(json, "leftRot", vr::simAngles(sim.eyes[0].orientation));
	writeAngles(json, "rightRot", vr::simAngles(sim.eyes[1].orientation));
	json.value("separation", glm::length(sim.eyes[1].position - sim.eyes[0].position));
	json.end();

	// The compositor: both eye images of a frame must come from one frame (F-06, F-116)
	json.object("compositor");
	json.value("endedFrames", sim.endedFrames);
	json.value("projectionFrames", sim.projectionFrames);
	json.value("pairedFrames", sim.pairedFrames);
	json.value("staleFrames", sim.staleFrames);
	json.value("freshFrames", sim.freshFrames);
	json.value("quadFrames", sim.quadFrames);
	json.value("syncs", sim.syncs);
	json.end();

	json.value("pendingShots", u64(sim.pendingShots));
	json.value("shotCount", u64(sim.shots.size()));
	json.object("shots");
	for(const vr::SimShot & shot : sim.shots) {
		json.object(shot.name);
		json.value("frame", shot.frame);
		json.value("timelineFrame", shot.timelineFrame);
		json.value("swapEyes", shot.swapEyes);
		for(size_t eye = 0; eye < 2; eye++) {
			const vr::SimShotEye & e = shot.eyes[eye];
			json.object(eye == 0 ? "left" : "right");
			json.value("file", e.file);
			writePixelStats(json, "scene", e.scene);
			writePixelStats(json, "layer", e.layer);
			writeVec(json, "position", e.position);
			writeAngles(json, "rot", e.angles);
			json.value("imageFrame", e.imageFrame);
			json.value("quadPixels", u64(shot.quadPixels[eye]));
			json.value("overlayPixels", u64(shot.overlayPixels[eye]));
			json.end();
		}
		json.object("quad");
		json.value("shown", shot.quadShown);
		json.value("file", shot.quadFile);
		writePixelStats(json, "stats", shot.quad);
		json.value("covered", u64(shot.quadCovered));
		json.end();
		json.object("lr");
		json.value("diff", shot.lrDiff);
		json.value("shift", shot.lrShift.cells);
		json.value("shiftFraction", shot.lrShift.fraction);
		json.value("residual", shot.lrShift.residual);
		json.value("unshifted", shot.lrShift.unshifted);
		json.value("separation", glm::length(shot.eyes[1].position - shot.eyes[0].position));
		json.end();
		json.object("versus");
		for(const auto & [other, diff] : shot.versus) {
			json.object(other);
			json.value("left", diff[0]);
			json.value("right", diff[1]);
			json.end();
		}
		json.end();
		json.end();
	}
	json.end();

	json.end();
}

//! The test playground's entities (#321.2) tell the channel what they are: their instance scripts set the
//! text variable pg_role ("npc", "lever_mode", "lever_respawn", "door", "chest", "corpse", scripts/dev/playground/build.mjs)
Entity * findPlaygroundEntity(std::string_view role) {
	for(Entity & entity : entities) {
		if(GETVarValueText(entity.m_variables, "\xA3pg_role") == role) {
			return &entity;
		}
	}
	return nullptr;
}

//! The playground NPC fights: its instance script's aggro variable (the mode lever sets it)
bool playgroundNpcAggressive(const Entity & npc) {
	return GETVarValueLong(npc.m_variables, "\xA7" "aggro") == 1;
}

bool playerKnowsAllRunes() {
	static constexpr RuneFlag runes[] = {
		FLAG_AAM, FLAG_CETRIUS, FLAG_COMUNICATUM, FLAG_COSUM, FLAG_FOLGORA, FLAG_FRIDD, FLAG_KAOM, FLAG_MEGA, FLAG_MORTE,
		FLAG_MOVIS, FLAG_NHI, FLAG_RHAA, FLAG_SPACIUM, FLAG_STREGUM, FLAG_TAAR, FLAG_TEMPUS, FLAG_TERA, FLAG_VISTA,
		FLAG_VITAE, FLAG_YOK,
	};
	return std::all_of(std::begin(runes), std::end(runes), [](RuneFlag rune) {
		return (player.rune_flags & rune) != 0;
	});
}

//! The items in an entity's inventory (an item fills several slots: counted once, at its first one)
size_t inventoryItemCount(const Entity & owner) {
	size_t count = 0;
	if(owner.inventory) {
		for(auto slot : owner.inventory->slots()) {
			if(slot.entity && slot.show) {
				count++;
			}
		}
	}
	return count;
}

//! status.json, area 30: the playground's NPC, its mode and the items in the room (#321.2)
void writePlaygroundStatus(JsonWriter & json) {

	size_t items = 0;
	for(const Entity & entity : entities) {
		if((entity.ioflags & IO_ITEM) && entity.show == SHOW_FLAG_IN_SCENE) {
			items++;
		}
	}

	const Entity * npc = findPlaygroundEntity("npc");
	json.object("playground");
	json.value("npc", npc ? npc->idString() : std::string("none"));
	if(npc && npc->_npcdata) {
		const IO_NPCDATA & data = *npc->_npcdata;
		json.value("npcMode", playgroundNpcAggressive(*npc) ? "aggressive" : "peaceful");
		json.value("npcAlive", data.lifePool.current > 0.f);
		// What a VR touch may reach (#465): both of the fight behaviour and the player as the target
		json.value("npcFighting", fightsPlayer(*npc));
		json.value("npcFightBehaviour", (data.behavior & BEHAVIOUR_FIGHT) != 0);
		json.value("npcTargetsPlayer", npc->targetinfo == EntityHandle_Player);
		// Units from the hero on the floor plane: whether the NPC stands where a sequence put it
		json.value("npcDistance", glm::distance(Vec2f(npc->pos.x, npc->pos.z), Vec2f(player.pos.x, player.pos.z)));
		json.value("npcLife", data.lifePool.current);
		json.value("npcLifeMax", data.lifePool.max);
		// Back at the place the level put it (revive -i takes it there): within a step, floor plane only
		json.value("npcAtSpot", glm::distance(Vec2f(npc->pos.x, npc->pos.z), Vec2f(npc->initpos.x, npc->initpos.z)) < 50.f);
	}
	json.value("items", items);

	// The levers, the door, the chest and the body worked by hand (#38): their scripts' own state
	for(std::string_view role : { "lever_mode", "lever_respawn" }) {
		const Entity * lever = findPlaygroundEntity(role);
		json.object(role == "lever_mode" ? "leverMode" : "leverRespawn");
		json.value("id", lever ? lever->idString() : std::string("none"));
		if(lever) {
			json.value("pulled", GETVarValueLong(lever->m_variables, "\xA7" "pulled") == 1);
		}
		json.end();
	}
	const Entity * door = findPlaygroundEntity("door");
	json.object("door");
	json.value("id", door ? door->idString() : std::string("none"));
	if(door) {
		json.value("open", GETVarValueLong(door->m_variables, "\xA7" "open") == 1);
		json.value("locked", GETVarValueLong(door->m_variables, "\xA7" "unlock") == 0);
	}
	json.end();
	for(std::string_view role : { "chest", "corpse" }) {
		const Entity * container = findPlaygroundEntity(role);
		json.object(role);
		json.value("id", container ? container->idString() : std::string("none"));
		if(container) {
			json.value("open", g_secondaryInventoryHud.openContainer() == container);
			json.value("items", inventoryItemCount(*container));
			if(container->_npcdata) {
				json.value("alive", container->_npcdata->lifePool.current > 0.f);
			}
		}
		json.end();
	}
	json.end();

}

/*!
 * The values the refresh-rate check compares (#23), as they are after the frame was updated: the motion
 * probe's totals, the head-tracked camera, the body's follow of the head, the weapon arm and the free
 * (left) hand's fingers, the swing detector. Written as \c motion and kept as a record when the
 * simulated headset asks
 */
void writeMotionValues(JsonWriter & json) {

	const vr::SimStatus sim = vr::simStatus();
	json.value("timelineMs", float(sim.timelineMs));
	json.value("frames", g.motion.frames());
	json.value("seconds", float(g.motion.seconds()));
	json.value("walked", g.motion.walked());
	json.value("turned", g.motion.turned());
	json.value("jumps", g.motion.jumps());
	json.value("airborne", g.motion.airborne());
	json.value("jumpApex", g.motion.jumpApex());
	json.value("jumpSeconds", float(g.motion.jumpSeconds()));
	json.value("yaw", player.angle.getYaw());
	json.value("cameraYaw", g_playerCamera.angle.getYaw());
	// The head's part of the camera's yaw (D-018): the camera against the heading, (-180, 180]; on the simulated
	// headset how far it is behind the head itself in this frame - 0 when the camera is where the head is, however
	// late the frame came (a late frame moves both)
	const float cameraTurn = vr::wrapSignedDegrees(g_playerCamera.angle.getYaw() - player.angle.getYaw());
	json.value("cameraTurn", cameraTurn);
	if(sim.active) {
		json.value("headYaw", sim.rig.head.angles.x);
		json.value("cameraLag", vr::wrapSignedDegrees(sim.rig.head.angles.x - cameraTurn));
	}
	json.value("cameraPitch", g_playerCamera.angle.getPitch());
	json.value("bodyOffset", vr::bodyStatus().offset);

	// The weapon hand's arm and the free hand's fingers (a hand holding a weapon is closed on its grip, #305)
	const vr::PlayerBodyStatus & body = vr::playerBodyStatus();
	json.value("armError", body.hands[size_t(vr::Hand::Right)].error);
	const vr::PlayerArmStatus & free = body.hands[size_t(vr::Hand::Left)];
	json.value("curl", free.curl.indexRoot);
	json.value("curlTarget", free.curlTarget.indexRoot);
	// How fast the bend went since its target last changed, degrees per second of the frame clock - the fingers'
	// smoothing as a speed, the same whichever frame reads it (until the bend arrives)
	json.value("curlSpeed", g.curlSpeed);

	const vr::MeleeStatus & melee = vr::meleeStatus();
	json.value("swings", melee.swings);
	json.value("swingKind", melee.kind);
	json.value("swingSpeed", melee.speed);
	json.value("swingPeak", melee.peak);
	json.value("swingJumps", melee.jumps);
	json.value("swingGaps", melee.gaps);
	}

//! status.json \c scene: the scene view of scripted scenes in VR, its meter and its subtitle (#29, SPEC.md)
void writeSceneStatus(JsonWriter & json) {

	const vr::SceneState & scene = vr::sceneState();
	const vr::SceneMeter & meter = scene.meter;
	const vr::SubtitleStatus & subtitle = vr::subtitleStatus();

	json.object("scene");
	json.value("active", scene.active);
	json.value("kind", vr::sceneKindName(scene.kind));
	json.value("view", vr::sceneViewName(scene.view));
	json.value("shown", vr::sceneShownName(scene.director.shown()));
	json.value("lookat", vr::sceneLookAtName(scene.lookAt));
	json.value("firstLow", vr::sceneFirstLowName(scene.firstLow));
	json.value("cut", vr::sceneCutName(scene.cut));
	json.value("fade", scene.director.fade());
	json.value("armsIk", scene.armsIk);
	json.value("dialogue", scene.director.dialogue());
	// The hero's head: hidden by the first-person selection or by the VR body's head-only view (the eyes inside it)
	json.value("heroHead", (HERO_SHOW_1ST == 1 || vr::hidesHeadOnly()) ? "hidden" : "drawn");
	json.object("meter");
	json.value("frames", meter.frames());
	json.value("sceneFrames", meter.sceneFrames());
	json.value("screenFrames", meter.screenFrames());
	json.value("minAboveFloorM", meter.minAboveFloorM());
	json.value("maxStepCm", meter.maxStepCm());
	json.value("maxYawStepDeg", meter.maxYawStepDeg());
	json.value("cuts", meter.cuts());
	json.value("fadedCuts", meter.fadedCuts());
	json.value("instantCuts", meter.instantCuts());
	json.value("unexplainedCuts", meter.unexplainedCuts());
	json.value("lookats", meter.lookats());
	json.value("fadedLookats", meter.fadedLookats());
	json.value("letterboxFrames", meter.letterboxFrames());
	json.value("blindFrames", meter.blindFrames());
	json.value("openingFrames", meter.openingFrames());
	json.value("worldFrames", meter.worldFrames());
	json.value("fadeFrames", meter.fadeFrames());
	json.end();
	json.object("subtitle");
	json.value("visible", subtitle.visible);
	json.value("text", subtitle.text);
	json.value("speaker", subtitle.speaker);
	json.value("placement", vr::subtitlePlacementName(subtitle.placement));
	json.value("lines", subtitle.lines);
	json.value("voicedLines", subtitle.voicedLines);
	// The panel fixed in the world for the line (#29): drawn into both eyes this frame, the pixels it covers, its pose
	// in LOCAL meters and degrees
	json.value("drawn", vr::subtitlePlacementName(subtitle.drawn));
	json.value("panel", subtitle.panel);
	json.value("panelPixels", subtitle.panelPixels);
	json.value("panelYawFromHeadDeg", subtitle.panelYawFromHeadDeg);
	json.object("panelLocal");
	json.value("x", subtitle.panelLocal.x);
	json.value("y", subtitle.panelLocal.y);
	json.value("z", subtitle.panelLocal.z);
	json.value("yawDeg", subtitle.panelYawDeg);
	json.value("widthM", subtitle.panelWidthM);
	json.end();
	// A notice of the game's own in the panel instead of a line (#551): none, marked, recording
	json.value("notice", vr::subtitleNoticeName(subtitle.notice));
	json.end();
	json.end();

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
	json.value("menuPage", ARXmenu.mode() == Mode_MainMenu ? menuPageName(currentMenuPage()) : "none");
	json.value("menuRows", ARXmenu.mode() == Mode_MainMenu ? visibleVRSettingRows() : std::string());

	// The next-launch crash notice (#545, M6): shown at the main menu until OK, and the report it names
	json.object("crashNotice");
	json.value("state", crashNoticeShown() ? "shown" : "none");
	json.value("report", crashNoticeShown() ? crashNoticeReport() : std::string());
	json.end();
	json.value("area", g_currentArea ? u32(g_currentArea) : 0u);
	json.value("paused", g_gameTime.isPaused() != 0);
	// The game clock: it runs while a VR radial menu is open (#376), stops in the pause menu
	json.value("gameTimeMs", toMsi(g_gameTime.now()));
	json.value("cinematic", isInCinematic());
	// The timed texts at the top of the 2D interface (a script's tutorial hint): a shot of the HUD that must not
	// change waits for none (#370)
	json.value("notifications", u64(notification_count()));
	// The level tour's last `levelstats report` (#596)
	json.object("levelStats");
	json.value("measuring", g.levelStats.measuring);
	json.value("reports", g.levelStats.reports);
	json.value("frames", g.levelStats.frames);
	json.value("meanMs", float(g.levelStats.meanMs));
	json.value("p95Ms", float(g.levelStats.p95Ms));
	json.value("maxMs", float(g.levelStats.maxMs));
	json.value("exits", u64(g.levelStats.exits));
	json.value("ladders", u64(g.levelStats.ladders));
	json.end();

	if(g_currentArea) {
		json.object("player");
		json.value("x", player.pos.x);
		json.value("y", player.pos.y);
		json.value("z", player.pos.z);
		json.value("yaw", player.angle.getYaw());
		// Where the player model faces, in the same terms as yaw (#25: the body yaw in VR, yaw in flat mode)
		if(entities.player()) {
			json.value("modelYaw", MAKEANGLE(180.f - entities.player()->angle.getYaw()));
		}
		json.value("life", player.lifePool.current);
		json.value("lifeMax", player.lifePool.max);
		json.value("level", int(player.level));
		json.value("allRunes", playerKnowsAllRunes());
		json.value("mana", player.manaPool.current);
		json.value("manaMax", player.manaPool.max);
		// Scripts block the controls during scripted scenes (the awakening of a new game): a save made then is not playable (#70)
		json.value("controlsBlocked", BLOCK_PLAYER_CONTROLS);
		// What the controls did (#74): the inventory and the book open, the crouch toggle, a jump under way
		json.value("inventoryOpen", (player.Interface & INTER_INVENTORY) != 0);
		json.value("inventoryItems", entities.player() ? inventoryItemCount(*entities.player()) : size_t(0));
		// Freelook (TRUE_PLAYER_MOUSELOOK_ON): off is the engine's cursor mode, which keeps the inventory open
		// and refuses melee; in VR always on in the game (#394)
		json.value("freelook", TRUE_PLAYER_MOUSELOOK_ON);
		json.value("bookOpen", (player.Interface & INTER_PLAYERBOOK) != 0);
		// The page the book shows (or last showed): stats, spells, map, quests (#376)
		const char * const bookPages[] = { "stats", "spells", "map", "quests" };
		const size_t bookPage = size_t(g_playerBook.currentPage());
		json.value("bookPage", bookPage < std::size(bookPages) ? bookPages[bookPage] : "?");
		json.value("torch", player.torch ? player.torch->idString() : std::string("none"));
		json.value("crouchToggle", bGCroucheToggle);
		// The character crouches - the button, the toggle, the head (#100) or a low ceiling
		json.value("crouched", (player.m_currentMovement & PLAYER_CROUCH) != 0);
		json.value("jumping", player.jumpphase != NotJumping);
		// The stealth walk in this update: the key or a slight push of the stick (#33)
		json.value("stealth", (player.m_currentMovement & PLAYER_MOVE_STEALTH) != 0);
		// The bag's slide in or out, 0 = fully open: a shot of the open bag waits for it (#522)
		json.value("inventorySlide", s64(g_playerInventoryHud.inventorySlide()));
		// The speed the current body animation moves the player at, units per second - what the engine walks
		// with (ARX_PLAYER_Manage_Movement), independent of walls and the frame rate: the run vs the sneak (#33)
		if(entities.player() && entities.player()->animlayer[0].cur_anim) {
			const AnimLayer & layer0 = entities.player()->animlayer[0];
			const float duration = toMsf(layer0.cur_anim->anims[layer0.altidx_cur]->anim_time);
			const Vec3f travel = GetAnimTotalTranslate(layer0.cur_anim, layer0.altidx_cur);
			json.value("animSpeed", duration > 0.f ? glm::length(travel) / duration * 1000.f : 0.f);
		}
		json.end();
		if(isPlaygroundArea(g_currentArea)) {
			writePlaygroundStatus(json);
		}
		// The first-person camera after this frame's effects: bob = how far the camera left the stable head
		// position (view bobbing), shake = the quake amplitude applied, fov in degrees (zoom) - the numbers
		// the VR comfort policy (#76) keeps at 0 / cfg.ini fov
		json.object("camera");
		json.value("x", g_playerCamera.m_pos.x);
		json.value("y", g_playerCamera.m_pos.y);
		json.value("z", g_playerCamera.m_pos.z);
		json.value("pitch", g_playerCamera.angle.getPitch());
		json.value("yaw", g_playerCamera.angle.getYaw());
		json.value("roll", g_playerCamera.angle.getRoll());
		json.value("fov", glm::degrees(g_playerCamera.fov()));
		json.value("bob", glm::distance(g_playerCamera.m_pos, g_playerCameraStablePos));
		// Where the view is above the character's feet (game units, y down): the character's eye in flat
		// mode; in VR the eye plus the head's movement since its reference (#27)
		json.value("eyeAboveFeet", player.basePosition().y - g_playerCameraStablePos.y);
		json.value("shake", quakeAmplitude());
		// How long the camera has not moved (#522): a test that aims from it waits until it settled
		json.value("stillMs", toMsi(now - g.cameraWatch.movedAt));
		// The camera the last frame's interface pass ended with (the status is written before this
		// frame's update prepares a new one): the projection's scale (m00, m11) and off-axis shift
		// (m20, m21) and the viewport - in VR the head camera's union projection, also with the book
		// open (#247)
		json.object("prepared");
		json.value("m00", g_preparedCamera.m_viewToClip[0][0]);
		json.value("m11", g_preparedCamera.m_viewToClip[1][1]);
		json.value("m20", g_preparedCamera.m_viewToClip[2][0]);
		json.value("m21", g_preparedCamera.m_viewToClip[2][1]);
		json.value("width", g_preparedCamera.m_viewport.width());
		json.value("height", g_preparedCamera.m_viewport.height());
		json.end();
		json.end();
		// The refresh-rate check (#23): totals since `motion reset` as of the last frame, and the records the
		// simulated headset's `record <name>` took after the frame it named
		json.object("motion");
		writeMotionValues(json);
		json.value("resets", g.motionResets);
		json.object("records");
		for(const auto & record : g.records) {
			json.raw(record.first, record.second);
		}
		json.end();
		json.end();
		// Spells by type: active now, and launched since the level was loaded - the evidence that a spellcast
		// really started a spell (#155: queued casts on the player never launch, TRAPS.md)
		std::array<size_t, SPELL_TYPES_COUNT> active = {};
		size_t activeTotal = 0;
		for(const Spell & spell : spells) {
			if(spell.m_type >= 0 && size_t(spell.m_type) < active.size()) {
				active[size_t(spell.m_type)]++;
			}
			activeTotal++;
		}
		size_t launchedTotal = 0;
		for(size_t i = 0; i < SPELL_TYPES_COUNT; i++) {
			launchedTotal += spells.launchedCount(SpellType(i));
		}
		json.object("spells");
		json.value("active", activeTotal);
		json.value("launched", launchedTotal);
		json.object("activeByType");
		for(size_t i = 0; i < SPELL_TYPES_COUNT; i++) {
			if(const char * name = getSpellName(SpellType(i)); name && active[i] != 0) {
				json.value(name, active[i]);
			}
		}
		json.end();
		json.object("launchedByType");
		for(size_t i = 0; i < SPELL_TYPES_COUNT; i++) {
			const char * name = getSpellName(SpellType(i));
			if(size_t count = spells.launchedCount(SpellType(i)); name && count != 0) {
				json.value(name, count);
			}
		}
		json.end();
		json.end();
	}

	writeSceneStatus(json);

	// Marks of the moment in the headset (#551): both trackpads pressed, the marker folder, the voice note
	{
		const vr::marks::MarksStatus & marks = vr::marks::status();
		json.object("marks");
		json.value("count", marks.count);
		json.value("last", marks.last);
		json.value("lastFiles", marks.lastFiles);
		// Marks v2 (#574): pending until the release decides; then press or hold
		json.value("lastKind", marks.lastKind);
		json.value("lastSnapshots", marks.lastSnapshots);
		json.value("lastClipMs", marks.lastClipMs);
		json.value("recording", marks.recording);
		json.value("noteMs", marks.noteMs);
		json.value("note", marks.note);
		// The gesture of the controllers in use: both trackpads on the Index, none without trackpads (the Touch)
		json.value("gesture", vr::markGestureAvailable(vr::controllerType()) ? "both.trackpad" : "none");
		json.end();
	}

	// The dev variants panel on the book's map page (#588)
	{
		const devvariants::PanelStatus panel = devvariants::panelStatus();
		json.object("devPanel");
		json.value("enabled", g_devLaunch);
		json.value("view", devvariants::mapPageViewName(panel.view));
		json.value("visible", panel.visible);
		json.value("groups", u64(devvariants::count()));
		json.value("page", u64(panel.page));
		json.value("pages", u64(panel.pages));
		json.object("value");
		for(const devvariants::Entry & entry : devvariants::entries()) {
			if(entry.kind == devvariants::Kind::Number) {
				json.value(entry.group, entry.value);
			} else {
				json.value(entry.group, devvariants::valueText(entry.group));
			}
		}
		json.end();
		json.object("demo");
		json.value("choice", g.devDemo.choice);
		json.value("number", g.devDemo.number);
		json.value("calls", g.devDemo.calls);
		json.end();
		json.end();
	}

	{
		const vr::ComfortPolicy & policy = vr::comfort();
		json.object("comfort");
		json.value("mode", vr::comfortModeName(vr::comfortMode()));
		json.value("active", vr::comfortActive());
		json.value("viewBobbing", policy.viewBobbing);
		json.value("screenShake", policy.screenShake);
		json.value("fovZoom", policy.fovZoom);
		json.end();
	}

	// The 2D interface under the mouse (#34): what a click there would take, and what is being dragged
	json.object("ui");
	if(GInput) {
		json.value("mouseX", GInput->getMousePosition().x);
		json.value("mouseY", GInput->getMousePosition().y);
		json.value("mouseInWindow", GInput->isMouseInWindow());
	}
	json.value("hover", FlyingOverIO ? FlyingOverIO->idString() : std::string("none"));
	json.value("drag", g_draggedEntity ? g_draggedEntity->idString() : std::string("none"));
	// The container whose inventory is open: a chest, a corpse (#38)
	const Entity * container = g_secondaryInventoryHud.openContainer();
	json.value("container", container ? container->idString() : std::string("none"));
	json.value("target", g.pointTarget.id);
	json.value("targetX", g.pointTarget.pixel.x);
	json.value("targetY", g.pointTarget.pixel.y);
	json.value("menuHover", GInput ? menuHover(Vec2f(GInput->getMousePosition())) : std::string("none"));
	// A main menu entry can be greyed out: Resume game in a user directory without a save
	if(g.pointTarget.menu && ARXmenu.mode() == Mode_MainMenu) {
		std::string why;
		const Widget * widget = findMenuWidget(g.pointTarget.id, why);
		json.value("menuTargetEnabled", widget && widget->isEnabled());
	}
	json.end();

	// The desktop mouse (#338): in VR the window only mirrors the headset, the real mouse is dropped
	if(GInput) {
		const Input::DesktopMouseStatus & mouse = GInput->desktopMouseStatus();
		json.object("desktopMouse");
		json.value("ignored", mouse.ignored);
		json.value("droppedButtons", mouse.droppedButtons);
		json.value("droppedWheel", mouse.droppedWheel);
		json.value("droppedMoves", mouse.droppedMoves);
		json.end();
		// ... and the keyboard (#487)
		const InputBackend::DroppedInput keyboard = GInput->droppedKeyboard();
		json.object("desktopKeyboard");
		json.value("ignored", keyboard.ignored);
		json.value("droppedKeys", keyboard.keys);
		json.value("droppedText", keyboard.text);
		json.end();
		// A test run's window takes no real mouse or keyboard, only what the channel injects (#487)
		const InputBackend::DroppedInput real = GInput->droppedRealInput();
		json.object("realInput");
		json.value("ignored", real.ignored);
		json.value("droppedKeys", real.keys);
		json.value("droppedText", real.text);
		json.value("droppedButtons", real.buttons);
		json.value("droppedWheel", real.wheel);
		json.value("droppedMoves", real.moves);
		json.end();
	}

	// The hero as the character creation makes it (#394): the attributes and skills before modifiers, the
	// points still to spend
	if(ARXmenu.mode() == Mode_CharacterCreation || g_currentArea) {
		json.object("hero");
		json.value("strength", player.m_attribute.strength);
		json.value("mind", player.m_attribute.mind);
		json.value("dexterity", player.m_attribute.dexterity);
		json.value("constitution", player.m_attribute.constitution);
		json.value("attributePoints", int(player.Attribute_Redistribute));
		json.value("skillPoints", int(player.Skill_Redistribute));
		json.object("skills");
		json.value("stealth", player.m_skill.stealth);
		json.value("mecanism", player.m_skill.mecanism);
		json.value("intuition", player.m_skill.intuition);
		json.value("etheral_link", player.m_skill.etheralLink);
		json.value("object_knowledge", player.m_skill.objectKnowledge);
		json.value("casting", player.m_skill.casting);
		json.value("close_combat", player.m_skill.closeCombat);
		json.value("projectile", player.m_skill.projectile);
		json.value("defense", player.m_skill.defense);
		json.end();
		json.value("skin", int(player.skin));
		json.end();
	}

	// A setting the menu sequence changes by pointer and a second launch reads back from cfg.ini (#394)
	json.object("settings");
	json.value("crosshair", config.interface.showCrosshair);
	json.end();

	// One entity followed by the watch command (#37): where it is and whether it is in an inventory
	{
		Entity * watched = g.watch.empty() ? nullptr : entities.getById(g.watch);
		json.object("watch");
		json.value("id", g.watch.empty() ? std::string("none") : g.watch);
		json.value("found", watched != nullptr);
		if(watched) {
			json.value("show", int(watched->show));
			json.value("inScene", watched->show == SHOW_FLAG_IN_SCENE);
			json.value("inInventory", bool(locateInInventories(watched)));
			json.value("dragged", watched == g_draggedEntity);
			json.value("x", watched->pos.x);
			json.value("y", watched->pos.y);
			json.value("z", watched->pos.z);
		}
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
		// A test run's window (#487): never activated, placed in the corner of the main display
		json.value("activation", window.isBackground() ? "never" : "normal");
		json.value("placement", window.isBackground() ? "corner" : "default");
		json.value("x", window.getPosition().x);
		json.value("y", window.getPosition().y);
		json.end();
	}

	json.object("vr");
	json.value("supported", vr::isSupported());
	json.value("enabled", vr::isEnabled());
	json.value("sessionState", vr::sessionStateString());
	vr::FrameCounters frames = vr::frameCounters();
	json.value("frames", frames.submitted);
	json.value("shownFrames", frames.shown);
	json.value("resubmittedFrames", frames.resubmitted);
	json.value("discardedFrames", frames.discarded);
	json.value("frameGaps", frames.gaps);
	json.value("frameDivisor", vr::frameDivisor());
	// Stereo (#19): the scene drawn once per eye with its own projection; angles in degrees
	{
		vr::StereoStatus stereo = vr::stereoStatus();
		json.object("stereo");
		json.value("active", stereo.active);
		json.value("frames", stereo.frames);
		json.value("eyeWidth", stereo.eyeSize.x);
		json.value("eyeHeight", stereo.eyeSize.y);
		json.value("ipdMm", stereo.eyeSeparationM * 1000.f);
		json.value("cantDeg", stereo.cantDeg);
		const struct { const char * name; const vr::EyeFov & fov; } fovs[] = {
			{ "fovLeft", stereo.fov[0] }, { "fovRight", stereo.fov[1] }, { "fovUnion", stereo.unionFov },
		};
		for(const auto & entry : fovs) {
			json.object(entry.name);
			json.value("left", glm::degrees(entry.fov.left));
			json.value("right", glm::degrees(entry.fov.right));
			json.value("up", glm::degrees(entry.fov.up));
			json.value("down", glm::degrees(entry.fov.down));
			json.end();
		}
		json.end();
	}
	// The interface's virtual screen (#73): the window in flat mode, the quad's own resolution in VR
	{
		vr::StereoStatus stereo = vr::stereoStatus();
		json.object("interface");
		json.value("width", g_size.width());
		json.value("height", g_size.height());
		const Vec2i window = (mainApp && mainApp->getWindow()) ? mainApp->getWindow()->getSize() : Vec2i(0);
		json.value("windowWidth", window.x);
		json.value("windowHeight", window.y);
		json.value("quadWidth", stereo.interfaceSize.x);
		json.value("quadHeight", stereo.interfaceSize.y);
		json.value("eyePxPerDeg", stereo.eyePixelsPerDegree);
		json.end();
	}
	// The desktop mirror (#21): "on" while the window shows what one eye sees
	vr::MirrorStatus mirror = vr::mirrorStatus();
	json.object("mirror");
	json.value("state", mirror.state);
	json.value("eye", mirror.eye);
	json.value("frames", mirror.frames);
	json.value("overlays", mirror.overlays);
	json.end();
	// The floating UI panel (#49): "world" while an open menu's panel is placed, its pose in the LOCAL space
	{
		vr::PanelStatus panel = vr::panelStatus();
		json.object("panel");
		json.value("mode", panel.mode);
		json.value("shown", vr::panelShown()); // placed for an open book, bag, container, note or menu (#469)
		json.value("menu", panel.menu);
		json.value("placements", panel.placements);
		json.value("lastPlacement", panel.lastPlacement);
		json.value("x", panel.position.x);
		json.value("y", panel.position.y);
		json.value("z", panel.position.z);
		json.value("yaw", panel.yawDeg);
		json.value("tilt", panel.tiltDeg);
		json.value("width", panel.size.x);
		json.value("height", panel.size.y);
		json.value("distance", panel.distance);
		json.value("drop", panel.drop);
		json.end();
	}
	// The menu pointer (#34): the hand on the panel is the mouse; cursor in the 2D interface's pixels
	{
		vr::PointerStatus pointer = vr::pointerStatus();
		json.object("pointer");
		json.value("shown", pointer.shown);
		json.value("active", pointer.active);
		json.value("owner", pointer.owner);
		json.value("x", pointer.cursor.x);
		json.value("y", pointer.cursor.y);
		json.value("beam", pointer.beam);
		json.value("leftOnPanel", pointer.onPanel[0]);
		json.value("rightOnPanel", pointer.onPanel[1]);
		json.value("clicks", pointer.clicks);
		json.value("offPanelPresses", pointer.offPanelPresses);
		json.value("ownerChanges", pointer.ownerChanges);
		json.value("leftOnWorld", pointer.onWorld[0]);
		json.value("rightOnWorld", pointer.onWorld[1]);
		json.value("worldClicks", pointer.worldClicks);
		json.end();
	}
	// The hand reticles in the world (#37): each hand's hidden ray, what it hits and hovers, the ring
	{
		const vr::ReticleStatus & reticle = vr::reticleStatus();
		json.object("reticle");
		json.value("on", reticle.on);
		json.value("off", reticle.off);
		json.value("cursor", reticle.cursor);
		json.value("cursorEntity", reticle.cursorEntity);
		json.value("presses", reticle.presses);
		// Acts on doors, levers, chests, NPCs and corpses (#38): how many, the last one and its hand
		json.value("acts", reticle.acts);
		json.value("lastAct", reticle.lastAct);
		json.value("lastActHand", reticle.lastActHand);
		json.value("hoverChanges", reticle.hoverChanges);
		json.value("picks", reticle.picks);
		json.value("blocked", reticle.blocked);
		const char * const names[] = { "left", "right" };
		for(size_t hand = 0; hand < reticle.hands.size(); hand++) {
			const vr::ReticleHandStatus & h = reticle.hands[hand];
			json.object(names[hand]);
			json.value("aimed", h.aimed);
			json.value("hit", h.hit);
			json.value("onWorld", h.onWorld);
			json.value("ring", h.ring);
			json.value("shown", h.shown);
			json.value("bright", h.bright);
			json.value("under", h.under);
			json.value("entity", h.entity);
			json.value("distance", h.distance);
			json.value("pixelX", int(h.pixel.x));
			json.value("pixelY", int(h.pixel.y));
			writeVec(json, "origin", h.origin);
			writeVec(json, "direction", h.direction);
			writeVec(json, "point", h.point);
			json.end();
		}
		json.end();
	}
	json.value("runtime", vr::runtimeName());
	json.value("manifest", vr::runtimeManifest());
	// Head tracking (#24): where the head comes from, its reference and what it does to the camera
	{
		const vr::HeadStatus & head = vr::headStatus();
		json.object("head");
		json.value("source", head.source);
		json.value("referenced", head.referenced);
		json.value("references", head.references);
		json.value("reason", head.reason);
		json.value("refY", head.reference.position.y);
		json.value("refYaw", head.reference.yaw);
		// The reference is the worn, settled head (#27): its kind, whether a new one is still wanted, the
		// headset worn, the head above the room's floor (meters, -1 without a STAGE space) at the reference
		// and now, the character's eye above its feet (game units, -1 when the camera is not the player's)
		json.value("referenceKind", vr::referenceKindName(head.referenceKind));
		json.value("pending", head.pending);
		json.value("worn", head.worn);
		json.value("refAboveFloor", head.referenceAboveFloor);
		json.value("aboveFloor", head.aboveFloor);
		json.value("raised", head.raised);
		json.value("heightOffset", head.heightOffset);
		json.value("eyeAboveFeet", head.eyeAboveFeet);
		json.value("unitsPerMeter", vr::UnitsPerMeter);
		json.value("pitch", head.head.getPitch());
		json.value("yaw", head.head.getYaw());
		json.value("roll", head.head.getRoll());
		json.value("x", head.position.x);
		json.value("y", head.position.y);
		json.value("z", head.position.z);
		json.value("offsetX", head.offset.x);
		json.value("offsetY", head.offset.y);
		json.value("offsetZ", head.offset.z);
		// Leaning and real steps, the wall fade (#26): the head against the body, what the body walked after it
		json.value("leanX", head.lean.x);
		json.value("leanZ", head.lean.y);
		json.value("leanM", head.leanDistance);
		json.value("anchorX", head.anchor.x);
		json.value("anchorZ", head.anchor.y);
		json.value("walkedM", head.walked);
		json.value("stepping", head.stepping);
		json.value("steps", head.steps);
		json.value("bodyBlocked", head.bodyBlocked);
		json.value("blocks", head.blocks);
		const vr::FadeStatus & fade = vr::fadeStatus();
		json.value("fade", fade.fade);
		json.value("wall", fade.wall ? fade.wall : "");
		json.value("walls", fade.walls);
		json.value("farFade", fade.farFade);
		json.value("fars", fade.fars);
		// Physical crouch (#100): the head crouching the character, its depth below the reference (meters), the
		// episodes, and how far the character's crouch lowered its eye (game units)
		json.value("crouch", head.crouch);
		json.value("crouchDepth", head.crouchDepth);
		json.value("crouches", head.crouches);
		json.value("characterDrop", head.characterDrop);
		// The look and the body (#25): where the stick's forward is and where the model faces
		const vr::BodyStatus & body = vr::bodyStatus();
		json.value("lookYaw", body.lookYaw);
		json.value("bodyYaw", body.bodyYaw);
		json.value("bodyOffset", body.offset);
		json.value("bodyDragged", body.dragged);
		json.value("bodyDrags", body.drags);
		json.end();
	}
	// The full body (#36): the body under the head, both arms posed towards the controllers; per hand the
	// controller (where the player's hand is) and the posed joints (where the model's hand is), F-185
	{
		const vr::PlayerBodyStatus & body = vr::playerBodyStatus();
		json.object("body");
		json.value("bound", body.bound);
		json.value("model", body.model);
		json.value("chains", body.chains);
		json.value("active", body.active);
		json.value("inactive", body.inactive);
		json.value("posedFrames", body.posedFrames);
		json.value("shiftX", body.shift.x);
		json.value("shiftZ", body.shift.z);
		json.value("facingYaw", body.facingYaw);
		// the arms drawn in VR (#385.3): cfg.ini vr_arms_model (classic, the only one since #466, #486)
		json.value("armsModel", body.armsModel);
		// the body variant (#385.11): cfg.ini vr_body, and the body's and the Arx arms' faces the last frame drew
		json.value("mode", body.bodyMode);
		json.value("drawnBodyFaces", body.drawnBodyFaces);
		json.value("drawnArmFaces", body.drawnArmFaces);
		// the own body out of the eyes (#385.9): the push back behind the eyes, the neck pivot the body hangs from,
		// the clearance, the faces hidden near an eye this frame, the nearest drawn body vertex, the legs still drawn
		json.value("push", body.push);
		json.object("neckPivot");
		json.value("x", body.neckPivot.x);
		json.value("y", body.neckPivot.y);
		json.value("z", body.neckPivot.z);
		json.end();
		json.value("clearance", body.clearance);
		json.value("hiddenNearEye", body.hiddenNearEye);
		json.value("nearestBodyToEye", body.nearestBodyToEye);
		json.value("legsDrawn", body.legsDrawn);
		{
			const vr::CustomArmsLoad load = vr::customArmsLoad();
			json.object("customArms");
			json.value("tried", load.tried);
			json.value("loaded", load.loaded);
			json.value("reason", load.reason);
			json.end();
		}
		for(size_t hand = 0; hand < vr::HandCount; hand++) {
			const vr::PlayerArmStatus & arm = body.hands[hand];
			json.object(vr::handName(vr::Hand(hand)));
			json.value("source", arm.source);
			json.value("tracked", arm.tracked);
			json.value("posed", arm.posed);
			// the controller in the OpenXR LOCAL space (meters) and in the world (units)
			json.value("x", arm.sample.position.x);
			json.value("y", arm.sample.position.y);
			json.value("z", arm.sample.position.z);
			const Anglef angles = vr::headAngles(arm.sample.orientation);
			json.value("yaw", angles.getYaw());
			json.value("pitch", angles.getPitch());
			json.value("roll", angles.getRoll());
			const struct { const char * name; const Vec3f & point; } points[] = {
				{ "controller", arm.controller }, { "target", arm.target }, { "shoulder", arm.shoulder },
				{ "elbow", arm.elbow }, { "wrist", arm.wrist },
			};
			for(const auto & point : points) {
				json.object(point.name);
				json.value("x", point.point.x);
				json.value("y", point.point.y);
				json.value("z", point.point.z);
				json.end();
			}
			// where the posed hand's palm and fingertips ended up (after the vertices were transformed)
			vr::HandLandmarks landmarks;
			if(vr::handLandmarks(vr::Hand(hand), landmarks)) {
				const struct { const char * name; const Vec3f & point; } points2[] = {
					{ "palm", landmarks.palm }, { "tip", landmarks.tip }
				};
				for(const auto & landmark : points2) {
					json.object(landmark.name);
					json.value("x", landmark.point.x);
					json.value("y", landmark.point.y);
					json.value("z", landmark.point.z);
					json.end();
				}
				json.value("tipAlong", landmarks.tipAlong);
				json.value("tipOut", landmarks.tipOut);
			}
			json.value("reach", arm.reach);
			json.value("armLength", arm.armLength);
			json.value("error", arm.error);
			json.value("slide", arm.slide);
			json.value("stretched", arm.stretched);
			// where the hand's roll went (#304, degrees): the hand's roll about the forearm, the elbow's swing,
			// what was left, the forearm's and the upper arm's roll off the bend plane, and the bands between
			// the bones - the wrist band (hand vs forearm) and the elbow band (forearm vs upper arm)
			json.value("handRoll", arm.handRoll);
			json.value("elbowSwing", arm.elbowSwing);
			json.value("residualRoll", arm.residualRoll);
			json.value("forearmRoll", arm.forearmRoll);
			json.value("upperRoll", arm.upperRoll);
			json.value("wristTwist", arm.wristTwist);
			json.value("elbowTwist", arm.elbowTwist);
			json.value("rollOverflow", arm.overflow);
			// The Arx arm along its rings (#466.1): the skin it was measured on (classic), the bands bent past the
			// threshold (the elbow of a bent arm is one), the most bent band, the elbow's bend and the forearm rings'
			// roll spread
			json.object("classic");
			json.value("skin", arm.armSkin);
			json.value("drawn", arm.classicDrawn);
			json.value("creases", arm.bend.creases);
			json.value("creaseThreshold", vr::CreaseThreshold);
			json.value("creaseBands", arm.bend.bands);
			json.value("maxBend", arm.bend.maxBend);
			json.value("elbowBend", arm.bend.elbowBend);
			json.value("forearmTwistSpread", arm.bend.forearmTwistSpread);
			// The classic arm's hand (#466.2): vr_classic_hand, and whether its Arx hand faces were left out this frame
			// for our glove at the Arx wrist (custom.gloveDrawn below)
			json.value("hand", vr::classicHandName(vr::classicHand()));
			json.value("arxHandHidden", vr::arxHandHidden(vr::Hand(hand)));
			json.end();
			// The glove drawn at this arm's Arx wrist (vr_classic_hand = glove, #466.2): the hand's size, where it sits
			const vr::CustomArmStatus & custom = vr::customArmStatus(vr::Hand(hand));
			json.value("handScale", custom.handScale);
			json.object("custom");
			json.value("drawn", custom.drawn);
			// the glove went into the batch this frame (#385.11, vr::customArmMeshes) with a texture (the file's, or a
			// plain one when it is missing)
			json.value("gloveDrawn", custom.drawn);
			json.value("tipLength", custom.tipLength);
			const struct { const char * name; const Vec3f & point; } customPoints[] = {
				{ "palm", custom.palm }, { "wrist", custom.wrist }
			};
			for(const auto & point : customPoints) {
				json.object(point.name);
				json.value("x", point.point.x);
				json.value("y", point.point.y);
				json.value("z", point.point.z);
				json.end();
			}
			// the Arx wrist ring as skinned to the glove's cuff (#466.2)
			json.value("wristGap", custom.wristGap);
			json.value("vertices", u64(custom.vertices));
			json.value("triangles", u64(custom.triangles));
			// its five fingers (#385.4): what they follow and each joint's bend (degrees, 0 straight) - root is the
			// thumb's first joint or a finger's knuckle, mid the next, tip the last
			json.object("fingers");
			json.value("source", vr::customFingerSourceName(custom.fingerSource));
			const char * const customFingerNames[] = { "thumb", "index", "middle", "ring", "little" };
			for(size_t finger = 0; finger < vr::FingerCount; finger++) {
				json.object(customFingerNames[finger]);
				json.value("root", custom.fingers.flex[finger][0]);
				json.value("mid", custom.fingers.flex[finger][1]);
				json.value("tip", custom.fingers.flex[finger][2]);
				json.value("spread", custom.fingers.spread[finger]);
				json.end();
			}
			json.end();
			// #385.7: the glove's palm centre from the controller's grip (the grip is the handle's centre), and the hilt
			// pose's curl - the line the closed fingers wrap - against the handle's line (units, degrees); for 385.10,
			// how far the held weapon's hilt (its attach vertex, drawn by the Arx hand) lies off that line (units)
			json.value("palmToGrip", custom.palmToGrip);
			json.value("curlToHandle", custom.curlToHandle);
			json.value("curlAngle", custom.curlAngle);
			const vr::HeldWeaponStatus & held = vr::heldWeaponStatus();
			if(vr::Hand(hand) == vr::Hand::Right && held.drawn && custom.drawn) {
				json.value("weaponToHandle", glm::length(glm::cross(held.hilt - custom.grip, custom.handle)));
			}
			json.end();
			// The fingers (#305): what they follow and how far each segment is bent (degrees)
			json.object("fingers");
			json.value("source", arm.fingerSource);
			json.value("groups", arm.fingerGroups);
			json.value("joints", arm.joints);
			json.value("holds", arm.holds);
			const struct { const char * name; const vr::FingerCurl & curl; } curls[] = {
				{ "curl", arm.curl }, { "target", arm.curlTarget }
			};
			for(const auto & entry : curls) {
				json.object(entry.name);
				json.value("thumb", entry.curl.thumb);
				json.value("indexRoot", entry.curl.indexRoot);
				json.value("indexTip", entry.curl.indexTip);
				json.value("fingersRoot", entry.curl.fingersRoot);
				json.value("fingersTip", entry.curl.fingersTip);
				json.end();
			}
			json.end();
			json.end();
		}
		json.value("fingerGroups", body.fingers);
		// the shares in use (cfg.ini vr_arm_*, #304)
		json.value("forearmShare", body.forearmShare);
		json.value("upperShare", body.upperShare);
		json.value("elbowShare", body.elbowShare);
		json.value("twistOverflow", body.twistOverflow);
		json.end();
	}
	// XR_EXT_hand_tracking (#305): whether the runtime gives hand joints and how often it did
	{
		const vr::HandJointsStatus & hands = vr::handJointsStatus();
		json.object("hands");
		json.value("extension", hands.extension);
		json.value("supported", hands.supported);
		json.value("trackers", hands.trackers);
		json.value("locates", hands.locates);
		json.value("failures", hands.failures);
		// #385.7: the hilt angles in use (cfg.ini vr_hilt_*, degrees)
		json.object("hiltAngles");
		json.value("knuckle", config.input.vrHiltKnuckle);
		json.value("middle", config.input.vrHiltMiddle);
		json.value("last", config.input.vrHiltLast);
		json.value("thumb", config.input.vrHiltThumb);
		json.end();
		for(size_t hand = 0; hand < vr::HandCount; hand++) {
			json.object(vr::handName(vr::Hand(hand)));
			json.value("active", hands.active[hand]);
			json.value("activeFrames", hands.activeFrames[hand]);
			// #385.7: the motion range of the joints in use - conforming to the controller (XR_EXT_hand_joints_motion_range),
			// the runtime's unobstructed default, or none (no joints this frame) - and what a held weapon closes the
			// custom hand on (none: no weapon in it, default: the vr_hilt_* pose, captured: `grip capture`'s)
			json.value("motionRange", !hands.active[hand] ? "none" : hands.motionRange ? "conforming" : "unobstructed");
			json.value("hiltPose", vr::hiltPoseSourceName(vr::customArmStatus(vr::Hand(hand)).hiltPose));
			json.end();
		}
		json.end();
	}
	// The rune trail (#43): the stance, the stroke the hand draws and the flares put at it
	{
		const vr::RuneTrailStatus & magic = vr::runeTrailStatus();
		json.object("magic");
		json.value("handsDraw", magic.handsDraw);
		json.value("stance", magic.stance);
		json.value("drawing", magic.drawing);
		json.value("hand", magic.hand);
		json.value("strokes", magic.strokes);
		json.value("length", magic.length);
		json.value("ms", magic.durationMs);
		json.value("lastEnd", magic.lastEnd);
		writeVec(json, "point", magic.point);
		json.value("flares", magic.flares);
		writeVec(json, "lastFlare", magic.lastFlare);
		for(size_t hand = 0; hand < vr::HandCount; hand++) {
			json.object(vr::handName(vr::Hand(hand)));
			json.value("located", magic.located[hand]);
			json.value("ignored", magic.ignored[hand]);
			writeVec(json, "point", magic.points[hand]);
			json.end();
		}
		json.end();
	}
	// The weapon in the IK hand (#75): where it is, how the blade sits on the controller's handle, and the
	// engine's arm layer that is not applied while the IK drives the arms (F-67)
	{
		const vr::HeldWeaponStatus & weapon = vr::heldWeaponStatus();
		json.object("weapon");
		json.value("item", weapon.item);
		json.value("kind", weapon.kind);
		json.value("place", weapon.place);
		json.value("inHand", weapon.inHand);
		json.value("sheathed", weapon.sheathed);
		json.value("toggles", weapon.toggles);
		json.value("handFrames", weapon.handFrames);
		json.value("relinks", weapon.relinks);
		json.value("measured", weapon.measured);
		json.value("bladeLength", weapon.bladeLength);
		json.value("assetAngle", weapon.assetAngle);
		json.value("correction", weapon.correction);
		json.value("drawn", weapon.drawn);
		const struct { const char * name; const Vec3f & point; } points[] = {
			{ "hilt", weapon.hilt }, { "tip", weapon.tip }, { "bladeGrip", weapon.bladeGrip },
		};
		for(const auto & point : points) {
			json.object(point.name);
			json.value("x", point.point.x);
			json.value("y", point.point.y);
			json.value("z", point.point.z);
			json.end();
		}
		// Distance from the hilt to the right palm, both taken in this one status write: a check that remembers
		// the palm and asserts the hilt later compares two frames of a moving idle rig (#316): the weapon's attach
		// vertex to the Arx hand's palm
		vr::HandLandmarks palm;
		if(vr::handLandmarks(vr::Hand::Right, palm)) {
			json.value("hiltToPalm", glm::distance(weapon.hilt, palm.palm));
		}
		json.value("bladeAngle", weapon.bladeAngle);
		// The class and the handle read from the weapon, the sword's tilt configured (#385.10; not applied, #548)
		json.value("weaponClass", weapon.weaponClass);
		json.value("handleLength", weapon.handleLength);
		json.value("tilt", weapon.tilt);
		json.value("armAnimSuppressed", weapon.armAnimSuppressed);
		json.value("armAnimation", weapon.armAnimation);
		json.value("suppressedFrames", weapon.suppressedFrames);
		json.value("combatMode", (player.Interface & INTER_COMBATMODE) != 0);
		json.end();
	}
	// Physical melee (#39): the swing detector, the hits and their damage from the swing (#465)
	{
		const vr::MeleeStatus & melee = vr::meleeStatus();
		json.object("melee");
		json.value("active", melee.active);
		json.value("refused", melee.refused);
		json.value("swinging", melee.swinging);
		json.value("kind", melee.kind);
		json.value("speed", melee.speed);
		json.value("peak", melee.peak);
		json.value("swings", melee.swings);
		json.value("gaps", melee.gaps);
		json.value("jumps", melee.jumps);
		json.value("nearMisses", melee.nearMisses);
		json.value("lastNearMiss", melee.lastNearMiss);
		json.value("trigger", melee.trigger);
		json.value("worldHit", melee.worldHit);
		json.value("hits", melee.hits);
		json.value("touches", melee.touches);
		json.value("contacts", melee.contacts);
		json.object("last");
		json.value("target", melee.lastTarget);
		json.value("aim", melee.lastAim);
		json.value("damage", melee.lastDamage);
		json.value("speed", melee.lastSpeed);
		json.value("size", melee.lastSize);
		json.value("kind", melee.lastKind);
		json.value("swing", melee.lastSwing);
		json.end();
		// The recovery after a hit (#323): a swing started within it hits nothing
		json.object("recovery");
		json.value("weapon", melee.weapon);
		json.value("seconds", melee.recovery);
		json.value("source", melee.recoverySource);
		json.value("game", melee.recoveryGame);
		json.object("games"); // the game's own strike of every type, whatever is in the hand
		for(vr::MeleeWeapon type : { vr::MeleeWeapon::Dagger, vr::MeleeWeapon::OneHanded, vr::MeleeWeapon::TwoHanded }) {
			json.value(vr::meleeWeaponName(type), melee.gameStrike[size_t(type)]);
		}
		json.end();
		json.value("left", melee.recoveryLeft);
		json.value("recovering", melee.recovering);
		json.value("swings", melee.recoverySwings);
		json.end();
		json.object("params");
		json.value("swingSpeed", melee.params.startSpeed);
		json.value("endSpeed", melee.params.endSpeed);
		json.value("class", melee.meleeClass);
		const struct { const char * name; const vr::DamageRange & range; } ranges[] = {
			{ "slash", melee.damage.slash }, { "stab", melee.damage.stab }
		};
		for(const auto & range : ranges) {
			json.object(range.name);
			json.value("pathMin", range.range.pathMin);
			json.value("pathMax", range.range.pathMax);
			json.value("speedMin", range.range.speedMin);
			json.value("speedMax", range.range.speedMax);
			json.value("cap", range.range.cap);
			json.end();
		}
		json.value("pathScale", melee.damage.pathScale);
		json.value("quality", vr::qualityName(melee.damage.quality));
		json.value("noSwing", vr::noSwingName(melee.damage.noSwing));
		json.value("targetCooldown", melee.params.targetCooldown);
		json.end();
		json.value("placed", melee.placed);
		const struct { const char * name; const Vec3f & point; } points[] = { { "hilt", melee.hilt }, { "tip", melee.tip } };
		for(const auto & point : points) {
			json.object(point.name);
			json.value("x", point.point.x);
			json.value("y", point.point.y);
			json.value("z", point.point.z);
			json.end();
		}
		json.end();
	}
	{
		// The radial menus on B (#376): per hand open, under the hand, the last release; the quick slots
		const vr::RadialStatus & radial = vr::radialStatus();
		json.object("radial");
		json.value("torchHand", radial.torchHand);
		// The menus' images exist: without them the menus work unseen (the owner's first headset run)
		json.value("images", vr::radialImagesMade());
		json.value("drawn", radial.drawn);
		json.value("sideLetters", radial.sideLetters);
		for(size_t hand = 0; hand < vr::HandCount; hand++) {
			const vr::RadialHandStatus & h = radial.hands[hand];
			json.object(vr::handName(vr::Hand(hand)));
			json.value("open", h.open);
			json.value("halfRing", h.halfRing);
			json.value("hovered", h.hovered);
			json.value("chosen", h.chosen);
			json.value("result", h.result);
			json.value("refused", h.refused.empty() ? std::string("none") : h.refused);
			json.value("opens", h.opens);
			json.value("choices", h.choices);
			json.value("gameMenuKeys", h.gameMenuKeys);
			json.value("placed", h.placed);
			json.value("shown", h.shown);
			// The quad layer was in the last submitted frame (#377: the same as shown), and why its image was not drawn
			json.value("submitted", vr::radialLayerShown(vr::Hand(hand)));
			const std::string undrawn = vr::radialImageRefusal(vr::Hand(hand));
			json.value("undrawn", undrawn.empty() ? std::string("none") : undrawn);
			json.value("pointX", h.point.x);
			json.value("pointY", h.point.y);
			writeVec(json, "center", h.center);
			json.end();
		}
		// The quick slots of the ring (#377): r1..r3 the right hand's, l1..l3 the left hand's - the kind each holds
		json.object("slots");
		for(vr::Hand side : { vr::Hand::Right, vr::Hand::Left }) {
			const vr::QuickSlotSet slots = vr::quickSlots(side);
			for(size_t slot = 0; slot < slots.size(); slot++) {
				const std::string name = (side == vr::Hand::Left ? "l" : "r") + std::to_string(slot + 1);
				json.value(name, slots[slot].empty() ? std::string("none") : slots[slot]);
			}
		}
		json.end();
		json.end();
	}
	{
		// Health and mana on the left wrist (#116): shown instead of the orbs, and how visible from the eyes
		const vr::WristVitalsStatus & wrist = vr::wristVitalsStatus();
		json.object("wrist");
		json.value("shown", wrist.shown);
		json.value("hidden", wrist.hidden);
		json.value("health", wrist.health);
		json.value("mana", wrist.mana);
		json.value("facingDeg", wrist.facingDeg);
		json.value("alpha", wrist.alpha);
		json.value("drawnViews", wrist.drawnViews);
		// the middle of the bars (units) and their axes in the world (engine: x right, y down, z forward)
		const struct { const char * name; const Vec3f & v; } vectors[] = {
			{ "center", wrist.center }, { "normal", wrist.normal }, { "across", wrist.across }, { "along", wrist.along },
		};
		for(const auto & vector : vectors) {
			json.object(vector.name);
			json.value("x", vector.v.x);
			json.value("y", vector.v.y);
			json.value("z", vector.v.z);
			json.end();
		}
		json.end();
	}
	{
		// The screen-frame HUD (#469): the switches, per element the frames it was let through to draw (0 in VR),
		// the level-up indication
		const vr::HudStatus & hud = vr::hudStatus();
		json.object("hud");
		json.value("vr", hud.vr);
		json.value("frame", hud.frame ? "on" : "off");
		json.value("keep", hud.keep.empty() ? std::string("none") : hud.keep);
		json.value("frames", hud.frames);
		json.value("drawnTotal", vr::hudDrawnTotal());
		json.object("drawn");
		for(size_t i = 0; i < vr::HudElementCount; i++) {
			json.value(vr::hudElementInfo(vr::HudElement(i)).name, hud.drawn[i]);
		}
		json.end();
		const vr::LevelUpIndicator & levelUp = vr::levelUpIndicator();
		json.object("levelUp");
		json.value("mode", vr::levelUpModeName(levelUp.mode()));
		json.value("pending", levelUp.pending());
		json.value("radialMark", levelUp.radialMark());
		json.value("wristNotice", levelUp.wristNotice());
		json.value("notices", levelUp.notices());
		json.value("radialImages", hud.levelUpRadialImages);
		json.value("wristViews", hud.levelUpWristViews);
		json.end();
		json.end();
	}
	{
		// The controllers and the control table (#30): profiles per hand, held controls, events
		const vr::ControlResolver & controls = vr::controls();
		const std::array<std::string, vr::HandCount> profiles = vr::controllerProfiles();
		json.object("input");
		json.value("controllers", vr::controllerTypeName(vr::controllerType()));
		json.object("profile");
		json.value("left", profiles[size_t(vr::Hand::Left)]);
		json.value("right", profiles[size_t(vr::Hand::Right)]);
		json.end();
		json.value("syncs", vr::controllerSyncs());
		std::string down;
		for(size_t i = 0; i < vr::ControlCount; i++) {
			if(controls.down()[i]) {
				down += (down.empty() ? "" : " ") + vr::controlName(vr::Control(i));
			}
		}
		json.value("down", down);
		std::string active;
		for(size_t i = 0; i < vr::EventCount; i++) {
			if(controls.active(vr::Event(i))) {
				active += (active.empty() ? "" : " ") + std::string(vr::eventName(vr::Event(i)));
			}
		}
		json.value("active", active);
		const vr::ControllerInput & analog = vr::controllerInput();
		json.object("trigger");
		json.value("left", analog.trigger[size_t(vr::Hand::Left)]);
		json.value("right", analog.trigger[size_t(vr::Hand::Right)]);
		json.end();
		json.object("grip");
		json.value("left", analog.grip[size_t(vr::Hand::Left)]);
		json.value("right", analog.grip[size_t(vr::Hand::Right)]);
		json.end();
		json.object("thumbTouch");
		json.value("left", analog.thumbTouch[size_t(vr::Hand::Left)]);
		json.value("right", analog.thumbTouch[size_t(vr::Hand::Right)]);
		json.end();
		json.object("presses");
		for(size_t i = 0; i < vr::EventCount; i++) {
			json.value(vr::eventName(vr::Event(i)), controls.presses(vr::Event(i)));
		}
		json.end();
		json.object("layout");
		for(size_t i = 0; i < vr::EventCount; i++) {
			json.value(vr::eventName(vr::Event(i)), vr::formatBinding(controls.layout()[vr::Event(i)]));
		}
		json.end();
		// The player's remapping in the VR settings (#178): saved per controller type, conflicts, the capture
		json.value("remapped", vr::layoutRemapped());
		// The game menu stays reachable whatever is remapped (#326)
		json.value("menuReachable", vr::menuReachable(controls.layout()));
		json.value("remapRefusals", vr::remapRefusals());
		json.value("layoutFile", vr::layoutFileName(vr::controllerType()));
		{
			const std::vector<vr::Conflict> conflicts = vr::findConflicts(controls.layout());
			std::string list;
			for(const vr::Conflict & conflict : conflicts) {
				list += (list.empty() ? "" : " ") + std::string(vr::eventName(conflict.first)) + '+'
				        + vr::eventName(conflict.second);
			}
			json.value("conflicts", conflicts.size());
			json.value("conflictList", list);
		}
		{
			const vr::BindingCapture & capture = vr::capture();
			const char * state = "idle";
			switch(capture.state()) {
				case vr::BindingCapture::State::Idle: state = "idle"; break;
				case vr::BindingCapture::State::Waiting: state = "waiting"; break;
				case vr::BindingCapture::State::Holding: state = "holding"; break;
				case vr::BindingCapture::State::Done: state = "done"; break;
				case vr::BindingCapture::State::Cancelled: state = "cancelled"; break;
			}
			json.object("capture");
			json.value("state", state);
			json.value("event", vr::eventName(vr::captureEvent()));
			json.value("binding", vr::formatBinding(capture.binding()));
			json.value("refused", capture.refused() == vr::Binding::None ? std::string()
			                                                            : vr::controlName(capture.refused()));
			json.value("why", capture.refusedWhy());
			json.value("count", vr::captureCount());
			json.end();
		}
		// What the engine was given (#74): held actions, buttons and keys; the move vector and turn axis
		const vr::GameInputState & game = vr::gameInput();
		json.object("game");
		json.value("held", vr::formatGameInput(game));
		json.value("moveX", game.move.x);
		json.value("moveY", game.move.y);
		json.value("turn", game.turn);
		json.end();
		// The stick walk the player controls applied (#31): its forward is the look (D-008)
		const vr::WalkStatus & walk = vr::walkStatus();
		json.object("walk");
		json.value("walking", walk.walking);
		json.value("yaw", walk.yaw);
		json.value("heading", walk.heading);
		json.value("dirX", walk.direction.x);
		json.value("dirZ", walk.direction.z);
		json.value("walks", walk.walks);
		json.value("sneak", walk.sneak);
		json.value("paceChanges", walk.paceChanges);
		json.end();
		// What the turn axis did (#32): the settings, snaps so far and the total turned (degrees, right positive)
		const vr::TurnSettings turnSettings = vr::turnSettings();
		const vr::TurnController & turn = vr::turnState();
		json.object("turn");
		json.value("mode", vr::turnModeName(turnSettings.mode));
		json.value("snapAngle", turnSettings.snapAngle);
		json.value("smoothSpeed", turnSettings.smoothSpeed);
		json.value("snapRepeat", turnSettings.snapRepeat);
		json.value("snaps", turn.snaps());
		json.value("lastSnap", turn.lastSnap());
		json.value("pushRepeats", vr::snapPushRepeats());
		json.value("repeatInterval", vr::snapRepeatInterval());
		json.value("turned", turn.turned());
		json.value("updates", turn.updates());
		json.value("allowed", turn.allowed());
		json.end();
		json.end();
	}
	// The last rate / cadence report of the frame loop (#20, F-07): one per 3 s while the session runs
	const vr::PacingReport & pacing = vr::lastPacingReport();
	if(pacing.valid) {
		json.object("pacing");
		json.value("reports", vr::pacingReports());
		json.value("seconds", float(pacing.seconds));
		json.value("frames", pacing.frames);
		json.value("shown", pacing.shown);
		json.value("divisor", pacing.divisor);
		json.value("periodMs", float(pacing.periodMs));
		json.value("slotsPerSecond", float(pacing.slotsPerSecond));
		json.value("framesPerSecond", float(pacing.framesPerSecond));
		json.value("waitMeanMs", float(pacing.waitMeanMs));
		json.value("waitMaxMs", float(pacing.waitMaxMs));
		json.value("endMeanMs", float(pacing.endMeanMs));
		json.value("endMaxMs", float(pacing.endMaxMs));
		json.value("budgetMeanMs", float(pacing.budgetMeanMs));
		json.value("budgetMaxMs", float(pacing.budgetMaxMs));
		json.value("gameMeanMs", float(pacing.gameMeanMs));
		json.value("gameMaxMs", float(pacing.gameMaxMs));
		json.value("slotsPerFrame", float(pacing.slotsPerFrame));
		json.value("cadenceSdMs", float(pacing.cadenceSdMs));
		json.value("worstFrameMs", float(pacing.worstFrameMs));
		json.value("worstRunFrames", pacing.worstRunFrames);
		json.value("worstRunMs", float(pacing.worstRunMs));
		json.value("verdict", vr::verdictName(pacing.verdict));
		json.value("cadence", vr::cadenceName(pacing.cadence));
		json.end();
	}
	// The simulated headset (#112): rig, eye poses, how the eye images paired up, the shots
	if(vr::isSimulated()) {
		writeSimStatus(json, vr::simStatus());
	}
	json.end();

	// The playground's damage numbers (#321): how many are shown now (none outside the playground)
	json.object("damageNumbers");
	json.value("visible", u64(damagenumbers::visible()));
	json.end();

	// Game event log (#130): how many events of each name happened, logged or not
	json.object("events");
	json.value("total", u64(gameevent::total()));
	json.object("byName");
	for(const auto & [name, count] : gameevent::counts()) {
		json.value(name, u64(count));
	}
	json.end();
	json.end();

	// The hang watchdog of the main thread (#440): its reports are in hang.txt and as [hang] log lines
	json.object("hangWatchdog");
	json.value("watching", g.watchdog && g.watchdog->watching());
	json.value("thresholdMs", s64(g.watchdogThresholdMs));
	json.value("stalls", u64(g.watchdog ? g.watchdog->stalls() : 0));
	json.end();

	// A command that changes the level or loads a save writes the status while its batch still runs: the batch
	// must not show up before its ack does (#142)
	json.value("cmdSeq", g.ackedSeq);
	json.value("lastCmd", g.lastCmd);
	json.value("lastCmdError", g.lastCmdError);
	json.value("captureSeq", g.captureSeq);
	json.value("lastCapture", g.lastCapture);
	if(!g.capture.name.empty()) {
		json.object("capture");
		json.value("name", g.capture.name);
		json.value("width", g.capture.size.x);
		json.value("height", g.capture.size.y);
		const char * const quarters[] = { "topLeft", "topRight", "bottomLeft", "bottomRight" };
		for(size_t i = 0; i < g.capture.quarters.size(); i++) {
			writePixelStats(json, quarters[i], g.capture.quarters[i]);
		}
		json.end();
	}

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

	if(g.stickFor.seq != 0) {
		const State::StickFor & hold = g.stickFor;
		json.object("stickFor");
		json.value("seq", hold.seq);
		json.value("active", hold.active);
		json.value("units", hold.units);
		json.value("moved", hold.moved);
		json.value("timedOut", hold.timedOut);
		json.object("walk");
		json.value("walking", hold.walk.walking);
		json.value("yaw", hold.walk.yaw);
		json.value("dirX", hold.walk.direction.x);
		json.value("dirZ", hold.walk.direction.z);
		json.end();
		json.end();
	}

	if(g.pick.seq != 0) {
		json.object("pick");
		json.value("seq", g.pick.seq);
		json.value("center", g.pick.center);
		json.value("scanPoints", g.pick.scanPoints);
		json.value("scan", g.pick.scan);
		json.value("target", g.pick.target);
		json.value("targetHits", g.pick.targetHits);
		json.value("targetHitX", g.pick.targetHit.x);
		json.value("targetHitY", g.pick.targetHit.y);
		json.value("targetSeenX", g.pick.targetSeen.x);
		json.value("targetSeenY", g.pick.targetSeen.y);
		json.value("targetOffset", g.pick.targetOffset);
		json.value("targetBox", g.pick.targetBox);
		json.value("targetSeenInBox", g.pick.targetSeenInBox);
		json.end();
	}

	return json.str();
}

//! \param periodic the write of the 100 ms timer: it does not wait for the writer's lock (the next one is near)
void writeStatus(PlatformInstant now, bool periodic = false) {

	if(!g.writer) {
		return;
	}

	if(g.lastStatusTime != PlatformInstant(0)) {
		s64 ms = toMsi(now - g.lastStatusTime);
		if(ms > 0) {
			g.fps = float(g.frame - g.lastStatusFrame) * 1000.f / float(ms);
		}
	}
	g.lastStatusTime = now;
	g.lastStatusFrame = g.frame;

	// Only built here: the writer thread writes it to a temporary file and renames that over status.json, so a reader
	// never sees a half-written status. The rename waits while a reader - the test runner reads status.json all the
	// time, a scanner may hold it under load - has the file: on the frame that cost 118 ms (#23, TRAPS.md) and 159.6 ms
	// (CI run 39359033513: 11 display slots missed, a refresh-rate record short of its frames, #553)
	const PlatformInstant start = platform::getTime();
	std::string text = buildStatus(now);
	noteSlowPart("status build", start);
	g.writer->status(std::move(text), periodic, g.counter ? g.counter->lines.load() : 0);
}
void writeAck(unsigned errors) {
	std::ostringstream oss;
	oss << "seq=" << g.cmdSeq << " frame=" << g.frame << " errors=" << errors
	    << " last=" << g.lastCmd << " error=" << (g.lastCmdError.empty() ? "-" : g.lastCmdError) << '\n';
	// Written after the status handed over before it has landed (#23)
	g.writer->ack(oss.str());
}

/*
 * A report of the hang watchdog (#440), on its own thread: hang.txt first and without the logger -
 * a main thread stuck inside the logger must not keep the report from the test - then one [hang]
 * line of arx.log per report line.
 */
void reportHang(std::string_view report) {

	// hang.txt only when the test channel gave us a directory (a plain launch has none)
	if(g.enabled && !g.dir.empty()) {
		fs::ofstream file(g.dir / "hang.txt", std::ios::out | std::ios::app);
		file << report;
	}

	// One [hang] log line per report line; the indented frames become the report's stack
	std::string stack;
	for(size_t pos = 0; pos < report.size(); ) {
		size_t end = report.find('\n', pos);
		if(end == std::string_view::npos) {
			end = report.size();
		}
		std::string_view line = report.substr(pos, end - pos);
		LogWarning << "[hang] " << line;
		if(!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
			stack += line;
			stack += '\n';
		}
		pos = end + 1;
	}
	Logger::flush();

	// On the first report of a stall, ask the out-of-process dump helper for a hang report with a dump of
	// every thread, and name the folder it made in the log (#545); the game runs on
	if(report.find("(report 1 of") != std::string_view::npos && platform::dumpHelperEnabled()) {
		std::string killer = std::to_string(platform::mainThreadId()) + " main thread";
		platform::requestReport(platform::DumpKind::Hang, true, platform::mainThreadId(), 0,
		                        killer, std::string_view(), std::string_view(), stack, 8000);
		std::string folder = platform::dumpLastReportFolder();
		if(!folder.empty()) {
			LogWarning << "[hang] dump written: hangs\\" << folder << "\\arx.dmp";
			Logger::flush();
		}
	}
}

void setState(GameState state) {
	// Logged by the event log (game.state), which saw the change earlier in this frame
	g.state = state;
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

//! Whitespace-separated numbers; false if any word is not a finite number
bool parseFloats(std::string_view text, std::vector<float> & values) {
	values.clear();
	std::string_view rest = text;
	while(!rest.empty()) {
		std::string_view next;
		std::string word(firstWord(rest, next));
		char * end = nullptr;
		float value = std::strtof(word.c_str(), &end);
		if(word.empty() || !end || *end != '\0' || !std::isfinite(value)) {
			return false;
		}
		values.push_back(value);
		rest = next;
	}
	return true;
}

//! A level change in a script (#596): `teleport -l<flags> [<angle>] <level> <target>` (ScriptedIOControl.cpp)
struct ScriptLevelChange {
	std::string level;
	std::string target;
};

//! Every level change in a (lowercase) script, each level and target once
std::vector<ScriptLevelChange> scriptLevelChanges(std::string_view script) {
	std::vector<ScriptLevelChange> changes;
	const std::string_view command = "teleport";
	for(size_t pos = script.find(command); pos != std::string_view::npos; pos = script.find(command, pos + 1)) {
		const size_t after = pos + command.size();
		if((pos > 0 && !std::isspace(static_cast<unsigned char>(script[pos - 1])))
		   || after >= script.size() || !std::isspace(static_cast<unsigned char>(script[after]))) {
			continue; // part of another word
		}
		std::string_view line = script.substr(after);
		line = line.substr(0, line.find_first_of("\r\n"));
		std::string_view rest;
		std::string_view flags = firstWord(trim(line), rest);
		if(flags.size() < 2 || flags[0] != '-' || flags.find('l') == std::string_view::npos) {
			continue; // a teleport within the level
		}
		if(flags.find('a') != std::string_view::npos) {
			firstWord(rest, rest); // the angle comes first
		}
		ScriptLevelChange change;
		change.level = std::string(firstWord(rest, rest));
		change.target = std::string(firstWord(rest, rest));
		if(change.level.empty()) {
			continue;
		}
		// A script variable (the magic portals: ~£leading_to~) has the Latin-1 sigil: '?' keeps the list ASCII
		for(std::string * word : { &change.level, &change.target }) {
			for(char & c : *word) {
				if(static_cast<unsigned char>(c) >= 0x80) {
					c = '?';
				}
			}
		}
		bool known = false;
		for(const ScriptLevelChange & other : changes) {
			known = known || (other.level == change.level && other.target == change.target);
		}
		if(!known) {
			changes.push_back(std::move(change));
		}
	}
	return changes;
}

//! The climbable background polygons (POLY_CLIMB, from a texture named "climb"), grouped into ladders
struct LevelLadder {
	Vec3f min = Vec3f(0.f);
	Vec3f max = Vec3f(0.f);
	size_t polygons = 0;
};

std::vector<LevelLadder> levelLadders() {

	std::vector<LevelLadder> pieces;
	if(!g_tiles) {
		return pieces;
	}
	for(auto tile : g_tiles->tiles()) {
		for(const EERIEPOLY & polygon : tile.polygons()) {
			if(polygon.type & POLY_CLIMB) {
				pieces.push_back(LevelLadder{ polygon.min, polygon.max, 1 });
			}
		}
	}

	// One ladder is many polygons: pieces closer than a hand's width (in game units) are one ladder
	const float gap = 30.f;
	std::vector<size_t> group(pieces.size());
	for(size_t i = 0; i < group.size(); i++) {
		group[i] = i;
	}
	auto root = [&group](size_t i) {
		while(group[i] != i) {
			i = group[i] = group[group[i]];
		}
		return i;
	};
	for(size_t i = 0; i < pieces.size(); i++) {
		for(size_t j = i + 1; j < pieces.size(); j++) {
			const LevelLadder & a = pieces[i];
			const LevelLadder & b = pieces[j];
			if(a.min.x - gap <= b.max.x && b.min.x - gap <= a.max.x && a.min.y - gap <= b.max.y
			   && b.min.y - gap <= a.max.y && a.min.z - gap <= b.max.z && b.min.z - gap <= a.max.z) {
				group[root(j)] = root(i);
			}
		}
	}

	std::vector<LevelLadder> ladders;
	std::vector<size_t> ladderOf(pieces.size(), size_t(-1));
	for(size_t i = 0; i < pieces.size(); i++) {
		const size_t r = root(i);
		if(ladderOf[r] == size_t(-1)) {
			ladderOf[r] = ladders.size();
			ladders.push_back(LevelLadder{ pieces[i].min, pieces[i].max, 0 });
		}
		LevelLadder & ladder = ladders[ladderOf[r]];
		ladder.min = glm::min(ladder.min, pieces[i].min);
		ladder.max = glm::max(ladder.max, pieces[i].max);
		ladder.polygons++;
	}
	return ladders;
}

std::string formatVec(const Vec3f & v) {
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(1) << v.x << ' ' << v.y << ' ' << v.z;
	return oss.str();
}

/*!
 * levelstats report (#596): the frame times since `levelstats start` and where the player is, one log line; the
 * level's exits (entities whose scripts change the level, with the zones they control) and ladders appended to
 * <dir>\level-tour.txt
 */
std::string reportLevelStats() {

	State::LevelStats & stats = g.levelStats;
	std::vector<s64> frames = std::move(stats.frameUs);
	stats.frameUs.clear();
	stats.measuring = false;
	stats.lastFrame = 0;

	stats.reports++;
	stats.frames = u32(frames.size());
	stats.meanMs = stats.p95Ms = stats.maxMs = 0.0;
	if(!frames.empty()) {
		s64 total = 0;
		for(s64 us : frames) {
			total += us;
		}
		stats.meanMs = double(total) / double(frames.size()) / 1000.0;
		// Nearest rank, as arx-perf.log: the smallest frame time that at least 95 % of the frames do not exceed
		const size_t rank = (frames.size() * 95 + 99) / 100;
		std::nth_element(frames.begin(), frames.begin() + std::ptrdiff_t(rank - 1), frames.end());
		stats.p95Ms = double(frames[rank - 1]) / 1000.0;
		stats.maxMs = double(*std::max_element(frames.begin(), frames.end())) / 1000.0;
	}

	const u32 area = g_currentArea ? u32(g_currentArea) : 0u;
	std::ostringstream exits;
	stats.exits = 0;
	for(const Entity & entity : entities) {
		std::vector<ScriptLevelChange> changes = scriptLevelChanges(entity.script.data);
		for(ScriptLevelChange & change : scriptLevelChanges(entity.over_script.data)) {
			changes.push_back(std::move(change));
		}
		for(const ScriptLevelChange & change : changes) {
			bool zoned = false;
			for(const Zone & zone : g_zones) {
				if(zone.controled != entity.idString()) {
					continue;
				}
				// A zone's box: x and z from its outline, y at its floor (pos.y), up by its height (y is down); a zone
				// without a height reaches from top to bottom
				const Vec3f center((zone.bbmin.x + zone.bbmax.x) / 2.f, zone.pos.y, (zone.bbmin.z + zone.bbmax.z) / 2.f);
				exits << "  exit to=" << change.level << ' ' << (change.target.empty() ? "-" : change.target)
				      << " by=" << entity.idString() << " zone=" << zone.name << " pos=" << formatVec(center)
				      << std::fixed << std::setprecision(1) << " size=" << (zone.bbmax.x - zone.bbmin.x) << ' '
				      << (zone.bbmax.z - zone.bbmin.z) << " height=";
				if(zone.height > 0) {
					exits << zone.height << '\n';
				} else {
					exits << "any\n";
				}
				stats.exits++;
				zoned = true;
			}
			if(!zoned) {
				exits << "  exit to=" << change.level << ' ' << (change.target.empty() ? "-" : change.target)
				      << " by=" << entity.idString() << " zone=- pos=" << formatVec(entity.pos) << '\n';
				stats.exits++;
			}
		}
	}

	const std::vector<LevelLadder> ladders = levelLadders();
	stats.ladders = ladders.size();

	std::ostringstream line;
	line << "level " << area << " frames=" << stats.frames << std::fixed << std::setprecision(2)
	     << " mean=" << stats.meanMs << " p95=" << stats.p95Ms << " max=" << stats.maxMs
	     << " player=" << formatVec(player.pos) << " exits=" << stats.exits << " ladders=" << stats.ladders;
	LogInfo << "[channel] levelstats: " << line.str();

	std::ostringstream listing;
	listing << line.str() << '\n' << exits.str();
	for(const LevelLadder & ladder : ladders) {
		listing << "  ladder pos=" << formatVec((ladder.min + ladder.max) / 2.f) << " size=" << formatVec(ladder.max - ladder.min)
		        << " polygons=" << ladder.polygons << '\n';
	}
	stats.listing += listing.str();
	if(!fs::write(g.dir / "level-tour.txt", stats.listing)) {
		return refuse("levelstats: could not write " + (g.dir / "level-tour.txt").string());
	}
	return std::string();
}

/*!
 * Run one command line.
 * \return an empty string on success, otherwise the reason it was refused
 */
std::string execute(std::string_view line) {

	std::string_view rest;
	std::string word(firstWord(line, rest));
	util::makeLowercase(word);

	// The flight recorder keeps every command (#545): the report shows what the game was asked to do last
	platform::flightRecord(platform::RecordKind::Command, line);

	if(word == "status") {
		// Nothing to do: the status is written after every batch
		return std::string();
	}

	if(word == "quit") {
		platform::logQuitRequest("the test channel's quit command");
		mainApp->quit();
		return std::string();
	}

	if(word == "shot") {
		g.pendingCapture = rest.empty() ? "shot" + std::to_string(g.captureSeq + 1) : std::string(rest);
		return std::string();
	}

	if(word == "window") {
		// window <width> <height>: the window resized as a player dragging its border does (#334) - the
		// interface is laid out again for the new size at the next frame
		std::vector<float> size;
		if(!parseFloats(rest, size) || size.size() != 2 || size[0] < 320.f || size[1] < 240.f) {
			return refuse("window: expected <width> <height> of at least 320 240, got \"" + std::string(rest) + "\"");
		}
		RenderWindow * window = mainApp ? mainApp->getWindow() : nullptr;
		if(!window || window->isFullScreen()) {
			return refuse("window: needs a windowed game window");
		}
		const Vec2i wanted = Vec2i(Vec2f(size[0], size[1]));
		LogInfo << "[channel] window resized from " << window->getSize().x << 'x' << window->getSize().y << " to "
		        << wanted.x << 'x' << wanted.y;
		window->setWindowSize(wanted);
		return std::string();
	}

	if(word == "mark") {
		LogInfo << "[mark] " << rest;
		vr::perf::mark(rest);
		return std::string();
	}

	if(word == "stall") {
		// A forced hitch (#327): the main thread sleeps in this frame, which the performance log must
		// mark as a hitch caused by the channel. "stall load <ms>": the next level load sleeps instead,
		// after its "Loading level" line - a stalled load for the hang watchdog's check (#440)
		std::string_view value;
		std::string_view where = firstWord(rest, value);
		const bool load = (where == "load");
		if(!load) {
			value = rest;
		}
		// Up to 60 s: over the watchdog's default 5 s threshold, so a stall can force a hang report (#545)
		long stallMs = 0;
		if(!parseIndex(value, stallMs) || stallMs < 1 || stallMs > 60000) {
			return refuse("stall: expected [load] and milliseconds 1..60000, got \"" + std::string(rest) + "\"");
		}
		if(load) {
			g.stallLoadMs = stallMs;
			LogInfo << "[channel] the next level load stalls for " << stallMs << " ms";
			return std::string();
		}
		LogInfo << "[channel] stalling the frame for " << stallMs << " ms";
		// Named in a hang report: the run digest knows this stall is on purpose (log-digest-allowlist.txt)
		note("a stall the test channel asked for");
		platform::stallThisThread(std::chrono::milliseconds(stallMs));
		return std::string();
	}

	if(word == "watchdog") {
		// The hang watchdog's threshold (#440): a main thread without a beat for this long is reported
		long thresholdMs = 0;
		if(!parseIndex(rest, thresholdMs) || thresholdMs < 100 || thresholdMs > 60000) {
			return refuse("watchdog: expected milliseconds 100..60000, got \"" + std::string(rest) + "\"");
		}
		g.watchdogThresholdMs = thresholdMs;
		if(g.watchdog) {
			g.watchdog->setThreshold(std::chrono::milliseconds(thresholdMs));
		}
		LogInfo << "[channel] hang watchdog threshold " << thresholdMs << " ms";
		return std::string();
	}

	if(word == "recenter") {
		// The head is measured from its next sample (#24); the player's is a long press of the left stick (#28)
		vr::resetHeadReference("channel");
		return std::string();
	}

	if(word == "headpose") {
		// A simulated head for checks without a headset (#24): yaw, pitch, roll in degrees (left, up, left
		// positive) and optionally x, y, z in meters (OpenXR LOCAL: right, up, back); "off" removes it.
		// A real headset pose wins over it
		std::string mode(rest);
		util::makeLowercase(mode);
		if(mode == "off") {
			vr::clearSimulatedHead();
			return std::string();
		}
		std::vector<float> values;
		if(!parseFloats(rest, values) || (values.size() != 3 && values.size() != 6)) {
			return refuse("headpose: expected <yaw> <pitch> <roll> [<x> <y> <z>] or off, got \"" + std::string(rest) + "\"");
		}
		vr::HeadSample head;
		head.orientation = vr::headOrientationFromDegrees(values[0], values[1], values[2]);
		if(values.size() == 6) {
			head.position = Vec3f(values[3], values[4], values[5]);
		}
		vr::setSimulatedHead(head);
		return std::string();
	}

	if(word == "handpose") {
		// A simulated controller for the body without one (#36): left|right, then yaw, pitch, roll in
		// degrees (left, up, left positive) and optionally x, y, z in meters (OpenXR LOCAL: right, up,
		// back); "left off" / "right off" removes it. A located controller wins over it
		std::string_view args;
		std::string handWord(firstWord(rest, args)); // the hand; the rest are the numbers or "off"
		util::makeLowercase(handWord);
		vr::Hand hand = vr::Hand::Left;
		if(handWord == "right") {
			hand = vr::Hand::Right;
		} else if(handWord != "left") {
			return refuse("handpose: expected left or right first, got \"" + std::string(rest) + "\"");
		}
		std::string mode(args);
		util::makeLowercase(mode);
		if(mode == "off") {
			vr::clearSimulatedHand(hand);
			return std::string();
		}
		std::vector<float> values;
		if(!parseFloats(args, values) || (values.size() != 3 && values.size() != 6)) {
			return refuse("handpose: expected left|right <yaw> <pitch> <roll> [<x> <y> <z>] or off, got \""
			              + std::string(rest) + "\"");
		}
		vr::HandSample sample;
		sample.orientation = vr::headOrientationFromDegrees(values[0], values[1], values[2]);
		if(values.size() == 6) {
			sample.position = Vec3f(values[3], values[4], values[5]);
		}
		vr::setSimulatedHand(hand, sample);
		return std::string();
	}

	if(word == "vr-input") {
		// The controllers' control table without a headset (#30): down|up|tap <control>, stick, bind, reset
		std::string error = vr::inputCommand(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "stick-for") {
		// stick-for left|right <x> <y> <units> [<seconds>]: a stick held deflected until the player has walked that
		// far on the ground, then let go by the game in that frame (#370) - a walk bounded by its distance, not by how
		// fast the runner reads the status (a busy PC walked a frame-counted walk into a wall). A player who never
		// gets that far (a wall) is let go after <seconds> of game time (default 10)
		std::istringstream iss{std::string(rest)};
		std::string hand;
		float x = 0.f, y = 0.f, units = 0.f, seconds = 10.f;
		bool parsed = bool(iss >> hand >> x >> y >> units);
		if(parsed && !(iss >> seconds)) {
			parsed = iss.eof();
			seconds = 10.f;
		}
		if(!parsed || (hand != "left" && hand != "right") || units <= 0.f || seconds <= 0.f) {
			return refuse("stick-for: expected left|right <x> <y> <units> [<seconds>], got \"" + std::string(rest) + "\"");
		}
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse(std::string("stick-for: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		std::ostringstream stick;
		stick << "stick " << hand << ' ' << x << ' ' << y;
		std::string error = vr::inputCommand(stick.str());
		if(!error.empty()) {
			return refuse(error);
		}
		State::StickFor & hold = g.stickFor;
		hold.seq++;
		hold.active = true;
		hold.hand = hand;
		hold.start = Vec2f(player.pos.x, player.pos.z);
		hold.units = units;
		hold.moved = 0.f;
		hold.deadline = g_gameTime.now() + GameDuration(std::chrono::milliseconds(s64(seconds * 1000.f)));
		hold.timedOut = false;
		hold.walk = vr::WalkStatus();
		return std::string();
	}

	if(word == "motion") {
		// motion reset: the walk, turn and jump totals of status.json motion start from zero (#23); motion clear:
		// the records too
		const std::string what = std::string(trim(rest));
		if(what != "reset" && what != "clear") {
			return refuse("motion: expected reset or clear, got \"" + what + "\"");
		}
		g.motion.reset();
		g.motionResets++;
		if(what == "clear") {
			g.records.clear();
		}
		return std::string();
	}

	if(word == "vr-hud") {
		// The screen-frame HUD in VR (#469): frame on|off, keep <names>|none, levelup <mode>, reset - cfg.ini's, live
		std::string error = vr::hudCommand(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "vr-radial") {
		// The radial menus (#376): point <hand> <item> moves the simulated hand onto an item of its open menu
		std::string error = vr::radialCommand(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "replay") {
		// The replay recorder (#550): mark <name> [<seconds>] cuts the last seconds of the session into a clip
		std::string error = vr::replay::command(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "marks") {
		// Marks of the moment (#551): note on|off - the voice note after a mark, for this session
		std::string error = vr::marks::command(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "vr-sim") {
		// The simulated headset (#112): any script command, now or @<frame> on its timeline
		std::string error = vr::simCommand(rest);
		if(error.empty() && rest.find("shot") != std::string_view::npos && !g.simShotWatch.active) {
			g.simShotWatch.active = true;
			g.simShotWatch.shots = vr::simStatus().shots.size();
			g.simShotWatch.lastFrame = g.frame + 600;
		}
		return error.empty() ? error : refuse("vr-sim: " + error);
	}

	if(word == "crash") {
		// A deliberate crash for the crash-report checks (#132): "crash" faults on the main thread;
		// "crash steal" replaces the exception filter the way an overlay would and crashes 3 s later,
		// after the game has put its own filter back (it does so every second)
		std::string mode(rest);
		util::makeLowercase(mode);
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
		if(mode == "steal") {
			SetUnhandledExceptionFilter([](PEXCEPTION_POINTERS) -> LONG { return EXCEPTION_CONTINUE_SEARCH; });
			g_crashAt = platform::getTime() + std::chrono::seconds(3);
			return std::string();
		}
#endif
		if(!mode.empty()) {
			return refuse("crash: expected nothing or steal, got \"" + std::string(rest) + "\"");
		}
		LogWarning << "[crash] deliberate crash requested through the test channel";
		volatile int * nothing = nullptr;
		*nothing = 1;
		return std::string();
	}

	if(word == "abort") {
		// abort() on the main thread (SIGABRT): the crash handler reports it, kind=abort (#545)
		LogWarning << "[channel] abort() requested through the test channel";
		std::abort();
	}

	if(word == "terminate") {
		std::string mode(rest);
		util::makeLowercase(mode);
		if(mode == "thread") {
			// std::terminate() on a new std::thread - a thread the game's handlers were never set up on
			LogWarning << "[channel] std::terminate() on a foreign thread requested through the test channel";
			std::thread([] { std::terminate(); }).detach();
			return std::string();
		}
		if(!mode.empty()) {
			return refuse("terminate: expected nothing or thread, got \"" + std::string(rest) + "\"");
		}
		LogWarning << "[channel] std::terminate() requested through the test channel";
		std::terminate();
	}

	if(word == "heapcorrupt") {
		// A heap corruption the heap detects (STATUS_HEAP_CORRUPTION, 0xc0000374): the vectored handler
		// reports it, kind=crash (#545)
		LogWarning << "[channel] heap corruption requested through the test channel";
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
		// Overwrite a block's own header on the process heap, then free it: the heap decodes the header,
		// finds it corrupt and raises STATUS_HEAP_CORRUPTION (termination on corruption is on by default)
		HANDLE heap = GetProcessHeap();
		unsigned char * block = static_cast<unsigned char *>(HeapAlloc(heap, 0, 64));
		if(block) {
			std::memset(block - 16, 0xcc, 16);
			HeapFree(heap, 0, block);
		}
#endif
		return std::string();
	}

	if(word == "exitprocess") {
		// A thread whose start routine IS ExitProcess, as a foreign kill does (#544): the ExitGuard catches
		// it and asks for a report before the exit proceeds (#545)
		long code = 0;
		if(!parseIndex(rest, code)) {
			return refuse("exitprocess: expected an exit code, got \"" + std::string(rest) + "\"");
		}
		LogWarning << "[channel] exitprocess " << code << " requested (a thread that starts at ExitProcess)";
#if ARX_PLATFORM == ARX_PLATFORM_WIN32
		HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
		LPTHREAD_START_ROUTINE exitProcess = reinterpret_cast<LPTHREAD_START_ROUTINE>(
			reinterpret_cast<void *>(GetProcAddress(kernel32, "ExitProcess")));
		if(exitProcess) {
			HANDLE thread = CreateThread(nullptr, 0, exitProcess,
			                             reinterpret_cast<LPVOID>(static_cast<uintptr_t>(u32(code))), 0, nullptr);
			if(thread) {
				CloseHandle(thread);
			}
		}
#endif
		return std::string();
	}

	if(word == "crashnotice") {
		// The crash notice's buttons (#545, M6): ok dismisses it (the reports get their marker), open opens the folder
		std::string what(rest);
		util::makeLowercase(what);
		if(!crashNoticeShown()) {
			return refuse("crashnotice: no notice is on screen");
		}
		if(what == "ok") {
			crashNoticeOk();
			return std::string();
		}
		if(what == "open") {
			crashNoticeOpen();
			return std::string();
		}
		return refuse("crashnotice: expected ok or open, got \"" + std::string(rest) + "\"");
	}

	if(word == "leak") {
		// Allocate and touch <MiB> of private memory and keep it until the exit: the R lines of arx-perf.log
		// show the step, perf-summary shows the trend (#545)
		long mb = 0;
		if(!parseIndex(rest, mb) || mb < 1 || mb > 4096) {
			return refuse("leak: expected MiB 1..4096, got \"" + std::string(rest) + "\"");
		}
		static std::vector<std::vector<char>> leaks;
		leaks.emplace_back(size_t(mb) << 20, char(1));
		LogWarning << "[channel] leaked " << mb << " MiB on purpose (kept until the exit)";
		return std::string();
	}

	if(word == "comfort") {
		// Who decides the VR comfort policy (#76): auto = the VR session, on / off force the VR rules
		std::string mode(rest);
		util::makeLowercase(mode);
		if(mode == "auto") {
			vr::setComfortMode(vr::ComfortMode::Auto);
		} else if(mode == "on") {
			vr::setComfortMode(vr::ComfortMode::On);
		} else if(mode == "off") {
			vr::setComfortMode(vr::ComfortMode::Off);
		} else {
			return refuse("comfort: expected auto, on or off, got \"" + std::string(rest) + "\"");
		}
		return std::string();
	}

	if(word == "devvariants") {
		// The dev variants panel (#588), only in a development launch: devvariants clear unregisters every group,
		// devvariants demo registers the demo groups of #588, devvariants set <group> <value> switches one as the
		// panel's buttons do (a choice by its label, a number)
		if(!g_devLaunch) {
			return refuse("devvariants: only in a development launch (--dev-launch)");
		}
		std::string_view args;
		std::string what(firstWord(rest, args));
		util::makeLowercase(what);
		if(what == "clear" && args.empty()) {
			devvariants::clear();
			LogInfo << "[channel] devvariants cleared";
		} else if(what == "demo" && args.empty()) {
			static const std::vector<std::string> sizes = { "Small", "Medium", "Large" };
			devvariants::registerChoice("demo_choice", "#588", sizes, 0, [](size_t index) {
				g.devDemo.choice = sizes[index];
				g.devDemo.calls++;
			});
			devvariants::registerNumber("demo_number", "#588", 0.f, 1.f, 0.25f, "m", 0.75f, [](float value) {
				g.devDemo.number = value;
				g.devDemo.calls++;
			});
			LogInfo << "[channel] devvariants demo registered";
		} else if(what == "set") {
			std::string_view value;
			const std::string group(firstWord(args, value));
			const devvariants::Entry * entry = devvariants::find(group);
			if(!entry) {
				return refuse("devvariants set: no group \"" + group + "\"");
			}
			std::vector<float> number;
			const bool valid = (entry->kind == devvariants::Kind::Choice)
			                   ? std::find(entry->choices.begin(), entry->choices.end(), value) != entry->choices.end()
			                   : parseFloats(value, number) && number.size() == 1;
			if(!valid) {
				return refuse("devvariants set: \"" + std::string(value) + "\" is no value of " + group);
			}
			if(!devvariants::set(group, value)) {
				LogInfo << "[channel] devvariants set: " << group << " is " << devvariants::valueText(group) << " already";
			}
		} else {
			return refuse("devvariants: expected clear, demo or set <group> <value>, got \"" + std::string(rest) + "\"");
		}
		return std::string();
	}

	if(word == "scene") {
		// Scripted scenes in VR (#29, SPEC.md): scene view first|observer|ghost|hybrid, scene lookat ignore|fade,
		// scene subtitles above|chest|lowerthird - live, for this session (cfg.ini keeps the player's choice: the
		// sequences start from the defaults in any order); scene meter reset starts a new
		// measuring stretch of status.json scene.meter. Accepted in flat mode too, where they change nothing
		std::string_view value;
		std::string what(firstWord(rest, value));
		util::makeLowercase(what);
		std::string name(trim(value));
		util::makeLowercase(name);
		if(what == "view" && vr::parseSceneView(name)) {
			vr::sceneState().view = *vr::parseSceneView(name);
		} else if(what == "lookat" && vr::parseSceneLookAt(name)) {
			vr::sceneState().lookAt = *vr::parseSceneLookAt(name);
		} else if(what == "firstlow" && vr::parseSceneFirstLow(name)) {
			vr::sceneState().firstLow = *vr::parseSceneFirstLow(name);
		} else if(what == "cut" && vr::parseSceneCut(name)) {
			vr::sceneState().cut = *vr::parseSceneCut(name);
		} else if(what == "subtitles" && vr::parseSubtitlePlacement(name)) {
			vr::setSubtitlePlacement(*vr::parseSubtitlePlacement(name));
		} else if(what == "meter" && name == "reset") {
			vr::sceneState().meter.reset();
			vr::resetSubtitleCounts();
		} else {
			return refuse("scene: expected view first|observer|ghost|hybrid, lookat ignore|fade, firstlow black|clamp|screen, cut instant|fade, "
			              "subtitles above|chest|lowerthird or meter reset, got \"" + std::string(rest) + "\"");
		}
		LogInfo << "[channel] scene " << what << ' ' << name;
		return std::string();
	}

	if(word == "arms") {
		// The arms drawn on the player in VR (#385.3): arms classic sets the model (the only one since #466, #486),
		// arms scale <factor> the glove's size (vr_hand_scale), arms classic-hand arx|glove the classic arm's hand
		// (vr_classic_hand, #466.2); all are saved to cfg.ini
		std::string_view args;
		std::string_view what = firstWord(rest, args);
		vr::ArmsModel model;
		if(what == "classic-hand") {
			vr::ClassicHand hand;
			if(!vr::parseClassicHandStrict(args, hand)) {
				return refuse("arms classic-hand: expected arx or glove, got \"" + std::string(args) + "\"");
			}
			vr::setClassicHand(hand, "the channel");
			return std::string();
		}
		if(what == "scale") {
			float scale = 0.f;
			std::istringstream iss{std::string(args)};
			if(!(iss >> scale) || scale < vr::MinHandScale || scale > vr::MaxHandScale) {
				return refuse("arms scale: expected a factor from " + std::to_string(vr::MinHandScale) + " to "
				              + std::to_string(vr::MaxHandScale) + ", got \"" + std::string(args) + "\"");
			}
			vr::setHandScale(scale, "the channel");
			return std::string();
		}
		if(!args.empty() || !vr::parseArmsModelStrict(what, model)) {
			return refuse("arms: expected " + vr::armsModelNames() + " or scale <factor>, got \"" + std::string(rest) + "\"");
		}
		vr::setArmsModel(model, "the channel");
		return std::string();
	}

	if(word == "body") {
		// The body drawn under a tracked head (#385.11): body full|hidden switches it live and saves it to cfg.ini
		std::string_view args;
		std::string_view what = firstWord(rest, args);
		vr::BodyMode mode;
		if(!args.empty() || !vr::parseBodyModeStrict(what, mode)) {
			return refuse("body: expected full or hidden, got \"" + std::string(rest) + "\"");
		}
		vr::setBodyMode(mode, "the channel");
		return std::string();
	}

	if(word == "grip") {
		// A custom hand on a hilt (#385.7): grip capture [left|right] stores the hand's runtime joints of the last
		// frame (SteamVR: the Index's conforming grip) as its hilt pose in the user dir, every hand with joints when
		// none is named; grip capture clear [left|right] goes back to the default hilt pose; grip angles <knuckle>
		// <middle> <last> [<thumb>] | default sets cfg.ini vr_hilt_* live (degrees) and saves them; grip tilt <deg> the
		// sword's forward tilt (#385.10)
		std::string_view args;
		const std::string_view what = firstWord(rest, args);
		if(what == "capture") {
			std::string_view more;
			std::string_view side = firstWord(args, more);
			const bool clear = (side == "clear");
			if(clear) {
				side = firstWord(more, more);
			}
			if(!more.empty() || (!side.empty() && side != "left" && side != "right")) {
				return refuse("grip capture: expected [clear] [left|right], got \"" + std::string(args) + "\"");
			}
			size_t done = 0;
			std::string why;
			for(size_t hand = 0; hand < vr::HandCount; hand++) {
				if(!side.empty() && side != vr::handName(vr::Hand(hand))) {
					continue;
				}
				if(clear) {
					vr::clearHiltPose(vr::Hand(hand));
					done++;
					continue;
				}
				std::string result;
				if(vr::captureHiltPose(vr::Hand(hand), result)) {
					done++;
				} else {
					why += (why.empty() ? "" : "; ") + result;
				}
			}
			if(done == 0) {
				return refuse("grip capture: nothing captured - " + why);
			}
			return std::string();
		}
		if(what == "angles") {
			vr::HiltAngles angles;
			if(args != "default") {
				std::istringstream iss{std::string(args)};
				std::string extra;
				if(!(iss >> angles.knuckle >> angles.middle >> angles.last)) {
					return refuse("grip angles: expected <knuckle> <middle> <last> [<thumb>] or default, got \""
					              + std::string(args) + "\"");
				}
				float thumb = 0.f;
				if(iss >> thumb) {
					angles.thumb = thumb;
				}
				iss.clear();
				if(iss >> extra) {
					return refuse("grip angles: too many values in \"" + std::string(args) + "\"");
				}
			}
			angles = vr::clampHiltAngles(angles);
			config.input.vrHiltKnuckle = angles.knuckle;
			config.input.vrHiltMiddle = angles.middle;
			config.input.vrHiltLast = angles.last;
			config.input.vrHiltThumb = angles.thumb;
			config.save();
			LogInfo << "[vr] arms: hilt angles " << angles.knuckle << " / " << angles.middle << " / " << angles.last
			        << ", thumb " << angles.thumb << " (the channel)";
			gameevent::Event("vr.hands.hiltAngles")("knuckle", angles.knuckle)("middle", angles.middle)
			                ("last", angles.last)("thumb", angles.thumb);
			return std::string();
		}
		if(what == "tilt") {
			// The sword's forward tilt in the glove (#385.10): grip tilt <deg> snaps to 0, -15 or -30 (cfg.ini vr_sword_tilt;
			// stored, not applied while the weapon is placed from the Arx hand, #486 - kept for #548)
			float degrees = 0.f;
			std::istringstream iss{std::string(args)};
			std::string extra;
			if(!(iss >> degrees) || !std::isfinite(degrees) || (iss >> extra)) {
				return refuse("grip tilt: expected degrees (0, -15 or -30), got \"" + std::string(args) + "\"");
			}
			const float variant = vr::swordTiltVariant(degrees);
			config.input.vrSwordTilt = variant;
			config.save();
			LogInfo << "[vr] weapon: sword tilt " << variant << " degrees (the channel, asked " << degrees << ")";
			gameevent::Event("vr.weapon.tilt")("tilt", variant)("asked", degrees);
			return std::string();
		}
		return refuse("grip: expected capture [clear] [left|right], angles <knuckle> <middle> <last> [<thumb>] | default"
		              " or tilt <deg>, got \"" + std::string(rest) + "\"");
	}

	if(word == "combat") {
		// The engine's combat mode without the mouse (#75): what the weapon key does - "on" draws the
		// weapon, "off" puts it away; the strike is the mouse button / the trigger
		std::string mode(rest);
		util::makeLowercase(mode);
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse("combat: needs a level");
		}
		if(mode == "on") {
			ARX_INTERFACE_setCombatMode(COMBAT_MODE_DRAW_WEAPON);
		} else if(mode == "off") {
			ARX_INTERFACE_setCombatMode(COMBAT_MODE_OFF);
		} else {
			return refuse("combat: expected on or off, got \"" + std::string(rest) + "\"");
		}
		return std::string();
	}

	if(word == "freelook") {
		// The engine's free look without the mouse (#323): "on" leaves the cursor mode, as loading a save does -
		// a level started with --loadlevel (the playground) stays in it, where the engine keeps the inventory open
		std::string mode(rest);
		util::makeLowercase(mode);
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse("freelook: needs a level");
		}
		if(mode == "on" || mode == "off") {
			TRUE_PLAYER_MOUSELOOK_ON = (mode == "on");
		} else {
			return refuse("freelook: expected on or off, got \"" + std::string(rest) + "\"");
		}
		return std::string();
	}

	if(word == "melee") {
		// The damage from the swing (#465) live, not saved: melee noswing none|min, melee quality min|geomean|product,
		// melee pathscale <factor>, melee slash|stab <class> <path min> <path max> <speed min> <speed max> | default -
		// the cfg.ini keys vr_melee_noswing, _quality, _path_scale, _slash_<class>, _stab_<class>
		std::string_view args;
		const std::string_view what = firstWord(rest, args);
		if(what == "noswing") {
			if(args != vr::noSwingName(vr::NoSwing::None) && args != vr::noSwingName(vr::NoSwing::Min)) {
				return refuse("melee noswing: expected none or min, got \"" + std::string(args) + "\"");
			}
			config.input.vrMeleeNoSwing = std::string(args);
			return std::string();
		}
		if(what == "quality") {
			if(args != vr::qualityName(vr::parseQuality(args))) {
				return refuse("melee quality: expected min, geomean or product, got \"" + std::string(args) + "\"");
			}
			config.input.vrMeleeQuality = std::string(args);
			return std::string();
		}
		if(what == "pathscale") {
			float scale = 0.f;
			std::istringstream iss{std::string(args)};
			if(!(iss >> scale) || scale < 0.25f || scale > 2.f) {
				return refuse("melee pathscale: expected a factor from 0.25 to 2, got \"" + std::string(args) + "\"");
			}
			config.input.vrMeleePathScale = scale;
			return std::string();
		}
		if(what == "slash" || what == "stab") {
			std::string_view range;
			const std::string_view name = firstWord(args, range);
			for(vr::MeleeClass meleeClass : vr::MeleeClasses) {
				if(name != vr::meleeClassName(meleeClass)) {
					continue;
				}
				bool ok = false;
				vr::parseDamageRange(range, vr::DamageRange(), &ok);
				if(range != "default" && (!ok || range.empty())) {
					return refuse("melee " + std::string(what) + ": expected <path min> <path max> <speed min> <speed max>"
					              " (0-20) or default, got \"" + std::string(range) + "\"");
				}
				auto & ranges = what == "slash" ? config.input.vrMeleeSlash : config.input.vrMeleeStab;
				ranges[size_t(meleeClass)] = range == "default" ? std::string() : std::string(range);
				return std::string();
			}
			return refuse("melee " + std::string(what) + ": expected a class (dagger, sword, blunt, 2h_sword, 2h_blunt), got \""
			              + std::string(name) + "\"");
		}
		return refuse("melee: expected noswing, quality, pathscale, slash or stab, got \"" + std::string(rest) + "\"");
	}

	if(word == "crouch") {
		// The crouch toggle without an input device, for flat runs (#263)
		std::string mode(rest);
		util::makeLowercase(mode);
		if(mode == "on") {
			bGCroucheToggle = true;
		} else if(mode == "off") {
			bGCroucheToggle = false;
		} else {
			return refuse("crouch: expected on or off, got \"" + std::string(rest) + "\"");
		}
		return std::string();
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
		if(vr::stereoActive()) {
			return refuse("drawcheck: needs flat rendering - in VR the two views of a frame are the two eyes (#19)");
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

	if(word == "pick") {
		// pick [<entity>]: what the mouse would hover, on an 8 pixel grid over the interface's screen - after a
		// stereo frame the screen data picking reads must be the head camera's at the interface's size (#72). For an
		// entity: where picking finds it against where the camera sees it
		if(currentState() != GameState::Gameplay) {
			return refuse(std::string("pick: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		Entity * target = nullptr;
		if(!rest.empty()) {
			target = entities.getById(rest);
			if(!target) {
				return refuse("pick: unknown entity \"" + std::string(rest) + "\"");
			}
		}
		State::Pick & pick = g.pick;
		pick.seq++;
		// The engine's own screen pick, not the hand reticles' (#37), which own world picking while they are on
		const vr::EnginePickOnly enginePickOnly;
		Entity * centered = FlyingOverObject(Vec2s(g_size.center()));
		pick.center = centered ? centered->idString() : std::string();
		struct Hits {
			Entity * entity;
			unsigned count;
			Vec2f sum;
		};
		std::vector<Hits> hits;
		pick.scanPoints = 0;
		const int step = 8;
		for(int y = g_size.top + step / 2; y < g_size.bottom; y += step) {
			for(int x = g_size.left + step / 2; x < g_size.right; x += step) {
				pick.scanPoints++;
				Entity * found = FlyingOverObject(Vec2s(s16(x), s16(y)));
				if(!found) {
					continue;
				}
				auto it = std::find_if(hits.begin(), hits.end(), [found](const Hits & h) { return h.entity == found; });
				if(it == hits.end()) {
					hits.push_back(Hits{ found, 0, Vec2f(0.f) });
					it = hits.end() - 1;
				}
				it->count++;
				it->sum += Vec2f(float(x), float(y));
			}
		}
		std::sort(hits.begin(), hits.end(), [](const Hits & a, const Hits & b) { return a.count > b.count; });
		std::ostringstream scan;
		for(const Hits & h : hits) {
			Vec2f center = h.sum / float(h.count);
			scan << (scan.tellp() > 0 ? ", " : "") << h.entity->idString() << ' ' << h.count << ' '
			     << std::lround(center.x) << ' ' << std::lround(center.y);
		}
		pick.scan = scan.str();
		pick.target = target ? target->idString() : std::string();
		pick.targetHits = 0;
		pick.targetHit = pick.targetSeen = Vec2f(-1.f);
		pick.targetOffset = -1.f;
		pick.targetBox.clear();
		pick.targetSeenInBox = false;
		if(target) {
			std::ostringstream box;
			box << std::fixed << std::setprecision(0) << target->bbox2D.min.x << ' ' << target->bbox2D.min.y << ' '
			    << target->bbox2D.max.x << ' ' << target->bbox2D.max.y;
			pick.targetBox = box.str();
			auto it = std::find_if(hits.begin(), hits.end(), [target](const Hits & h) { return h.entity == target; });
			if(it != hits.end()) {
				pick.targetHits = it->count;
				pick.targetHit = it->sum / float(it->count);
			}
			// The camera prepared last: the head camera at the interface's size (#73), which the interface is drawn with
			Vec4f p = worldToClipSpace((target->bbox3D.min + target->bbox3D.max) / 2.f);
			if(p.w > 0.f) {
				pick.targetSeen = Vec2f(p) / p.w;
				pick.targetSeenInBox = target->bbox2D.valid() && target->bbox2D.toRect().contains(pick.targetSeen);
				if(pick.targetHits > 0) {
					pick.targetOffset = glm::distance(pick.targetHit, pick.targetSeen);
				}
			}
		}
		std::ostringstream log;
		log << "[channel] pick: " << (pick.center.empty() ? "nothing" : pick.center) << " at the interface's center; "
		    << pick.scanPoints << " grid points: " << (pick.scan.empty() ? "nothing" : pick.scan);
		if(target) {
			log << "; " << pick.target << " picked at " << pick.targetHits << " points around " << pick.targetHit.x << ' '
			    << pick.targetHit.y << ", seen at " << pick.targetSeen.x << ' ' << pick.targetSeen.y << ", bbox2D "
			    << pick.targetBox;
		}
		LogInfo << log.str();
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

	if(word == "newgame") {
		// What the menu does for "New game" and the end of the character creation, without the screens (#70)
		if(ARXmenu.mode() != Mode_MainMenu) {
			return refuse(std::string("newgame: needs the main menu (a plain launch), mode is ")
			              + menuModeName(ARXmenu.mode()));
		}
		LogInfo << "[channel] starting a new game (average hero)";
		ARX_MENU_Clicked_NEWQUEST();
		ARX_PLAYER_MakeAverageHero();
		START_NEW_QUEST = true;
		g_canResumeGame = true;
		ARX_MENU_Clicked_QUIT();
		return std::string();
	}

	if(word == "menu") {
		// Open a page of the main / pause menu as a click on its entry would (#178): none, options, input,
		// vr, vr-turning; "pause" opens the pause menu from the game, as Escape does, "resume" goes back to
		// it; "step <row> left|right", "drag <slider> <0..1>"
		// is a click on an arrow of a row of the VR turning page, "drag" its slider's dot dragged and let go (#260)
		std::string name(rest);
		util::makeLowercase(name);
		if(name == "pause") {
			if(ARXmenu.mode() != Mode_InGame || !g_currentArea) {
				return refuse("menu pause: needs a running game");
			}
			ARX_MENU_Launch(true);
			return std::string();
		}
		if(ARXmenu.mode() != Mode_MainMenu || !g_mainMenu) {
			return refuse(std::string("menu: needs the main or pause menu, mode is ") + menuModeName(ARXmenu.mode()));
		}
		if(name == "resume") {
			// The main menu's "Resume game" with a game running: back to it (#260)
			if(!g_canResumeGame) {
				return refuse("menu resume: no game to resume");
			}
			ARXMenu_ResumeGame();
			return std::string();
		}
		if(name.rfind("step ", 0) == 0) {
			std::string_view after;
			std::string row(firstWord(std::string_view(name).substr(5), after));
			std::string direction(firstWord(after, after));
			if(direction != "left" && direction != "right") {
				return refuse("menu step: expected <row> left|right, got \"" + name + "\"");
			}
			std::string why = stepVRSettingRow(row, direction == "right" ? 1 : -1);
			return why.empty() ? why : refuse(why);
		}
		if(name.rfind("drag ", 0) == 0) {
			std::string_view after;
			std::string row(firstWord(std::string_view(name).substr(5), after));
			std::vector<float> fraction;
			if(!parseFloats(after, fraction) || fraction.size() != 1) {
				return refuse("menu drag: expected <slider> <0..1 along the bar>, got \"" + name + "\"");
			}
			std::string why = dragVRSettingSlider(row, fraction[0]);
			return why.empty() ? why : refuse(why);
		}
		for(const auto & entry : MenuPages) {
			if(name == entry.name) {
				g_mainMenu->requestPage(entry.page);
				return std::string();
			}
		}
		return refuse("menu: expected pause, resume, none, options, input, vr, vr-turning, loadsave, interface, quit, language, step <row> left|right or drag <slider> <0..1>, got \""
		              + name + "\"");
	}

	if(word == "ui") {
		// What the inventory key does, for tests without a keyboard (#130); the book has the
		// script command "book open|close"
		std::string what(rest);
		util::makeLowercase(what);
		if(ARXmenu.mode() != Mode_InGame || !g_currentArea) {
			return refuse("ui: needs a running game");
		}
		if(what == "inventory open") {
			InventoryOpenClose(1);
		} else if(what == "inventory close") {
			InventoryOpenClose(2);
		} else {
			return refuse("ui: expected \"inventory open\" or \"inventory close\", got \"" + std::string(rest) + "\"");
		}
		return std::string();
	}

	if(word == "desktop-mouse") {
		// The desktop mouse over the game window (#338), through the input backend's event queue as a real
		// mouse: "move <dx> <dy>" (window pixels), "at <x> <y>" (interface pixels) or "at menu <name>" (the
		// middle of a menu target, as vr-point names it - also the ui.menuHover target), "button <1-6> down|up",
		// "wheel <steps>". In VR the game must drop all of it
		std::string_view args;
		std::string what(firstWord(rest, args));
		std::string_view value;
		const std::string first(firstWord(args, value));
		std::vector<float> numbers;
		bool injected = false;
		if(!GInput) {
			return refuse("desktop-mouse: no input yet");
		} else if(what == "move" && parseFloats(args, numbers) && numbers.size() == 2) {
			injected = GInput->injectDesktopMouseMove(Vec2i(Vec2f(numbers[0], numbers[1])));
		} else if(what == "at" && first == "menu") {
			Rectf rect;
			std::string why;
			if(value.empty() || !menuTargetRect(value, rect, why)) {
				return refuse("desktop-mouse at menu: " + (value.empty() ? std::string("expected menu <name>") : why));
			}
			g.pointTarget.id = std::string(value);
			g.pointTarget.pixel = rect.center();
			g.pointTarget.menu = true;
			injected = GInput->injectDesktopMouseAt(Vec2s(rect.center()));
		} else if(what == "at" && parseFloats(args, numbers) && numbers.size() == 2) {
			injected = GInput->injectDesktopMouseAt(Vec2s(Vec2f(numbers[0], numbers[1])));
		} else if(what == "button" && parseFloats(first, numbers) && numbers.size() == 1 && numbers[0] >= 1.f
		          && numbers[0] <= 6.f && (value == "down" || value == "up")) {
			injected = GInput->injectDesktopMouseButton(Mouse::ButtonBase + int(numbers[0]) - 1, value == "down");
		} else if(what == "wheel" && parseFloats(args, numbers) && numbers.size() == 1 && numbers[0] != 0.f) {
			injected = GInput->injectDesktopMouseWheel(int(numbers[0]));
		} else {
			return refuse("desktop-mouse: expected move <dx> <dy> | at <x> <y> | at menu <name> | button <1-6> down|up"
			              " | wheel <steps>, got \"" + std::string(rest) + "\"");
		}
		if(!injected) {
			return refuse("desktop-mouse: the input backend cannot inject \"" + std::string(rest) + "\"");
		}
		LogInfo << "[channel] desktop mouse: " << rest;
		return std::string();
	}

	if(word == "desktop-key") {
		// desktop-key <name> down|up: a key of the desktop keyboard through the input backend's event queue, like
		// desktop-mouse (#399) - also where the xrs @key cannot post one (the CI runner's session has no window).
		// <name> as cfg.ini names keys: Space, Escape, Enter, A ...
		std::string_view value;
		const std::string name(firstWord(rest, value));
		const InputKeyId key = Input::getKeyId(name);
		if(key < Keyboard::KeyBase || key >= Keyboard::KeyMax || (value != "down" && value != "up")) {
			return refuse("desktop-key: expected <key name> down|up, got \"" + std::string(rest) + "\"");
		}
		if(!GInput || !GInput->injectDesktopKey(key, value == "down")) {
			return refuse("desktop-key: the input backend cannot inject \"" + std::string(rest) + "\"");
		}
		LogInfo << "[channel] desktop key: " << rest;
		return std::string();
	}

	if(word == "real-input") {
		// real-input key <name> down|up | mouse at <x> <y> | mouse button <1-3> down|up | mouse wheel <steps>
		// (#487): the window system's own messages posted to the game window, the path of the real mouse and
		// keyboard - a test run's window must drop them, unlike desktop-mouse / desktop-key. Windows only
		std::string_view args;
		const std::string device(firstWord(rest, args));
		std::string_view tail;
		const std::string what(firstWord(args, tail));
		std::string_view value;
		const std::string first(firstWord(tail, value));
		std::vector<float> numbers;
		bool posted = false;
		if(!GInput) {
			return refuse("real-input: no input yet");
		} else if(device == "key") {
			const InputKeyId key = Input::getKeyId(what);
			if(key < Keyboard::KeyBase || key >= Keyboard::KeyMax || (tail != "down" && tail != "up")) {
				return refuse("real-input key: expected <key name> down|up, got \"" + std::string(args) + "\"");
			}
			posted = GInput->postDesktopKey(key, tail == "down");
		} else if(device == "mouse" && what == "at" && parseFloats(tail, numbers) && numbers.size() == 2) {
			posted = GInput->postDesktopMouseAt(Vec2s(Vec2f(numbers[0], numbers[1])));
		} else if(device == "mouse" && what == "button" && parseFloats(first, numbers) && numbers.size() == 1
		          && numbers[0] >= 1.f && numbers[0] <= 3.f && (value == "down" || value == "up")) {
			posted = GInput->postDesktopMouseButton(Mouse::ButtonBase + int(numbers[0]) - 1, value == "down");
		} else if(device == "mouse" && what == "wheel" && parseFloats(tail, numbers) && numbers.size() == 1
		          && numbers[0] != 0.f) {
			posted = GInput->postDesktopMouseWheel(int(numbers[0]));
		} else {
			return refuse("real-input: expected key <name> down|up | mouse at <x> <y> | mouse button <1-3> down|up"
			              " | mouse wheel <steps>, got \"" + std::string(rest) + "\"");
		}
		if(!posted) {
			return refuse("real-input: cannot post \"" + std::string(rest) + "\" to the game window (Windows only)");
		}
		LogInfo << "[channel] real input: " << rest;
		return std::string();
	}

	if(word == "vr-point") {
		// vr-point <left|right> item|container [<dx> <dy>] | bag | menu <name> | <x> <y>: turn a simulated hand
		// (--vr-sim) so its aim ray points at the first item in the open inventory bag or container (#38), a free bag
		// slot (#38), a menu widget (#394) or the open book's bookmark (#377), or a pixel of the 2D interface, on the
		// floating panel (#34). Aims only - what the pointer makes of the ray is the game's own path
		std::istringstream iss{std::string(rest)};
		std::string hand, what;
		iss >> hand >> what;
		if((hand != "left" && hand != "right") || what.empty()) {
			return refuse("vr-point: expected left|right item, menu <name> or <x> <y>, got \"" + std::string(rest) + "\"");
		}
		const vr::SimStatus sim = vr::simStatus();
		if(!sim.active) {
			return refuse("vr-point: needs the simulated headset (--vr-sim)");
		}
		State::PointTarget target;
		if(what == "entity" || what == "face" || what == "at") {
			// vr-point <hand> entity <id> [<dx> <dy> <dz>]: the hand's hidden ray at the middle of an entity's
			// box in the world, optionally that many game units beside it (#37) - a hand that drags it away;
			// vr-point <hand> face <id>: at the middle of the entity's mesh face nearest the middle of its box - a
			// thin object (a lever on a wall, an open door) can be all air there (#38);
			// vr-point <hand> at <x> <y> <z>: at a point of the world (game units, y down)
			std::string id = "point";
			Vec3f point(0.f);
			if(what == "entity" || what == "face") {
				Vec3f offset(0.f);
				iss >> id;
				Entity * entity = (id.empty() || id == "point") ? nullptr : entities.getById(id);
				if(!entity) {
					return refuse("vr-point " + what + ": no entity \"" + id + "\"");
				}
				if(what == "entity" && iss >> offset.x && !(iss >> offset.y >> offset.z)) {
					return refuse("vr-point entity: expected entity <id> [<dx> <dy> <dz>], got \"" + std::string(rest) + "\"");
				}
				point = (entity->bbox3D.min + entity->bbox3D.max) * 0.5f + offset;
				if(what == "face") {
					const EERIE_3DOBJ * obj = entity->obj;
					if(!obj || obj->facelist.empty() || obj->vertexWorldPositions.size() != obj->vertexlist.size()) {
						return refuse("vr-point face: " + id + " has no mesh in the world");
					}
					// One the hand's ray reaches: not in or behind a wall (a pulled lever's handle lies against it)
					const Vec3f origin = vr::reticleStatus().hands[hand == "left" ? 0 : 1].origin;
					const Vec3f middle = point;
					float best = std::numeric_limits<float>::max();
					for(const EERIE_FACE & face : obj->facelist) {
						const Vec3f centre = (obj->vertexWorldPositions[face.vid[0]].v + obj->vertexWorldPositions[face.vid[1]].v
						                      + obj->vertexWorldPositions[face.vid[2]].v) / 3.f;
						if(glm::distance(centre, middle) < best && !raycastScene(origin, centre, POLY_NOCOL | POLY_WATER)) {
							best = glm::distance(centre, middle);
							point = centre;
						}
					}
				}
			} else if(!(iss >> point.x >> point.y >> point.z)) {
				return refuse("vr-point at: expected at <x> <y> <z> in game units, got \"" + std::string(rest) + "\"");
			}
			float yaw = 0.f;
			float pitch = 0.f;
			std::string error;
			if(!vr::aimAtWorldPoint(hand == "left" ? vr::Hand::Left : vr::Hand::Right, point, yaw, pitch, error)) {
				return refuse("vr-point entity: " + error);
			}
			std::ostringstream command;
			command << "hand " << hand << " rot " << yaw << ' ' << pitch << " 0";
			error = vr::simCommand(command.str());
			if(!error.empty()) {
				return refuse("vr-point: " + error);
			}
			const Vec3f & from = vr::reticleStatus().hands[hand == "left" ? 0 : 1].origin;
			LogInfo << "[channel] vr-point: the " << hand << " hand aimed at " << id << " (" << point.x << ", "
			        << point.y << ", " << point.z << "): yaw " << yaw << ", pitch " << pitch << " from " << from.x << ", "
			        << from.y << ", " << from.z;
			target.id = id;
			g.pointTarget = target;
			return std::string();
		}
		if(what == "item" || what == "container") {
			// vr-point <hand> container [<dx> <dy>]: the first item of the open container - a chest, a corpse (#38)
			const bool bag = (what == "item");
			if(bag && (!(player.Interface & INTER_INVENTORY) || !entities.player())) {
				return refuse("vr-point item: needs the inventory open");
			}
			if(!bag && !g_secondaryInventoryHud.openContainer()) {
				return refuse("vr-point container: needs a container open");
			}
			// The first item of the shown bag - of a kind when one is named (#376: item potion_life) - and the
			// middle of the pixels it answers to
			std::string kind;
			if(bag) {
				const std::streampos mark = iss.tellg();
				std::string next;
				if(iss >> next && !util::toFloat(next)) {
					kind = next;
				} else {
					iss.clear();
					iss.seekg(mark);
				}
			}
			Entity * item = nullptr;
			Vec2f low(0.f), high(0.f);
			const s32 step = 2;
			for(s32 y = 0; y < g_size.height(); y += step) {
				for(s32 x = 0; x < g_size.width(); x += step) {
					const Vec2s pixel = Vec2s(s16(x), s16(y));
					Entity * found = bag ? g_playerInventoryHud.getObj(pixel) : g_secondaryInventoryHud.getObj(pixel);
					if(!found || (item && found != item) || (!kind.empty() && found->className() != kind)) {
						continue;
					}
					if(!item) {
						item = found;
						low = high = Vec2f(float(x), float(y));
					}
					low = glm::min(low, Vec2f(float(x), float(y)));
					high = glm::max(high, Vec2f(float(x), float(y)));
				}
			}
			if(!item) {
				if(!bag) {
					return refuse("vr-point container: no item in the open container");
				}
				return refuse("vr-point item: no item" + (kind.empty() ? std::string() : " of the kind \"" + kind + "\"")
				              + " in the shown inventory bag");
			}
			// Optionally that many pixels beside it: a hand moving while the trigger is held (a drag)
			Vec2f offset(0.f);
			if(!(iss >> offset.x)) {
				offset = Vec2f(0.f);
			} else if(!(iss >> offset.y)) {
				return refuse("vr-point " + what + ": expected " + what + " [<dx> <dy>], got \"" + std::string(rest) + "\"");
			}
			target.id = item->idString();
			target.pixel = (low + high) * 0.5f + offset;
		} else if(what == "bag") {
			// vr-point <hand> bag: the middle of the first free slot of the shown bag - where a dragged item goes, its icon
			// over the slot (#38)
			if(!(player.Interface & INTER_INVENTORY) || !entities.player() || !entities.player()->inventory) {
				return refuse("vr-point bag: needs the inventory open");
			}
			bool found = false;
			Vec2f low(0.f), high(0.f);
			const s32 step = 2;
			for(s32 y = 0; y < g_size.height(); y += step) {
				for(s32 x = 0; x < g_size.width(); x += step) {
					if(g_playerInventoryHud.containsPos(Vec2s(s16(x), s16(y)))) {
						low = found ? glm::min(low, Vec2f(float(x), float(y))) : Vec2f(float(x), float(y));
						high = found ? glm::max(high, Vec2f(float(x), float(y))) : Vec2f(float(x), float(y));
						found = true;
					}
				}
			}
			if(!found) {
				return refuse("vr-point bag: the bag is not shown");
			}
			// The bag's slots fill its area evenly, row by row; the area is one slot wider and taller than the grid
			// (PlayerInventoryHud::InPlayerInventoryBag)
			const Vec3s size = entities.player()->inventory->size();
			const Vec2f slot = (high - low) / Vec2f(float(size.x + 1), float(size.y + 1));
			found = false;
			for(s16 row = 0; row < size.y && !found; row++) {
				for(s16 column = 0; column < size.x && !found; column++) {
					const Vec2f centre = low + slot * (Vec2f(float(column), float(row)) + Vec2f(0.5f));
					const Vec2s pixel = Vec2s(s16(centre.x), s16(centre.y));
					if(g_playerInventoryHud.containsPos(pixel) && !g_playerInventoryHud.getObj(pixel)) {
						target.pixel = centre;
						found = true;
					}
				}
			}
			if(!found) {
				return refuse("vr-point bag: no free slot in the shown bag");
			}
			// A dragged item is put where its icon is, which hangs beside the cursor: the icon over the slot
			if(g_draggedEntity) {
				target.pixel -= g_draggedIconOffset;
			}
			target.id = "bag";
		} else if(what == "menu") {
			// vr-point <hand> menu <name>: the middle of a widget of the main menu or of a target of the
			// character creation (#394)
			std::string name;
			iss >> name;
			Rectf rect;
			std::string why;
			if(name.empty() || !menuTargetRect(name, rect, why)) {
				return refuse("vr-point menu: " + (name.empty() ? std::string("expected menu <name>") : why));
			}
			target.id = name;
			target.pixel = rect.center();
			target.menu = true;
		} else {
			std::istringstream numbers{std::string(rest)};
			std::string skip;
			float x = 0.f, y = 0.f;
			if(!(numbers >> skip >> x >> y)) {
				return refuse("vr-point: expected left|right <x> <y> in the 2D interface's pixels, got \"" + std::string(rest) + "\"");
			}
			target.id = "pixel";
			target.pixel = Vec2f(x, y);
		}
		Vec3f point(0.f);
		if(!vr::panelPoint(target.pixel, point)) {
			return refuse("vr-point: no floating panel shown (no menu open)");
		}
		const Vec3f from = sim.rig.hands[hand == "left" ? 0 : 1].position;
		const Vec3f direction = glm::normalize(point - from);
		const float yaw = glm::degrees(std::atan2(-direction.x, -direction.z));
		const float pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.f, 1.f)));
		std::ostringstream command;
		command << "hand " << hand << " rot " << yaw << ' ' << pitch << " 0";
		std::string error = vr::simCommand(command.str());
		if(!error.empty()) {
			return refuse("vr-point: " + error);
		}
		LogInfo << "[channel] vr-point: the " << hand << " hand aimed at " << target.id << " (pixel "
		        << target.pixel.x << ", " << target.pixel.y << "): yaw " << yaw << ", pitch " << pitch;
		g.pointTarget = target;
		return std::string();
	}

	if(word == "save") {
		// A new save without a thumbnail; the newest save is slot 0 for loadslot (#130)
		if(rest.empty()) {
			return refuse("save: expected a save name");
		}
		if(ARXmenu.mode() != Mode_InGame || !g_currentArea) {
			return refuse("save: needs a running game (start with --loadlevel N or use loadslot)");
		}
		LogInfo << "[channel] saving as \"" << rest << '"';
		if(!savegames.save(rest, SavegameHandle(), Image())) {
			return refuse("save: the game could not save (see the save event and the lines before it)");
		}
		return std::string();
	}

	if(word == "watch") {
		// watch <id> | watch off: report one entity's place in status.json (watch.*), e.g. an item dragged by a hand (#37)
		const std::string id(rest);
		if(id.empty()) {
			return refuse("watch: expected an entity id or off");
		}
		if(id == "off") {
			g.watch.clear();
			return std::string();
		}
		if(!entities.getById(id)) {
			return refuse("watch: no entity \"" + id + "\"");
		}
		g.watch = id;
		return std::string();
	}

	if(word == "entities") {
		// One line per entity into <dir>\entities.txt: where the scenarios of the test saves put the player
		std::string filter(rest);
		util::makeLowercase(filter);
		std::ostringstream oss;
		size_t count = 0;
		for(const Entity & entity : entities) {
			std::string id = entity.idString();
			std::string lower = id;
			util::makeLowercase(lower);
			if(!filter.empty() && lower.find(filter) == std::string::npos) {
				continue;
			}
			const char * kind = "other";
			if(entity.ioflags & IO_NPC) {
				kind = "npc";
			} else if(entity.ioflags & IO_ITEM) {
				kind = "item";
			} else if(entity.ioflags & IO_FIX) {
				kind = "fix";
			} else if(entity.ioflags & IO_MARKER) {
				kind = "marker";
			} else if(entity.ioflags & IO_CAMERA) {
				kind = "camera";
			}
			oss << id << ' ' << kind << " show=" << int(entity.show) << " pos=" << std::fixed << std::setprecision(1)
			    << entity.pos.x << ' ' << entity.pos.y << ' ' << entity.pos.z;
			if((entity.ioflags & IO_NPC) && entity._npcdata) {
				oss << " life=" << entity._npcdata->lifePool.current;
			}
			// What the hand reticles and the mouse can hover (its script switches it, #210)
			oss << " interactive=" << ((entity.gameFlags & GFLAG_INTERACTIVITY) ? 1 : 0);
			oss << '\n';
			count++;
		}
		if(!fs::write(g.dir / "entities.txt", oss.str())) {
			return refuse("entities: could not write " + (g.dir / "entities.txt").string());
		}
		LogInfo << "[channel] entities: " << count << " written to " << (g.dir / "entities.txt");
		return std::string();
	}

	if(word == "setpos") {
		// setpos <x> <y> <z> [yaw]: the player to exact coordinates (entities.txt lists them), for scenario saves
		std::istringstream iss{std::string(rest)};
		float x = 0.f, y = 0.f, z = 0.f, yaw = 0.f;
		if(!(iss >> x >> y >> z)) {
			return refuse("setpos: expected x y z [yaw], got \"" + std::string(rest) + "\"");
		}
		bool hasYaw = bool(iss >> yaw);
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse(std::string("setpos: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		if(hasYaw) {
			player.desiredangle.setYaw(yaw);
			player.angle.setYaw(yaw);
		}
		ARX_INTERACTIVE_Teleport(entities.player(), Vec3f(x, y, z));
		LogInfo << "[channel] player to " << x << " " << y << " " << z;
		return std::string();
	}

	if(word == "lookat") {
		// lookat <entity>: turn the player towards an entity. An entity's position is on the floor (levers,
		// chests, items), so the aim is 40 units above it, seen from the player's eyes (player.pos is the eye)
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse(std::string("lookat: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		Entity * target = entities.getById(rest);
		if(!target) {
			return refuse("lookat: unknown entity \"" + std::string(rest) + "\"");
		}
		Vec3f delta = target->pos - player.pos;
		delta.y -= 40.f;
		if(glm::length(Vec2f(delta.x, delta.z)) < 1.f) {
			return refuse("lookat: \"" + std::string(rest) + "\" is where the player stands");
		}
		Anglef angle = vectorToAngle(glm::normalize(delta));
		player.desiredangle.setYaw(angle.getYaw());
		player.angle.setYaw(angle.getYaw());
		player.desiredangle.setPitch(angle.getPitch());
		player.angle.setPitch(angle.getPitch());
		LogInfo << "[channel] player looks at " << rest << " (yaw " << angle.getYaw() << ", pitch " << angle.getPitch() << ")";
		return std::string();
	}

	if(word == "approach") {
		// approach <entity> [<distance>]: the player to a spot where the hand reticles reach an entity, facing it -
		// found by number, not by trying spots in launches (#210, TRAPS.md). Every 15 degrees on rings around the
		// entity: a floor near the entity's own, room for the player's cylinder there, the eye within the pick's
		// reach (350 units of the entity's position, VRReticle.cpp) and nothing of the level before the entity
		// on the rays from the eye and from both simulated hands to the middle of its box. The spot nearest to
		// <distance> along the floor (default 170) wins. Far from the player an entity is not updated (its box,
		// its faces): the first call can only test the level, a second one from there tests the faces too
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse(std::string("approach: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		std::istringstream iss{std::string(rest)};
		std::string id;
		float wanted = 170.f;
		if(!(iss >> id)) {
			return refuse("approach: expected <entity> [<distance>], got \"" + std::string(rest) + "\"");
		}
		if(!iss.eof() && !(iss >> wanted)) {
			return refuse("approach: expected <entity> [<distance>], got \"" + std::string(rest) + "\"");
		}
		Entity * target = entities.getById(id);
		if(!target || target == entities.player()) {
			return refuse("approach: unknown entity \"" + id + "\"");
		}
		const Vec3f base = target->pos;
		const EERIE_3D_BBOX & box = target->bbox3D;
		const Vec3f middle = (box.min + box.max) * 0.5f;
		// Updated: in the treat zone around the player, and its box drawn where the entity is
		const bool updated = (target->gameFlags & GFLAG_ISINTREATZONE) && box.max.x > box.min.x
		                     && closerThan(Vec2f(middle.x, middle.z), Vec2f(base.x, base.z), 300.f);
		const Vec3f aim = updated ? middle : base + Vec3f(0.f, -80.f, 0.f);
		const float reach = 350.f - 20.f;
		// Whether nothing of the level is before the entity on the ray from origin to the aim point
		auto sees = [&](const Vec3f & origin) {
			const Vec3f direction = glm::normalize(aim - origin);
			if(!updated) {
				// Short of the position: a lever's is at the wall it is fixed to
				return !raycastScene(origin, aim - direction * 30.f, POLY_NOCOL | POLY_WATER);
			}
			const Vec3f end = origin + direction * (glm::distance(origin, aim) + 100.f);
			const EntityRaycastResult first = raycastEntities(origin, end, POLY_TRANS, RaycastIgnorePlayer);
			if(first.entity != target) {
				return false;
			}
			const RaycastResult scene = raycastScene(origin, end, POLY_NOCOL | POLY_WATER);
			return !scene || glm::distance(origin, scene.pos) >= glm::distance(origin, first.pos);
		};
		long climb = COLLIDED_CLIMB_POLY;
		struct Rejects {
			int floor = 0;
			int room = 0;
			int reach = 0;
			int sight = 0;
		} rejects;
		bool found = false;
		Vec3f best(0.f);
		float bestScore = 0.f;
		int tested = 0;
		for(float radius = 80.f; radius <= 320.f; radius += 20.f) {
			for(float degrees = 0.f; degrees < 360.f; degrees += 15.f) {
				tested++;
				const Vec3f around = base + angleToVectorXZ(degrees) * radius;
				float floorY = 0.f;
				if(!CheckInPoly(Vec3f(around.x, base.y - 120.f, around.z), &floorY)
				   || std::abs(floorY - base.y) > 100.f) {
					rejects.floor++;
					continue;
				}
				const Vec3f feet(around.x, floorY, around.z);
				const Cylinder body(feet, ARXCHARACTER::baseRadius(), ARXCHARACTER::baseHeight());
				if(CheckAnythingInCylinder(body, entities.player(), CFLAG_JUST_TEST | CFLAG_PLAYER) < -20.f) {
					rejects.room++;
					continue;
				}
				const Vec3f eye = feet + ARXCHARACTER::baseOffset();
				if(!closerThan(eye, base, reach)) {
					rejects.reach++;
					continue;
				}
				// The simulated hands (SimRig: 20 cm aside, 45 below the eyes, 35 ahead), facing the aim point
				Vec3f ahead = aim - eye;
				ahead.y = 0.f;
				ahead = glm::normalize(ahead);
				const Vec3f side(-ahead.z, 0.f, ahead.x);
				const Vec3f hand = eye + ahead * 35.f + Vec3f(0.f, 45.f, 0.f);
				if(!sees(eye) || !sees(hand + side * 20.f) || !sees(hand - side * 20.f)) {
					rejects.sight++;
					continue;
				}
				const float score = std::abs(radius - wanted);
				if(!found || score < bestScore) {
					found = true;
					best = eye;
					bestScore = score;
				}
			}
		}
		COLLIDED_CLIMB_POLY = climb;
		if(!found) {
			std::ostringstream why;
			why << "approach: no spot in reach of " << id << " among " << tested << " (no floor " << rejects.floor
			    << ", no room " << rejects.room << ", out of reach " << rejects.reach << ", no sight " << rejects.sight
			    << (updated ? "" : "; the entity is not updated here - come closer first") << ")";
			return refuse(why.str());
		}
		// The player's teleport takes the feet (player.pos, the eye, is 170 units above them - TRAPS.md)
		ARX_INTERACTIVE_Teleport(entities.player(), best - ARXCHARACTER::baseOffset());
		const Anglef angle = vectorToAngle(glm::normalize(aim - best));
		player.desiredangle.setYaw(angle.getYaw());
		player.angle.setYaw(angle.getYaw());
		player.desiredangle.setPitch(angle.getPitch());
		player.angle.setPitch(angle.getPitch());
		LogInfo << "[channel] approach " << id << ": the eye to " << best.x << ' ' << best.y << ' ' << best.z
		        << " (yaw " << angle.getYaw() << ", pitch " << angle.getPitch() << ", " << glm::distance(best, base)
		        << " from its position), " << (updated ? "the entity's faces seen" : "the level only: not updated there")
		        << "; of " << tested << " spots: no floor " << rejects.floor << ", no room " << rejects.room
		        << ", out of reach " << rejects.reach << ", no sight " << rejects.sight;
		return std::string();
	}

	if(word == "place") {
		// place <entity> <ahead> [<right>]: an entity on the floor ahead of the player (units along the player's
		// heading, and to its right), facing the player - a target in reach for melee checks (#39)
		if(currentState() != GameState::Gameplay || !entities.player()) {
			return refuse(std::string("place: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		std::istringstream iss{std::string(rest)};
		std::string id;
		float ahead = 0.f, right = 0.f;
		if(!(iss >> id >> ahead)) {
			return refuse("place: expected <entity> <ahead> [<right>], got \"" + std::string(rest) + "\"");
		}
		iss >> right;
		Entity * target = entities.getById(id);
		if(!target || target == entities.player()) {
			return refuse("place: unknown entity \"" + id + "\"");
		}
		const float yaw = player.angle.getYaw();
		const Vec3f forward = angleToVectorXZ(yaw);
		const Vec3f side = angleToVectorXZ(yaw - 90.f);
		const Vec3f pos = player.basePosition() + forward * ahead + side * right;
		ARX_INTERACTIVE_Teleport(target, pos);
		target->angle.setYaw(MAKEANGLE(yaw + 180.f));
		LogInfo << "[channel] " << id << " placed at " << pos.x << " " << pos.y << " " << pos.z << " (" << ahead
		        << " ahead, " << right << " right of the player)";
		return std::string();
	}

	if(word == "playground") {
		// playground pull mode|respawn | kill | fight | place <ahead> [<right>] | hit <n>: the test playground's NPC and
		// levers (#321.2). pull sends the lever what a hand pull sends it (Interface.cpp: SM_ACTION from the player)
		if(!isPlaygroundArea(g_currentArea) || !entities.player()) {
			return refuse("playground: needs the playground level (area 30) on screen");
		}
		std::string_view args;
		std::string_view what = firstWord(rest, args);
		Entity * npc = findPlaygroundEntity("npc");
		if(what == "pull") {
			std::string role = "lever_" + std::string(args);
			Entity * lever = (args == "mode" || args == "respawn") ? findPlaygroundEntity(role) : nullptr;
			if(!lever) {
				return refuse("playground pull: expected mode or respawn, got \"" + std::string(args) + "\"");
			}
			SendIOScriptEvent(entities.player(), lever, SM_ACTION);
			LogInfo << "[channel] playground: " << lever->idString() << " pulled (" << args << ")";
			return std::string();
		}
		if(!npc || !npc->_npcdata) {
			return refuse("playground " + std::string(what) + ": the level has no NPC");
		}
		if(what == "kill") {
			ARX_DAMAGES_ForceDeath(*npc, entities.player());
			return std::string();
		}
		if(what == "fight") {
			// The NPC fights the player now, as a fight script sets it (behavior -f move_to, settarget player): a
			// test's set-up for what only fighting NPCs feel - a VR touch (#465)
			ARX_NPC_Behaviour_Change(npc, BEHAVIOUR_FIGHT | BEHAVIOUR_MOVE_TO, 0);
			npc->targetinfo = EntityHandle_Player;
			return std::string();
		}
		if(what == "place") {
			std::istringstream iss{std::string(args)};
			float ahead = 0.f, right = 0.f;
			if(!(iss >> ahead)) {
				return refuse("playground place: expected <ahead> [<right>], got \"" + std::string(args) + "\"");
			}
			iss >> right;
			const float yaw = player.angle.getYaw();
			const Vec3f pos = player.basePosition() + angleToVectorXZ(yaw) * ahead + angleToVectorXZ(yaw - 90.f) * right;
			ARX_INTERACTIVE_Teleport(npc, pos);
			npc->angle.setYaw(MAKEANGLE(yaw + 180.f));
			LogInfo << "[channel] playground: " << npc->idString() << " placed at " << pos.x << " " << pos.y << " " << pos.z;
			return std::string();
		}
		if(what == "hit") {
			float damage = 0.f;
			std::istringstream iss{std::string(args)};
			if(!(iss >> damage) || damage <= 0.f) {
				return refuse("playground hit: expected a damage greater than 0, got \"" + std::string(args) + "\"");
			}
			// What a weapon hit does to the NPC (Equipment.cpp), without the weapon
			damageNpc(*npc, damage, entities.player(), nullptr, DAMAGE_TYPE_METAL, &npc->pos);
			return std::string();
		}
		return refuse("playground: expected pull, kill, fight, place or hit, got \"" + std::string(what) + "\"");
	}

	if(word == "levelstats") {
		// levelstats start | report: the level tour's frame times, player position, exits and ladders (#596)
		if(currentState() != GameState::Gameplay || !g_currentArea) {
			return refuse(std::string("levelstats: needs a level on screen (state ") + gameStateName(currentState()) + ")");
		}
		if(rest == "start") {
			g.levelStats.measuring = true;
			g.levelStats.lastFrame = 0;
			g.levelStats.frameUs.clear();
			return std::string();
		}
		if(rest == "report") {
			if(!g.levelStats.measuring) {
				return refuse("levelstats report: no levelstats start before it");
			}
			return reportLevelStats();
		}
		return refuse("levelstats: expected start or report, got \"" + std::string(rest) + "\"");
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
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		const PlatformInstant start = platform::getTime();
		ARX_CHANGELEVEL_Change(AreaId(u32(area)), target, 0.f);
		// How much of the change the loading screen took (#440): its frames are bound by the present
		const LoadingScreenStats screen = loadingScreenStats();
		LogInfo << "[channel] level " << area << " loaded in " << toMsi(platform::getTime() - start)
		        << " ms, the loading screen drew " << screen.frames << " frames in " << toMsi(screen.drawTime) << " ms";
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

/*
 * Run the batch's lines from the first one not run yet. A `waitframes <n>` stops it: the rest of the
 * lines run, and the ack is written, in the update of the frame after n more frames were rendered
 * (#522) - so a test waiting for frames needs no status.json polling, only the ack it waits for anyway.
 */
void runBatch() {

	State::Batch & batch = g.batch;
	batch.resumeFrame = 0;
	while(batch.next < batch.lines.size()) {
		const std::string & line = batch.lines[batch.next++];
		g.lastCmd = line;
		std::string_view rest;
		std::string word(firstWord(line, rest));
		util::makeLowercase(word);
		if(word == "waitframes") {
			long frames = 0;
			if(!parseIndex(rest, frames)) {
				batch.errors++;
				g.lastCmdError = refuse("waitframes: expected a number of frames, got \"" + std::string(rest) + "\"");
				continue;
			}
			if(frames > 0) {
				batch.resumeFrame = g.frame + u64(frames);
				return;
			}
			continue;
		}
		std::string error = execute(line);
		if(!error.empty()) {
			batch.errors++;
			g.lastCmdError = error;
		}
	}

	// The status first, then the ack (#23): a test that waits for the ack reads status.json right after it, and the ack
	// written first left a window - the status still of the batch before - in which that read got the old state (the
	// merge gate's scene-switches: `scene view observer` acked, `scene.view` read `first`). The status carries the
	// batch's number (cmdSeq = ackedSeq) from the moment it shows the batch's effects, as the ack follows at once
	const unsigned errors = batch.errors;
	g.ackedSeq = g.cmdSeq;
	batch = State::Batch();
	writeStatus(platform::getTime());
	writeAck(errors);
}

/*
 * Read command.txt whole, sharing the file with whoever still has it open. The test writes command.txt.tmp and
 * renames it into place; on Windows the renamer still holds the file open with DELETE access for a moment after
 * the new name is visible, and an open without FILE_SHARE_DELETE (std::ifstream, fs::read) fails then with a
 * sharing violation. The old code read "" in that moment, deleted the file and acknowledged nothing: the batch was
 * lost without a line in the log (CI run 39120032077 attempt 1, vr-remap's 'waitframes 5' after 'vr-input down
 * left.grip': the ack of the quit came back as seq 15, the lost batch's number; #527). The moment is microseconds
 * when the PC is idle and grows with every preemption of the writer's thread, so it hit under three games. Returns
 * false when the file could not be opened: then it is left alone and read next frame.
 */
bool readCommandFile(const fs::path & file, std::string & contents) {

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	HANDLE handle = CreateFileW(platform::WinPath(file), GENERIC_READ,
	                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if(handle == INVALID_HANDLE_VALUE) {
		return false;
	}
	contents.clear();
	char buffer[4096];
	DWORD read = 0;
	while(ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read != 0) {
		contents.append(buffer, read);
	}
	CloseHandle(handle);
	return true;
#else
	contents = fs::read(file);
	return true;
#endif

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

	std::string contents;
	if(!readCommandFile(file, contents)) {
		// Still open by the test's rename (see above): the file stays, the next frame reads it
		g.commandReadRetries++;
		if(g.commandReadRetries == 1 || g.commandReadRetries % 100 == 0) {
			LogInfo << "[channel] " << file << " cannot be opened yet (its writer still has it open), reading it next frame"
			        << (g.commandReadRetries > 1 ? " (" + std::to_string(g.commandReadRetries) + " frames so far)" : "");
		}
		if(g.commandReadRetries >= 900) {
			LogWarning << "[channel] giving up on " << file << " after " << g.commandReadRetries << " frames: it stays open";
			g.commandReadRetries = 0;
			fs::remove(file);
		}
		return;
	}
	g.commandReadRetries = 0;
	if(!fs::remove(file)) {
		// Do not run the same batch again next frame
		LogWarning << "[channel] could not delete " << file << " after reading it";
		fs::write(file, std::string_view());
	}

	std::vector<std::string> lines;
	for(size_t pos = 0; pos < contents.size(); ) {
		size_t end = contents.find('\n', pos);
		if(end == std::string::npos) {
			end = contents.size();
		}
		std::string_view line = trim(std::string_view(contents).substr(pos, end - pos));
		if(!line.empty() && line.front() != '#') {
			lines.emplace_back(line);
		}
		pos = end + 1;
	}
	if(lines.empty()) {
		return;
	}

	g.cmdSeq++;
	g.lastCmdError.clear();
	g.batch = State::Batch();
	g.batch.lines = std::move(lines);
	runBatch();
}

//! stick-for: keep the walk the hold made, and let the stick go once the player has walked far enough (#370)
void updateStickFor() {

	State::StickFor & hold = g.stickFor;
	if(!hold.active) {
		return;
	}

	if(currentState() != GameState::Gameplay || !entities.player()) {
		// The level is gone (a load, the menu): nothing to measure the distance on, let go now
		vr::inputCommand("stick " + hold.hand + " 0 0");
		hold.active = false;
		LogInfo << "[channel] stick-for: " << hold.hand << " stick let go, the level is not on screen";
		return;
	}

	const vr::WalkStatus & walk = vr::walkStatus();
	if(walk.walking) {
		hold.walk = walk;
	}
	hold.moved = glm::distance(Vec2f(player.pos.x, player.pos.z), hold.start);
	if(hold.moved >= hold.units) {
		vr::inputCommand("stick " + hold.hand + " 0 0");
		hold.active = false;
		LogInfo << "[channel] stick-for: " << hold.hand << " stick let go after " << hold.moved << " units";
	} else if(g_gameTime.now() >= hold.deadline) {
		vr::inputCommand("stick " + hold.hand + " 0 0");
		hold.active = false;
		hold.timedOut = true;
		LogWarning << "[channel] stick-for: " << hold.hand << " stick let go at its time limit after only "
		           << hold.moved << " of " << hold.units << " units";
	}

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
	fs::remove(g.dir / "hang.txt");

	g.counter = new LogCounter;
	Logger::add(g.counter);

	// The file logger flushes on its own every second (#129); a test reading arx.log wants it fresher: flushed after a
	// status that landed. A test reads status.json right after a log line it waited for (@log, then @assert): the old
	// main-thread write never showed a line newer than the status on disk, as nothing was logged between building,
	// writing and flushing. On the writer thread the frame goes on logging meanwhile - a command's event in the same
	// frame (scene-switches: `scene.active` false after its `vr.scene state=start` line) - so the log is flushed only
	// while no line came after the status was built; else the next frame builds a fresh status, which flushes it. The
	// ack does not wait for that: a test reads its batch's own state right after it (melee-damage: `playground fight`,
	// and the fight is over a frame later)
	g.writer = new ChannelWriter(
		ChannelWriter::Files{ std::filesystem::path((g.dir / "status.json").string()),
		                      std::filesystem::path((g.dir / "status.json.tmp").string()),
		                      std::filesystem::path((g.dir / "ack.txt").string()) },
		[](unsigned long long lines) {
			if(g.counter->lines.load() != lines) {
				g.freshStatus = true;
				return;
			}
			Logger::flush();
			if(g.counter->lines.load() != lines) {
				// A line came in before the flush took the logger's lock: a status after it follows
				g.freshStatus = true;
			}
		},
		[](std::string_view message) { LogWarning << "[channel] " << message; });

	// The simulated headset's shots go next to the channel's own
	vr::setSimCaptureDir((g.dir / "capture").string());

	// Inherited by the crash processor: an automated run has nobody to click the crash dialog
	platform::setEnvironmentVariable("ARX_UNATTENDED", "1");

	LogInfo << "[channel] test channel enabled in " << g.dir << " (pid " << platform::getProcessId() << ")";

	writeStatus(g.startInstant);
}

void update() {

	// The hang watchdog runs in every launch, with or without the test channel (#440; always-on since
	// #545): the main thread beats every frame and a stall is reported with its stack and a dump (reportHang)
	if(!g.watchdog) {
		g.watchdog = std::make_unique<platform::HangWatchdog>("main thread",
			std::chrono::milliseconds(g.watchdogThresholdMs), reportHang);
	}
	g.watchdog->beat("frame");
	// One beat record a second in the flight recorder (#545): a stall shows as the gap before the hang's exit cause
	{
		static PlatformInstant s_lastBeatRecord = PlatformInstant(0);
		const PlatformInstant beatNow = platform::getTime();
		if(s_lastBeatRecord == PlatformInstant(0) || toMsi(beatNow - s_lastBeatRecord) >= 1000) {
			s_lastBeatRecord = beatNow;
			platform::flightRecord(platform::RecordKind::Beat, "frame", s64(g.watchdog->stalls()));
		}
	}
	if(!g.watchdogLogged && g.watchdog->watching()) {
		g.watchdogLogged = true;
		LogInfo << "[channel] hang watchdog: watching the main thread, threshold "
		        << g.watchdog->threshold().count() << " ms";
	}

	if(!g.enabled) {
		return;
	}

	g.frame++;
	PlatformInstant now = platform::getTime();

	// levelstats (#596): one frame is from this point to the same point of the next frame, as in arx-perf.log
	if(g.levelStats.measuring) {
		if(g.levelStats.lastFrame != PlatformInstant(0)) {
			g.levelStats.frameUs.push_back(toUs(now - g.levelStats.lastFrame));
		}
		g.levelStats.lastFrame = now;
	}

	if(g_crashAt != PlatformInstant(0) && toUs(now - g_crashAt) >= 0) {
		LogWarning << "[crash] deliberate crash requested through the test channel";
		volatile int * nothing = nullptr;
		*nothing = 1;
	}

	GameState state = currentState();
	if(state != g.state) {
		setState(state);
	}

	updateStickFor();

	// camera.stillMs (#522): measured from where the camera was when it last moved, so a slow drift counts too
	{
		State::CameraWatch & watch = g.cameraWatch;
		const Vec3f angles(g_playerCamera.angle.getPitch(), g_playerCamera.angle.getYaw(), g_playerCamera.angle.getRoll());
		const float threshold = 0.0005f; // units and degrees: below what status.json shows (0.001)
		const Vec3f turn = glm::abs(angles - watch.angles);
		if(watch.movedAt == PlatformInstant(0) || glm::distance(g_playerCamera.m_pos, watch.pos) > threshold
		   || turn.x > threshold || turn.y > threshold || turn.z > threshold) {
			watch.pos = g_playerCamera.m_pos;
			watch.angles = angles;
			watch.movedAt = now;
		}
	}

	const PlatformInstant commandsStart = platform::getTime();
	if(g.batch.resumeFrame != 0) {
		// A batch waits for frames (waitframes): no new batch before its ack
		if(g.frame >= g.batch.resumeFrame) {
			runBatch();
		}
	} else {
		pollCommands();
	}
	noteSlowPart("commands", commandsStart);

	if(g.freshStatus.exchange(false) || toMsi(now - g.lastStatusTime) >= StatusIntervalMs) {
		writeStatus(now, true);
	} else {
		g.writer->poll(); // a timer status the writer's lock kept back
	}

}

void afterRender() {

	if(!g.enabled) {
		return;
	}

	// The motion probe (#23) after the frame's update, and the records the simulated headset asked for in it:
	// the state this frame displays, at the display time its timeline named
	if(g_currentArea && entities.player()) {
		vr::MotionSample sample;
		sample.seconds = double(toUs(g_platformTime.frameStart() - PlatformInstant(0))) * 1e-6;
		sample.position = player.pos;
		sample.yaw = player.angle.getYaw();
		sample.jumping = player.jumpphase != NotJumping;
		g.motion.update(sample);
		// The free hand's bend: its speed since the target changed, measured while it is on its way
		const vr::PlayerArmStatus & free = vr::playerBodyStatus().hands[size_t(vr::Hand::Left)];
		const float curl = free.curl.indexRoot;
		const float target = free.curlTarget.indexRoot;
		if(target != g.curlTargetLast) {
			g.curlFrom = g.curlLast;
			g.curlFromSeconds = g.curlLastSeconds;
		}
		if(curl != target && sample.seconds > g.curlFromSeconds) {
			g.curlSpeed = std::abs(curl - g.curlFrom) / float(sample.seconds - g.curlFromSeconds);
		}
		g.curlLast = curl;
		g.curlTargetLast = target;
		g.curlLastSeconds = sample.seconds;
	}
	const PlatformInstant recordsStart = platform::getTime();
	for(const std::string & name : vr::takeSimRecords()) {
		std::string json = "{}";
		if(g_currentArea && entities.player()) {
			JsonWriter values;
			writeMotionValues(values);
			json = values.str();
			json.pop_back(); // the newline
		}
		auto found = std::find_if(g.records.begin(), g.records.end(), [&](const auto & record) { return record.first == name; });
		if(found != g.records.end()) {
			found->second = json;
		} else {
			g.records.emplace_back(name, json);
		}
		// The values in the log too: the evidence of a CI run outlives its status.json
		LogInfo << "[channel] record " << name << ": " << json;
	}
	noteSlowPart("records", recordsStart);

	if(g.simShotWatch.active) {
		// The simulator saves its shots when the frame ends (xrEndFrame), before this; a test waits for the
		// shot in status.json
		const vr::SimStatus sim = vr::simStatus();
		if(sim.shots.size() > g.simShotWatch.shots || g.frame >= g.simShotWatch.lastFrame) {
			g.simShotWatch.active = false;
			writeStatus(platform::getTime());
		}
	}

	if(g.pendingCapture.empty()) {
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

	// Each quarter's verdict (#334): a menu filling the window has detail in all four, one in a corner of it or on
	// the floating panel seen from the eye leaves flat background in some
	if(image.getFormat() == Image::Format_R8G8B8) {
		vr::RgbImage rgb(image.getWidth(), image.getHeight());
		const size_t row = rgb.width * 3;
		for(size_t y = 0; y < rgb.height; y++) {
			// The snapshot's rows go top to bottom, the picture's bottom to top
			std::copy_n(image.getData() + (rgb.height - 1 - y) * row, row, rgb.pixels.data() + y * row);
		}
		const vr::ImageRegion regions[] = {
			{ 0.f, 0.5f, 0.5f, 1.f }, { 0.5f, 0.5f, 1.f, 1.f }, { 0.f, 0.f, 0.5f, 0.5f }, { 0.5f, 0.f, 1.f, 0.5f },
		};
		const u8 black[3] = { 0, 0, 0 };
		g.capture.name = name;
		g.capture.size = Vec2i(int(rgb.width), int(rgb.height));
		for(size_t i = 0; i < g.capture.quarters.size(); i++) {
			g.capture.quarters[i] = vr::pixelStats(rgb, regions[i], black);
		}
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

	// Logged by the event log (game.state to=LOADING area=N)
	ARX_UNUSED(area);
	g.state = GameState::Loading;
	beat("level load");
	writeStatus(platform::getTime());

	if(g.stallLoadMs != 0) {
		long stallMs = g.stallLoadMs;
		g.stallLoadMs = 0;
		LogInfo << "[channel] stalling the level load for " << stallMs << " ms";
		// Named in a hang report: the run digest knows this stall is on purpose (log-digest-allowlist.txt)
		note("a stall the test channel asked for");
		platform::stallThisThread(std::chrono::milliseconds(stallMs));
	}
}

void beat(const char * where) {
	if(g.watchdog) {
		g.watchdog->beat(where);
	}
}

void note(const char * what) {
	if(g.watchdog) {
		g.watchdog->note(what);
	}
}

void shutdown() {

	// The game is down: nothing beats any more (the watchdog runs even without the test channel, so it is
	// stopped here whether or not the channel was enabled)
	g.watchdog.reset();

	if(!g.enabled) {
		return;
	}

	g.state = GameState::Exited;
	g.cleanShutdown = true;
	writeStatus(platform::getTime());
	// The final status (and an ack still on its way) lands before the game ends
	delete g.writer;
	g.writer = nullptr;

	g.enabled = false;
}

} // namespace channel
