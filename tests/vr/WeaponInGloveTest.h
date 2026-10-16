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

#ifndef ARX_TESTS_VR_WEAPONINGLOVETEST_H
#define ARX_TESTS_VR_WEAPONINGLOVETEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The weapon in the glove (#385.10, research #453 F-487 / F-489). Pure API the worker adds to
 * src/vr/WeaponGrip.h (namespace vr) - the tests are written against exactly this:
 *
 *   enum class WeaponClass { Sword, Dagger, Axe, Mace, TwoHanded };
 *
 *   //! Distance from the pommel end of the handle, along the handle towards the guard, to the point that sits
 *   //! on the grip axis. Sword, Dagger: 0.5 * handleLength. Axe, Mace: one named-table fraction in
 *   //! [0.15, 0.25] of handleLength from the handle end. TwoHanded: handWidth (a hand-width above the
 *   //! pommel), at most handleLength. All results in (0, handleLength].
 *   float weaponAnchorOffset(WeaponClass weaponClass, float handleLength, float handWidth);
 *
 *   //! The rotation about \a blade (unit) that turns \a edge (unit, perpendicular to the blade: the sharp
 *   //! side) towards \a knuckles (any length, the glove's knuckle direction): the rotated edge lies on the
 *   //! knuckles' projection onto the plane perpendicular to the blade. The blade is unchanged. A knuckle
 *   //! direction (nearly) parallel to the blade gives a finite rotation (identity), never NaN.
 *   glm::quat bladeRollToKnuckles(const Vec3f & blade, const Vec3f & edge, const Vec3f & knuckles);
 *
 *   //! The sword's forward tilt: the rotation by |degrees| about cross(handle, thumb) (an axis perpendicular
 *   //! to the handle, the tilt lying in the handle-thumb plane). \a handle and \a thumb unit and
 *   //! perpendicular. Negative degrees tilt the blade's tip towards the thumb; 0 is the identity.
 *   glm::quat swordTiltRotation(float degrees, const Vec3f & handle, const Vec3f & thumb);
 *
 *   //! cfg vr_sword_tilt / `grip tilt <deg>`: the value snapped to the nearest variant of 0, -15, -30
 *   float swordTiltVariant(float degrees);
 */
class WeaponInGloveTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(WeaponInGloveTest);
	CPPUNIT_TEST(testSwordAndDaggerAtHandleCentre);
	CPPUNIT_TEST(testAxeAndMaceNearTheHandleEnd);
	CPPUNIT_TEST(testTwoHandedAHandWidthAbovePommel);
	CPPUNIT_TEST(testEveryAnchorInsideTheHandle);
	CPPUNIT_TEST(testEdgeTowardKnucklesBothHands);
	CPPUNIT_TEST(testEdgeAlreadyAwayFromKnuckles);
	CPPUNIT_TEST(testRollWithKnucklesAlongTheBlade);
	CPPUNIT_TEST(testSwordTiltVariants);
	CPPUNIT_TEST(testSwordTiltSnapsToVariants);
	CPPUNIT_TEST_SUITE_END();

public:

	void testSwordAndDaggerAtHandleCentre();
	void testAxeAndMaceNearTheHandleEnd();
	void testTwoHandedAHandWidthAbovePommel();
	void testEveryAnchorInsideTheHandle();
	void testEdgeTowardKnucklesBothHands();
	void testEdgeAlreadyAwayFromKnuckles();
	void testRollWithKnucklesAlongTheBlade();
	void testSwordTiltVariants();
	void testSwordTiltSnapsToVariants();

};

#endif // ARX_TESTS_VR_WEAPONINGLOVETEST_H
