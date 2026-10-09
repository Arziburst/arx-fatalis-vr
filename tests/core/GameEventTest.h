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

#ifndef ARX_TESTS_CORE_GAMEEVENTTEST_H
#define ARX_TESTS_CORE_GAMEEVENTTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

//! The text of game event lines and the first-N limiter (#130, src/core/GameEventFormat.h)
class GameEventTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(GameEventTest);
	CPPUNIT_TEST(nameOnlyTest);
	CPPUNIT_TEST(numbersTest);
	CPPUNIT_TEST(floatsTest);
	CPPUNIT_TEST(booleansTest);
	CPPUNIT_TEST(plainStringsTest);
	CPPUNIT_TEST(quotedStringsTest);
	CPPUNIT_TEST(limiterTest);
	CPPUNIT_TEST(limiterUnlimitedTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void nameOnlyTest();
	void numbersTest();
	void floatsTest();
	void booleansTest();
	void plainStringsTest();
	void quotedStringsTest();
	void limiterTest();
	void limiterUnlimitedTest();

};

#endif // ARX_TESTS_CORE_GAMEEVENTTEST_H
