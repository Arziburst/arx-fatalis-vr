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

#include "vr/HeadTracking.h"

#include <cmath>
#include <cstring>
#include <string>

#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "vr/BodyYaw.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

struct HeadTrackingState {

	bool haveHmd = false;   //!< a headset sample was seen in this session
	HeadSample lastHmd;

	bool haveSim = false;
	HeadSample sim;

	bool resetPending = true;
	std::string resetReason = "startup";

	HeadStatus status;
	BodyStatus body;

};

HeadTrackingState g_head;

//! A drag episode ends only this far inside the dead zone, so a head held at its edge is one episode
const float DragEndMarginDegrees = 5.f;

/*!
 * The body after the head (#25): \a headYaw is the head's turn since the reference; the body keeps its
 * own turn from the heading, so turning the heading turns both
 */
void updateBody(float headYaw, float heading, float lookYaw) {

	BodyStatus & body = g_head.body;
	const BodyFollowParams params;
	const float seconds = toMsf(g_platformTime.lastFrameDuration()) / 1000.f;
	const BodyFollow follow = followHead(body.offset, headYaw, seconds, params);

	const float gap = std::abs(wrapSignedDegrees(headYaw - follow.yaw));
	const bool dragged = follow.dragged || (body.dragged && gap > params.deadZoneDegrees - DragEndMarginDegrees);
	if(dragged != body.dragged) {
		if(dragged) {
			body.drags++;
		}
		// An episode, not a frame: the head reached the dead zone's edge and pulls the body, or let it go
		gameevent::Event("vr.body.drag", 100)("state", dragged ? "start" : "end")
			("head", headYaw, 1)("body", follow.yaw, 1)("deadZone", params.deadZoneDegrees, 0);
	}

	body.offset = follow.yaw;
	body.dragged = dragged;
	body.lookYaw = wrapDegrees(lookYaw);
	body.bodyYaw = wrapDegrees(heading + body.offset);
}

void resetBody(float heading) {
	BodyStatus & body = g_head.body;
	body.offset = 0.f;
	body.dragged = false;
	body.lookYaw = body.bodyYaw = wrapDegrees(heading);
}

} // anonymous namespace

bool applyHeadPose(Vec3f & position, Anglef & angle, float bodyYaw) {

	HeadStatus & status = g_head.status;
	const char * source = "none";
	HeadSample head;

	if(!isEnabled()) {
		g_head.haveHmd = false;
	}

	const FrameViews & views = frameViews();
	if(isEnabled() && views.valid) {
		head = headFromEyes(views.eyes[0].position, views.eyes[0].orientation,
		                    views.eyes[1].position, views.eyes[1].orientation);
		g_head.lastHmd = head;
		g_head.haveHmd = true;
		source = "hmd";
	} else if(isEnabled() && g_head.haveHmd) {
		head = g_head.lastHmd;
		source = "held";
	} else if(g_head.haveSim) {
		head = g_head.sim;
		source = "sim";
	}

	if(std::strcmp(source, status.source) != 0) {
		gameevent::Event("vr.head")("source", source)("was", status.source);
		// A new head (a session began, a simulated pose replaced the headset) is measured from where it is
		// now; tracking that comes back after a gap keeps its reference
		if(std::strcmp(status.source, "held") != 0) {
			resetHeadReference((std::string("source ") + source).c_str());
		}
		status.source = source;
	}

	if(std::strcmp(source, "none") == 0) {
		status.clamped = false;
		resetBody(bodyYaw);
		return false;
	}

	if(g_head.resetPending || !status.referenced) {
		status.reference = makeHeadReference(head);
		status.referenced = true;
		status.references++;
		g_head.resetPending = false;
		// The new forward is where the body faces (#25)
		resetBody(bodyYaw);
		gameevent::Event("vr.head.reference")("reason", g_head.resetReason)("source", source)
			("yaw", status.reference.yaw, 1)("bodyYaw", bodyYaw, 1)
			("x", status.reference.position.x, 3)("y", status.reference.position.y, 3)
			("z", status.reference.position.z, 3);
	}

	HeadCamera camera = headToCamera(head, status.reference, bodyYaw);

	if(camera.clamped != status.clamped) {
		// An episode, not a frame: logged when the lean reaches the interim limit and when it is back within
		gameevent::Event("vr.head.lean", 100)("state", camera.clamped ? "clamped" : "free")
			("limitM", HeadCameraParams().maxLeanMeters, 2);
	}

	status.head = headAngles(head.orientation);
	status.position = head.position;
	status.offset = camera.offset;
	status.clamped = camera.clamped;

	updateBody(wrapSignedDegrees(status.head.getYaw() - status.reference.yaw), bodyYaw, camera.angle.getYaw());

	position += camera.offset;
	angle = camera.angle;

	return true;
}

void resetHeadReference(const char * reason) {
	g_head.resetPending = true;
	g_head.resetReason = reason;
	// An open menu's panel follows the recenter to where the player looks now (F-180)
	recenterPanel();
}

void setSimulatedHead(const HeadSample & head) {
	g_head.sim = head;
	g_head.haveSim = true;
}

void clearSimulatedHead() {
	g_head.haveSim = false;
}

const HeadStatus & headStatus() {
	return g_head.status;
}

const BodyStatus & bodyStatus() {
	return g_head.body;
}

} // namespace vr
