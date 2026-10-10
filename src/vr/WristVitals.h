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

#ifndef ARX_VR_WRISTVITALS_H
#define ARX_VR_WRISTVITALS_H

#include <array>

#include "math/Types.h"

/*!
 * \brief Health and mana as two bars on the inner side of the left wrist (#116, D-029, D-NEW)
 *
 * The pure layout: where the two bars are and how visible they are, from the posed arm and the
 * eye - no engine globals, so it is covered by host tests. The bars lie flat on the palm side of
 * the wrist, a little towards the elbow from the wrist joint; each runs across the wrist and fills
 * towards the thumb, so they stand upright when the wrist is turned to the face like a watch.
 * Health is the one towards the elbow, mana the one towards the hand: red left and blue right,
 * as on the flat HUD, for a left forearm held across the face. They fade out as the inner wrist
 * turns away from the eyes.
 */
namespace vr {

//! The look of the wrist bars (owner-approved MVP defaults, D-NEW)
struct WristVitalsParams {
	float unitsPerMeter = 100.f;
	float barWidthM = 0.01f;     //!< each bar, along the forearm
	float barLengthM = 0.05f;    //!< each bar, across the wrist (the full value)
	float gapM = 0.006f;         //!< between the two bars
	float liftM = 0.03f;         //!< from the wrist joint out to the skin on the palm side
	float backM = 0.04f;         //!< from the wrist joint towards the elbow
	float fullAngleDeg = 50.f;   //!< inner wrist within this angle of the eyes: fully visible
	float zeroAngleDeg = 80.f;   //!< beyond this angle: invisible
};

//! The posed left arm in the world (game units), as the full body (#36) left it this frame
struct WristFrame {
	Vec3f elbow = Vec3f(0.f);
	Vec3f wrist = Vec3f(0.f);
	Vec3f palmNormal = Vec3f(0.f); //!< out of the palm (the grip's +X for the left hand)
	Vec3f thumb = Vec3f(0.f);      //!< towards the thumb across the wrist (the grip's -Z)
};

//! One bar: corners empty end (elbow side, hand side), then full end (hand side, elbow side); two triangles 012, 023
struct WristBar {
	std::array<Vec3f, 4> back;  //!< the whole bar (the dark backing)
	std::array<Vec3f, 4> fill;  //!< the filled part, from the empty end up to the value
	float fraction = 0.f;       //!< [0, 1]
};

struct WristVitalsLayout {
	bool valid = false;          //!< false for a degenerate arm (elbow on the wrist, palm along the forearm)
	float facingDeg = 180.f;     //!< angle between the inner wrist's normal and the direction to the eye
	float alpha = 0.f;           //!< [0, 1] from \ref facingDeg
	Vec3f center = Vec3f(0.f);   //!< middle between the bars, on the skin
	Vec3f normal = Vec3f(0.f);   //!< out of the inner wrist
	Vec3f along = Vec3f(0.f);    //!< elbow -> hand
	Vec3f across = Vec3f(0.f);   //!< the direction the bars fill
	WristBar health;
	WristBar mana;
};

//! \return the visibility for an inner wrist turned \a facingDeg away from the eyes
float wristVitalsAlpha(float facingDeg, const WristVitalsParams & params = WristVitalsParams());

/*!
 * \param health, mana the values as fractions of their maximum (clamped to [0, 1])
 * \param eye the eye (or the head camera) the bars are seen from, in the world
 */
WristVitalsLayout layoutWristVitals(const WristFrame & frame, const Vec3f & eye, float health, float mana,
                                    const WristVitalsParams & params = WristVitalsParams());

} // namespace vr

#endif // ARX_VR_WRISTVITALS_H
