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

#include "vr/EyeProjectionTest.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "game/Camera.h"
#include "graphics/Math.h"

CppUnit::AutoRegisterSuite<EyeProjectionTest> g_registerEyeProjectionTest;

namespace {

const float NearDist = 9.f;      //!< 0.09 m at 100 units per meter (F-39)
const float FarDist = 6000.f;    //!< a typical camera depth
const float ProjectionEpsilon = 1e-4f;

} // anonymous namespace

vr::EyeFov EyeProjectionTest::quest3Left() {
	vr::EyeFov fov;
	fov.left = glm::radians(-54.f);
	fov.right = glm::radians(40.f);
	fov.up = glm::radians(44.f);
	fov.down = glm::radians(-55.f);
	return fov;
}

vr::EyeFov EyeProjectionTest::quest3Right() {
	vr::EyeFov fov = quest3Left();
	fov.left = glm::radians(-40.f);
	fov.right = glm::radians(54.f);
	return fov;
}

vr::LocatedEye EyeProjectionTest::eyeAt(const Vec3f & position, const vr::EyeFov & fov) {
	vr::LocatedEye eye;
	eye.position = position;
	eye.fov = fov;
	return eye;
}

Vec3f EyeProjectionTest::project(const glm::mat4x4 & projection, const Vec3f & view) {
	Vec4f clip = projection * Vec4f(view, 1.f);
	CPPUNIT_ASSERT(clip.w > 0.f);
	return Vec3f(clip) / clip.w;
}

void EyeProjectionTest::testSymmetricFovIsTheEnginesProjection() {

	// A 75° vertical FOV in a 640x480 viewport, as createProjectionMatrix() in game/Camera.cpp
	// builds it: w = aspect * cot(fov / 2), h = cot(fov / 2), depth Q - Q near / z, y flipped
	const float fov = glm::radians(75.f);
	const float halfTan = std::tan(fov / 2.f);
	vr::EyeFov symmetric;
	symmetric.up = fov / 2.f;
	symmetric.down = -fov / 2.f;
	symmetric.right = std::atan(halfTan * 640.f / 480.f);
	symmetric.left = -symmetric.right;

	glm::mat4x4 m = vr::eyeProjection(symmetric, NearDist, FarDist);

	const float h = std::cos(fov / 2.f) / std::sin(fov / 2.f);
	const float w = (480.f / 640.f) * h;
	const float Q = FarDist / (FarDist - NearDist);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(w, m[0][0], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-h, m[1][1], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, m[2][0], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, m[2][1], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(Q, m[2][2], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-Q * NearDist, m[3][2], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, m[2][3], ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, m[3][3], ProjectionEpsilon);

	// The view axis is the image center
	Vec3f center = project(m, Vec3f(0.f, 0.f, 100.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, center.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, center.y, ProjectionEpsilon);

	// The equivalent symmetric angles are the angles themselves
	CPPUNIT_ASSERT_DOUBLES_EQUAL(fov, vr::verticalFov(symmetric), ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.f * symmetric.right, vr::horizontalFov(symmetric), ProjectionEpsilon);
}

void EyeProjectionTest::testAsymmetricEdgesLandOnTheClipEdges() {

	const vr::EyeFov fov = quest3Left();
	const vr::FovTangents t = vr::tangents(fov);
	glm::mat4x4 m = vr::eyeProjection(fov, NearDist, FarDist);

	// The four FOV edges at any depth: left and right on x = -1 / +1, down and up on y = -1 / +1
	// (the engine's view space has y down, so the up edge is at -tan(up))
	const float z = 250.f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.f, project(m, Vec3f(t.left * z, 0.f, z)).x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, project(m, Vec3f(t.right * z, 0.f, z)).x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, project(m, Vec3f(0.f, -t.up * z, z)).y, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.f, project(m, Vec3f(0.f, -t.down * z, z)).y, ProjectionEpsilon);

	// The view axis is off the image center: this eye sees more to the left (54° > 40°) and below
	// (55° > 44°), so straight ahead lands right of and above the center
	Vec3f axis = project(m, Vec3f(0.f, 0.f, z));
	CPPUNIT_ASSERT(axis.x > 0.1f);
	CPPUNIT_ASSERT(axis.y > 0.1f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-(t.right + t.left) / (t.right - t.left), axis.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-(t.up + t.down) / (t.up - t.down), axis.y, ProjectionEpsilon);

	// The mirrored eye mirrors the shift
	Vec3f axisRight = project(vr::eyeProjection(quest3Right(), NearDist, FarDist), Vec3f(0.f, 0.f, z));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-axis.x, axisRight.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(axis.y, axisRight.y, ProjectionEpsilon);
}

void EyeProjectionTest::testWithoutTheYFlipTheUpEdgeIsAtTheBottom() {

	// Negative control (F-115): the same conversion written for a y-up view space puts the up edge
	// at the bottom of the image when fed the engine's y-down coordinates. eyeProjection() must
	// not be that matrix.
	const vr::EyeFov fov = quest3Left();
	const vr::FovTangents t = vr::tangents(fov);
	glm::mat4x4 noFlip = vr::eyeProjection(fov, NearDist, FarDist);
	noFlip[1][1] = -noFlip[1][1];
	noFlip[2][1] = -noFlip[2][1];

	const float z = 250.f;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.f, project(noFlip, Vec3f(0.f, -t.up * z, z)).y, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, project(vr::eyeProjection(fov, NearDist, FarDist), Vec3f(0.f, -t.up * z, z)).y, ProjectionEpsilon);
}

