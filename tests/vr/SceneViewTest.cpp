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

#include "vr/SceneViewTest.h"

#include <optional>
#include <string>

#include <glm/geometric.hpp>

#include "vr/SceneView.h"

CppUnit::AutoRegisterSuite<SceneViewTest> g_registerSceneViewTest;

using namespace vr;

namespace {

const float SvFrame = 1.f / 90.f;
const float SvEps = 1e-3f;

//! The hero standing on a floor at y = 0 (y down), his eye 1.7 m up, facing yaw 0, the head ahead
SceneInput standing() {
	SceneInput in;
	in.setting = SceneViewSetting::First;
	in.firstPos = Vec3f(1000.f, -170.f, 2000.f);
	in.enginePos = in.firstPos;
	in.engineFloorY = 0.f;
	in.engineFloor = true;
	in.seconds = SvFrame;
	return in;
}

SceneInput sceneOf(SceneInput in, SceneKind kind, u64 shot, SceneViewSetting setting) {
	in.active = true;
	in.kind = kind;
	in.shot = shot;
	in.setting = setting;
	return in;
}

bool same(const Vec3f & a, const Vec3f & b) {
	return std::abs(a.x - b.x) < SvEps && std::abs(a.y - b.y) < SvEps && std::abs(a.z - b.z) < SvEps;
}

/*!
 * Frames of \a in until the director cuts (at most \a limit). \return the cutting frame's output;
 * \a held is false if any frame before it showed something else than \a heldPos
 */
SceneOutput untilCut(SceneDirector & director, const SceneInput & in, const Vec3f & heldPos, bool & held,
                     int & frames, int limit = 200) {
	held = true;
	for(frames = 1; frames <= limit; frames++) {
		SceneOutput out = director.update(in);
		if(out.cut) {
			return out;
		}
		if(!same(out.pos, heldPos)) {
			held = false;
		}
	}
	return SceneOutput();
}

//! Frames of \a in until the fade is gone
void settle(SceneDirector & director, const SceneInput & in) {
	for(int i = 0; i < 200 && (i < 2 || director.fade() > 0.f); i++) {
		director.update(in);
	}
}

} // anonymous namespace

void SceneViewTest::namesTest() {

	for(SceneViewSetting view : { SceneViewSetting::First, SceneViewSetting::Observer, SceneViewSetting::Ghost,
	                              SceneViewSetting::Hybrid }) {
		CPPUNIT_ASSERT(parseSceneView(sceneViewName(view)) == view);
	}
	CPPUNIT_ASSERT_EQUAL(std::string("hybrid"), std::string(sceneViewName(DefaultSceneView)));
	CPPUNIT_ASSERT(!parseSceneView("Ghost"));
	CPPUNIT_ASSERT(!parseSceneView(""));

	CPPUNIT_ASSERT(parseSceneLookAt("ignore") == SceneLookAt::Ignore);
	CPPUNIT_ASSERT(parseSceneLookAt("fade") == SceneLookAt::Fade);
	CPPUNIT_ASSERT(!parseSceneLookAt("snap"));
	CPPUNIT_ASSERT_EQUAL(std::string("ignore"), std::string(sceneLookAtName(DefaultSceneLookAt)));

	CPPUNIT_ASSERT_EQUAL(std::string("none"), std::string(sceneShownName(SceneShown::None)));
	CPPUNIT_ASSERT_EQUAL(std::string("screen"), std::string(sceneShownName(SceneShown::Screen)));
	CPPUNIT_ASSERT_EQUAL(std::string("entity"), std::string(sceneKindName(SceneKind::Entity)));
	CPPUNIT_ASSERT_EQUAL(std::string("speech"), std::string(sceneKindName(SceneKind::Speech)));
}

void SceneViewTest::shownViewTest() {

	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::First, SceneKind::Entity) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Ghost, SceneKind::Speech) == SceneShown::Ghost);
	// The letterbox alone has no scene camera: the hero's head in every view
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Ghost, SceneKind::None) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Observer, SceneKind::None) == SceneShown::First);
	// The observer watches every scene camera's scene
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Observer, SceneKind::Entity) == SceneShown::Observer);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Observer, SceneKind::Speech) == SceneShown::Observer);
	// The hybrid: the screen only for a camera entity's shot that wants it; a speak -c line is a dialogue
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Hybrid, SceneKind::Speech) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Hybrid, SceneKind::Speech, true) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Hybrid, SceneKind::Entity) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Hybrid, SceneKind::Entity, true) == SceneShown::Screen);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::Hybrid, SceneKind::None, true) == SceneShown::First);
	CPPUNIT_ASSERT(chooseShownView(SceneViewSetting::First, SceneKind::Entity, true) == SceneShown::First);

	CPPUNIT_ASSERT(showsHeroOutside(SceneShown::Ghost));
	CPPUNIT_ASSERT(showsHeroOutside(SceneShown::Observer));
	CPPUNIT_ASSERT(!showsHeroOutside(SceneShown::First));
	CPPUNIT_ASSERT(!showsHeroOutside(SceneShown::None));
	CPPUNIT_ASSERT(!showsHeroOutside(SceneShown::Screen));
	CPPUNIT_ASSERT(!letterboxInEyes(SceneShown::First));
	CPPUNIT_ASSERT(!letterboxInEyes(SceneShown::Ghost));
	CPPUNIT_ASSERT(!letterboxInEyes(SceneShown::Observer));
}

