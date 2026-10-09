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

#ifndef ARX_VR_HEADTRACKING_H
#define ARX_VR_HEADTRACKING_H

#include <string>

#include "math/Types.h"
#include "vr/HeadPose.h"

/*!
 * \brief Head tracking: the headset drives the first-person camera (#24, D-018)
 *
 * Each frame the first-person camera takes the head between the two eyes located for that frame
 * (\ref frameViews - the same sample the frame is submitted with, F-21) and becomes the head: its
 * orientation (pitch and roll absolute, yaw relative to a reference and added to the body's yaw,
 * F-120) and its movement since the reference as an offset from the character's eye (no neck model,
 * F-37). The math is \ref headToCamera (HeadPose.h).
 *
 * Where the head comes from, in this order:
 *  - \c hmd: the eyes located for this frame
 *  - \c held: the last \c hmd sample, while the session runs but the runtime has no valid pose
 *    (tracking lost, frame not begun) - the view freezes instead of snapping to the body
 *  - \c sim: a pose set through the test channel (\c headpose), for checks without a headset
 *  - \c none: flat mode, the camera stays the engine's
 *
 * The reference is taken from the first sample after a reset: at startup, on every level load, when
 * a session begins, when the runtime moves the LOCAL space (its own recenter), and on the test
 * channel's \c recenter, and on the player's recenter (#28, D-019: a long press of the left stick
 * click, read in \ref updateInput). Recenter is the calibration (D-018): the head at the reference is
 * the character's eye, standing or seated, and looking that way is the character's facing. Auto-raise:
 * a head above the reference takes its height up with it at once (\ref raiseHeadReference), so the
 * camera is never above the character's eyes; a lower head is not followed (crouch, #100). The world
 * scale is #27 (until then 1 m = 100 units).
 */
namespace vr {

/*!
 * \brief Drive the camera the frame renders with by the head - call once per frame, after every
 *        engine camera owner (first person, a conversation's camera, the death camera) placed it
 *
 * The caller takes the result off again before the engine updates its cameras next frame
 * (\ref ArxGame::restoreEngineCamera), so the head never feeds back into the engine's camera state.
 *
 * \param position the camera position, the engine's eye on entry; the head on return
 * \param angle the camera angles, replaced by the head's
 * \param bodyYaw the engine camera's yaw (engine degrees): the character's yaw in first person
 * \return false when there is no head (flat mode without a simulated pose): nothing changed
 */
bool applyHeadPose(Vec3f & position, Anglef & angle, float bodyYaw);

//! Take a new reference from the next head sample; \a reason goes into the game event
void resetHeadReference(const char * reason);

//! Use \a head when no headset pose is available (test channel)
void setSimulatedHead(const HeadSample & head);
void clearSimulatedHead();

//! The state of head tracking after the last \ref applyHeadPose - for status.json
struct HeadStatus {
	const char * source = "none"; //!< hmd, held, sim or none
	bool referenced = false;      //!< a reference was taken
	u64 references = 0;           //!< references taken since the game started
	std::string reason;           //!< why the last one was taken: startup, level, source ..., runtime, button, channel
	u64 raises = 0;               //!< auto-raise episodes: the head rose above the eye height (D-018)
	HeadReference reference;      //!< its height follows a head above it (auto-raise)
	Anglef head;                  //!< the head's own angles in LOCAL (engine degrees)
	Vec3f position = Vec3f(0.f);  //!< the head in LOCAL, meters
	Vec3f offset = Vec3f(0.f);    //!< the camera offset from the character's eye, game units
	bool clamped = false;         //!< the lean is at the interim limit (#26)
};

const HeadStatus & headStatus();

} // namespace vr

#endif // ARX_VR_HEADTRACKING_H
