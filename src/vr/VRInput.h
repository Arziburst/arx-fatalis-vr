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

#include <string>
#include <string_view>

#include "vr/BindingCapture.h"
#include "vr/Locomotion.h"
#include "vr/VRControls.h"
#include "vr/VRGameInput.h"
#include "vr/VRTurn.h"

/*!
 * \brief The controllers as gameplay events (#30, D-020)
 *
 * Every frame the physical controls are read from OpenXR (\ref readControllers) and the game's
 * control table turns them into gameplay events (\ref ControlResolver). The layout follows the
 * controllers: when the interaction profile changes, the default layout for that controller type
 * is applied (or the layout the player saved for that type in the VR settings, #178).
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

//! The stick walk of the last game update (#31): what status.json reports
struct WalkStatus {
	bool walking = false;
	float yaw = 0.f;     //!< engine degrees [0, 360): where the stick's forward pointed (the look, D-008)
	float heading = 0.f; //!< engine degrees: the player's heading in that update
	Vec3f direction = Vec3f(0.f);
	u64 walks = 0;       //!< walk episodes since the game started
	bool sneak = false;  //!< the walk sneaks: a slight deflection (#33)
	u64 paceChanges = 0; //!< switches between sneak and run within a walk since the game started (#33)
};

const WalkStatus & walkStatus();

//! Called by the game's player controls every update with the stick walk it applied
void reportWalk(const StickWalk & walk, float yaw, float heading);

//! The controllers the current layout is for
ControllerType controllerType();

//! The controllers' state of the last update: sticks and analog values
const ControllerInput & controllerInput();

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

//! The repeats of the last snap push (0: a single snap) and their mean interval in seconds of the clock, from
//! the push's first snap to its last repeat (0 without a repeat) - for status.json (#285)
u64 snapPushRepeats();
float snapRepeatInterval();

/*!
 * The controller type a remapped layout is saved for: an unknown controller uses the Index layout
 * (\ref defaultLayout), so it also uses the Index's saved one
 */
ControllerType layoutType(ControllerType type);

/*!
 * The file a remapped layout of these controllers is saved in (#178): \c vr-controls-<type>.ini in
 * the config directory, next to cfg.ini - only the events that differ from the default (\ref formatLayoutChanges)
 */
std::string layoutFileName(ControllerType type);

//! Whether the current controllers have a saved, remapped layout
bool layoutRemapped();

/*!
 * The player's remapping in the VR settings (#178): bind \a event, apply it at once and save the
 * layout for the current controllers
 */
void setPlayerBinding(Event event, const Binding & binding);

//! The VR settings' "reset to default": #35's layout for the current controllers, the saved file removed
void restoreDefaultLayout();

/*!
 * Start capturing what the player presses for \a event (\ref BindingCapture): the controls then do
 * nothing else until the capture ends, and its result is bound with \ref setPlayerBinding.
 * In flat mode only the test channel's controls (\c vr-input \c down) can answer it.
 */
void startCapture(Event event);

void cancelCapture();

//! The capture of the VR settings page: its state, what is held so far, the last refusal
const BindingCapture & capture();

//! The event being captured (or captured last)
Event captureEvent();

//! How many captures ended with a binding since the start
u32 captureCount();

/*!
 * \brief The test channel's \c vr-input command
 *
 * \c down|up|tap \c <control> (e.g. \c tap \c right.a) - a control as if the controller pressed it;
 * \c stick \c left|right \c <x> \c <y> (e.g. \c stick \c left \c 0 \c 1) - hold a stick deflected
 * (the analog value only; its direction controls are \c down / \c up);
 * \c bind \c <event> \c <binding> (e.g. \c bind \c jump \c left.grip+left.a) - remap one event;
 * \c reset - the default layout for the current controllers, not saved;
 * \c capture \c <event> - what the VR settings page does when the player selects the event
 * (\ref startCapture); \c cancel - end the capture; \c default - the page's "reset to default"
 * (\ref restoreDefaultLayout); \c reload - read the saved layout again, as at a start;
 * \c turn \c snap|smooth, \c turn \c angle \c <degrees>, \c turn \c speed \c <degrees per second>,
 * \c turn \c repeat \c <seconds> -
 * the turn settings, as cfg.ini would set them (#32).
 *
 * \return an empty string on success, otherwise the reason it was refused
 */
std::string inputCommand(std::string_view arguments);

} // namespace vr

#endif // ARX_VR_VRINPUT_H
