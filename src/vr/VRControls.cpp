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

#include "vr/VRControls.h"

#include <cmath>

namespace vr {

namespace {

const char * const HandNames[HandCount] = { "left", "right" };

const char * const ControlKindNames[ControlKindCount] = {
	"trigger",
	"grip",
	"a",
	"b",
	"stick_click",
	"stick_up",
	"stick_down",
	"stick_left",
	"stick_right",
	"trackpad",
	"menu",
};

struct EventInfo {
	const char * name;
	const char * label;
};

const EventInfo Events[EventCount] = {
	{ "interact_left", "Click / drag (left hand)" },
	{ "interact_right", "Click / drag (right hand)" },
	{ "use_left", "Use item (left hand)" },
	{ "use_right", "Use item (right hand)" },
	{ "stash_left", "Straight into the inventory (left hand)" },
	{ "stash_right", "Straight into the inventory (right hand)" },
	{ "jump", "Jump" },
	{ "crouch_toggle", "Crouch (toggle)" },
	{ "inventory", "Inventory" },
	{ "book", "Character book" },
	{ "menu", "Game menu" },
	{ "recenter", "Recenter" },
	{ "magic_mode", "Magic: draw runes" },
};

const std::string_view LongSuffix = ":long";

std::string_view trim(std::string_view text) {
	while(!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
		text.remove_prefix(1);
	}
	while(!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
		text.remove_suffix(1);
	}
	return text;
}

bool fail(std::string * error, std::string message) {
	if(error) {
		*error = std::move(message);
	}
	return false;
}

} // anonymous namespace

const char * handName(Hand hand) {
	return HandNames[size_t(hand)];
}

std::string controlName(Control control) {
	if(control >= ControlCount) {
		return "none";
	}
	return std::string(handName(controlHand(control))) + '.' + ControlKindNames[size_t(controlKind(control))];
}

std::optional<Control> parseControl(std::string_view name) {
	for(size_t hand = 0; hand < HandCount; hand++) {
		std::string_view prefix = HandNames[hand];
		if(name.size() <= prefix.size() + 1 || name.substr(0, prefix.size()) != prefix || name[prefix.size()] != '.') {
			continue;
		}
		std::string_view kind = name.substr(prefix.size() + 1);
		for(size_t i = 0; i < ControlKindCount; i++) {
			if(kind == ControlKindNames[i]) {
				return control(Hand(hand), ControlKind(i));
			}
		}
	}
	return std::nullopt;
}

const char * eventName(Event event) {
	return Events[size_t(event)].name;
}

const char * eventLabel(Event event) {
	return Events[size_t(event)].label;
}

std::optional<Event> parseEvent(std::string_view name) {
	for(size_t i = 0; i < EventCount; i++) {
		if(name == Events[i].name) {
			return Event(i);
		}
	}
	return std::nullopt;
}

std::string formatBinding(const Binding & binding) {
	if(!binding.isBound()) {
		return "none";
	}
	std::string result = controlName(binding.first);
	if(binding.isChord()) {
		result += '+';
		result += controlName(binding.second);
	}
	if(binding.longPress) {
		result += LongSuffix;
	}
	return result;
}

std::optional<Binding> parseBinding(std::string_view text, std::string * error) {

	text = trim(text);
	if(text == "none") {
		return Binding();
	}

	Binding binding;
	if(text.size() > LongSuffix.size() && text.substr(text.size() - LongSuffix.size()) == LongSuffix) {
		binding.longPress = true;
		text.remove_suffix(LongSuffix.size());
	} else if(text.find(':') != std::string_view::npos) {
		fail(error, "unknown suffix in \"" + std::string(text) + "\" (only \":long\")");
		return std::nullopt;
	}

	std::string_view firstName = text;
	std::string_view secondName;
	if(size_t plus = text.find('+'); plus != std::string_view::npos) {
		firstName = trim(text.substr(0, plus));
		secondName = trim(text.substr(plus + 1));
	}

	std::optional<Control> first = parseControl(firstName);
	if(!first) {
		fail(error, "unknown control \"" + std::string(firstName) + "\"");
		return std::nullopt;
	}
	binding.first = *first;

	if(!secondName.empty() || text.find('+') != std::string_view::npos) {
		std::optional<Control> second = parseControl(secondName);
		if(!second) {
			fail(error, "unknown control \"" + std::string(secondName) + "\"");
			return std::nullopt;
		}
		if(*second == *first) {
			fail(error, "a chord of " + controlName(*first) + " with itself");
			return std::nullopt;
		}
		if(controlHand(*second) != controlHand(*first)) {
			fail(error, "a chord across hands (" + controlName(*first) + " + " + controlName(*second)
			            + "): both controls must be on one hand");
			return std::nullopt;
		}
		binding.second = *second;
	}

	return binding;
}

const char * controllerTypeName(ControllerType type) {
	switch(type) {
		case ControllerType::Unknown: return "unknown";
		case ControllerType::Index:   return "index";
		case ControllerType::Touch:   return "touch";
		case ControllerType::Simple:  return "simple";
	}
	return "unknown";
}

ControllerType controllerTypeFromProfile(std::string_view profile) {
	if(profile == "/interaction_profiles/valve/index_controller") {
		return ControllerType::Index;
	}
	if(profile == "/interaction_profiles/oculus/touch_controller") {
		return ControllerType::Touch;
	}
	if(profile == "/interaction_profiles/khr/simple_controller") {
		return ControllerType::Simple;
	}
	return ControllerType::Unknown;
}

ControlLayout defaultLayout(ControllerType type) {

	ControlLayout layout;

	const Control leftTrigger = control(Hand::Left, ControlKind::Trigger);
	const Control rightTrigger = control(Hand::Right, ControlKind::Trigger);
	layout[Event::InteractLeft] = Binding::single(leftTrigger);
	layout[Event::InteractRight] = Binding::single(rightTrigger);

	if(type == ControllerType::Simple) {
		// Select (bound to the trigger action) and menu are all a simple controller has
		layout[Event::Menu] = Binding::single(control(Hand::Left, ControlKind::Menu));
		return layout;
	}

	// The D-019 table: positions are the same on the Index and the Touch (A/B right, X/Y left)
	const Control leftGrip = control(Hand::Left, ControlKind::Grip);
	const Control rightGrip = control(Hand::Right, ControlKind::Grip);
	layout[Event::UseLeft] = Binding::single(leftGrip);
	layout[Event::UseRight] = Binding::single(rightGrip);
	layout[Event::StashLeft] = Binding::chord(leftGrip, leftTrigger);
	layout[Event::StashRight] = Binding::chord(rightGrip, rightTrigger);
	layout[Event::Jump] = Binding::single(control(Hand::Right, ControlKind::A));
	layout[Event::CrouchToggle] = Binding::single(control(Hand::Right, ControlKind::StickDown));
	layout[Event::Inventory] = Binding::single(control(Hand::Right, ControlKind::B));
	layout[Event::Book] = Binding::single(control(Hand::Left, ControlKind::B));
	// The Touch has a menu button on the left; the Index does not, its left A is free in D-019
	layout[Event::Menu] = Binding::single(control(Hand::Left, type == ControllerType::Touch ? ControlKind::Menu
	                                                                                         : ControlKind::A));
	layout[Event::Recenter] = Binding::hold(control(Hand::Left, ControlKind::StickClick));
	// Magic mode: decided with the magic tasks (#43), unbound until then

	return layout;
}

std::string formatLayout(const ControlLayout & layout) {
	std::string result;
	for(size_t i = 0; i < EventCount; i++) {
		result += eventName(Event(i));
		result += " = ";
		result += formatBinding(layout.bindings[i]);
		result += '\n';
	}
	return result;
}

bool parseLayout(std::string_view text, ControlLayout & layout, std::string * error) {

	ControlLayout result = layout;

	size_t lineNumber = 0;
	while(!text.empty()) {

		size_t end = text.find('\n');
		std::string_view line = trim(text.substr(0, end));
		text = (end == std::string_view::npos) ? std::string_view() : text.substr(end + 1);
		lineNumber++;

		if(line.empty() || line.front() == '#') {
			continue;
		}

		const std::string where = "line " + std::to_string(lineNumber) + ": ";

		size_t equals = line.find('=');
		if(equals == std::string_view::npos) {
			return fail(error, where + "expected \"event = binding\", got \"" + std::string(line) + "\"");
		}

		std::string_view name = trim(line.substr(0, equals));
		std::optional<Event> event = parseEvent(name);
		if(!event) {
			return fail(error, where + "unknown event \"" + std::string(name) + "\"");
		}

		std::string reason;
		std::optional<Binding> binding = parseBinding(line.substr(equals + 1), &reason);
		if(!binding) {
			return fail(error, where + reason);
		}

		result[*event] = *binding;
	}

	layout = result;
	return true;
}

std::vector<Conflict> findConflicts(const ControlLayout & layout) {
	std::vector<Conflict> conflicts;
	for(size_t i = 0; i < EventCount; i++) {
		if(!layout.bindings[i].isBound()) {
			continue;
		}
		for(size_t j = i + 1; j < EventCount; j++) {
			if(layout.bindings[j] == layout.bindings[i]) {
				conflicts.push_back({ Event(i), Event(j) });
			}
		}
	}
	return conflicts;
}

const Hysteresis & gripThreshold(ControllerType type) {
	return type == ControllerType::Index ? IndexGripThreshold : GripThreshold;
}

void updateStickDirections(ControlSet & down, Hand hand, float x, float y) {

	const Control up = control(hand, ControlKind::StickUp);
	const Control below = control(hand, ControlKind::StickDown);
	const Control left = control(hand, ControlKind::StickLeft);
	const Control right = control(hand, ControlKind::StickRight);

	// How far the stick is pushed towards each direction
	const struct { Control control; float value; } directions[] = {
		{ up, y }, { below, -y }, { left, -x }, { right, x },
	};

	// A held direction stays held until it falls below the release threshold
	for(const auto & direction : directions) {
		if(down[direction.control]) {
			if(StickDirectionThreshold.update(true, direction.value)) {
				return;
			}
			down[direction.control] = false;
		}
	}

	const bool horizontal = std::abs(x) >= std::abs(y);
	for(const auto & direction : directions) {
		const bool alongX = direction.control == left || direction.control == right;
		if(alongX == horizontal && StickDirectionThreshold.update(false, direction.value)) {
			down[direction.control] = true;
			return;
		}
	}
}

ControlResolver::ControlResolver(const ControlLayout & layout) {
	setLayout(layout);
}

void ControlResolver::setLayout(const ControlLayout & layout) {

	m_layout = layout;

	m_deferred.reset();
	m_hasLongPress.reset();
	for(const Binding & binding : m_layout.bindings) {
		if(binding.isBound() && (binding.isChord() || binding.longPress)) {
			m_deferred[binding.first] = true;
		}
		if(binding.isBound() && !binding.isChord() && binding.longPress) {
			m_hasLongPress[binding.first] = true;
		}
	}

	for(EventState & event : m_events) {
		if(event.active) {
			event.pulse = true; // reported as released by the next update
		}
		event.pending = -1;
	}

	for(size_t i = 0; i < ControlCount; i++) {
		if(m_down[i]) {
			m_controls[i].consumed = true;
		}
	}
}

void ControlResolver::activate(Event event) {
	EventState & state = m_events[size_t(event)];
	if(!state.active) {
		state.active = true;
		state.pressed = true;
		state.pulse = false;
		state.presses++;
	}
}

void ControlResolver::deactivate(Event event) {
	EventState & state = m_events[size_t(event)];
	if(!state.active) {
		return;
	}
	if(state.pressed) {
		// Came on in this update: stays on until the next one, so a per-frame poll sees it
		state.pulse = true;
		return;
	}
	state.active = false;
	state.released = true;
}

void ControlResolver::press(Control control, s64 timeMs) {

	m_down[control] = true;
	m_controls[control].since = timeMs;
	m_controls[control].consumed = false;

	// A chord: this control is pressed while the one leading it is held
	bool chord = false;
	for(size_t i = 0; i < EventCount; i++) {
		const Binding & binding = m_layout.bindings[i];
		if(binding.isChord() && binding.second == control && m_down[binding.first]) {
			chord = true;
			m_controls[binding.first].consumed = true;
			m_controls[control].consumed = true;
			if(binding.longPress) {
				m_events[i].pending = timeMs;
			} else {
				activate(Event(i));
			}
		}
	}
	if(chord || m_deferred[control]) {
		return;
	}

	for(size_t i = 0; i < EventCount; i++) {
		const Binding & binding = m_layout.bindings[i];
		if(binding.first == control && !binding.isChord() && !binding.longPress) {
			activate(Event(i));
		}
	}
}

void ControlResolver::holdLong(s64 timeMs) {

	for(size_t i = 0; i < EventCount; i++) {

		const Binding & binding = m_layout.bindings[i];
		if(!binding.isBound() || !binding.longPress) {
			continue;
		}

		EventState & event = m_events[i];
		if(binding.isChord()) {
			if(event.pending >= 0 && timeMs - event.pending >= longPressMs) {
				event.pending = -1;
				activate(Event(i));
			}
			continue;
		}

		ControlState & state = m_controls[binding.first];
		if(m_down[binding.first] && state.upSince < 0 && !state.consumed && timeMs - state.since >= longPressMs) {
			state.consumed = true;
			activate(Event(i));
		}
	}
}

void ControlResolver::release(Control control, s64 timeMs) {

	for(size_t i = 0; i < EventCount; i++) {
		const Binding & binding = m_layout.bindings[i];
		if(binding.isBound() && (binding.first == control || binding.second == control)) {
			m_events[i].pending = -1;
			deactivate(Event(i));
		}
	}

	// A tap of a control whose single binding had to wait for the release
	const ControlState & state = m_controls[control];
	// A control with a long press of its own taps only when let go before it; a chord's leader taps
	// after any hold the chord did not use (D-019: the hand opens without a trigger = use)
	if(m_deferred[control] && !state.consumed
	   && (!m_hasLongPress[control] || timeMs - state.since < longPressMs)) {
		for(size_t i = 0; i < EventCount; i++) {
			const Binding & binding = m_layout.bindings[i];
			if(binding.first == control && !binding.isChord() && !binding.longPress) {
				activate(Event(i));
				deactivate(Event(i));
			}
		}
	}

	m_down[control] = false;
	m_controls[control] = ControlState();
}

void ControlResolver::update(s64 timeMs, const ControlSet & down, const ControlSet & taps) {

	for(size_t i = 0; i < EventCount; i++) {
		EventState & event = m_events[i];
		event.pressed = false;
		event.released = false;
		if(event.pulse) {
			event.pulse = false;
			deactivate(Event(i));
		}
	}

	// A deferred control seen up is still held until it stayed up for the debounce window; seen down
	// again inside the window, it is the same hold (the press time and a chord it made are kept)
	ControlSet held = down;
	for(size_t i = 0; i < ControlCount; i++) {
		ControlState & state = m_controls[i];
		if(down[i]) {
			state.upSince = -1;
		} else if(m_down[i] && m_deferred[i]) {
			// A tap inside the window was the control down again: the window starts over
			if(state.upSince < 0 || taps[i]) {
				state.upSince = timeMs;
			}
			held[i] = (timeMs - state.upSince < releaseDebounceMs);
		}
	}

	const ControlSet tapped = taps & ~held & ~m_down;
	const ControlSet pressed = (held & ~m_down) | tapped;
	const ControlSet released = (m_down & ~held) | (tapped & ~m_deferred);

	// Presses in the same update count as simultaneous: the controls that lead chords go first
	for(size_t i = 0; i < ControlCount; i++) {
		if(pressed[i] && m_deferred[i]) {
			press(Control(i), timeMs);
		}
	}
	for(size_t i = 0; i < ControlCount; i++) {
		if(pressed[i] && !m_deferred[i]) {
			press(Control(i), timeMs);
		}
	}

	holdLong(timeMs);

	for(size_t i = 0; i < ControlCount; i++) {
		if(released[i]) {
			// A deferred control was really released when it was first seen up
			const s64 upSince = m_controls[i].upSince;
			release(Control(i), upSince >= 0 ? upSince : timeMs);
		} else if(tapped[i] && m_deferred[i]) {
			// Pressed and released between two updates: its release waits for the window like any other
			m_controls[i].upSince = timeMs;
		}
	}
}

} // namespace vr
