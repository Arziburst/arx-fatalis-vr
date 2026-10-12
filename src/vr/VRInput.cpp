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
#include <cmath>
#include <optional>

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
#include "vr/BodyYaw.h"
#include "vr/HeadTracking.h"
#include "vr/MenuPointer.h"
#include "vr/VRReticle.h"
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
	//! The push of the last snap (#285): when its first snap was made, its repeats, their mean interval
	PlatformInstant pushStart = 0;
	u64 pushRepeats = 0;
	float repeatInterval = 0.f;

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

	//! The menu pointer (#34): the hand on the floating panel is the mouse
	MenuPointer pointer;
	bool panelShown = false;
	std::array<PointerRay, HandCount> rays;
	u64 loggedOffPanelPresses = 0;
	WalkStatus walk;

};

InputState g_input;

struct HapticState {
	HapticMixer mixer;
	bool focused = false;  //!< the controllers were read in the last update
	Hand pointer = Hand::Right;
	HapticStatus status;
};

HapticState g_haptics;

//! The hand that clicked, used or stashed last is the one that holds what it took (D-016)
void updatePointerHand(const ControlResolver & resolver) {
	for(Event event : { Event::InteractLeft, Event::UseLeft, Event::StashLeft }) {
		if(resolver.pressed(event)) {
			g_haptics.pointer = Hand::Left;
		}
	}
	for(Event event : { Event::InteractRight, Event::UseRight, Event::StashRight }) {
		if(resolver.pressed(event)) {
			g_haptics.pointer = Hand::Right;
		}
	}
}

//! Send what the mixer has for this frame - right after the sync, on the frame thread (F-44)
void sendHaptics(bool focused) {

	g_haptics.mixer.strength = config.input.vrHapticStrength;

	if(!focused) {
		if(g_haptics.focused) {
			// The runtime stops the pulses itself; what was queued is stale by the time focus is back
			g_haptics.mixer.clear();
		}
		g_haptics.focused = false;
		return;
	}
	g_haptics.focused = true;

	const auto pulses = g_haptics.mixer.take(double(toMsi(platform::getTime())) / 1000.0);
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(pulses[hand] && vibrate(Hand(hand), *pulses[hand])) {
			g_haptics.status.sent[hand]++;
			g_haptics.status.lastAmplitude[hand] = pulses[hand]->amplitude;
		}
	}
}

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
	if(!readSavedLayout(fs::read(file), type, layout, &error)) {
		// The default layout keeps the game playable; the file stays for the player to look at
		LogWarning << "[vr] input: ignoring the saved layout " << file << ": " << error;
		return layout;
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

	// Both hands work as the mouse (D-016): the left button clicks, drags and double-clicks; on the
	// floating panel the pointing hand's pixel is the mouse, and a press off the panel clicks nothing (#34)
	GInput->setVirtualMouseButton(Mouse::Button_0, state.click && !g_input.pointer.suppressClick());
	if(g_input.pointer.active()) {
		const Vec2f cursor = g_input.pointer.cursor();
		// A world reticle (#37) is the exploration mode's crosshair too: also in the relative mouse mode
		GInput->setVirtualMousePosition(Vec2s(s16(std::lround(cursor.x)), s16(std::lround(cursor.y))),
		                                !g_input.pointer.onPanel());
	} else {
		GInput->setVirtualMousePosition(std::nullopt);
	}
	// The original's right click: the use action in game, the right button in the menus (in game
	// the right button is free look)
	GInput->setVirtualMouseButton(Mouse::Button_1, state.use && ARXmenu.mode() != Mode_InGame);
	GInput->setVirtualKey(Keyboard::Key_Escape, state.escape);
	for(size_t i = 0; i < GameActionCount; i++) {
		GInput->setVirtualAction(engineAction(GameAction(i)), state.actions[i]);
	}
}

/*!
 * The menu pointer (#34): where each hand's aim ray hits the floating panel this frame, which hand
 * owns the cursor (vr/MenuPointer.h) and the beam from it. Game events on the panel appearing and
 * going, on an owner change and on every press - bounded by the player's hands, never per frame.
 */