void SceneViewTest::hybridClassificationTest() {

	HybridShot shot;
	shot.heroHead = Vec3f(1000.f, -170.f, 2000.f);

	// A dialogue camera 3 m from the hero, in sight: first person
	shot.camera = shot.heroHead + Vec3f(300.f, 20.f, 0.f);
	CPPUNIT_ASSERT(!hybridWantsScreen(shot));
	// Just inside the far range: still near; just beyond it: a far place
	shot.camera = shot.heroHead + Vec3f(0.f, 0.f, HybridFarUnits - 1.f);
	CPPUNIT_ASSERT(!hybridWantsScreen(shot));
	shot.camera = shot.heroHead + Vec3f(0.f, 0.f, HybridFarUnits + 1.f);
	CPPUNIT_ASSERT(hybridWantsScreen(shot));
	// A vision ball's camera 20 m away
	shot.camera = shot.heroHead + Vec3f(1500.f, 200.f, -1250.f);
	CPPUNIT_ASSERT(hybridWantsScreen(shot));
	// Near, but behind a wall: another place
	shot.camera = shot.heroHead + Vec3f(300.f, 0.f, 0.f);
	shot.lineOfSight = false;
	CPPUNIT_ASSERT(hybridWantsScreen(shot));
	// Near and in sight, but the hero's body played by a script (dragged in the prison intro)
	shot.lineOfSight = true;
	shot.heroScripted = true;
	CPPUNIT_ASSERT(hybridWantsScreen(shot));
	// The threshold is the caller's to tune
	shot.heroScripted = false;
	CPPUNIT_ASSERT(hybridWantsScreen(shot, 250.f));
}

namespace {

//! An open hall: a floor at y = 0 everywhere, walls at x = \a wallLow and x = \a wallHigh (none when 0)
ObserverWorld hall(float wallLow = 0.f, float wallHigh = 0.f) {
	ObserverWorld world;
	world.freeDistance = [wallLow, wallHigh](const Vec3f & from, const Vec3f & to) {
		const float length = glm::distance(from, to);
		const float dx = to.x - from.x;
		float wall = 0.f;
		if(wallHigh != 0.f && dx > 0.f && to.x > wallHigh) {
			wall = wallHigh;
		} else if(wallLow != 0.f && dx < 0.f && to.x < wallLow) {
			wall = wallLow;
		} else {
			return length;
		}
		return length * (wall - from.x) / dx;
	};
	world.floorBelow = [](const Vec3f &) -> std::optional<float> { return 0.f; };
	return world;
}

//! The hero and an NPC 2 m apart along z, both heads 1.6 m up, the game's camera on the +x side
ObserverInput twoHeads() {
	ObserverInput in;
	in.heroHead = Vec3f(1000.f, -160.f, 2000.f);
	in.otherHead = Vec3f(1000.f, -160.f, 2200.f);
	in.gameCamera = Vec3f(1200.f, -150.f, 2100.f);
	in.eyeHeight = 165.f;
	return in;
}

} // anonymous namespace

void SceneViewTest::observerPlacementTest() {

	// Open ground: beside the middle of the line, 1.5 m away (0.75 of 2 m is less than the minimum), on the
	// game camera's side, the player's eye above the floor, looking at the middle (-x is yaw 90)
	ObserverPlace place = placeObserver(twoHeads(), hall());
	CPPUNIT_ASSERT(same(place.pos, Vec3f(1150.f, -165.f, 2100.f)));
	CPPUNIT_ASSERT(std::abs(place.yaw - 90.f) < SvEps);
	CPPUNIT_ASSERT(std::abs(yawTowards(Vec3f(0.f), Vec3f(0.f, 0.f, 10.f))) < SvEps);

	// The game's camera on the other side: that side
	ObserverInput in = twoHeads();
	in.gameCamera.x = 800.f;
	place = placeObserver(in, hall());
	CPPUNIT_ASSERT(same(place.pos, Vec3f(850.f, -165.f, 2100.f)));
	CPPUNIT_ASSERT(std::abs(place.yaw - 270.f) < SvEps);

	// Heads farther apart: 0.75 of their distance, at most 3 m
	in = twoHeads();
	in.otherHead.z = 2000.f + 320.f;
	place = placeObserver(in, hall());
	CPPUNIT_ASSERT(same(place.pos, Vec3f(1240.f, -165.f, 2160.f)));
	in.otherHead.z = 2000.f + 1000.f;
	place = placeObserver(in, hall());
	CPPUNIT_ASSERT(same(place.pos, Vec3f(1300.f, -165.f, 2500.f)));

	// A wall 1 m away on the camera's side: the open side, whole
	place = placeObserver(twoHeads(), hall(0.f, 1100.f));
	CPPUNIT_ASSERT(same(place.pos, Vec3f(850.f, -165.f, 2100.f)));

	// Walls on both sides, 1 m and 1.2 m away: the roomier side, 40 cm short of its wall
	place = placeObserver(twoHeads(), hall(880.f, 1100.f));
	CPPUNIT_ASSERT(same(place.pos, Vec3f(920.f, -165.f, 2100.f)));

	// The floor drops away beyond 1 m: the point moves back to where there is one, then up from it
	ObserverWorld ledge = hall();
	ledge.floorBelow = [](const Vec3f & pos) -> std::optional<float> {
		if(pos.x > 1100.f) {
			return std::nullopt;
		}
		return -20.f;
	};
	place = placeObserver(twoHeads(), ledge);
	CPPUNIT_ASSERT(same(place.pos, Vec3f(1075.f, -185.f, 2100.f)));

	// No other head (the speaker is the hero): the line runs along the hero's facing (yaw 90 is -x)
	in = twoHeads();
	in.otherHead = in.heroHead;
	in.heroYaw = 90.f;
	in.gameCamera = Vec3f(975.f, -150.f, 2500.f);
	place = placeObserver(in, hall());
	CPPUNIT_ASSERT(same(place.pos, Vec3f(975.f, -165.f, 2150.f)));
	CPPUNIT_ASSERT(std::abs(place.yaw - 180.f) < SvEps);
}

