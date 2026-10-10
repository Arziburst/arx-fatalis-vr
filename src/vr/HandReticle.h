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

#ifndef ARX_VR_HANDRETICLE_H
#define ARX_VR_HANDRETICLE_H

#include <array>
#include <optional>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief The hand reticles in the world (#37): the pure rules behind them
 *
 * Each hand has a hidden ray from its aim pose (D-016, D-010: no visible beam); the engine glue
 * (vr/VRReticle.cpp) casts it against the level and the entities and puts a small ring where it hits.
 * What it hits is the engine's mouse when that hand owns the mouse (the arbiter is the menu pointer of
 * #34, vr/MenuPointer.h, extended to world targets). The rules here have no OpenXR, no engine globals
 * and no clock reads - host-tested in tests/vr/HandReticleTest.cpp:
 *  - \ref HoverFilter: a new entity under the ray counts after 2 frames, a lost one stays for 150 ms
 *    (F-57, F-160) - a thin item under a shaking hand does not flicker.
 *  - \ref worldCursorPixel and \ref PickHistory: the engine picks what is under the mouse by a 2D
 *    position. A world reticle's pixel is its hit projected with the head camera, moved into a band to
 *    the right of the 2D interface, where no widget of the interface is (so the inventory drawn at the
 *    bottom of the screen never answers for a floor item seen there); the history gives back the entity
 *    the ray picked for such a pixel - one mapping from hit to pixel for the highlight and the click
 *    (F-175).
 *  - \ref reticleQuad: the ring's quad layer, the same angular size at any distance, facing the eyes.
 */
namespace vr {

//! The hidden ray's length: the reticle is shown on what it hits up to here (game units, 20 m)
constexpr float ReticleRayUnits = 2000.f;

//! The ring's angular diameter (D-029: a small dot / ring; the coordinator's number for the owner, 2026-10-10)
constexpr float ReticleAngleDegrees = 1.5f;

//! The ring's opacity: over nothing that can be taken, and brightened over an item that can (D-029)
constexpr float ReticleDimAlpha = 0.45f;
constexpr float ReticleBrightAlpha = 1.f;

//! Hover hysteresis (F-57): frames a new entity must stay under the ray, seconds a lost one is kept
constexpr u32 HoverFramesIn = 2;
constexpr float HoverHoldSeconds = 0.15f;

//! Where the world reticles' pixels start: x of the band right of any 2D interface (see \ref worldCursorPixel)
constexpr s32 WorldCursorBandX = 16384;
//! How far a projected hit may be from the interface's origin before it is clamped (pixels)
constexpr s32 WorldCursorClamp = 8000;

/*!
 * \brief The hover of one hand with hysteresis (F-57, F-160)
 *
 * Entities are given by any id that is not \ref None (the engine glue uses entity indices).
 */
class HoverFilter {

public:

	static constexpr s32 None = -1;

	/*!
	 * \param candidate what the ray picks this frame, \ref None for nothing
	 * \param now       seconds on any steady clock
	 * \return the hover after this frame
	 */
	s32 update(s32 candidate, double now);

	s32 hovered() const { return m_hovered; }

	void reset() { *this = HoverFilter(); }

private:

	s32 m_hovered = None;
	s32 m_candidate = None;
	u32 m_candidateFrames = 0;
	double m_lastSeen = 0.0;

};

/*!
 * \brief The engine mouse position for a world hit: its projection on the interface, in the band
 *
 * \param projected the hit projected with the head camera, in the interface's pixels
 * \return the pixel \c WorldCursorBandX to the right of the projection, both axes clamped to
 *         \c WorldCursorClamp - distances between such pixels are the projection's (the click dead
 *         zone and the engine's drag threshold measure them), and no interface widget is there
 */
Vec2s worldCursorPixel(const Vec2f & projected);

//! \return whether \a pixel is in the world reticles' band (see \ref worldCursorPixel)
bool isWorldCursorPixel(const Vec2s & pixel);

/*!
 * \brief Which entity a world cursor pixel stands for
 *
 * The engine asks for the entity under the mouse at the current pixel and, when a drag starts, at the
 * pixel of the press (frames later). Every frame the arbiter records the pixel it gave the engine with
 * the entity it stands for; a press is kept per hand until the next one, so a long press still finds
 * its entity. The newest record of a pixel wins.
 */
class PickHistory {

public:

	static constexpr size_t Size = 32;

	//! The engine mouse is \a pixel this frame and stands for \a entity (HoverFilter::None: nothing)
	void record(const Vec2s & pixel, s32 entity);

	//! \a hand pressed with the mouse at \a pixel over \a entity
	void press(Hand hand, const Vec2s & pixel, s32 entity);

	//! \return the entity recorded for \a pixel, nothing when it was never recorded
	std::optional<s32> find(const Vec2s & pixel) const;

	void clear() { *this = PickHistory(); }

private:

	struct Record {
		bool valid = false;
		Vec2s pixel = Vec2s(0);
		s32 entity = HoverFilter::None;
	};

	std::array<Record, Size> m_records;
	size_t m_next = 0;
	std::array<Record, HandCount> m_presses;

};

//! The ring's quad layer
struct ReticleQuad {
	Vec3f center = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	float size = 0.f; //!< width and height, meters
};

/*!
 * \brief The ring at \a hit, seen from \a eye
 *
 * Its face (+z) points at the eye, its up (+y) stays as close to the space's up as it can, and its size
 * covers \a angleDegrees at the distance from the eye.
 * \return false for a hit at the eye - nothing to draw
 */
bool reticleQuad(const Vec3f & hit, const Vec3f & eye, float angleDegrees, ReticleQuad & quad);

} // namespace vr

#endif // ARX_VR_HANDRETICLE_H