void updatePointer() {

	const bool shown = panelShown();
	const std::array<HandAim, HandCount> aims = handAims();

	// The hands' hidden rays in the world (#37): their reticles are targets of the same mouse
	updateReticles(aims, double(toUs(platform::getTime())) / 1000000.0);
	const bool world = reticlesOn();

	std::array<Vec3f, HandCount> hits = { };
	std::array<bool, HandCount> trigger = { };
	std::array<bool, HandCount> claims = { };
	for(size_t hand = 0; hand < HandCount; hand++) {
		PointerRay & ray = g_input.rays[hand];
		ray = PointerRay();
		if(shown && aims[hand].valid) {
			ray.onPanel = panelHit(aims[hand].origin, aims[hand].direction, ray.pixel, &hits[hand]);
		}
		if(!ray.onPanel) {
			reticleTarget(Hand(hand), ray);
		}
		claims[hand] = reticleClaims(Hand(hand));
	}
	trigger[size_t(Hand::Left)] = g_input.resolver.active(Event::InteractLeft) || g_input.resolver.active(Event::StashLeft);
	trigger[size_t(Hand::Right)] = g_input.resolver.active(Event::InteractRight) || g_input.resolver.active(Event::StashRight);
	// The grip's use acts under its own hand (D-019): the hand that uses takes an idle cursor
	claims[size_t(Hand::Left)] = claims[size_t(Hand::Left)] || g_input.resolver.pressed(Event::UseLeft);
	claims[size_t(Hand::Right)] = claims[size_t(Hand::Right)] || g_input.resolver.pressed(Event::UseRight);

	MenuPointer & pointer = g_input.pointer;
	pointer.update(shown || world, g_input.rays, trigger, PointerParams(), claims);
	afterPointer(pointer, g_input.rays);

	const size_t owner = size_t(pointer.owner());
	if(pointer.onPanel()) {
		setPointerBeam(true, aims[owner].origin, hits[owner]);
	} else {
		setPointerBeam(false);
	}

	if(shown != g_input.panelShown) {
		g_input.panelShown = shown;
		gameevent::Event("vr.pointer")("state", shown ? "on" : "off")("hand", handName(pointer.owner()))
			("tracked", int(aims[0].valid) + int(aims[1].valid));
	}
	if(!shown && !world) {
		return;
	}
	if(pointer.ownerChanged()) {
		gameevent::Event("vr.pointer.owner")("hand", handName(pointer.owner()));
	}
	for(size_t hand = 0; hand < HandCount; hand++) {
		const bool pressed = trigger[hand] && pointer.pressedOnPanel(Hand(hand));
		if(pressed) {
			gameevent::Event("vr.pointer.click")("hand", handName(Hand(hand)))
				("x", g_input.rays[hand].pixel.x, 0)("y", g_input.rays[hand].pixel.y, 0);
		}
	}
	if(pointer.offPanelPresses() != g_input.loggedOffPanelPresses) {
		g_input.loggedOffPanelPresses = pointer.offPanelPresses();
		gameevent::Event("vr.pointer.muted")("hand", handName(pointer.owner()))
			("reason", world ? "no target" : "off the panel");
	}
}

} // anonymous namespace

