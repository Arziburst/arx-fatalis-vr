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

#ifndef ARX_TESTS_PLATFORM_EXITREPORTTEST_H
#define ARX_TESTS_PLATFORM_EXITREPORTTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The report the exit hook writes when the process ends outside the game's shutdown (#146): its
 * lines name the way out, the exit code, the last cause noted and the calling modules - and the
 * fixed buffer it is built in never overflows.
 */
class ExitReportTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ExitReportTest);
	CPPUNIT_TEST(testNumbers);
	CPPUNIT_TEST(testReportNamesCodeCauseAndStack);
	CPPUNIT_TEST(testReportWithoutCauseOrStack);
	CPPUNIT_TEST(testLinesAreErrorLines);
	CPPUNIT_TEST(testOverflowIsCut);
	CPPUNIT_TEST(testModuleFileName);
	CPPUNIT_TEST_SUITE_END();

public:

	void testNumbers();
	void testReportNamesCodeCauseAndStack();
	void testReportWithoutCauseOrStack();
	void testLinesAreErrorLines();
	void testOverflowIsCut();
	void testModuleFileName();

};

#endif // ARX_TESTS_PLATFORM_EXITREPORTTEST_H
