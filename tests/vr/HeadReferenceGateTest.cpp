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

#include "vr/HeadReferenceGateTest.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "vr/HeadPose.h"
#include "vr/HeadReferenceGate.h"
#include "vr/WorldScale.h"

CppUnit::AutoRegisterSuite<HeadReferenceGateTest> g_registerHeadReferenceGateTest;

namespace {

//! One frame of the headset at 90 Hz
const double FrameSeconds = 1.0 / 90.0;

struct Take {
	double seconds;
	vr::ReferenceKind kind;
	Vec3f position;
};

/*!
 * Run the gate frame by frame from \a from to \a to (seconds) with the head at \a position(t) and
 * the headset \a worn; \return the references it asked for
 */
std::vector<Take> run(vr::HeadReferenceGate & gate, double from, double to,
                      const std::function<Vec3f(double)> & position, bool worn) {
	std::vector<Take> takes;
	for(double t = from; t < to - 1e-9; t += FrameSeconds) {
		vr::ReferenceKind kind = gate.update(t, position(t), worn);
		if(kind != vr::ReferenceKind::None) {
			takes.push_back(Take{ t, kind, position(t) });
		}
	}
	return takes;
}

Vec3f still(double /* t */) {
	return Vec3f(0.f, 0.f, 0.f);
}

//! A head that keeps moving 10 cm back and forth (looking around, adjusting the straps)
Vec3f restless(double t) {
	return Vec3f(0.1f * float(std::sin(t * 6.0)), 0.f, 0.f);
}

std::string describe(const std::vector<Take> & takes) {
	std::string text;
	for(const Take & take : takes) {
		text += std::string(text.empty() ? "" : ", ") + vr::referenceKindName(take.kind) + " at "
		        + std::to_string(take.seconds);
	}
	return text.empty() ? "nothing" : text;
}

void checkTakes(const char * what, const std::vector<Take> & takes,
                const std::vector<std::pair<vr::ReferenceKind, double>> & expected, double tolerance = 0.02) {
	bool ok = takes.size() == expected.size();
	for(size_t i = 0; ok && i < takes.size(); i++) {
		ok = takes[i].kind == expected[i].first && std::abs(takes[i].seconds - expected[i].second) <= tolerance;
	}
	if(!ok) {
		std::string want;
		for(const auto & e : expected) {
			want += std::string(want.empty() ? "" : ", ") + vr::referenceKindName(e.first) + " at " + std::to_string(e.second);
		}
		CPPUNIT_FAIL(std::string(what) + ": expected " + (want.empty() ? "nothing" : want) + ", got " + describe(takes));
	}
}

} // anonymous namespace

void HeadReferenceGateTest::testProvisionalWhileNotWorn() {

	// The headset idle on the desk for a minute: one provisional reference so the view turns, never a settled one
	vr::HeadReferenceGate gate;
	checkTakes("idle headset", run(gate, 0.0, 60.0, still, false), { { vr::ReferenceKind::Provisional, 0.0 } });
	CPPUNIT_ASSERT(gate.pending());
	CPPUNIT_ASSERT(gate.current() == vr::ReferenceKind::Provisional);
	CPPUNIT_ASSERT(!gate.worn());
}

void HeadReferenceGateTest::testSettledOnceTheWornHeadIsStill() {

	// Worn from the start and still: provisional on the first frame, settled after settleSeconds
	vr::HeadReferenceGate gate;
	const double settle = double(vr::ReferenceGateParams().settleSeconds);
	checkTakes("still worn head", run(gate, 0.0, 10.0, still, true),
	           { { vr::ReferenceKind::Provisional, 0.0 }, { vr::ReferenceKind::Settled, settle } });
	CPPUNIT_ASSERT(!gate.pending());
	CPPUNIT_ASSERT(gate.current() == vr::ReferenceKind::Settled);

	// Moving within settleMeters counts as still
	vr::HeadReferenceGate gate2;
	auto sway = [](double t) { return Vec3f(0.01f * float(std::sin(t * 6.0)), 0.f, 0.f); };
	checkTakes("small sway", run(gate2, 0.0, 10.0, sway, true),
	           { { vr::ReferenceKind::Provisional, 0.0 }, { vr::ReferenceKind::Settled, settle } });
}

void HeadReferenceGateTest::testMovingHeadSettlesAfterTheMaximumWait() {

	vr::HeadReferenceGate gate;
	const double wait = double(vr::ReferenceGateParams().maxWaitSeconds);
	checkTakes("restless worn head", run(gate, 0.0, 10.0, restless, true),
	           { { vr::ReferenceKind::Provisional, 0.0 }, { vr::ReferenceKind::Settled, wait } });
}

