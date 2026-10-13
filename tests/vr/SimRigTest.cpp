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

#include "vr/SimRigTest.h"

#include <cmath>
#include <string>

#include <glm/gtc/constants.hpp>

CppUnit::AutoRegisterSuite<SimRigTest> g_registerSimRigTest;

namespace {

const double RigEpsilon = 1e-4;

bool rigStartsWith(const std::string & text, const std::string & prefix) {
	return text.rfind(prefix, 0) == 0;
}

} // anonymous namespace

void SimRigTest::testEyeSeparationIsTheIpd() {
	vr::SimView view;
	view.ipd = 0.064f;
	std::array<vr::SimEye, 2> eyes = vr::simEyes(view, vr::SimPose());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.064, glm::length(eyes[1].position - eyes[0].position), RigEpsilon);
	// The left eye is on the left
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.032, eyes[0].position.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.032, eyes[1].position.x, RigEpsilon);
}

void SimRigTest::testYawRotatesBothEyes() {
	vr::SimView view;
	vr::SimPose head;
	head.angles = Vec3f(25.f, 0.f, 0.f);
	std::array<vr::SimEye, 2> eyes = vr::simEyes(view, head);
	for(const vr::SimEye & eye : eyes) {
		CPPUNIT_ASSERT_DOUBLES_EQUAL(25.0, vr::simAngles(eye.orientation).x, 1e-3);
	}
	// The eyes turn around the head's center: the separation stays, the middle stays
	CPPUNIT_ASSERT_DOUBLES_EQUAL(view.ipd, glm::length(eyes[1].position - eyes[0].position), RigEpsilon);
	Vec3f middle = (eyes[0].position + eyes[1].position) * 0.5f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, glm::length(middle), RigEpsilon);
	// The line between the eyes turned with the head
	Vec3f across = glm::normalize(eyes[1].position - eyes[0].position);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(std::cos(glm::radians(25.f)), across.x, RigEpsilon);
}

void SimRigTest::testPositiveYawTurnsLeft() {
	vr::SimPose head;
	head.angles = Vec3f(90.f, 0.f, 0.f);
	Vec3f forward = head.orientation() * Vec3f(0.f, 0.f, -1.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.0, forward.x, RigEpsilon);
	head.angles = Vec3f(0.f, 30.f, 0.f);
	forward = head.orientation() * Vec3f(0.f, 0.f, -1.f);
	CPPUNIT_ASSERT(forward.y > 0.4f); // pitch + looks up
}

void SimRigTest::testHeadOffsetMovesBothEyes() {
	vr::SimView view;
	vr::SimPose head;
	std::array<vr::SimEye, 2> before = vr::simEyes(view, head);
	head.position = Vec3f(0.1f, -0.3f, 0.25f);
	std::array<vr::SimEye, 2> after = vr::simEyes(view, head);
	for(size_t i = 0; i < 2; i++) {
		Vec3f moved = after[i].position - before[i].position;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, moved.x, RigEpsilon);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.3, moved.y, RigEpsilon);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, moved.z, RigEpsilon);
	}
}

void SimRigTest::testEyesSeenFromTheHead() {
	vr::SimView view;
	vr::SimPose head;
	head.position = Vec3f(1.f, 2.f, 3.f);
	head.angles = Vec3f(40.f, -10.f, 5.f);
	std::array<vr::SimEye, 2> eyes = vr::simEyes(view, head);
	std::array<vr::SimEye, 2> inHead = vr::simEyesInHead(view);
	for(size_t i = 0; i < 2; i++) {
		Vec3f position = eyes[i].position;
		glm::quat orientation = eyes[i].orientation;
		vr::poseInSpace(head.position, head.orientation(), position, orientation);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(inHead[i].position.x, position.x, RigEpsilon);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, position.y, RigEpsilon);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, position.z, RigEpsilon);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, std::abs(orientation.w), RigEpsilon);
	}
}

void SimRigTest::testAnglesRoundTrip() {
	vr::SimPose pose;
	pose.angles = Vec3f(-120.f, 35.f, -15.f);
	Vec3f angles = vr::simAngles(pose.orientation());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-120.0, angles.x, 1e-2);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(35.0, angles.y, 1e-2);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-15.0, angles.z, 1e-2);
}

void SimRigTest::testDefaultFovIsAsymmetricAndMirrored() {
	vr::SimView view;
	// Research row F-165: more to the outside than to the nose, mirrored between the eyes
	CPPUNIT_ASSERT(-view.fov[0].left > view.fov[0].right);
	CPPUNIT_ASSERT(view.fov[1].right > -view.fov[1].left);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-view.fov[0].left, view.fov[1].right, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(view.fov[0].right, -view.fov[1].left, RigEpsilon);
}

