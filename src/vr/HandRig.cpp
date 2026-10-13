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

#include "vr/HandRig.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vr {

namespace {

// Helpers are prefixed rig: arx is a unity build

Vec3f rigJointPosition(const std::vector<glm::mat4> & world, const HandRig & rig, HandJoint joint) {
	return Vec3f(world[size_t(rig.joint[size_t(joint)])][3]);
}

//! \a direction (mesh space) in the rest frame of the joint whose world matrix is \a world, unit
Vec3f rigToLocal(const glm::mat4 & world, const Vec3f & direction) {
	return glm::normalize(glm::inverse(glm::mat3(world)) * direction);
}

//! A unit axis across \a bone toward \a toward: turning \a bone about it by a positive angle moves it toward \a toward
bool rigAxisAcross(const Vec3f & bone, const Vec3f & toward, Vec3f & axis) {
	const Vec3f across = glm::cross(bone, toward);
	const float length = glm::length(across);
	if(!(length > 1e-6f)) {
		return false;
	}
	axis = across / length;
	return true;
}

glm::mat4 rigTurn(float degrees, const Vec3f & axis) {
	return glm::mat4_cast(glm::angleAxis(glm::radians(degrees), axis));
}

//! The rotation of a joint matrix (its columns made unit, so a scaled matrix gives its rotation too)
glm::quat rigRotation(const glm::mat4 & matrix) {
	glm::mat3 m(matrix);
	for(int c = 0; c < 3; c++) {
		const float length = glm::length(m[c]);
		if(length > 1e-9f) {
			m[c] /= length;
		}
	}
	return glm::normalize(glm::quat_cast(m));
}

// The hand-shape JSON (#385.7): a small reader for the layout loadHandShape takes, no library needed

//! A JSON value
struct RigJson {
	enum Type : u8 { Null, Bool, Number, String, Array, Object };
	Type type = Null;
	double number = 0.0;
	std::string text;
	std::vector<RigJson> items;
	std::vector<std::pair<std::string, RigJson>> members;
	const RigJson * member(const char * name) const {
		for(const auto & entry : members) {
			if(entry.first == name) {
				return &entry.second;
			}
		}
		return nullptr;
	}
};

//! A recursive descent JSON reader (RFC 8259, without \u escapes beyond ASCII)
class RigJsonReader {

public:

	RigJsonReader(const char * data, size_t size) : m_at(data), m_end(data + size) { }

	bool read(RigJson & out, std::string & error) {
		skipSpace();
		if(!value(out, 0)) {
			error = m_error;
			return false;
		}
		skipSpace();
		if(m_at != m_end) {
			error = "unexpected text after the JSON value";
			return false;
		}
		return true;
	}

private:

	static const int MaxDepth = 16;

	bool fail(const char * what) {
		if(m_error.empty()) {
			m_error = what;
		}
		return false;
	}

	void skipSpace() {
		while(m_at != m_end && (*m_at == ' ' || *m_at == '\t' || *m_at == '\n' || *m_at == '\r')) {
			m_at++;
		}
	}

	bool literal(const char * word) {
		const size_t length = std::strlen(word);
		if(size_t(m_end - m_at) < length || std::memcmp(m_at, word, length) != 0) {
			return fail("not JSON: an unknown word");
		}
		m_at += length;
		return true;
	}

