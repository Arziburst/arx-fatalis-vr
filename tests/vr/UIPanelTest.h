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

#ifndef ARX_TESTS_VR_UIPANELTEST_H
#define ARX_TESTS_VR_UIPANELTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The floating UI panel of #49 (F-80, F-82, F-180): placed on the head's heading only, ahead and
 * below the eyes, facing them; fixed while the menu stays open whatever the head does; kept over a
 * short close (inventory -> book), placed anew after it and on a recenter; never placed without a
 * tracked head; a ray maps to the pixel under it for the menu pointer (#34).
 */
class UIPanelTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(UIPanelTest);
	CPPUNIT_TEST(testPlacedAheadBelowTheEyes);
	CPPUNIT_TEST(testFacesTheEyes);
	CPPUNIT_TEST(testOnlyTheYawPlacesIt);
	CPPUNIT_TEST(testHeadingAtThePoles);
	CPPUNIT_TEST(testSizeFollowsTheImage);
	CPPUNIT_TEST(testRayToPixel);
	CPPUNIT_TEST(testPointIsTheInverseOfTheHit);
	CPPUNIT_TEST(testRayMisses);
	CPPUNIT_TEST(testFixedWhileOpen);
	CPPUNIT_TEST(testShortCloseKeepsThePose);
	CPPUNIT_TEST(testRecenterPlacesAnew);
	CPPUNIT_TEST(testWaitsForTracking);
	CPPUNIT_TEST_SUITE_END();

public:

	void testPlacedAheadBelowTheEyes();
	void testFacesTheEyes();
	void testOnlyTheYawPlacesIt();
	void testHeadingAtThePoles();
	void testSizeFollowsTheImage();
	void testRayToPixel();
	void testPointIsTheInverseOfTheHit();
	void testRayMisses();
	void testFixedWhileOpen();
	void testShortCloseKeepsThePose();
	void testRecenterPlacesAnew();
	void testWaitsForTracking();

};

#endif // ARX_TESTS_VR_UIPANELTEST_H
