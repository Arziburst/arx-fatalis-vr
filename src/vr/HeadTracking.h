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
 * every time it is put back on. Auto-raise: a head above the reference lifts it (D-018); the crouch
 * threshold below it is #100.
 *
 * Leaning and real steps (#26, D-018, HeadLean.h): the head's horizontal movement is split between
 * the body and the camera. The body - the player's collision cylinder - stays while the head is within
 * the follow radius of it and steps after it beyond, through the engine's collision (\ref takeBodyStep,
 * \ref bodyStepped); the camera adds only the rest, the lean. A body held by a wall leaves a growing
 * lean, and a head that ends up in geometry, or too far from the body, fades the world to black
 * (\ref updateWallFade). The defaults are cfg.ini keys (\c vr_body_follow_radius, \c vr_wall_fade_out,
 * \c vr_wall_fade_in, \c vr_head_far_start, \c vr_head_far_full).
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
 * \return the head sample \ref applyHeadPose will use this frame (hmd, held or sim), without
 *         changing any state - for the body (#36), which is posed before the camera is placed
 */
bool peekHead(HeadSample & head);

/*!
 * \brief The character's eye above its feet (game units) for the next \ref applyHeadPose - set by the
 *        first-person camera before it, so the reference event and status.json show the character's
 *        eye height next to the player's; not set for other cameras
 */
void setCharacterEyeHeight(float units);

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
	// Leaning and real steps (#26, HeadLean.h)
	Vec2f anchor = Vec2f(0.f);    //!< meters the body walked after the head since the reference (reference frame)
	Vec2f lean = Vec2f(0.f);      //!< meters the head is from the body (reference frame)
	float leanDistance = 0.f;     //!< meters, the length of \c lean
	float walked = 0.f;           //!< meters the body walked after the head since the game started
	bool stepping = false;        //!< the body is walking after the head
	u64 steps = 0;                //!< stepping episodes since the game started
	bool bodyBlocked = false;     //!< the body was asked to step but the collision held it
	u64 blocks = 0;               //!< blocked episodes since the game started
};

const HeadStatus & headStatus();

/*!
 * \brief Real steps (#26): the step the body is asked to take after the head, game units in the
 *        world (x, z) - taken once; zero when there is none
 *
 * Asked by \ref applyHeadPose, taken by the player's movement (ARX_PLAYER_Manage_Movement), which
 * moves the collision cylinder through the engine's collision and answers with \ref bodyStepped.
 * A step nobody took (the controls are blocked, the player jumps) is replaced by next frame's.
 */
Vec2f takeBodyStep();

//! What the collision let the body walk of the step taken by \ref takeBodyStep, game units (x, z)
void bodyStepped(const Vec2f & moved);

/*!
 * \brief The wall fade (#26, D-018): call once per frame after \ref applyHeadPose with whether the head
 *        is inside geometry or behind it as seen from the character's eye
 *
 * \param blocked why the head is in geometry (\c inside, \c behind), nullptr when it is not
 * \param seconds the frame's duration
 */
void updateWallFade(const char * blocked, float seconds);

//! The wall fade after the last \ref updateWallFade - for status.json and the eyes
struct FadeStatus {
	float fade = 0.f;            //!< 0..1: how black the world is drawn
	const char * wall = nullptr; //!< why the head is in geometry (\c inside, \c behind), nullptr when it is not
	u64 walls = 0;               //!< head-in-geometry episodes since the game started
	float farFade = 0.f;         //!< 0..1: the part of the fade a head too far from the body asks for
	u64 fars = 0;                //!< too-far episodes since the game started
};

const FadeStatus & fadeStatus();

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