	bool string(std::string & out) {
		m_at++; // "
		while(m_at != m_end && *m_at != '"') {
			char c = *m_at++;
			if(c == '\\') {
				if(m_at == m_end) {
					break;
				}
				c = *m_at++;
				switch(c) {
					case 'n': c = '\n'; break;
					case 't': c = '\t'; break;
					case 'r': c = '\r'; break;
					case 'b': c = '\b'; break;
					case 'f': c = '\f'; break;
					case 'u': {
						if(m_end - m_at < 4) {
							return fail("a broken \\u escape");
						}
						unsigned code = 0;
						for(int i = 0; i < 4; i++) {
							const char h = *m_at++;
							code = code * 16 + unsigned(h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
						}
						c = (code < 128) ? char(code) : '?';
						break;
					}
					default: break; // \" \\ \/
				}
			}
			out.push_back(c);
		}
		if(m_at == m_end) {
			return fail("an unterminated string");
		}
		m_at++; // "
		return true;
	}

	bool number(double & out) {
		const char * start = m_at;
		double sign = 1.0;
		if(*m_at == '-') {
			sign = -1.0;
			m_at++;
		}
		double value = 0.0;
		int digits = 0;
		while(m_at != m_end && *m_at >= '0' && *m_at <= '9') {
			value = value * 10.0 + double(*m_at++ - '0');
			digits++;
		}
		if(m_at != m_end && *m_at == '.') {
			m_at++;
			double scale = 0.1;
			while(m_at != m_end && *m_at >= '0' && *m_at <= '9') {
				value += double(*m_at++ - '0') * scale;
				scale *= 0.1;
				digits++;
			}
		}
		if(digits == 0) {
			m_at = start;
			return fail("not JSON: a number without digits");
		}
		if(m_at != m_end && (*m_at == 'e' || *m_at == 'E')) {
			m_at++;
			int exponentSign = 1;
			if(m_at != m_end && (*m_at == '+' || *m_at == '-')) {
				exponentSign = (*m_at++ == '-') ? -1 : 1;
			}
			int exponent = 0;
			while(m_at != m_end && *m_at >= '0' && *m_at <= '9') {
				exponent = std::min(exponent * 10 + (*m_at++ - '0'), 400);
			}
			value *= std::pow(10.0, double(exponentSign * exponent));
		}
		out = sign * value;
		return true;
	}

	bool value(RigJson & out, int depth) {
		if(depth > MaxDepth) {
			return fail("JSON nested too deep");
		}
		if(m_at == m_end) {
			return fail("the JSON ends early");
		}
		const char c = *m_at;
		if(c == '{') {
			out.type = RigJson::Object;
			m_at++;
			skipSpace();
			if(m_at != m_end && *m_at == '}') {
				m_at++;
				return true;
			}
			for(;;) {
				skipSpace();
				if(m_at == m_end || *m_at != '"') {
					return fail("an object key is not a string");
				}
				std::string key;
				if(!string(key)) {
					return false;
				}
				skipSpace();
				if(m_at == m_end || *m_at != ':') {
					return fail("an object key without ':'");
				}
				m_at++;
				skipSpace();
				out.members.emplace_back(std::move(key), RigJson());
				if(!value(out.members.back().second, depth + 1)) {
					return false;
				}
				skipSpace();
				if(m_at != m_end && *m_at == ',') {
					m_at++;
					continue;
				}
				if(m_at != m_end && *m_at == '}') {
					m_at++;
					return true;
				}
				return fail("an object without its '}'");
			}
		}
		if(c == '[') {
			out.type = RigJson::Array;
			m_at++;
			skipSpace();
			if(m_at != m_end && *m_at == ']') {
				m_at++;
				return true;
			}
			for(;;) {
				skipSpace();
				out.items.emplace_back();
				if(!value(out.items.back(), depth + 1)) {
					return false;
				}
				skipSpace();
				if(m_at != m_end && *m_at == ',') {
					m_at++;
					continue;
				}
				if(m_at != m_end && *m_at == ']') {
					m_at++;
					return true;
				}
				return fail("an array without its ']'");
			}
		}
		if(c == '"') {
			out.type = RigJson::String;
			return string(out.text);
		}
		if(c == 't' || c == 'f') {
			out.type = RigJson::Bool;
			out.number = (c == 't') ? 1.0 : 0.0;
			return literal(c == 't' ? "true" : "false");
		}
		if(c == 'n') {
			return literal("null");
		}
		if(c == '-' || (c >= '0' && c <= '9')) {
			out.type = RigJson::Number;
			return number(out.number);
		}
		return fail("not JSON");
	}

	const char * m_at;
	const char * m_end;
	std::string m_error;

};

//! The names MRTK3 gives the joints (TrackedHandJoint), in XrHandJointEXT order
const char * const RigShapeJointNames[HandJointCount] = {
	"Palm", "Wrist",
	"ThumbMetacarpal", "ThumbProximal", "ThumbDistal", "ThumbTip",
	"IndexMetacarpal", "IndexProximal", "IndexIntermediate", "IndexDistal", "IndexTip",
	"MiddleMetacarpal", "MiddleProximal", "MiddleIntermediate", "MiddleDistal", "MiddleTip",
	"RingMetacarpal", "RingProximal", "RingIntermediate", "RingDistal", "RingTip",
	"LittleMetacarpal", "LittleProximal", "LittleIntermediate", "LittleDistal", "LittleTip",
};

bool rigNumbers(const RigJson * object, const char * const * names, size_t count, double * out) {
	if(!object || object->type != RigJson::Object) {
		return false;
	}
	for(size_t i = 0; i < count; i++) {
		const RigJson * value = object->member(names[i]);
		if(!value || value->type != RigJson::Number || !std::isfinite(value->number)) {
			return false;
		}
		out[i] = value->number;
	}
	return true;
}

//! Unity (left-handed, y up, z forward) to OpenXR (right-handed): z negated; a rotation's x and y negated
Vec3f rigFromUnity(double x, double y, double z) {
	return Vec3f(float(x), float(y), float(-z));
}

glm::quat rigFromUnity(double x, double y, double z, double w) {
	return glm::quat(float(w), float(-x), float(-y), float(z));
}

//! The finger a joint index belongs to, or FingerCount for the palm and the wrist
size_t rigFingerOf(size_t joint) {
	if(joint < size_t(HandJoint::ThumbMetacarpal)) {
		return FingerCount;
	}
	if(joint <= size_t(HandJoint::ThumbTip)) {
		return size_t(Finger::Thumb);
	}
	return 1 + (joint - size_t(HandJoint::IndexMetacarpal)) / 5;
}

//! The bone from \a joint to the next joint of its finger exists (not a tip, not the palm or the wrist)
bool rigHasBone(size_t joint) {
	return rigFingerOf(joint) < FingerCount && joint != size_t(HandJoint::ThumbTip)
	       && (joint < size_t(HandJoint::IndexMetacarpal) || (joint - size_t(HandJoint::IndexMetacarpal)) % 5 != 4);
}

//! The first joint of a finger's chain: the thumb's or a finger's metacarpal
bool rigIsChainRoot(size_t joint) {
	return joint == size_t(HandJoint::ThumbMetacarpal)
	       || (joint >= size_t(HandJoint::IndexMetacarpal) && (joint - size_t(HandJoint::IndexMetacarpal)) % 5 == 0);
}

// The hilt pose (#385.7): the synthetic hand of vr/FingerCurl.h, closed around a hilt in the right hand's grip frame

/*!
 * The thumb's metacarpal leaves its base toward the palm's face and the fingers, in the right hand's grip frame:
 * over the hilt, so its two outer joints lay it across the fingers wrapped round the hilt
 */
const Vec3f HiltThumbDirection(-0.8f, -0.6f, 0.f);
//! Each bend is opened by this factor while the pose leaves a fingertip inside the hilt
const float HiltOpenStep = 0.9f;
//! A fingertip may sink this far into the hilt (meters: its skin pressed on it) before its finger opens
const float HiltTipTolerance = 0.001f;

//! The distance of \a p from the hilt's axis (the Z axis through \a axis)
float hiltAxisDistance(const Vec3f & p, const Vec3f & axis) {
	return std::sqrt((p.x - axis.x) * (p.x - axis.x) + (p.y - axis.y) * (p.y - axis.y));
}

//! The centre of the circle through three points of the XY plane (\a a if they are in a line)
Vec3f hiltCircleCentre(const Vec3f & a, const Vec3f & b, const Vec3f & c) {
	const float d = 2.f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
	if(!(std::abs(d) > 1e-12f)) {
		return a;
	}
	const float a2 = a.x * a.x + a.y * a.y;
	const float b2 = b.x * b.x + b.y * b.y;
	const float c2 = c.x * c.x + c.y * c.y;
	return Vec3f((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
	             (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d, 0.f);
}

/*!
 * A finger chain of \a joints (the right hand's grip frame) re-bent from its knuckle: the bones of \a open,
 * the n-th bent by bends[n] x \a scale more toward the palm's face (-X) in the plane of the fingers (XY);
 * \a first is the knuckle (a finger's proximal joint), the chain's three bones follow it
 */
void hiltBendChain(HandJoints & joints, const HandJoints & open, size_t first, const float (&bends)[3], float scale) {
	Vec3f at = open.position[first];
	joints.position[first] = at;
	float total = 0.f;
	for(size_t k = 0; k < 3; k++) {
		total += glm::radians(bends[k] * scale);
		const float length = glm::distance(open.position[first + k + 1], open.position[first + k]);
		at += Vec3f(-std::sin(total), -std::cos(total), 0.f) * length;
		joints.position[first + k + 1] = at;
	}
}

/*!
 * The thumb of \a joints laid from its base (\a open's) along HiltThumbDirection, its proximal and distal joint
 * bent by \a bend toward the fingers (turning toward -Y in the XY plane), its tip reaching \a tipZ; the bones of \a open
 */
void hiltLayThumb(HandJoints & joints, const HandJoints & open, float bend, float tipZ) {
	const size_t first = size_t(HandJoint::ThumbMetacarpal);
	Vec3f at = open.position[first];
	joints.position[first] = at;
	const float zStep = (tipZ - at.z) / 3.f;
	float total = std::atan2(HiltThumbDirection.y, HiltThumbDirection.x);
	for(size_t k = 0; k < 3; k++) {
		if(k > 0) {
			total += glm::radians(bend);
		}
		const float length = glm::distance(open.position[first + k + 1], open.position[first + k]);
		const float flat = std::sqrt(std::max(length * length - zStep * zStep, 0.f));
		at += Vec3f(std::cos(total) * flat, std::sin(total) * flat, zStep);
		joints.position[first + k + 1] = at;
	}
}

} // anonymous namespace

HandJoint flexingJoint(Finger finger, size_t joint) {
	if(finger == Finger::Thumb) {
		return HandJoint(size_t(HandJoint::ThumbMetacarpal) + joint);
	}
	return HandJoint(size_t(HandJoint::IndexProximal) + 5 * (size_t(finger) - 1) + joint);
}

HandRig buildHandRig(const SkinnedMesh & hand) {

	HandRig rig;
	for(size_t j = 0; j < HandJointCount; j++) {
		rig.joint[j] = findHandJoint(hand, HandJoint(j));
		if(rig.joint[j] < 0) {
			return rig;
		}
	}

	std::vector<glm::mat4> world;
	restWorldMatrices(hand, world);

	// The palm's normal: across the metacarpals' bases and along the middle metacarpal, on the Palm joint's side
	const Vec3f thumbSide = rigJointPosition(world, rig, HandJoint::IndexMetacarpal)
	                        - rigJointPosition(world, rig, HandJoint::LittleMetacarpal);
	const Vec3f along = rigJointPosition(world, rig, HandJoint::MiddleProximal)
	                    - rigJointPosition(world, rig, HandJoint::MiddleMetacarpal);
	Vec3f palmNormal = glm::cross(thumbSide, along);
	if(!(glm::length(palmNormal) > 1e-9f) || !(glm::length(thumbSide) > 1e-6f)) {
		return rig;
	}
	palmNormal = glm::normalize(palmNormal);
	const Vec3f toPalm = rigJointPosition(world, rig, HandJoint::Palm)
	                     - rigJointPosition(world, rig, HandJoint::MiddleMetacarpal);
	if(glm::dot(palmNormal, toPalm) < 0.f) {
		palmNormal = -palmNormal;
	}

	for(size_t finger = 0; finger < FingerCount; finger++) {
		for(size_t k = 0; k < FingerJointCount; k++) {
			const HandJoint joint = flexingJoint(Finger(finger), k);
			const size_t index = size_t(rig.joint[size_t(joint)]);
			const size_t child = size_t(rig.joint[size_t(joint) + 1]);
			// The bone to the next joint is that joint's rest translation, in this joint's frame
			const Vec3f bone = Vec3f(hand.skeleton[child].restLocal[3]);
			if(!(glm::length(bone) > 1e-6f)
			   || !rigAxisAcross(bone, rigToLocal(world[index], palmNormal), rig.flexAxis[finger * FingerJointCount + k])) {
				return rig;
			}
			if(k == 0 && !rigAxisAcross(bone, rigToLocal(world[index], thumbSide), rig.spreadAxis[finger])) {
				return rig;
			}
		}
	}

	// The rest frames in the OpenXR joint convention and each joint's rest correction (#385.7)
	HandJoints rest;
	for(size_t j = 0; j < HandJointCount; j++) {
		rest.position[j] = rigJointPosition(world, rig, HandJoint(j));
	}
	orientHandJoints(rest, along, -palmNormal, thumbSide);
	for(size_t j = 0; j < HandJointCount; j++) {
		rig.restFrame[j] = rest.orientation[j];
		rig.frameToJoint[j] = glm::normalize(glm::inverse(rest.orientation[j]) * rigRotation(world[size_t(rig.joint[j])]));
	}

	rig.valid = true;
	return rig;
}

HiltAngles clampHiltAngles(const HiltAngles & angles) {
	const FingerPoseLimits limits;
	const HiltAngles defaults;
	auto held = [](float value, float fallback, float limit) {
		return (value == value) ? std::clamp(value, 0.f, limit) : fallback; // NaN: the default
	};
	HiltAngles clamped;
	clamped.knuckle = held(angles.knuckle, defaults.knuckle, limits.flex[size_t(Finger::Index)][0]);
	clamped.middle = held(angles.middle, defaults.middle, limits.flex[size_t(Finger::Index)][1]);
	clamped.last = held(angles.last, defaults.last, limits.flex[size_t(Finger::Index)][2]);
	clamped.thumb = held(angles.thumb, defaults.thumb, 90.f);
	return clamped;
}

HandJoints restHandJoints(const SkinnedMesh & hand, const HandRig & rig) {
	HandJoints joints;
	if(!rig.valid) {
		return joints;
	}
	std::vector<glm::mat4> world;
	restWorldMatrices(hand, world);
	for(size_t j = 0; j < HandJointCount; j++) {
		joints.position[j] = rigJointPosition(world, rig, HandJoint(j));
		joints.orientation[j] = rig.restFrame[j];
	}
	joints.valid = true;
	return joints;
}

void poseHandFromJoints(const SkinnedMesh & hand, const HandRig & rig, const HandJoints & joints,
                        std::vector<glm::mat4> & local) {

	const size_t count = hand.skeleton.size();
	local.resize(count);
	for(size_t i = 0; i < count; i++) {
		local[i] = hand.skeleton[i].restLocal;
	}
	if(!rig.valid || !joints.valid) {
		return;
	}

	// The finger joints (the metacarpals to the tips) follow the joints; the palm, the wrist and the rest stay
	std::vector<int> driven(count, -1);
	for(size_t j = 0; j < HandJointCount; j++) {
		if(rigFingerOf(j) < FingerCount) {
			driven[size_t(rig.joint[j])] = int(j);
		}
	}

	// A joint's frame relative to the joints' palm, put on the glove palm's rest frame: the frame in mesh space
	const size_t palm = size_t(HandJoint::Palm);
	const glm::quat toMesh = glm::normalize(rig.restFrame[palm] * glm::inverse(joints.orientation[palm]));

	// World rotations, parents first (a parent may come after its child in the file)
	std::vector<glm::quat> world(count, glm::quat(1.f, 0.f, 0.f, 0.f));
	std::vector<u8> done(count, 0);
	for(size_t pass = 0; pass < count; pass++) {
		bool progress = false;
		for(size_t i = 0; i < count; i++) {
			const int parent = hand.skeleton[i].parent;
			if(done[i] || (parent >= 0 && !done[size_t(parent)])) {
				continue;
			}
			const glm::quat above = (parent >= 0) ? world[size_t(parent)] : glm::quat(1.f, 0.f, 0.f, 0.f);
			if(driven[i] >= 0) {
				const size_t j = size_t(driven[i]);
				world[i] = glm::normalize(toMesh * joints.orientation[j] * rig.frameToJoint[j]);
				const Vec3f translation(hand.skeleton[i].restLocal[3]);
				local[i] = glm::translate(glm::mat4(1.f), translation) * glm::mat4_cast(glm::normalize(glm::inverse(above) * world[i]));
			} else {
				world[i] = glm::normalize(above * rigRotation(hand.skeleton[i].restLocal));
			}
			done[i] = 1;
			progress = true;
		}
		if(!progress) {
			break;
		}
	}
}

bool loadHandShape(const char * json, size_t size, HandJoints & out, std::string & error) {

	out = HandJoints();
	if(!json || size == 0) {
		error = "hand shape: empty";
		return false;
	}
	RigJson root;
	RigJsonReader reader(json, size);
	if(!reader.read(root, error)) {
		error = "hand shape: " + error;
		return false;
	}
	const RigJson * items = (root.type == RigJson::Object) ? root.member("items") : nullptr;
	if(!items || items->type != RigJson::Array) {
		error = "hand shape: no \"items\" array";
		return false;
	}

	static const char * const xyz[] = { "x", "y", "z" };
	static const char * const xyzw[] = { "x", "y", "z", "w" };
	HandJoints joints;
	std::array<bool, HandJointCount> seen = { };
	size_t item = 0;
	for(const RigJson & entry : items->items) {
		item++;
		const RigJson * name = entry.member("joint");
		size_t joint = HandJointCount;
		if(name && name->type == RigJson::Number && name->number >= 0.0 && name->number < double(HandJointCount)
		   && std::floor(name->number) == name->number) {
			joint = size_t(name->number);
		} else if(name && name->type == RigJson::String) {
			for(size_t j = 0; j < HandJointCount; j++) {
				if(name->text == RigShapeJointNames[j]) {
					joint = j;
				}
			}
		}
		if(joint == HandJointCount) {
			error = "hand shape: item " + std::to_string(item) + " names no known joint";
			return false;
		}
		if(seen[joint]) {
			error = std::string("hand shape: the joint ") + RigShapeJointNames[joint] + " is given twice";
			return false;
		}
		const RigJson * pose = entry.member("pose");
		double p[3];
		double r[4];
		if(!pose || !rigNumbers(pose->member("position"), xyz, 3, p) || !rigNumbers(pose->member("rotation"), xyzw, 4, r)) {
			error = std::string("hand shape: the joint ") + RigShapeJointNames[joint] + " has no position and rotation";
			return false;
		}
		const glm::quat rotation = rigFromUnity(r[0], r[1], r[2], r[3]);
		const float length = glm::length(rotation);
		if(!(length > 1e-6f)) {
			error = std::string("hand shape: the joint ") + RigShapeJointNames[joint] + " has a zero rotation";
			return false;
		}
		joints.position[joint] = rigFromUnity(p[0], p[1], p[2]);
		joints.orientation[joint] = rotation / length;
		seen[joint] = true;
	}
	for(size_t j = 0; j < HandJointCount; j++) {
		if(!seen[j]) {
			error = std::string("hand shape: the joint ") + RigShapeJointNames[j] + " is missing (" + std::to_string(item)
			        + " items, 26 needed)";
			return false;
		}
	}

	joints.valid = true;
	out = joints;
	return true;
}

std::string handShapeJson(const HandJoints & joints) {
	std::ostringstream json;
	json.imbue(std::locale::classic());
	json << std::setprecision(9);
	json << "{\n  \"items\": [\n";
	for(size_t j = 0; j < HandJointCount; j++) {
		// OpenXR back to Unity's space: the same flips
		const Vec3f & p = joints.position[j];
		const glm::quat & q = joints.orientation[j];
		json << "    { \"joint\": \"" << RigShapeJointNames[j] << "\", \"pose\": { \"position\": { \"x\": " << p.x
		     << ", \"y\": " << p.y << ", \"z\": " << -p.z << " }, \"rotation\": { \"x\": " << -q.x << ", \"y\": " << -q.y
		     << ", \"z\": " << q.z << ", \"w\": " << q.w << " } } }" << (j + 1 < HandJointCount ? ",\n" : "\n");
	}
	json << "  ]\n}\n";
	return json.str();
}

HandJoints mirrorHandJoints(const HandJoints & joints) {
	HandJoints mirrored = joints;
	for(size_t j = 0; j < HandJointCount; j++) {
		mirrored.position[j].x = -joints.position[j].x;
		const glm::quat & q = joints.orientation[j];
		mirrored.orientation[j] = glm::quat(q.w, q.x, -q.y, -q.z);
	}
	return mirrored;
}

HandJoints palmRelativeJoints(const HandJoints & joints) {
	HandJoints relative = joints;
	const size_t palm = size_t(HandJoint::Palm);
	const glm::quat toPalm = glm::inverse(joints.orientation[palm]);
	for(size_t j = 0; j < HandJointCount; j++) {
		relative.position[j] = toPalm * (joints.position[j] - joints.position[palm]);
		relative.orientation[j] = glm::normalize(toPalm * joints.orientation[j]);
	}
	return relative;
}

HandJoints hiltPose(Hand hand, float hiltRadius, const HiltAngles & configured) {

	const HiltAngles angles = clampHiltAngles(configured);
	const float radius = std::max(hiltRadius, 0.f);

	// The right hand in its grip frame, the palm at the origin (vr/FingerCurl.h): fingers along -Y, the thumb's
	// side -Z, the palm facing -X. A thicker hilt opens the fingers in proportion; a thinner one never closes
	// them past the configured angles
	const HandJoints open = synthesizeHandJoints(Hand::Right, HandShape());
	HandJoints joints = open;
	const float fingerBends[3] = { angles.knuckle, angles.middle, angles.last };
	const float scale = (radius > DefaultHiltRadius) ? DefaultHiltRadius / radius : 1.f;
	const size_t knuckles[] = {
		size_t(HandJoint::IndexProximal), size_t(HandJoint::MiddleProximal), size_t(HandJoint::RingProximal),
		size_t(HandJoint::LittleProximal),
	};
	for(size_t knuckle : knuckles) {
		hiltBendChain(joints, open, knuckle, fingerBends, scale);
	}

	// The hilt runs along Z through the mean centre of the four fingers' curls (each its knuckle, last joint and tip)
	Vec3f axis(0.f);
	for(size_t knuckle : knuckles) {
		axis += hiltCircleCentre(joints.position[knuckle], joints.position[knuckle + 2], joints.position[knuckle + 3]) * 0.25f;
	}

	// A fingertip still inside the hilt opens its finger
	for(size_t knuckle : knuckles) {
		float opened = scale;
		for(int step = 0; step < 32 && hiltAxisDistance(joints.position[knuckle + 3], axis) < radius - HiltTipTolerance; step++) {
			opened *= HiltOpenStep;
			hiltBendChain(joints, open, knuckle, fingerBends, opened);
		}
	}

	// The thumb over the hilt, its tip across the index and the middle finger's middle segments
	const float tipZ = 0.5f * (joints.position[size_t(HandJoint::IndexIntermediate)].z
	                           + joints.position[size_t(HandJoint::MiddleIntermediate)].z);
	float thumbBend = angles.thumb * scale;
	hiltLayThumb(joints, open, thumbBend, tipZ);
	for(int step = 0; step < 32 && hiltAxisDistance(joints.position[size_t(HandJoint::ThumbTip)], axis) < radius - HiltTipTolerance; step++) {
		thumbBend *= HiltOpenStep;
		hiltLayThumb(joints, open, thumbBend, tipZ);
	}

	// The frames as the runtime's, then the hilt's axis through the grip frame's origin
	orientHandJoints(joints, Vec3f(0.f, -1.f, 0.f), Vec3f(1.f, 0.f, 0.f), Vec3f(0.f, 0.f, -1.f));
	axis.z = 0.f;
	for(Vec3f & position : joints.position) {
		position -= axis;
	}
	joints.valid = true;

	return (hand == Hand::Right) ? joints : mirrorHandJoints(joints);
}

Vec3f palmBehindGrip(Hand hand, const Vec3f & gripPosition, const glm::quat & gripOrientation, float handleRadius,
                     float palmRadius) {
	const float side = (hand == Hand::Right) ? 1.f : -1.f;
	return gripPosition + gripOrientation * Vec3f(side * (handleRadius + palmRadius), 0.f, 0.f);
}

HandJoints blendHandJoints(const HandJoints & open, const HandJoints & closed, const std::array<float, FingerCount> & amounts) {
	HandJoints blended = open;
	if(!open.valid || !closed.valid) {
		return blended;
	}
	for(size_t j = 0; j < HandJointCount; j++) {
		const size_t finger = rigFingerOf(j);
		if(finger < FingerCount) {
			const float amount = std::clamp(amounts[finger], 0.f, 1.f);
			blended.orientation[j] = glm::normalize(glm::slerp(open.orientation[j], closed.orientation[j], amount));
		}
	}
	// The positions along the frames' -Z with the open hand's bone lengths, each chain from its root
	for(size_t j = 0; j < HandJointCount; j++) {
		if(rigHasBone(j)) {
			const float length = glm::distance(open.position[j + 1], open.position[j]);
			blended.position[j + 1] = blended.position[j] + blended.orientation[j] * Vec3f(0.f, 0.f, -length);
		}
	}
	return blended;
}

GloveCurl gloveCurlAxis(const SkinnedMesh & hand, const HandRig & rig, const std::vector<glm::mat4> & local) {

	GloveCurl curl;
	if(!rig.valid || local.size() != hand.skeleton.size()) {
		return curl;
	}

	// The joints in mesh space: each local composed with its parents'
	auto at = [&](HandJoint joint) {
		size_t i = size_t(rig.joint[size_t(joint)]);
		glm::mat4 matrix = local[i];
		for(int parent = hand.skeleton[i].parent; parent >= 0; parent = hand.skeleton[size_t(parent)].parent) {
			matrix = local[size_t(parent)] * matrix;
		}
		return Vec3f(matrix[3]);
	};

	const Vec3f across = at(HandJoint::IndexProximal) - at(HandJoint::LittleProximal);
	const HandJoint knuckles[] = {
		HandJoint::IndexProximal, HandJoint::MiddleProximal, HandJoint::RingProximal, HandJoint::LittleProximal,
	};
	Vec3f centres(0.f);
	Vec3f normals(0.f);
	for(HandJoint knuckle : knuckles) {
		// The circle through three points: the centre of their triangle's circumcircle, the triangle's normal
		const Vec3f tip = at(HandJoint(size_t(knuckle) + 3));
		const Vec3f a = at(knuckle) - tip;
		const Vec3f b = at(HandJoint(size_t(knuckle) + 2)) - tip;
		const Vec3f normal = glm::cross(a, b);
		const float area = glm::dot(normal, normal);
		if(!(area > 1e-14f)) {
			return curl;
		}
		centres += tip + glm::cross(glm::dot(a, a) * b - glm::dot(b, b) * a, normal) / (2.f * area);
		normals += glm::normalize(normal) * (glm::dot(normal, across) < 0.f ? -1.f : 1.f);
	}
	if(!(glm::length(normals) > 1e-6f)) {
		return curl;
	}
	curl.centre = centres * 0.25f;
	curl.axis = glm::normalize(normals);
	curl.valid = true;
	return curl;
}

glm::quat gloveOnHandle(const GloveCurl & curl, const glm::quat & toGrip) {
	if(!curl.valid) {
		return toGrip;
	}
	// The handle's direction in the glove's mesh space, signed to lie with the curl axis; the least rotation onto it
	Vec3f handle = glm::inverse(toGrip) * Vec3f(0.f, 0.f, 1.f);
	if(glm::dot(handle, curl.axis) < 0.f) {
		handle = -handle;
	}
	const Vec3f axis = glm::cross(curl.axis, handle);
	const float sine = glm::length(axis);
	const float cosine = glm::dot(curl.axis, handle);
	if(!(sine > 1e-6f)) {
		return toGrip;
	}
	return glm::normalize(toGrip * glm::angleAxis(std::atan2(sine, cosine), axis / sine));
}

const char * hiltPoseSourceName(HiltPoseSource source) {
	switch(source) {
		case HiltPoseSource::None: return "none";
		case HiltPoseSource::Default: return "default";
		case HiltPoseSource::Captured: return "captured";
	}
	return "none";
}

CustomHandTarget customHandTarget(const CustomFingerInput & input, const HandJoints & runtime,
                                  const CustomHandShapes & shapes, const FingerPoseLimits & limits) {

	CustomHandTarget target;
	if(input.holds) {
		target.source = CustomFingerSource::Held;
		target.hilt = shapes.captured.valid ? HiltPoseSource::Captured : HiltPoseSource::Default;
		target.joints = shapes.captured.valid ? shapes.captured : shapes.hilt;
	} else if(runtime.valid) {
		target.source = CustomFingerSource::Joints;
		target.joints = palmRelativeJoints(runtime);
	} else if(input.controls) {
		target.source = CustomFingerSource::Controls;
		const float thumb = input.thumbTouch ? 1.f : 0.f;
		const float grip = std::clamp(input.grip, 0.f, 1.f);
		const std::array<float, FingerCount> amounts = { { thumb, std::clamp(input.trigger, 0.f, 1.f), grip, grip, grip } };
		target.joints = blendHandJoints(shapes.open, shapes.grab, amounts);
	} else {
		target.joints = shapes.open;
	}

	// What the glove is posed by, read as the joints are (the thumb's first joint past its rest angle)
	if(target.joints.valid) {
		FingerPoseLimits read = limits;
		read.flex[size_t(Finger::Thumb)][0] += ThumbRestFlexion;
		target.pose = fingerPoseFromJoints(target.joints, read);
		float & thumbRoot = target.pose.flex[size_t(Finger::Thumb)][0];
		thumbRoot = std::clamp(thumbRoot - ThumbRestFlexion, 0.f, limits.flex[size_t(Finger::Thumb)][0]);
	}
	return target;
}

void poseHand(const SkinnedMesh & hand, const HandRig & rig, const FingerPose & pose, std::vector<glm::mat4> & local) {

	local.resize(hand.skeleton.size());
	for(size_t i = 0; i < hand.skeleton.size(); i++) {
		local[i] = hand.skeleton[i].restLocal;
	}
	if(!rig.valid) {
		return;
	}

	for(size_t finger = 0; finger < FingerCount; finger++) {
		for(size_t k = 0; k < FingerJointCount; k++) {
			const float flex = pose.flex[finger][k];
			const float spread = (k == 0) ? pose.spread[finger] : 0.f;
			if(flex == 0.f && spread == 0.f) {
				continue; // exactly the rest
			}
			glm::mat4 & matrix = local[size_t(rig.joint[size_t(flexingJoint(Finger(finger), k))])];
			if(spread != 0.f) {
				matrix = matrix * rigTurn(spread, rig.spreadAxis[finger]);
			}
			matrix = matrix * rigTurn(flex, rig.flexAxis[finger * FingerJointCount + k]);
		}
	}
}

const char * customFingerSourceName(CustomFingerSource source) {
	switch(source) {
		case CustomFingerSource::None: return "none";
		case CustomFingerSource::Joints: return "joints";
		case CustomFingerSource::Controls: return "controls";
		case CustomFingerSource::Held: return "held";
	}
	return "none";
}

CustomFingerTarget customFingerTarget(const CustomFingerInput & input, const HandJoints & joints,
                                      const FingerPoseLimits & limits) {

	CustomFingerTarget target;
	if(input.holds) {
		target.source = CustomFingerSource::Held;
		target.pose = fingerPoseClosed(limits);
	} else if(joints.valid) {
		// The thumb's first joint is read past its rest angle: its limit there is raised by as much
		FingerPoseLimits read = limits;
		read.flex[size_t(Finger::Thumb)][0] += ThumbRestFlexion;
		target.source = CustomFingerSource::Joints;
		target.pose = fingerPoseFromJoints(joints, read);
		float & thumbRoot = target.pose.flex[size_t(Finger::Thumb)][0];
		thumbRoot = std::clamp(thumbRoot - ThumbRestFlexion, 0.f, limits.flex[size_t(Finger::Thumb)][0]);
	} else if(input.controls) {
		target.source = CustomFingerSource::Controls;
		target.pose = fingerPoseFromControls(input.trigger, input.grip, input.thumbTouch, limits);
	}
	return target;
}

} // namespace vr