void SimRigTest::testSetupCommands() {
	vr::Simulation sim;
	std::string error = sim.load("# a comment\nrate 72\neye-size 800 900   # trailing comment\nipd 0.07\n"
	                             "fov left -50 40 45 -45\ntimeline session\nhead pos 0 0.1 0\n");
	CPPUNIT_ASSERT_EQUAL(std::string(), error);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(72.0, sim.view.rate, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(800, int(sim.view.eyeSize.x));
	CPPUNIT_ASSERT_EQUAL(900, int(sim.view.eyeSize.y));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.07, sim.view.ipd, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(-50.0), sim.view.fov[0].left, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(-45.0), sim.view.fov[0].down, RigEpsilon);
	CPPUNIT_ASSERT(sim.timeline == vr::SimTimeline::Session);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, sim.rig.head.position.y, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(size_t(0), sim.pending());
}

void SimRigTest::testTimedCommandRunsAtItsFrame() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("@10 head rot 25 0 0\n"));
	CPPUNIT_ASSERT_EQUAL(size_t(1), sim.pending());
	sim.advance(9);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.head.angles.x, RigEpsilon);
	sim.advance(10);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(25.0, sim.rig.head.angles.x, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(size_t(0), sim.pending());
}

void SimRigTest::testRampInterpolates() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("head pos 0 0 0\n@10-20 head pos 1 -1 0\n"));
	sim.advance(10);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.head.position.x, RigEpsilon);
	sim.advance(15);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.head.position.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.5, sim.rig.head.position.y, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(size_t(1), sim.pending());
	sim.advance(25);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, sim.rig.head.position.x, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(size_t(0), sim.pending());
}

void SimRigTest::testRelativeFramesFollowThePreviousLine() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("@10-20 ipd 0.07\n@+5 shot after\n"));
	sim.advance(24);
	CPPUNIT_ASSERT(sim.takeRequests().shots.empty());
	sim.advance(25);
	vr::SimRequests requests = sim.takeRequests();
	CPPUNIT_ASSERT_EQUAL(size_t(1), requests.shots.size());
	CPPUNIT_ASSERT_EQUAL(std::string("after"), requests.shots[0]);
}

void SimRigTest::testErrorsNameTheLine() {
	vr::Simulation sim;
	std::string error = sim.load("rate 90\nhead rot 1 2\n");
	CPPUNIT_ASSERT(rigStartsWith(error, "line 2: "));
	error = sim.load("\n\n@abc shot x\n");
	CPPUNIT_ASSERT(rigStartsWith(error, "line 3: "));
	error = sim.load("fly away\n");
	CPPUNIT_ASSERT(rigStartsWith(error, "line 1: unknown command"));
	error = sim.load("fov left 40 -50 45 -45\n");
	CPPUNIT_ASSERT(rigStartsWith(error, "line 1: fov"));
}

void SimRigTest::testBadScriptAppliesNothing() {
	vr::Simulation sim;
	CPPUNIT_ASSERT(!sim.load("ipd 0.07\n@5 shot a\nbogus\n").empty());
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.064, sim.view.ipd, RigEpsilon);
	CPPUNIT_ASSERT_EQUAL(size_t(0), sim.pending());
}

void SimRigTest::testShotAndQuitAreRequests() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("shot first"));
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("shot"));
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("mark hello there"));
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("quit"));
	vr::SimRequests requests = sim.takeRequests();
	CPPUNIT_ASSERT_EQUAL(size_t(2), requests.shots.size());
	CPPUNIT_ASSERT_EQUAL(std::string("first"), requests.shots[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("sim2"), requests.shots[1]);
	CPPUNIT_ASSERT_EQUAL(size_t(1), requests.marks.size());
	CPPUNIT_ASSERT_EQUAL(std::string("hello there"), requests.marks[0]);
	CPPUNIT_ASSERT(requests.quit);
	CPPUNIT_ASSERT(sim.takeRequests().empty());
	CPPUNIT_ASSERT(!sim.execute("shot ../escape").empty());
}

void SimRigTest::testSetupOnlyCommandsAfterTheSessionStarted() {
	vr::Simulation sim;
	sim.lockSetup();
	CPPUNIT_ASSERT(!sim.execute("eye-size 100 100").empty());
	CPPUNIT_ASSERT(!sim.execute("timeline session").empty());
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("ipd 0.06"));
}

void SimRigTest::testActionValues() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("action trigger right 0.8"));
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("action thumbstick both 0.5 -1"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.8, sim.rig.actions["trigger"][1].x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.actions["trigger"][0].x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.actions["thumbstick"][0].x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.0, sim.rig.actions["thumbstick"][1].y, RigEpsilon);
	CPPUNIT_ASSERT(!sim.execute("action trigger middle 1").empty());
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("profile touch"));
	CPPUNIT_ASSERT_EQUAL(std::string("/interaction_profiles/oculus/touch_controller"), sim.rig.profile);
	CPPUNIT_ASSERT(sim.takeRequests().profileChanged);
}

