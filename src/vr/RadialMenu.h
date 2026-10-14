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

#ifndef ARX_VR_RADIALMENU_H
#define ARX_VR_RADIALMENU_H

#include <array>
#include <string>
#include <string_view>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief The radial menu of a hand (#376, D-042): the ring, its half-ring and what the hand is over
 *
 * Holding B opens the menu (the right B by default, #377): a ring of 6 segments hangs in the air at the hand,
 * facing the eyes, fixed in the OpenXR LOCAL space - the room, which stick walking and turning do not move, so
 * the ring stays with the player and in reach. The hand selects by moving: its grip position projected onto the
 * ring's plane. Inside the hole in the middle nothing is selected; outside it, the segment in the hand's
 * direction. The right half holds the right hand's three quick slots - R1 on top, R2 upper right, R3 lower
 * right - the left half the left hand's: L1 upper left, L2 lower left, L3 at the bottom (the owner, #377: the
 * inventory segment is gone, the bag is on the left A). Moving the hand above the ring (past the top segment's
 * outer edge) opens the half-ring over it - game menu, the book's four pages, quick save, quick load,
 * recenter, cancel the running spell - left to right; it closes when the hand comes back into the ring or
 * drops below its middle.
 *
 * Pure logic - positions in, choices out; no engine, no OpenXR, no clock (tests/vr/RadialMenuTest).
 */
namespace vr {

//! The ring's segments, clockwise from the top: R1, R2, R3 on the right half, L3, L2, L1 on the left (#377)
constexpr size_t RadialSegmentCount = 6;
//! A hand's quick slots: its half of the ring (the owner, #377: 6 in all for now)
constexpr size_t QuickSlotCount = RadialSegmentCount / 2;
//! The half-ring's items, left to right
constexpr size_t HalfRingItemCount = 9;

//! What a menu item does
enum class RadialItem : u8 {
	None,
	QuickSlot,
	GameMenu,
	Character,
	Spells,
	Map,
	Quests,
	QuickSave,
	QuickLoad,
	Recenter,
	CancelSpell
};

//! The item a hand is over: the item and, for a quick slot, which one of which hand (0 = R1 / L1)
struct RadialChoice {

	RadialItem item = RadialItem::None;
	u8 slot = 0;
	bool left = false; //!< a quick slot of the left hand (its half of the ring)

	bool operator==(const RadialChoice & other) const {
		return item == other.item && slot == other.slot && left == other.left;
	}
	bool operator!=(const RadialChoice & other) const { return !(*this == other); }

};

//! The text form for status.json and the log: "none", "r1" … "r3", "l1" … "l3", "game_menu", "quick_save", …
std::string radialChoiceName(const RadialChoice & choice);

//! The name players read in the middle of the ring, e.g. "Quick save"; empty for a quick slot and none
const char * radialItemLabel(RadialItem item);

//! The choice of a ring segment (0 = top, clockwise): R1, R2, R3, L3, L2, L1
RadialChoice segmentChoice(size_t segment);

//! The half-ring's item at \a index (0 = left)
RadialItem halfRingItem(size_t index);

//! Where a segment's middle points: degrees counterclockwise from the ring's right (90 = up)
float segmentCenterDegrees(size_t segment);

//! Where a half-ring item's middle points, in the same terms
float halfRingCenterDegrees(size_t index);

//! The half-ring's items each span this many degrees
constexpr float HalfRingItemDegrees = 180.f / float(HalfRingItemCount);

//! The ring's size and its rules, meters in the ring's plane (D-045: proposals, judged in the headset)
struct RadialParams {
	float holeRadius = 0.04f;        //!< the middle selects nothing
	float ringRadius = 0.12f;        //!< the segments' outer edge
	float halfInnerRadius = 0.145f;  //!< the half-ring as drawn
	float halfOuterRadius = 0.215f;
	float halfOpenHeight = 0.125f;   //!< the hand above the ring's top edge opens the half-ring
	float halfCloseRadius = 0.10f;   //!< back inside the ring closes it
	float halfCloseHeight = -0.02f;  //!< and so does the hand below the ring's middle
	float marginDegrees = 6.f;       //!< a segment stays selected this far past its edge
	float push = 0.04f;              //!< the ring's middle sits this far beyond the hand, away from the eyes
	float imageExtent = 0.46f;       //!< the square the ring is drawn in, centered on its middle
};

//! A ring in the LOCAL space: its face (+z) towards the eyes, its up (+y) as close to the room's up as it can
struct RadialPose {
	Vec3f center = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

//! The ring opened by a hand at \a hand seen from \a eye (both LOCAL, meters)
RadialPose placeRadial(const Vec3f & hand, const Vec3f & eye, const RadialParams & params = RadialParams());

//! A point of the LOCAL space in the ring's plane: meters from its middle, x right, y up
Vec2f radialPlanePoint(const RadialPose & pose, const Vec3f & point);

//! What a hand is over while its menu is open
class RadialSelector {

public:

	//! A menu opened: the hand at the middle, the half-ring closed
	void reset();

	//! \param point the hand in the ring's plane (\ref radialPlanePoint)
	const RadialChoice & update(const Vec2f & point, const RadialParams & params = RadialParams());

	const RadialChoice & hovered() const { return m_hovered; }
	bool halfRingOpen() const { return m_halfOpen; }

private:

	RadialChoice m_hovered;
	int m_segment = -1;   //!< the ring segment selected, -1 for none
	int m_halfItem = -1;  //!< the half-ring item selected, -1 for none
	bool m_halfOpen = false;

};

//! A hand's quick slots: the item class each holds (e.g. "potion_life"), empty for an empty slot
typedef std::array<std::string, QuickSlotCount> QuickSlotSet;

//! "potion_life|torch|" - how a set is kept in the save (a global script variable)
std::string formatQuickSlots(const QuickSlotSet & slots);

/*!
 * The set \ref formatQuickSlots wrote; a slot that is not a plain class name (lowercase letters, digits,
 * '_') is left empty, so a broken value never names an item. Entries past the third are dropped: a save of
 * the first layout kept 7 per hand (#376), of which the first 3 carry over (#377)
 */
QuickSlotSet parseQuickSlots(std::string_view text);

} // namespace vr

#endif // ARX_VR_RADIALMENU_H
