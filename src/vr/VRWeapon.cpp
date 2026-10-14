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
#include "core/GameEvent.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Player.h"
#include "graphics/GraphicsTypes.h"
#include "io/log/Logger.h"
#include "scene/Object.h"
#include "vr/ArmIK.h"
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

	status.measured = true;
	status.bladeLength = blade.length;
	status.assetAngle = angleDegrees(blade.axis, frame->thumb);
	status.correction = status.assetAngle;

	std::ostringstream text;
	text << std::fixed << std::setprecision(1) << "blade " << blade.length << " units, the asset's blade "
	     << status.assetAngle << " deg from the handle axis (knuckles " << angleDegrees(blade.axis, frame->knuckles)
	     << ", palm " << angleDegrees(blade.axis, frame->palm) << ")";
	LogInfo << "[vr] weapon: " << weapon->idString() << " (" << weaponKind(weapon) << ", " << obj->vertexlist.size()
	        << " vertices, attach vertex " << size_t(link.lidx2) << "): " << text.str()
	        << " - turned onto the handle (grip -Z)";
	gameevent::Event("vr.weapon.grip")("item", weapon->idString())("bladeLength", blade.length)
		("assetAngle", status.assetAngle);
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

bool heldWeaponRotation(const Entity * linked, glm::quat & rotation) {
	if(!g_weapon.inHand || !linked || linked != g_weapon.weapon || !g_weapon.corrected
	   || linked->obj != g_weapon.measuredObject) {
		return false;
	}
	rotation = rotation * g_weapon.correction;
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
	heldWeaponRotation(link->io, rotation);
	TransformInfo t(io->obj->vertexWorldPositions[link->lidx].v, rotation, link->io ? link->io->scale : 1.f);
	t.pos = t(obj->vertexlist[obj->origin].v - obj->vertexlist[link->lidx2].v);
	DrawEERIEInter_ModelTransform(obj, t);

	hilt = obj->vertexWorldPositions[link->lidx2].v;
	tip = (g_weapon.corrected && obj == g_weapon.measuredObject) ? obj->vertexWorldPositions[g_weapon.tip].v : hilt;
	return true;
}

const HeldWeaponStatus & heldWeaponStatus() {

	HeldWeaponStatus & status = g_weapon.status;
	status.drawn = false;
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
	const glm::quat & hand = io->obj->m_skeleton->bones[link->lgroup].anim.quat;
	status.bladeGrip = directionInGrip(status.tip - status.hilt, hand, handToGrip(*frame));
	if(glm::length(status.bladeGrip) > 0.f) {
		status.bladeGrip = glm::normalize(status.bladeGrip);
	}
	status.bladeAngle = angleDegrees(status.bladeGrip, Vec3f(0.f, 0.f, -1.f));
	status.drawn = true;

	return status;
}

} // namespace vr
