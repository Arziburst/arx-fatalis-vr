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

#ifndef ARX_VR_ARMMESH_H
#define ARX_VR_ARMMESH_H

#include <array>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/FingerCurl.h"

/*!
 * \file
 * \brief The mesh core of the VR arms (#385): pure, host-tested in tests/vr/ArmMeshTest.cpp
 *
 * No engine globals, no renderer, no file access: a skinned mesh, a glTF loader that reads one from
 * a memory buffer (cgltf, MIT), weighted CPU skinning with up to four joints per vertex, and a
 * procedural low-poly sleeve from the shoulder to the wrist. Drawing it is the body module's job.
 *
 * The hands are the Godot XR Tools low-poly glove hands (data/core/vr/arms/Hand_Glove_low_L/R.gltf,
 * CC0, research rows F-219 / F-274). The game reads them through its resource system as
 * \c vr/arms/Hand_Glove_low_R.gltf: data/core/ is a resource directory (the build points
 * \c data.dirs at it, the installer and the tester package copy it). The library hand (the arms model
 * \c library, #467.1) is the WebXR generic hand, binary glTF: data/core/vr/hands/webxr/left.glb and
 * right.glb (MIT).
 *
 * Skinning (research rows F-279, F-280): a vertex is the weighted sum of its joints' skin matrices
 * (the joint's world matrix times its inverse bind matrix) applied to the rest vertex; the normal is
 * the same blend applied to the rest normal, renormalised.
 */
