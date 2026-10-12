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

#include "vr/HeadPoseTest.h"

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "graphics/Math.h"
#include "vr/HeadPose.h"

CppUnit::AutoRegisterSuite<HeadPoseTest> g_registerHeadPoseTest;

namespace {

//! The difference of two angles in degrees, in [0, 180]
float angleDistance(float a, float b) {
	float d = std::fmod(std::abs(a - b), 360.f);
	return (d > 180.f) ? 360.f - d : d;
}

void checkAngle(const char * what, float expected, float actual, float tolerance = 0.01f) {
	if(angleDistance(expected, actual) > tolerance) {
		CPPUNIT_FAIL(std::string(what) + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void checkVector(const char * what, const Vec3f & expected, const Vec3f & actual, float tolerance = 0.001f) {
	if(glm::length(expected - actual) > tolerance) {
		CPPUNIT_FAIL(std::string(what) + ": expected (" + std::to_string(expected.x) + ", " + std::to_string(expected.y)
		             + ", " + std::to_string(expected.z) + "), got (" + std::to_string(actual.x) + ", "
		             + std::to_string(actual.y) + ", " + std::to_string(actual.z) + ")");
	}
}

vr::HeadSample head(float yaw, float pitch, float roll, const Vec3f & position = Vec3f(0.f)) {
	vr::HeadSample sample;
	sample.orientation = vr::headOrientationFromDegrees(yaw, pitch, roll);
	sample.position = position;
	return sample;
}

/*!
 * The world-to-view rotation a head must give, built independently of the decomposition: the head's
 * rotation in OpenXR axes, conjugated into engine axes (a half turn about x), turned about the
 * vertical by \a turn degrees (the engine's view-to-world rotation for a yaw is Ry(-yaw)), inverted
 */
glm::mat3 expectedWorldToView(const glm::quat & orientation, float turn) {
	const glm::mat3 axes(1.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, -1.f);
	const glm::mat3 headToLocal = axes * glm::mat3_cast(orientation) * axes;
	const glm::mat3 localToWorld = glm::transpose(glm::mat3(arx::eulerAngleY(glm::radians(turn))));
	return glm::transpose(localToWorld * headToLocal);
}

void checkRotation(const std::string & what, const glm::mat3 & expected, const Anglef & angle) {
	const glm::mat3 actual(toRotationMatrix(angle));
	for(int column = 0; column < 3; column++) {
		for(int row = 0; row < 3; row++) {
			if(std::abs(expected[column][row] - actual[column][row]) > 1e-4f) {
				CPPUNIT_FAIL(what + ": world-to-view element [" + std::to_string(column) + "][" + std::to_string(row)
				             + "] expected " + std::to_string(expected[column][row]) + ", got "
				             + std::to_string(actual[column][row]) + " (angles " + std::to_string(angle.getPitch())
				             + ", " + std::to_string(angle.getYaw()) + ", " + std::to_string(angle.getRoll()) + ")");
			}
		}
	}
}

//! A fixed pseudo-random sequence in [0, 1)
struct Sequence {
	u32 state = 12345;
	float next() {
		state = state * 1664525u + 1013904223u;
		return float(state >> 8) / float(1u << 24);
	}
};

} // anonymous namespace

void HeadPoseTest::testIdentityIsTheBodyCamera() {
	vr::HeadCamera camera = vr::headToCamera(head(0.f, 0.f, 0.f), vr::HeadReference(), 0.f);
	checkAngle("pitch", 0.f, camera.angle.getPitch());
	checkAngle("yaw", 0.f, camera.angle.getYaw());
	checkAngle("roll", 0.f, camera.angle.getRoll());
	checkVector("offset", Vec3f(0.f), camera.offset);
	checkVector("moved", Vec3f(0.f), camera.moved);

	// The body's yaw alone turns the camera with it
	camera = vr::headToCamera(head(0.f, 0.f, 0.f), vr::HeadReference(), 123.f);
	checkAngle("yaw with the body", 123.f, camera.angle.getYaw());
}

void HeadPoseTest::testSimpleTurnsHaveTheEngineSigns() {

	// Head turned 90 degrees left: the engine's yaw grows to the left, the view looks along -x
	vr::HeadCamera camera = vr::headToCamera(head(90.f, 0.f, 0.f), vr::HeadReference(), 0.f);
	checkAngle("yaw left", 90.f, camera.angle.getYaw());
	checkVector("looking left", Vec3f(-1.f, 0.f, 0.f), angleToVector(camera.angle));

	// Head 30 degrees up: the engine's pitch is negative upwards (y is down)
	camera = vr::headToCamera(head(0.f, 30.f, 0.f), vr::HeadReference(), 0.f);
	checkAngle("pitch up", -30.f, camera.angle.getPitch());
	CPPUNIT_ASSERT(angleToVector(camera.angle).y < -0.49f);

	// Head tilted 20 degrees left: the world's up direction appears turned to the right in the view
	camera = vr::headToCamera(head(0.f, 0.f, 20.f), vr::HeadReference(), 0.f);
	checkAngle("roll left", -20.f, camera.angle.getRoll());
	Vec3f worldUpInView = glm::mat3(toRotationMatrix(camera.angle)) * Vec3f(0.f, -1.f, 0.f);
	CPPUNIT_ASSERT(worldUpInView.x > 0.3f);
	CPPUNIT_ASSERT(worldUpInView.y < -0.9f);
}

void HeadPoseTest::testRotationMatchesTheEngineForAnyOrientation() {
	Sequence random;
	for(int i = 0; i < 2000; i++) {
		// A uniformly random orientation (Shoemake) and body / reference yaws
		float u1 = random.next();
		float u2 = random.next() * 2.f * glm::pi<float>();
		float u3 = random.next() * 2.f * glm::pi<float>();
		glm::quat orientation(std::sqrt(u1) * std::cos(u3), std::sqrt(1.f - u1) * std::sin(u2),
		                      std::sqrt(1.f - u1) * std::cos(u2), std::sqrt(u1) * std::sin(u3));
		vr::HeadSample sample;
		sample.orientation = orientation;
		vr::HeadReference reference;
		reference.yaw = random.next() * 360.f - 180.f;
		float bodyYaw = random.next() * 360.f;
		vr::HeadCamera camera = vr::headToCamera(sample, reference, bodyYaw);
		checkRotation("orientation " + std::to_string(i), expectedWorldToView(orientation, bodyYaw - reference.yaw),
		              camera.angle);
	}
}

void HeadPoseTest::testPolesAreExact() {
	const float pitches[] = { 90.f, -90.f, 89.999f, -89.999f, 89.9f, -89.9f };
	const float others[] = { 0.f, 37.f, -140.f };
	for(float pitch : pitches) {
		for(float yaw : others) {
			for(float roll : others) {
				glm::quat orientation = vr::headOrientationFromDegrees(yaw, pitch, roll);
				vr::HeadSample sample;
				sample.orientation = orientation;
				vr::HeadCamera camera = vr::headToCamera(sample, vr::HeadReference(), 25.f);
				checkRotation("pole pitch " + std::to_string(pitch) + " yaw " + std::to_string(yaw) + " roll "
				              + std::to_string(roll), expectedWorldToView(orientation, 25.f), camera.angle);
			}
		}
	}
}

void HeadPoseTest::testOnlyTheYawIsRelative() {

	// The reference keeps the heading only (F-120): a pitched and rolled head at the reference ...
	vr::HeadSample tilted = head(40.f, 20.f, 10.f);
	vr::HeadReference reference = vr::makeHeadReference(tilted);
	checkAngle("reference yaw", 40.f, reference.yaw);

	// ... keeps its absolute pitch and roll in the camera, while its heading becomes the body's yaw
	vr::HeadCamera camera = vr::headToCamera(tilted, reference, 10.f);
	checkAngle("yaw = body", 10.f, camera.angle.getYaw());
	checkAngle("pitch absolute", -20.f, camera.angle.getPitch());
	checkAngle("roll absolute", -10.f, camera.angle.getRoll());

	// Turning the head 30 degrees further left turns the camera 30 degrees left of the body
	camera = vr::headToCamera(head(70.f, 0.f, 0.f), reference, 10.f);
	checkAngle("yaw = body + head turn", 40.f, camera.angle.getYaw());
	checkAngle("pitch level", 0.f, camera.angle.getPitch());
}

void HeadPoseTest::testRotationAloneDoesNotMoveTheCamera() {
	const Vec3f position(0.3f, 1.6f, -0.2f);
	vr::HeadReference reference = vr::makeHeadReference(head(15.f, 0.f, 0.f, position));
	const float turns[][3] = { { 15.f, 0.f, 0.f }, { 100.f, 40.f, -15.f }, { -60.f, -70.f, 30.f } };
	for(const auto & turn : turns) {
		vr::HeadCamera camera = vr::headToCamera(head(turn[0], turn[1], turn[2], position), reference, 200.f);
		checkVector("no movement", Vec3f(0.f), camera.offset);
	}
}

void HeadPoseTest::testMovementIsTurnedWithTheBody() {

	vr::HeadReference reference = vr::makeHeadReference(head(0.f, 0.f, 0.f, Vec3f(0.f, 1.7f, 0.f)));

	// 10 cm forward (OpenXR -z) with the body facing yaw 90 (engine -x): 10 units along the body's facing
	vr::HeadCamera camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.f, 1.7f, -0.1f)), reference, 90.f);
	checkVector("forward", 10.f * angleToVector(Anglef(0.f, 90.f, 0.f)), camera.offset);

