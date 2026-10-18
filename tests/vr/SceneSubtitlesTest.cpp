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

#include <cmath>
#include <string>

#include "vr/SceneSubtitles.h"

CppUnit::AutoRegisterSuite<SceneSubtitlesTest> g_registerSceneSubtitlesTest;

using namespace vr;

namespace {

void subCheckVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.01f) {
	if(!(glm::length(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

void subCheckFloat(const char * what, float expected, float actual, float tolerance = 0.01f) {
	if(!(std::abs(expected - actual) <= tolerance)) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

//! 90 degrees wide and tall, centred
FovTangents squareView() {
	FovTangents fov;
	fov.left = -1.f;
	fov.right = 1.f;
	fov.up = 1.f;
	fov.down = -1.f;
	return fov;
}

} // anonymous namespace

void SceneSubtitlesTest::placementNameTest() {

	for(SubtitlePlacement placement : { SubtitlePlacement::Above, SubtitlePlacement::Chest,
	                                    SubtitlePlacement::LowerThird }) {
		CPPUNIT_ASSERT(parseSubtitlePlacement(subtitlePlacementName(placement)) == placement);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("above"), std::string(subtitlePlacementName(DefaultSubtitlePlacement)));
	CPPUNIT_ASSERT(!parseSubtitlePlacement("below"));

}

void SceneSubtitlesTest::aboveAnchorTest() {

	// y down: above the head is a smaller y, 0.25 m at 100 units per meter
	SubtitleParams params;
	params.unitsPerMeter = 100.f;
	subCheckVector("above the head", Vec3f(10.f, -195.f, 20.f), subtitleAboveAnchor(Vec3f(10.f, -170.f, 20.f), params));

	// the world scale carries over
	params.unitsPerMeter = 50.f;
	subCheckVector("scaled", Vec3f(0.f, -12.5f, 0.f), subtitleAboveAnchor(Vec3f(0.f), params));

}

void SceneSubtitlesTest::anchorInViewTest() {

	SubtitleParams params;
	params.unitsPerMeter = 100.f;
	const FovTangents fov = squareView();

	// straight ahead, 1 m
	CPPUNIT_ASSERT(subtitleAnchorInView(Vec3f(0.f, 0.f, 100.f), fov, false, params));
	// behind the eyes, or nearer than 0.6 m (the hero speaking in first person): the chest panel
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(0.f, 0.f, -100.f), fov, true, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(0.f, -20.f, 10.f), fov, true, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(0.f, 0.f, 59.f), fov, true, params));
	// off to the side, above or below: outside the view
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(150.f, 0.f, 100.f), fov, true, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(-150.f, 0.f, 100.f), fov, true, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(0.f, -150.f, 100.f), fov, true, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(0.f, 150.f, 100.f), fov, true, params));

	// near the edge: kept while shown, not taken back from the chest panel until well inside
	for(const Vec3f & edge : { Vec3f(95.f, 0.f, 100.f), Vec3f(-95.f, 0.f, 100.f), Vec3f(0.f, -95.f, 100.f),
	                           Vec3f(0.f, 95.f, 100.f) }) {
		CPPUNIT_ASSERT(subtitleAnchorInView(edge, fov, true, params));
		CPPUNIT_ASSERT(!subtitleAnchorInView(edge, fov, false, params));
	}
	CPPUNIT_ASSERT(subtitleAnchorInView(Vec3f(80.f, -80.f, 100.f), fov, false, params));

	// an asymmetric view (an eye's frustum): its own edges
	FovTangents eye = fov;
	eye.left = -0.5f;
	CPPUNIT_ASSERT(!subtitleAnchorInView(Vec3f(-60.f, 0.f, 100.f), eye, true, params));
	CPPUNIT_ASSERT(subtitleAnchorInView(Vec3f(60.f, 0.f, 100.f), eye, true, params));

}

