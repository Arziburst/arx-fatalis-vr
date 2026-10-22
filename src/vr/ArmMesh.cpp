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
	std::string_view wanted = GltfHandJointNames[size_t(joint)];
	for(size_t i = 0; i < mesh.skeleton.size(); i++) {
		if(withoutSideSuffix(mesh.skeleton[i].name) == wanted) {
			return int(i);
		}
	}
	return -1;
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

} // namespace vr