	// Head 20 cm lower: camera 20 cm lower, i.e. +20 units (y is down in the engine) - D-018
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.f, 1.5f, 0.f)), reference, 90.f);
	checkVector("crouch", Vec3f(0.f, 20.f, 0.f), camera.offset);

	// 30 cm to the right of the reference heading, body at yaw 0: engine +x
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.3f, 1.7f, 0.f)), reference, 0.f);
	checkVector("right", Vec3f(30.f, 0.f, 0.f), camera.offset);

	// A reference taken while looking 90 degrees to the right: stepping that way is the body's forward
	reference = vr::makeHeadReference(head(-90.f, 0.f, 0.f, Vec3f(0.f, 1.7f, 0.f)));
	camera = vr::headToCamera(head(-90.f, 0.f, 0.f, Vec3f(0.2f, 1.7f, 0.f)), reference, 0.f);
	checkVector("forward of a turned reference", Vec3f(0.f, 0.f, 20.f), camera.offset);
}

void HeadPoseTest::testAnchorIsTheBodysPart() {
	vr::HeadReference reference;
	// No lean limit any more (#26): 1 m away is 1 m of offset while the body did not follow
	vr::HeadCamera camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.8f, -0.1f, -0.6f)), reference, 0.f);
	checkVector("unclamped", Vec3f(80.f, 10.f, 60.f), camera.offset);
	checkVector("moved, reference frame", Vec3f(0.8f, 0.1f, 0.6f), camera.moved);

	// The body walked 0.7 m right and 0.5 m forward of it: the camera keeps the rest, the height untouched
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.8f, -0.1f, -0.6f)), reference, 0.f, Vec2f(0.7f, 0.5f));
	checkVector("lean after the body", Vec3f(10.f, 10.f, 10.f), camera.offset);
	checkVector("moved is the head's own", Vec3f(0.8f, 0.1f, 0.6f), camera.moved);

	// The anchor is in the reference frame: with the body at yaw 90 the rest turns with it
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.f, 0.f, -0.5f)), reference, 90.f, Vec2f(0.f, 0.4f));
	checkVector("turned rest", 10.f * angleToVector(Anglef(0.f, 90.f, 0.f)), camera.offset);
}

