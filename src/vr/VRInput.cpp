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

#include "vr/VRInput.h"

#include <algorithm>
#include <array>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/TimeTypes.h"
#include "gui/Menu.h"
#include "input/Input.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "platform/Time.h"
#include "util/Number.h"
#include "vr/HeadTracking.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

struct InputState {

	ControllerType type = ControllerType::Unknown;
	ControlResolver resolver = ControlResolver(defaultLayout(ControllerType::Unknown));
	ControllerInput input;
	GameInputMapper game;
	TurnController turn;
	bool smoothTurning = false; //!< a smooth turn is under way (for its start / stop game events)

	//! Controls set by the test channel, merged into what the controllers report
	ControlSet injectedDown;
	ControlSet injectedTaps;
	//! Stick deflections set by the test channel, added to the controllers' (directions: down / up)
	std::array<Stick, HandCount> injectedSticks;

	//! The saved layout of the first controller type was read (#178)
	bool loaded = false;

	//! The VR settings page's remapping (#178)
	BindingCapture capture;
	Event captureEvent = Event::Jump;
	u32 captures = 0;

};

InputState g_input;

//! The right hand decides (the trigger hand of most players), the left one when the right has no profile
ControllerType typeFromProfiles(const std::array<std::string, HandCount> & profiles) {
	ControllerType right = controllerTypeFromProfile(profiles[size_t(Hand::Right)]);
	if(right != ControllerType::Unknown) {
		return right;
	}
	return controllerTypeFromProfile(profiles[size_t(Hand::Left)]);
}

void logConflicts(const ControlLayout & layout) {
	for(const Conflict & conflict : findConflicts(layout)) {
		LogWarning << "[vr] input: " << eventName(conflict.first) << " and " << eventName(conflict.second)
		           << " share " << formatBinding(layout[conflict.first]) << " - both fire";
	}
}

void applyLayout(const ControlLayout & layout, const char * why) {
	if(layout == g_input.resolver.layout()) {
		return;
	}
	g_input.resolver.setLayout(layout);
	LogInfo << "[vr] input: layout " << why;
	logConflicts(layout);
}

fs::path layoutPath(ControllerType type) {
	return fs::getConfigDir() / layoutFileName(type);
}

//! The layout the player saved for these controllers over their default, or the default (#178)
ControlLayout playerLayout(ControllerType type, std::string & why) {

	ControlLayout layout = defaultLayout(type);
	why = std::string("default for ") + controllerTypeName(type);

	const fs::path file = layoutPath(type);
	if(file.empty() || !fs::exists(file)) {
		return layout;
	}

	std::string error;
	if(!parseLayout(fs::read(file), layout, &error)) {
		// The default layout keeps the game playable; the file stays for the player to look at
		LogWarning << "[vr] input: ignoring the saved layout " << file << ": " << error;
		return defaultLayout(type);
	}

	why = std::string("saved for ") + controllerTypeName(type) + " (" + file.string() + ")";
	return layout;
}

void savePlayerLayout(const ControlLayout & layout) {

	const ControllerType type = layoutType(g_input.type);
	const fs::path file = layoutPath(type);
	if(file.empty()) {
		LogWarning << "[vr] input: no config directory - the remapped layout is not saved";
		return;
	}

	const std::string changes = formatLayoutChanges(layout, defaultLayout(g_input.type));
	if(changes.empty()) {
		if(fs::exists(file) && !fs::remove(file)) {
			LogWarning << "[vr] input: could not remove " << file;
		}
		return;
	}

	std::string text = std::string("# Arx Fatalis VR: the controls remapped in the VR settings for ")
	                   + controllerTypeName(type) + " controllers (#178).\n"
	                   "# The events that differ from the default layout; delete the file to reset them.\n";
	text += changes;
	if(!fs::write(file, text)) {
		LogWarning << "[vr] input: could not save the remapped layout to " << file;
	}
}

//! While the VR settings capture a binding the controls do nothing else (#178)
void updateCapture(s64 timeMs) {

	BindingCapture & capture = g_input.capture;
	if(!capture.active()) {
		return;
	}

	const Control refused = capture.refused();
	switch(capture.update(timeMs, g_input.input.down, g_input.input.taps)) {
		case BindingCapture::State::Done: {
			g_input.captures++;
			gameevent::Event("vr.remap")("event", eventName(g_input.captureEvent))
			                            ("binding", formatBinding(capture.binding()));
			setPlayerBinding(g_input.captureEvent, capture.binding());
			break;
		}
		case BindingCapture::State::Cancelled: {
			gameevent::Event("vr.remap")("event", eventName(g_input.captureEvent))("binding", "cancelled")
			                            ("why", "nothing pressed");
			break;
		}
		default: break;
	}

	if(capture.refused() != refused && capture.refused() != Binding::None) {
		LogInfo << "[vr] input: remapping " << eventName(g_input.captureEvent) << " refused "
		        << controlName(capture.refused()) << ": " << capture.refusedWhy();
	}
}

