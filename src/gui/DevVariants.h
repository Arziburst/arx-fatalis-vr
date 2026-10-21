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

#ifndef ARX_GUI_DEVVARIANTS_H
#define ARX_GUI_DEVVARIANTS_H

#include <stddef.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "math/Types.h"

/*!
 * \brief The variants of the PRs under the owner's headset test (#588), switched live on the spellbook's map page
 *
 * A PR that lets the owner choose between variants (a font, a threshold, an offset) registers each set in one line
 * - a group id, its PR label and either named choices or a number with a range and a step - with a setter that
 * applies a choice at once. In a development launch (--dev-launch) the book's map page shows a panel of every
 * registered group instead of the map (gui/book/DevPanel.h); a player's launch never shows it. After the owner's
 * choice the PR keeps the winner as the code's default and deletes the other variants and their registration
 * (docs/vr/TESTING.md), so only the PRs under test have groups. Nothing is saved: a launch starts from the code.
 *
 * Pure logic - no renderer, no game state; the caller says whether the launch is a development launch.
 * Host tests: tests/gui/DevVariantsTest.
 *
 * A new kind of entry (an action button such as the playground's teleports, #481) is a new Kind with its own
 * register function, a ButtonKind for its button(s) and its own branch in placeGroup() of the layout; the
 * registry, the pages and the panel's drawing and clicks (by ButtonKind) need no other change.
 */
namespace devvariants {

enum class Kind {
	Choice, //!< Named choices, one current
	Number, //!< A number in [min, max] changed by - / + in steps
};

struct Entry {
	std::string group;                //!< The group's id: not empty, no white space; the panel's label
	std::string prLabel;              //!< The PR / issue the group belongs to, e.g. "#588"
	Kind kind = Kind::Choice;
	std::vector<std::string> choices; //!< Choice: the named choices, in order
	size_t current = 0;               //!< Choice: the index of the current choice
	float min = 0.f;                  //!< Number: the range and the step of - / +
	float max = 0.f;
	float step = 0.f;
	std::string unit;                 //!< Number: shown after the value ("m", "%"; empty: none)
	float value = 0.f;                //!< Number: the current value, always within [min, max]
	bool afterRestart = false;        //!< The change takes effect after a restart: the panel says so
};

/*!
 * Registers a set of named choices. Registering calls no setter (the code's default is in effect already); a
 * group id that exists is replaced in its place.
 * \return false (nothing registered) for an empty id or one with white space, no choices or \a defaultIndex out
 *         of range
 */
bool registerChoice(std::string_view group, std::string_view prLabel, std::vector<std::string> choices,
                    size_t defaultIndex, std::function<void(size_t)> setter, bool afterRestart = false);

/*!
 * Registers a number changed by - / + in \a step within [\a min, \a max]; \a defaultValue outside is clamped.
 * \return false (nothing registered) for a bad id, \a min >= \a max or \a step <= 0
 */
bool registerNumber(std::string_view group, std::string_view prLabel, float min, float max, float step,
                    std::string_view unit, float defaultValue, std::function<void(float)> setter,
                    bool afterRestart = false);

//! \return false for an unknown group
bool unregister(std::string_view group);

//! Every group gone
void clear();

//! The groups in registration order
std::vector<Entry> entries();

//! The number of registered groups
size_t count();

//! nullptr for an unknown group; valid until the next registration change
const Entry * find(std::string_view group);

/*
 * Switching. A call that changes the value calls the setter once before it returns, logs one line
 * "[dev] variant <group> = <valueText>" and returns true. A call that leaves the value as it is or is invalid
 * (unknown group, wrong kind, index out of range, unknown label, not a number) does nothing and returns false.
 */

//! A choice by its index
bool choose(std::string_view group, size_t index);

//! A number's - (\a direction < 0) or + (> 0): one step on the grid min + k * step, clamped to the range
bool step(std::string_view group, int direction);

//! A choice by its exact label, a number as a decimal number with '.' (clamped) - the test channel's set
bool set(std::string_view group, std::string_view value);

/*!
 * The value as shown and logged: a choice's label; a number rounded to 3 decimals without trailing zeros, then
 * " <unit>" when it has one. Empty for an unknown group.
 */
std::string valueText(std::string_view group);

//! What the book's map page shows
enum class MapPageView {
	Map,   //!< The normal map
	Panel, //!< The variants panel
	Empty, //!< "No variants under test" and a Show map button
};

/*!
 * The map in every launch that is not a development launch; in a development launch the panel when a group is
 * registered, else Empty until Show map was pressed (\a showMap, reset when the book closes), then the map
 */
MapPageView mapPageView(bool devLaunch, size_t groups, bool showMap);

enum class ButtonKind { Choice, Minus, Plus, PrevPage, NextPage, ShowMap };

struct Button {
	ButtonKind kind = ButtonKind::Choice;
	std::string group;  //!< Choice / Minus / Plus: the group; empty otherwise
	size_t index = 0;   //!< Choice: the choice's index
	std::string label;  //!< The text on the button
	/*!
	 * The test channel's name (vr-point <hand> menu <target>, ui.menuHover): devpanel.<group>.<index>,
	 * devpanel.<group>.minus, devpanel.<group>.plus, devpanel.prev, devpanel.next, devpanel.show_map
	 */
	std::string target;
	Rectf rect;
	bool current = false; //!< Choice: the current choice (drawn highlighted)
};

//! A PR's block on a page and its title
struct BlockLayout {
	std::string prLabel;
	Rectf title;
};

struct GroupLayout {
	std::string group;
	size_t block = 0;          //!< Index into Layout::blocks: the block of the group's PR
	Rectf label;               //!< The group's label (and "after restart" when afterRestart)
	Rectf value;               //!< Number: the value between - and +; empty for a choice
	std::string valueText;
	bool afterRestart = false;
};

struct Layout {
	size_t page = 0;   //!< The page laid out: the asked page, clamped to the last
	size_t pages = 1;  //!< At least 1
	bool empty = true; //!< No group registered: only the notice and the Show map button
	std::string notice; //!< When empty: "No variants under test"
	std::vector<BlockLayout> blocks;
	std::vector<GroupLayout> groups; //!< The groups on this page, in panel order
	std::vector<Button> buttons;
};

//! The width of a text in interface pixels; empty: an estimate from the row height
using TextWidth = std::function<float(std::string_view)>;

/*!
 * One page of the panel in \a area (interface pixels), rows \a rowHeight high. The panel order: the PRs in the
 * order their first group was registered, a PR's groups in registration order; a block per PR on a page (its
 * title repeated on the next page when its groups go on there), a group never split between pages. Every rect
 * lies inside \a area and none overlaps another; with more than one page the bottom row holds the page buttons.
 */
Layout layout(const Rectf & area, float rowHeight, size_t page, const TextWidth & textWidth = TextWidth());

} // namespace devvariants

#endif // ARX_GUI_DEVVARIANTS_H