void HeadPoseTest::testHeightOffsetLiftsTheCamera() {

	vr::HeadReference reference = vr::makeHeadReference(head(0.f, 0.f, 0.f, Vec3f(0.f, 1.7f, 0.f)));
	vr::HeadCameraParams params;

	// The character height setting (#260): +10 cm sees 10 units above the character's eye (y is down)
	params.heightOffsetMeters = 0.1f;
	vr::HeadCamera camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.f, 1.7f, 0.f)), reference, 0.f, Vec2f(0.f),
	                                         params);
	checkVector("at the reference", Vec3f(0.f, -10.f, 0.f), camera.offset);
	// The head's own movement (crouch #100 reads it) does not include the setting
	checkVector("moved untouched", Vec3f(0.f), camera.moved);

	// It adds to the head's own movement: 20 cm lower and turned with the body - the height stays vertical
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.3f, 1.5f, 0.f)), reference, 90.f, Vec2f(0.f), params);
	checkVector("crouched and to the side", Vec3f(0.f, 10.f, 30.f), camera.offset);

	// Shorter, with the body's anchor (#26) taken off the horizontal part only
	params.heightOffsetMeters = -0.2f;
	camera = vr::headToCamera(head(0.f, 0.f, 0.f, Vec3f(0.8f, 1.7f, -0.6f)), reference, 0.f, Vec2f(0.7f, 0.5f),
	                          params);
	checkVector("shorter after the body", Vec3f(10.f, 20.f, 10.f), camera.offset);

	// The setting's range
	CPPUNIT_ASSERT_EQUAL(0.2f, vr::clampHeightOffset(0.5f));
	CPPUNIT_ASSERT_EQUAL(-0.2f, vr::clampHeightOffset(-3.f));
	CPPUNIT_ASSERT_EQUAL(0.06f, vr::clampHeightOffset(0.06f));
	CPPUNIT_ASSERT_EQUAL(0.f, vr::clampHeightOffset(std::nanf("")));
}

