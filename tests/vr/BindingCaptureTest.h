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

#ifndef ARX_TESTS_VR_BINDINGCAPTURETEST_H
#define ARX_TESTS_VR_BINDINGCAPTURETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class BindingCaptureTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(BindingCaptureTest);
	CPPUNIT_TEST(singleTest);
	CPPUNIT_TEST(tapTest);
	CPPUNIT_TEST(longPressTest);
	CPPUNIT_TEST(chordTest);
	CPPUNIT_TEST(longChordTest);
	CPPUNIT_TEST(slowChordTest);
	CPPUNIT_TEST(heldFromBeforeTest);
	CPPUNIT_TEST(flickerTest);
	CPPUNIT_TEST(otherHandTest);
	CPPUNIT_TEST(reservedTest);
	CPPUNIT_TEST(timeoutTest);
	CPPUNIT_TEST(labelTest);
	CPPUNIT_TEST(layoutChangesTest);
	CPPUNIT_TEST(brokenLayoutFileTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void singleTest();
	void tapTest();
	void longPressTest();
	void chordTest();
	void longChordTest();
	void slowChordTest();
	void heldFromBeforeTest();
	void flickerTest();
	void otherHandTest();
	void reservedTest();
	void timeoutTest();
	void labelTest();
	void layoutChangesTest();
	void brokenLayoutFileTest();

};

#endif // ARX_TESTS_VR_BINDINGCAPTURETEST_H
