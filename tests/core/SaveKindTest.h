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

#ifndef ARX_TESTS_CORE_SAVEKINDTEST_H
#define ARX_TESTS_CORE_SAVEKINDTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "io/fs/FilePath.h"

//! VR saves and flat saves (#417, src/core/SaveKind.h): the mark, its way through a save block, the filter's rule
class SaveKindTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(SaveKindTest);
	CPPUNIT_TEST(namesTest);
	CPPUNIT_TEST(markTest);
	CPPUNIT_TEST(refusalTest);
	CPPUNIT_TEST(vrSaveBlockTest);
	CPPUNIT_TEST(flatSaveBlockTest);
	CPPUNIT_TEST(flatSaveOfVRBlockTest);
	CPPUNIT_TEST(missingFileTest);
	CPPUNIT_TEST_SUITE_END();

	fs::path m_root;

public:

	void setUp() override;
	void tearDown() override;

	void namesTest();
	void markTest();
	void refusalTest();
	void vrSaveBlockTest();
	void flatSaveBlockTest();
	void flatSaveOfVRBlockTest();
	void missingFileTest();

};

#endif // ARX_TESTS_CORE_SAVEKINDTEST_H