void SceneViewTest::observerHoldsSceneTest() {

	SceneDirector director;
	SceneInput in = standing();
	in.playerYaw = 30.f;
	in.headYaw = 10.f;
	director.update(in);

	// The scene starts: the observer is placed once, at black, and the eyes look at the middle of the heads
	int placed = 0;
	ObserverPlace spot;
	spot.pos = Vec3f(1400.f, -165.f, 2300.f);
	spot.yaw = 200.f;
	SceneInput scene = sceneOf(in, SceneKind::Entity, 5, SceneViewSetting::Observer);
	scene.enginePos = Vec3f(1500.f, -100.f, 2600.f);
	scene.placeObserver = [&placed, spot]() { placed++; return spot; };
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, in.firstPos, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT(out.shown == SceneShown::Observer);
	CPPUNIT_ASSERT_EQUAL(1, placed);
	CPPUNIT_ASSERT(same(out.pos, spot.pos));
	// Where the player looks now (the hero's yaw plus the head's) is the observer's yaw: 200 - 10 - 30
	CPPUNIT_ASSERT(std::abs(out.yawDelta - 160.f) < SvEps);

	// The scene's camera entities glide and change, a line is spoken: the observer stays, no cut, placed once
	for(int i = 0; i < 120; i++) {
		scene.shot = (i < 60) ? 5 : 6;
		scene.enginePos.x += 3.f;
		scene.firstPos.z += 1.f;
		out = director.update(scene);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(same(out.pos, spot.pos));
		CPPUNIT_ASSERT(std::abs(out.yawDelta - 160.f) < SvEps);
	}
	CPPUNIT_ASSERT_EQUAL(0.f, out.fade);
	CPPUNIT_ASSERT_EQUAL(1, placed);

	// The scene ends: held until black, then the game's view
	SceneInput after = standing();
	out = untilCut(director, after, spot.pos, held, frames);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.ended);
	CPPUNIT_ASSERT(out.shown == SceneShown::None);

	// The next scene places the observer anew
	out = untilCut(director, scene, after.firstPos, held, frames);
	CPPUNIT_ASSERT(out.shown == SceneShown::Observer);
	CPPUNIT_ASSERT_EQUAL(2, placed);
}

void SceneViewTest::hybridScreenTest() {

	SceneDirector director;
	SceneInput in = standing();
	in.playerYaw = 40.f;
	director.update(in);

	// A dialogue camera near the hero: first person
	SceneInput scene = sceneOf(in, SceneKind::Entity, 8, SceneViewSetting::Hybrid);
	settle(director, scene);
	CPPUNIT_ASSERT(director.shown() == SceneShown::First);

	// Another camera entity wants the screen (far, or the hero's scripted body): cut behind the fade, the eyes'
	// camera stays at the hero's head and its yaw while the game's camera plays on the screen
	scene.shot = 9;
	scene.hybridScreen = true;
	scene.enginePos = Vec3f(4000.f, -500.f, 6000.f);
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, in.firstPos, held, frames);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT_EQUAL(std::string("camera"), std::string(out.cutReason));
	CPPUNIT_ASSERT(out.shown == SceneShown::Screen);
	CPPUNIT_ASSERT(!showsHeroOutside(out.shown));
	CPPUNIT_ASSERT(same(out.pos, in.firstPos));

	// The shot keeps the screen when it stops wanting it (the hero's animation ended, the camera glided
	// nearer); the hero turned by the script, a camera entity after it on the screen too: no cut, the eyes'
	// camera and its yaw stay
	for(int i = 0; i < 120; i++) {
		scene.hybridScreen = (i >= 60);
		scene.shot = (i < 60) ? 9 : 10;
		scene.playerYaw = 40.f + 2.f * float(i);
		scene.enginePos.x -= 20.f;
		out = director.update(scene);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(out.shown == SceneShown::Screen);
		CPPUNIT_ASSERT(same(out.pos, in.firstPos));
		CPPUNIT_ASSERT(yawDistance(scene.playerYaw + out.yawDelta, 40.f) < SvEps);
	}

	// A camera near the hero again: back to first person behind the fade, the hero's yaw now
	scene.shot = 11;
	scene.hybridScreen = false;
	out = untilCut(director, scene, in.firstPos, held, frames);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT(out.shown == SceneShown::First);
	CPPUNIT_ASSERT_EQUAL(0.f, out.yawDelta);

	// A speak -c line is a dialogue: first person even if a camera entity's rule would say screen
	SceneInput line = sceneOf(in, SceneKind::Speech, 12, SceneViewSetting::Hybrid);
	line.hybridScreen = true;
	out = untilCut(director, line, in.firstPos, held, frames);
	CPPUNIT_ASSERT(out.shown == SceneShown::First);
}

