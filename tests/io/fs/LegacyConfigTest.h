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

#ifndef ARX_TESTS_IO_FS_LEGACYCONFIGTEST_H
#define ARX_TESTS_IO_FS_LEGACYCONFIGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "io/fs/FilePath.h"

//! The first start's copy of cfg.ini from the old Arx Libertatis user directory (#417, fs::copyLegacyConfig)
class LegacyConfigTest final : public CppUnit::TestFixture {
	
	CPPUNIT_TEST_SUITE(LegacyConfigTest);
	CPPUNIT_TEST(firstStartTest);
	CPPUNIT_TEST(onlyOnceTest);
	CPPUNIT_TEST(ownConfigKeptTest);
	CPPUNIT_TEST(noLegacyTest);
	CPPUNIT_TEST(savesNeverCopiedTest);
	CPPUNIT_TEST_SUITE_END();
	
	fs::path m_root;
	fs::path m_legacy;
	fs::path m_user;
	
public:
	
	void setUp() override;
	void tearDown() override;
	
	void firstStartTest();
	void onlyOnceTest();
	void ownConfigKeptTest();
	void noLegacyTest();
	void savesNeverCopiedTest();
	
};

#endif // ARX_TESTS_IO_FS_LEGACYCONFIGTEST_H
