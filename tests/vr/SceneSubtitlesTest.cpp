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

#include "vr/SceneSubtitlesTest.h"

#include <string>

#include "vr/SceneSubtitles.h"

CppUnit::AutoRegisterSuite<SceneSubtitlesTest> g_registerSceneSubtitlesTest;

using namespace vr;

void SceneSubtitlesTest::placementNameTest() {

	for(SubtitlePlacement placement : { SubtitlePlacement::Above, SubtitlePlacement::Chest,
	                                    SubtitlePlacement::LowerThird }) {
		CPPUNIT_ASSERT(parseSubtitlePlacement(subtitlePlacementName(placement)) == placement);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("above"), std::string(subtitlePlacementName(DefaultSubtitlePlacement)));
	CPPUNIT_ASSERT(!parseSubtitlePlacement("below"));

}