void HeadReferenceGateTest::testTakingTheHeadsetOffReArms() {

	vr::HeadReferenceGate gate;
	run(gate, 0.0, 5.0, still, true);
	CPPUNIT_ASSERT(gate.current() == vr::ReferenceKind::Settled);

	// Taken off: nothing is taken while it is off, the settled reference stays in use
	checkTakes("off", run(gate, 5.0, 20.0, still, false), {});
	CPPUNIT_ASSERT(gate.pending());
	CPPUNIT_ASSERT(gate.current() == vr::ReferenceKind::Settled);

	// Put on again (sitting now, 50 cm lower): a new settled reference once the head is still there
	auto seated = [](double /* t */) { return Vec3f(0.f, -0.5f, 0.f); };
	const double settle = double(vr::ReferenceGateParams().settleSeconds);
	std::vector<Take> takes = run(gate, 20.0, 25.0, seated, true);
	checkTakes("on again", takes, { { vr::ReferenceKind::Settled, 20.0 + settle } });
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.5, double(takes[0].position.y), 1e-6);

	// The dashboard (visible) is worn too: brief focus changes do not count as taking it off - the caller
	// passes worn for visible and focused alike, so nothing more is taken
	checkTakes("worn on", run(gate, 25.0, 40.0, seated, true), {});
}

void HeadReferenceGateTest::testResetWhileWornSettlesAtOnce() {

	// A level load or a recenter while the headset is worn: settled on the same frame, still or moving -
	// one reference, so a head that turns meanwhile never turns the view a second time
	vr::HeadReferenceGate gate;
	run(gate, 0.0, 5.0, still, true);
	gate.reset();
	CPPUNIT_ASSERT(gate.pending());
	checkTakes("reset, still", run(gate, 5.0, 10.0, still, true), { { vr::ReferenceKind::Settled, 5.0 } });

	vr::HeadReferenceGate restlessGate;
	run(restlessGate, 0.0, 5.0, restless, true);
	restlessGate.reset();
	checkTakes("reset, moving", run(restlessGate, 5.0, 10.0, restless, true), { { vr::ReferenceKind::Settled, 5.0 } });
	CPPUNIT_ASSERT(!restlessGate.pending());
}

void HeadReferenceGateTest::testResetWhileSettlingWaits() {

	// Put on at 0 s with the head moving until 1 s; a level load at 0.5 s: provisional at once (the view keeps
	// turning), settled only once the head has been still for the settle time
	auto stopsAt1 = [](double t) { return (t < 1.0) ? restless(t) : restless(1.0); };
	vr::HeadReferenceGate gate;
	std::vector<Take> takes = run(gate, 0.0, 0.5, stopsAt1, true);
	checkTakes("put on", takes, { { vr::ReferenceKind::Provisional, 0.0 } });
	gate.reset();
	takes = run(gate, 0.5, 5.0, stopsAt1, true);
	const double settle = double(vr::ReferenceGateParams().settleSeconds);
	CPPUNIT_ASSERT_EQUAL(size_t(2), takes.size());
	CPPUNIT_ASSERT(takes[0].kind == vr::ReferenceKind::Provisional);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, takes[0].seconds, 0.02);
	CPPUNIT_ASSERT(takes[1].kind == vr::ReferenceKind::Settled);
	// The head left the 3 cm window a little before it stopped, so the settle ends between 1 s and 1 s + settle
	CPPUNIT_ASSERT(takes[1].seconds > 1.0 && takes[1].seconds <= 1.0 + settle + 0.02);

	// A reset while not worn: provisional at once, nothing more until it is worn
	gate.reset();
	checkTakes("reset while off", run(gate, 10.0, 20.0, still, false), { { vr::ReferenceKind::Provisional, 10.0 } });
}

void HeadReferenceGateTest::testSimulatedHeadIsInstant() {

	// A head set through the test channel never waits: settled on its first sample and on every reset
	vr::HeadReferenceGate gate;
	std::vector<Take> takes;
	for(double t = 0.0; t < 2.0; t += FrameSeconds) {
		if(vr::ReferenceKind kind = gate.update(t, restless(t), true, true); kind != vr::ReferenceKind::None) {
			takes.push_back(Take{ t, kind, restless(t) });
		}
		if(std::abs(t - 1.0) < FrameSeconds / 2) {
			gate.reset();
		}
	}
	CPPUNIT_ASSERT_EQUAL(size_t(2), takes.size());
	CPPUNIT_ASSERT(takes[0].kind == vr::ReferenceKind::Settled && takes[0].seconds == 0.0);
	CPPUNIT_ASSERT(takes[1].kind == vr::ReferenceKind::Settled);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0 + FrameSeconds, takes[1].seconds, 0.001);
}

