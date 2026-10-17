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

#ifndef ARX_VR_MOTIONPROBE_H
#define ARX_VR_MOTIONPROBE_H

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief What the player's body did, measured the same way at any frame rate (#23)
 *
 * The refresh-rate check runs the same timed inputs at 72, 90, 120 and 144 Hz and compares what came
 * out: how far the player walked, how far he turned, how high and how long a jump went. A value read
 * from status.json at some moment depends on when the runner looked; these are totals since a reset
 * and the last jump's measures, which stay put once the motion is over.
 *
 * Pure logic over one sample per frame (no engine globals, no clock reads): host-tested in
 * tests/vr/MotionProbeTest.cpp. The test channel feeds it after each frame was updated
 * (\c channel::afterRender) and writes it to status.json as \c motion.
 */
namespace vr {

//! The player after one frame
struct MotionSample {
	double seconds = 0.0;      //!< the game's frame clock, seconds (any steady origin)
	Vec3f position = Vec3f(0.f); //!< the player (engine units; y points down)
	float yaw = 0.f;           //!< degrees
	bool jumping = false;      //!< the engine's jump is under way (from its take-off on)
};

/*!
 * \brief Odometers and a jump gauge over the frames since a reset
 *
 *  - \ref walked: the horizontal distance, frame by frame (a walk there and back counts both ways).
 *    A step longer than \ref TeleportUnits in one frame is a teleport or a load, not a walk: skipped.
 *  - \ref turned: the yaw change, signed, unwrapped (+ turns left as the engine's yaw grows).
 *  - A jump starts when the engine's jump does, at the height it stands on; it is in the air while the
 *    player is more than \ref AirborneUnits above that, and lands when he is back. Leaving and landing
 *    are placed between the two moves of the engine's stepped jump (25-35 ms apart, #435) that lie on
 *    both sides, so the air time does not move by a frame, or by a step's phase, with the rate. \ref jumpApex is the highest point above the take-off.
 */
class MotionProbe {

public:

	//! A step this long in one frame is no walk (a level load, a teleport): 200 units, 2 m
	static constexpr float TeleportUnits = 200.f;
	/*!
	 * Above the take-off by more than this is in the air: 50 units, 50 cm. The frames on both sides of the
	 * crossing are on the flight's curve (the jump rises 130 units in 200 ms: 77 ms to get there), never one
	 * still on the floor - which would move the crossing with the rate. And it is above where the engine's
	 * jump hangs before it lands: it moves a jumping player in steps of 25-35 ms, a step that would end
	 * below the floor is refused, and the player hovers up to a step (~35 units) above the floor until the
	 * landing animation ends (#435) - a gauge at 20 cm read 0.4 s or 1.0 s for the same jump
	 */
	static constexpr float AirborneUnits = 50.f;

	//! Start counting from the next sample
	void reset();

	void update(const MotionSample & sample);

	float walked() const { return m_walked; }
	float turned() const { return m_turned; }
	double seconds() const { return m_seconds; }
	u64 frames() const { return m_frames; }

	u32 jumps() const { return m_jumps; }      //!< jumps landed since the reset
	bool airborne() const { return m_airborne; }
	float jumpApex() const { return m_apex; }  //!< the last landed jump, units
	double jumpSeconds() const { return m_air; } //!< the last landed jump: its time above \ref AirborneUnits

private:

	bool m_have = false;
	MotionSample m_last;

	float m_walked = 0.f;
	float m_turned = 0.f;
	double m_seconds = 0.0;
	u64 m_frames = 0;

	bool m_jump = false;       //!< a jump is being measured
	bool m_airborne = false;
	float m_takeoffY = 0.f;
	float m_peak = 0.f;
	double m_left = 0.0;       //!< when the player left the take-off height
	double m_moveSeconds = 0.0; //!< when the player last moved during the jump (a position that was real then)
	float m_moveHeight = 0.f;  //!< ... and how high he was
	u32 m_jumps = 0;
	float m_apex = 0.f;
	double m_air = 0.0;

};

} // namespace vr

#endif // ARX_VR_MOTIONPROBE_H