void SceneViewTest::blindTest() {

	SceneDirector director;
	director.update(standing());

	// A new game's letterbox starts with the hero off the level (no floor under his eye): the hybrid's first
	// person stays black after the start cut
	SceneInput bars = sceneOf(standing(), SceneKind::None, 0, SceneViewSetting::Hybrid);
	bars.firstOnFloor = false;
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, bars, bars.firstPos, held, frames);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(out.shown == SceneShown::First);
	for(int i = 0; i < 180; i++) {
		out = director.update(bars);
		CPPUNIT_ASSERT(out.fade >= BehindFade);
	}

	// The intro's script places him and its camera starts: the cut is taken at once, then the world fades in
	SceneInput camera = sceneOf(standing(), SceneKind::Entity, 4, SceneViewSetting::Hybrid);
	camera.hybridScreen = true;
	out = director.update(camera);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.shown == SceneShown::Screen);
	settle(director, camera);
	CPPUNIT_ASSERT_EQUAL(0.f, director.fade());

	// Every view is held black while its eye has no floor under it (the owner, #29): first person...
	SceneDirector first;
	first.update(standing());
	SceneInput firstBars = sceneOf(standing(), SceneKind::None, 0, SceneViewSetting::First);
	firstBars.firstOnFloor = false;
	settle(first, firstBars);
	CPPUNIT_ASSERT(first.shown() == SceneShown::First);
	CPPUNIT_ASSERT(first.fade() >= BehindFade);
	firstBars.firstOnFloor = true;
	settle(first, firstBars);
	CPPUNIT_ASSERT_EQUAL(0.f, first.fade());

	// ...a ghost's shot and the observer's point
	for(SceneViewSetting setting : { SceneViewSetting::Ghost, SceneViewSetting::Observer }) {
		SceneDirector held;
		held.update(standing());
		SceneInput shot = sceneOf(standing(), SceneKind::Entity, 7, setting);
		shot.hasFloor = [](const Vec3f &) { return false; };
		settle(held, shot);
		CPPUNIT_ASSERT(held.shown() != SceneShown::None);
		CPPUNIT_ASSERT(held.fade() >= BehindFade);
		shot.hasFloor = [](const Vec3f &) { return true; };
		settle(held, shot);
		CPPUNIT_ASSERT_EQUAL(0.f, held.fade());
	}
}

void SceneViewTest::firstLowTest() {

	// The hero lies 10 cm over the floor (dragged by the prison intro's goblins), first person in a camera entity's shot
	SceneInput lying = sceneOf(standing(), SceneKind::Entity, 3, SceneViewSetting::First);
	lying.firstPos.y = -10.f;
	SceneInput up = lying;
	up.firstPos.y = -170.f;

	// clamp: the eye raised to the minimum, nothing black
	{
		SceneDirector director;
		director.update(standing());
		lying.firstLow = SceneFirstLow::Clamp;
		SceneOutput out;
		for(int i = 0; i < 200; i++) {
			out = director.update(lying);
		}
		CPPUNIT_ASSERT(out.shown == SceneShown::First);
		CPPUNIT_ASSERT(!out.blind);
		CPPUNIT_ASSERT_EQUAL(0.f, out.fade);
		CPPUNIT_ASSERT(std::abs(out.pos.y + GhostMinAboveFloor) < SvEps);
	}

	// black: held black while the eye is low, the world back once he stands
	{
		SceneDirector director;
		director.update(standing());
		lying.firstLow = SceneFirstLow::Black;
		up.firstLow = SceneFirstLow::Black;
		settle(director, up);
		// He goes down fast, 8 cm a frame: no frame the eyes see shows an eye lower than the minimum
		SceneOutput out;
		SceneInput falling = up;
		for(int i = 0; i < 200; i++) {
			falling.firstPos.y = std::min(-10.f, -170.f + 8.f * float(i));
			out = director.update(falling);
			if(out.fade < BehindFade) {
				CPPUNIT_ASSERT(-out.pos.y >= GhostMinAboveFloor - SvEps);
			}
		}
		// Barely up again (under the rise) stays black
		SceneInput barely = up;
		barely.firstPos.y = -(GhostMinAboveFloor + FirstLowRise / 2.f);
		for(int i = 0; i < 100; i++) {
			CPPUNIT_ASSERT(director.update(barely).fade >= BehindFade);
		}
		CPPUNIT_ASSERT(out.shown == SceneShown::First);
		CPPUNIT_ASSERT(out.blind);
		CPPUNIT_ASSERT(out.fade >= BehindFade);
		settle(director, up);
		CPPUNIT_ASSERT_EQUAL(0.f, director.fade());
	}

	// screen: the scene goes on the screen and stays there until it ends, also once he stands
	{
		SceneDirector director;
		director.update(standing());
		lying.firstLow = SceneFirstLow::Screen;
		up.firstLow = SceneFirstLow::Screen;
		settle(director, lying);
		CPPUNIT_ASSERT(director.shown() == SceneShown::Screen);
		settle(director, up);
		CPPUNIT_ASSERT(director.shown() == SceneShown::Screen);
		SceneInput after = standing();
		after.firstLow = SceneFirstLow::Screen;
		settle(director, after);
		CPPUNIT_ASSERT(director.shown() == SceneShown::None);
	}

	CPPUNIT_ASSERT(parseSceneFirstLow("black") == SceneFirstLow::Black);
	CPPUNIT_ASSERT(parseSceneFirstLow("screen") == SceneFirstLow::Screen);
	CPPUNIT_ASSERT(!parseSceneFirstLow("floor"));
	CPPUNIT_ASSERT_EQUAL(std::string("clamp"), std::string(sceneFirstLowName(DefaultSceneFirstLow)));
}

