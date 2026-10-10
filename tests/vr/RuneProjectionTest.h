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

#ifndef ARX_TESTS_VR_RUNEPROJECTIONTEST_H
#define ARX_TESTS_VR_RUNEPROJECTIONTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The rune stroke as the recogniser sees it (#44, vr/RuneProjection.h): the projection's convention (the
 * look's right is +x, down is +y, the depth is dropped, for any look yaw - also for a hand placed by
 * handToWorld); the sampler emits the first point and then one per step along the path, never two closer
 * than the step; the view gate refuses a stroke behind the look, too low or too high; the aim is where the
 * controller's axis points, as the engine's pitch (+ down) and yaw.
 */
class RuneProjectionTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(RuneProjectionTest);
	CPPUNIT_TEST(testProjectionConvention);
	CPPUNIT_TEST(testProjectionOfTheHand);
	CPPUNIT_TEST(testSamplerStep);
	CPPUNIT_TEST(testSamplerCorner);
	CPPUNIT_TEST(testViewGate);
	CPPUNIT_TEST(testAim);
	CPPUNIT_TEST_SUITE_END();

public:

	void testProjectionConvention();
	void testProjectionOfTheHand();
	void testSamplerStep();
	void testSamplerCorner();
	void testViewGate();
	void testAim();

};

#endif // ARX_TESTS_VR_RUNEPROJECTIONTEST_H