ControlAction engineAction(GameAction action) {
	switch(action) {
		case GameAction::Jump: return CONTROLS_CUST_JUMP;
		case GameAction::CrouchToggle: return CONTROLS_CUST_CROUCHTOGGLE;
		case GameAction::Inventory: return CONTROLS_CUST_INVENTORY;
		case GameAction::Book: return CONTROLS_CUST_BOOK;
		case GameAction::MagicMode: return CONTROLS_CUST_MAGICMODE;
		case GameAction::Stealth: return CONTROLS_CUST_STEALTHMODE;
		case GameAction::Use: return CONTROLS_CUST_USE;
		case GameAction::Count: break;
	}
	arx_unreachable();
}

//! Hand the snapshot to the engine's input, which applies it in its next update (this frame)
void applyToEngine(const GameInputState & state) {

	if(!GInput) {
		return;
	}

	// Both hands work as the mouse (D-016): the left button clicks, drags and double-clicks
	GInput->setVirtualMouseButton(Mouse::Button_0, state.click);
	// The original's right click: the use action in game, the right button in the menus (in game
	// the right button is free look)
	GInput->setVirtualMouseButton(Mouse::Button_1, state.use && ARXmenu.mode() != Mode_InGame);
	GInput->setVirtualKey(Keyboard::Key_Escape, state.escape);
	for(size_t i = 0; i < GameActionCount; i++) {
		GInput->setVirtualAction(engineAction(GameAction(i)), state.actions[i]);
	}
}

} // anonymous namespace

void updateInput() {

	ControllerInput input;
	readControllers(input);

	// The layout follows the controllers: the one the player saved for them, else their default (#178)
	ControllerType type = typeFromProfiles(controllerProfiles());
	if((type != g_input.type && type != ControllerType::Unknown) || !g_input.loaded) {
		if(type == ControllerType::Unknown) {
			type = g_input.type;
		}
		g_input.loaded = true;
		g_input.type = type;
		std::string why;
		ControlLayout layout = playerLayout(type, why);
		LogInfo << "[vr] input: controllers " << controllerTypeName(type) << " - layout " << why;
		if(g_input.capture.active()) {
			// A capture for the old controllers' layout would land in the new one's file
			cancelCapture();
		}
		applyLayout(layout, why.c_str());
	}

	input.down |= g_input.injectedDown;
	input.taps |= g_input.injectedTaps & ~input.down;
	g_input.injectedTaps.reset();
	for(size_t hand = 0; hand < HandCount; hand++) {
		Stick & stick = input.sticks[hand];
		stick.x = std::clamp(stick.x + g_input.injectedSticks[hand].x, -1.f, 1.f);
		stick.y = std::clamp(stick.y + g_input.injectedSticks[hand].y, -1.f, 1.f);
	}

	g_input.resolver.update(toMsi(platform::getTime()), input.down, input.taps);
	g_input.input = input;

	// A press is a state change made by the player: one line each, bounded by how fast hands are (not while a capture takes the controls)
	for(size_t i = 0; i < EventCount; i++) {
		const Event event = Event(i);
		if(g_input.resolver.pressed(event) && !g_input.capture.active()) {
			LogInfo << "[vr] input: " << eventName(event) << " (" << formatBinding(g_input.resolver.layout()[event]) << ')';
		}
	}

	// Events -> the engine's actions, mouse buttons and keys (#74); nothing while the VR settings
	// capture a binding, so pressing the menu's or the inventory's control only binds it (#178)
	// - also in the update that ends it, whose events were resolved with the old layout
	const GameInputState & resolved = g_input.game.update(g_input.resolver, g_input.input);
	const bool capturing = g_input.capture.active();
	updateCapture(toMsi(platform::getTime()));
	const GameInputState game = capturing || g_input.capture.active() ? GameInputState() : resolved;
	applyToEngine(game);

	// Recenter (#28, D-019: a long press of the left stick click): the head as it is at the next sample
	// becomes the character's eye and facing - horizontal and height, standing or seated (D-018). Any
	// time, menus included: the reference is taken as soon as there is a head to take it from
	if(game.recenter) {
		resetHeadReference("button");
	}
}

