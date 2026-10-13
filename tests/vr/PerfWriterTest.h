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

#ifndef ARX_TESTS_VR_PERFWRITERTEST_H
#define ARX_TESTS_VR_PERFWRITERTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The performance log's writer (#327): lines reach the sink whole and in order, a push never waits
 * for a slow sink (the frame never waits for the disk), a sink that falls far behind costs dropped
 * lines instead of memory, and nothing pushed is lost at the end.
 */
class PerfWriterTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(PerfWriterTest);
	CPPUNIT_TEST(testLinesReachTheSinkInOrder);
	CPPUNIT_TEST(testPushNeverWaitsForTheSink);
	CPPUNIT_TEST(testPushNeverWaitsForTheLock);
	CPPUNIT_TEST(testDropsWhenTheSinkFallsBehind);
	CPPUNIT_TEST(testDestructorWritesTheRest);
	CPPUNIT_TEST_SUITE_END();

public:

	void testLinesReachTheSinkInOrder();
	void testPushNeverWaitsForTheSink();
	void testPushNeverWaitsForTheLock();
	void testDropsWhenTheSinkFallsBehind();
	void testDestructorWritesTheRest();

};

#endif // ARX_TESTS_VR_PERFWRITERTEST_H