void EyeProjectionTest::testNearPlaneIsDepthZeroAndFarIsOne() {

	glm::mat4x4 m = vr::eyeProjection(quest3Left(), NearDist, FarDist);

	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, project(m, Vec3f(0.f, 0.f, NearDist)).z, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1.f, project(m, Vec3f(0.f, 0.f, FarDist)).z, ProjectionEpsilon);

	// Depth grows with distance, and so does clip w (the perspective divide the TL path relies on)
	Vec4f closer = m * Vec4f(0.f, 0.f, 2.f * NearDist, 1.f);
	Vec4f farther = m * Vec4f(0.f, 0.f, FarDist / 2.f, 1.f);
	CPPUNIT_ASSERT(closer.z / closer.w < farther.z / farther.w);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2.f * NearDist, closer.w, ProjectionEpsilon);

	// 0.09 m is the VR near plane
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.09f, vr::NearClipMeters, 1e-6f);
}

void EyeProjectionTest::testEquivalentFovsHaveTheSameExtent() {

	// The symmetric FOV with the same extent: its half-tangent is half the asymmetric extent, so a
	// camera set to it scales sprites and fog as the eye's projection does
	const vr::EyeFov fov = quest3Left();
	const vr::FovTangents t = vr::tangents(fov);
	CPPUNIT_ASSERT_DOUBLES_EQUAL((t.up - t.down) / 2.f, std::tan(vr::verticalFov(fov) / 2.f), ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL((t.right - t.left) / 2.f, std::tan(vr::horizontalFov(fov) / 2.f), ProjectionEpsilon);

	// Not the sum of the angles: 44° + 55° is less than the extent says
	CPPUNIT_ASSERT(vr::verticalFov(fov) > glm::radians(99.f));
}

void EyeProjectionTest::testParallelEyesHaveTheRightParallax() {

	// Eyes 64 mm apart looking along -z (OpenXR), the head at the origin
	std::array<vr::EyeRig, 2> rig = vr::eyeRig(eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left()),
	                                           eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Right()));

	CPPUNIT_ASSERT_DOUBLES_EQUAL(-3.2f, rig[0].offset.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3.2f, rig[1].offset.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[0].offset.y, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[0].offset.z, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.064f, vr::eyeSeparationMeters(rig), 1e-5f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, vr::cantAngle(rig), ProjectionEpsilon);

	// A head camera at the origin looking along the engine's +z; a point 2 m straight ahead
	Camera head;
	head.m_pos = Vec3f(0.f);
	head.angle = Anglef(0.f, 0.f, 0.f);
	glm::mat4x4 headWorldToView = cameraWorldToView(head);
	const Vec3f point(0.f, 0.f, 200.f);

	// The left eye sees it to the right, the right eye to the left - by half the separation each
	// (F-06: parallax sign and eye separation)
	Vec3f inLeft = Vec3f(vr::eyeWorldToView(headWorldToView, rig[0]) * Vec4f(point, 1.f));
	Vec3f inRight = Vec3f(vr::eyeWorldToView(headWorldToView, rig[1]) * Vec4f(point, 1.f));
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3.2f, inLeft.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-3.2f, inRight.x, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(200.f, inLeft.z, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(200.f, inRight.z, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, inLeft.y, ProjectionEpsilon);

	// On the screen the left eye's image of the point is right of the right eye's
	Vec3f ndcLeft = project(vr::eyeProjection(rig[0].fov, NearDist, FarDist), inLeft);
	Vec3f ndcRight = project(vr::eyeProjection(rig[1].fov, NearDist, FarDist), inRight);
	// Both eyes see straight ahead off their own image centers (asymmetric FOV), so compare the
	// shift of the point against each eye's view axis
	Vec3f axisLeft = project(vr::eyeProjection(rig[0].fov, NearDist, FarDist), Vec3f(0.f, 0.f, 200.f));
	Vec3f axisRight = project(vr::eyeProjection(rig[1].fov, NearDist, FarDist), Vec3f(0.f, 0.f, 200.f));
	CPPUNIT_ASSERT(ndcLeft.x - axisLeft.x > 0.01f);
	CPPUNIT_ASSERT(ndcRight.x - axisRight.x < -0.01f);
}

