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

#ifndef ARX_GAME_JUMPPHYSICS_H
#define ARX_GAME_JUMPPHYSICS_H

#include <algorithm>
#include <cmath>

/*!
 * \file
 * \brief The player's jump in fixed physics steps (#23, #537)
 *
 * The engine moved a jumping player once 25 ms or more of frames had added up, by a step as long as they were
 * (ARX_PLAYER_Manage_Movement's loop and its TODO): the Euler fall and the damped forward push then depended on
 * the refresh rate and on every hitch - one jump's time in the air read 0.29 to 0.36 s. Now the frame's time goes
 * into a \ref FixedStepClock and the jumping player moves by \ref JumpStepMs at a time, what is left waiting for
 * the next frame; the rise is in step time too (\ref jumpRise). The flight is then a function of the number of
 * steps alone: the same at 72, 90, 120 and 144 Hz, and after a hitch, which only takes several steps in one frame.
 *
 * What the flight needs without the engine lives here, so tests/game/JumpPhysicsTest.cpp flies it with the frames
 * of every rate and with an irregular sequence and compares.
 */

/*!
 * One physics step of a jumping player, in milliseconds: the engine's own FIXED_TIMESTEP, and the step the flat game's
 * jump had in practice (at 60 Hz its frames added up to steps of 25 and 33 ms)
 */
constexpr float JumpStepMs = 25.f;

/*!
 * How much physics time a step of the jump integrates per millisecond of frames, by default (cfg.ini
 * `jump_time_scale`, 1 to 1.5). The flat game's loop integrated the remainder it carried a second time: at 60-144 Hz
 * 25 ms of frames were 29-33 ms of physics, so the fall and the forward push of the jump players know are 1.2-1.33
 * times quicker than the constants below say (measured above 50 cm: 0.30-0.32 s at 72-144 Hz and flat at 120 Hz;
 * the constants' own arc reads 0.34 s). 1.2 keeps that arc; 1 is the constants' own. The rise is not scaled: it was
 * 200 ms of the clock in the flat game too.
 */
constexpr float JumpTimeScaleDefault = 1.2f;

//! The rise: \ref JumpRiseHeight units up in \ref JumpRiseMs of step time, at a constant speed
constexpr float JumpRiseMs = 200.f;
constexpr float JumpRiseHeight = 130.f;

//! The frame the player's forces are tuned for: a force is per such a frame, a step applies it per millisecond
constexpr float PlayerTargetDt = 1000.f / 30.f;

//! The pull down per target frame: the world's (a jump's descent, a fall from a step) and a fall's from a height
constexpr float PlayerWorldGravity = 0.1f;
constexpr float PlayerJumpGravity = 0.02f;

//! The share of the horizontal velocity a step's friction takes per millisecond (\ref dampenVelocity)
constexpr float PlayerVelocityDamping = 0.009f;

/*!
 * \brief Frames of any length into physics steps of one length
 *
 * A frame's time is added; a step is taken while a whole one is stored, and what does not fill a step waits for the
 * next frame. The same number of steps for the same time whatever the frames, and a long frame takes its steps at once.
 */
class FixedStepClock {
	
	float m_stepMs;
	float m_storedMs = 0.f;
	
public:
	
	explicit FixedStepClock(float stepMs) : m_stepMs(stepMs) { }
	
	void add(float ms) { m_storedMs += ms; }
	
	//! True, and a step's time taken, while a whole step is stored
	bool takeStep() {
		if(m_storedMs < m_stepMs) {
			return false;
		}
		m_storedMs -= m_stepMs;
		return true;
	}
	
	//! All the stored time, taken: a frame of walking moves by the frame
	float takeAll() {
		const float ms = m_storedMs;
		m_storedMs = 0.f;
		return ms;
	}
	
	float stored() const { return m_storedMs; }
	float stepMs() const { return m_stepMs; }
	
};

/*!
 * The rise of one step: the units up, with \a progress (0 at the take-off, 1 at the apex) advanced by the step
 */
inline float jumpRise(float & progress, float stepMs) {
	const float before = progress;
	progress = std::min(1.f, progress + stepMs / JumpRiseMs);
	return (progress - before) * JumpRiseHeight;
}

/*!
 * A horizontal velocity after one step's friction: the engine's dampen, a factor of 1 - \ref PlayerVelocityDamping per
 * millisecond, never below zero; a velocity under a thousandth is at rest
 */
inline float dampenVelocity(float velocity, float stepMs) {
	float dampen = 1.f - PlayerVelocityDamping * stepMs;
	if(dampen < 0.001f) {
		dampen = 0.f;
	}
	velocity *= dampen;
	if(std::abs(velocity) < 0.001f) {
		velocity = 0.f;
	}
	return velocity;
}

#endif // ARX_GAME_JUMPPHYSICS_H