const GameInputState & gameInput() {
	return g_input.game.state();
}

const ControlResolver & controls() {
	return g_input.resolver;
}

ControllerType controllerType() {
	return g_input.type;
}

const ControllerInput & controllerInput() {
	return g_input.input;
}

TurnSettings turnSettings() {
	TurnSettings settings;
	settings.mode = config.input.vrSmoothTurn ? TurnMode::Smooth : TurnMode::Snap;
	settings.snapAngle = config.input.vrSnapTurnAngle;
	settings.smoothSpeed = config.input.vrSmoothTurnSpeed;
	settings.snapRepeat = config.input.vrSnapTurnRepeat;
	return settings;
}

float updateTurn(float seconds, bool allowed) {

	const TurnSettings settings = turnSettings();
	const float axis = g_input.game.state().turn;
	const u64 snaps = g_input.turn.snaps();
	const float yaw = g_input.turn.update(settings, axis, seconds, allowed);

	// A snap is a player action: one event each; a smooth turn is an episode: logged when it starts and stops
	if(g_input.turn.snaps() != snaps) {
		gameevent::Event("vr.turn.snap", 1000)("dir", g_input.turn.lastSnap() > 0 ? "right" : "left")
			("deg", settings.snapAngle, 1)("repeat", g_input.turn.lastSnapRepeated() ? "yes" : "no")
			("count", g_input.turn.snaps());
	}
	const bool smoothTurning = settings.mode == TurnMode::Smooth && yaw != 0.f;
	if(smoothTurning != g_input.smoothTurning) {
		gameevent::Event("vr.turn.smooth", 1000)("state", smoothTurning ? "start" : "stop")
			("speed", settings.smoothSpeed, 1)("turnedDeg", g_input.turn.turned(), 1);
		g_input.smoothTurning = smoothTurning;
	}

	return yaw;
}

const TurnController & turnState() {
	return g_input.turn;
}

ControllerType layoutType(ControllerType type) {
	return type == ControllerType::Unknown ? ControllerType::Index : type;
}

std::string layoutFileName(ControllerType type) {
	return std::string("vr-controls-") + controllerTypeName(layoutType(type)) + ".ini";
}

bool layoutRemapped() {
	return g_input.resolver.layout() != defaultLayout(g_input.type);
}

void setPlayerBinding(Event event, const Binding & binding) {
	ControlLayout layout = g_input.resolver.layout();
	layout[event] = binding;
	const std::string why = std::string(eventName(event)) + " = " + formatBinding(binding) + " (VR settings)";
	applyLayout(layout, why.c_str());
	savePlayerLayout(layout);
}

void restoreDefaultLayout() {
	cancelCapture();
	applyLayout(defaultLayout(g_input.type), "reset to the default (VR settings)");
	savePlayerLayout(g_input.resolver.layout());
	gameevent::Event("vr.remap")("event", "all")("binding", "default")
	                            ("controllers", controllerTypeName(layoutType(g_input.type)));
}

void startCapture(Event event) {
	cancelCapture();
	g_input.captureEvent = event;
	g_input.capture.longPressMs = g_input.resolver.longPressMs;
	g_input.capture.releaseDebounceMs = g_input.resolver.releaseDebounceMs;
	g_input.capture.start(toMsi(platform::getTime()), g_input.input.down);
	LogInfo << "[vr] input: remapping " << eventName(event) << " - waiting for a press";
}

void cancelCapture() {
	if(g_input.capture.active()) {
		g_input.capture.cancel();
		LogInfo << "[vr] input: remapping " << eventName(g_input.captureEvent) << " cancelled";
	}
}

const BindingCapture & capture() {
	return g_input.capture;
}

Event captureEvent() {
	return g_input.captureEvent;
}

u32 captureCount() {
	return g_input.captures;
}

namespace {

std::string_view nextWord(std::string_view & text) {
	size_t start = text.find_first_not_of(" \t");
	if(start == std::string_view::npos) {
		text = std::string_view();
		return text;
	}
	text.remove_prefix(start);
	size_t end = text.find_first_of(" \t");
	std::string_view word = text.substr(0, end);
	text = (end == std::string_view::npos) ? std::string_view() : text.substr(end);
	return word;
}

} // anonymous namespace

