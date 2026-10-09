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
#include "io/log/Logger.h"
#include "platform/Time.h"
#include "util/Number.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

struct InputState {

	ControllerType type = ControllerType::Unknown;
	ControlResolver resolver = ControlResolver(defaultLayout(ControllerType::Unknown));
	ControllerInput input;
	GameInputMapper game;

	//! Controls set by the test channel, merged into what the controllers report
	ControlSet injectedDown;
	ControlSet injectedTaps;
	//! Stick deflections set by the test channel, added to the controllers' (directions: down / up)
	std::array<Stick, HandCount> injectedSticks;

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
	const bool focused = readControllers(input);
	sendHaptics(focused);

	// The layout follows the controllers (saved layouts per type: #178)
	ControllerType type = typeFromProfiles(controllerProfiles());
	if(type != g_input.type && type != ControllerType::Unknown) {
		LogInfo << "[vr] input: controllers " << controllerTypeName(type) << " - default layout for them";
		g_input.type = type;
		applyLayout(defaultLayout(type), controllerTypeName(type));
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

	// A press is a state change made by the player: one line each, bounded by how fast hands are
	for(size_t i = 0; i < EventCount; i++) {
		const Event event = Event(i);
		if(g_input.resolver.pressed(event)) {
			LogInfo << "[vr] input: " << eventName(event) << " (" << formatBinding(g_input.resolver.layout()[event]) << ')';
		}
	}

	// Events -> the engine's actions, mouse buttons and keys (#74)
	applyToEngine(g_input.game.update(g_input.resolver, g_input.input));
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

	if(verb == "reset") {
		g_input.injectedDown.reset();
		g_input.injectedTaps.reset();
		g_input.injectedSticks = { };
		applyLayout(defaultLayout(g_input.type), "reset to the default (test channel)");
		return std::string();
	}

	return "vr-input: expected down|up|tap <control>, stick <hand> <x> <y>, bind <event> <binding>, haptic <event> [hand] or reset, got \""
	       + std::string(arguments) + "\"";
}

} // namespace vr
