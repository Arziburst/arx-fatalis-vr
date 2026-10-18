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

#ifndef ARX_TESTS_GAME_JUMPPHYSICSTEST_H
#define ARX_TESTS_GAME_JUMPPHYSICSTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The player's jump in fixed steps (src/game/JumpPhysics.h, #23, #537): the same flight - steps in the air, apex,
 * forward distance - with the frames of 72, 90, 120 and 144 Hz and with an irregular sequence of frames, and the
 * flight the game is known for.
 */
class JumpPhysicsTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(JumpPhysicsTest);
	CPPUNIT_TEST(testTheClockTakesTheSameStepsForTheSameTime);
	CPPUNIT_TEST(testAHitchTakesItsStepsAtOnce);
	CPPUNIT_TEST(testTheRiseIsTheSameWhateverTheStep);
	CPPUNIT_TEST(testTheFlightIsTheSameAtEveryRate);
	CPPUNIT_TEST(testTheFlightIsTheSameUnderIrregularFrames);
	CPPUNIT_TEST(testTheFlightIsTheGamesJump);
	CPPUNIT_TEST(testAVariableStepWouldNotBe);
	CPPUNIT_TEST_SUITE_END();

public:

	void testTheClockTakesTheSameStepsForTheSameTime();
	void testAHitchTakesItsStepsAtOnce();
	void testTheRiseIsTheSameWhateverTheStep();
	void testTheFlightIsTheSameAtEveryRate();
	void testTheFlightIsTheSameUnderIrregularFrames();
	void testTheFlightIsTheGamesJump();
	void testAVariableStepWouldNotBe();

};

#endif // ARX_TESTS_GAME_JUMPPHYSICSTEST_H
