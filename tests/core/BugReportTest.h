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

#ifndef ARX_TESTS_CORE_BUGREPORTTEST_H
#define ARX_TESTS_CORE_BUGREPORTTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "io/fs/FilePath.h"

//! A save exported for a bug report (#417, src/core/BugReport.h): the scrubber, the zip, the export
class BugReportTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(BugReportTest);
	CPPUNIT_TEST(homePathTest);
	CPPUNIT_TEST(steamIdTest);
	CPPUNIT_TEST(otherIdentifiersTest);
	CPPUNIT_TEST(namesTest);
	CPPUNIT_TEST(technicalTextStaysTest);
	CPPUNIT_TEST(usableNamesTest);
	CPPUNIT_TEST(zipTest);
	CPPUNIT_TEST(fileNameTest);
	CPPUNIT_TEST(exportTest);
	CPPUNIT_TEST(exportWithoutLogsTest);
	CPPUNIT_TEST(exportWithoutSaveTest);
	CPPUNIT_TEST_SUITE_END();

	fs::path m_root;

public:

	void setUp() override;
	void tearDown() override;

	void homePathTest();
	void steamIdTest();
	void otherIdentifiersTest();
	void namesTest();
	void technicalTextStaysTest();
	void usableNamesTest();
	void zipTest();
	void fileNameTest();
	void exportTest();
	void exportWithoutLogsTest();
	void exportWithoutSaveTest();

};

#endif // ARX_TESTS_CORE_BUGREPORTTEST_H
