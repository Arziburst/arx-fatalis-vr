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

#ifndef ARX_GUI_DAMAGENUMBERS_H
#define ARX_GUI_DAMAGENUMBERS_H

#include <stddef.h>

class Entity;

/*!
 * \brief Damage numbers in the test playground (#321): every hit on an NPC shows its damage over the target
 *
 * Only in the playground (\ref isPlaygroundArea); the original levels show nothing new. A number appears
 * above the target's head, rises and fades over about a second. It is drawn into every scene view - each
 * eye in VR - at the point above the target projected by that view, so the two eyes see it at the
 * target's depth; it keeps the same angular size at any distance and is not hidden by the target or the
 * scene. Damage that comes every frame (burning, a damage field) adds up into one number per target for
 * a short window. Each number is one \c damage.number event when its window closes.
 */
namespace damagenumbers {

/*!
 * A hit on an NPC took effect (damageNpc): show it in the playground
 * \param damage the hit's damage
 * \param dealt  the life it took (less than \a damage when it killed)
 */
void hit(const Entity & target, float damage, float dealt);

//! Once per frame, before the scene views: age the numbers, follow their targets, log the closed ones
void update();

//! Draw the numbers into the current scene view (each eye in VR), over the scene
void render();

//! The numbers shown now (status.json \c damageNumbers.visible)
size_t visible();

//! Forget all numbers (a level is left)
void clear();

//! Release the font - before the font cache shuts down
void releaseFont();

} // namespace damagenumbers

#endif // ARX_GUI_DAMAGENUMBERS_H
