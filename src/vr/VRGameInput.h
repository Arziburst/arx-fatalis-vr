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

#ifndef ARX_VR_VRGAMEINPUT_H
#define ARX_VR_VRGAMEINPUT_H

#include <bitset>
#include <string>

#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief Gameplay events -> what the engine sees: actions, mouse buttons, keys, analog axes (#74, F-42)
 *
 * The control table (VRControls.h) turns the controllers into gameplay events; this file turns those
 * events and the sticks into one snapshot per frame in the engine's own terms, which VRInput.cpp feeds
 * into the engine's input (OR-ed with the keyboard and the mouse):
 *  - click / drag with either hand is the mouse's left button - both hands work as the mouse (D-016);
 *  - use (the grip opened without a trigger) is the original's right click;
 *  - stash (grip + trigger) is the original's stealth-click: the left button with the stealth action
 *    held, and the stealth action stays one more update after the button goes up, because the game
 *    stashes on the button's release;
 *  - jump, crouch toggle, inventory, book and magic mode are the engine's actions of those names;
 *  - the game menu is the Escape key, held for at least two updates (the game acts on its release);
 *  - recenter, the move vector and the turn axis are only reported: #28, #31 and #32 use them.
 *
 * Pure logic - no OpenXR, no engine globals - covered by tests/vr/VRGameInputTest.
 */
namespace vr {

//! The engine actions the controllers drive (the engine's CONTROLS_CUST_* of the same name)
enum class GameAction : u8 {
	Jump,
	CrouchToggle,
	Inventory,
	Book,
	MagicMode,
	Stealth, //!< held for a stash: the original's stealth-click
	Use,     //!< the original's right click on an item - the engine's "use" action in game
	Count
};

const size_t GameActionCount = size_t(GameAction::Count);

//! "jump", "crouch_toggle", ... - for the log and status.json
const char * gameActionName(GameAction action);

//! A thumbstick past its dead zone: the dead zone maps to 0, full deflection to 1, never longer than 1
Stick applyDeadZone(Stick stick, float deadZone);

//! What the engine sees from the controllers in one frame
struct GameInputState {

	std::bitset<GameActionCount> actions; //!< held now
	bool click = false;      //!< the mouse's left button: click / drag or stash with either hand
	bool use = false;        //!< the original's right click
	bool escape = false;     //!< the game menu key
	bool recenter = false;   //!< recenter requested in this update (#28)
	Stick move;              //!< walk: the left stick past the dead zone, x right, y forward (#31)
	float turn = 0.f;        //!< turn: the right stick's x past the dead zone, right positive (#32)

	bool action(GameAction a) const { return actions[size_t(a)]; }

	bool operator==(const GameInputState & other) const {
		return actions == other.actions && click == other.click && use == other.use && escape == other.escape
		       && recenter == other.recenter && move.x == other.move.x && move.y == other.move.y
		       && turn == other.turn;
	}
	bool operator!=(const GameInputState & other) const { return !(*this == other); }

};

//! The held actions and buttons of a state as text, e.g. "click stealth" - for status.json and the log
std::string formatGameInput(const GameInputState & state);

//! Turns the resolved events and the sticks into \ref GameInputState, once per frame
class GameInputMapper {

public:

	//! Stick dead zones: a resting stick drifts, the Index stick by up to ~0.1
	float moveDeadZone = 0.2f;
	float turnDeadZone = 0.2f;
	/*!
	 * The turn stick turns only while its x is at least this share of its y (F-121 axis dominance): a push
	 * down for the crouch toggle (#33) that drifts sideways does not turn as well
	 */
	float turnDominance = 0.6f;

	const GameInputState & update(const ControlResolver & controls, const ControllerInput & input);

	const GameInputState & state() const { return m_state; }

private:

	GameInputState m_state;
	bool m_stashed = false;    //!< a stash was on in the last update
	u32 m_escapeUpdates = 0;   //!< for how many updates in a row the game menu key is held

};

} // namespace vr

#endif // ARX_VR_VRGAMEINPUT_H