void SceneViewTest::scriptTurnTest() {

	// A ghost's and the observer's shot: a script turning the hero (teleport -pa 180) does not turn the eyes; the
	// stick does
	for(SceneViewSetting setting : { SceneViewSetting::Ghost, SceneViewSetting::Observer }) {
		SceneDirector director;
		director.update(standing());
		SceneInput shot = sceneOf(standing(), SceneKind::Entity, 9, setting);
		settle(director, shot);
		SceneOutput out = director.update(shot);
		const float eyes = shot.playerYaw + out.yawDelta;
		shot.playerYaw = 180.f;
		out = director.update(shot);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(yawDistance(shot.playerYaw + out.yawDelta, eyes) < SvEps);
		// The stick turns 30 degrees right: the hero's yaw goes down, the eyes with it
		shot.stickTurned = 30.f;
		shot.playerYaw = 150.f;
		out = director.update(shot);
		CPPUNIT_ASSERT(yawDistance(shot.playerYaw + out.yawDelta, eyes - 30.f) < SvEps);
	}
}

void SceneViewTest::firstYawJumpTest() {

	// First person in a scene: a script snapping the hero round (teleport -a) is a cut behind the fade, the eyes keep
	// the old yaw until black; the stick's own snap turn is never one
	SceneDirector director;
	director.update(standing());
	SceneInput scene = sceneOf(standing(), SceneKind::Entity, 4, SceneViewSetting::First);
	settle(director, scene);
	SceneOutput out = director.update(scene);
	CPPUNIT_ASSERT(!out.cut);

	SceneInput stick = scene;
	stick.stickTurned = 45.f;
	stick.playerYaw = -45.f;
	out = director.update(stick);
	CPPUNIT_ASSERT(!out.cut);
	CPPUNIT_ASSERT_EQUAL(0.f, director.fade());
	settle(director, stick);

	SceneInput turned = stick;
	turned.playerYaw = 135.f;
	bool held = false;
	int frames = 0;
	out = untilCut(director, turned, director.update(stick).pos, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT(frames > 1);
}

void SceneViewTest::fadeAtBlackTest() {

	// A fade asked for again at black, on the last frame it holds black: it stays black for that frame, so the
	// cut waiting for it is taken (a cut asked for there once waited for ever, #29)
	SceneFade fade;
	fade.request(0.1f, 0.1f, 0);
	fade.update(1.f);
	CPPUNIT_ASSERT(fade.isBlack());
	fade.request(0.1f, 0.1f, 0);
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.isBlack());
	fade.update(SvFrame);
	CPPUNIT_ASSERT(!fade.isBlack());
}

void SceneViewTest::heldYawTest() {

	SceneDirector director;
	SceneInput in = standing();
	in.playerYaw = 10.f;
	director.update(in);

	// The scene starts and its script turns the hero in the same frame (teleport -a): the eyes keep the old
	// yaw until black, then take the hero's
	SceneInput scene = sceneOf(in, SceneKind::Entity, 2, SceneViewSetting::First);
	scene.playerYaw = 190.f;
	SceneOutput out;
	for(int i = 0; i < 100; i++) {
		out = director.update(scene);
		if(out.cut) {
			break;
		}
		CPPUNIT_ASSERT(yawDistance(scene.playerYaw + out.yawDelta, 10.f) < SvEps);
	}
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT_EQUAL(0.f, out.yawDelta);
}

void SceneViewTest::fadeTimingTest() {

	SceneFade fade;
	CPPUNIT_ASSERT(!fade.busy());
	fade.request(0.15f, 0.25f, 2);

	// Out in 0.15 s: black on the 14th frame at 90 Hz, not before
	for(int i = 1; i < 14; i++) {
		fade.update(SvFrame);
		CPPUNIT_ASSERT(!fade.isBlack());
	}
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.isBlack());

	// Black two more frames, then in
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.isBlack());
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.isBlack());
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.value() < BehindFade && fade.value() > 0.9f);

	// Back in 0.25 s
	int frames = 1;
	while(fade.busy() && frames < 100) {
		fade.update(SvFrame);
		frames++;
	}
	CPPUNIT_ASSERT_EQUAL(0.f, fade.value());
	CPPUNIT_ASSERT(frames >= 22 && frames <= 24);

	// A second request while black keeps it black for the black frames from a hold
	fade.request(0.1f, 0.1f, 2);
	for(int i = 0; i < 10 && !fade.isBlack(); i++) {
		fade.update(SvFrame);
	}
	CPPUNIT_ASSERT(fade.isBlack());
	fade.update(SvFrame);
	fade.holdBlack();
	fade.update(SvFrame);
	fade.update(SvFrame);
	CPPUNIT_ASSERT(fade.isBlack());
	fade.update(SvFrame);
	CPPUNIT_ASSERT(!fade.isBlack());
}

