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

#ifndef ARX_TESTS_IO_FS_STEAMLIBRARYTEST_H
#define ARX_TESTS_IO_FS_STEAMLIBRARYTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "io/fs/FilePath.h"

class SteamLibraryTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(SteamLibraryTest);
	CPPUNIT_TEST(keyValuesTest);
	CPPUNIT_TEST(malformedKeyValuesTest);
	CPPUNIT_TEST(libraryFoldersTest);
	CPPUNIT_TEST(oldLibraryFoldersTest);
	CPPUNIT_TEST(appManifestTest);
	CPPUNIT_TEST(findInSecondLibraryTest);
	CPPUNIT_TEST(notFoundTest);
	CPPUNIT_TEST_SUITE_END();

	fs::path m_root;

public:

	void setUp() override;
	void tearDown() override;

	void keyValuesTest();
	void malformedKeyValuesTest();
	void libraryFoldersTest();
	void oldLibraryFoldersTest();
	void appManifestTest();
	void findInSecondLibraryTest();
	void notFoundTest();

};

#endif // ARX_TESTS_IO_FS_STEAMLIBRARYTEST_H
