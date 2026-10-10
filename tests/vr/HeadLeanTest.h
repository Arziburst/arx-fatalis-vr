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

#ifndef ARX_TESTS_VR_HEADLEANTEST_H
#define ARX_TESTS_VR_HEADLEANTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Leaning and real steps of #26 (D-018): inside the follow radius the body stays, beyond it the body
 * is asked to step back to the radius; what the collision lets it walk becomes the anchor; turning
 * the body turns only the lean; a head far from the body fades; the fade moves at its two rates.
 */
class HeadLeanTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HeadLeanTest);
	CPPUNIT_TEST(testInsideTheRadiusTheBodyStays);
	CPPUNIT_TEST(testBeyondTheRadiusTheBodySteps);
	CPPUNIT_TEST(testWalkingAMeter);
	CPPUNIT_TEST(testBlockedBodyLeavesTheLean);
	CPPUNIT_TEST(testTurnRoundTrip);
	CPPUNIT_TEST(testTurningKeepsTheWalk);
	CPPUNIT_TEST(testFarFade);
	CPPUNIT_TEST(testFadeRates);
	CPPUNIT_TEST(testDistanceToTriangle);
	CPPUNIT_TEST_SUITE_END();

public:

	void testInsideTheRadiusTheBodyStays();
	void testBeyondTheRadiusTheBodySteps();
	void testWalkingAMeter();
	void testBlockedBodyLeavesTheLean();
	void testTurnRoundTrip();
	void testTurningKeepsTheWalk();
	void testFarFade();
	void testFadeRates();
	void testDistanceToTriangle();

};

#endif // ARX_TESTS_VR_HEADLEANTEST_H
