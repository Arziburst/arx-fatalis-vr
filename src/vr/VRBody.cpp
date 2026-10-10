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

#include "vr/VRBody.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "animation/Skeleton.h"
#include "core/GameEvent.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Player.h"
#include "graphics/GraphicsTypes.h"
#include "io/log/Logger.h"
#include "scene/Object.h"
#include "vr/ArmIK.h"
#include "vr/HeadTracking.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

//! How far the shoulder may slide towards a target the arm cannot reach (units; F-52)
const float MaxShoulderSlide = 20.f;

//! One arm of the model: the three joints from the shoulder to the wrist, and the hand's frame
struct ArmChain {
	bool valid = false;
	VertexGroupId upper;                   //!< the group whose origin is the shoulder joint
	VertexGroupId forearm;                 //!< ... the elbow joint
	VertexGroupId hand;                    //!< ... the wrist joint: the bone that takes the controller's orientation
	VertexId palm;                         //!< the attach vertex of the hand: lands on the grip
	VertexId tip;                          //!< the vertex farthest along the hand: the fingertips (status.json)
	//! the zero-length groups between the wrist joint and the hand's own group: rigid with the wrist, so the
	//! hand mesh takes the controller's orientation itself and not the animation's bend of the hand on the wrist
	std::vector<VertexGroupId> rigid;
	Vec3f palmLocal = Vec3f(0.f);          //!< the attach vertex relative to the wrist joint, at rest
	HandFrame frame;                       //!< the hand mesh's axes at rest (valid: the hand takes the controller's orientation)
	glm::quat handToGrip = quat_identity(); //!< wrist bone rest frame -> grip frame in engine axes
	std::string names;                     //!< "upper>forearm>wrist[>hand]", or why there is no chain
	std::string thumbGroup;                //!< the thumb group the lateral axis was read from, if any
};

struct BodyState {
	const EERIE_3DOBJ * boundObject = nullptr;
	size_t boundGroups = 0;
	size_t boundVertices = 0;
	VertexId viewVertex;
	VertexGroupId viewGroup;
	std::array<ArmChain, HandCount> chains;
	std::array<bool, HandCount> haveSim = { };
	std::array<HandSample, HandCount> sim;
	Vec3f shift = Vec3f(0.f);
	std::array<bool, HandCount> posedBefore = { }; //!< for the per-arm events
	bool activeBefore = false;
	std::string inactiveBefore;
	std::vector<char> touched;                    //!< scratch: bones recomputed after the chain
	std::vector<char> headVertex;                 //!< per vertex of the bound model: in the head or the neck
	size_t capStart = 0;                          //!< the first cap face added to the bound model (its face count without one)
	PlayerBodyStatus status;
};

BodyState g_body;

std::string groupName(const EERIE_3DOBJ * obj, VertexGroupId group) {
	return group ? obj->grouplist[group].name : std::string("-");
}

std::string vectorText(const Vec3f & v, int decimals = 2) {
	std::ostringstream text;
	text << std::fixed << std::setprecision(decimals) << '(' << v.x << ", " << v.y << ", " << v.z << ')';
	return text.str();
}

