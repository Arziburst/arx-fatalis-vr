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
 *  - a long press (about 2 s) of the LEFT controller's menu button opens the game menu with ANY layout - also one
 *    with no binding to the menu at all, and one where the player bound that button to other events; a short
 *    press does not (the fallback is fixed, not part of the layout). "Opens the game menu" is the Escape key the
 *    controls give the engine (GameInputMapper's GameInputState::escape, VRGameInput.h), wherever the worker puts
 *    the fallback (ControlResolver or GameInputMapper);
 *  - a saved controls file (readSavedLayout, vr-controls-<type>.ini) that is empty, broken, or leaves no way to the
 *    game menu gives back the default way to it; a file that only moves the way elsewhere is kept.
 * The way to the game menu (D-042): the menu event, or a radial menu event - its ring has the game menu, and where
 * it cannot open its press is the game menu key. On the Index and the Touch it is the right B's radial menu.
 */
class MenuReachTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MenuReachTest);
	CPPUNIT_TEST(fallbackOpensMenuTest);
	CPPUNIT_TEST(fallbackNotRebindableTest);
	CPPUNIT_TEST(fallbackShortPressTest);
	CPPUNIT_TEST(savedLayoutKeepsMenuTest);
	CPPUNIT_TEST(savedLayoutRestoresMenuTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void fallbackOpensMenuTest();
	void fallbackNotRebindableTest();
	void fallbackShortPressTest();
	void savedLayoutKeepsMenuTest();
	void savedLayoutRestoresMenuTest();

};

#endif // ARX_TESTS_VR_MENUREACHTEST_H
