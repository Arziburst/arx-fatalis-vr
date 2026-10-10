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

#ifndef ARX_VR_ARMROLL_H
#define ARX_VR_ARMROLL_H

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "vr/ArmIK.h"

/*!
 * \file
 * \brief Roll-aware arm IK (#304, D-NEW): where the hand's roll goes along the arm
 *
 * Pure math on top of \ref solveArm, host-tested in tests/vr/ArmRollTest.cpp: no engine globals,
 * no OpenXR, no clock. The body module (vr/VRBody.cpp) feeds it the shoulder, the wrist target and
 * the hand's world rotation each frame and writes the three bone rotations back into the skeleton.
 *
 * The engine skins one bone per vertex, so every degree of roll between two neighbouring bones is
 * drawn by the one band of faces between them (research section 16a): a hand rolled by 180 degrees
 * on a forearm that keeps the animation's roll wrings the wrist band into a flat ribbon. The rules
 * here are ported from RENIK's limb solver (V-Sekai/renik, MIT, see LICENSE.renik), the same rules
 * that make Valheim VR's arms look right (research row F-212):
 *  - the three rotations are built from the bend plane, not on top of the animation: a bone's roll
 *    about its own axis is whatever puts its rest bend reference into the current bend plane;
 *  - the hand's roll about the forearm is measured against that neutral forearm, and the roll past
 *    a half turn keeps its sign until the overflow and then flips to the complement (RENIK's
 *    inflection point and \c twist_overflow, with hysteresis);
 *  - the elbow swings about the shoulder-target line by a share of the hand's roll (RENIK's
 *    \c target_rotation_influence), so a hand rolled far moves the elbow instead of the mesh;
 *  - the forearm takes a share of what is left (\c lower_limb_twist) and the upper arm a share of
 *    the forearm's (\c upper_limb_twist); the wrist band shows the rest.
 * The shares are named constants here and cfg.ini keys (\c vr_arm_*), so the owner can tune them
 * in the headset; the VR settings screen (#178) exposes them later.
 */
namespace vr {

//! The forearm's share of the hand's roll off the bend plane (RENIK's lower_limb_twist, two thirds)
constexpr float DefaultForearmTwist = 0.667f;
//! The upper arm's share of the forearm's roll (RENIK's upper_limb_twist)
constexpr float DefaultUpperTwist = 0.5f;
//! The share of the hand's roll that swings the elbow about the shoulder-target line (RENIK's target_rotation_influence)
constexpr float DefaultElbowRoll = 0.33f;
//! Degrees past a half turn the roll keeps its sign before flipping to the complement (RENIK's twist_overflow)
constexpr float DefaultTwistOverflow = 45.f;
constexpr float MinTwistOverflow = 0.f;
constexpr float MaxTwistOverflow = 90.f;

//! How the hand's roll is shared along the arm
struct ArmRollParams {
	float forearmShare = DefaultForearmTwist;  //!< [0, 1] of the residual roll, on the forearm
	float upperShare = DefaultUpperTwist;      //!< [0, 1] of the forearm's roll, on the upper arm
	float elbowShare = DefaultElbowRoll;       //!< [0, 1] of the hand's roll, swung into the elbow
	float overflowDegrees = DefaultTwistOverflow; //!< [0, 90]
};

/*!
 * \brief The arm chain at rest, in the model's mesh space - the frame a bone's world rotation maps
 *        to the world (the engine's \c vertexlocal are rest vectors from the joint)
 */
struct ArmRest {
	Vec3f upper = Vec3f(0.f);   //!< the elbow joint from the shoulder joint
	Vec3f forearm = Vec3f(0.f); //!< the wrist joint from the elbow joint
	//! the hand's lateral axis towards the thumb (HandFrame::thumb): the bend reference of a rest arm
	//! too straight to have a bend of its own - at neutral pronation the elbow is opposite the thumb
	//! (human_base needs neither: its rest arm bends 10 degrees, the elbow back, the thumb forward)
	Vec3f thumb = Vec3f(0.f);
};

//! The overflow side the roll is on: 0 none, +1 past +180 (continuing positive), -1 past -180
struct ArmRollState {
	int overflow = 0;
};

//! The posed arm with its rotations and where the roll went (degrees)
struct ArmRollPose {
	ArmPose pose;                           //!< the joints, from \ref solveArm with the swung elbow
	glm::quat upper = quat_identity();      //!< world rotation of the upper arm bone (rest -> world)
	glm::quat forearm = quat_identity();    //!< ... of the forearm bone
	glm::quat hand = quat_identity();       //!< ... of the hand bone: the given one
	float handRoll = 0.f;     //!< the hand's roll about the forearm against the base bend plane, unwrapped past a half turn
	float elbowSwing = 0.f;   //!< how far the elbow was swung about the shoulder-target line (elbowShare * handRoll)
	float residualRoll = 0.f; //!< the hand's roll against the final bend plane
	float forearmRoll = 0.f;  //!< the forearm's roll off its bend plane (forearmShare * residualRoll)
	float upperRoll = 0.f;    //!< the upper arm's roll off its bend plane (upperShare * forearmRoll)
	float wristTwist = 0.f;   //!< residualRoll - forearmRoll: what the band between the forearm and the hand shows
	float elbowTwist = 0.f;   //!< forearmRoll - upperRoll: the band at the elbow; the shoulder band shows upperRoll
	int overflow = 0;         //!< the state after this solve
	const char * restElbowSource = ""; //!< what named the rest bend reference, see \ref restElbowDirection
};

/*!
 * \return the rest bend reference of the chain: the direction the elbow sticks out at rest, across
 *         the shoulder-wrist line. A unit vector. In this order, the first that applies: the rest
 *         triangle when the arm bends by more than a degree or two ("the rest triangle"), opposite
 *         the thumb ("opposite the thumb"), any perpendicular ("any"); \a source gets the name.
 */
Vec3f restElbowDirection(const ArmRest & rest, const char ** source = nullptr);

/*!
 * \brief The twist of a rotation about an axis: the roll component of its swing-twist split, degrees in (-180, 180]
 */
float twistAboutAxis(const glm::quat & q, const Vec3f & axis);

/*!
 * \brief RENIK's overflow rule: a roll keeps its sign past a half turn until \a overflowDegrees beyond it
 *
 * \param rollDegrees the roll as measured, in (-180, 180]
 * \param state       the side the roll is continuing on; updated
 * \return the roll in (-180 - overflow, 180 + overflow]
 */
float unwrapRoll(float rollDegrees, float overflowDegrees, ArmRollState & state);

/*!
 * \brief Pose an arm with the hand's roll shared along it
 *
 * \param shoulder      the shoulder joint (world)
 * \param target        where the wrist should be (world)
 * \param hand          the hand bone's world rotation (rest -> world): what the controller says
 * \param upperLength   shoulder to elbow (> 0)
 * \param forearmLength elbow to wrist (> 0)
 * \param elbowHint     the base direction the elbow bends towards (the body's down, out, back) before the hand's roll swings it
 * \param rest          the chain at rest in mesh space
 * \param ik            the slide limit of \ref solveArm
 * \param params        the shares
 * \param state         the overflow state of this arm, kept between frames
 */
ArmRollPose solveArmRoll(const Vec3f & shoulder, const Vec3f & target, const glm::quat & hand,
                         float upperLength, float forearmLength, const Vec3f & elbowHint, const ArmRest & rest,
                         const ArmIKParams & ik, const ArmRollParams & params, ArmRollState & state);

} // namespace vr

#endif // ARX_VR_ARMROLL_H
