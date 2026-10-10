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

#ifndef ARX_VR_RUNEPROJECTION_H
#define ARX_VR_RUNEPROJECTION_H

#include <vector>

#include "math/Angle.h"
#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/HeadPose.h"
#include "vr/WorldScale.h"

/*!
 * \file
 * \brief The rune stroke as the engine's recogniser sees it (#44, D-010, D-036, F-75, F-76, F-77, F-202)
 *
 * The engine recognises runes from 2D points: the mouse cursor's screen pixels, sampled while the button is
 * held, turned into a chain of numpad directions and matched against a table (game/magic/SpellRecognition.cpp).
 * In VR the hand draws in the air (#43), so its 3D stroke is projected onto a vertical plane that faces where
 * the player looked when the stroke started - a head-yaw-only frame (F-77): x along the look's right, y down
 * (the engine's y), the depth dropped - scaled to the recogniser's pixels and resampled at a fixed step along
 * the path, so a slow hand gives the same chain as a fast one. The plane is never drawn (D-010).
 *
 * The view gate (F-202 rule 1): a stroke whose centre is behind the player's look or far below / above the
 * head is not a rune - the guard against a trigger squeezed with the hand at the hip or behind the back.
 *
 * The aim (F-75): where the wand points - the controller's axis the trail is drawn along - as the engine's
 * angles, for the spells that fly from the player (F-76).
 *
 * Pure logic - positions and angles in, no engine globals, no OpenXR - covered by tests/vr/RuneProjectionTest.
 */
namespace vr {

//! Recogniser pixels per unit (1 unit = 1 cm): a 30 cm rune is 300 px, what a mouse rune is at 1080p
const float RunePixelsPerUnit = 10.f;

//! A recogniser point every this many pixels along the stroke (2 cm) - above the recogniser's own 10 px segment
const float RuneSampleStepPixels = 20.f;

//! The plane a stroke is projected onto: vertical, facing the look's yaw when the stroke started
struct RunePlane {
	float yaw = 0.f;           //!< engine degrees: the look the plane faces
	Vec3f origin = Vec3f(0.f); //!< the stroke's first point (units): the 2D origin
	float pixelsPerUnit = RunePixelsPerUnit;
};

RunePlane makeRunePlane(float lookYaw, const Vec3f & firstPoint, float pixelsPerUnit = RunePixelsPerUnit);

//! \return the horizontal right of a look at \a yaw (engine axes, unit length)
Vec3f runePlaneRight(float yaw);

//! \return \a point as the recogniser sees it: x along the plane's right, y down, in pixels from the origin
Vec2f projectOntoRunePlane(const RunePlane & plane, const Vec3f & point);

/*!
 * \brief Turns the hand's points of a stroke into the recogniser's points
 *
 * The first point is the origin; then one point every step along the path, chasing the hand: each new
 * point lies a step from the last one towards where the hand is. Nothing between two hand points closer
 * than a step, so a still hand adds nothing.
 */
class RuneSampler {

public:

	void begin(const RunePlane & plane, float stepPixels = RuneSampleStepPixels);

	//! The hand's point this frame (units); \return how many recogniser points were appended to \ref points
	size_t add(const Vec3f & point);

	const RunePlane & plane() const { return m_plane; }
	//! The recogniser's points of this stroke, the first (0, 0)
	const std::vector<Vec2s> & points() const { return m_points; }
	//! The hand's points fed so far
	size_t fed() const { return m_fed; }
	//! The centre of the hand's points' bounding box (units) - what the view gate judges
	Vec3f centre() const;
	//! The width and height of the 2D stroke (pixels)
	Vec2f size() const;

private:

	RunePlane m_plane;
	float m_step = RuneSampleStepPixels;
	std::vector<Vec2s> m_points;
	Vec2f m_last = Vec2f(0.f);   //!< the last emitted point, unrounded
	Vec2f m_min2 = Vec2f(0.f);
	Vec2f m_max2 = Vec2f(0.f);
	Vec3f m_min3 = Vec3f(0.f);
	Vec3f m_max3 = Vec3f(0.f);
	size_t m_fed = 0;

};

//! The view gate's verdict on a stroke (F-202 rule 1)
enum class RuneGate : u8 {
	None,   //!< no stroke judged yet
	Ok,
	Behind, //!< the stroke's centre is too far off the look's yaw
	Low,    //!< too far below the head
	High    //!< too far above the head
};

//! "none", "ok", "behind", "low", "high"
const char * runeGateName(RuneGate gate);

struct RuneGateParams {
	float yawDegrees = 75.f;       //!< the centre within this of the look's yaw, either side
	float belowHeadMeters = 0.9f;  //!< not lower than this below the head (about the hips)
	float aboveHeadMeters = 0.5f;  //!< not higher than this above the head
	float unitsPerMeter = UnitsPerMeter;
};

struct RuneGateResult {
	RuneGate gate = RuneGate::None;
	float offYawDegrees = 0.f;   //!< the centre's yaw from the head minus the look's, in (-180, 180]
	float belowHeadMeters = 0.f; //!< the centre below the head (+) or above it (-)
};

/*!
 * \param headCamera the head in the world (units)
 * \param lookYaw    the look the stroke's plane faces (engine degrees)
 * \param centre     the stroke's centre (units)
 */
RuneGateResult gateRuneStroke(const Vec3f & headCamera, float lookYaw, const Vec3f & centre,
                              const RuneGateParams & params = RuneGateParams());

/*!
 * \return where the wand points as engine angles: pitch (+ is down) in [-90, 90], yaw in [0, 360) - the
 *         controller's axis the trail is drawn along (\ref runeTrailPoint), for the spells that fly (F-75)
 */
Anglef aimFromPlacement(const HandPlacement & hand);

} // namespace vr

#endif // ARX_VR_RUNEPROJECTION_H
