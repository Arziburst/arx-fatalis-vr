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

#ifndef ARX_TESTS_PLATFORM_HANGWATCHDOGTEST_H
#define ARX_TESTS_PLATFORM_HANGWATCHDOGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The hang watchdog (#440): a thread that stops beating is reported within its threshold, with
 * the stack it is stuck in (on Windows x64 the stalling function by name), again while it stays
 * stuck (at most three times) and once more when it beats again; a thread that keeps beating is
 * never reported.
 */
class HangWatchdogTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HangWatchdogTest);
	CPPUNIT_TEST(testStallIsReportedWithStack);
	CPPUNIT_TEST(testBeatingThreadIsQuiet);
	CPPUNIT_TEST_SUITE_END();

public:

	void testStallIsReportedWithStack();
	void testBeatingThreadIsQuiet();

};

#endif // ARX_TESTS_PLATFORM_HANGWATCHDOGTEST_H
