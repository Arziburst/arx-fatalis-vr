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

#ifndef ARX_TESTS_VR_VRTURNTEST_H
#define ARX_TESTS_VR_VRTURNTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class VRTurnTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(VRTurnTest);
	CPPUNIT_TEST(snapOncePerPushTest);
	CPPUNIT_TEST(snapHysteresisTest);
	CPPUNIT_TEST(snapLeftTest);
	CPPUNIT_TEST(snapAngleTest);
	CPPUNIT_TEST(smoothTest);
	CPPUNIT_TEST(blockedTest);
	CPPUNIT_TEST(modeSwitchTest);
	CPPUNIT_TEST(modeNameTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void snapOncePerPushTest();
	void snapHysteresisTest();
	void snapLeftTest();
	void snapAngleTest();
	void smoothTest();
	void blockedTest();
	void modeSwitchTest();
	void modeNameTest();

};

#endif // ARX_TESTS_VR_VRTURNTEST_H
