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

#ifndef ARX_TESTS_VR_RUNETRAILTEST_H
#define ARX_TESTS_VR_RUNETRAILTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The rune trail (#43, vr/RuneTrail.h): the glow is ahead of the grip along the controller, whichever
 * way the hand and the body turn; a segment is filled at the step; a stroke is drawn only in the stance
 * with a located hand's trigger, the first trigger wins and the other is ignored until released, and a
 * stroke ends on its trigger, the stance or a lost hand, with its length and duration.
 */
class RuneTrailTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(RuneTrailTest);
	CPPUNIT_TEST(testPointIsAheadOfTheGrip);
	CPPUNIT_TEST(testSegmentFill);
	CPPUNIT_TEST(testStrokeNeedsStanceTriggerAndHand);
	CPPUNIT_TEST(testStrokeFollowsTheHand);
	CPPUNIT_TEST(testStrokeEnds);
	CPPUNIT_TEST(testFirstTriggerWins);
	CPPUNIT_TEST_SUITE_END();

public:

	void testPointIsAheadOfTheGrip();
	void testSegmentFill();
	void testStrokeNeedsStanceTriggerAndHand();
	void testStrokeFollowsTheHand();
	void testStrokeEnds();
	void testFirstTriggerWins();

};

#endif // ARX_TESTS_VR_RUNETRAILTEST_H