void HeadReferenceGateTest::testDeskToHeadScenario() {

	// The owner's #24 run: the save loads with the headset on the desk, 80 cm below where the standing head
	// will be; a minute later it is put on (one second of lifting it), then the player stands still
	const float desk = -0.8f;
	auto lifted = [desk](double t) {
		const double up = std::min(std::max(t - 60.0, 0.0), 1.0);
		return Vec3f(0.f, desk * float(1.0 - up), 0.f);
	};

	vr::HeadReferenceGate gate;
	std::vector<Take> takes = run(gate, 0.0, 60.0, lifted, false);
	checkTakes("on the desk", takes, { { vr::ReferenceKind::Provisional, 0.0 } });
	CPPUNIT_ASSERT_DOUBLES_EQUAL(double(desk), double(takes[0].position.y), 1e-6);

	const double settle = double(vr::ReferenceGateParams().settleSeconds);
	takes = run(gate, 60.0, 70.0, lifted, true);
	CPPUNIT_ASSERT_EQUAL(size_t(1), takes.size());
	CPPUNIT_ASSERT(takes[0].kind == vr::ReferenceKind::Settled);
	CPPUNIT_ASSERT(takes[0].seconds > 61.0 - 0.1 && takes[0].seconds <= 61.0 + settle + 0.02);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(takes[0].position.y), 0.031);

	// The standing head at the settled reference is the character's eye: no offset; with the provisional
	// reference from the desk it would have been 80 units above the eye (y down: -80)
	vr::HeadSample standing;
	standing.position = Vec3f(0.f, 0.f, 0.f);
	vr::HeadReference settled;
	settled.position = takes[0].position;
	vr::HeadReference fromDesk;
	fromDesk.position = Vec3f(0.f, desk, 0.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::headToCamera(standing, settled, 0.f).offset.y), 3.1);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-80.0, double(vr::headToCamera(standing, fromDesk, 0.f).offset.y), 0.01);
}

void HeadReferenceGateTest::testWorldScale() {

	// One scale for the head and the eyes: 1 m = 100 units (the engine's audio: 0.01 m per unit)
	CPPUNIT_ASSERT_DOUBLES_EQUAL(100.0, double(vr::UnitsPerMeter), 0.0);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(double(vr::UnitsPerMeter), double(vr::HeadCameraParams().unitsPerMeter), 0.0);

	// D-018: head 20 cm lower than at the reference -> camera 20 units lower (engine y is down)
	vr::HeadReference reference;
	reference.position = Vec3f(0.f, 0.1f, 0.f);
	vr::HeadSample lower;
	lower.position = Vec3f(0.f, -0.1f, 0.f);
	vr::HeadCamera camera = vr::headToCamera(lower, reference, 0.f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, double(camera.offset.y), 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(camera.offset.x), 0.001);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(camera.offset.z), 0.001);
}

void HeadReferenceGateTest::testAutoRaise() {

	// Recentered seated (the head 50 cm lower than standing), then the player stands up: the reference rises
	// with the head, so the view stops at the character's eye instead of going 50 units above it
	vr::HeadReference reference;
	reference.yaw = 30.f;
	reference.position = Vec3f(0.2f, -0.5f, 0.1f);

	vr::HeadSample head;
	head.position = Vec3f(0.2f, -0.6f, 0.1f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::raiseHeadReference(reference, head)), 0.0);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-0.5, double(reference.position.y), 1e-6);

	head.position = Vec3f(0.3f, 0.f, 0.1f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.5, double(vr::raiseHeadReference(reference, head)), 1e-6);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(reference.position.y), 1e-6);
	// Only the height: the heading and the horizontal reference stay
	CPPUNIT_ASSERT_DOUBLES_EQUAL(30.0, double(reference.yaw), 0.0);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.2, double(reference.position.x), 1e-6);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.1, double(reference.position.z), 1e-6);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::headToCamera(head, reference, 0.f).offset.y), 0.001);

	// Back down 20 cm: the camera goes 20 units below the eye at once, and the reference stays up
	head.position.y = -0.2f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, double(vr::raiseHeadReference(reference, head)), 0.0);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, double(vr::headToCamera(head, reference, 0.f).offset.y), 0.001);
}
