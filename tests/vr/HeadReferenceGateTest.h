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

#ifndef ARX_TESTS_VR_HEADREFERENCEGATETEST_H
#define ARX_TESTS_VR_HEADREFERENCEGATETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * When the head's reference is taken (#27, D-018): never settled from a headset that is not worn
 * (the owner's #24 run: loaded with the headset on the desk), settled once the head put on is still,
 * or after the maximum wait; re-armed by taking the headset off and by a reset; and the settled head
 * is the character's eye - a head 20 cm lower is a camera 20 units lower.
 */
class HeadReferenceGateTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HeadReferenceGateTest);
	CPPUNIT_TEST(testProvisionalWhileNotWorn);
	CPPUNIT_TEST(testSettledOnceTheWornHeadIsStill);
	CPPUNIT_TEST(testMovingHeadSettlesAfterTheMaximumWait);
	CPPUNIT_TEST(testTakingTheHeadsetOffReArms);
	CPPUNIT_TEST(testResetWhileWornSettlesAtOnce);
	CPPUNIT_TEST(testResetWhileSettlingWaits);
	CPPUNIT_TEST(testSimulatedHeadIsInstant);
	CPPUNIT_TEST(testDeskToHeadScenario);
	CPPUNIT_TEST(testWorldScale);
	CPPUNIT_TEST(testAutoRaise);
	CPPUNIT_TEST_SUITE_END();

public:

	void testProvisionalWhileNotWorn();
	void testSettledOnceTheWornHeadIsStill();
	void testMovingHeadSettlesAfterTheMaximumWait();
	void testTakingTheHeadsetOffReArms();
	void testResetWhileWornSettlesAtOnce();
	void testResetWhileSettlingWaits();
	void testSimulatedHeadIsInstant();
	void testDeskToHeadScenario();
	void testWorldScale();
	void testAutoRaise();

};

#endif // ARX_TESTS_VR_HEADREFERENCEGATETEST_H
