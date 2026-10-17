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

#ifndef ARX_VR_STABLEVIEW_H
#define ARX_VR_STABLEVIEW_H

#include <string_view>

#include "math/Angle.h"
#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \brief The stable view (#416, #57): in VR the camera moves only with the head and the body's real travel
 *
 * Pure math, no engine globals - host-tested in tests/vr/StableViewTest.cpp.
 *
 * The engine puts the first-person eye on the animated player model: walking, jumping, landing, crouching,
 * striking, being hit and dying move or tilt the view. In VR the view instead stands on the collision
 * cylinder - the body's real travel: horizontally exactly where the body is, vertically the body's feet plus
 * the eye of its stance (standing, crouched or levitating), never an animation. What changes the height is smoothed:
 *  - the view's feet follow the body's feet at \ref ViewMaxSpeed at most (2 cm per frame at 90 Hz) - stairs,
 *    slopes, a landing; a jump either keeps the view level (\ref JumpView::Level: never above the take-off)
 *    or follows its arc that slowly (\ref JumpView::Arc);
 *  - the eye glides between the stances' eyes (standing, crouched, levitating) at the crouch toggle's speed
 *    (\ref CrouchGlide), within what the feet leave of the same per-frame limit;
 *  - a fall the speed cannot follow pulls the view along \ref ViewMaxLag behind the body (a fall is real
 *    travel; only a jump's arc and the crouch are bound to the limit), and a teleport is a cut.
 * Death is a still ghost (\ref DeathView): the view stays where the eye was, only the head and the turn move it.
 */
namespace vr {

//! How the view follows a jump (cfg.ini \c vr_view_jump)
enum class JumpView {
	Level, //!< the view never rises above the take-off: a jump on the spot does not move it (the calmest)
	Arc    //!< the view follows the jump's arc at the speed limit: a low, soft hop
};

//! How fast the view glides between the standing and the crouched eye (cfg.ini \c vr_view_crouch)
enum class CrouchGlide {
	Slow,   //!< 0.5 m/s: the 50 cm in a second (the calmest)
	Medium, //!< 1 m/s
	Fast    //!< 1.8 m/s, the speed limit: about a quarter of a second
};

//! What the still ghost sees at death (cfg.ini \c vr_death_view)
enum class DeathView {
	Body,    //!< the whole body below, falling as the engine shows a dead hero
	Headless //!< the head stays hidden as in life: no look out of the falling head
};

bool parseJumpView(std::string_view text, JumpView & value);
bool parseCrouchGlide(std::string_view text, CrouchGlide & value);
bool parseDeathView(std::string_view text, DeathView & value);
const char * jumpViewName(JumpView value);
const char * crouchGlideName(CrouchGlide value);
const char * deathViewName(DeathView value);

//! \return the glide speed, game units (cm) per second
float crouchGlideSpeed(CrouchGlide value);

constexpr float ViewMaxSpeed = 180.f;   //!< units per second the view's height may change by: 2 cm per frame at 90 Hz
constexpr float ViewMaxLag = 60.f;      //!< units a fall may leave the view above the body before it pulls it along
constexpr float ViewCatchUp = 360.f;    //!< units per second a lag beyond \ref ViewMaxLag left by a jump's arc closes at
constexpr float ViewTeleport = 150.f;   //!< the body's feet moving more than this in one frame are a cut, not travel
//! units a landing may be off the take-off and still be the same floor: the collision brings the feet back to a few
//! units of where they were (1.3 measured on CI after a strafe, #416), and the view does not follow that
constexpr float ViewLandBand = 5.f;
//! units the levitating character's eye is above its standing eye: the player model's eye in its levitation pose
//! (the camera motion catalog of #416: 30.5); the collision cylinder does not move, the model floats over it
constexpr float LevitateEyeRise = 30.f;

//! How the view follows the body
struct StableViewParams {
	JumpView jump = JumpView::Level;
	float crouchSpeed = 50.f;      //!< units per second, \ref crouchGlideSpeed
	float maxSpeed = ViewMaxSpeed;
	float maxLag = ViewMaxLag;
	float catchUp = ViewCatchUp;
	float teleport = ViewTeleport;
	float landBand = ViewLandBand;
};

//! The body this frame
struct StableViewInput {
	float dt = 0.f;           //!< seconds since the last frame
	float feetY = 0.f;        //!< the body's feet, engine y (down positive)
	float eyeAboveFeet = 0.f; //!< the eye of the body's stance above its feet, units (standing or crouched)
	bool jumping = false;     //!< a jump is under way, from the take-off to the landing
};

//! The view's height, carried from frame to frame
struct StableView {
	bool valid = false;
	float feetY = 0.f;      //!< the view's feet, engine y
	float eye = 0.f;        //!< the view's eye above its feet, units
	float bodyFeetY = 0.f;  //!< the body's feet last frame (teleport detection)
	bool jumping = false;   //!< a jump was under way last frame
	float takeoffY = 0.f;   //!< the view's feet at the take-off of the current jump
	float lagCap = ViewMaxLag; //!< the lag allowed now: above \ref ViewMaxLag only while a jump's arc closes
	bool landed = false;    //!< the last jump came down within \ref ViewLandBand of its take-off: the view keeps that floor
	u64 cuts = 0;           //!< teleports and first frames: the view jumped to the body

	//! The camera's height, engine y
	float cameraY() const { return feetY - eye; }
};

//! Move the view after the body for one frame
void updateStableView(StableView & view, const StableViewInput & input, const StableViewParams & params);

//! A camera sample for \ref CameraMotion
struct CameraSample {
	float dt = 0.f;            //!< seconds since the last frame
	Vec3f camera = Vec3f(0.f); //!< the camera the frame renders with (game units = cm)
	Anglef angle;              //!< its angles, degrees
	Vec3f body = Vec3f(0.f);   //!< the body (the player's collision position)
	float bodyYaw = 0.f;       //!< the body's heading, degrees (what the stick and the mouse turn)
};

/*!
 * \brief The camera's motion over a stretch of frames (#416): what moved the view that the head and the body's
 *        travel do not explain - status.json \c camera.motion, the catalog's measuring instrument
 *
 * Every frame adds the camera's step against the body's step; the maxima and the range of the camera's place
 * relative to the body are kept until \ref resetCameraMotion. With the head still, a camera that only follows
 * the body has 0 everywhere but \c maxStepY / \c maxSpeedY (its smoothed height changes).
 */
struct CameraMotion {
	u64 frames = 0;           //!< frames measured (the first only sets the start)
	float maxDt = 0.f;        //!< the longest frame, seconds
	float maxStepY = 0.f;     //!< the largest height change of one frame, cm
	float maxSpeedY = 0.f;    //!< the largest height change per second, cm/s: \c maxSpeedY / 90 = the step at 90 Hz
	float maxStepXZ = 0.f;    //!< the largest horizontal step of one frame, cm
	float maxOffStepXZ = 0.f; //!< the largest horizontal step that is not the body's own, cm
	float maxOffStepY = 0.f;  //!< the largest height step that is not the body's own, cm
	float maxPitchStep = 0.f; //!< degrees per frame
	float maxRollStep = 0.f;  //!< degrees per frame
	float maxYawStep = 0.f;   //!< degrees per frame, the turn included
	float maxOffYawStep = 0.f; //!< degrees per frame that are not the body's turn
	Vec3f minOffset = Vec3f(0.f); //!< the camera's place relative to the body: its range is the sway
	Vec3f maxOffset = Vec3f(0.f);
	float minCameraY = 0.f;       //!< the camera's own height range: what a jump or a crouch did to the view
	float maxCameraY = 0.f;
	float minBodyY = 0.f;         //!< the body's height range: what the body itself travelled up and down
	float maxBodyY = 0.f;
	bool have = false;
	CameraSample last;

	//! The range of the camera's place relative to the body, cm (x, y, z)
	Vec3f offsetRange() const { return maxOffset - minOffset; }
	//! How far the camera's height moved in all, cm
	float cameraRangeY() const { return maxCameraY - minCameraY; }
	//! How far the body's height moved in all, cm
	float bodyRangeY() const { return maxBodyY - minBodyY; }
};

void resetCameraMotion(CameraMotion & motion);
void addCameraSample(CameraMotion & motion, const CameraSample & sample);

} // namespace vr

#endif // ARX_VR_STABLEVIEW_H
