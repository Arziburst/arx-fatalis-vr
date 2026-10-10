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

#ifndef ARX_VR_RUNETRAIL_H
#define ARX_VR_RUNETRAIL_H

#include <array>
#include <vector>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/HeadPose.h"
#include "vr/VRControls.h"
#include "vr/WorldScale.h"

/*!
 * \file
 * \brief The rune trail's decisions: which hand draws a stroke and where its glow is (#43, D-010, F-77)
 *
 * Runes are drawn in the air with the hand (D-010). The control model is the engine's own (Ctrl + the
 * mouse in flat mode): the magic button held is the casting stance; while it is held, the trigger of a
 * hand draws one stroke - one rune - with that hand; the trigger's release ends the rune, the magic
 * button's release casts what was drawn (the engine's recognition, #44).
 *
 * Stroke rules (F-77: arm on press, sample while held, finish on release):
 *  - a stroke starts when the stance is on and a hand's trigger is held while that hand is located;
 *    a trigger already held when the stance starts draws at once, as the engine's mouse button does;
 *  - one hand draws at a time: the first trigger wins; when both start in the same update the right
 *    hand draws; the other hand's trigger is ignored until it is released, even after the stroke ended;
 *  - a stroke ends when its trigger is released, when the stance ends, or when its hand is lost.
 *
 * Pure logic - positions and times in, no engine globals, no OpenXR, no clock reads - covered by
 * tests/vr/RuneTrailTest.
 */
namespace vr {

//! How far ahead of the grip the trail is drawn: about the tip of the index finger, a wand's tip (D-037)
const float RuneTipMeters = 0.08f;

/*!
 * \brief Where the trail is drawn for a hand: \a tipMeters ahead of the grip along the controller
 *
 * \param headCamera the head camera in the world (units)
 * \param hand       the hand placed rigid with that camera (\ref handToWorld)
 * \return the point in the world (units). The controller's forward is the grip's -Z in OpenXR, +Z in
 *         the engine axes \ref HandPlacement::orientation turns from.
 */
Vec3f runeTrailPoint(const Vec3f & headCamera, const HandPlacement & hand, float tipMeters = RuneTipMeters,
                     float unitsPerMeter = UnitsPerMeter);

/*!
 * The points that fill a segment of the trail: every \a step units from \a from (not included) up to
 * \a to (not included). Nothing for a step that is not positive or a segment shorter than a step.
 */
std::vector<Vec3f> fillTrailSegment(const Vec3f & from, const Vec3f & to, float step);

//! Why a stroke ended
enum class StrokeEnd : u8 {
	None,    //!< it did not end in this update
	Trigger, //!< its trigger was released: the rune is finished
	Stance,  //!< the magic button was released: the runes are cast
	Lost     //!< its hand was lost by the tracking
};

//! "none", "trigger", "stance", "lost"
const char * strokeEndName(StrokeEnd end);

//! One hand in an update
struct StrokeHand {
	bool trigger = false; //!< the hand's trigger is held (its click / drag event)
	bool located = false; //!< the hand's point is known in this update
	Vec3f point = Vec3f(0.f); //!< where the trail would be drawn for this hand (\ref runeTrailPoint)
};

//! Which hand draws, and the stroke's measures - one \ref update per frame
class RuneStroke {

public:

	/*!
	 * \param timeMs a monotonic time in milliseconds
	 * \param stance the casting stance is on: the magic button held and the engine lets the player cast
	 * \param hands  left, right
	 */
	void update(s64 timeMs, bool stance, const std::array<StrokeHand, HandCount> & hands);

	//! A stroke is being drawn after the last update
	bool drawing() const { return m_drawing; }
	//! The hand of the current stroke, or of the last one
	Hand hand() const { return m_hand; }
	//! Where the current stroke is now, or where the last one ended
	const Vec3f & point() const { return m_point; }

	//! A stroke started in the last update
	bool started() const { return m_started; }
	//! How the stroke ended in the last update - \ref StrokeEnd::None when it did not
	StrokeEnd ended() const { return m_ended; }
	//! How the last stroke that ended ended
	StrokeEnd lastEnd() const { return m_lastEnd; }

	//! The path the hand drew in the current or the last stroke (units)
	float length() const { return m_length; }
	//! How long the current or the last stroke was drawn
	s64 durationMs() const { return m_durationMs; }
	//! Strokes started since the object was made
	u32 strokes() const { return m_strokes; }

	//! A hand's trigger is held but ignored until it is released (another hand drew when it was pressed)
	bool ignored(Hand hand) const { return m_ignored[size_t(hand)]; }

private:

	bool m_drawing = false;
	Hand m_hand = Hand::Right;
	Vec3f m_point = Vec3f(0.f);
	bool m_started = false;
	StrokeEnd m_ended = StrokeEnd::None;
	StrokeEnd m_lastEnd = StrokeEnd::None;
	float m_length = 0.f;
	s64 m_startMs = 0;
	s64 m_durationMs = 0;
	u32 m_strokes = 0;
	std::array<bool, HandCount> m_ignored = { { false, false } };

};

} // namespace vr

#endif // ARX_VR_RUNETRAIL_H
