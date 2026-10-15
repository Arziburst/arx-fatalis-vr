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

#ifndef ARX_VR_WRISTHUD_H
#define ARX_VR_WRISTHUD_H

#include "math/Types.h"
#include "vr/WristVitals.h"

/*!
 * \brief The wrist bars in the game (#116, D-029, D-037): health and mana drawn in the world on the
 *        inner side of the left wrist of the full body (#36)
 *
 * The bars are shown while the left arm is posed from a controller (a located one or the test
 * channel's \c handpose); then the health and mana orbs of the HUD step aside. Without a posed left
 * arm - flat mode, no left controller, a model without arm chains - the orbs stay where they were
 * (on the head-locked quad in VR). The rest of the screen-frame HUD is not drawn in VR (#469, vr/VRHud.h);
 * the level-up notice shows beside the bars.
 */
namespace vr {

//! Once per frame, after the player was animated and before the scene views: is the left arm posed?
void updateWristVitals();

//! \return true while the bars are on the wrist this frame: the HUD's orbs are not drawn
bool wristVitalsShown();

//! Draw the bars into the current scene view (each eye in VR), after the opaque scene
void drawWristVitals();

//! The wrist bars as status.json reports them
struct WristVitalsStatus {
	bool shown = false;
	const char * hidden = "";  //!< why not, when not
	float health = 0.f;        //!< the fractions drawn
	float mana = 0.f;
	float facingDeg = 180.f;   //!< of the last view drawn
	float alpha = 0.f;
	u64 drawnViews = 0;        //!< views the bars were drawn into with alpha > 0
	Vec3f center = Vec3f(0.f);
	Vec3f normal = Vec3f(0.f);   //!< out of the inner wrist
	Vec3f across = Vec3f(0.f);   //!< the direction the bars fill (towards the thumb)
	Vec3f along = Vec3f(0.f);    //!< elbow -> hand
};

const WristVitalsStatus & wristVitalsStatus();

} // namespace vr

#endif // ARX_VR_WRISTHUD_H
