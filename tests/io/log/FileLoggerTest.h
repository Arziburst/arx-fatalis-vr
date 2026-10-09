/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Libertatis.
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

#ifndef ARX_TESTS_IO_LOG_FILELOGGERTEST_H
#define ARX_TESTS_IO_LOG_FILELOGGERTEST_H

#include <string>

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "io/fs/FilePath.h"

/*!
 * The file logger must keep the log on disk while the game runs (#129): warnings and errors at
 * once, everything else within the flush interval, and the previous runs' logs rotated away
 * instead of overwritten. Everything here runs against a temporary directory in the working
 * directory, with a short flush interval so the suite stays fast.
 */
class FileLoggerTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(FileLoggerTest);
	CPPUNIT_TEST(testWarningIsOnDiskAtOnce);
	CPPUNIT_TEST(testInfoIsOnDiskWithinTheInterval);
	CPPUNIT_TEST(testExplicitFlush);
	CPPUNIT_TEST(testRotationShiftsEveryRun);
	CPPUNIT_TEST(testRotationWithoutHistory);
	CPPUNIT_TEST(testRotationStopsAtAFileItCannotMove);
	CPPUNIT_TEST_SUITE_END();

	fs::path m_root;

	fs::path numbered(size_t n) const;

	//! Read the file, or an empty string when it does not exist
	std::string contents(const fs::path & file) const;

	//! Poll the file until it contains \a text or \a timeoutMs passed; \return the wait in ms, or -1
	long waitForText(const fs::path & file, const std::string & text, long timeoutMs) const;

public:

	void setUp() override;
	void tearDown() override;

	void testWarningIsOnDiskAtOnce();
	void testInfoIsOnDiskWithinTheInterval();
	void testExplicitFlush();
	void testRotationShiftsEveryRun();
	void testRotationWithoutHistory();
	void testRotationStopsAtAFileItCannotMove();

};

#endif // ARX_TESTS_IO_LOG_FILELOGGERTEST_H
