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

#ifndef ARX_TESTS_CORE_FILEREPLACETEST_H
#define ARX_TESTS_CORE_FILEREPLACETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The test channel's status.json replacement (#23): a file a reader holds open delays a write a test waits for and
 * never loses it, and a skippable write gives up at once.
 */
class FileReplaceTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(FileReplaceTest);
	CPPUNIT_TEST(testFreeTargetIsReplaced);
	CPPUNIT_TEST(testMissingTargetIsCreated);
	CPPUNIT_TEST(testHeldTargetDelaysAPatientWriteButNeverLosesIt);
	CPPUNIT_TEST(testHeldTargetMakesAOnceWriteGiveUpAtOnce);
	CPPUNIT_TEST(testPatienceEndsWhenTheReaderNeverLetsGo);
	CPPUNIT_TEST_SUITE_END();

public:

	void testFreeTargetIsReplaced();
	void testMissingTargetIsCreated();
	void testHeldTargetDelaysAPatientWriteButNeverLosesIt();
	void testHeldTargetMakesAOnceWriteGiveUpAtOnce();
	void testPatienceEndsWhenTheReaderNeverLetsGo();

};

#endif // ARX_TESTS_CORE_FILEREPLACETEST_H
