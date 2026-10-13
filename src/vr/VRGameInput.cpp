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

#include "vr/VRGameInput.h"

#include <algorithm>
#include <cmath>

namespace vr {

const char * gameActionName(GameAction action) {
	switch(action) {
		case GameAction::Jump: return "jump";
		case GameAction::CrouchToggle: return "crouch_toggle";
		case GameAction::Inventory: return "inventory";
		case GameAction::Book: return "book";
		case GameAction::MagicMode: return "magic_mode";
		case GameAction::Stealth: return "stealth";
		case GameAction::Use: return "use";
		case GameAction::QuickSave: return "quick_save";
		case GameAction::QuickLoad: return "quick_load";
		case GameAction::CancelSpell: return "cancel_spell";
		case GameAction::Count: break;
	}
	return "?";
}

Stick applyDeadZone(Stick stick, float deadZone) {

	// Radial: the direction is kept, only the length is rescaled
	float length = std::sqrt(stick.x * stick.x + stick.y * stick.y);
	if(length <= deadZone || deadZone >= 1.f) {
		return Stick();
	}

	float scaled = std::min((length - deadZone) / (1.f - deadZone), 1.f);
	Stick result;
	result.x = stick.x / length * scaled;
	result.y = stick.y / length * scaled;
	return result;
}

std::string formatGameInput(const GameInputState & state) {
	std::string result;
	auto add = [&result](const char * word) {
		if(!result.empty()) {
			result += ' ';
		}
		result += word;
	};
	if(state.click) {
		add("click");
	}
	for(size_t i = 0; i < GameActionCount; i++) {
		if(state.actions[i]) {
			add(gameActionName(GameAction(i)));
		}
	}
	if(state.escape) {
		add("escape");
	}
	if(state.recenter) {
		add("recenter");
	}
	return result;
}

const GameInputState & GameInputMapper::update(const ControlResolver & controls, const ControllerInput & input) {

	GameInputState state;

	const bool stash = controls.active(Event::StashLeft) || controls.active(Event::StashRight);

	state.click = controls.active(Event::InteractLeft) || controls.active(Event::InteractRight) || stash;
	state.use = controls.active(Event::UseLeft) || controls.active(Event::UseRight);
	// The game acts on Escape's release, which the engine only sees for a key it saw held for two
	// updates: a press that started and ended in one update stays one more
	state.escape = controls.active(Event::Menu) || m_escapeUpdates == 1;
	m_escapeUpdates = state.escape ? m_escapeUpdates + 1 : 0;
	state.recenter = controls.pressed(Event::Recenter);

	state.actions[size_t(GameAction::Jump)] = controls.active(Event::Jump);
	state.actions[size_t(GameAction::CrouchToggle)] = controls.active(Event::CrouchToggle);
	state.actions[size_t(GameAction::Inventory)] = controls.active(Event::Inventory);
	state.actions[size_t(GameAction::Book)] = controls.active(Event::Book);
	state.actions[size_t(GameAction::MagicMode)] = controls.active(Event::MagicMode);
	// The game stashes when the button goes up with stealth held: keep stealth for that update
	state.actions[size_t(GameAction::Stealth)] = stash || m_stashed;
	state.actions[size_t(GameAction::Use)] = state.use;
	m_stashed = stash;

	// D-019: walk with the left stick, turn with the right one (left-handed mode swaps them: #178)
	state.move = applyDeadZone(input.sticks[size_t(Hand::Left)], moveDeadZone);
	const Stick & turnStick = input.sticks[size_t(Hand::Right)];
	if(std::abs(turnStick.x) >= turnDominance * std::abs(turnStick.y)) {
		state.turn = applyDeadZone(Stick{ turnStick.x, 0.f }, turnDeadZone).x;
	}

	m_state = state;
	return m_state;
}

} // namespace vr
