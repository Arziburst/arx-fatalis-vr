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

#include "vr/ArmMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// cgltf (MIT, vcpkg) is a single header; its implementation lives in this file only
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace vr {

namespace {

const char * gltfResultText(cgltf_result result) {
	switch(result) {
		case cgltf_result_success: return "success";
		case cgltf_result_data_too_short: return "the data is too short";
		case cgltf_result_unknown_format: return "not a glTF file";
		case cgltf_result_invalid_json: return "invalid JSON";
		case cgltf_result_invalid_gltf: return "invalid glTF";
		case cgltf_result_invalid_options: return "invalid options";
		case cgltf_result_file_not_found: return "a referenced file was not found";
		case cgltf_result_io_error: return "an I/O error";
		case cgltf_result_out_of_memory: return "out of memory";
		case cgltf_result_legacy_gltf: return "a glTF 1.0 file";
		default: return "unknown error";
	}
}

struct GltfFree {
	void operator()(cgltf_data * data) const { cgltf_free(data); }
};

glm::mat4 gltfNodeWorld(const cgltf_node * node) {
	glm::mat4 world;
	cgltf_node_transform_world(node, glm::value_ptr(world));
	return world;
}

//! The XR_EXT_hand_tracking joint names in XrHandJointEXT order, as the Godot XR Tools hands spell them
const char * const GltfHandJointNames[HandJointCount] = {
	"Palm", "Wrist",
	"Thumb_Metacarpal", "Thumb_Proximal", "Thumb_Distal", "Thumb_Tip",
	"Index_Metacarpal", "Index_Proximal", "Index_Intermediate", "Index_Distal", "Index_Tip",
	"Middle_Metacarpal", "Middle_Proximal", "Middle_Intermediate", "Middle_Distal", "Middle_Tip",
	"Ring_Metacarpal", "Ring_Proximal", "Ring_Intermediate", "Ring_Distal", "Ring_Tip",
	"Little_Metacarpal", "Little_Proximal", "Little_Intermediate", "Little_Distal", "Little_Tip",
};

//! \a name without a side suffix (_L, _R, .L, .R)
std::string_view withoutSideSuffix(std::string_view name) {
	if(name.size() > 2 && (name[name.size() - 2] == '_' || name[name.size() - 2] == '.')
	   && (name.back() == 'L' || name.back() == 'R')) {
		name.remove_suffix(2);
	}
	return name;
}

//! The same joints as the WebXR hand input names them (#467.1, the WebXR generic hand); WebXR has no palm joint
const char * const WebXRHandJointNames[HandJointCount] = {
	"", "wrist",
	"thumb-metacarpal", "thumb-phalanx-proximal", "thumb-phalanx-distal", "thumb-tip",
	"index-finger-metacarpal", "index-finger-phalanx-proximal", "index-finger-phalanx-intermediate",
	"index-finger-phalanx-distal", "index-finger-tip",
	"middle-finger-metacarpal", "middle-finger-phalanx-proximal", "middle-finger-phalanx-intermediate",
	"middle-finger-phalanx-distal", "middle-finger-tip",
	"ring-finger-metacarpal", "ring-finger-phalanx-proximal", "ring-finger-phalanx-intermediate",
	"ring-finger-phalanx-distal", "ring-finger-tip",
	"pinky-finger-metacarpal", "pinky-finger-phalanx-proximal", "pinky-finger-phalanx-intermediate",
	"pinky-finger-phalanx-distal", "pinky-finger-tip",
};

//! The skeleton index of the joint named \a joint's name in either spelling, without the palm's substitute; -1 if none
int findNamedHandJoint(const SkinnedMesh & mesh, HandJoint joint) {
	std::string_view godot = GltfHandJointNames[size_t(joint)];
	std::string_view webxr = WebXRHandJointNames[size_t(joint)];
	for(size_t i = 0; i < mesh.skeleton.size(); i++) {
		std::string_view name = mesh.skeleton[i].name;
		if(withoutSideSuffix(name) == godot || (!webxr.empty() && name == webxr)) {
			return int(i);
		}
	}
	return -1;
}

/*!
 * The parent of \a joint in the XR_EXT_hand_tracking chain: a metacarpal's (and the palm's) is the wrist, a finger
 * joint's the one before it; the wrist has none
 */
HandJoint chainedHandParent(HandJoint joint) {
	if(joint == HandJoint::Wrist) {
		return HandJoint::Count;
	}
	if(joint == HandJoint::Palm || joint == HandJoint::ThumbMetacarpal) {
		return HandJoint::Wrist;
	}
	if(joint > HandJoint::ThumbMetacarpal && joint <= HandJoint::ThumbTip) {
		return HandJoint(size_t(joint) - 1);
	}
	const size_t inFinger = (size_t(joint) - size_t(HandJoint::IndexMetacarpal)) % 5;
	return inFinger == 0 ? HandJoint::Wrist : HandJoint(size_t(joint) - 1);
}

bool readGltfPrimitive(const cgltf_primitive & primitive, size_t jointCount, SkinnedMesh & out, std::string & error) {

	const cgltf_accessor * position = nullptr;
	const cgltf_accessor * normal = nullptr;
	const cgltf_accessor * uv = nullptr;
	const cgltf_accessor * joints = nullptr;
	const cgltf_accessor * weights = nullptr;
	for(size_t i = 0; i < primitive.attributes_count; i++) {
		const cgltf_attribute & attribute = primitive.attributes[i];
		switch(attribute.type) {
			case cgltf_attribute_type_position: position = attribute.data; break;
			case cgltf_attribute_type_normal: normal = attribute.data; break;
			case cgltf_attribute_type_texcoord: if(attribute.index == 0) { uv = attribute.data; } break;
			case cgltf_attribute_type_joints: if(attribute.index == 0) { joints = attribute.data; } break;
			case cgltf_attribute_type_weights: if(attribute.index == 0) { weights = attribute.data; } break;
			default: break;
		}
	}
	if(!position || !joints || !weights) {
		error = "a primitive without POSITION, JOINTS_0 or WEIGHTS_0";
		return false;
	}

	const size_t base = out.positions.size();
	const size_t count = position->count;
	if(base + count > 65536) {
		error = "more than 65536 vertices";
		return false;
	}
	if((normal && normal->count != count) || (uv && uv->count != count)
	   || joints->count != count || weights->count != count) {
		error = "vertex attributes of different lengths";
		return false;
	}

	for(size_t v = 0; v < count; v++) {

		float p[3] = { 0.f, 0.f, 0.f };
		cgltf_accessor_read_float(position, v, p, 3);
		out.positions.emplace_back(p[0], p[1], p[2]);

		float n[3] = { 0.f, 0.f, 0.f };
		if(normal) {
			cgltf_accessor_read_float(normal, v, n, 3);
		}
		out.normals.emplace_back(n[0], n[1], n[2]);

		float t[2] = { 0.f, 0.f };
		if(uv) {
			cgltf_accessor_read_float(uv, v, t, 2);
		}
		out.uvs.emplace_back(t[0], t[1]);

		cgltf_uint j[4] = { 0, 0, 0, 0 };
		float w[4] = { 0.f, 0.f, 0.f, 0.f };
		if(!cgltf_accessor_read_uint(joints, v, j, 4) || !cgltf_accessor_read_float(weights, v, w, 4)) {
			error = "unreadable JOINTS_0 or WEIGHTS_0";
			return false;
		}
		std::array<u8, 4> vertexJoints = { 0, 0, 0, 0 };
		std::array<float, 4> vertexWeights = { 0.f, 0.f, 0.f, 0.f };
		float sum = 0.f;
		for(int k = 0; k < 4; k++) {
			if(!(w[k] > 0.f)) {
				continue; // an unused slot, or a negative / NaN weight
			}
			if(j[k] >= jointCount || j[k] > 255) {
				error = "a vertex weighted to a joint the skin does not have";
				return false;
			}
			vertexJoints[k] = u8(j[k]);
			vertexWeights[k] = w[k];
			sum += w[k];
		}
		if(sum > 0.f) {
			for(float & weight : vertexWeights) {
				weight /= sum;
			}
		} else {
			vertexWeights[0] = 1.f; // an unweighted vertex follows the first joint
		}
		out.joints.push_back(vertexJoints);
		out.weights.push_back(vertexWeights);
	}

	if(primitive.indices) {
		if(primitive.indices->count % 3 != 0) {
			error = "a triangle list whose length is not a multiple of 3";
			return false;
		}
		for(size_t i = 0; i < primitive.indices->count; i++) {
			size_t index = cgltf_accessor_read_index(primitive.indices, i);
			if(index >= count) {
				error = "a triangle index outside the vertices";
				return false;
			}
			out.indices.push_back(u16(base + index));
		}
	} else {
		if(count % 3 != 0) {
			error = "a triangle list whose length is not a multiple of 3";
			return false;
		}
		for(size_t i = 0; i < count; i++) {
			out.indices.push_back(u16(base + i));
		}
	}

	return true;
}

//! Linear interpolation over keys sorted by x, clamped at the ends
float interpolateSleeveKeys(const std::vector<std::pair<float, float>> & keys, float x) {
	if(x <= keys.front().first) {
		return keys.front().second;
	}
	for(size_t i = 0; i + 1 < keys.size(); i++) {
		if(x <= keys[i + 1].first) {
			float span = keys[i + 1].first - keys[i].first;
			float t = span > 0.f ? (x - keys[i].first) / span : 1.f;
			return keys[i].second + (keys[i + 1].second - keys[i].second) * t;
		}
	}
	return keys.back().second;
}

} // anonymous namespace

bool loadGltfSkinnedMesh(const char * data, size_t size, SkinnedMesh & out, std::string & error) {

	out = SkinnedMesh();

	if(!data || size == 0) {
		error = "empty glTF buffer";
		return false;
	}

	cgltf_options options;
	std::memset(&options, 0, sizeof(options));
	cgltf_data * parsed = nullptr;
	cgltf_result result = cgltf_parse(&options, data, size, &parsed);
	std::unique_ptr<cgltf_data, GltfFree> gltf(parsed);
	if(result != cgltf_result_success) {
		error = std::string("glTF parse failed: ") + gltfResultText(result);
		return false;
	}

	// Only embedded buffers (data: URIs, or a GLB's own chunk): never the file system
	for(size_t i = 0; i < gltf->buffers_count; i++) {
		const char * uri = gltf->buffers[i].uri;
		if(uri && std::strncmp(uri, "data:", 5) != 0) {
			error = std::string("glTF buffer outside the file: ") + uri;
			return false;
		}
	}
	result = cgltf_load_buffers(&options, gltf.get(), nullptr);
	if(result != cgltf_result_success) {
		error = std::string("glTF buffers failed: ") + gltfResultText(result);
		return false;
	}
	result = cgltf_validate(gltf.get());
	if(result != cgltf_result_success) {
		error = std::string("glTF validation failed: ") + gltfResultText(result);
		return false;
	}

	const cgltf_node * meshNode = nullptr;
	for(size_t i = 0; i < gltf->nodes_count; i++) {
		if(gltf->nodes[i].mesh && gltf->nodes[i].skin) {
			meshNode = &gltf->nodes[i];
			break;
		}
	}
	if(!meshNode) {
		error = "no skinned mesh in the glTF file";
		return false;
	}
	const cgltf_skin & skin = *meshNode->skin;
	if(skin.joints_count == 0 || skin.joints_count > 256) {
		error = "a skin with no joints or more than 256";
		return false;
	}

	// The skeleton: the skin's joints in its order
	out.skeleton.resize(skin.joints_count);
	for(size_t i = 0; i < skin.joints_count; i++) {
		const cgltf_node * node = skin.joints[i];
		MeshJoint & joint = out.skeleton[i];
		joint.name = node->name ? node->name : "";
		joint.parent = -1;
		for(const cgltf_node * up = node->parent; up && joint.parent < 0; up = up->parent) {
			for(size_t k = 0; k < skin.joints_count; k++) {
				if(skin.joints[k] == up) {
					joint.parent = int(k);
					break;
				}
			}
		}
		// Relative to the parent joint, folding in any node between them (a root: in mesh space)
		glm::mat4 world = gltfNodeWorld(node);
		joint.restLocal = joint.parent < 0 ? world : glm::inverse(gltfNodeWorld(skin.joints[joint.parent])) * world;
		joint.inverseBind = glm::mat4(1.f);
		if(skin.inverse_bind_matrices) {
			cgltf_accessor_read_float(skin.inverse_bind_matrices, i, glm::value_ptr(joint.inverseBind), 16);
		}
	}

	bool haveNormals = true;
	const cgltf_mesh & mesh = *meshNode->mesh;
	for(size_t i = 0; i < mesh.primitives_count; i++) {
		const cgltf_primitive & primitive = mesh.primitives[i];
		if(primitive.type != cgltf_primitive_type_triangles) {
			continue;
		}
		bool primitiveHasNormals = false;
		for(size_t a = 0; a < primitive.attributes_count; a++) {
			primitiveHasNormals = primitiveHasNormals || primitive.attributes[a].type == cgltf_attribute_type_normal;
		}
		haveNormals = haveNormals && primitiveHasNormals;
		if(!readGltfPrimitive(primitive, skin.joints_count, out, error)) {
			return false;
		}
	}
	if(out.positions.empty() || out.indices.empty()) {
		error = "the skinned mesh has no triangles";
		return false;
	}

	// A hand whose joints the file keeps side by side (the WebXR hands: all children of one armature node) bends
	// a finger as one chain only once its joints are chained
	chainHandSkeleton(out);

	if(haveNormals) {
		for(Vec3f & normal : out.normals) {
			float length = glm::length(normal);
			normal = length > 0.f ? normal / length : Vec3f(0.f, 0.f, 1.f);
		}
	} else {
		std::vector<Vec3f> normals;
		recomputeNormals(out, out.positions, normals);
		out.normals = normals;
	}

	return true;
}

int findJoint(const SkinnedMesh & mesh, std::string_view name) {
	for(size_t i = 0; i < mesh.skeleton.size(); i++) {
		if(mesh.skeleton[i].name == name) {
			return int(i);
		}
	}
	return -1;
}

int findHandJoint(const SkinnedMesh & mesh, HandJoint joint) {
	if(size_t(joint) >= HandJointCount) {
		return -1;
	}
	const int found = findNamedHandJoint(mesh, joint);
	if(found < 0 && joint == HandJoint::Palm) {
		return findNamedHandJoint(mesh, HandJoint::MiddleMetacarpal);
	}
	return found;
}

bool chainHandSkeleton(SkinnedMesh & mesh) {

	for(const MeshJoint & joint : mesh.skeleton) {
		if(joint.parent >= 0) {
			return false;
		}
	}
	std::array<int, HandJointCount> index;
	for(size_t j = 0; j < HandJointCount; j++) {
		index[j] = findHandJoint(mesh, HandJoint(j));
		if(index[j] < 0) {
			return false;
		}
	}

	// Every joint is a root: its restLocal is its rest in mesh space
	std::vector<glm::mat4> world;
	world.reserve(mesh.skeleton.size());
	for(const MeshJoint & joint : mesh.skeleton) {
		world.push_back(joint.restLocal);
	}
	for(size_t j = 0; j < HandJointCount; j++) {
		const HandJoint parent = chainedHandParent(HandJoint(j));
		const size_t child = size_t(index[j]);
		const bool substitute = HandJoint(j) == HandJoint::Palm
		                        && index[j] == index[size_t(HandJoint::MiddleMetacarpal)];
		if(parent == HandJoint::Count || substitute) {
			continue; // the wrist, or the palm's substitute (chained as the middle metacarpal)
		}
		const size_t above = size_t(index[size_t(parent)]);
		mesh.skeleton[child].parent = int(above);
		mesh.skeleton[child].restLocal = glm::inverse(world[above]) * world[child];
	}
	return true;
}

void restWorldMatrices(const SkinnedMesh & mesh, std::vector<glm::mat4> & world) {

	const size_t count = mesh.skeleton.size();
	world.assign(count, glm::mat4(1.f));
	std::vector<u8> state(count, 0); // 0 to do, 1 on the stack, 2 done
	std::vector<size_t> chain;

	for(size_t i = 0; i < count; i++) {
		// Walk up to the first joint already done (or a root), then compose back down
		chain.clear();
		size_t j = i;
		while(state[j] == 0) {
			state[j] = 1;
			chain.push_back(j);
			int parent = mesh.skeleton[j].parent;
			if(parent < 0 || size_t(parent) >= count || state[size_t(parent)] == 1) {
				break; // a root, a bad index or a cycle: treated as a root
			}
			j = size_t(parent);
		}
		for(size_t c = chain.size(); c-- > 0;) {
			size_t k = chain[c];
			int parent = mesh.skeleton[k].parent;
			bool hasParent = parent >= 0 && size_t(parent) < count && state[size_t(parent)] == 2;
			world[k] = hasParent ? world[size_t(parent)] * mesh.skeleton[k].restLocal : mesh.skeleton[k].restLocal;
			state[k] = 2;
		}
	}
}

void skinMesh(const SkinnedMesh & mesh, const std::vector<glm::mat4> & jointWorld,
              std::vector<Vec3f> & positions, std::vector<Vec3f> & normals) {

	const size_t jointCount = std::min(mesh.skeleton.size(), jointWorld.size());
	std::vector<glm::mat4> skin(jointCount);
	for(size_t j = 0; j < jointCount; j++) {
		skin[j] = jointWorld[j] * mesh.skeleton[j].inverseBind;
	}

	const size_t count = mesh.positions.size();
	positions.resize(count);
	normals.resize(count);
	for(size_t v = 0; v < count; v++) {
		glm::mat4 blend(0.f);
		float used = 0.f;
		for(int k = 0; k < 4; k++) {
			float w = mesh.weights[v][k];
			size_t j = mesh.joints[v][k];
			if(w > 0.f && j < jointCount) {
				blend += skin[j] * w;
				used += w;
			}
		}
		if(!(used > 0.f)) {
			blend = glm::mat4(1.f);
		}
		positions[v] = Vec3f(blend * glm::vec4(mesh.positions[v], 1.f));
		Vec3f normal = glm::mat3(blend) * mesh.normals[v];
		float length = glm::length(normal);
		normals[v] = length > 1e-12f ? normal / length : mesh.normals[v];
	}
}

void recomputeNormals(const SkinnedMesh & mesh, const std::vector<Vec3f> & positions, std::vector<Vec3f> & normals) {

	const size_t count = std::min(mesh.positions.size(), positions.size());
	std::vector<Vec3f> sum(count, Vec3f(0.f));
	for(size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		size_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
		if(a >= count || b >= count || c >= count) {
			continue;
		}
		// The cross product's length is twice the area: larger triangles count more
		Vec3f face = glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
		sum[a] += face;
		sum[b] += face;
		sum[c] += face;
	}

	// Vertices at the same rest position (a UV seam) share their sums
	std::vector<size_t> order(count);
	for(size_t v = 0; v < count; v++) {
		order[v] = v;
	}
	auto lessRest = [&](size_t l, size_t r) {
		const Vec3f & p = mesh.positions[l];
		const Vec3f & q = mesh.positions[r];
		return p.x != q.x ? p.x < q.x : p.y != q.y ? p.y < q.y : p.z < q.z;
	};
	std::sort(order.begin(), order.end(), lessRest);
	for(size_t first = 0; first < count;) {
		size_t last = first + 1;
		while(last < count && mesh.positions[order[last]] == mesh.positions[order[first]]) {
			last++;
		}
		if(last - first > 1) {
			Vec3f shared(0.f);
			for(size_t i = first; i < last; i++) {
				shared += sum[order[i]];
			}
			for(size_t i = first; i < last; i++) {
				sum[order[i]] = shared;
			}
		}
		first = last;
	}

	normals.resize(count);
	for(size_t v = 0; v < count; v++) {
		float length = glm::length(sum[v]);
		if(length > 1e-12f) {
			normals[v] = sum[v] / length;
		} else if(v < mesh.normals.size() && glm::length(mesh.normals[v]) > 1e-12f) {
			normals[v] = glm::normalize(mesh.normals[v]);
		} else {
			normals[v] = Vec3f(0.f, 0.f, 1.f);
		}
	}
}

SkinnedMesh buildSleeve(const SleeveParams & params) {

	SkinnedMesh sleeve;

	const float upper = std::max(params.upperLength, 1e-3f);
	const float forearm = std::max(params.forearmLength, 1e-3f);
	const float total = upper + forearm;
	const int sides = std::max(params.sides, 3);

	// Joint stations along +X
	const float upperTwist = upper * glm::clamp(params.upperTwistAt, 0.f, 0.9f);
	const float twist1 = upper + forearm / 3.f;
	const float twist2 = upper + forearm * 2.f / 3.f;
	const float jointX[SleeveJointCount] = { 0.f, upperTwist, upper, twist1, twist2, total };
	const char * const jointNames[SleeveJointCount] = {
		"Shoulder", "UpperTwist", "Elbow", "ForearmTwist1", "ForearmTwist2", "Wrist"
	};
	for(int j = 0; j < SleeveJointCount; j++) {
		MeshJoint joint;
		joint.name = jointNames[j];
		joint.parent = j - 1;
		float local = j == 0 ? jointX[0] : jointX[j] - jointX[j - 1];
		joint.restLocal = glm::translate(glm::mat4(1.f), Vec3f(local, 0.f, 0.f));
		joint.inverseBind = glm::translate(glm::mat4(1.f), Vec3f(-jointX[j], 0.f, 0.f));
		sleeve.skeleton.push_back(joint);
	}

	// The elbow's blend and its side rings must stay between the upper twist and the first forearm twist
	const float elbowRoom = std::min(upper - upperTwist, forearm / 3.f);
	const float blend = glm::clamp(params.elbowBlend, 1e-3f, elbowRoom * 0.9f);
	const float ringOffset = glm::clamp(params.elbowRingOffset, 1e-3f, elbowRoom * 0.9f);

	// The rings: every joint, the elbow's side rings, the rest spread over the longest gaps
	std::vector<float> stations = { 0.f, upperTwist, upper - ringOffset, upper, upper + ringOffset, twist1, twist2, total };
	std::sort(stations.begin(), stations.end());
	stations.erase(std::unique(stations.begin(), stations.end(), [](float a, float b) { return b - a < 1e-3f; }),
	               stations.end());
	std::vector<int> pieces(stations.size() - 1, 1);
	for(int extra = params.rings - int(stations.size()); extra > 0; extra--) {
		size_t longest = 0;
		for(size_t g = 1; g < pieces.size(); g++) {
			float gap = (stations[g + 1] - stations[g]) / float(pieces[g]);
			float best = (stations[longest + 1] - stations[longest]) / float(pieces[longest]);
			if(gap > best) {
				longest = g;
			}
		}
		pieces[longest]++;
	}
	std::vector<float> rings;
	for(size_t g = 0; g < pieces.size(); g++) {
		for(int p = 0; p < pieces[g]; p++) {
			rings.push_back(stations[g] + (stations[g + 1] - stations[g]) * float(p) / float(pieces[g]));
		}
	}
	rings.push_back(total);

	// The weights: linear from joint to joint, the elbow handing over across its blend
	const std::pair<float, int> weightKeys[] = {
		{ 0.f, SleeveShoulder },
		{ upperTwist, SleeveUpperTwist },
		{ upper - blend, SleeveUpperTwist },
		{ upper + blend, SleeveElbow },
		{ twist1, SleeveForearmTwist1 },
		{ twist2, SleeveForearmTwist2 },
		{ total, SleeveWrist },
	};
	const size_t weightKeyCount = sizeof(weightKeys) / sizeof(weightKeys[0]);

	// The profile: the flared top, the upper arm narrowing to the elbow, the forearm to the wrist
	const std::vector<std::pair<float, float>> radiusKeys = {
		{ 0.f, params.shoulderRadius * params.topFlare },
		{ upperTwist, params.shoulderRadius },
		{ upper, params.elbowRadius },
		{ twist1, params.forearmRadius },
		{ total, params.wristRadius },
	};
	const std::vector<std::pair<float, float>> flattenKeys = {
		{ twist1, 1.f },
		{ total, params.wristFlatten },
	};

	for(float x : rings) {

		std::array<u8, 4> joints = { u8(SleeveShoulder), 0, 0, 0 };
		std::array<float, 4> weights = { 1.f, 0.f, 0.f, 0.f };
		for(size_t k = 0; k + 1 < weightKeyCount; k++) {
			const auto & from = weightKeys[k];
			const auto & to = weightKeys[k + 1];
			if(x <= to.first || k + 2 == weightKeyCount) {
				float span = to.first - from.first;
				float t = span > 0.f ? glm::clamp((x - from.first) / span, 0.f, 1.f) : 1.f;
				if(from.second == to.second) {
					joints = { u8(from.second), 0, 0, 0 };
					weights = { 1.f, 0.f, 0.f, 0.f };
				} else {
					joints = { u8(from.second), u8(to.second), 0, 0 };
					weights = { 1.f - t, t, 0.f, 0.f };
				}
				break;
			}
		}

		const float radiusZ = interpolateSleeveKeys(radiusKeys, x);
		const float radiusY = radiusZ * interpolateSleeveKeys(flattenKeys, x);
		for(int s = 0; s <= sides; s++) {
			// From the inner side (+Y) through the front (-Z), the outer side (-Y) and the back (+Z)
			float angle = glm::two_pi<float>() * float(s % sides) / float(sides);
			sleeve.positions.emplace_back(x, radiusY * std::cos(angle), -radiusZ * std::sin(angle));
			sleeve.uvs.emplace_back(float(s) / float(sides), x / total);
			sleeve.joints.push_back(joints);
			sleeve.weights.push_back(weights);
		}
	}

	// Quads between neighbouring rings, counter-clockwise seen from outside
	const size_t stride = size_t(sides) + 1;
	for(size_t r = 0; r + 1 < rings.size(); r++) {
		for(size_t s = 0; s < size_t(sides); s++) {
			u16 a = u16(r * stride + s);
			u16 b = u16((r + 1) * stride + s);
			u16 c = u16(r * stride + s + 1);
			u16 d = u16((r + 1) * stride + s + 1);
			for(u16 index : { a, b, c, c, b, d }) {
				sleeve.indices.push_back(index);
			}
		}
	}

	recomputeNormals(sleeve, sleeve.positions, sleeve.normals);

	return sleeve;
}

void applyElbowCorrective(const SkinnedMesh & sleeve, float bendDegrees, float k, std::vector<Vec3f> & positions) {

	const float push = k * std::sin(glm::radians(glm::clamp(bendDegrees, 0.f, 180.f)) * 0.5f);
	if(push == 0.f) {
		return;
	}

	const size_t count = std::min(sleeve.positions.size(), positions.size());
	for(size_t v = 0; v < count; v++) {

		float upperShare = 0.f;
		float forearmShare = 0.f;
		for(int s = 0; s < 4; s++) {
			int joint = sleeve.joints[v][s];
			if(joint == SleeveShoulder || joint == SleeveUpperTwist) {
				upperShare += sleeve.weights[v][s];
			} else {
				forearmShare += sleeve.weights[v][s];
			}
		}
		const float shared = 4.f * upperShare * forearmShare;
		if(shared <= 0.f) {
			continue;
		}

		// The direction away from the arm's axis, at rest
		const Vec3f & rest = sleeve.positions[v];
		const float radius = std::sqrt(rest.y * rest.y + rest.z * rest.z);
		if(radius < 1e-6f) {
			continue;
		}
		const Vec3f out(0.f, rest.y / radius, rest.z / radius);

		if(out.y < 0.f) {
			// The outer side: out, most at the point of the elbow
			positions[v] += out * (push * shared * -out.y);
		} else {
			// The inner side: in toward the fold, never past half the radius
			positions[v] -= out * std::min(push * shared * out.y, radius * 0.5f);
		}
	}
}

} // namespace vr
