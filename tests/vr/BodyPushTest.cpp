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

#include "vr/BodyPushTest.h"

#include <cmath>

#include "vr/BodyView.h"

CppUnit::AutoRegisterSuite<BodyPushTest> g_registerBodyPushTest;

void BodyPushTest::testAheadAndUpUseTheBase() {
	vr::BodyViewParams params;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push, vr::pushForPitch(0.f, params), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push, vr::pushForPitch(-45.f, params), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push, vr::pushForPitch(-90.f, params), 1e-4);
}

void BodyPushTest::testStraightDownAddsTheExtra() {
	vr::BodyViewParams params;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push + params.pushDown, vr::pushForPitch(90.f, params), 1e-4);
	// Past straight down (a head tipped over) it stays there
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push + params.pushDown, vr::pushForPitch(120.f, params), 1e-4);
	// The values cfg.ini gives are used
	params.push = 0.f;
	params.pushDown = 20.f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.f, vr::pushForPitch(90.f, params), 1e-4);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, vr::pushForPitch(0.f, params), 1e-4);
}

void BodyPushTest::testGrowsAsTheHeadLooksDown() {
	vr::BodyViewParams params;
	float before = vr::pushForPitch(0.f, params);
	for(float down = 10.f; down <= 90.f; down += 10.f) {
		const float push = vr::pushForPitch(down, params);
		CPPUNIT_ASSERT(push > before);
		before = push;
	}
	// Half the extra at 30 degrees down (the sine)
	CPPUNIT_ASSERT_DOUBLES_EQUAL(params.push + 0.5f * params.pushDown, vr::pushForPitch(30.f, params), 1e-4);
}
