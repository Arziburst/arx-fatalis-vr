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

#ifndef ARX_TESTS_VR_BODYYAWTEST_H
#define ARX_TESTS_VR_BODYYAWTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The body after the head of #25 (D-009 / F-52): a head beyond the dead zone drags the body at once,
 * inside it the body settles on the head in the settle time, the short way round across ±180°.
 */
class BodyYawTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(BodyYawTest);
	CPPUNIT_TEST(testWrapSignedDegrees);
	CPPUNIT_TEST(testStillHeadKeepsTheBody);
	CPPUNIT_TEST(testDeadZoneDragsAtOnce);
	CPPUNIT_TEST(testSettlesInsideTheDeadZone);
	CPPUNIT_TEST(testGlanceBarelyTurnsTheBody);
	CPPUNIT_TEST(testShortWayAcrossTheSeam);
	CPPUNIT_TEST(testStepSizeDoesNotMatter);
	CPPUNIT_TEST(testDegenerateParams);
	CPPUNIT_TEST_SUITE_END();

public:

	void testWrapSignedDegrees();
	void testStillHeadKeepsTheBody();
	void testDeadZoneDragsAtOnce();
	void testSettlesInsideTheDeadZone();
	void testGlanceBarelyTurnsTheBody();
	void testShortWayAcrossTheSeam();
	void testStepSizeDoesNotMatter();
	void testDegenerateParams();

};

#endif // ARX_TESTS_VR_BODYYAWTEST_H