void SceneViewTest::floorClampTest() {

	// 0.5 m above a floor at y = 300 (y down): raised to 1.2 m
	Vec3f low = clampAboveFloor(Vec3f(10.f, 250.f, 20.f), 300.f, true);
	CPPUNIT_ASSERT(same(low, Vec3f(10.f, 300.f - GhostMinAboveFloor, 20.f)));
	CPPUNIT_ASSERT((300.f - low.y) / 100.f >= 1.2f);

	// High enough, or no floor found: unchanged
	CPPUNIT_ASSERT(same(clampAboveFloor(Vec3f(0.f, 100.f, 0.f), 300.f, true), Vec3f(0.f, 100.f, 0.f)));
	CPPUNIT_ASSERT(same(clampAboveFloor(Vec3f(0.f, 250.f, 0.f), 300.f, false), Vec3f(0.f, 250.f, 0.f)));

	CPPUNIT_ASSERT(std::abs(yawDistance(350.f, 10.f) - 20.f) < SvEps);
	CPPUNIT_ASSERT(std::abs(yawDistance(10.f, 190.f) - 180.f) < SvEps);
}

void SceneViewTest::firstStartHoldsUntilBlackTest() {

	SceneDirector director;
	SceneInput in = standing();

	// No scene: the game's own view, followed, nothing driven
	for(int i = 0; i < 5; i++) {
		SceneOutput out = director.update(in);
		CPPUNIT_ASSERT(!out.drive);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(out.shown == SceneShown::None);
		CPPUNIT_ASSERT(same(out.pos, in.firstPos));
	}
	const Vec3f before = in.firstPos;

	// The scene starts with the hero teleported 3 m and a camera entity far away: the eyes keep the old head
	// until black, then take the hero's new head
	SceneInput scene = sceneOf(in, SceneKind::Entity, 7, SceneViewSetting::First);
	scene.firstPos = before + Vec3f(300.f, 0.f, 0.f);
	scene.enginePos = Vec3f(0.f, -50.f, 0.f);
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, before, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT_EQUAL(14, frames);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(!out.ended);
	CPPUNIT_ASSERT_EQUAL(std::string("start"), std::string(out.cutReason));
	CPPUNIT_ASSERT(out.shown == SceneShown::First);
	CPPUNIT_ASSERT(out.kind == SceneKind::Entity);
	CPPUNIT_ASSERT(out.drive);
	CPPUNIT_ASSERT(same(out.pos, scene.firstPos));
	CPPUNIT_ASSERT_EQUAL(0.f, out.yawDelta);

	// Then the world fades in on the new head, no more cuts
	for(int i = 0; i < 40; i++) {
		out = director.update(scene);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(same(out.pos, scene.firstPos));
	}
	CPPUNIT_ASSERT_EQUAL(0.f, out.fade);
}

void SceneViewTest::firstTeleportInSceneTest() {

	SceneDirector director;
	SceneInput scene = sceneOf(standing(), SceneKind::Entity, 7, SceneViewSetting::First);
	settle(director, scene);
	CPPUNIT_ASSERT(director.shown() == SceneShown::First);

	// A teleport within the scene: held, a cut behind the fade
	const Vec3f before = scene.firstPos;
	scene.firstPos += Vec3f(0.f, 0.f, 200.f);
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, before, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT_EQUAL(std::string("jump"), std::string(out.cutReason));
	CPPUNIT_ASSERT(!out.started);
	CPPUNIT_ASSERT(!out.ended);
	CPPUNIT_ASSERT(same(out.pos, scene.firstPos));
}

void SceneViewTest::firstFollowsHeroTest() {

	SceneDirector director;
	SceneInput scene = sceneOf(standing(), SceneKind::Speech, 1, SceneViewSetting::First);
	settle(director, scene);

	// The hero's head moving a little (the script walks him, the body steps after the head): followed
	for(int i = 0; i < 30; i++) {
		scene.firstPos.x += 2.f;
		SceneOutput out = director.update(scene);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(same(out.pos, scene.firstPos));
		CPPUNIT_ASSERT_EQUAL(0.f, out.fade);
	}
}

void SceneViewTest::ghostHoldsShotTest() {

	SceneDirector director;
	SceneInput in = standing();
	in.playerYaw = 90.f;
	in.headYaw = 20.f;
	director.update(in);

	// A camera entity 0.3 m above the floor, dollying and panning
	SceneInput scene = sceneOf(in, SceneKind::Entity, 3, SceneViewSetting::Ghost);
	scene.enginePos = Vec3f(500.f, -30.f, 500.f);
	scene.engineYaw = 180.f;
	SceneOutput out;
	int frames = 0;
	for(frames = 1; frames < 100; frames++) {
		out = director.update(scene);
		if(out.cut) {
			break;
		}
		scene.enginePos.x += 1.f;
		scene.engineYaw += 0.2f;
	}
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(out.shown == SceneShown::Ghost);
	CPPUNIT_ASSERT(showsHeroOutside(out.shown));
	// The shot's first frame, raised to 1.2 m above the floor
	CPPUNIT_ASSERT(same(out.pos, Vec3f(500.f, -GhostMinAboveFloor, 500.f)));
	// Where the player looks now (the hero's yaw plus the head's) is where the camera looked: 180 - 20 - 90
	CPPUNIT_ASSERT(std::abs(out.yawDelta - 70.f) < SvEps);

	// The dolly and the pan go on: the shot stays where it began
	for(int i = 0; i < 60; i++) {
		scene.enginePos.x += 1.f;
		scene.engineYaw += 0.2f;
		out = director.update(scene);
		CPPUNIT_ASSERT(!out.cut);
		CPPUNIT_ASSERT(same(out.pos, Vec3f(500.f, -GhostMinAboveFloor, 500.f)));
		CPPUNIT_ASSERT(std::abs(out.yawDelta - 70.f) < SvEps);
	}
}

