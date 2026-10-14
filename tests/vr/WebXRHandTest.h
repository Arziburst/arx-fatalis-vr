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

#ifndef ARX_TESTS_VR_WEBXRHANDTEST_H
#define ARX_TESTS_VR_WEBXRHANDTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The WebXR generic hand as a library hand (#467.1, data/core/vr/hands/webxr/left.glb and right.glb, MIT): the
 * loader reads the binary glTF, the asset's joints (named as WebXR names them: wrist, thumb-metacarpal,
 * thumb-phalanx-proximal, thumb-phalanx-distal, thumb-tip, index-finger-metacarpal, ...-phalanx-proximal,
 * ...-phalanx-intermediate, ...-phalanx-distal, index-finger-tip, then middle-finger-, ring-finger- and
 * pinky-finger- alike) map to the XR_EXT_hand_tracking set, the weights sum to 1, the hand rig is valid, and the
 * rest joint set reproduces the hand's rest.
 */
class WebXRHandTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(WebXRHandTest);
	CPPUNIT_TEST(testBothFilesLoadAsBinaryGltf);
	CPPUNIT_TEST(testEveryOpenXRJointIsFound);
	CPPUNIT_TEST(testWeightsSumToOne);
	CPPUNIT_TEST(testHandRigIsValid);
	CPPUNIT_TEST(testRestJointsReproduceTheRestLocals);
	CPPUNIT_TEST_SUITE_END();

public:

	void testBothFilesLoadAsBinaryGltf();
	void testEveryOpenXRJointIsFound();
	void testWeightsSumToOne();
	void testHandRigIsValid();
	void testRestJointsReproduceTheRestLocals();

};

#endif // ARX_TESTS_VR_WEBXRHANDTEST_H
