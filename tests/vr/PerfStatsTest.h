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

#ifndef ARX_TESTS_VR_PERFSTATSTEST_H
#define ARX_TESTS_VR_PERFSTATSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/PerfStats.h"

/*!
 * The performance log's numbers (#327): the per-second frame time stats (min / mean / p95 / max,
 * the display rate, missed slots, the phase split) and the hitch detector with its cause, from
 * synthetic frames.
 */
class PerfStatsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(PerfStatsTest);
	CPPUNIT_TEST(testSecondStats);
	CPPUNIT_TEST(testP95IsTheNearestRank);
	CPPUNIT_TEST(testHitchThreshold);
	CPPUNIT_TEST(testHitchThresholdFollowsTheCap);
	CPPUNIT_TEST(testNoHitchWithoutAPeriod);
	CPPUNIT_TEST(testHitchNamesItsCause);
	CPPUNIT_TEST(testSecondSumsMissedUploadsAndPhases);
	CPPUNIT_TEST(testFlushClosesTheOpenSecond);
	CPPUNIT_TEST(testFormatSecond);
	CPPUNIT_TEST_SUITE_END();

public:

	void testSecondStats();
	void testP95IsTheNearestRank();
	void testHitchThreshold();
	void testHitchThresholdFollowsTheCap();
	void testNoHitchWithoutAPeriod();
	void testHitchNamesItsCause();
	void testSecondSumsMissedUploadsAndPhases();
	void testFlushClosesTheOpenSecond();
	void testFormatSecond();

};

#endif // ARX_TESTS_VR_PERFSTATSTEST_H