//! The arm whose hand holds the attach vertex: the wrist joint it sits on, its father and its father's father
ArmChain findChain(const EERIE_3DOBJ * obj, Hand hand) {

	ArmChain chain;

	const VertexId palm = (hand == Hand::Right) ? obj->fastaccess.primary_attach : obj->fastaccess.left_attach;
	if(!palm) {
		chain.names = (hand == Hand::Right) ? "no primary_attach vertex" : "no left_attach vertex";
		return chain;
	}

	const VertexGroupId handGroup = getGroupForVertex(obj, palm);
	if(!handGroup) {
		chain.names = "the attach vertex is in no group";
		return chain;
	}

	// The wrist joint: the hand's group, or the ancestor it sits on when the model keeps the hand as a
	// zero-length bone under a wrist group (human_base: right_arm > right_elbow > right_wrist > right_hand)
	const Skeleton & rig = *obj->m_skeleton;
	VertexGroupId wrist = handGroup;
	while(rig.bones[wrist].father && glm::length(rig.bones[wrist].transinit_global) < 0.5f) {
		wrist = rig.bones[wrist].father;
	}
	const VertexGroupId forearm = rig.bones[wrist].father;
	const VertexGroupId upper = forearm ? rig.bones[forearm].father : VertexGroupId();
	if(!forearm || !upper) {
		chain.names = "chain too short above " + groupName(obj, wrist) + " (father "
		              + groupName(obj, forearm) + ")";
		return chain;
	}

	chain.upper = upper;
	chain.forearm = forearm;
	chain.hand = wrist;
	chain.palm = palm;
	for(VertexGroupId group = handGroup; group != wrist; group = rig.bones[group].father) {
		chain.rigid.push_back(group);
	}
	chain.names = groupName(obj, upper) + ">" + groupName(obj, forearm) + ">" + groupName(obj, wrist)
	              + (wrist != handGroup ? ">" + groupName(obj, handGroup) : std::string());

	// The hand's axes, relative to the wrist joint in the rest mesh: the long axis from the wrist to
	// the palm attach point, the lateral axis towards the model's thumb group when it has one (else
	// the wider side of the hand mesh), from the vertices of the wrist and of everything under it
	const Vec3f wristOrigin = obj->vertexlist[obj->grouplist[wrist].origin].v;
	chain.palmLocal = obj->vertexlist[palm].v - wristOrigin;
	std::vector<Vec3f> vertices;
	std::vector<VertexId> vertexIds;
	bool haveThumb = false;
	Vec3f thumbLocal(0.f);
	for(VertexGroupId group : obj->grouplist.handles()) {
		// A descendant of the wrist (or the wrist itself)?
		VertexGroupId ancestor = group;
		while(ancestor && ancestor != wrist) {
			ancestor = rig.bones[ancestor].father;
		}
		if(!ancestor) {
			continue;
		}
		for(VertexId vertex : obj->m_boneVertices[group]) {
			vertices.push_back(obj->vertexlist[vertex].v - wristOrigin);
			vertexIds.push_back(vertex);
		}
		if(group != wrist && !haveThumb && obj->grouplist[group].name.find("thumb") != std::string::npos) {
			haveThumb = true;
			thumbLocal = obj->vertexlist[obj->grouplist[group].origin].v - wristOrigin;
			chain.thumbGroup = groupName(obj, group);
		}
	}
	chain.frame = handFrameFromMesh(vertices, &chain.palmLocal, haveThumb ? &thumbLocal : nullptr);
	if(chain.frame.valid) {
		chain.handToGrip = handToGrip(chain.frame);
	}
	// The fingertips: the vertex farthest from the wrist along the hand
	chain.tip = palm;
	float farthest = 0.f;
	for(size_t i = 0; i < vertices.size(); i++) {
		const float along = glm::dot(vertices[i], chain.frame.knuckles);
		if(along > farthest) {
			farthest = along;
			chain.tip = vertexIds[i];
		}
	}

	chain.valid = true;

	return chain;
}

/*!
 * \brief Close the opening the hidden head and neck leave in the torso
 *
 * Under a tracked head the model is drawn two-sided and the eyes sit above the chest, so looking down
 * meets the neck opening and the hollow torso behind it. The boundary between the hidden faces (any
 * vertex in the head or the neck) and the visible ones is walked as loops, and each loop is closed by
 * a triangle fan with the material and texture coordinates of the visible faces on it. The cap faces are
 * appended to the model's face list and drawn only under a tracked head (\ref bodyCapStart).
 */
