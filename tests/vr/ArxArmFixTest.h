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

#ifndef ARX_TESTS_VR_ARXARMFIXTEST_H
#define ARX_TESTS_VR_ARXARMFIXTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The classic Arx arm (#466.1, vr/ArxArmFix.h) on a synthetic arm built like human_base's: tube rings along a
 * straight arm, a ring at the elbow mixing the upper arm's and the forearm's vertices, the forearm's last two
 * thirds in the wrist group and a web face from the upper arm to one of them. The diagnosis finds all of it, the
 * classic skin re-assigns by ring, and the measures read two creases and a twisted forearm on the engine's skin
 * and one crease with a rigid forearm on the classic one.
 */
class ArxArmFixTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ArxArmFixTest);
	CPPUNIT_TEST(testDiagnosisReadsTheArm);
	CPPUNIT_TEST(testClassicPartsByRing);
	CPPUNIT_TEST(testStraightArmHasNoCrease);
	CPPUNIT_TEST(testEngineSkinFoldsTheForearm);
	CPPUNIT_TEST(testClassicSkinFoldsOnce);
	CPPUNIT_TEST(testRollStaysOffTheClassicForearm);
	CPPUNIT_TEST(testSkinOnBone);
	CPPUNIT_TEST(testDegenerateChain);
	CPPUNIT_TEST_SUITE_END();

public:

	void testDiagnosisReadsTheArm();
	void testClassicPartsByRing();
	void testStraightArmHasNoCrease();
	void testEngineSkinFoldsTheForearm();
	void testClassicSkinFoldsOnce();
	void testRollStaysOffTheClassicForearm();
	void testSkinOnBone();
	void testDegenerateChain();

};

#endif // ARX_TESTS_VR_ARXARMFIXTEST_H
