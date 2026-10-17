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

#ifndef ARX_VR_ARXARMFIX_H
#define ARX_VR_ARXARMFIX_H

#include <array>
#include <stddef.h>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief The classic Arx arm fixed (#466.1): one bend at the elbow, the forearm one rigid piece
 *
 * Pure math, host-tested in tests/vr/ArxArmFixTest.cpp: no engine globals. The body module (vr/VRBody.cpp) hands
 * it one Arx arm at rest - the three joints and every vertex of the model with the part of the arm it is skinned to -
 * once per bound model, and the posed bones every frame.
 *
 * The diagnosis (\ref diagnoseArxArm) reads the arm along its own axis: the vertices of each group, how far past the
 * elbow joint the upper arm's vertices reach and where the forearm's begin, the rings of vertices from the shoulder to
 * the wrist, and the faces whose corners are in two parts of the arm (the band at the elbow, the band at the wrist,
 * and faces joining parts that are not neighbours).
 *
 * What human_base's arms are (measured, docs/vr/TRAPS.md 2026-10-14): the forearm group holds only the forearm's
 * first third (11 vertices, 2.5 .. 11.3 units past the elbow of 30.5); its other two thirds are in the wrist group
 * (8 vertices, 10.7 .. 20.1), so every bend of the wrist and the wrist band's share of the roll fold the forearm in
 * its middle - the second elbow - and one of them is joined straight to the upper arm by 3 faces (a web); the ring
 * at the elbow mixes 6 upper-arm vertices (up to 3.1 past the joint) with 4 of the forearm.
 *
 * The classic skin (\ref classicArmParts) re-assigns the arm's vertices by the ring they are in, not by their group:
 * the rings before the elbow ring stay on the upper arm, the elbow ring and every ring after it up to the wrist ring
 * are the forearm's (the upper arm's vertices past the joint and the wrist group's in the forearm among them), and
 * the wrist ring with the hand stays the hand's. The arm then folds in one band, just above the elbow joint, and the
 * forearm is one rigid piece: its roll is the forearm bone's, the wrist band and the elbow band take the rest.
 *
 * The measures (\ref measureArm) read any posed arm along the same rings: the bands bent by more than
 * \ref CreaseThreshold, and the spread of the forearm rings' roll about the forearm.
 */
