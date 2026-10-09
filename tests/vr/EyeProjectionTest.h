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

#ifndef ARX_TESTS_VR_EYEPROJECTIONTEST_H
#define ARX_TESTS_VR_EYEPROJECTIONTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/EyeProjection.h"

/*!
 * The per-eye matrices of the stereo render (#19) are pure math from the located eyes, so every
 * property is checked here with a headset's real numbers (research rows F-06, F-115, F-163): the
 * symmetric case is the engine's own projection, asymmetric angles land on the clip edges with the
 * engine's y flip (a conversion without the flip is the negative control), the near plane maps to
 * depth 0, the eye rig has the right parallax sign and separation under any head yaw, a canted eye
 * turns its view, and the union frustum contains both eyes.
 */
class EyeProjectionTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(EyeProjectionTest);
	CPPUNIT_TEST(testSymmetricFovIsTheEnginesProjection);
	CPPUNIT_TEST(testAsymmetricEdgesLandOnTheClipEdges);
	CPPUNIT_TEST(testWithoutTheYFlipTheUpEdgeIsAtTheBottom);
	CPPUNIT_TEST(testNearPlaneIsDepthZeroAndFarIsOne);
	CPPUNIT_TEST(testEquivalentFovsHaveTheSameExtent);
	CPPUNIT_TEST(testParallelEyesHaveTheRightParallax);
	CPPUNIT_TEST(testTheRigDoesNotDependOnTheHeadPose);
	CPPUNIT_TEST(testEyeWorldPositionsStraddleTheHead);
	CPPUNIT_TEST(testCantedEyeTurnsItsView);
	CPPUNIT_TEST(testUnionIsTheEyeFovForIdenticalEyes);
	CPPUNIT_TEST(testUnionContainsBothEyesWithCant);
	CPPUNIT_TEST_SUITE_END();

	//! The Quest 3's left eye as reported by its runtime (F-115), in radians
	static vr::EyeFov quest3Left();
	//! ... and its right eye: mirrored
	static vr::EyeFov quest3Right();

	//! A located eye with no rotation at \a position (meters)
	static vr::LocatedEye eyeAt(const Vec3f & position, const vr::EyeFov & fov);

	//! Project a view-space direction with \a projection and return its normalized device coordinates
	static Vec3f project(const glm::mat4x4 & projection, const Vec3f & view);

public:

	void testSymmetricFovIsTheEnginesProjection();
	void testAsymmetricEdgesLandOnTheClipEdges();
	void testWithoutTheYFlipTheUpEdgeIsAtTheBottom();
	void testNearPlaneIsDepthZeroAndFarIsOne();
	void testEquivalentFovsHaveTheSameExtent();
	void testParallelEyesHaveTheRightParallax();
	void testTheRigDoesNotDependOnTheHeadPose();
	void testEyeWorldPositionsStraddleTheHead();
	void testCantedEyeTurnsItsView();
	void testUnionIsTheEyeFovForIdenticalEyes();
	void testUnionContainsBothEyesWithCant();

};

#endif // ARX_TESTS_VR_EYEPROJECTIONTEST_H
