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
 * a memory buffer (cgltf, MIT) and weighted CPU skinning with up to four joints per vertex. Drawing it
 * is the body module's job.
 *
 * The hands are the Godot XR Tools low-poly glove hands (data/core/vr/arms/Hand_Glove_low_L/R.gltf,
 * CC0, research rows F-219 / F-274). The game reads them through its resource system as
 * \c vr/arms/Hand_Glove_low_R.gltf: data/core/ is a resource directory (the build points
 * \c data.dirs at it, the installer and the tester package copy it).
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
 * outside the file (an external URI) is an error.
 *
 * \return false with a reason in \a error for anything that is not such a file; \a out is then unspecified
 */
bool loadGltfSkinnedMesh(const char * data, size_t size, SkinnedMesh & out, std::string & error);

//! The skeleton index of the joint named \a name exactly, or -1
int findJoint(const SkinnedMesh & mesh, std::string_view name);

/*!
 * The skeleton index of an \c XR_EXT_hand_tracking joint: the joint whose name, without a side
 * suffix (\c _L / \c _R), is the joint's name in the spec (Palm, Wrist, Thumb_Metacarpal, ...,
 * Little_Tip); -1 when the mesh has none
 */
int findHandJoint(const SkinnedMesh & mesh, HandJoint joint);

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

} // namespace vr

#endif // ARX_VR_ARMMESH_H
