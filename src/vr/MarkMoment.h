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

#ifndef ARX_VR_MARKMOMENT_H
#define ARX_VR_MARKMOMENT_H

#include <string>

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief Mark the moment in the headset (#551): the marker folder's name, its meta.txt and the panel's lines
 *
 * During a headset test both Index trackpads pressed (vr::ControlResolver::markGesture) write
 * <user dir>\c \\marks\\<name>\\ with the eye images, the flight recorder's tail, the replay ring's last 30 s, this
 * meta.txt and an optional voice note (vr/VoiceNote.h); the game's side is vr/VRMarks.h. Pure: no engine globals,
 * no file system - covered by tests/vr/MarkMomentTest.cpp.
 */
namespace vr {

//! The confirmation in the hero-dialogue panel: "Moment marked (#<number>)"
std::string markNoticeText(u32 number);

//! The voice note's line in the same panel: "Recording note... (<seconds> s)"
std::string noteNoticeText(u32 seconds);

/*!
 * The marker folder's name: the mark's UTC time (ISO 8601 basic) and its number in the session -
 * "20261020T153012Z-1"; sorts by time, valid on every file system
 */
std::string markFolderName(s64 unixSeconds, u32 number);

//! What meta.txt says about a mark
struct MarkMeta {
	u32 number = 0;               //!< the mark's number in this session, from 1 (the "#N" of the confirmation)
	s64 unixSeconds = 0;          //!< when it was made: UTC seconds since 1970
	u32 area = 0;                 //!< the level (status.json "area"), 0 for none
	Vec3f position = Vec3f(0.f);  //!< the player's position (status.json player.x / y / z), game units
	float yaw = 0.f;              //!< the player's heading (status.json player.yaw), degrees
	std::string build;            //!< the build: version, commit and config
	std::string state;            //!< the game's state (status.json "state": GAMEPLAY, MENU, ...)
	std::string session;          //!< the VR session's state (focused, visible, synchronized, ...)
};

/*!
 * meta.txt: "# moment mark (#551)", then one key=value line per field - mark, time (YYYY-MM-DDTHH:MM:SSZ, UTC),
 * area, position ("<x> <y> <z>", one decimal each), yaw (one decimal), build, state, session - each ending in "\n".
 * A control character in a text field is written as a space, so a field is always one line. No path of any kind.
 */
std::string formatMarkMeta(const MarkMeta & meta);

} // namespace vr

#endif // ARX_VR_MARKMOMENT_H
