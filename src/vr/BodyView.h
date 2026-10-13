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

#ifndef ARX_VR_BODYVIEW_H
#define ARX_VR_BODYVIEW_H

#include "math/Types.h"

/*!
 * \brief The own body never blocks the view (#385.9, #451)
 *
 * Pure math, no engine globals; host-tested in tests/vr/BodyViewTest.cpp. Two rules keep the player's
 * own body out of the eyes under a tracked head (research F-469 to F-472):
 *  - the body hangs from a neck pivot below the eyes and sits pushed back behind them along its facing
 *    (FRIK, Valheim VR), so looking down shows the chest and the legs ahead of the feet, not the inside
 *    of the shoulders;
 *  - the faces of the body still within a clearance of an eye are not drawn (VRM / VRChat hide the
 *    head-weighted triangles in the wearer's view) - never the legs: a face wholly lower than
 *    \c keepBelow under the eyes is always drawn.
 *
 * Engine axes: y is down, a yaw in engine degrees faces (-sin yaw, 0, cos yaw). All lengths are game units.
 */
namespace vr {

struct BodyViewParams {
	float push = 12.f;         //!< how far the body sits behind the eyes along its facing (cfg.ini vr_body_push)
	float neckBelowEye = 10.f; //!< the neck pivot below the eyes
	float clearance = 15.f;    //!< faces with a corner within this distance of an eye are hidden (cfg.ini vr_body_clearance)
	float keepBelow = 60.f;    //!< faces lower than this below the eyes are never hidden: the legs
};

//! The limits cfg.ini's push and clearance are clamped to (units)
const float MaxBodyPush = 40.f;
const float MaxBodyClearance = 40.f;

//! \return the neck pivot: \c neckBelowEye below \a eye and \c push behind it along the facing \a yawDegrees
Vec3f neckPivot(const Vec3f & eye, float yawDegrees, const BodyViewParams & params);

//! \return the shift that puts the model's neck \a modelNeck on the \ref neckPivot of \a eye
Vec3f bodyOffset(const Vec3f & eye, float yawDegrees, const Vec3f & modelNeck, const BodyViewParams & params);

//! \return true when the face is wholly lower than \c keepBelow under the eyes (its highest corner): the legs
bool faceBelowKeepLine(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight,
                       const BodyViewParams & params);

//! \return the distance from the nearest corner of the face to the nearer eye
float faceDistanceToEyes(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight);

/*!
 * \return true for a face not to draw in the wearer's view: a corner is within \c clearance of either eye
 *         and the face is not below the keep line (\ref faceBelowKeepLine)
 */
bool hideFaceNearEye(const Vec3f & a, const Vec3f & b, const Vec3f & c, const Vec3f & eyeLeft, const Vec3f & eyeRight,
                     const BodyViewParams & params);

} // namespace vr

#endif // ARX_VR_BODYVIEW_H
