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

#ifndef ARX_VR_VRINPUT_H
#define ARX_VR_VRINPUT_H

#include <array>
#include <string>
#include <string_view>

#include "math/Types.h"
#include "vr/VRControls.h"
#include "vr/VRGameInput.h"
#include "vr/VRTurn.h"

/*!
 * \brief The controllers as gameplay events (#30, D-020)
 *
 * Every frame the physical controls are read from OpenXR (\ref readControllers) and the game's
 * control table turns them into gameplay events (\ref ControlResolver). The layout follows the
 * controllers: when the interaction profile changes, the default layout for that controller type
 * is applied (saved layouts per type come with the settings screen, #178).
 *
 * The events then become what the engine sees (\ref GameInputMapper, #74): its actions, the mouse's
 * buttons and the Escape key, OR-ed with the keyboard and the mouse; the analog axes are kept for
 * locomotion and turning.
 *
 * The test channel drives the same table without a headset (\c vr-input, docs/vr/TESTING.md).
 */
namespace vr {

/*!
 * Read the controllers, resolve the gameplay events and hand them to the engine's input - once per
 * frame, after \ref pollEvents and before the engine's input update
 */
void updateInput();

//! The gameplay events of the last update
const ControlResolver & controls();

//! What the engine was given in the last update: actions, buttons, the move vector, the turn axis
const GameInputState & gameInput();

//! The controllers the current layout is for
ControllerType controllerType();

//! The controllers' state of the last update: sticks and analog values
const ControllerInput & controllerInput();

//! The menu pointer (#34) as status.json reports it
struct PointerStatus {
	bool shown = false;          //!< the floating panel is shown: the pointer is on
	bool active = false;         //!< the owning hand's ray is on the panel: the cursor follows it
	const char * owner = "right"; //!< the hand that owns the cursor
	Vec2f cursor = Vec2f(0.f);   //!< the mouse it gives, the 2D interface's pixels
	bool beam = false;           //!< the beam was in the last submitted frame
	std::array<bool, HandCount> onPanel = { }; //!< each hand's ray hits the panel
	u64 clicks = 0;              //!< trigger presses on the panel
	u64 offPanelPresses = 0;     //!< presses off it, kept from the engine
	u64 ownerChanges = 0;
};

PointerStatus pointerStatus();

//! The turn settings of cfg.ini (\c vr_smooth_turn, \c vr_snap_turn_angle, \c vr_snap_turn_repeat,
//! \c vr_smooth_turn_speed - #32)
TurnSettings turnSettings();

/*!
 * \brief The yaw the player turns by in this frame from the turn axis (#32) - call once per frame
 *
 * \param seconds the frame's duration
 * \param allowed false while the player cannot turn (menus, cinematics, blocked controls)
 * \return degrees, right positive
 */
float updateTurn(float seconds, bool allowed);

//! Snaps and the total turned so far - for status.json
const TurnController & turnState();

/*!
 * \brief The test channel's \c vr-input command
 *
 * \c down|up|tap \c <control> (e.g. \c tap \c right.a) - a control as if the controller pressed it;
 * \c stick \c left|right \c <x> \c <y> (e.g. \c stick \c left \c 0 \c 1) - hold a stick deflected
 * (the analog value only; its direction controls are \c down / \c up);
 * \c bind \c <event> \c <binding> (e.g. \c bind \c jump \c left.grip+left.a) - remap one event;
 * \c reset - the default layout for the current controllers;
 * \c turn \c snap|smooth, \c turn \c angle \c <degrees>, \c turn \c speed \c <degrees per second>,
 * \c turn \c repeat \c <seconds> -
 * the turn settings, as cfg.ini would set them (#32).
 *
 * \return an empty string on success, otherwise the reason it was refused
 */
std::string inputCommand(std::string_view arguments);

} // namespace vr

#endif // ARX_VR_VRINPUT_H
