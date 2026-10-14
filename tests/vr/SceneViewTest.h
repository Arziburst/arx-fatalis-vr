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

#ifndef ARX_TESTS_VR_SCENEVIEWTEST_H
#define ARX_TESTS_VR_SCENEVIEWTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

class SceneViewTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(SceneViewTest);
	CPPUNIT_TEST(namesTest);
	CPPUNIT_TEST(shownViewTest);
	CPPUNIT_TEST(fadeTimingTest);
	CPPUNIT_TEST(floorClampTest);
	CPPUNIT_TEST(firstStartHoldsUntilBlackTest);
	CPPUNIT_TEST(firstTeleportInSceneTest);
	CPPUNIT_TEST(firstFollowsHeroTest);
	CPPUNIT_TEST(ghostHoldsShotTest);
	CPPUNIT_TEST(ghostJumpIsNewShotTest);
	CPPUNIT_TEST(speechLineCutTest);
	CPPUNIT_TEST(sceneEndTest);
	CPPUNIT_TEST(letterboxOnlyTest);
	CPPUNIT_TEST(lookAtTest);
	CPPUNIT_TEST(meterTest);
	CPPUNIT_TEST(meterCutsTest);
	CPPUNIT_TEST(hybridClassificationTest);
	CPPUNIT_TEST(observerPlacementTest);
	CPPUNIT_TEST(observerHoldsSceneTest);
	CPPUNIT_TEST(hybridScreenTest);
	CPPUNIT_TEST(hybridBlindTest);
	CPPUNIT_TEST(heldYawTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void namesTest();
	void shownViewTest();
	void fadeTimingTest();
	void floorClampTest();
	void firstStartHoldsUntilBlackTest();
	void firstTeleportInSceneTest();
	void firstFollowsHeroTest();
	void ghostHoldsShotTest();
	void ghostJumpIsNewShotTest();
	void speechLineCutTest();
	void sceneEndTest();
	void letterboxOnlyTest();
	void lookAtTest();
	void meterTest();
	void meterCutsTest();
	void hybridClassificationTest();
	void observerPlacementTest();
	void observerHoldsSceneTest();
	void hybridScreenTest();
	void hybridBlindTest();
	void heldYawTest();

};

#endif // ARX_TESTS_VR_SCENEVIEWTEST_H