namespace vr {

//! One joint of a skinned mesh's skeleton
struct MeshJoint {
	std::string name;     //!< as in the file (Wrist_R, Thumb_Metacarpal_R, ...)
	int parent = -1;      //!< index into the skeleton, -1 for a root
	glm::mat4 restLocal = glm::mat4(1.f);   //!< rest transform relative to the parent (a root: in mesh space)
	glm::mat4 inverseBind = glm::mat4(1.f); //!< mesh space to the joint's rest space
};

/*!
 * \brief A triangle mesh skinned to a skeleton, every vertex on up to four weighted joints
 *
 * All per-vertex arrays have the same length; the weights of a vertex sum to 1. Triangles are
 * counter-clockwise seen from outside (glTF's front face). The skeleton keeps the file's joint order,
 * in which a parent may come after its children: \c restWorldMatrices composes them.
 */
struct SkinnedMesh {
	std::vector<Vec3f> positions;
	std::vector<Vec3f> normals;
	std::vector<Vec2f> uvs;
	std::vector<std::array<u8, 4>> joints;     //!< skeleton indices; an unused slot has weight 0
	std::vector<std::array<float, 4>> weights;
	std::vector<u16> indices;                  //!< three per triangle
	std::vector<MeshJoint> skeleton;
};

/*!
 * \brief Read the first skinned mesh of a glTF 2.0 file (JSON with embedded buffers, or GLB) from memory
 *
 * Positions, normals (computed when the file has none), the first UV set, \c JOINTS_0 /
 * \c WEIGHTS_0 (normalised to sum 1), the triangles of every primitive, and the skin's joints with
 * their names, parents, rest transforms and inverse bind matrices. Units stay as in the file
 * (glTF: meters; the glove hands are about 0.2 m long). Never touches the file system: a buffer
 * outside the file (an external URI) is an error. A hand skeleton the file keeps flat is chained
 * (\ref chainHandSkeleton).
 *
 * \return false with a reason in \a error for anything that is not such a file; \a out is then unspecified
 */
bool loadGltfSkinnedMesh(const char * data, size_t size, SkinnedMesh & out, std::string & error);

//! The skeleton index of the joint named \a name exactly, or -1
int findJoint(const SkinnedMesh & mesh, std::string_view name);

/*!
 * The skeleton index of an \c XR_EXT_hand_tracking joint: the joint whose name, without a side
 * suffix (\c _L / \c _R), is the joint's name in the spec (Palm, Wrist, Thumb_Metacarpal, ...,
 * Little_Tip), or whose name is the WebXR hand input's (wrist, thumb-metacarpal, thumb-phalanx-proximal,
 * ..., index-finger-phalanx-intermediate, ..., pinky-finger-tip: the WebXR generic hand, #467.1); -1 when
 * the mesh has none. WebXR has no palm joint: a hand without one gives its middle finger's metacarpal for
 * the palm (the joint whose bone the OpenXR palm is the centre of).
 */
int findHandJoint(const SkinnedMesh & mesh, HandJoint joint);

/*!
 * \brief Chain a hand skeleton whose joints are all roots, as \c XR_EXT_hand_tracking chains them (#467.1)
 *
 * The wrist stays the root; each metacarpal (and a palm of its own) becomes the wrist's child and every
 * other finger joint the child of the one before it, its \c restLocal made relative to that parent (the
 * rest pose and the inverse bind matrices do not change). The WebXR generic hand keeps its 25 joints side
 * by side under one armature node; chained, a finger bends as one. \ref loadGltfSkinnedMesh does this.
 *
 * \return false, changing nothing, unless every joint is a root and every hand joint is found (\ref findHandJoint)
 */
bool chainHandSkeleton(SkinnedMesh & mesh);

//! The joints' world (mesh space) matrices at rest: every joint's \c restLocal composed with its parents'
void restWorldMatrices(const SkinnedMesh & mesh, std::vector<glm::mat4> & world);

/*!
 * \brief Weighted CPU skinning
 *
 * \param jointWorld one world matrix per skeleton joint (the rest ones reproduce the rest mesh)
 * \param positions  receives the skinned positions, one per vertex
 * \param normals    receives the skinned normals, unit length
 */
void skinMesh(const SkinnedMesh & mesh, const std::vector<glm::mat4> & jointWorld,
              std::vector<Vec3f> & positions, std::vector<Vec3f> & normals);

/*!
 * \brief Smooth normals of \a positions (the mesh's vertices moved, e.g. skinned), unit length
 *
 * Each vertex gets the area-weighted sum of its triangles' normals. Vertices at the same rest
 * position (a UV seam) share one normal, so a seam does not show in the shading.
 */
void recomputeNormals(const SkinnedMesh & mesh, const std::vector<Vec3f> & positions, std::vector<Vec3f> & normals);

/*!
 * \brief The joints of the procedural sleeve; a sleeve's skeleton index is this value
 *
 * A chain along +X, each the parent of the next, all with the identity rotation at rest:
 *
 *  joint               | at x (rest)                       | drives
 *  --------------------|-----------------------------------|----------------------------------------
 *  SleeveShoulder      | 0                                 | the top ring (sunk into the torso)
 *  SleeveUpperTwist    | upperTwistAt * upperLength        | the upper arm, full upper-arm twist
 *  SleeveElbow         | upperLength                       | the forearm at the elbow, none of the wrist's roll
 *  SleeveForearmTwist1 | upperLength + forearmLength / 3   | a third of the wrist's roll
 *  SleeveForearmTwist2 | upperLength + forearmLength * 2/3 | two thirds of the wrist's roll
 *  SleeveWrist         | upperLength + forearmLength       | the last ring, the full wrist roll
 *
 * The weights blend linearly from one joint's ring to the next along the chain, except across the
 * elbow: there the upper twist hands over to the elbow over \c elbowBlend units each side of the
 * elbow, so the ring at the elbow is 50/50 and the rings \c elbowRingOffset each side of it are mostly
 * their own side (research rows F-310, F-311). The body module poses the joints itself (it knows
 * the shoulder, elbow and wrist, and the wrist's roll): the bend is about the elbow's local Z with the
 * forearm folding toward +Y, so +Y is the arm's inner side and -Y its outer side (the point of the
 * elbow).
 */
enum SleeveJoint {
	SleeveShoulder,
	SleeveUpperTwist,
	SleeveElbow,
	SleeveForearmTwist1,
	SleeveForearmTwist2,
	SleeveWrist,
	SleeveJointCount
};

/*!
 * \brief The shape of the procedural sleeve, lengths and radii in game units (about a centimetre)
 *
 * The default lengths are the Arx player's bones (research row F-233). The last ring is sized to meet
 * the glove hand's cuff with no gap; the join stays a plain visible seam, nothing widens to hide it.
 *
 * The glove's cuff (Hand_Glove_low_L/R.gltf, meters, the hand pointing to -Z): its opening is an
 * ellipse 4.8 cm across mesh X (the hand's thickness) by 6.4 cm across mesh Y (its width), centred
 * within 0.4 cm of the Wrist joint's axis, at z = 2.6 cm, 0.14 cm toward the fingers from the Wrist
 * joint (z = 2.7 cm). So the defaults end the sleeve in an ellipse of 3.2 units across Z (the width)
 * by 2.4 across Y (the thickness, the inner and outer side): with the hand's Wrist joint on the
 * sleeve's, the hand's width along the sleeve's Z and its thickness along the sleeve's Y, the two
 * openings meet.
 */
struct SleeveParams {
	float upperLength = 27.6f;   //!< shoulder to elbow
	float forearmLength = 30.5f; //!< elbow to wrist
	int rings = 16;              //!< rings from the shoulder to the wrist (at least the 8 the joints need)
	int sides = 14;              //!< sides of each ring (12 to 16 for the low-poly look, F-312)
	float topFlare = 1.3f;       //!< the top ring is this much wider than shoulderRadius, to sink into the torso
	float shoulderRadius = 5.f;  //!< at the upper twist ring
	float elbowRadius = 3.8f;
	float forearmRadius = 4.f;   //!< at the first forearm twist ring
	float wristRadius = 3.2f;    //!< the last ring across Z (the hand's width)
	float wristFlatten = 0.75f;  //!< the last ring across Y is wristRadius times this (the hand's thickness)
	float upperTwistAt = 0.25f;  //!< where the upper twist joint sits, as a fraction of upperLength
	float elbowBlend = 4.f;      //!< the elbow's weight blend reaches this far each side of the elbow
	float elbowRingOffset = 2.5f; //!< the rings each side of the elbow ring (F-310: 2 to 3 cm)
	/*!
	 * Where the sleeve starts along +X (#385.8): 0 at the shoulder (the open top sunk into the torso); past 0 the
	 * sleeve is cut there - a short forearm, or at upperLength + forearmLength only the wrist's ring - and the cut
	 * is closed by a cuff
	 */
	float startAt = 0.f;
	float cuffFlare = 1.1f;      //!< the cuff's rim is this much wider than the cut ring (a first proposal)
	float cuffDepth = 1.f;       //!< the cuff closes this far past the cut, towards the shoulder
};

/*!
 * \brief Build the low-poly sleeve
 *
 * An open tube along +X from the shoulder joint at the origin to the wrist at
 * upperLength + forearmLength. Rings sit at every joint, at the elbow and \c elbowRingOffset each
 * side of it; the rest are spread over the longest gaps. Each ring has sides + 1 vertices: the
 * first and the last are the same point (the UV seam), which runs along the inner side (+Y), the
 * side least seen. The rings are round down to the first forearm twist ring and flatten from there
 * to the last ring's ellipse (wristFlatten).
 *
 * UVs: the whole texture is the sleeve. U goes once around the arm from the seam on the inner side
 * (+Y, U = 0) through the front (-Z, U = 0.25), the outer side (-Y, the elbow's point, U = 0.5) and
 * the back (+Z, U = 0.75) to the seam again (U = 1). V goes along the arm, 0 at the shoulder's top
 * ring to 1 at the wrist, proportional to the length (the elbow at V = upperLength / (upperLength +
 * forearmLength), about 0.475 by default).
 *
 * Cut (\c startAt past 0, #385.8): the rings start at \c startAt, and three more rings close it towards the
 * shoulder - the rim (\c cuffFlare wider, at 0.4 of \c cuffDepth), a ring at 0.6 of the radius (0.8 of the depth)
 * and the centre on the axis (the whole depth). They are weighted and textured as the cut ring, at its V.
 */
SkinnedMesh buildSleeve(const SleeveParams & params);

/*!
 * \brief The elbow's corrective shape, in rest space before skinning (research row F-310)
 *
 * The bend \a bendDegrees (0 straight, up to 180) pushes the outer side of the elbow (-Y) out by
 * up to k * sin(bend / 2) and pulls the inner side (+Y) toward the arm's axis by as much, where the
 * fold closes. Each vertex moves by how much it is shared between the upper arm and the forearm
 * (4 * upper share * forearm share: 1 on the elbow ring, 0 outside the elbow's blend) and by how
 * far round it is on its side. Vertices outside the elbow's blend do not move.
 *
 * \param positions the sleeve's rest positions (or ones already corrected); changed in place
 */
void applyElbowCorrective(const SkinnedMesh & sleeve, float bendDegrees, float k, std::vector<Vec3f> & positions);

} // namespace vr

#endif // ARX_VR_ARMMESH_H
