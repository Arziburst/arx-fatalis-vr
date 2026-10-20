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

#ifndef ARX_TESTS_VR_MENUREACHTEST_H
#define ARX_TESTS_VR_MENUREACHTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The game menu stays reachable whatever the player binds (#326), with the interfaces that exist today:
 *  - the fixed fallback of #326 / D-058 is REMOVED (#551, the owner 2026-10-19/20: the menu only via the radial;
 *    the hold of both thumbsticks is the mark of the moment, tests/vr/MarkMomentTest.h): both thumbsticks clicked
 *    (pressed in) and held TOGETHER - for the old 2 s, the mark's 3 s or longer, with ANY layout - never press the
 *    game menu key; one stick alone, or both for a short time, open nothing either. "Opens the game menu" is the
 *    Escape key the controls give the engine (GameInputMapper's GameInputState::escape, VRGameInput.h) coming on;
 *  - a saved controls file (readSavedLayout, vr-controls-<type>.ini) that is empty, broken, or leaves no way to the
 *    game menu gives back the default way to it; a file that only moves the way elsewhere is kept (#326's guard,
 *    which stays; the refused last binding is tests/vr/MenuRebindTest.h).
 * The way to the game menu (D-042): the menu event, or a radial menu event - its ring has the game menu, and where
 * it cannot open its press is the game menu key. On the Index and the Touch it is the right B's radial menu.
 */
class MenuReachTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MenuReachTest);
	CPPUNIT_TEST(bothSticksOpenNoMenuTest);
	CPPUNIT_TEST(bothSticksBoundOpenNoMenuTest);
	CPPUNIT_TEST(fallbackShortPressTest);
	CPPUNIT_TEST(savedLayoutKeepsMenuTest);
	CPPUNIT_TEST(savedLayoutRestoresMenuTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void bothSticksOpenNoMenuTest();
	void bothSticksBoundOpenNoMenuTest();
	void fallbackShortPressTest();
	void savedLayoutKeepsMenuTest();
	void savedLayoutRestoresMenuTest();

};

#endif // ARX_TESTS_VR_MENUREACHTEST_H
