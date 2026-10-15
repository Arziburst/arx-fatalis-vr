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

#include "vr/VRWeapon.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>

#include "animation/AnimationRender.h"
#include "core/Config.h"
#include "core/GameEvent.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Player.h"
#include "graphics/GraphicsTypes.h"
#include "io/log/Logger.h"
#include "scene/Object.h"
#include "vr/ArmIK.h"
#include "vr/VRArms.h"
#include "vr/VRBody.h"
#include "vr/WeaponGrip.h"

namespace vr {

namespace {

struct HeldWeaponState {
	Entity * weapon = nullptr;           //!< the weapon this module linked to the hand, while it holds it
	bool inHand = false;
	// The blade, read from the mesh once per weapon and hand frame
	const EERIE_3DOBJ * measuredObject = nullptr;
	Vec3f measuredThumb = Vec3f(0.f);
	VertexId attach;
	VertexId tip;
	bool corrected = false;
	glm::quat correction = glm::quat(1.f, 0.f, 0.f, 0.f);
	// The glove's frame (#385.10): the blade's axis, its handle and edge in the mesh, the class the anchor is read by
	Vec3f bladeAxis = Vec3f(0.f, 0.f, 1.f);
	WeaponHandle handle;
	WeaponClass weaponClass = WeaponClass::Sword;
	glm::quat placedRotation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< the last placement's rotation (the draw, the melee)
	bool placedInGlove = false;                                //!< and its frame
	// The arm layer (F-67)
	bool suppressedBefore = false;
	std::string animationBefore;
	HeldWeaponStatus status;
};

HeldWeaponState g_weapon;

const char * weaponKind(const Entity * weapon) {
	if(!weapon) {
		return "none";
	}
	if(weapon->type_flags & OBJECT_TYPE_DAGGER) {
		return "dagger";
	}
	if(weapon->type_flags & OBJECT_TYPE_1H) {
		return "1h";
	}
	if(weapon->type_flags & OBJECT_TYPE_2H) {
		return "2h";
	}
	if(weapon->type_flags & OBJECT_TYPE_BOW) {
		return "bow";
	}
	return "none";
}

//! A weapon the IK hand holds: a dagger, a one- or two-handed weapon (bows are left to the engine)
bool isMeleeWeapon(const Entity * weapon) {
	return weapon && (weapon->type_flags & (OBJECT_TYPE_DAGGER | OBJECT_TYPE_1H | OBJECT_TYPE_2H));
}

//! The player's link holding \a weapon, or nullptr
const EERIE_LINKED * findLink(const EERIE_3DOBJ * obj, const Entity * weapon) {
	if(!obj || !weapon) {
		return nullptr;
	}
	for(const EERIE_LINKED & link : obj->linked) {
		if(link.io == weapon) {
			return &link;
		}
	}
	return nullptr;
}

/*!
 * The weapon this module holds, resolved again through the player's weapon slot: a load (`loadslot`, a quick
 * load) frees the entities before the next updateHeldWeapon, and the kept pointer would dangle (#210)
 */
Entity * heldEntity() {
	if(!g_weapon.inHand || !g_weapon.weapon) {
		return nullptr;
	}
	Entity * equipped = entities.get(player.equiped[EQUIP_SLOT_WEAPON]);
	return equipped == g_weapon.weapon ? equipped : nullptr;
}

void noteState(const char * state, const char * reason, const Entity * weapon) {
	const std::string key = std::string(state) + "/" + reason;
	static std::string before;
	if(key == before) {
		return;
	}
	before = key;
	gameevent::Event("vr.weapon", 200)("state", state)("reason", reason)
		("item", weapon ? weapon->idString() : std::string("none"))("kind", weaponKind(weapon));
}

//! Read the blade from the weapon's mesh and turn it onto the hand's thumb axis (the grip's -Z)
void measure(Entity * weapon, const EERIE_LINKED & link) {

	HeldWeaponStatus & status = g_weapon.status;
	const HandFrame * frame = handRestFrame(Hand::Right);
	const EERIE_3DOBJ * obj = weapon->obj;
	if(g_weapon.measuredObject == obj && frame && frame->thumb == g_weapon.measuredThumb) {
		return;
	}
	g_weapon.measuredObject = obj;
	g_weapon.measuredThumb = frame ? frame->thumb : Vec3f(0.f);
	g_weapon.corrected = false;
	g_weapon.correction = glm::quat(1.f, 0.f, 0.f, 0.f);
	status.measured = false;

	g_weapon.attach = link.lidx2;
	std::vector<Vec3f> vertices;
	vertices.reserve(obj->vertexlist.size());
	const Vec3f attach = obj->vertexlist[link.lidx2].v;
	for(const EERIE_VERTEX & vertex : obj->vertexlist) {
		vertices.push_back(vertex.v - attach);
	}
	const BladeAxis blade = bladeAxisFromMesh(vertices);
	if(!blade.valid || !frame) {
		LogWarning << "[vr] weapon: " << weapon->idString() << " held as the asset has it - "
		           << (!blade.valid ? "no blade axis in its mesh" : "the hand has no usable frame");
		return;
	}
	g_weapon.tip = VertexId(blade.tip);
	g_weapon.correction = bladeOntoHandle(blade.axis, frame->thumb);
	g_weapon.corrected = true;
	// For the glove's frame (#385.10): the handle the fingers close on, the edge, the anchor's class
	g_weapon.bladeAxis = blade.axis;
	g_weapon.handle = weaponHandleFromMesh(vertices, blade);
	g_weapon.weaponClass = weaponClassOf((weapon->type_flags & OBJECT_TYPE_DAGGER) != 0,
	                                     (weapon->type_flags & OBJECT_TYPE_2H) != 0, weapon->className(),
	                                     weapon->weaponmaterial);

	status.measured = true;
	status.bladeLength = blade.length;
	status.assetAngle = angleDegrees(blade.axis, frame->thumb);
	status.correction = status.assetAngle;
	status.weaponClass = weaponClassName(g_weapon.weaponClass);
	status.handleLength = g_weapon.handle.length;

	std::ostringstream text;
	text << std::fixed << std::setprecision(1) << "blade " << blade.length << " units, the asset's blade "
	     << status.assetAngle << " deg from the handle axis (knuckles " << angleDegrees(blade.axis, frame->knuckles)
	     << ", palm " << angleDegrees(blade.axis, frame->palm) << "); class " << status.weaponClass << " (material \""
	     << weapon->weaponmaterial << "\"), handle " << g_weapon.handle.length << " units from the pommel end "
	     << g_weapon.handle.pommel << (g_weapon.handle.guardFound ? " to the guard" : " (no guard found)") << ", radius "
	     << g_weapon.handle.radius;
	LogInfo << "[vr] weapon: " << weapon->idString() << " (" << weaponKind(weapon) << ", " << obj->vertexlist.size()
	        << " vertices, attach vertex " << size_t(link.lidx2) << "): " << text.str()
	        << " - turned onto the handle (grip -Z)";
	gameevent::Event("vr.weapon.grip")("item", weapon->idString())("bladeLength", blade.length)
		("assetAngle", status.assetAngle)("class", status.weaponClass)("handle", g_weapon.handle.length);
}

/*!
 * The held weapon is drawn from the glove's hilt frame (#385.10): the model draws our glove and the right one was
 * posed this frame; else (arx, a glove that did not load) from the Arx hand as before
 */
const CustomArmStatus * gloveHiltFrame() {
	if(!armsModelInfo().glove || !customArmDrawn(Hand::Right)) {
		return nullptr;
	}
	const CustomArmStatus & glove = customArmStatus(Hand::Right);
	return glove.hiltFrame ? &glove : nullptr;
}

//! The sword's tilt in use (cfg.ini vr_sword_tilt): only a sword leans; the other classes are held along the handle
float appliedTilt() {
	return g_weapon.weaponClass == WeaponClass::Sword ? swordTiltVariant(config.input.vrSwordTilt) : 0.f;
}

//! The weapon mesh to the world in the glove: the blade along the glove's handle line, the edge to its knuckles, tilted
glm::quat gloveRotation(const CustomArmStatus & glove) {
	GloveHilt hilt;
	hilt.centre = glove.hiltCentre;
	hilt.axis = glove.hiltAxis;
	hilt.knuckles = glove.knuckles;
	return weaponInGlove(g_weapon.bladeAxis, g_weapon.handle.edge, hilt, appliedTilt());
}

//! The anchor (the point of the handle on the glove's grip axis) in the mesh, off the attach vertex; \a scale the weapon's
Vec3f anchorInMesh(const CustomArmStatus & glove, float scale) {
	const WeaponHandle & handle = g_weapon.handle;
	const float offset = weaponAnchorOffset(g_weapon.weaponClass, handle.length, glove.handWidth / std::max(scale, 1e-3f));
	return handle.centre + g_weapon.bladeAxis * (handle.pommel + offset);
}

} // anonymous namespace

void updateHeldWeapon() {

	HeldWeaponStatus & status = g_weapon.status;
	Entity * io = entities.player();
	if(!io || !io->obj) {
		return;
	}

	Entity * weapon = entities.get(player.equiped[EQUIP_SLOT_WEAPON]);
	if(weapon && !weapon->obj) {
		weapon = nullptr;
	}
	status.item = weapon ? weapon->idString() : std::string();
	status.kind = weaponKind(weapon);

	const bool melee = isMeleeWeapon(weapon);
	const bool driven = playerBodyStatus().hands[size_t(Hand::Right)].posed;

	if(melee && driven && !status.sheathed) {
		const EERIE_LINKED * link = findLink(io->obj, weapon);
		if(!link || link->lidx != io->obj->fastaccess.primary_attach) {
			ARX_EQUIPMENT_AttachPlayerWeaponToHand();
			status.relinks++;
			link = findLink(io->obj, weapon);
		}
		if(!link || link->lidx != io->obj->fastaccess.primary_attach) {
			// The model has no primary_attach vertex: the engine keeps the weapon where it is
			g_weapon.inHand = false;
			g_weapon.weapon = nullptr;
			status.inHand = false;
			status.place = "engine";
			noteState("engine", "no hand attach point on the model", weapon);
			return;
		}
		g_weapon.weapon = weapon;
		g_weapon.inHand = true;
		status.inHand = true;
		status.place = "hand";
		status.handFrames++;
		measure(weapon, *link);
		noteState("hand", "the IK drives the right arm", weapon);
		return;
	}

	// The engine's: give back a weapon this module took, unless the engine wants it in the hand (combat mode)
	if(g_weapon.inHand && weapon && weapon == g_weapon.weapon && !(player.Interface & INTER_COMBATMODE)) {
		const EERIE_LINKED * link = findLink(io->obj, weapon);
		if(link && link->lidx == io->obj->fastaccess.primary_attach) {
			ARX_EQUIPMENT_AttachPlayerWeaponToBack();
			status.relinks++;
		}
	}
	g_weapon.inHand = false;
	g_weapon.weapon = nullptr;
	status.inHand = false;
	status.place = weapon ? "engine" : "none";
	noteState(weapon ? "engine" : "none",
	          !weapon ? "no weapon equipped" : !melee ? "not a melee weapon"
	          : status.sheathed ? "sheathed" : "the right arm is the engine's", weapon);
}

const char * toggleHeldWeapon() {

	HeldWeaponStatus & status = g_weapon.status;
	Entity * weapon = entities.get(player.equiped[EQUIP_SLOT_WEAPON]);
	const char * result = "no_weapon";
	if(isMeleeWeapon(weapon) && weapon->obj) {
		status.sheathed = !status.sheathed;
		status.toggles++;
		result = status.sheathed ? "sheathed" : "drawn";
	}
	gameevent::Event("vr.weapon.toggle")("result", result)
		("item", weapon ? weapon->idString() : std::string("none"))("kind", weaponKind(weapon));
	return result;
}

bool heldWeaponTransform(const Entity * linked, Vec3f & attach, glm::quat & rotation) {
	if(!g_weapon.inHand || !linked || linked != g_weapon.weapon || !g_weapon.corrected
	   || linked->obj != g_weapon.measuredObject) {
		return false;
	}
	const CustomArmStatus * glove = gloveHiltFrame();
	if(!glove || !g_weapon.handle.valid) {
		rotation = rotation * g_weapon.correction;
		g_weapon.placedRotation = rotation;
		g_weapon.placedInGlove = false;
		return true;
	}
	// The anchor on the glove's hold point: the engine draws vertex v at attach + rotation * (v + origin - attach
	// vertex) * scale (AnimationRender.cpp), so the attach point is chosen to put the anchor (off the attach vertex)
	// there
	const EERIE_3DOBJ * obj = linked->obj;
	const float scale = linked->scale;
	rotation = gloveRotation(*glove);
	attach = glove->hiltCentre - rotation * ((anchorInMesh(*glove, scale) + obj->vertexlist[obj->origin].v) * scale);
	g_weapon.placedRotation = rotation;
	g_weapon.placedInGlove = true;
	return true;
}

bool suppressArmAnimation(const Entity * io, const std::string & armAnimation) {

	HeldWeaponStatus & status = g_weapon.status;
	if(!io || io != entities.player()) {
		return false;
	}

	// The IK drove the arms in the last frame (the body is posed after the animation, so this frame's
	// decision is not known yet; a one-frame lag at a controller's arrival or loss)
	const bool suppress = playerBodyStatus().active && !armAnimation.empty();
	status.armAnimSuppressed = suppress;
	status.armAnimation = armAnimation;
	if(suppress) {
		status.suppressedFrames++;
	}
	if(suppress != g_weapon.suppressedBefore || armAnimation != g_weapon.animationBefore) {
		g_weapon.suppressedBefore = suppress;
		g_weapon.animationBefore = armAnimation;
		if(!armAnimation.empty()) {
			gameevent::Event("vr.body.armAnim", 200)("state", suppress ? "suppressed" : "applied")("anim", armAnimation);
		}
	}
	return suppress;
}

Entity * weaponInHand() {
	return heldEntity();
}

bool placeHeldWeapon(Vec3f & hilt, Vec3f & tip) {

	const Entity * weapon = heldEntity();
	if(!weapon) {
		return false;
	}
	Entity * io = entities.player();
	if(!io || !io->obj || !io->obj->m_skeleton) {
		return false;
	}
	const EERIE_LINKED * link = findLink(io->obj, weapon);
	if(!link || !link->lgroup || !link->obj || link->obj != weapon->obj) {
		return false;
	}
	EERIE_3DOBJ * obj = link->obj;
	if(obj->vertexWorldPositions.size() != obj->vertexlist.size()) {
		return false;
	}

	// As Cedric_AnimateDrawEntityRender draws the player's linked objects
	glm::quat rotation = io->obj->m_skeleton->bones[link->lgroup].anim.quat;
	Vec3f attach = io->obj->vertexWorldPositions[link->lidx].v;
	heldWeaponTransform(link->io, attach, rotation);
	TransformInfo t(attach, rotation, link->io ? link->io->scale : 1.f);
	t.pos = t(obj->vertexlist[obj->origin].v - obj->vertexlist[link->lidx2].v);
	DrawEERIEInter_ModelTransform(obj, t);

	hilt = obj->vertexWorldPositions[link->lidx2].v;
	tip = (g_weapon.corrected && obj == g_weapon.measuredObject) ? obj->vertexWorldPositions[g_weapon.tip].v : hilt;
	return true;
}

const HeldWeaponStatus & heldWeaponStatus() {

	HeldWeaponStatus & status = g_weapon.status;
	status.drawn = false;
	status.frame = (gloveHiltFrame() && g_weapon.handle.valid) ? "glove" : "arx";
	status.tilt = swordTiltVariant(config.input.vrSwordTilt);
	if(!g_weapon.inHand || !g_weapon.weapon || !status.measured) {
		return status; // nothing held yet: before the player entity exists, entities.player() must not be asked
	}
	const Entity * weapon = heldEntity();
	Entity * io = entities.player();
	const HandFrame * frame = handRestFrame(Hand::Right);
	if(!weapon || !io || !io->obj || !frame) {
		return status;
	}
	const EERIE_3DOBJ * obj = weapon->obj;
	if(!obj || obj != g_weapon.measuredObject || obj->vertexWorldPositions.size() != obj->vertexlist.size()) {
		return status;
	}
	const EERIE_LINKED * link = findLink(io->obj, weapon);
	if(!link || !link->lgroup || !io->obj->m_skeleton) {
		return status;
	}

	status.hilt = obj->vertexWorldPositions[g_weapon.attach].v;
	status.tip = obj->vertexWorldPositions[g_weapon.tip].v;
	const CustomArmStatus * glove = g_weapon.placedInGlove ? gloveHiltFrame() : nullptr;
	if(glove) {
		// In the glove (#385.10): the blade read in the controller grip's own axes, the drawn anchor against the glove's
		// hold point now, the drawn edge against its knuckles
		status.bladeGrip = directionInGrip(status.tip - status.hilt, glove->hiltGrip, quat_identity());
		const float scale = weapon->scale;
		const Vec3f anchor = status.hilt + g_weapon.placedRotation * (anchorInMesh(*glove, scale) * scale);
		status.hiltToGrip = glm::distance(anchor, glove->hiltCentre);
		const Vec3f blade = g_weapon.placedRotation * g_weapon.bladeAxis;
		const Vec3f edge = g_weapon.placedRotation * g_weapon.handle.edge;
		status.edgeToKnuckles = angleDegrees(edge - blade * glm::dot(edge, blade),
		                                     glove->knuckles - blade * glm::dot(glove->knuckles, blade));
		status.anchor = weaponAnchorOffset(g_weapon.weaponClass, g_weapon.handle.length,
		                                   glove->handWidth / std::max(scale, 1e-3f)) * scale;
		status.handWidth = glove->handWidth;
		status.tiltApplied = appliedTilt();
		status.frame = "glove";
	} else {
		status.frame = "arx";
		const glm::quat & hand = io->obj->m_skeleton->bones[link->lgroup].anim.quat;
		status.bladeGrip = directionInGrip(status.tip - status.hilt, hand, handToGrip(*frame));
		status.hiltToGrip = 0.f;
		status.edgeToKnuckles = 0.f;
		status.anchor = 0.f;
		status.handWidth = 0.f;
		status.tiltApplied = 0.f;
	}
	if(glm::length(status.bladeGrip) > 0.f) {
		status.bladeGrip = glm::normalize(status.bladeGrip);
	}
	status.bladeAngle = angleDegrees(status.bladeGrip, Vec3f(0.f, 0.f, -1.f));
	status.drawn = true;

	return status;
}

} // namespace vr
