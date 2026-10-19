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

#ifndef ARX_TESTS_MATH_RANDOMTEST_H
#define ARX_TESTS_MATH_RANDOMTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The seedable RNG of a replay (#550): `--seed <n>` seeds the main thread's generator, and a clip records the seed
 * that was used - given or taken from the clock - so its replay draws the same numbers.
 *
 * The interface these tests expect - src/math/Random.h:
 *
 *   class Random {
 *     ...
 *     //! The seed this thread's generator was last seeded with: by seed(u64), or the clock's value seed() took
 *     static u64 currentSeed();
 *   };
 *
 * The game: arx.exe --seed <n> (a decimal u64) calls Random::seed(n) on the main thread at start-up, after the
 * clock seed of utf8_main; the clip header's "# seed: " is Random::currentSeed() of the main thread.
 */
class RandomTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(RandomTest);
	CPPUNIT_TEST(testSeedRepeatsTheSequence);
	CPPUNIT_TEST(testClockSeedIsKnown);
	CPPUNIT_TEST(testDifferentSeedsDiffer);
	CPPUNIT_TEST(testThreadsKeepTheirOwnSeed);
	CPPUNIT_TEST_SUITE_END();

public:

	void testSeedRepeatsTheSequence();
	void testClockSeedIsKnown();
	void testDifferentSeedsDiffer();
	void testThreadsKeepTheirOwnSeed();

};

#endif // ARX_TESTS_MATH_RANDOMTEST_H
