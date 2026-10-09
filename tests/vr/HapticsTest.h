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

#ifndef ARX_TESTS_VR_HAPTICSTEST_H
#define ARX_TESTS_VR_HAPTICSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Haptic feedback of #42 (D-031): the event table, the master strength, and the per-hand mixer -
 * the strongest request of a frame wins, a weaker pulse never cuts a stronger one still playing.
 */
class HapticsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HapticsTest);
	CPPUNIT_TEST(testNames);
	CPPUNIT_TEST(testTable);
	CPPUNIT_TEST(testHitTakenScalesWithDamage);
	CPPUNIT_TEST(testStrength);
	CPPUNIT_TEST(testStrongestOfAFrameWins);
	CPPUNIT_TEST(testWeakerDoesNotCutStronger);
	CPPUNIT_TEST(testHandsAreIndependent);
	CPPUNIT_TEST(testClear);
	CPPUNIT_TEST_SUITE_END();

public:

	void testNames();
	void testTable();
	void testHitTakenScalesWithDamage();
	void testStrength();
	void testStrongestOfAFrameWins();
	void testWeakerDoesNotCutStronger();
	void testHandsAreIndependent();
	void testClear();

};

#endif // ARX_TESTS_VR_HAPTICSTEST_H
