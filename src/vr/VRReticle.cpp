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

#include "vr/VRReticle.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "cinematic/CinematicController.h"
#include "core/GameEvent.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Player.h"
#include "graphics/Raycast.h"
#include "gui/Dragging.h"
#include "gui/Interface.h"
#include "gui/Menu.h"
#include "gui/Note.h"
#include "math/GtxFunctions.h"
#include "math/Vector.h"
#include "scene/Object.h"
#include "vr/HandReticle.h"
#include "vr/HeadPose.h"
#include "vr/HeadTracking.h"
#include "vr/WorldScale.h"

namespace vr {

namespace {

struct ReticleHandState {
	HoverFilter hover;
	bool ray = false;               //!< an aim ray this frame
	Vec3f localOrigin = Vec3f(0.f); //!< the aim ray in LOCAL, meters
	Vec3f localDirection = Vec3f(0.f);
	Vec3f origin = Vec3f(0.f);      //!< the aim ray in the world, game units
	Vec3f direction = Vec3f(0.f);
	bool hit = false;
	float distance = 0.f;
	bool inFront = false;           //!< the hit projects in front of the head camera
	Vec2s pixel = Vec2s(0);
	bool claims = false;
	s32 pressEntity = HoverFilter::None;
};

struct ReticleState {

	//! The head camera of the last update (noteHeadCamera)
	struct Camera {
		bool firstPerson = false;
		Vec3f position = Vec3f(0.f);
		float bodyYaw = 0.f;
		Vec3f head = Vec3f(0.f);   //!< the head the camera was placed with, LOCAL
		float referenceYaw = 0.f;
		glm::mat4 worldToScreen = glm::mat4(1.f);
	} camera;