void HeadPoseTest::testRecenterIsTheEye() {
	// Recenter taken seated and turned: the head there is the character's eye, facing the body's way
	vr::HeadSample seated = head(40.f, 0.f, 0.f, Vec3f(0.25f, 1.2f, 0.1f));
	vr::HeadReference reference = vr::makeHeadReference(seated);
	vr::HeadCamera camera = vr::headToCamera(seated, reference, 90.f);
	checkVector("recentered", Vec3f(0.f), camera.offset);
	CPPUNIT_ASSERT(std::abs(camera.angle.getYaw() - 90.f) < 1e-3f);
}

void HeadPoseTest::testHeadBetweenTheEyes() {
	glm::quat orientation = vr::headOrientationFromDegrees(30.f, -10.f, 5.f);
	vr::HeadSample sample = vr::headFromEyes(Vec3f(-0.032f, 1.6f, 0.1f), orientation, Vec3f(0.032f, 1.62f, 0.1f),
	                                         orientation);
	checkVector("midpoint", Vec3f(0.f, 1.61f, 0.1f), sample.position);
	CPPUNIT_ASSERT(std::abs(glm::dot(sample.orientation, orientation)) > 0.99999f);
}

void HeadPoseTest::testWrapDegrees() {
	CPPUNIT_ASSERT_EQUAL(0.f, vr::wrapDegrees(0.f));
	CPPUNIT_ASSERT_EQUAL(0.f, vr::wrapDegrees(360.f));
	CPPUNIT_ASSERT_EQUAL(350.f, vr::wrapDegrees(-10.f));
	CPPUNIT_ASSERT_EQUAL(10.f, vr::wrapDegrees(730.f));
	CPPUNIT_ASSERT(vr::wrapDegrees(-1e-7f) < 360.f);
}

void HeadPoseTest::testQuadFovFillsTheQuad() {
	// The 2.4 m wide quad at 1.75 m of #18 with a 16:9 and a 4:3 frame: the engine's projection keeps the
	// vertical FOV and widens it by the aspect ratio, so both directions must cover the quad exactly
	const Vec2i frames[] = { Vec2i(1280, 720), Vec2i(640, 480) };
	for(const Vec2i & frame : frames) {
		float fov = vr::headLockedQuadFov(2.4f, 1.75f, frame);
		float halfHeight = 2.4f * float(frame.y) / float(frame.x) / 2.f;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(double(halfHeight / 1.75f), double(std::tan(fov / 2.f)), 1e-6);
		float horizontal = std::tan(fov / 2.f) * float(frame.x) / float(frame.y);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(1.2 / 1.75, double(horizontal), 1e-6);
	}
	CPPUNIT_ASSERT_DOUBLES_EQUAL(42.2, double(glm::degrees(vr::headLockedQuadFov(2.4f, 1.75f, Vec2i(1280, 720)))), 0.1);
}
