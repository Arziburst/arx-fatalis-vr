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

#ifndef ARX_VR_VRTURN_H
#define ARX_VR_VRTURN_H

#include <optional>
#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief Snap turn and smooth turn from the turn axis (#32, F-32, D-019)
 *
 * The right stick's x past its dead zone (\ref GameInputState::turn, #74) turns the player:
 *  - snap (the default): one step of \ref TurnSettings::snapAngle per push - the step fires when the
 *    axis passes \ref SnapFire and re-arms only when it falls below \ref SnapRearm, so holding the
 *    stick never repeats;
 *  - smooth: \ref TurnSettings::smoothSpeed degrees per second at full deflection, linear in between.
 *
 * The result is a change of the body's yaw; the head keeps its own yaw on top of it (#24), so the
 * play-space heading turns and head and body turn together.
 *
 * Pure logic - no engine globals, no clock reads - covered by tests/vr/VRTurnTest.
 */
namespace vr {

enum class TurnMode : u8 {
	Snap,
	Smooth
};

//! "snap" or "smooth" - for the log, status.json, cfg.ini and the test channel
const char * turnModeName(TurnMode mode);

//! \return the mode named by \ref turnModeName, nothing for an unknown name
std::optional<TurnMode> parseTurnMode(std::string_view name);

//! The defaults (owner, 2026-10-10): snap 45 degrees; smooth 120 degrees per second at full deflection
constexpr float DefaultSnapAngle = 45.f;
constexpr float DefaultSmoothSpeed = 120.f;

//! The ranges cfg.ini and the test channel clamp a setting to
constexpr float MinSnapAngle = 5.f;
constexpr float MaxSnapAngle = 180.f;
constexpr float MinSmoothSpeed = 10.f;
constexpr float MaxSmoothSpeed = 720.f;

//! The player's turn settings (cfg.ini now, the settings screen later: #178)
struct TurnSettings {
	TurnMode mode = TurnMode::Snap;
	float snapAngle = DefaultSnapAngle;     //!< degrees per snap
	float smoothSpeed = DefaultSmoothSpeed; //!< degrees per second at full deflection
};

/*!
 * Snap thresholds on the dead-zoned axis: 0.5 fires and 0.125 re-arms, which are 0.6 and 0.3 of the
 * raw stick with the 0.2 dead zone of \ref GameInputMapper (F-32)
 */
const float SnapFire = 0.5f;
const float SnapRearm = 0.125f;

//! Turns the turn axis into yaw changes, once per frame
class TurnController {

public:

	/*!
	 * \param axis the turn axis, -1 (left) to 1 (right), already past the dead zone
	 * \param seconds the frame's duration (smooth turn)
	 * \param allowed false while the player cannot turn (a menu, a cinematic, blocked controls):
	 *        nothing turns, and a stick still pushed when that ends has to come back before a snap
	 * \return the yaw change in degrees, right positive
	 */
	float update(const TurnSettings & settings, float axis, float seconds, bool allowed);

	//! Snaps since the start
	u64 snaps() const { return m_snaps; }

	//! The direction of the last snap: 1 right, -1 left, 0 none yet
	int lastSnap() const { return m_lastSnap; }

	//! All yaw changes since the start, degrees, right positive
	float turned() const { return m_turned; }

	//! Updates so far, and whether the last one let the player turn
	u64 updates() const { return m_updates; }
	bool allowed() const { return m_allowed; }

private:

	u64 m_updates = 0;
	bool m_allowed = false;
	bool m_armed = true;
	u64 m_snaps = 0;
	int m_lastSnap = 0;
	float m_turned = 0.f;

};

} // namespace vr

#endif // ARX_VR_VRTURN_H