void SceneViewTest::ghostJumpIsNewShotTest() {

	SceneDirector director;
	SceneInput scene = sceneOf(standing(), SceneKind::Entity, 3, SceneViewSetting::Ghost);
	scene.enginePos = Vec3f(500.f, -200.f, 500.f);
	settle(director, scene);
	CPPUNIT_ASSERT(director.shown() == SceneShown::Ghost);

	// The same camera entity put elsewhere in one frame: a new shot, cut behind the fade
	const Vec3f before = scene.enginePos;
	scene.enginePos = Vec3f(900.f, -200.f, 500.f);
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, before, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(held);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT_EQUAL(std::string("camera"), std::string(out.cutReason));
	CPPUNIT_ASSERT(same(out.pos, scene.enginePos));
}

void SceneViewTest::speechLineCutTest() {

	SceneDirector director;
	SceneInput scene = sceneOf(standing(), SceneKind::Speech, 1, SceneViewSetting::First);
	settle(director, scene);

	// The next speak -c line: a cut behind the fade (SPEC.md), though the hero's head stays
	scene.shot = 2;
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, scene, scene.firstPos, held, frames);
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
	CPPUNIT_ASSERT_EQUAL(std::string("line"), std::string(out.cutReason));
	CPPUNIT_ASSERT(!out.started);
}

void SceneViewTest::sceneEndTest() {

	SceneDirector director;
	SceneInput scene = sceneOf(standing(), SceneKind::Entity, 3, SceneViewSetting::Ghost);
	scene.enginePos = Vec3f(500.f, -200.f, 500.f);
	settle(director, scene);

	// The game gives the camera back: the ghost shot is held and driven until black, then the game's view
	SceneInput after = standing();
	SceneOutput out;
	for(int i = 0; i < 100; i++) {
		out = director.update(after);
		if(out.cut) {
			break;
		}
		CPPUNIT_ASSERT(out.drive);
		CPPUNIT_ASSERT(out.shown == SceneShown::Ghost);
		CPPUNIT_ASSERT(same(out.pos, scene.enginePos));
	}
	CPPUNIT_ASSERT(out.cut);
	CPPUNIT_ASSERT(out.ended);
	CPPUNIT_ASSERT(!out.started);
	CPPUNIT_ASSERT(out.endedKind == SceneKind::Entity);
	CPPUNIT_ASSERT_EQUAL(std::string("end"), std::string(out.cutReason));
	CPPUNIT_ASSERT(out.shown == SceneShown::None);
	CPPUNIT_ASSERT(!out.drive);
	CPPUNIT_ASSERT(same(out.pos, after.firstPos));
	CPPUNIT_ASSERT_EQUAL(0.f, out.yawDelta);
}

void SceneViewTest::letterboxOnlyTest() {

	SceneDirector director;
	director.update(standing());

	// The letterbox first, the camera entity a moment later: two starts, the first one's end between
	SceneInput bars = sceneOf(standing(), SceneKind::None, 0, SceneViewSetting::Ghost);
	bool held = false;
	int frames = 0;
	SceneOutput out = untilCut(director, bars, bars.firstPos, held, frames);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(out.shown == SceneShown::First);
	CPPUNIT_ASSERT(out.kind == SceneKind::None);

	SceneInput camera = sceneOf(standing(), SceneKind::Entity, 4, SceneViewSetting::Ghost);
	camera.enginePos = Vec3f(0.f, -200.f, 0.f);
	out = untilCut(director, camera, bars.firstPos, held, frames);
	CPPUNIT_ASSERT(out.started);
	CPPUNIT_ASSERT(out.ended);
	CPPUNIT_ASSERT(out.endedKind == SceneKind::None);
	CPPUNIT_ASSERT(out.kind == SceneKind::Entity);
	CPPUNIT_ASSERT(out.shown == SceneShown::Ghost);
	CPPUNIT_ASSERT(out.fade >= BehindFade);
}

void SceneViewTest::lookAtTest() {

	SceneDirector director;
	SceneInput in = standing();
	director.update(in);

	// ignore: nothing to apply, no fade
	CPPUNIT_ASSERT(!director.lookAt(SceneLookAt::Ignore));
	for(int i = 0; i < 30; i++) {
		SceneOutput out = director.update(in);
		CPPUNIT_ASSERT(!out.applyLookAt);
		CPPUNIT_ASSERT_EQUAL(0.f, out.fade);
	}

	// fade: applied once, at black (0.1 s: the 9th or, rounded, the 10th frame), about 0.2 s in all
	CPPUNIT_ASSERT(director.lookAt(SceneLookAt::Fade));
	int applied = 0;
	int appliedFrame = 0;
	int frames = 0;
	for(frames = 1; frames < 100; frames++) {
		SceneOutput out = director.update(in);
		if(out.applyLookAt) {
			applied++;
			appliedFrame = frames;
			CPPUNIT_ASSERT(out.fade >= BehindFade);
		}
		CPPUNIT_ASSERT(!out.cut);
		if(applied && out.fade == 0.f) {
			break;
		}
	}
	CPPUNIT_ASSERT_EQUAL(1, applied);
	CPPUNIT_ASSERT(appliedFrame >= 9 && appliedFrame <= 10);
	CPPUNIT_ASSERT(frames <= appliedFrame + 2 + 10 + 1);
}

