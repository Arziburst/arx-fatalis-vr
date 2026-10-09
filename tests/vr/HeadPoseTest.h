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

#ifndef ARX_TESTS_VR_HEADPOSETEST_H
#define ARX_TESTS_VR_HEADPOSETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The head pose → camera math of #24 (D-018, F-37, F-120): the engine's own toRotationMatrix() of
 * the produced angles must be the head's rotation for any orientation, the poles included; pitch
 * and roll stay absolute while the yaw follows the body; the head's movement becomes the camera
 * offset in game units, turned with the body, and a lean beyond the limit is clamped.
 */
class HeadPoseTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(HeadPoseTest);
	CPPUNIT_TEST(testIdentityIsTheBodyCamera);
	CPPUNIT_TEST(testSimpleTurnsHaveTheEngineSigns);
	CPPUNIT_TEST(testRotationMatchesTheEngineForAnyOrientation);
	CPPUNIT_TEST(testPolesAreExact);
	CPPUNIT_TEST(testOnlyTheYawIsRelative);
	CPPUNIT_TEST(testRotationAloneDoesNotMoveTheCamera);
	CPPUNIT_TEST(testMovementIsTurnedWithTheBody);
	CPPUNIT_TEST(testLeanIsClamped);
	CPPUNIT_TEST(testRecenterIsTheEye);
	CPPUNIT_TEST(testHeadBetweenTheEyes);
	CPPUNIT_TEST(testWrapDegrees);
	CPPUNIT_TEST(testQuadFovFillsTheQuad);
	CPPUNIT_TEST_SUITE_END();

public:

	void testIdentityIsTheBodyCamera();
	void testSimpleTurnsHaveTheEngineSigns();
	void testRotationMatchesTheEngineForAnyOrientation();
	void testPolesAreExact();
	void testOnlyTheYawIsRelative();
	void testRotationAloneDoesNotMoveTheCamera();
	void testMovementIsTurnedWithTheBody();
	void testLeanIsClamped();
	void testRecenterIsTheEye();
	void testHeadBetweenTheEyes();
	void testWrapDegrees();
	void testQuadFovFillsTheQuad();

};

#endif // ARX_TESTS_VR_HEADPOSETEST_H
