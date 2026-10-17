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

#ifndef ARX_VR_VRHUD_H
#define ARX_VR_VRHUD_H

#include <array>
#include <string>
#include <string_view>

#include "platform/Platform.h"
#include "vr/HudFrame.h"

/*!
 * \file
 * \brief The screen-frame HUD in the game (#469): gui/Hud.cpp asks here which of its elements it draws
 *
 * In VR none of them is drawn on the 2D interface (vr/HudFrame.h): their functions live on the wrist
 * and in the radial menus. cfg.ini \c vr_hud_frame brings the whole old frame back (the comparison
 * switch), \c vr_hud_keep single elements; \c vr_levelup picks the level-up indication. Flat mode
 * draws the frame as before.
 */
namespace vr {

//! Once per frame, before the HUD is drawn: the gate's inputs and the level-up indication
void updateHud();

//! \return true when the element is drawn on the 2D interface this frame; counted (status.json vr.hud.drawn)
bool hudDraw(HudElement element);

//! \return the same as \ref hudDraw without counting - for the element's clicks
bool hudShown(HudElement element);

//! The level-up indication (the radial mark, the wrist notice)
const LevelUpIndicator & levelUpIndicator();

//! Count a view the wrist notice was drawn into / a radial image the mark was drawn on
void noteLevelUpWristDrawn();
void noteLevelUpRadialDrawn();

//! The HUD in VR as status.json reports it
struct HudStatus {
	bool vr = false;        //!< the gate saw a VR session in the last update
	bool frame = false;     //!< vr_hud_frame
	std::string keep;       //!< vr_hud_keep as read
	u64 frames = 0;         //!< HUD updates (frames the HUD was drawn in)
	std::array<u64, HudElementCount> drawn = { }; //!< per element: frames it was let through to draw
	u64 levelUpWristViews = 0;  //!< views the wrist notice was drawn into
	u64 levelUpRadialImages = 0; //!< radial images the Character item's mark was drawn on
};

const HudStatus & hudStatus();

//! \return the sum of \ref HudStatus::drawn over every element
u64 hudDrawnTotal();

/*!
 * \brief The test channel's \c vr-hud command:
 *        <tt>frame on|off</tt>, <tt>keep &lt;names&gt;|none</tt>, <tt>levelup both|radial|wrist|off</tt>,
 *        <tt>reset</tt> (the draw counters to zero)
 * \return an error, empty when done
 */
std::string hudCommand(std::string_view arguments);

} // namespace vr

#endif // ARX_VR_VRHUD_H
