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
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "core/SaveGame.h"
#include "core/TimeTypes.h"
#include "core/Version.h"
#include "game/Camera.h"
#include "game/EntityManager.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/effect/Quake.h"
#include "graphics/Math.h"
#include "graphics/Renderer.h"
#include "graphics/image/Image.h"
#include "gui/Console.h"
#include "gui/Interface.h"
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
#include "scene/Interactive.h"
#include "util/String.h"
#include "vr/HeadTracking.h"
#include "vr/VRComfort.h"
#include "vr/VRInput.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"
#include "vr/WorldScale.h"
#include "window/RenderWindow.h"

extern bool START_NEW_QUEST;
extern bool bGCroucheToggle;

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
	json.value("pending", u64(sim.pending));

	json.object("view");
	json.value("eyeWidth", sim.view.eyeSize.x);
	json.value("eyeHeight", sim.view.eyeSize.y);
	json.value("ipd", sim.view.ipd);
	json.value("rate", sim.view.rate);
	json.end();

	json.object("rig");
	writeVec(json, "head", sim.rig.head.position);
	writeAngles(json, "headRot", sim.rig.head.angles);
	writeVec(json, "leftHand", sim.rig.hands[0].position);
	writeVec(json, "rightHand", sim.rig.hands[1].position);
	json.value("profile", sim.rig.profile);
	json.value("focused", sim.rig.focused);
	json.value("swapEyes", sim.rig.swapEyes);
	json.object("actions");
	for(const auto & [name, values] : sim.rig.actions) {
		json.object(name);
		json.value("left", values[0].x);
		json.value("right", values[1].x);
		json.end();
	}
	json.end();
	json.end();

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
			json.end();
		}
		json.object("quad");
		json.value("shown", shot.quadShown);
		json.value("file", shot.quadFile);
		writePixelStats(json, "stats", shot.quad);
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
		// Scripts block the controls during scripted scenes (the awakening of a new game): a save made then is not playable (#70)
		json.value("controlsBlocked", BLOCK_PLAYER_CONTROLS);
		// What the controls did (#74): the inventory and the book open, the crouch toggle, a jump under way
		json.value("inventoryOpen", (player.Interface & INTER_INVENTORY) != 0);
		json.value("bookOpen", (player.Interface & INTER_PLAYERBOOK) != 0);
		json.value("crouchToggle", bGCroucheToggle);
		json.value("jumping", player.jumpphase != NotJumping);
		json.end();
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
	// The desktop mirror (#21): "on" while the window shows what one eye sees
	vr::MirrorStatus mirror = vr::mirrorStatus();
	json.object("mirror");
	json.value("state", mirror.state);
	json.value("eye", mirror.eye);
	json.value("frames", mirror.frames);
	json.end();
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
		json.value("clamped", head.clamped);
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
		// What the engine was given (#74): held actions, buttons and keys; the move vector and turn axis
		const vr::GameInputState & game = vr::gameInput();
		json.object("game");
		json.value("held", vr::formatGameInput(game));
		json.value("moveX", game.move.x);
		json.value("moveY", game.move.y);
		json.value("turn", game.turn);
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

	// Game event log (#130): how many events of each name happened, logged or not
	json.object("events");
	json.value("total", u64(gameevent::total()));
	json.object("byName");
	for(const auto & [name, count] : gameevent::counts()) {
		json.value(name, u64(count));
	}
	json.end();
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

	if(word == "vr-input") {
		// The controllers' control table without a headset (#30): down|up|tap <control>, stick, bind, reset
		std::string error = vr::inputCommand(rest);
		return error.empty() ? error : refuse(error);
	}

	if(word == "vr-sim") {
		// The simulated headset (#112): any script command, now or @<frame> on its timeline
		std::string error = vr::simCommand(rest);
		return error.empty() ? error : refuse("vr-sim: " + error);
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

	// The simulated headset's shots go next to the channel's own
	vr::setSimCaptureDir((g.dir / "capture").string());

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

	// Logged by the event log (game.state to=LOADING area=N)
	ARX_UNUSED(area);
	g.state = GameState::Loading;
	writeStatus(platform::getTime());
}

void shutdown() {

	if(!g.enabled) {
		return;
	}

	g.state = GameState::Exited;
	g.cleanShutdown = true;
	writeStatus(platform::getTime());

	g.enabled = false;
}

} // namespace channel