void EyeProjectionTest::testTheRigDoesNotDependOnTheHeadPose() {

	// The same eyes on a head turned 90° left, tilted 20° down and moved across the room: the rig
	// (eyes relative to the head) is unchanged
	glm::quat headOrientation = glm::angleAxis(glm::radians(90.f), Vec3f(0.f, 1.f, 0.f))
	                            * glm::angleAxis(glm::radians(-20.f), Vec3f(1.f, 0.f, 0.f));
	Vec3f headPosition(1.5f, 1.7f, -2.f);

	vr::LocatedEye left = eyeAt(headPosition + headOrientation * Vec3f(-0.032f, 0.f, 0.f), quest3Left());
	left.orientation = headOrientation;
	vr::LocatedEye right = eyeAt(headPosition + headOrientation * Vec3f(0.032f, 0.f, 0.f), quest3Right());
	right.orientation = headOrientation;

	std::array<vr::EyeRig, 2> rig = vr::eyeRig(left, right);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-3.2f, rig[0].offset.x, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(3.2f, rig[1].offset.x, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[0].offset.y, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[0].offset.z, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[1].offset.y, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, rig[1].offset.z, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, vr::cantAngle(rig), 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.064f, vr::eyeSeparationMeters(rig), 1e-5f);
}

void EyeProjectionTest::testEyeWorldPositionsStraddleTheHead() {

	std::array<vr::EyeRig, 2> rig = vr::eyeRig(eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left()),
	                                           eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Right()));

	// Yaw 0: the engine's view x is the world's x, so the left eye is at -3.2 along it
	Camera head;
	head.m_pos = Vec3f(1000.f, -170.f, 2000.f);
	head.angle = Anglef(0.f, 0.f, 0.f);
	Vec3f left = vr::eyeWorldPosition(cameraWorldToView(head), rig[0]);
	Vec3f right = vr::eyeWorldPosition(cameraWorldToView(head), rig[1]);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1000.f - 3.2f, left.x, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(1000.f + 3.2f, right.x, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(-170.f, left.y, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(2000.f, left.z, 1e-3f);

	// Any heading and pitch: 6.4 units apart, level with the head, across the view direction,
	// and the head halfway between them
	const float yaws[] = { 37.f, 90.f, 180.f, 275.f };
	for(float yaw : yaws) {
		head.angle = Anglef(15.f, yaw, 0.f);
		glm::mat4x4 worldToView = cameraWorldToView(head);
		left = vr::eyeWorldPosition(worldToView, rig[0]);
		right = vr::eyeWorldPosition(worldToView, rig[1]);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(6.4f, glm::distance(left, right), 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(head.m_pos.y, left.y, 1e-3f);
		CPPUNIT_ASSERT_DOUBLES_EQUAL(head.m_pos.y, right.y, 1e-3f);
		Vec3f front = angleToFrontUpVec(head.angle).first;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::dot(right - left, front), 1e-3f);
		Vec3f mid = (left + right) * 0.5f;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::distance(mid, head.m_pos), 1e-3f);
		// The eye is where its own view matrix says the origin is
		Vec3f inLeft = Vec3f(vr::eyeWorldToView(worldToView, rig[0]) * Vec4f(left, 1.f));
		CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, glm::length(inLeft), 1e-2f);
	}
}

