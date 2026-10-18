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

#ifndef ARX_TESTS_VR_RADIALMENUTEST_H
#define ARX_TESTS_VR_RADIALMENUTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The radial menu of a hand (#376, D-042): the ring faces the eyes at the hand; the middle selects
 * nothing; 8 segments with the inventory on top and the quick slots clockwise; a segment stays
 * selected a little past its edge; the half-ring opens above the ring, selects left to right and
 * closes back in the ring or below it; the quick slots' saved text round-trips and a broken one names
 * no item.
 */
class RadialMenuTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(RadialMenuTest);
	CPPUNIT_TEST(testPlacedAtTheHandFacingTheEyes);
	CPPUNIT_TEST(testPlanePoint);
	CPPUNIT_TEST(testMiddleSelectsNothing);
	CPPUNIT_TEST(testSegments);
	CPPUNIT_TEST(testSegmentHysteresis);
	CPPUNIT_TEST(testHalfRingOpensAboveTheRing);
	CPPUNIT_TEST(testHalfRingItemsLeftToRight);
	CPPUNIT_TEST(testHalfRingCloses);
	CPPUNIT_TEST(testNames);
	CPPUNIT_TEST(testQuickSlotText);
	CPPUNIT_TEST_SUITE_END();

public:

	void testPlacedAtTheHandFacingTheEyes();
	void testPlanePoint();
	void testMiddleSelectsNothing();
	void testSegments();
	void testSegmentHysteresis();
	void testHalfRingOpensAboveTheRing();
	void testHalfRingItemsLeftToRight();
	void testHalfRingCloses();
	void testNames();
	void testQuickSlotText();

};

#endif // ARX_TESTS_VR_RADIALMENUTEST_H
