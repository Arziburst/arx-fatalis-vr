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

#ifndef ARX_TESTS_VR_VRBINDINGSTEST_H
#define ARX_TESTS_VR_VRBINDINGSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class VRBindingsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(VRBindingsTest);
	CPPUNIT_TEST(profilePathTest);
	CPPUNIT_TEST(specPathTest);
	CPPUNIT_TEST(noDuplicateTest);
	CPPUNIT_TEST(everyActionBoundTest);
	CPPUNIT_TEST(layoutCoverageTest);
	CPPUNIT_TEST(defaultLayoutsTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void profilePathTest();
	void specPathTest();
	void noDuplicateTest();
	void everyActionBoundTest();
	void layoutCoverageTest();
	void defaultLayoutsTest();

};

#endif // ARX_TESTS_VR_VRBINDINGSTEST_H
