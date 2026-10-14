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

#ifndef ARX_VR_HUDFRAME_H
#define ARX_VR_HUDFRAME_H

#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The original game's screen-frame HUD in VR (#469): which of its elements are drawn, and where
 *        each one's function lives instead
 *
 * The flat game spreads its HUD along the edges of the monitor: the health and mana orbs, the
 * level-up, book, bag and purse icons, the active and precast spells, the stealth gauge and the
 * rest. In VR that frame is the 2D interface's rectangle - on the head-locked quad in play, on the
 * floating panel around the book or the bag - and the owner removed it (2026-10-14): every element
 * gets a VR home (the wrist, the radial menu, the panel it belongs to) or is listed as an open
 * question, and none of them is drawn on the 2D interface in VR. Flat mode keeps the frame.
 *
 * Pure logic: no engine globals, no clock - covered by a host unit test (tests/vr/HudFrameTest.cpp).
 * vr/VRHud.cpp feeds it the game's state and counts what is drawn.
 */
namespace vr {

//! The elements of the screen-frame HUD (gui/Hud.cpp's HudRoot); the bag, the book and the containers are panels
enum class HudElement : u8 {
	Health,           //!< the red orb
	Mana,             //!< the blue orb
	LevelUp,          //!< the level-up icon: points to spend, a click opens the character page
	Book,             //!< the book icon
	Backpack,         //!< the bag icon
	Purse,            //!< the gold purse: a click takes gold out
	ActiveSpells,     //!< the running spells' icons: a click cancels one
	PrecastSpells,    //!< the precast spells' icons: a click launches one
	Stealth,          //!< the stealth gauge while sneaking unseen
	Torch,            //!< the burning torch's icon: a click puts it out
	Steal,            //!< the steal icon while a container can be stolen from
	DamagedEquipment, //!< the damaged equipment figure
	ChangeLevel,      //!< the zone-change icon at a level's exit: a click goes
	QuickSave,        //!< the quick save's flash
	MemorizedRunes,   //!< the runes of the spell being drawn
	HitStrength,      //!< the combat mode's swing charge
	MechanismHint,    //!< the cog that hints at the cursor mode
	ScreenArrows,     //!< the arrows on the screen edges in the cursor mode
	MiniMap,          //!< the minimap in the top right corner (gui/MiniMap.cpp)
	Count
};

constexpr size_t HudElementCount = size_t(HudElement::Count);

//! Where an element's function lives in VR
enum class HudHome : u8 {
	Wrist,    //!< on the left wrist (#116)
	Radial,   //!< in the radial menus (#376, D-042)
	None,     //!< nothing to carry over: it served the mouse and the monitor only
	Question, //!< no obvious home yet: hidden, listed for the owner (vr_hud_keep brings it back)
};

//! \return "wrist", "radial", "none" or "question"
const char * hudHomeName(HudHome home);

struct HudElementInfo {
	const char * name;  //!< for cfg.ini (vr_hud_keep), status.json and the log
	HudHome home;
	const char * where; //!< the VR home in words, or the open question
};

const HudElementInfo & hudElementInfo(HudElement element);

//! \return the element of that name (\ref HudElementInfo::name), \c Count for none
HudElement hudElementFromName(std::string_view name);

//! A set of elements as bits (1 << element)
using HudElementMask = u32;

constexpr HudElementMask hudElementBit(HudElement element) {
	return HudElementMask(1) << u32(element);
}

/*!
 * \brief Parse \c vr_hud_keep: element names separated by commas or spaces ("precast_spells, stealth"),
 *        "all" for every element, empty for none
 * \param unknown receives the first unknown name (empty when all are known); unknown names are skipped
 */
HudElementMask parseHudKeep(std::string_view text, std::string * unknown = nullptr);

//! What decides whether an element is drawn this frame
struct HudGate {
	bool vr = false;            //!< a VR session is on: the frame is the 2D interface's rectangle there
	bool frame = false;         //!< cfg.ini vr_hud_frame: the old frame back in VR (the comparison switch)
	HudElementMask keep = 0;    //!< cfg.ini vr_hud_keep: these elements stay on the 2D interface in VR
	bool wristVitals = false;   //!< health and mana are on the wrist this frame (#116)
};

/*!
 * \return true when the element is drawn on the 2D interface (and takes its clicks)
 *
 * Flat: everything, as before. VR: nothing, unless \c frame or \c keep say otherwise - except the
 * orbs while the wrist bars cannot be shown (no left controller): health and mana have no other
 * home then. The orbs step aside for the wrist bars in either mode, as since #116.
 */
bool hudElementDrawn(HudElement element, const HudGate & gate);

//! The level-up indication in VR (#469): cfg.ini vr_levelup
enum class LevelUpMode : u8 {
	Both,   //!< the mark on the radial menu's Character item and the wrist notice
	Radial, //!< the mark only
	Wrist,  //!< the notice only
	Off,
};

//! \return "both", "radial", "wrist" or "off"
const char * levelUpModeName(LevelUpMode mode);

//! \return the mode of that name, \a fallback for an unknown one
LevelUpMode levelUpModeFromName(std::string_view name, LevelUpMode fallback = LevelUpMode::Both);

//! How long the wrist notice shows after a level-up (microseconds)
constexpr s64 LevelUpNoticeUs = 8000000;

/*!
 * \brief The level-up indication over time (#469): the radial mark while there are points to spend,
 *        the wrist notice for a while after a level-up
 *
 * One \ref update per frame. "Points to spend" is the flat level-up icon's own condition. A notice
 * starts when the points appear, or when the level rises again while they are still there; the
 * first update only takes the state in (a loaded save with points to spend has the mark, no notice).
 * The notice ends after \c noticeUs or as soon as the points are spent.
 */
class LevelUpIndicator {

public:

	/*!
	 * \param pending points to spend (attributes or skills)
	 * \param level   the player's level
	 * \param nowUs   a monotonic time in microseconds
	 * \return true when a notice started in this update
	 */
	bool update(bool pending, s32 level, s64 nowUs, LevelUpMode mode, s64 noticeUs = LevelUpNoticeUs);

	bool pending() const { return m_pending; }

	//! The radial menu's Character item is marked
	bool radialMark() const { return m_pending && (m_mode == LevelUpMode::Both || m_mode == LevelUpMode::Radial); }

	//! The wrist notice shows now
	bool wristNotice() const { return m_notice && (m_mode == LevelUpMode::Both || m_mode == LevelUpMode::Wrist); }

	//! \return [0, 1] through the notice: 0 when it starts, 1 when it ends
	float noticeProgress() const { return m_progress; }

	//! \return how many notices started
	u64 notices() const { return m_notices; }

	LevelUpMode mode() const { return m_mode; }

private:

	bool m_started = false;
	bool m_pending = false;
	s32 m_level = 0;
	bool m_notice = false;
	s64 m_noticeStart = 0;
	float m_progress = 0.f;
	u64 m_notices = 0;
	LevelUpMode m_mode = LevelUpMode::Both;

};

} // namespace vr

#endif // ARX_VR_HUDFRAME_H
