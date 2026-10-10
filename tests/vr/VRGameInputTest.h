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

#ifndef ARX_TESTS_VR_VRGAMEINPUTTEST_H
#define ARX_TESTS_VR_VRGAMEINPUTTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class VRGameInputTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(VRGameInputTest);
	CPPUNIT_TEST(clickTest);
	CPPUNIT_TEST(useTest);
	CPPUNIT_TEST(stashTest);
	CPPUNIT_TEST(buttonActionsTest);
	CPPUNIT_TEST(menuTest);
	CPPUNIT_TEST(recenterTest);
	CPPUNIT_TEST(deadZoneTest);
	CPPUNIT_TEST(sticksTest);
	CPPUNIT_TEST(crouchDoesNotTurnTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void clickTest();
	void useTest();
	void stashTest();
	void buttonActionsTest();
	void menuTest();
	void recenterTest();
	void deadZoneTest();
	void sticksTest();
	void crouchDoesNotTurnTest();

};

#endif // ARX_TESTS_VR_VRGAMEINPUTTEST_H
