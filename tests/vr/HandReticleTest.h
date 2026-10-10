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

#ifndef ARX_TESTS_VR_HANDRETICLETEST_H
#define ARX_TESTS_VR_HANDRETICLETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The hand reticles of #37 (D-016, D-019, D-029, F-57, F-175): the hover hysteresis, the world band of
 * mouse positions and what they stand for, the ring's quad, and the menu pointer's arbiter with world
 * targets - a press on the world clicks, a press on nothing is muted, the pressing hand keeps the
 * mouse while its trigger is held, a hand that moved onto something new takes an idle mouse.
 */
class HandReticleTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HandReticleTest);
	CPPUNIT_TEST(testHoverNeedsTwoFrames);
	CPPUNIT_TEST(testHoverHeldBriefly);
	CPPUNIT_TEST(testWorldBand);
	CPPUNIT_TEST(testPickHistory);
	CPPUNIT_TEST(testReticleQuad);
	CPPUNIT_TEST(testWorldPressClicks);
	CPPUNIT_TEST(testDragStaysWithItsHand);
	CPPUNIT_TEST(testClaimTakesIdleCursor);
	CPPUNIT_TEST(testPanelWinsOverWorld);
	CPPUNIT_TEST_SUITE_END();

public:

	void testHoverNeedsTwoFrames();
	void testHoverHeldBriefly();
	void testWorldBand();
	void testPickHistory();
	void testReticleQuad();
	void testWorldPressClicks();
	void testDragStaysWithItsHand();
	void testClaimTakesIdleCursor();
	void testPanelWinsOverWorld();

};

#endif // ARX_TESTS_VR_HANDRETICLETEST_H
