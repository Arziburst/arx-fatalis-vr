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

#ifndef ARX_TESTS_VR_MENUREBINDTEST_H
#define ARX_TESTS_VR_MENUREBINDTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Rebinding never leaves the game menu without a binding (#326, written before the implementation).
 *
 * Assumes two functions the worker adds to src/vr/VRControls.h (namespace vr), named exactly:
 *
 *   //! Whether a binding of \a layout leads to the game menu: the menu event, or a radial menu event (its ring has
 *   //! the game menu, and where it cannot open its press is the game menu key, D-042) is bound
 *   bool menuReachable(const ControlLayout & layout);
 *
 *   //! The player's remapping (#178, #326): bind \a event to \a binding in \a layout - unless that leaves the game
 *   //! menu without a binding (\ref menuReachable): then \a layout is unchanged and \a note is a short reason for the
 *   //! VR settings page. \return whether the binding was made
 *   bool assignBinding(ControlLayout & layout, Event event, const Binding & binding, std::string * note = nullptr);
 *
 * VRInput.cpp's setPlayerBinding (the VR settings page) and the test channel's `vr-input bind` go through
 * assignBinding (scripts/dev/xrs/menu-rebind-kept.xrs checks it in the game).
 * Whether giving the menu's control to another event also takes it from the menu ("moving" it) is the worker's
 * choice: the tests only ask that the menu keeps a binding and that pressing it still opens the menu.
 */
class MenuRebindTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MenuRebindTest);
	CPPUNIT_TEST(menuReachableTest);
	CPPUNIT_TEST(rebindMenuButtonTest);
	CPPUNIT_TEST(refuseUnbindingTest);
	CPPUNIT_TEST(allowedRebindTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void menuReachableTest();
	void rebindMenuButtonTest();
	void refuseUnbindingTest();
	void allowedRebindTest();

};

#endif // ARX_TESTS_VR_MENUREBINDTEST_H