void EyeProjectionTest::testCantedEyeTurnsItsView() {

	// The right eye's display canted 5° outward: in OpenXR a turn about +y by -5° (toward -x is
	// left, so a negative angle looks right)
	vr::LocatedEye left = eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left());
	vr::LocatedEye right = eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Right());
	right.orientation = glm::angleAxis(glm::radians(-5.f), Vec3f(0.f, 1.f, 0.f));

	std::array<vr::EyeRig, 2> rig = vr::eyeRig(left, right);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(5.f), vr::cantAngle(rig), 1e-4f);

	// The head's orientation is halfway: each eye is turned 2.5° from it, the right one to the
	// right, so a point straight ahead of the head is left of the right eye's view axis by the
	// cant on top of the parallax, and right of the left eye's by less than the parallax alone
	Camera head;
	head.m_pos = Vec3f(0.f);
	head.angle = Anglef(0.f, 0.f, 0.f);
	glm::mat4x4 headWorldToView = cameraWorldToView(head);
	const Vec3f point(0.f, 0.f, 200.f);
	Vec3f inRight = Vec3f(vr::eyeWorldToView(headWorldToView, rig[1]) * Vec4f(point, 1.f));
	Vec3f inLeft = Vec3f(vr::eyeWorldToView(headWorldToView, rig[0]) * Vec4f(point, 1.f));
	const float cantShift = 200.f * std::tan(glm::radians(2.5f));
	CPPUNIT_ASSERT(inRight.x < -3.f - cantShift + 0.1f);
	CPPUNIT_ASSERT(inLeft.x > 3.f + cantShift - 0.1f);

	// The right eye's view direction in the head's axes points right of straight ahead
	Vec3f forward = rig[1].rotation * Vec3f(0.f, 0.f, 1.f);
	CPPUNIT_ASSERT(forward.x > 0.04f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(0.f, forward.y, 1e-4f);
	Vec3f forwardLeft = rig[0].rotation * Vec3f(0.f, 0.f, 1.f);
	CPPUNIT_ASSERT(forwardLeft.x < -0.04f);
}

void EyeProjectionTest::testUnionIsTheEyeFovForIdenticalEyes() {

	// Two parallel eyes with the same FOV: the union is that FOV
	std::array<vr::EyeRig, 2> rig = vr::eyeRig(eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left()),
	                                           eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Left()));
	vr::EyeFov fov = vr::unionFov(rig);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(quest3Left().left, fov.left, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(quest3Left().right, fov.right, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(quest3Left().up, fov.up, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(quest3Left().down, fov.down, ProjectionEpsilon);

	// Mirrored eyes: the union is symmetric with the outer angles
	rig = vr::eyeRig(eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left()), eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Right()));
	fov = vr::unionFov(rig);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(-54.f), fov.left, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(54.f), fov.right, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(44.f), fov.up, ProjectionEpsilon);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(-55.f), fov.down, ProjectionEpsilon);
}

