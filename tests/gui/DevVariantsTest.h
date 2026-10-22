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

#ifndef ARX_TESTS_GUI_DEVVARIANTSTEST_H
#define ARX_TESTS_GUI_DEVVARIANTSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The dev variants panel (#588): a registry of the variants of the PRs under test and the panel that replaces the
 * spellbook's map page in development launches. Written before the implementation - the interface below is the
 * contract the implementation follows as written.
 *
 * Header "gui/DevVariants.h", source src/gui/DevVariants.cpp (added to arxtest_SOURCES in tests/CMakeLists.txt by
 * the implementation), namespace devvariants. Pure logic: no renderer, no game state, no g_devLaunch inside (the
 * caller passes it). One global registry.
 *
 *   enum class Kind { Choice, Number };                       // later kinds (an action button, #481) are added here
 *
 *   struct Entry {
 *     std::string group;                  // the group's id: not empty, no white space; the panel shows it as the label
 *     std::string prLabel;                // the PR / issue the group belongs to, e.g. "#588"
 *     Kind kind;
 *     std::vector<std::string> choices;   // Choice: the named choices, in order
 *     size_t current;                     // Choice: the index of the current choice
 *     float min, max, step;               // Number: the range and the step of - / +
 *     std::string unit;                   // Number: shown after the value ("m", "%"; empty: none)
 *     float value;                        // Number: the current value, always within [min, max]
 *     bool afterRestart;                  // the change takes effect after a restart: the panel says "after restart"
 *   };
 *
 *   // One line per variant set. Registering does NOT call the setter (the code's default is already in effect).
 *   // Registering a group id that exists replaces it in its place. false (and nothing registered) for: an empty id
 *   // or one with white space; no choices or defaultIndex out of range; min >= max or step <= 0. A number's default
 *   // outside [min, max] is clamped.
 *   bool registerChoice(std::string_view group, std::string_view prLabel, std::vector<std::string> choices,
 *                       size_t defaultIndex, std::function<void(size_t)> setter, bool afterRestart = false);
 *   bool registerNumber(std::string_view group, std::string_view prLabel, float min, float max, float step,
 *                       std::string_view unit, float defaultValue, std::function<void(float)> setter,
 *                       bool afterRestart = false);
 *   bool unregister(std::string_view group);   // false for an unknown group
 *   void clear();                              // every group gone
 *   std::vector<Entry> entries();              // (or a const reference) in registration order
 *   const Entry * find(std::string_view group);  // nullptr for an unknown group
 *
 *   // Switching. A call that CHANGES the value calls the setter once, at once (before it returns), logs exactly one
 *   // Info line "[dev] variant <group> = <valueText>" and returns true. A call that leaves the value as it is (the
 *   // current choice again, + at the max, - at the min) or is invalid (unknown group, wrong kind, index out of
 *   // range, unknown label, not a number) calls nothing, logs nothing and returns false.
 *   bool choose(std::string_view group, size_t index);          // Choice
 *   bool step(std::string_view group, int direction);           // Number: value + direction * step, clamped
 *                                                               // (the value stays on its grid min + k * step or
 *                                                               // at max: no float drift)
 *   bool set(std::string_view group, std::string_view value);   // Choice: by its exact label; Number: a decimal
 *                                                               // number with '.', clamped (the channel's set)
 *
 *   // The value as shown and logged: a Choice's label; a Number rounded to 3 decimals with trailing zeros and a
 *   // trailing point removed ("0.75", "1", "0.3" after three steps of 0.1), then " <unit>" when there is a unit.
 *   // Empty for an unknown group.
 *   std::string valueText(std::string_view group);
 *
 *   // What the map page shows: the normal map in every launch that is not a development launch (whatever is
 *   // registered); in a development launch the panel when a group is registered; with none, Empty ("No variants
 *   // under test" and a "Show map" button) until "Show map" is pressed, then the map (showMap is the page's state;
 *   // closing the book resets it).
 *   enum class MapPageView { Map, Panel, Empty };
 *   MapPageView mapPageView(bool devLaunch, size_t groups, bool showMap);
 *
 *   // The panel's layout in the area of the page (interface pixels), one page of it. Rows are rowHeight high.
 *   enum class ButtonKind { Choice, Minus, Plus, PrevPage, NextPage, ShowMap };
 *   struct Button {
 *     ButtonKind kind;
 *     std::string group;    // Choice / Minus / Plus: the group; empty otherwise
 *     size_t index;         // Choice: the choice's index
 *     std::string label;    // Choice: the choice's text; Minus "-", Plus "+"
 *     std::string target;   // the test channel's name (vr-point <hand> menu <target>, ui.menuHover):
 *                           //   devpanel.<group>.<index>, devpanel.<group>.minus, devpanel.<group>.plus,
 *                           //   devpanel.prev, devpanel.next, devpanel.show_map
 *     Rectf rect;
 *     bool current;         // Choice: the current choice (drawn highlighted)
 *   };
 *   struct BlockLayout { std::string prLabel; Rectf title; };   // a PR's block on this page and its title
 *   struct GroupLayout {
 *     std::string group;
 *     size_t block;         // index into Layout::blocks: the block of the group's PR
 *     Rectf label;          // the group's label (and "after restart" when afterRestart)
 *     Rectf value;          // Number: the value between - and +; empty for a Choice
 *     std::string valueText;
 *     bool afterRestart;
 *   };
 *   struct Layout {
 *     size_t page;          // the page laid out: the asked page, clamped to the last
 *     size_t pages;         // at least 1
 *     bool empty;           // no group registered: only the notice and the ShowMap button
 *     std::string notice;   // empty: "No variants under test"
 *     std::vector<BlockLayout> blocks;
 *     std::vector<GroupLayout> groups;   // the groups on this page, in panel order
 *     std::vector<Button> buttons;
 *   };
 *   Layout layout(const Rectf & area, float rowHeight, size_t page);
 *   // (more parameters with defaults are fine, e.g. a text width measure for the button widths)
 *
 * The panel order: the PRs in the order their first group was registered, a PR's groups in registration order;
 * one block per PR on a page (a PR's groups may continue on the next page under its title again), a group never
 * split between pages. Every rect (block titles, group labels, number values, buttons) lies inside the area, no two
 * overlap, each is at least 0.8 * rowHeight high and every button at least rowHeight wide. When everything fits there
 * is one page and no paging button; otherwise the first page has NextPage, the last PrevPage, the ones between both.
 *
 * The game side (the sim sequence scripts/dev/xrs/dev-variants.xrs): the channel command `devvariants` and the
 * status fields devPanel.* are listed at the top of that sequence.
 */
class DevVariantsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(DevVariantsTest);
	CPPUNIT_TEST(testRegisterAndListInOrder);
	CPPUNIT_TEST(testRegisterRefusesBadGroups);
	CPPUNIT_TEST(testRegisterAgainReplaces);
	CPPUNIT_TEST(testRegisterCallsNoSetter);
	CPPUNIT_TEST(testChooseCallsTheSetterAtOnce);
	CPPUNIT_TEST(testChooseSameOrInvalidDoesNothing);
	CPPUNIT_TEST(testNumberStepsAndClamps);
	CPPUNIT_TEST(testNumberStepsWithoutDrift);
	CPPUNIT_TEST(testNumberDefaultClamped);
	CPPUNIT_TEST(testSetByLabelAndNumber);
	CPPUNIT_TEST(testLogLine);
	CPPUNIT_TEST(testValueText);
	CPPUNIT_TEST(testUnregister);
	CPPUNIT_TEST(testAfterRestartKept);
	CPPUNIT_TEST(testMapPageView);
	CPPUNIT_TEST(testLayoutEmpty);
	CPPUNIT_TEST(testLayoutGroups);
	CPPUNIT_TEST(testLayoutHighlightFollowsChoice);
	CPPUNIT_TEST(testLayoutWideChoicesWrap);
	CPPUNIT_TEST(testLayoutBlocksPerPr);
	CPPUNIT_TEST(testLayoutPagesWhenTooMany);
	CPPUNIT_TEST_SUITE_END();

public:

	void setUp() override;
	void tearDown() override;

	void testRegisterAndListInOrder();
	void testRegisterRefusesBadGroups();
	void testRegisterAgainReplaces();
	void testRegisterCallsNoSetter();
	void testChooseCallsTheSetterAtOnce();
	void testChooseSameOrInvalidDoesNothing();
	void testNumberStepsAndClamps();
	void testNumberStepsWithoutDrift();
	void testNumberDefaultClamped();
	void testSetByLabelAndNumber();
	void testLogLine();
	void testValueText();
	void testUnregister();
	void testAfterRestartKept();
	void testMapPageView();
	void testLayoutEmpty();
	void testLayoutGroups();
	void testLayoutHighlightFollowsChoice();
	void testLayoutWideChoicesWrap();
	void testLayoutBlocksPerPr();
	void testLayoutPagesWhenTooMany();

};

#endif // ARX_TESTS_GUI_DEVVARIANTSTEST_H
