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

#ifndef ARX_TESTS_VR_WEAPONHILTFRAMETEST_H
#define ARX_TESTS_VR_WEAPONHILTFRAMETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The weapon drawn from the glove's hilt frame (#385.10, research #453 F-485): the handle read from a weapon mesh,
 * the class read from the item, and the rotation that puts a mesh in the glove (vr/WeaponGrip.h)
 */
class WeaponHiltFrameTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(WeaponHiltFrameTest);
	CPPUNIT_TEST(testSwordHandleFromMesh);
	CPPUNIT_TEST(testAxeHandleAndBit);
	CPPUNIT_TEST(testHandleWithoutGuard);
	CPPUNIT_TEST(testClassFromItem);
	CPPUNIT_TEST(testWeaponInGloveBothHands);
	CPPUNIT_TEST(testAnchorOnTheHoldPoint);
	CPPUNIT_TEST_SUITE_END();

public:

	void testSwordHandleFromMesh();
	void testAxeHandleAndBit();
	void testHandleWithoutGuard();
	void testClassFromItem();
	void testWeaponInGloveBothHands();
	void testAnchorOnTheHoldPoint();

};

#endif // ARX_TESTS_VR_WEAPONHILTFRAMETEST_H
