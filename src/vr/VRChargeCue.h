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

#ifndef ARX_VR_VRCHARGECUE_H
#define ARX_VR_VRCHARGECUE_H

#include "platform/Platform.h"

/*!
 * \brief The melee charge shown in VR (#322): the variants of vr/MeleeChargeCue.h in the game
 *
 * While physical melee is active (vr/VRMelee.h: a weapon in the IK hand, no menu) the cues the setting
 * (cfg.ini \c vr_melee_charge_cue, the channel's \c melee-cue) selects show the charge of the weapon hand
 * (\ref meleeHand): the weapon brightens (\c glow), a ring at that wrist fills (\c wrist), the HUD's hit
 * strength gauge is drawn on the interface quad (\c hud, gui/Hud.cpp), a strip along the blade fills
 * (\c blade). Each follows the engine's charge (\c player.m_aimTime against \c Full_AimTime) and is empty
 * again when the strike spends it or the trigger is released.
 */
namespace vr {

//! Once per frame after \ref updateMelee and before the scene is drawn: the weapon's glow and the cues' state
void updateChargeCue();

//! Draw the wrist ring and the blade strip into the current scene view (each eye in VR), after the opaque scene
void drawChargeCue();

//! \return true while the HUD's hit strength gauge is drawn for the melee charge (the \c hud cue)
bool chargeCueHudShown();

//! The cues as status.json reports them
struct ChargeCueStatus {
	const char * mode = "off";   //!< the setting (\ref chargeCueName)
	const char * hand = "right"; //!< the weapon hand the cues follow
	bool active = false;         //!< physical melee is active: the cues show the charge
	float fill = 0.f;            //!< the charge the cues show (chargeFill), 0 .. 1
	// Per variant: shown (selected and active) and what it shows now
	bool glowShown = false;
	float glow = 0.f;            //!< the fill the weapon's glow was set to this frame
	bool wristShown = false;
	float wristFill = 0.f;       //!< the ring's filled arc / 360 degrees in the last view drawn
	u64 wristViews = 0;          //!< views the ring was drawn into
	bool hudShown = false;
	bool bladeShown = false;
	float bladeFill = 0.f;       //!< the strip's length / the blade's in the last view drawn
	u64 bladeViews = 0;          //!< views a strip was drawn into (an empty charge draws none)
	u64 changes = 0;             //!< setting changes seen
};

const ChargeCueStatus & chargeCueStatus();

} // namespace vr

#endif // ARX_VR_VRCHARGECUE_H
