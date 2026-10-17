/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
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
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
// Code: Cyril Meynier
//
// Copyright (c) 1999-2001 ARKANE Studios SA. All rights reserved

#ifndef ARX_GAME_EQUIPMENT_H
#define ARX_GAME_EQUIPMENT_H

#include <stddef.h>
#include <string_view>
#include <vector>

#include "game/Item.h"
#include "math/Types.h"
#include "util/Flags.h"

class Entity;

enum WeaponType {
	WEAPON_BARE = 0,
	WEAPON_DAGGER = 1,
	WEAPON_1H = 2,
	WEAPON_2H = 3,
	WEAPON_BOW = 4
};

enum EquipmentSlot {
	EQUIP_SLOT_RING_LEFT = 0,
	EQUIP_SLOT_RING_RIGHT = 1,
	EQUIP_SLOT_WEAPON = 2,
	EQUIP_SLOT_SHIELD = 3,
	EQUIP_SLOT_TORCH = 4,
	EQUIP_SLOT_ARMOR = 5,
	EQUIP_SLOT_HELMET = 6,
	EQUIP_SLOT_LEGGINGS = 7
};

void ARX_EQUIPMENT_Init();
void ARX_EQUIPMENT_Remove_All_Special(Entity * io);
void ARX_EQUIPMENT_SetEquip(Entity * io, bool special,
                            std::string_view modifierName, float val,
                            EquipmentModifierFlags flags);

//! Sets/unsets an object type flag
bool ARX_EQUIPMENT_SetObjectType(Entity & io, std::string_view temp, bool set);

ItemType ARX_EQUIPMENT_GetObjectTypeFlag(std::string_view temp);
void ARX_EQUIPMENT_Equip(Entity * target, Entity * toequip);
void ARX_EQUIPMENT_UnEquip(Entity * target, Entity * tounequip, long flags = 0);
void ARX_EQUIPMENT_ReleaseAll(Entity * io);

void ARX_EQUIPMENT_AttachPlayerWeaponToHand();
void ARX_EQUIPMENT_AttachPlayerWeaponToBack();
void ARX_EQUIPMENT_LaunchPlayerReadyWeapon();
 
void ARX_EQUIPMENT_LaunchPlayerUnReadyWeapon();
WeaponType ARX_EQUIPMENT_GetPlayerWeaponType();

/*!
 * Get a base modifier (of a specific type) for all items equipped by the player.
 *
 * \param modifier The modifier type to calculate the value for.
 * \param relative \c true to sum up relative modifiers,
 *                 \c false to sum up absolute modifiers.
 *
 * \return an absolute modifier value to add to the base stat (for absolute modifiers),
 *         or a relative modifier value to be moltiplied by the base and then added to
 *         the base (for relative modifiers).
 */
float getEquipmentBaseModifier(EquipmentModifierType modifier, bool relative = false);

/*!
 * Get the total modifier (of a specific type) for all items equipped by the player.
 *
 * This includes both absolute and relative modifiers.
 *
 * \param modifier The modifier type to calculate the value for.
 * \param baseval  The base value to apply to relative modifiers.
 *
 * \return an absolute modifier value to add to the base stat.
 */
float getEquipmentModifier(EquipmentModifierType modifier, float baseval);

//! A target a strike check touched (VR melee, #39)
struct StrikeContact {
	EntityHandle target;
	float damage = 0.f; //!< 0 for a miss of the armour class roll, and for objects
	bool npc = false;
};

//! What a VR contact hit asks of a strike check (#39): targets to leave out (the per-target cooldown), and
//! the targets it touched
struct StrikeTargets {
	std::vector<EntityHandle> skip;
	/*!
	 * VR (#465): a touch outside a swing - it reaches only NPCs that fight the player (a resting blade does nothing to
	 * an item, a crate, a friend or a merchant) and never the world's geometry (no wall sound, no durability)
	 */
	bool touch = false;
	std::vector<EntityHandle> refused; //!< a touch: the NPCs it reached that do not fight the player
	// VR (#465): the stroke the aim ratio was measured from, for the melee.hit line
	float speed = 0.f;           //!< m/s, the lever point's
	float size = 0.f;            //!< m, the hand's path (a thrust: its lunge)
	const char * kind = nullptr; //!< "slash", "thrust"; nullptr: not a VR contact
	std::vector<StrikeContact> touched;
};

/*!
 * \return true for an NPC fighting the player: in its fight behaviour with the player as its target - what a VR touch
 *         may reach (#465; the target alone is not enough: a merchant or a friend looks at the player too)
 */
bool fightsPlayer(const Entity & target);

bool ARX_EQUIPMENT_Strike_Check(Entity * io_source, Entity * io_weapon, float ratioaim, long flags,
                                EntityHandle targ = EntityHandle(), StrikeTargets * targets = nullptr);
void ARX_EQUIPMENT_RecreatePlayerMesh();
Entity * getWeapon(Entity & entity) noexcept;
std::string_view getWeaponMaterial(Entity & entity) noexcept;
DamageType getDamageTypeFromWeaponMaterial(std::string_view material) noexcept;
float ARX_EQUIPMENT_ComputeDamages(Entity * io_source, Entity * io_target, float ratioaim, Vec3f * pos = nullptr);
 
void ARX_EQUIPMENT_IdentifyAll();

void ARX_EQUIPMENT_UnEquipAllPlayer();
float GetHitValue(std::string_view name);
void ARX_EQUIPMENT_UnEquipPlayerWeapon();
bool isEquippedByPlayer(const Entity * item);

#endif // ARX_GAME_EQUIPMENT_H