void addNeckCap(EERIE_3DOBJ * obj) {

	g_body.capStart = obj->facelist.size();
	if(g_body.headVertex.size() != obj->vertexlist.size()) {
		return;
	}

	typedef std::pair<size_t, size_t> EdgeKey;
	struct EdgeUse {
		size_t hidden = 0;
		size_t visible = 0;
		size_t visibleFace = 0;
	};
	auto edgeKey = [](size_t a, size_t b) { return EdgeKey(std::min(a, b), std::max(a, b)); };

	std::map<EdgeKey, EdgeUse> edges;
	for(size_t f = 0; f < obj->facelist.size(); f++) {
		const EERIE_FACE & face = obj->facelist[f];
		if(!face.material) {
			continue;
		}
		bool hidden = false;
		for(VertexId vertex : face.vid) {
			hidden = hidden || g_body.headVertex[size_t(vertex)];
		}
		for(size_t k = 0; k < 3; k++) {
			const size_t a = size_t(face.vid[k]);
			const size_t b = size_t(face.vid[(k + 1) % 3]);
			if(a == b) {
				continue;
			}
			EdgeUse & use = edges[edgeKey(a, b)];
			if(hidden) {
				use.hidden++;
			} else {
				use.visible++;
				use.visibleFace = f;
			}
		}
	}

	// The boundary: edges with a hidden face on one side and a visible one on the other
	std::map<size_t, std::vector<size_t>> neighbours;
	std::map<EdgeKey, size_t> boundaryFace;
	for(const auto & entry : edges) {
		if(entry.second.hidden && entry.second.visible) {
			neighbours[entry.first.first].push_back(entry.first.second);
			neighbours[entry.first.second].push_back(entry.first.first);
			boundaryFace[entry.first] = entry.second.visibleFace;
		}
	}

	// A vertex's texture coordinates on the visible side: from a visible boundary face holding it
	auto cornerOf = [&](size_t vertex, size_t neighbour, float & u, float & v, MaterialId & material) {
		const EERIE_FACE & face = obj->facelist[boundaryFace[edgeKey(vertex, neighbour)]];
		material = face.material;
		for(size_t k = 0; k < 3; k++) {
			if(size_t(face.vid[k]) == vertex) {
				u = face.u[k];
				v = face.v[k];
				return;
			}
		}
		u = v = 0.f;
	};

	std::set<EdgeKey> used;
	size_t loops = 0;
	size_t capFaces = 0;
	for(const auto & start : neighbours) {
		for(size_t first : start.second) {
			if(used.count(edgeKey(start.first, first))) {
				continue;
			}
			used.insert(edgeKey(start.first, first));
			std::vector<size_t> loop;
			loop.push_back(start.first);
			size_t current = first;
			while(current != start.first && loop.size() < obj->vertexlist.size()) {
				loop.push_back(current);
				size_t next = size_t(-1);
				for(size_t candidate : neighbours[current]) {
					if(!used.count(edgeKey(current, candidate))) {
						next = candidate;
						used.insert(edgeKey(current, candidate));
						break;
					}
				}
				if(next == size_t(-1)) {
					break;
				}
				current = next;
			}
			if(loop.size() < 3) {
				continue;
			}
			// A fan from the loop's first vertex; the material of the visible faces on the loop
			for(size_t i = 1; i + 1 < loop.size(); i++) {
				EERIE_FACE face;
				face.facetype = POLY_DOUBLESIDED;
				const size_t corners[3] = { loop[0], loop[i], loop[i + 1] };
				const size_t along[3] = { loop[1], loop[i + 1], loop[i] };
				for(size_t k = 0; k < 3; k++) {
					face.vid[k] = VertexId(corners[k]);
					MaterialId material;
					cornerOf(corners[k], along[k], face.u[k], face.v[k], material);
					if(k == 0 || !face.material) {
						face.material = material;
					}
				}
				obj->facelist.push_back(face);
				capFaces++;
			}
			loops++;
		}
	}

	LogInfo << "[vr] body: neck opening closed with " << capFaces << " cap face(s) over " << loops
	        << " boundary loop(s) (" << boundaryFace.size() << " boundary edges); drawn under a tracked head only";
}