void SceneViewTest::meterTest() {

	SceneMeter meter;
	CPPUNIT_ASSERT_EQUAL(-1.f, meter.minAboveFloorM());
	CPPUNIT_ASSERT_EQUAL(u64(0), meter.frames());

	SceneMeterFrame frame;
	frame.active = true;
	frame.shown = SceneShown::First;
	frame.aboveFloor = 1.7f;
	frame.pos = Vec3f(0.f, -170.f, 0.f);

	// Steps of 2 cm and 0.5 degrees
	for(int i = 0; i < 10; i++) {
		meter.add(frame);
		frame.pos.x += 2.f;
		frame.yaw += 0.5f;
	}
	CPPUNIT_ASSERT_EQUAL(u64(10), meter.frames());
	CPPUNIT_ASSERT_EQUAL(u64(10), meter.sceneFrames());
	CPPUNIT_ASSERT(std::abs(meter.maxStepCm() - 2.f) < SvEps);
	CPPUNIT_ASSERT(std::abs(meter.maxYawStepDeg() - 0.5f) < SvEps);
	CPPUNIT_ASSERT(std::abs(meter.minAboveFloorM() - 1.7f) < SvEps);

	// Behind the fade: a 3 m move is neither a step nor a height; the next seen frame starts a new comparison
	frame.fade = 1.f;
	frame.pos.x += 300.f;
	frame.aboveFloor = 0.3f;
	meter.add(frame);
	frame.fade = 0.5f;
	frame.pos.x += 1.f;
	frame.aboveFloor = 1.5f;
	meter.add(frame);
	CPPUNIT_ASSERT(std::abs(meter.maxStepCm() - 2.f) < SvEps);
	CPPUNIT_ASSERT(std::abs(meter.minAboveFloorM() - 1.5f) < SvEps);

	// The player's own stick turn is no yaw step; the game's view (no scene) is not measured
	frame.ownTurn = true;
	frame.yaw += 45.f;
	meter.add(frame);
	frame.ownTurn = false;
	CPPUNIT_ASSERT(std::abs(meter.maxYawStepDeg() - 0.5f) < SvEps);
	frame.active = false;
	frame.shown = SceneShown::None;
	frame.aboveFloor = 0.1f;
	frame.pos.x += 20.f;
	meter.add(frame);
	CPPUNIT_ASSERT(std::abs(meter.minAboveFloorM() - 1.5f) < SvEps);
	CPPUNIT_ASSERT(std::abs(meter.maxStepCm() - 2.f) < SvEps);
	CPPUNIT_ASSERT_EQUAL(u64(13), meter.sceneFrames());
	CPPUNIT_ASSERT_EQUAL(u64(14), meter.frames());

	// The screen is counted, not measured; letterbox frames are counted
	frame.shown = SceneShown::Screen;
	meter.letterboxDrawn();
	meter.add(frame);
	CPPUNIT_ASSERT_EQUAL(u64(1), meter.screenFrames());
	CPPUNIT_ASSERT_EQUAL(u64(1), meter.letterboxFrames());

	meter.lookAtSeen();
	meter.lookAtFaded();
	CPPUNIT_ASSERT_EQUAL(u64(1), meter.lookats());
	CPPUNIT_ASSERT_EQUAL(u64(1), meter.fadedLookats());

	meter.reset();
	CPPUNIT_ASSERT_EQUAL(u64(0), meter.frames());
	CPPUNIT_ASSERT_EQUAL(u64(0), meter.lookats());
	CPPUNIT_ASSERT_EQUAL(-1.f, meter.minAboveFloorM());
}

void SceneViewTest::meterCutsTest() {

	SceneMeter meter;
	SceneMeterFrame frame;
	frame.shown = SceneShown::First;
	frame.aboveFloor = 1.7f;
	meter.add(frame);

	// A shot change behind the fade, then a jump of 60 cm behind it: two faded cuts, one per frame
	frame.fade = 1.f;
	frame.shotChanged = true;
	frame.pos.x += 60.f;
	meter.add(frame);
	frame.shotChanged = false;
	frame.pos.x += 60.f;
	meter.add(frame);
	CPPUNIT_ASSERT_EQUAL(u64(2), meter.cuts());
	CPPUNIT_ASSERT_EQUAL(u64(2), meter.fadedCuts());

	// An unfaded yaw cut of 15 degrees, an unfaded jump - and the player's own snap, which is none
	frame.fade = 0.f;
	frame.yaw += 15.f;
	meter.add(frame);
	frame.pos.z += 51.f;
	meter.add(frame);
	frame.ownTurn = true;
	frame.yaw += 45.f;
	meter.add(frame);
	CPPUNIT_ASSERT_EQUAL(u64(4), meter.cuts());
	CPPUNIT_ASSERT_EQUAL(u64(2), meter.fadedCuts());
}
