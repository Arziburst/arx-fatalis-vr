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

#ifndef ARX_VR_VRRADIAL_H
#define ARX_VR_VRRADIAL_H

#include <array>
#include <bitset>
#include <string>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/RadialMenu.h"
#include "vr/VRControls.h"
#include "vr/VRGameInput.h"

/*!
 * \brief The radial menu in the game (#376, D-042): on the right B by default (#377, the owner; the left
 *        hand's radial event is unbound, a player may bind it)
 *
 * While a hand's radial event is held its ring hangs at the hand (vr/RadialMenu.h) and follows nothing but the
 * room; releasing B runs the item under the hand: the game menu, the book's pages, quick save and load,
 * recenter, cancel the running spell, or a quick slot. The ring's right half holds the right hand's three
 * slots, its left half the left hand's: a slot acts for its own side's hand, whichever hand holds the menu (a
 * torch lit from an L slot is recorded as the left hand's; equipment is equipped by the engine - a weapon
 * still goes to the right hand until #380). An item dragged from the inventory and released onto a quick slot
 * fills it with its kind (the next potion of the kind refills it). Each hand's slots are kept in the save as the
 * global script text variables \c $vr_quickslots_left / \c $vr_quickslots_right, so no save format changes (a
 * save of the first layout's seven per hand keeps its first three).
 * The game time runs while a menu is open.
 * Released over nothing while the book, the bag, a container or a note is open, B closes it (#377, the owner).
 * The game menu is never out of reach (#377): a press that cannot open a menu - a menu shown (the game menu, the
 * character creation, the credits), in the game the head or the hand not tracked - is the game menu key (Escape):
 * it opens the game menu, or goes back.
 *
 * The menus are drawn by the engine's 2D drawing into an image per hand, shown by the VR module as a
 * quad layer on top of everything (\ref beginRadialImage, \ref setRadialLayer).
 */
namespace vr {

//! What the menus ask of the engine in this update: actions pressed for one update, the game menu
struct RadialRequests {
	std::bitset<GameActionCount> pulses;
	bool gameMenu = false; //!< the game menu key (Escape), as the menu's left A gives it
	/*!
	 * A dragged item went into a quick slot and back into the bag: the mouse button is up for the engine
	 * until every trigger is let go, so the held trigger does not pick the item up again
	 */
	bool releaseClick = false;
};

/*!
 * Open, follow and run the menus - once per frame, after the control table resolved this frame's events
 * and before the engine's input update.
 * \param blocked the controls do nothing else now (the VR settings capture a binding): menus close unrun
 */
void updateRadialMenus(const ControlResolver & controls, bool blocked, RadialRequests & requests);

//! Draw the open menus into their images for this frame (stereo frames only; after \ref updateRadialMenus)
void drawRadialMenus();

/*!
 * Close what is open in the game (#377): the book, a note, a container and the bag - the bag through its key in
 * \a requests, as the menu's inventory item. Not the game menu. B released over nothing does it while one of them
 * is open (the owner's amendment to D-042), and the casting stance's press (A) before it starts (no stance while an
 * item is dragged, VRInput). Nothing is closed
 * while an item is dragged (a \c vr.panels.kept event): it would stay in the hand with its bag gone.
 * \param direct close the bag now instead of through its key: the casting stance starts in the same update, and the
 *        engine leaves the bag alone in its magic mode
 * \return what was closed ("book inventory" …), empty when nothing was open
 */
std::string closeOpenPanels(RadialRequests & requests, bool direct = false);

//! Close the open radial menus without running their items (the casting stance takes the hands, #377)
void closeRadialMenus(const char * why);

//! A hand's quick slots as saved in the running game
QuickSlotSet quickSlots(Hand hand);

//! One hand's menu as status.json reports it
struct RadialHandStatus {
	bool open = false;
	bool halfRing = false;
	std::string hovered = "none";
	std::string chosen = "none";   //!< the item run by the last release (also "none")
	std::string result = "none";   //!< what the last release did, e.g. "book_open", "slot_filled", "nothing_left"
	std::string refused;           //!< why the last press did not open the menu, empty when it did
	u64 opens = 0;
	u64 choices = 0;               //!< releases that ran an item
	u64 gameMenuKeys = 0;          //!< presses that could not open the menu and were the game menu key (#377)
	std::string placed = "none";   //!< where the last menu opened: "hand", or "gaze" before the eyes (vr_radial_place)
	bool undrawnLogged = false;    //!< the open menu's image could not be drawn once (vr.radial.undrawn logged, #377)
	Vec2f point = Vec2f(0.f);      //!< the hand in the ring's plane, meters
	Vec3f center = Vec3f(0.f);     //!< the ring in LOCAL, meters
	bool shown = false;            //!< the menu was in the last submitted frame
};

struct RadialStatus {
	std::array<RadialHandStatus, HandCount> hands;
	std::string torchHand = "none"; //!< the hand whose menu lit the torch that burns now
	u64 drawn = 0;                  //!< menu images drawn since the start
};

const RadialStatus & radialStatus();

/*!
 * The test channel's \c vr-radial command: \c point \c <hand> \c <item> moves the simulated hand to the
 * middle of that item of its open menu (\c r1 … \c r3, \c l1 … \c l3, \c game_menu …, \c center),
 * through the half-ring when the item is in it.
 * \return an empty string, or why it was refused
 */
std::string radialCommand(std::string_view arguments);

} // namespace vr

#endif // ARX_VR_VRRADIAL_H