void SimRigTest::testChannelTimedLineCountsFromNow() {
	vr::Simulation sim;
	sim.advance(100);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("@+5 head rot 10 0 0"));
	sim.advance(104);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.head.angles.x, RigEpsilon);
	sim.advance(106);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, sim.rig.head.angles.x, RigEpsilon);
}

void SimRigTest::testShotCannotBeARamp() {
	vr::Simulation sim;
	CPPUNIT_ASSERT(rigStartsWith(sim.load("@1-5 shot a\n"), "line 1: @1-5: shot cannot be a ramp"));
	CPPUNIT_ASSERT(!sim.load("@5-1 ipd 0.07\n").empty());
}

void SimRigTest::testFocusAndFault() {
	vr::Simulation sim;
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("session visible"));
	CPPUNIT_ASSERT(!sim.rig.focused);
	CPPUNIT_ASSERT(sim.takeRequests().focusChanged);
	// The headset taken off and put on again (#27)
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("session idle"));
	CPPUNIT_ASSERT(!sim.rig.worn);
	CPPUNIT_ASSERT(sim.takeRequests().focusChanged);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("session idle"));
	CPPUNIT_ASSERT(!sim.takeRequests().focusChanged);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("session focused"));
	CPPUNIT_ASSERT(sim.rig.worn && sim.rig.focused);
	CPPUNIT_ASSERT(sim.takeRequests().focusChanged);
	CPPUNIT_ASSERT(!sim.execute("session asleep").empty());
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("fault swap-eyes on"));
	CPPUNIT_ASSERT(sim.rig.swapEyes);
	CPPUNIT_ASSERT(!sim.execute("fault melt on").empty());
	CPPUNIT_ASSERT(!sim.rig.saveLayers);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("layers on"));
	CPPUNIT_ASSERT(sim.rig.saveLayers);
	CPPUNIT_ASSERT(!sim.execute("layers maybe").empty());
}

void SimRigTest::testFingers() {
	vr::Simulation sim;
	// The default: joints for both hands, open
	CPPUNIT_ASSERT(sim.rig.fingers[0].joints && sim.rig.fingers[1].joints);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.fingers[1].curl.y, RigEpsilon);
	// How far the thumb, the index finger and the other three are closed (#305)
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand right fingers 0.2 1 0.5"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.2, sim.rig.fingers[1].curl.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, sim.rig.fingers[1].curl.y, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.fingers[1].curl.z, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.fingers[0].curl.y, RigEpsilon);
	CPPUNIT_ASSERT(!sim.execute("hand right fingers 1.5 0 0").empty());
	CPPUNIT_ASSERT(!sim.execute("hand right fingers 1 0").empty());
	CPPUNIT_ASSERT(!sim.execute("hand both fingers 1 1 1").empty());
	// No joints for a hand, and joints again
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand left fingers off"));
	CPPUNIT_ASSERT(!sim.rig.fingers[0].joints && sim.rig.fingers[1].joints);
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand left fingers on"));
	CPPUNIT_ASSERT(sim.rig.fingers[0].joints);
	// A ramp closes the hand over frames
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("@10-20 hand left fingers 1 1 1\n"));
	sim.advance(15);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.fingers[0].curl.x, RigEpsilon);
	sim.advance(20);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, sim.rig.fingers[0].curl.z, RigEpsilon);
}

void SimRigTest::testFiveFingers() {
	vr::Simulation sim;
	// Each finger on its own (#385): the thumb, index, middle, ring and little finger
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand left fingers 0.1 0.2 0.3 0.4 0.5"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, sim.rig.fingers[0].curl.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.2, sim.rig.fingers[0].curl.y, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.3, sim.rig.fingers[0].curl.z, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.4, sim.rig.fingers[0].outer.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.fingers[0].outer.y, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.fingers[1].outer.y, RigEpsilon);
	// The three-value form closes the middle, ring and little finger together
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.execute("hand left fingers 0 0 0.7"));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.7, sim.rig.fingers[0].curl.z, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.7, sim.rig.fingers[0].outer.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.7, sim.rig.fingers[0].outer.y, RigEpsilon);
	// Four or six values, or one out of range, is an error
	CPPUNIT_ASSERT(!sim.execute("hand left fingers 0 0 0 0").empty());
	CPPUNIT_ASSERT(!sim.execute("hand left fingers 0 0 0 0 1.5").empty());
	CPPUNIT_ASSERT(!sim.execute("hand left fingers 0 0 0 0 0 0").empty());
	// A ramp moves all five
	CPPUNIT_ASSERT_EQUAL(std::string(), sim.load("@10-20 hand right fingers 0 0 0 1 0.5\n"));
	sim.advance(15);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, sim.rig.fingers[1].outer.x, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.25, sim.rig.fingers[1].outer.y, RigEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, sim.rig.fingers[1].curl.z, RigEpsilon);
	sim.advance(20);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, sim.rig.fingers[1].outer.x, RigEpsilon);
}