void bind(EERIE_3DOBJ * obj) {

	PlayerBodyStatus & status = g_body.status;

	g_body.boundObject = obj;
	g_body.boundGroups = obj->grouplist.size();
	g_body.boundVertices = obj->vertexlist.size();
	g_body.viewVertex = obj->fastaccess.view_attach;
	g_body.viewGroup = g_body.viewVertex ? getGroupForVertex(obj, g_body.viewVertex) : VertexGroupId();
	status.model = obj->file.string();

	// The vertices hidden under the tracked head: the head group with everything under it, and the neck
	g_body.headVertex.assign(obj->vertexlist.size(), 0);
	if(obj->m_skeleton) {
		const VertexGroupId headGroup = obj->fastaccess.head_group;
		for(VertexGroupId group : obj->grouplist.handles()) {
			bool hidden = (obj->grouplist[group].name == "neck");
			for(VertexGroupId ancestor = group; ancestor && !hidden; ancestor = obj->m_skeleton->bones[ancestor].father) {
				hidden = (headGroup && ancestor == headGroup);
			}
			if(hidden) {
				for(VertexId vertex : obj->m_boneVertices[group]) {
					g_body.headVertex[size_t(vertex)] = 1;
				}
			}
		}
	}

	// The model's groups with their fathers, once: what every chain and every later body task reads
	{
		std::ostringstream groups;
		for(VertexGroupId group : obj->grouplist.handles()) {
			if(size_t(group) > 0) {
				groups << ", ";
			}
			groups << obj->grouplist[group].name;
			if(obj->m_skeleton && obj->m_skeleton->bones[group].father) {
				groups << '<' << groupName(obj, obj->m_skeleton->bones[group].father);
			}
		}
		std::ostringstream selections;
		for(VertexSelectionId selection : obj->selections.handles()) {
			selections << (size_t(selection) > 0 ? ", " : "") << obj->selections[selection].name;
		}
		// What the engine hides in first person (the "1st" selection, ARX_INTERACTIVE_Show_Hide_1st): the
		// groups its vertices belong to - the head alone, or the whole body
		std::ostringstream hidden;
		if(VertexSelectionId first = EERIE_OBJECT_GetSelection(obj, "1st")) {
			std::vector<size_t> counts(obj->grouplist.size(), 0);
			for(VertexId vertex : obj->selections[first].selected) {
				if(VertexGroupId group = getGroupForVertex(obj, vertex)) {
					counts[size_t(group)]++;
				}
			}
			for(size_t i = 0; i < counts.size(); i++) {
				if(counts[i]) {
					hidden << (hidden.tellp() > 0 ? ", " : "") << obj->grouplist[VertexGroupId(i)].name << ' ' << counts[i];
				}
			}
		} else {
			hidden << "no 1st selection";
		}
		LogInfo << "[vr] body: player model " << obj->file << ": " << obj->grouplist.size() << " groups (child<father): "
		        << groups.str() << "; selections: " << selections.str() << "; view_attach in "
		        << groupName(obj, g_body.viewGroup) << "; hidden in first person (vertices per group): " << hidden.str();
	}

	status.bound = false;
	status.chains.clear();
	for(size_t hand = 0; hand < HandCount; hand++) {
		ArmChain & chain = g_body.chains[hand];
		chain = findChain(obj, Hand(hand));
		const char * name = handName(Hand(hand));
		if(!chain.valid) {
			LogWarning << "[vr] body: " << name << " arm: no chain on the model (" << chain.names
			           << ") - the engine animates it";
			status.chains += std::string(hand ? "; " : "") + name + ": none";
			continue;
		}
		const Skeleton & rig = *obj->m_skeleton;
		const float upperLength = glm::length(rig.bones[chain.forearm].transinit_global);
		const float forearmLength = glm::length(rig.bones[chain.hand].transinit_global);
		std::ostringstream lengths;
		lengths << std::fixed << std::setprecision(1) << upperLength << " + " << forearmLength;
		LogInfo << "[vr] body: " << name << " arm chain " << chain.names << ", lengths " << lengths.str()
		        << " units, palm attach vertex " << size_t(chain.palm) << " at " << vectorText(chain.palmLocal, 1)
		        << " from the wrist; hand frame "
		        << (chain.frame.valid ? (chain.thumbGroup.empty() ? "from the mesh extents"
		                                                           : "from the thumb group " + chain.thumbGroup)
		                              : "UNUSABLE (the hand follows the forearm)")
		        << ": knuckles " << vectorText(chain.frame.knuckles) << " thumb " << vectorText(chain.frame.thumb)
		        << " palm " << vectorText(chain.frame.palm) << ", length " << std::fixed << std::setprecision(1)
		        << chain.frame.length << ", thumb side " << chain.frame.thumbExtent << " vs "
		        << chain.frame.otherExtent;
		status.chains += std::string(hand ? "; " : "") + name + ": " + chain.names;
		status.bound = true;
	}

	addNeckCap(obj);

	gameevent::Event("vr.body.model")("model", status.model)("chains", status.chains)("bound", status.bound);
}