namespace vr {

//! What a vertex of the player model is to one arm: the body, or the part of the arm its group belongs to
enum class ArmPart : u8 {
	Body,
	Upper,   //!< the upper arm group (and any other group under it but the forearm's)
	Forearm, //!< the forearm group (and any other group under it but the wrist's)
	Hand     //!< the wrist group and everything under it (the hand, the fingers)
};

//! One Arx arm at rest, in the model's mesh space
struct ArxArmRest {
	Vec3f shoulder = Vec3f(0.f);
	Vec3f elbow = Vec3f(0.f);
	Vec3f wrist = Vec3f(0.f);
	std::vector<Vec3f> positions;                //!< every vertex of the model
	std::vector<ArmPart> parts;                  //!< per vertex
	std::vector<std::array<size_t, 3>> faces;    //!< every face of the model (vertex indices)
};

//! A ring of the arm: vertices at about the same distance along it
struct ArmRing {
	float along = 0.f;       //!< the mean distance along the arm from the shoulder joint (units)
	float alongMin = 0.f;
	float alongMax = 0.f;
	Vec3f centre = Vec3f(0.f);
	float wide = 0.f;        //!< the cross-section's extent across the arm, the wide way and the thin way (units)
	float thin = 0.f;
	std::array<size_t, 4> parts = { };  //!< vertices per ArmPart
	std::vector<size_t> vertices;
};

//! The arm's vertices of one part along the arm
struct ArmPartSpan {
	size_t vertices = 0;
	float alongMin = 0.f;    //!< from the shoulder joint (units)
	float alongMax = 0.f;
};

//! What the diagnosis found on one Arx arm (#466.1, research F-268, F-308)
struct ArxArmDiagnosis {
	bool valid = false;
	float upperLength = 0.f;
	float forearmLength = 0.f;
	std::array<ArmPartSpan, 4> parts;  //!< per ArmPart (Body unused)
	//! the upper arm's vertices past the elbow joint along the upper arm's axis, and the farthest (units)
	size_t upperPastElbow = 0;
	float upperPastElbowMax = 0.f;
	//! where the forearm's vertices begin and end past the elbow joint, along the forearm's axis (units)
	float forearmStart = 0.f;
	float forearmEnd = 0.f;
	//! the hand's vertices before the wrist joint, along the forearm's axis, and the farthest back (units before it)
	size_t handBeforeWrist = 0;
	float handBeforeWristMax = 0.f;
	//! faces by the parts of the arm their corners are in
	size_t facesUpperForearm = 0;      //!< the band at the elbow
	size_t facesForearmHand = 0;       //!< the band at the wrist
	size_t facesUpperHand = 0;         //!< parts that are not neighbours (H2: a web across the forearm when bent)
	size_t facesBodyForearm = 0;       //!< ... the body and the forearm or the hand
	std::vector<ArmRing> rings;        //!< from the shoulder to the wrist ring, in order
	size_t elbowRing = 0;              //!< the ring nearest the elbow joint
	size_t wristRing = 0;              //!< the ring nearest the wrist joint
	size_t flatUpperRings = 0;         //!< upper-arm rings thinner than FlatRingRatio of their width (H1)
	size_t mixedRings = 0;             //!< rings with vertices of two parts of the arm
};

//! Vertices further apart than this along the arm start a new ring (units)
constexpr float RingGap = 1.5f;
//! A ring past the wrist joint by more than this is the hand's, not the arm's (units)
constexpr float WristRingReach = 3.f;
//! An upper-arm ring is flat when its thin extent is below this share of its wide one
constexpr float FlatRingRatio = 0.6f;

//! \return the arm read along its axis; invalid for a degenerate chain or an arm without vertices
ArxArmDiagnosis diagnoseArxArm(const ArxArmRest & rest);

//! The diagnosis in one line, for the log
std::string diagnosisText(const ArxArmDiagnosis & diagnosis);

/*!
 * \return the part each vertex of the model is skinned to under the classic arm: the vertices of the rings before
 *         the elbow ring the upper arm's, of the elbow ring up to the ring before the wrist ring the forearm's, the
 *         rest as \a rest has them (the body, the wrist ring, the hand). \a rest's parts for an invalid diagnosis
 */
std::vector<ArmPart> classicArmParts(const ArxArmRest & rest, const ArxArmDiagnosis & diagnosis);

//! A band of the arm bent by more than this is a crease (degrees): the elbow of a bent arm is one
constexpr float CreaseThreshold = 20.f;

//! How a posed arm bends along its rings (\ref measureArm)
struct ArmBend {
	size_t creases = 0;            //!< bands between two rings, shoulder to wrist band, bent past the threshold
	float maxBend = 0.f;           //!< the most any such band is bent (degrees)
	float elbowBend = 0.f;         //!< the forearm bone against the upper arm bone, the twist aside (degrees)
	float forearmTwistSpread = 0.f; //!< the roll about the forearm of the rings past the elbow joint, most less least (degrees)
	std::string bands;             //!< the creases as "ring>ring bend", for the log and status
};

/*!
 * \brief How a posed arm bends along the rings of \a diagnosis
 *
 * A ring turns as the mean of its vertices' skinning rotations: \a bones holds the world rotation of the upper arm,
 * the forearm and the hand (rest -> world) and \a parts the part each vertex is skinned to. A band between two
 * neighbouring rings is bent by its rings' relative rotation without the twist about the arm's axis there. The bands
 * run from the first ring to the ring before the wrist ring: the wrist band is the hand's joint, not the arm's.
 */
ArmBend measureArm(const ArxArmRest & rest, const ArxArmDiagnosis & diagnosis, const std::vector<ArmPart> & parts,
                   const std::array<glm::quat, 3> & bones, float threshold = CreaseThreshold);

//! A vertex skinned on one bone as the engine does it: the bone's rotation of its scaled offset from the bone's joint
Vec3f skinOnBone(const glm::quat & rotation, const Vec3f & joint, const Vec3f & scale, const Vec3f & local);

} // namespace vr

#endif // ARX_VR_ARXARMFIX_H