void EyeProjectionTest::testUnionContainsBothEyesWithCant() {

	vr::LocatedEye left = eyeAt(Vec3f(-0.032f, 0.f, 0.f), quest3Left());
	left.orientation = glm::angleAxis(glm::radians(5.f), Vec3f(0.f, 1.f, 0.f));
	vr::LocatedEye right = eyeAt(Vec3f(0.032f, 0.f, 0.f), quest3Right());
	right.orientation = glm::angleAxis(glm::radians(-5.f), Vec3f(0.f, 1.f, 0.f));
	std::array<vr::EyeRig, 2> rig = vr::eyeRig(left, right);

	vr::EyeFov fov = vr::unionFov(rig);
	vr::FovTangents u = vr::tangents(fov);

	// Wider than either eye by the cant: the right eye's 54° edge is 5° further right in the head's axes
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(59.f), fov.right, 1e-3f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(glm::radians(-59.f), fov.left, 1e-3f);
	CPPUNIT_ASSERT(fov.up > quest3Left().up);
	CPPUNIT_ASSERT(fov.down < quest3Left().down);

	// Every frustum corner of both eyes, turned into the head's axes, is inside the union
	for(const vr::EyeRig & eye : rig) {
		vr::FovTangents t = vr::tangents(eye.fov);
		const Vec3f corners[4] = {
			Vec3f(t.left, -t.up, 1.f), Vec3f(t.right, -t.up, 1.f),
			Vec3f(t.left, -t.down, 1.f), Vec3f(t.right, -t.down, 1.f),
		};
		for(const Vec3f & corner : corners) {
			Vec3f d = eye.rotation * corner;
			float x = d.x / d.z;
			float y = -d.y / d.z; // up positive, like the FOV angles
			CPPUNIT_ASSERT(x >= u.left - ProjectionEpsilon);
			CPPUNIT_ASSERT(x <= u.right + ProjectionEpsilon);
			CPPUNIT_ASSERT(y <= u.up + ProjectionEpsilon);
			CPPUNIT_ASSERT(y >= u.down - ProjectionEpsilon);
		}
	}

	// And the union is tight: each of its four edges is some eye's corner
	bool onLeft = false, onRight = false, onUp = false, onDown = false;
	for(const vr::EyeRig & eye : rig) {
		vr::FovTangents t = vr::tangents(eye.fov);
		const Vec3f corners[4] = {
			Vec3f(t.left, -t.up, 1.f), Vec3f(t.right, -t.up, 1.f),
			Vec3f(t.left, -t.down, 1.f), Vec3f(t.right, -t.down, 1.f),
		};
		for(const Vec3f & corner : corners) {
			Vec3f d = eye.rotation * corner;
			float x = d.x / d.z;
			float y = -d.y / d.z;
			onLeft = onLeft || std::abs(x - u.left) < ProjectionEpsilon;
			onRight = onRight || std::abs(x - u.right) < ProjectionEpsilon;
			onUp = onUp || std::abs(y - u.up) < ProjectionEpsilon;
			onDown = onDown || std::abs(y - u.down) < ProjectionEpsilon;
		}
	}
	CPPUNIT_ASSERT(onLeft && onRight && onUp && onDown);
}

void EyeProjectionTest::testPixelsPerUnitOfTheEnginesProjection() {

	// The engine's own projection (createProjectionMatrix() in game/Camera.cpp) for the default focal: the
	// scale must be what the sprites computed before #72, min(width, height) / 480 * focal, in a landscape
	// window and in a portrait one
	const float focal = 350.f;
	const float cot = 1.f / std::tan(Camera::focalToFov(focal) / 2.f);
	const Vec2f sizes[2] = { Vec2f(1920.f, 1080.f), Vec2f(1080.f, 1920.f) };
	for(const Vec2f & size : sizes) {
		glm::mat4x4 m(1.f);
		m[0][0] = ((size.y < size.x) ? size.y / size.x : 1.f) * cot;
		m[1][1] = -((size.y < size.x) ? 1.f : size.x / size.y) * cot;
		const float expected = std::min(size.x, size.y) / 480.f * focal;
		CPPUNIT_ASSERT_DOUBLES_EQUAL(expected, projectionPixelsPerUnit(m, size), expected * 1e-5f);
	}
}

void EyeProjectionTest::testPixelsPerUnitOfAnEyeIsItsVerticalScale() {

	// An Index-sized portrait eye image with the Quest 3's asymmetric angles: a unit at depth 1 spans
	// height / (tan up - tan down) pixels vertically - measured by projecting one
	const vr::EyeFov fov = quest3Left();
	const Vec2f size(2468.f, 2740.f);
	const glm::mat4x4 m = vr::eyeProjection(fov, NearDist, FarDist);
	const float depth = 100.f;
	const float y0 = project(m, Vec3f(0.f, 0.f, depth)).y;
	const float y1 = project(m, Vec3f(0.f, 1.f, depth)).y;
	const float measured = std::abs(y1 - y0) * size.y / 2.f * depth;
	const vr::FovTangents t = vr::tangents(fov);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(size.y / (t.up - t.down), measured, measured * 1e-4f);
	CPPUNIT_ASSERT_DOUBLES_EQUAL(measured, projectionPixelsPerUnit(m, size), measured * 1e-4f);

	// The formula before #72 with the eye camera's focal reads the narrow side of the image: ~10 % too small
	const float focal = Camera::fovToFocal(vr::verticalFov(fov));
	const float before = std::min(size.x, size.y) / 480.f * focal;
	CPPUNIT_ASSERT_DOUBLES_EQUAL(size.x / size.y, before / measured, 1e-4f);
}