//! A child bone from its parent, as the engine's concatenation does
void concatenate(Bone & bone, const Bone & parent) {
	bone.anim.quat = parent.anim.quat * bone.init.quat;
	bone.anim.trans = parent.anim.trans + parent.anim.quat * (bone.init.trans * parent.anim.scale);
	bone.anim.scale = (bone.init.scale + Vec3f(1.f)) * parent.anim.scale;
}

void noteArm(size_t hand, bool posed, const char * source) {
	if(posed != g_body.posedBefore[hand]) {
		g_body.posedBefore[hand] = posed;
		gameevent::Event("vr.body.arm", 200)("hand", handName(Hand(hand)))("posed", posed)("source", source);
	}
}

void noteBody(bool active, const char * inactive) {
	PlayerBodyStatus & status = g_body.status;
	status.active = active;
	status.inactive = inactive;
	if(active != g_body.activeBefore || (!active && g_body.inactiveBefore != inactive)) {
		g_body.activeBefore = active;
		g_body.inactiveBefore = inactive;
		gameevent::Event("vr.body", 200)("state", active ? "posed" : "engine")("reason", inactive);
	}
}

} // anonymous namespace

void posePlayerBody(EERIE_3DOBJ * eobj, Skeleton & skeleton, Entity * io) {

	PlayerBodyStatus & status = g_body.status;

	if(!eobj || !io || !eobj->m_skeleton) {
		return;
	}

	if(g_body.boundObject != eobj || g_body.boundGroups != eobj->grouplist.size()
	   || g_body.boundVertices != eobj->vertexlist.size()) {
		bind(eobj);
	}

	status.poseCalls++;
	g_body.shift = Vec3f(0.f);
	status.shift = Vec3f(0.f);
	for(size_t hand = 0; hand < HandCount; hand++) {
		PlayerArmStatus & arm = status.hands[hand];
		arm.source = "none";
		arm.tracked = false;
		arm.posed = false;
	}

	if(!status.bound) {
		noteBody(false, "no arm chain on the model");
		return;
	}

	// The head: the sample the camera takes this frame, against its reference
	HeadSample head;
	if(!peekHead(head)) {
		noteBody(false, "no head");
		return;
	}
	const HeadStatus & headState = headStatus();
	if(!headState.referenced) {
		noteBody(false, "head not referenced yet");
		return;
	}

	// The body under the head: the lean - the head's movement less what the collision cylinder already walked
	// after it (#26), the same offset the camera takes - moves every bone
	const float bodyYaw = player.angle.getYaw();
	const HeadCamera camera = headToCamera(head, headState.reference, bodyYaw, headState.anchor);
	const Vec3f shift(camera.offset.x, 0.f, camera.offset.z);
	for(Bone & bone : skeleton.bones) {
		bone.anim.trans += shift;
	}
	g_body.shift = shift;
	status.shift = shift;

	// The head camera in the world: the model's own eye (shifted with it) plus the head's height
	// change - the camera that renders this frame, up to the model's head animation
	Vec3f eye;
	if(g_body.viewVertex && g_body.viewGroup) {
		eye = skeleton.bones[g_body.viewGroup].anim(eobj->vertexlocal[g_body.viewVertex]);
	} else {
		eye = player.pos + shift;
	}
	const Vec3f headCamera = eye + Vec3f(0.f, camera.offset.y, 0.f);

	// The model faces where the body yaw of #25 says - the heading plus the body's offset after the
	// head - not the heading the engine built the skeleton with: every bone turns about the vertical
	// through the model's origin (the eye above was read before the turn: the camera does not turn)
	const float facingOffset = bodyStatus().offset;
	if(std::abs(facingOffset) > 0.01f) {
		const Vec3f root = skeleton.bones.front().anim.trans;
		const glm::quat turn = glm::angleAxis(-glm::radians(facingOffset), Vec3f(0.f, 1.f, 0.f));
		for(Bone & bone : skeleton.bones) {
			bone.anim.trans = root + turnAboutVertical(bone.anim.trans - root, facingOffset);
			bone.anim.quat = turn * bone.anim.quat;
		}
	}
	const float facingYaw = bodyYaw + facingOffset;
	status.facingYaw = wrapDegrees(facingYaw);

	// The body's axes for the elbow hints: engine x right, y down, z forward
	const float yaw = glm::radians(facingYaw);
	const Vec3f forward(-std::sin(yaw), 0.f, std::cos(yaw));
	const Vec3f right(std::cos(yaw), 0.f, std::sin(yaw));
	const Vec3f down(0.f, 1.f, 0.f);

	const FrameHands & located = frameHands();

	bool any = false;
	for(size_t hand = 0; hand < HandCount; hand++) {

		PlayerArmStatus & arm = status.hands[hand];
		const ArmChain & chain = g_body.chains[hand];

		// Where the controller is: located this frame, else the simulated one
		HandSample sample;
		const char * source = "none";
		if(located.tracked[hand]) {
			sample = located.hands[hand];
			source = "xr";
		} else if(g_body.haveSim[hand]) {
			sample = g_body.sim[hand];
			source = "sim";
		}
		arm.source = source;
		arm.tracked = (std::strcmp(source, "none") != 0);
		arm.sample = sample;
		if(!arm.tracked || !chain.valid) {
			noteArm(hand, false, source);
			continue;
		}

		Bone & upper = skeleton.bones[chain.upper];
		Bone & forearm = skeleton.bones[chain.forearm];
		Bone & handBone = skeleton.bones[chain.hand];

		// The grip in the world, rigid with the head camera; the wrist bone takes the controller's
		// orientation through the hand frame, or keeps its animated relation to the forearm
		const HandPlacement placement = handToWorld(sample, head, headState.reference, bodyYaw);
		const Vec3f grip = headCamera + placement.offset;
		const glm::quat handQuat = chain.frame.valid ? placement.orientation * chain.handToGrip : handBone.anim.quat;

		// The wrist target: the palm attach point (relative to the wrist, scaled like the bone) on the grip
		const Vec3f palmLocal = chain.palmLocal * handBone.anim.scale;
		const Vec3f target = grip - handQuat * palmLocal;

		// The bones as the engine built them: joint to joint, in the parent's scale
		const Vec3f upperVector = forearm.init.trans * upper.anim.scale;
		const Vec3f forearmVector = handBone.init.trans * forearm.anim.scale;
		const float upperLength = glm::length(upperVector);
		const float forearmLength = glm::length(forearmVector);

		const float side = (Hand(hand) == Hand::Right) ? 1.f : -1.f;
		const Vec3f hint = down + right * (0.5f * side) - forward * 0.2f;
		ArmIKParams params;
		params.maxShoulderSlide = MaxShoulderSlide;
		const ArmPose pose = solveArm(upper.anim.trans, target, upperLength, forearmLength, hint, params);

		// Write the chain back: world rotations that put each bone on its joints, the animated twist kept
		upper.anim.trans = pose.shoulder;
		upper.anim.quat = rotationBetween(upper.anim.quat * upperVector, pose.elbow - pose.shoulder) * upper.anim.quat;
		forearm.anim.trans = pose.elbow;
		const glm::quat forearmFollowing = upper.anim.quat * forearm.init.quat;
		forearm.anim.quat = rotationBetween(forearmFollowing * forearmVector, pose.wrist - pose.elbow) * forearmFollowing;
		handBone.anim.trans = pose.wrist;
		handBone.anim.quat = chain.frame.valid ? handQuat : forearm.anim.quat * handBone.init.quat;

		// Whatever hangs below the chain (the hand, fingers, other children of these groups) follows it
		std::vector<char> & touched = g_body.touched;
		touched.assign(skeleton.bones.size(), 0);
		touched[size_t(chain.upper)] = 1;
		touched[size_t(chain.forearm)] = 1;
		touched[size_t(chain.hand)] = 1;
		for(size_t i = size_t(chain.upper) + 1; i < skeleton.bones.size(); i++) {
			if(i == size_t(chain.forearm) || i == size_t(chain.hand)) {
				continue;
			}
			Bone & bone = skeleton.bones[VertexGroupId(i)];
			if(bone.father && touched[size_t(bone.father)]) {
				concatenate(bone, skeleton.bones[bone.father]);
				for(VertexGroupId rigid : chain.rigid) {
					if(rigid == VertexGroupId(i)) {
						// The hand on the wrist: the controller's orientation, not the animation's bend
						bone.anim.quat = skeleton.bones[bone.father].anim.quat;
					}
				}
				touched[i] = 1;
			}
		}

		arm.posed = true;
		arm.controller = grip;
		arm.grip = placement.orientation;
		arm.target = target;
		arm.shoulder = pose.shoulder;
		arm.elbow = pose.elbow;
		arm.wrist = pose.wrist;
		arm.reach = pose.reach;
		arm.armLength = upperLength + forearmLength;
		arm.error = pose.error;
		arm.slide = pose.slide;
		arm.stretched = pose.stretched;
		noteArm(hand, true, source);
		any = true;
	}

	if(any) {
		status.posedFrames++;
	}
	noteBody(any, any ? "" : "no tracked hand");
}

