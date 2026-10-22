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

#ifndef ARX_TESTS_PLATFORM_CRASHNOTICEKINDTEST_H
#define ARX_TESTS_PLATFORM_CRASHNOTICEKINDTEST_H

#include <string>

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The crash notice and a report's kind (#545, M6, the owner's rule): a report of kind=terminated - a
 * TerminateProcess from outside, SteamVR's own quit or the task manager - is never announced; every other kind
 * is. Complements tests/platform/CrashNoticeTest.cpp (the tester's fixtures, untouched).
 */
class CrashNoticeKindTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(CrashNoticeKindTest);
	CPPUNIT_TEST(testTerminatedIsNotAnnounced);
	CPPUNIT_TEST(testEveryOtherKindIs);
	CPPUNIT_TEST_SUITE_END();

public:

	void setUp() override;
	void tearDown() override;

	void testTerminatedIsNotAnnounced();
	void testEveryOtherKindIs();

private:

	std::string m_root;

};

#endif // ARX_TESTS_PLATFORM_CRASHNOTICEKINDTEST_H
