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
#include "vr/HeadReferenceGate.h"

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
 * Recenter is the calibration (D-018): the head at the reference is the character's eye, standing or
 * seated, at 1 m = 100 units (WorldScale.h), and looking that way is the character's facing. A new
 * reference is wanted at startup, on every level load, when a session begins, when the runtime moves
 * the LOCAL space (its own recenter), on the test channel's \c recenter and on the player's recenter
 * (#28, D-019: a long press of the left stick click, read in \ref updateInput).
 *
 * The reference must be the worn head (#27, \ref HeadReferenceGate): while the headset is not worn
 * (session neither visible nor focused - lying on the desk during a load) only a provisional reference
 * is taken; when it is put on, the settled one comes once the head has been still for a moment - again
 * every time it is put back on. Auto-raise: a head above the reference lifts it (D-018); a head far
 * enough below it crouches the character (#100, PhysicalCrouch.h).
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
 * \param bodyYaw the engine camera's yaw (engine degrees): the character's yaw in first person - the
 *        heading the stick and the mouse turn; the player model faces \ref bodyStatus instead (#25)
 * \return false when there is no head (flat mode without a simulated pose): nothing changed
 */
bool applyHeadPose(Vec3f & position, Anglef & angle, float bodyYaw);

//! Take a new reference (the next settled head, a provisional one meanwhile); \a reason goes into the game event
void resetHeadReference(const char * reason);

/*!
 * \brief The character's eye above its feet (game units) for the next \ref applyHeadPose - set by the
 *        first-person camera before it, so the reference event and status.json show the character's
 *        eye height next to the player's; not set for other cameras
 * \param standing the character stands (not crouching, getting up or levitating): its eye now is the
 *        standing eye, which a crouch lowers the eye from (#100)
 */
void setCharacterEyeHeight(float units, bool standing);

/*!
 * \return the head is far enough below its reference to crouch the character (#100, PhysicalCrouch.h):
 *         read by the player's controls next to the crouch button; false without a head
 */
bool physicalCrouch();

//! Use \a head when no headset pose is available (test channel)
void setSimulatedHead(const HeadSample & head);
void clearSimulatedHead();

//! The state of head tracking after the last \ref applyHeadPose - for status.json
struct HeadStatus {
	const char * source = "none"; //!< hmd, held, sim or none
	bool referenced = false;      //!< a reference was taken
	u64 references = 0;           //!< references taken since the game started
	std::string reason;           //!< why the last one was taken: startup, level, source ..., runtime, button, channel
	HeadReference reference;
	ReferenceKind referenceKind = ReferenceKind::None; //!< provisional until the worn head settled
	bool pending = true;          //!< a settled reference is still wanted
	bool worn = false;            //!< the headset is on a head (session visible or focused, or simulated)
	float referenceAboveFloor = -1.f; //!< the head above the room's floor at the reference, meters; -1 unknown (no STAGE)
	float aboveFloor = -1.f;      //!< the head above the floor now, meters; -1 unknown
	float raised = 0.f;           //!< how far auto-raise lifted the reference since it was taken, meters
	float eyeAboveFeet = -1.f;    //!< the character's eye above its feet, game units; -1 for a camera that is not the player's
	Anglef head;                  //!< the head's own angles in LOCAL (engine degrees)
	Vec3f position = Vec3f(0.f);  //!< the head in LOCAL, meters
	Vec3f offset = Vec3f(0.f);    //!< the camera offset from the character's eye, game units
	bool clamped = false;         //!< the lean is at the interim limit (#26)
	bool crouch = false;          //!< the head crouches the character (#100)
	float crouchDepth = 0.f;      //!< the head below its reference, meters (negative above it)
	u64 crouches = 0;             //!< physical crouch episodes since the game started
	float characterDrop = 0.f;    //!< the character's eye below its standing eye, game units (its crouch)
};

const HeadStatus & headStatus();

/*!
 * \brief Where the head looks and where the body faces (#25, D-008, D-009)
 *
 * Updated by \ref applyHeadPose. The heading is the yaw the stick and the mouse turn (\c player.angle);
 * the head's yaw since the reference is added to it for the look, and the body follows the look after
 * \ref followHead (BodyYaw.h). Without a head all three are the heading.
 */
struct BodyStatus {
	float lookYaw = 0.f;  //!< engine degrees [0, 360): where the camera looks - the stick's forward (#31)
	float bodyYaw = 0.f;  //!< engine degrees [0, 360): where the player model faces (#36)
	float offset = 0.f;   //!< body yaw - heading, in (-180, 180]; 0 without a head
	bool dragged = false; //!< the head is at the dead zone's edge, pulling the body along
	u64 drags = 0;        //!< drag episodes since the game started
};

const BodyStatus & bodyStatus();

} // namespace vr

#endif // ARX_VR_HEADTRACKING_H
