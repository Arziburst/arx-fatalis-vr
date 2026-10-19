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

#include "math/RandomTest.h"

#include <thread>
#include <vector>

#include "math/Random.h"

CppUnit::AutoRegisterSuite<RandomTest> g_registerRandomTest;

namespace ARX_ANONYMOUS_NAMESPACE {

//! What the engine draws: integers, floats and a count, as its effects and scripts do
std::vector<double> randomDraws(size_t count) {
	std::vector<double> draws;
	for(size_t i = 0; i < count; i++) {
		draws.push_back(double(Random::get(0, 1000000)));
		draws.push_back(double(Random::getf()));
		draws.push_back(double(Random::getf(-5.f, 5.f)));
		draws.push_back(double(Random::getCount(10, 0.3f)));
	}
	return draws;
}

} // namespace ARX_ANONYMOUS_NAMESPACE

void RandomTest::testSeedRepeatsTheSequence() {
	Random::seed(4242);
	CPPUNIT_ASSERT_EQUAL(u64(4242), Random::currentSeed());
	const std::vector<double> first = randomDraws(200);
	Random::seed(4242);
	const std::vector<double> second = randomDraws(200);
	CPPUNIT_ASSERT(first == second);
	Random::seed();
}

void RandomTest::testClockSeedIsKnown() {
	// A session without --seed records the clock's seed; seeding with it again replays the same numbers
	Random::seed();
	const u64 seed = Random::currentSeed();
	const std::vector<double> first = randomDraws(200);
	Random::seed(seed);
	CPPUNIT_ASSERT_EQUAL(seed, Random::currentSeed());
	CPPUNIT_ASSERT(first == randomDraws(200));
	Random::seed();
}

void RandomTest::testDifferentSeedsDiffer() {
	Random::seed(1);
	const std::vector<double> one = randomDraws(50);
	Random::seed(2);
	CPPUNIT_ASSERT_EQUAL(u64(2), Random::currentSeed());
	CPPUNIT_ASSERT(one != randomDraws(50));
	Random::seed();
}

void RandomTest::testThreadsKeepTheirOwnSeed() {
	// The generator is per thread: a worker thread seeding its own leaves the main thread's seed and sequence alone
	Random::seed(99);
	const std::vector<double> expected = randomDraws(20);
	Random::seed(99);
	u64 otherSeed = 0;
	std::thread other([&otherSeed]() {
		Random::seed(12345);
		otherSeed = Random::currentSeed();
		(void)randomDraws(100);
		Random::shutdown();
	});
	other.join();
	CPPUNIT_ASSERT_EQUAL(u64(12345), otherSeed);
	CPPUNIT_ASSERT_EQUAL(u64(99), Random::currentSeed());
	CPPUNIT_ASSERT(expected == randomDraws(20));
	Random::seed();
}
