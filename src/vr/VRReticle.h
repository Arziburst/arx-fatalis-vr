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

#ifndef ARX_VR_VRRETICLE_H
#define ARX_VR_VRRETICLE_H

#include <array>
#include <string>

#include <glm/glm.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/MenuPointer.h"
#include "vr/VRControls.h"
#include "vr/VRSystem.h"

class Entity;

/*!
 * \file
 * \brief The hand reticles in the world (#37, D-016): both hands work as the mouse
 *
 * Each hand has a hidden ray from its aim pose (no visible beam, D-010). In the game it is cast
 * against the level and the entities in 3D - the engine's own pick is a 2D test against screen boxes
 * that in VR are partly in the last eye's pixels (TRAPS.md) - and the entity it hits within the
 * engine's reach is that hand's hover, kept steady by the hysteresis of vr/HandReticle.h. A ring is
 * drawn where the ray hits, brighter over an item that can be taken (D-029).
 *
 * The menu pointer's arbiter (vr/MenuPointer.h) decides which hand is the mouse; its world pixel lies
 * in a band right of the 2D interface (vr/HandReticle.h), and the engine's pick asks this module what
 * such a pixel stands for (\ref reticlePickAt, from \c GetFirstInterAtPos). A dragged item goes where
 * the owning hand's ring is (\ref reticleDragRay, from \c updateDraggedEntity), not along a ray through the mouse.
 *
 * The order in a frame: the input module calls \ref updateReticles with the hands' aim rays, feeds
 * \ref reticleTarget into the arbiter, then \ref afterPointer with its decision - all before the
 * engine's input update, so the engine sees this frame's mouse.
 */
namespace vr {

/*!
 * The first-person head camera as the last update placed it - where the hands' rays start from. Called
 * by the engine every frame after the camera is prepared; \a firstPerson false for any other camera
 * (a cinematic, the death camera, an external view), which turns the reticles off
 */
void noteHeadCamera(bool firstPerson, const Vec3f & position, float bodyYaw, const glm::mat4 & worldToScreen);

//! Cast both hands' rays for this frame (\a aims from \c handAims()); \a now seconds on a steady clock
void updateReticles(const std::array<HandAim, HandCount> & aims, double now);

//! The reticles are on: in the game, first person, nothing that takes the mouse from the world
bool reticlesOn();

//! The hand's world target for the arbiter (\c onWorld and \c pixel; untouched when it has none)
void reticleTarget(Hand hand, PointerRay & ray);

//! The hand's reticle moved onto something new to hover this frame: it claims an idle cursor
bool reticleClaims(Hand hand);

/*!
 * The arbiter's decision for this frame: records what the engine's mouse stands for, logs presses,
 * and asks for the rings (none for a hand whose ray is on the panel)
 */
void afterPointer(const MenuPointer & pointer, const std::array<PointerRay, HandCount> & rays);

//! What the engine's pick should do with a mouse position
enum class ReticlePick {
	NotMine, //!< the reticles are off: the engine's own pick
	Found,   //!< a world reticle's pixel: \a entity is what its ray picked (may be null)
	Blocked  //!< a pixel of the 2D interface: it never picks the world while the reticles are on
};

ReticlePick reticlePickAt(const Vec2s & pos, Entity *& entity);

//! The ray a dragged entity is placed along, while the reticles are on: from the head camera to the owning hand's ring
bool reticleDragRay(Vec3f & origin, Vec3f & direction);

//! The engine's own cursor is not drawn: the mouse is a world reticle (the ring shows it), or no hand aims
bool engineCursorHidden();

//! One hand's reticle as status.json reports it
struct ReticleHandStatus {
	bool aimed = false;          //!< the hand has an aim pose
	bool hit = false;            //!< its ray hits something within ReticleRayUnits
	bool onWorld = false;        //!< a world target for the arbiter (hit, not on the panel, in front of the head)
	bool ring = false;           //!< a ring was asked for this frame
	bool shown = false;          //!< the ring was in the last submitted frame
	bool bright = false;         //!< over an item that can be taken
	std::string under = "none";  //!< the interactive entity in reach under the ray this frame
	std::string entity = "none"; //!< the hover after the hysteresis
	float distance = 0.f;        //!< along the ray to the hit, game units
	Vec3f origin = Vec3f(0.f);   //!< the ray in the world, game units
	Vec3f direction = Vec3f(0.f);
	Vec3f point = Vec3f(0.f);    //!< the hit in the world
	Vec2s pixel = Vec2s(0);      //!< the engine mouse for it (the band)
};

struct ReticleStatus {
	bool on = false;
	std::string off = "no session"; //!< why the reticles are off
	std::array<ReticleHandStatus, HandCount> hands;
	bool cursor = false;            //!< the engine's mouse is a world reticle
	std::string cursorEntity = "none";
	u64 presses = 0;                //!< trigger presses with a reticle in the world
	u64 hoverChanges = 0;
	u64 picks = 0;                  //!< the engine asked what a world pixel stands for
	u64 blocked = 0;                //!< the engine asked about an interface pixel and got nothing from the world
};

const ReticleStatus & reticleStatus();

/*!
 * \brief For the test channel: the aim, in the simulated headset's terms, that points \a hand at a
 *        point of the world
 *
 * \param yaw, pitch receive degrees as \c vr-sim \c hand \c rot takes them (yaw left, pitch up)
 * \return false with the reason in \a error when the hand has no ray this frame
 */
bool aimAtWorldPoint(Hand hand, const Vec3f & point, float & yaw, float & pitch, std::string & error);

} // namespace vr

#endif // ARX_VR_VRRETICLE_H
