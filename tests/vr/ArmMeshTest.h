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

#ifndef ARX_TESTS_VR_ARMMESHTEST_H
#define ARX_TESTS_VR_ARMMESHTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The arm mesh core of the VR body (#385.1): the glTF loader on both shipped glove hands (26 joints,
 * every XR_EXT_hand_tracking joint found, vertex and triangle counts, normalised weights, a malformed
 * buffer refused), the weighted CPU skinning (rest pose, half-way blend, four joints, renormalised
 * normals) and smooth normals recomputed from moved positions.
 *
 * The shipped files are found through the compiled-in ARX_TEST_SOURCE_DIR (the source tree's root).
 */
class ArmMeshTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ArmMeshTest);
	CPPUNIT_TEST(testLoadsBothHands);
	CPPUNIT_TEST(testEveryHandJointIsFound);
	CPPUNIT_TEST(testWeightsAreNormalised);
	CPPUNIT_TEST(testMalformedBufferFails);
	CPPUNIT_TEST(testRestPoseReproducesRestPositions);
	CPPUNIT_TEST(testHalfWeightLandsHalfWay);
	CPPUNIT_TEST(testFourJointVertexMatchesHandComputation);
	CPPUNIT_TEST(testNormalsAreRenormalised);
	CPPUNIT_TEST(testRecomputeNormalsOfAFlatStrip);
	CPPUNIT_TEST_SUITE_END();

public:

	void testLoadsBothHands();
	void testEveryHandJointIsFound();
	void testWeightsAreNormalised();
	void testMalformedBufferFails();
	void testRestPoseReproducesRestPositions();
	void testHalfWeightLandsHalfWay();
	void testFourJointVertexMatchesHandComputation();
	void testNormalsAreRenormalised();
	void testRecomputeNormalsOfAFlatStrip();

};

#endif // ARX_TESTS_VR_ARMMESHTEST_H