std::string inputCommand(std::string_view arguments) {

	std::string_view rest = arguments;
	std::string_view verb = nextWord(rest);

	if(verb == "down" || verb == "up" || verb == "tap") {
		std::string_view name = nextWord(rest);
		std::optional<Control> control = parseControl(name);
		if(!control) {
			return "vr-input " + std::string(verb) + ": unknown control \"" + std::string(name)
			       + "\" (e.g. right.a, left.grip, right.stick_down)";
		}
		if(verb == "down") {
			g_input.injectedDown[*control] = true;
		} else if(verb == "up") {
			g_input.injectedDown[*control] = false;
		} else {
			g_input.injectedTaps[*control] = true;
		}
		return std::string();
	}

	if(verb == "bind") {
		std::string_view name = nextWord(rest);
		std::optional<Event> event = parseEvent(name);
		if(!event) {
			return "vr-input bind: unknown event \"" + std::string(name) + "\"";
		}
		std::string error;
		std::optional<Binding> binding = parseBinding(rest, &error);
		if(!binding) {
			return "vr-input bind: " + error;
		}
		ControlLayout layout = g_input.resolver.layout();
		layout[*event] = *binding;
		std::string why = std::string(eventName(*event)) + " = " + formatBinding(*binding) + " (test channel)";
		applyLayout(layout, why.c_str());
		return std::string();
	}

	if(verb == "stick") {
		std::string_view hand = nextWord(rest);
		std::optional<float> x = util::toFloat(nextWord(rest));
		std::optional<float> y = util::toFloat(nextWord(rest));
		if((hand != "left" && hand != "right") || !x || !y) {
			return "vr-input stick: expected left|right <x> <y>, each -1 to 1 (e.g. stick left 0 1)";
		}
		Stick & stick = g_input.injectedSticks[size_t(hand == "left" ? Hand::Left : Hand::Right)];
		stick.x = std::clamp(*x, -1.f, 1.f);
		stick.y = std::clamp(*y, -1.f, 1.f);
		return std::string();
	}

	if(verb == "turn") {
		// The turn settings as cfg.ini sets them (#32); the settings screen is #178
		std::string_view what = nextWord(rest);
		if(std::optional<TurnMode> mode = parseTurnMode(what)) {
			config.input.vrSmoothTurn = (*mode == TurnMode::Smooth);
		} else if(what == "angle" || what == "speed" || what == "repeat") {
			std::optional<float> value = util::toFloat(nextWord(rest));
			if(!value) {
				return "vr-input turn " + std::string(what) + ": expected a number";
			}
			if(what == "angle") {
				config.input.vrSnapTurnAngle = std::clamp(*value, MinSnapAngle, MaxSnapAngle);
			} else if(what == "repeat") {
				config.input.vrSnapTurnRepeat = std::clamp(*value, 0.f, MaxSnapRepeat);
			} else {
				config.input.vrSmoothTurnSpeed = std::clamp(*value, MinSmoothSpeed, MaxSmoothSpeed);
			}
		} else {
			return "vr-input turn: expected snap, smooth, angle <degrees>, speed <degrees per second> or repeat <seconds>, got \""
			       + std::string(what) + "\"";
		}
		const TurnSettings settings = turnSettings();
		LogInfo << "[vr] input: turn " << turnModeName(settings.mode) << ", snap " << settings.snapAngle
		        << " deg repeating every " << settings.snapRepeat << " s, smooth " << settings.smoothSpeed
		        << " deg/s (test channel)";
		return std::string();
	}

	if(verb == "height") {
		// The character height setting as cfg.ini sets it (#260): meters above the character's eye
		std::optional<float> value = util::toFloat(nextWord(rest));
		if(!value) {
			return "vr-input height: expected meters, e.g. 0.1";
		}
		config.input.vrHeightOffset = clampHeightOffset(*value);
		LogInfo << "[vr] input: character height " << config.input.vrHeightOffset << " m (test channel)";
		return std::string();
	}

	if(verb == "capture") {
		std::string_view name = nextWord(rest);
		std::optional<Event> event = parseEvent(name);
		if(!event) {
			return "vr-input capture: unknown event \"" + std::string(name) + "\"";
		}
		startCapture(*event);
		return std::string();
	}

	if(verb == "cancel") {
		cancelCapture();
		return std::string();
	}

	if(verb == "default") {
		restoreDefaultLayout();
		return std::string();
	}

	if(verb == "reset") {
		g_input.injectedDown.reset();
		g_input.injectedTaps.reset();
		g_input.injectedSticks = { };
		applyLayout(defaultLayout(g_input.type), "reset to the default (test channel)");
		return std::string();
	}

	return "vr-input: expected down|up|tap <control>, stick <hand> <x> <y>, bind <event> <binding>, turn <setting>, height <meters>, reset, capture <event>, cancel or default, got \""
	       + std::string(arguments) + "\"";
}

} // namespace vr