void updateInput() {

	ControllerInput input;
	const bool focused = readControllers(input);
	sendHaptics(focused);

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
	updatePointerHand(g_input.resolver);

	// A press is a state change made by the player: one line each, bounded by how fast hands are (not while a capture takes the controls)
	for(size_t i = 0; i < EventCount; i++) {
		const Event event = Event(i);
		if(g_input.resolver.pressed(event) && !g_input.capture.active()) {
			LogInfo << "[vr] input: " << eventName(event) << " (" << formatBinding(g_input.resolver.layout()[event]) << ')';
		}
	}

	// The hands on the floating panel (#34), before the events reach the engine: a press moves the
	// cursor under the pressing hand in the same update, so the click lands where it points
	updatePointer();

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

PointerStatus pointerStatus() {
	const MenuPointer & pointer = g_input.pointer;
	PointerStatus status;
	status.shown = g_input.panelShown;
	status.active = pointer.active();
	status.owner = handName(pointer.owner());
	status.cursor = pointer.cursor();
	status.beam = pointerBeamShown();
	status.onPanel = { g_input.rays[0].onPanel, g_input.rays[1].onPanel };
	status.onWorld = { g_input.rays[0].onWorld, g_input.rays[1].onWorld };
	status.clicks = pointer.clicks();
	status.worldClicks = pointer.worldClicks();
	status.offPanelPresses = pointer.offPanelPresses();
	status.ownerChanges = pointer.ownerChanges();
	return status;
}

const GameInputState & gameInput() {
	return g_input.game.state();
}

const WalkStatus & walkStatus() {
	return g_input.walk;
}

void reportWalk(const StickWalk & walk, float yaw, float heading) {

	WalkStatus & status = g_input.walk;
	yaw -= 360.f * std::floor(yaw / 360.f);
	const bool walking = walk.moving();
	if(walking != status.walking) {
		if(walking) {
			status.walks++;
		}
		// An episode, not a frame: the stick left its dead zone, or came back to rest
		gameevent::Event("vr.walk", 200)("state", walking ? "start" : "end")("yaw", yaw, 1)
			("heading", heading, 1)("look", wrapSignedDegrees(yaw - heading), 1)
			("pace", walk.sneak ? "sneak" : "run");
	} else if(walking && walk.sneak != status.sneak) {
		// The deflection crossed a threshold of the pace (#33): one line per switch, not per frame
		status.paceChanges++;
		gameevent::Event("vr.walk", 200)("state", "pace")("pace", walk.sneak ? "sneak" : "run");
	}
	status.sneak = walking && walk.sneak;

	status.walking = walking;
	status.yaw = yaw;
	status.heading = heading;
	status.direction = walk.direction;
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

void haptic(HapticEvent event, float intensity) {
	haptic(event, hapticHand(event), intensity);
}

void haptic(HapticEvent event, HapticHand hand, float intensity) {

	HapticStatus & status = g_haptics.status;
	status.requests[size_t(event)]++;

	const HapticPulse pulse = hapticPulse(event, intensity);
	std::array<bool, HandCount> hands = { };
	switch(hand) {
		case HapticHand::Left: hands[size_t(Hand::Left)] = true; break;
		case HapticHand::Right: hands[size_t(Hand::Right)] = true; break;
		case HapticHand::Both: hands = { true, true }; break;
		case HapticHand::Pointer: hands[size_t(g_haptics.pointer)] = true; break;
		// The left-handed setting (#178) will move the weapon to the left hand
		case HapticHand::Weapon: hands[size_t(Hand::Right)] = true; break;
	}

	const char * outcome = "queued";
	if(!isEnabled()) {
		outcome = "flat";
	} else if(!g_haptics.focused) {
		outcome = "not-focused";
	} else {
		g_haptics.mixer.strength = config.input.vrHapticStrength;
		for(size_t i = 0; i < HandCount; i++) {
			if(hands[i] && !g_haptics.mixer.request(Hand(i), pulse)) {
				outcome = "off";
			}
		}
	}
	if(std::string_view(outcome) != "queued") {
		status.refused++;
	}

	// Bounded by the callers: hits taken at most every 500 ms, one per swing, one per click
	gameevent::Event("haptic.request")("event", hapticEventName(event))
		("hand", hands[0] && hands[1] ? "both" : handName(hands[0] ? Hand::Left : Hand::Right))
		("amplitude", pulse.amplitude * g_haptics.mixer.strength, 2)("ms", u32(pulse.seconds * 1000.f + 0.5f))
		("outcome", outcome);
}

HapticStatus hapticStatus() {
	HapticStatus status = g_haptics.status;
	status.strength = config.input.vrHapticStrength;
	status.masked = g_haptics.mixer.masked();
	status.merged = g_haptics.mixer.merged();
	status.pointer = g_haptics.pointer;
	return status;
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
	// A capture of the VR settings takes the controls: the turn stick does not turn meanwhile (#178)
	const float yaw = g_input.turn.update(settings, axis, seconds, allowed && !g_input.capture.active());

	// A snap is a player action: one event each; a smooth turn is an episode: logged when it starts and stops
	if(g_input.turn.snaps() != snaps) {
		// The repeat measured by the clock: a test reads it instead of a count of snaps, which depends on when
		// the stick is let go (#285)
		const PlatformInstant now = platform::getTime();
		if(g_input.turn.lastSnapRepeated()) {
			g_input.pushRepeats++;
			g_input.repeatInterval = toS(now - g_input.pushStart) / float(g_input.pushRepeats);
		} else {
			g_input.pushStart = now;
			g_input.pushRepeats = 0;
			g_input.repeatInterval = 0.f;
		}
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

u64 snapPushRepeats() {
	return g_input.pushRepeats;
}

float snapRepeatInterval() {
	return g_input.repeatInterval;
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

	if(verb == "haptic") {
		std::string_view name = nextWord(rest);
		std::optional<HapticEvent> event = parseHapticEvent(name);
		if(!event) {
			return "vr-input haptic: unknown event \"" + std::string(name) + "\" (e.g. hit_taken, weapon_hit, item_grabbed)";
		}
		std::string_view hand = nextWord(rest);
		std::string_view intensity = nextWord(rest);
		std::optional<float> value = intensity.empty() ? std::optional<float>(0.f) : util::toFloat(intensity);
		if(!value) {
			return "vr-input haptic: the intensity must be a number from 0 to 1";
		}
		if(hand.empty() || hand == "table") {
			haptic(*event, *value);
		} else if(hand == "left" || hand == "right" || hand == "both") {
			haptic(*event, hand == "left" ? HapticHand::Left : hand == "right" ? HapticHand::Right : HapticHand::Both, *value);
		} else {
			return "vr-input haptic: expected <event> [left|right|both|table] [intensity]";
		}
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

	if(verb == "reload") {
		// Read the saved layout again at the next update, as when the controllers are detected at a start
		cancelCapture();
		g_input.loaded = false;
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

	if(verb == "reset") {
		g_input.injectedDown.reset();
		g_input.injectedTaps.reset();
		g_input.injectedSticks = { };
		applyLayout(defaultLayout(g_input.type), "reset to the default (test channel)");
		return std::string();
	}

	return "vr-input: expected down|up|tap <control>, stick <hand> <x> <y>, bind <event> <binding>, haptic <event> [hand],"
	       " turn <setting>, reset, capture <event>, cancel, default or reload, got \"" + std::string(arguments) + "\"";
}

} // namespace vr
