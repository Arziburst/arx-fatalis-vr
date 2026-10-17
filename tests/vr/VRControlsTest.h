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

#ifndef ARX_TESTS_VR_VRCONTROLSTEST_H
#define ARX_TESTS_VR_VRCONTROLSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class VRControlsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(VRControlsTest);
	CPPUNIT_TEST(singlePressTest);
	CPPUNIT_TEST(tapBetweenUpdatesTest);
	CPPUNIT_TEST(chordTest);
	CPPUNIT_TEST(chordLeaderTapTest);
	CPPUNIT_TEST(simultaneousChordTest);
	CPPUNIT_TEST(gripFlickerTest);
	CPPUNIT_TEST(indexGripThresholdTest);
	CPPUNIT_TEST(longPressTest);
	CPPUNIT_TEST(longChordTest);
	CPPUNIT_TEST(layoutChangeTest);
	CPPUNIT_TEST(stickDirectionTest);
	CPPUNIT_TEST(hysteresisTest);
	CPPUNIT_TEST(bindingTextTest);
	CPPUNIT_TEST(layoutTextTest);
	CPPUNIT_TEST(defaultLayoutTest);
	CPPUNIT_TEST(defaultChordTest);
	CPPUNIT_TEST(jumpStickTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void singlePressTest();
	void tapBetweenUpdatesTest();
	void chordTest();
	void chordLeaderTapTest();
	void simultaneousChordTest();
	void gripFlickerTest();
	void indexGripThresholdTest();
	void longPressTest();
	void longChordTest();
	void layoutChangeTest();
	void stickDirectionTest();
	void hysteresisTest();
	void bindingTextTest();
	void layoutTextTest();
	void defaultLayoutTest();
	void defaultChordTest();
	void jumpStickTest();

};

#endif // ARX_TESTS_VR_VRCONTROLSTEST_H
