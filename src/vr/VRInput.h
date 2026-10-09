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

#include "vr/VRControls.h"

/*!
 * \brief The controllers as gameplay events (#30, D-020)
 *
 * Every frame the physical controls are read from OpenXR (\ref readControllers) and the game's
 * control table turns them into gameplay events (\ref ControlResolver). The layout follows the
 * controllers: when the interaction profile changes, the default layout for that controller type
 * is applied (saved layouts per type come with the settings screen, #178).
 *
 * The test channel drives the same table without a headset (\c vr-input, docs/vr/TESTING.md).
 * Nothing in the game reacts to the events yet - that is #74.
 */
namespace vr {

//! Read the controllers and resolve the gameplay events - once per frame, after \ref pollEvents
void updateInput();

//! The gameplay events of the last update
const ControlResolver & controls();

//! The controllers the current layout is for
ControllerType controllerType();

//! The controllers' state of the last update: sticks and analog values
const ControllerInput & controllerInput();

/*!
 * \brief The test channel's \c vr-input command
 *
 * \c down|up|tap \c <control> (e.g. \c tap \c right.a) - a control as if the controller pressed it;
 * \c bind \c <event> \c <binding> (e.g. \c bind \c jump \c left.grip+left.a) - remap one event;
 * \c reset - the default layout for the current controllers.
 *
 * \return an empty string on success, otherwise the reason it was refused
 */
std::string inputCommand(std::string_view arguments);

} // namespace vr

#endif // ARX_VR_VRINPUT_H