Vec3f bodyShift() {
	return g_body.shift;
}

bool hidesHeadOnly() {
	// A head drives the camera (a headset, the last headset sample, or a simulated head)
	return std::strcmp(headStatus().source, "none") != 0;
}

bool isHeadVertex(EERIE_3DOBJ * obj, VertexId vertex) {
	if(!obj || !obj->m_skeleton) {
		return false;
	}
	if(g_body.boundObject != obj || g_body.boundGroups != obj->grouplist.size()
	   || g_body.boundVertices != obj->vertexlist.size()) {
		bind(obj);
	}
	return size_t(vertex) < g_body.headVertex.size() && g_body.headVertex[size_t(vertex)];
}

size_t bodyCapStart(const EERIE_3DOBJ * obj) {
	return (obj && obj == g_body.boundObject) ? g_body.capStart : (obj ? obj->facelist.size() : 0);
}

bool handLandmarks(Hand hand, Vec3f & palm, Vec3f & tip) {
	const ArmChain & chain = g_body.chains[size_t(hand)];
	if(!chain.valid || !g_body.boundObject) {
		return false; // nothing bound yet: before the player entity exists, entities.player() must not be asked
	}
	const Entity * io = entities.player();
	if(!io || io->obj != g_body.boundObject) {
		return false;
	}
	palm = io->obj->vertexWorldPositions[chain.palm].v;
	tip = io->obj->vertexWorldPositions[chain.tip].v;
	return true;
}

const HandFrame * handRestFrame(Hand hand) {
	const ArmChain & chain = g_body.chains[size_t(hand)];
	return (g_body.boundObject && chain.valid && chain.frame.valid) ? &chain.frame : nullptr;
}

void setSimulatedHand(Hand hand, const HandSample & sample) {
	g_body.sim[size_t(hand)] = sample;
	g_body.haveSim[size_t(hand)] = true;
}

void clearSimulatedHand(Hand hand) {
	g_body.haveSim[size_t(hand)] = false;
}

const PlayerBodyStatus & playerBodyStatus() {
	return g_body.status;
}

} // namespace vr