	bool on = false;
	std::string loggedOff;
	bool logged = false;
	std::array<ReticleHandState, HandCount> hands;
	PickHistory history;
	Hand owner = Hand::Right;
	bool cursorOnWorld = false;
	bool cursorOnPanel = false;
	int enginePickOnly = 0; //!< EnginePickOnly scopes alive
	s32 act = HoverFilter::None; //!< what a hand's act chose this frame, for takeReticleAct
	ReticleStatus status;

};

ReticleState g_reticle;

const char * ReticleHandNames[] = { "left", "right" };

//! What the engine's own pick accepts as "under the cursor" (GetFirstInterAtPos), for the world
bool isReticlePickable(const Entity & entity) {
	return entity.obj
	       && !(entity.ioflags & (IO_CAMERA | IO_MARKER))
	       && (entity.gameFlags & GFLAG_INTERACTIVITY)
	       && (entity.gameFlags & GFLAG_ISINTREATZONE)
	       && entity.show == SHOW_FLAG_IN_SCENE
	       && &entity != entities.player();
}

//! The engine's reach for the pick (GetFirstInterAtPos: 350 units from the player, 850 with telekinesis)
bool isInPickReach(const Entity & entity) {
	const float reach = player.m_telekinesis ? 850.f : 350.f;
	return closerThan(entity.pos, player.pos, reach);
}

Entity * reticleEntityById(s32 id) {
	if(id == HoverFilter::None) {
		return nullptr;
	}
	return entities.get(EntityHandle(id));
}

std::string reticleEntityLabel(s32 id) {
	Entity * entity = reticleEntityById(id);
	return entity ? gameevent::entityName(entity) : std::string("none");
}

bool isReticleTakeable(s32 id) {
	Entity * entity = reticleEntityById(id);
	return entity && (entity->ioflags & IO_ITEM);
}

//! Why the reticles are off, or nullptr when they are on
const char * reticleOffReason() {
	if(!stereoActive()) {
		return "no session";
	}
	if(!entities.player()) {
		return "no player";
	}
	if(ARXmenu.mode() != Mode_InGame) {
		return "menu";
	}
	if(isInCinematic()) {
		return "cinematic";
	}
	if(BLOCK_PLAYER_CONTROLS) {
		return "controls blocked";
	}
	if(!g_reticle.camera.firstPerson) {
		return "not first person";
	}
	if(!headStatus().referenced) {
		return "no head reference";
	}
	if(player.Interface & INTER_PLAYERBOOK) {
		return "book";
	}
	if(g_note.isOpen()) {
		return "note";
	}
	// The weapon drawn: the engine's hover is off and the trigger attacks (a follow-up of #37)
	if(player.Interface & INTER_COMBATMODE) {
		return "combat mode";
	}
	if(player.doingmagic >= 2) {
		return "magic";
	}
	return nullptr;
}

/*!
 * The nearest entity face along the ray within \a maxDistance - what raycastEntities does, but the
 * entity being dragged is not in the way (it moves along this very ray) and only what is in the scene counts
 */
Entity * castReticleEntities(const Vec3f & origin, const Vec3f & direction, float maxDistance, float & distance) {

	const Vec3f segment = direction * maxDistance;
	const Vec3f inverse = 1.f / segment;
	Entity * found = nullptr;
	float t = std::numeric_limits<float>::max();

	for(Entity & entity : entities) {
		if(!entity.obj
		   || &entity == g_draggedEntity
		   || &entity == entities.player()
		   || entity.show != SHOW_FLAG_IN_SCENE
		   || !(entity.gameFlags & GFLAG_ISINTREATZONE)
		   || (entity.gameFlags & (GFLAG_INVISIBILITY | GFLAG_MEGAHIDE))
		   || (entity.ioflags & (IO_CAMERA | IO_MARKER))
		   || entity.obj->vertexWorldPositions.size() != entity.obj->vertexlist.size()) {
			continue;
		}

		// The box first (the slab test of raycastEntities)
		const Vec3f low = (entity.bbox3D.min - origin) * inverse;
		const Vec3f high = (entity.bbox3D.max - origin) * inverse;
		// (not "near" / "far": empty macros of windows.h)
		const Vec3f exits = glm::max(low, high);
		const Vec3f entries = glm::min(low, high);
		const float tmax = std::min(std::min(exits.x, exits.y), exits.z);
		const float tmin = std::max(std::max(entries.x, entries.y), entries.z);
		if(tmax < 0.f || tmin > tmax || tmin > t) {
			continue;
		}

		for(const EERIE_FACE & face : entity.obj->facelist) {
			const Vec3f & v0 = entity.obj->vertexWorldPositions[face.vid[0]].v;
			const Vec3f & v1 = entity.obj->vertexWorldPositions[face.vid[1]].v;
			const Vec3f & v2 = entity.obj->vertexWorldPositions[face.vid[2]].v;
			Vec3f hit;
			if(arx::intersectLineTriangle(origin, segment, v0, v1, v2, hit) && hit.x >= 0.f && hit.x <= 1.f && hit.x < t) {
				t = hit.x;
				found = &entity;
			}
		}
	}

	distance = found ? t * maxDistance : 0.f;
	return found;
}

void noteReticleOnOff(const char * off) {
	const bool on = (off == nullptr);
	const std::string reason = off ? off : "";
	if(g_reticle.logged && on == g_reticle.on && reason == g_reticle.loggedOff) {
		return;
	}
	g_reticle.logged = true;
	g_reticle.loggedOff = reason;
	gameevent::Event("vr.reticle", 200)("state", on ? "on" : "off")("reason", on ? "game" : off);
}

} // anonymous namespace

void noteHeadCamera(bool firstPerson, const Vec3f & position, float bodyYaw, const glm::mat4 & worldToScreen) {
	ReticleState::Camera & camera = g_reticle.camera;
	camera.firstPerson = firstPerson;
	if(!firstPerson) {
		return;
	}
	const HeadStatus & head = headStatus();
	camera.position = position;
	camera.bodyYaw = bodyYaw;
	camera.head = head.position;
	camera.referenceYaw = head.reference.yaw;
	camera.worldToScreen = worldToScreen;
}

void updateReticles(const std::array<HandAim, HandCount> & aims, double now) {

	ReticleStatus & status = g_reticle.status;
	// An act the engine did not take in its frame (the world interaction was off) is not kept for later
	g_reticle.act = HoverFilter::None;
	const char * off = reticleOffReason();
	noteReticleOnOff(off);
	g_reticle.on = (off == nullptr);
	status.on = g_reticle.on;
	status.off = off ? off : "";

	const ReticleState::Camera & camera = g_reticle.camera;
	const float turn = camera.bodyYaw - camera.referenceYaw;

	for(size_t i = 0; i < HandCount; i++) {

		ReticleHandState & hand = g_reticle.hands[i];
		ReticleHandStatus & out = status.hands[i];
		const s32 before = hand.hover.hovered();
		hand.ray = false;
		hand.hit = false;
		hand.inFront = false;
		hand.claims = false;
		out = ReticleHandStatus();
		out.aimed = aims[i].valid;

		if(!g_reticle.on || !aims[i].valid) {
			hand.hover.update(HoverFilter::None, now);
			if(!g_reticle.on) {
				hand.hover.reset();
			}
			out.entity = reticleEntityLabel(hand.hover.hovered());
			continue;
		}

		// The aim ray in the world: rigid with the head camera, like the hands of the body (#36, F-185 -
		// the controller where it really is, not the posed hand)
		hand.ray = true;
		hand.localOrigin = aims[i].origin;
		hand.localDirection = aims[i].direction;
		hand.origin = camera.position + turnAboutVertical(toEngineAxes(aims[i].origin - camera.head) * UnitsPerMeter, turn);
		hand.direction = glm::normalize(turnAboutVertical(toEngineAxes(aims[i].direction), turn));

		// The first hit: the level (walls stop the ray), then the entities in front of that. Transparent
		// polygons stop it too: gratings and plank floors are drawn with them, and the player stands on them
		const Vec3f end = hand.origin + hand.direction * ReticleRayUnits;
		const RaycastResult scene = raycastScene(hand.origin, end, POLY_NOCOL | POLY_WATER);
		float distance = scene ? glm::distance(hand.origin, scene.pos) : ReticleRayUnits;
		float entityDistance = 0.f;
		Entity * entity = castReticleEntities(hand.origin, hand.direction, distance, entityDistance);
		if(entity) {
			distance = entityDistance;
		}
		hand.hit = scene || entity;
		hand.distance = distance;

		// The hover: an interactive entity first under the ray, within the engine's reach
		s32 under = HoverFilter::None;
		if(entity && isReticlePickable(*entity) && isInPickReach(*entity)) {
			under = entity->index().handleData();
		}
		const s32 hovered = hand.hover.update(under, now);
		hand.claims = (hovered != before && hovered != HoverFilter::None);
		if(hovered != before) {
			status.hoverChanges++;
			gameevent::Event("vr.reticle.hover", 500)("hand", ReticleHandNames[i])("item", reticleEntityLabel(hovered))
				("takeable", isReticleTakeable(hovered) ? "yes" : "no");
		}

		// The engine's mouse for the hit: projected with the head camera, in the world band
		const Vec3f point = hand.origin + hand.direction * distance;
		const glm::vec4 projected = camera.worldToScreen * glm::vec4(point, 1.f);
		if(hand.hit && projected.w > 0.f) {
			hand.inFront = true;
			hand.pixel = worldCursorPixel(Vec2f(projected.x / projected.w, projected.y / projected.w));
		}

		out.hit = hand.hit;
		out.under = reticleEntityLabel(under);
		out.entity = reticleEntityLabel(hovered);
		out.bright = isReticleTakeable(hovered);
		out.distance = distance;
		out.origin = hand.origin;
		out.direction = hand.direction;
		out.point = point;
		out.pixel = hand.pixel;
	}
}

bool reticlesOn() {
	return g_reticle.on;
}

void reticleTarget(Hand hand, PointerRay & ray) {
	const ReticleHandState & state = g_reticle.hands[size_t(hand)];
	if(g_reticle.on && state.hit && state.inFront) {
		ray.onWorld = true;
		ray.pixel = Vec2f(state.pixel);
	}
}

bool reticleClaims(Hand hand) {
	return g_reticle.on && g_reticle.hands[size_t(hand)].claims;
}

void afterPointer(const MenuPointer & pointer, const std::array<PointerRay, HandCount> & rays) {

	ReticleStatus & status = g_reticle.status;

	if(!g_reticle.on) {
		for(size_t i = 0; i < HandCount; i++) {
			setHandReticle(Hand(i), false);
			status.hands[i].shown = handReticleShown(Hand(i));
		}
		g_reticle.cursorOnWorld = false;
		g_reticle.cursorOnPanel = false;
		status.cursor = false;
		status.cursorEntity = "none";
		return;
	}

	// A press with the reticle in the world: what it pressed on stays what the press pixel stands for
	for(size_t i = 0; i < HandCount; i++) {
		ReticleHandState & hand = g_reticle.hands[i];
		if(pointer.pressedOnWorld(Hand(i))) {
			hand.pressEntity = hand.hover.hovered();
			g_reticle.history.press(Hand(i), hand.pixel, hand.pressEntity);
			status.presses++;
			gameevent::Event("vr.reticle.press", 500)("hand", ReticleHandNames[i])("item", reticleEntityLabel(hand.pressEntity))
				("distance", hand.distance, 0);
		}
	}

	g_reticle.owner = pointer.owner();
	g_reticle.cursorOnPanel = pointer.onPanel();
	g_reticle.cursorOnWorld = pointer.active() && !pointer.onPanel();
	status.cursor = g_reticle.cursorOnWorld;
	status.cursorEntity = "none";
	if(g_reticle.cursorOnWorld) {
		const ReticleHandState & owner = g_reticle.hands[size_t(g_reticle.owner)];
		// Inside the click dead zone the cursor holds the press pixel, which stands for the pressed entity
		const s32 entity = pointer.holding() ? owner.pressEntity : owner.hover.hovered();
		const Vec2f cursor = pointer.cursor();
		g_reticle.history.record(Vec2s(s16(std::lround(cursor.x)), s16(std::lround(cursor.y))), entity);
		status.cursorEntity = reticleEntityLabel(entity);
	}

	// The rings: where each ray hits the world, not for a ray on the panel (the panel has its own cursor)
	for(size_t i = 0; i < HandCount; i++) {
		const ReticleHandState & hand = g_reticle.hands[i];
		ReticleHandStatus & out = status.hands[i];
		out.onWorld = rays[i].onWorld;
		out.ring = hand.hit && !rays[i].onPanel;
		const Vec3f local = hand.localOrigin + hand.localDirection * (hand.distance / UnitsPerMeter);
		setHandReticle(Hand(i), out.ring, local, out.bright);
		out.shown = handReticleShown(Hand(i));
	}
}

bool reticleAct(Hand hand) {

	if(!g_reticle.on) {
		return false;
	}
	const ReticleHandState & state = g_reticle.hands[size_t(hand)];
	const s32 id = state.hover.hovered();
	Entity * entity = reticleEntityById(id);
	// What the engine's double click acts on (managePlayerControls): not an item - the same chord uses those
	if(!entity || (entity->ioflags & IO_ITEM) || !(entity->ioflags & (IO_FIX | IO_NPC))
	   || !isReticlePickable(*entity) || !isInPickReach(*entity)) {
		return false;
	}

	g_reticle.act = id;
	ReticleStatus & status = g_reticle.status;
	status.acts++;
	status.lastAct = reticleEntityLabel(id);
	status.lastActHand = ReticleHandNames[size_t(hand)];
	gameevent::Event("vr.reticle.act", 200)("hand", status.lastActHand)("item", status.lastAct)
		("distance", state.distance, 0);
	return true;
}

Entity * takeReticleAct() {
	Entity * entity = reticleEntityById(g_reticle.act);
	g_reticle.act = HoverFilter::None;
	return entity;
}

EnginePickOnly::EnginePickOnly() {
	g_reticle.enginePickOnly++;
}

EnginePickOnly::~EnginePickOnly() {
	g_reticle.enginePickOnly--;
}

ReticlePick reticlePickAt(const Vec2s & pos, Entity *& entity) {

	entity = nullptr;
	if(!g_reticle.on || g_reticle.enginePickOnly > 0) {
		return ReticlePick::NotMine;
	}

	if(!isWorldCursorPixel(pos)) {
		g_reticle.status.blocked++;
		return ReticlePick::Blocked;
	}

	g_reticle.status.picks++;
	if(std::optional<s32> id = g_reticle.history.find(pos)) {
		Entity * found = reticleEntityById(*id);
		// Still there to be picked: an item taken into the inventory a moment ago is not
		if(found && isReticlePickable(*found)) {
			entity = found;
		}
	}
	return ReticlePick::Found;
}

bool reticleDragRay(Vec3f & origin, Vec3f & direction) {
	if(!g_reticle.on) {
		return false;
	}
	const ReticleHandState & owner = g_reticle.hands[size_t(g_reticle.owner)];
	if(!owner.ray) {
		return false;
	}
	// The hand says where (its ring, or the end of its ray); the entity is placed along the line from the
	// head to there. The engine steps the entity along the ray from its start until it collides: from the
	// hand that would fail whenever the hand is in or behind a wall or a table edge, the head is always free
	origin = g_reticle.camera.position;
	const Vec3f target = owner.origin + owner.direction * owner.distance;
	const Vec3f toTarget = target - origin;
	if(glm::length(toTarget) < 1.f) {
		direction = owner.direction;
	} else {
		direction = glm::normalize(toTarget);
	}
	return true;
}

bool engineCursorHidden() {
	return g_reticle.on && !g_reticle.cursorOnPanel;
}

const ReticleStatus & reticleStatus() {
	return g_reticle.status;
}

bool aimAtWorldPoint(Hand hand, const Vec3f & point, float & yaw, float & pitch, std::string & error) {

	const ReticleHandState & state = g_reticle.hands[size_t(hand)];
	if(!g_reticle.on || !state.ray) {
		error = std::string("the ") + ReticleHandNames[size_t(hand)] + " hand has no ray in the world"
		        + (g_reticle.on ? " (no aim pose)" : " (reticles off: " + g_reticle.status.off + ")");
		return false;
	}
	const Vec3f world = point - state.origin;
	if(glm::length(world) < 1e-3f) {
		error = "the point is at the hand";
		return false;
	}

	// Back into LOCAL: the inverse of the turn and of the axes change of updateReticles
	const float turn = g_reticle.camera.bodyYaw - g_reticle.camera.referenceYaw;
	const Vec3f local = glm::normalize(toEngineAxes(turnAboutVertical(glm::normalize(world), -turn)));
	yaw = glm::degrees(std::atan2(-local.x, -local.z));
	pitch = glm::degrees(std::asin(std::clamp(local.y, -1.f, 1.f)));
	return true;
}

} // namespace vr
