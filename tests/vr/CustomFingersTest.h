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

#ifndef ARX_TESTS_VR_CUSTOMFINGERSTEST_H
#define ARX_TESTS_VR_CUSTOMFINGERSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * What the custom hand's five fingers follow (#385.4, vr::customFingerTarget): a held weapon closes either hand
 * (D-042) whatever its joints say, else the hand joints (the thumb's first joint past its rest angle), else the
 * controls (D-041), else nothing.
 */
class CustomFingersTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(CustomFingersTest);
	CPPUNIT_TEST(testHeldClosesEitherHand);
	CPPUNIT_TEST(testJointsWithTheThumbPastItsRest);
	CPPUNIT_TEST(testControlsWithoutJoints);
	CPPUNIT_TEST(testNoSource);
	CPPUNIT_TEST_SUITE_END();

public:

	void testHeldClosesEitherHand();
	void testJointsWithTheThumbPastItsRest();
	void testControlsWithoutJoints();
	void testNoSource();

};

#endif // ARX_TESTS_VR_CUSTOMFINGERSTEST_H