void SceneSubtitlesTest::chestPanelTest() {

	SubtitleParams params;
	const Vec3f head(0.2f, -1.7f, 0.3f);

	// yaw 0 looks along +z (engine axes, y down): the panel 0.8 m ahead and 0.45 m below the eyes
	subCheckVector("yaw 0", Vec3f(0.2f, -1.25f, 1.1f), subtitleChestPanel(head, 0.f, params), 0.001f);

	// any yaw: horizontal, at the same distance, along the way the head faces
	for(float yaw : { 30.f, 90.f, -135.f, 180.f }) {
		const Vec3f panel = subtitleChestPanel(head, yaw, params);
		const Vec3f ahead = panel - head;
		subCheckFloat("below the eyes", params.chestDropM, ahead.y, 0.001f);
		subCheckFloat("ahead", params.chestDistanceM, glm::length(Vec2f(ahead.x, ahead.z)), 0.001f);
		// in the view of a head with that yaw: straight ahead and down
		const Vec3f view = subtitleRoomToView(panel, head, Anglef(0.f, yaw, 0.f), params);
		subCheckVector("in front of the head",
		               Vec3f(0.f, params.chestDropM, params.chestDistanceM) * params.unitsPerMeter, view);
	}

	// a quarter turn puts the panel a quarter turn around the head
	const Vec3f a = subtitleChestPanel(head, 0.f, params) - head;
	const Vec3f b = subtitleChestPanel(head, 90.f, params) - head;
	subCheckFloat("perpendicular", 0.f, a.x * b.x + a.z * b.z, 0.001f);

	// distance and drop are the parameters
	params.chestDistanceM = 1.2f;
	params.chestDropM = 0.3f;
	subCheckVector("tuned", Vec3f(0.2f, -1.4f, 1.5f), subtitleChestPanel(head, 0.f, params), 0.001f);

}

void SceneSubtitlesTest::roomToViewTest() {

	SubtitleParams params;
	params.unitsPerMeter = 100.f;
	const Vec3f head(1.f, -1.6f, -2.f);

	// a head turned nowhere: the offset in the room in game units
	subCheckVector("identity", Vec3f(10.f, 20.f, 100.f),
	               subtitleRoomToView(head + Vec3f(0.1f, 0.2f, 1.f), head, Anglef(0.f, 0.f, 0.f), params));

	// looking down by 30 degrees (positive pitch): a point straight ahead at the eyes' height is up in the view
	const float pitch = 30.f;
	const Vec3f view = subtitleRoomToView(head + Vec3f(0.f, 0.f, 1.f), head, Anglef(pitch, 0.f, 0.f), params);
	subCheckVector("pitched", Vec3f(0.f, -std::sin(glm::radians(pitch)), std::cos(glm::radians(pitch))) * 100.f, view);

	// the panel placed while looking ahead stays in the room: turning the head away takes it out of the view
	const Vec3f panel = subtitleChestPanel(head, 0.f, params);
	CPPUNIT_ASSERT(subtitleAnchorInView(subtitleRoomToView(panel, head, Anglef(0.f, 0.f, 0.f), params), squareView(),
	                                    false, params));
	CPPUNIT_ASSERT(!subtitleAnchorInView(subtitleRoomToView(panel, head, Anglef(0.f, 90.f, 0.f), params), squareView(),
	                                     true, params));

}

void SceneSubtitlesTest::boxTest() {

	// above the speaker: the anchor is the box's lower middle
	const Rectf above = subtitleBox(Vec2f(100.f, 200.f), Vec2f(40.f, 20.f), SubtitleAlign::Above);
	subCheckFloat("above left", 80.f, above.left);
	subCheckFloat("above right", 120.f, above.right);
	subCheckFloat("above top", 180.f, above.top);
	subCheckFloat("above bottom", 200.f, above.bottom);

	// the chest panel: centred on the anchor
	const Rectf chest = subtitleBox(Vec2f(100.f, 200.f), Vec2f(40.f, 20.f), SubtitleAlign::Center);
	subCheckFloat("chest top", 190.f, chest.top);
	subCheckFloat("chest bottom", 210.f, chest.bottom);
	subCheckFloat("chest left", 80.f, chest.left);

	// the lower third: centred, its lower edge at 92 % of the interface's height, in its lowest third
	SubtitleParams params;
	const Rectf lower = subtitleLowerThirdBox(Vec2f(1000.f, 800.f), Vec2f(200.f, 50.f), params);
	subCheckFloat("lower left", 400.f, lower.left);
	subCheckFloat("lower right", 600.f, lower.right);
	subCheckFloat("lower bottom", 736.f, lower.bottom);
	subCheckFloat("lower top", 686.f, lower.top);
	CPPUNIT_ASSERT(lower.top > 800.f * 2.f / 3.f);

	// the widest box: 32 degrees seen from the eyes; 90 degrees is twice the pixels per unit
	subCheckFloat("32 degrees", 2.f * std::tan(glm::radians(16.f)) * 1000.f, subtitleMaxWidth(1000.f, params), 0.1f);
	params.widthDeg = 90.f;
	subCheckFloat("90 degrees", 2000.f, subtitleMaxWidth(1000.f, params), 0.1f);

}
