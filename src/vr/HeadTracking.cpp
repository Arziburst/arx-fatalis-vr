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

#include <glm/glm.hpp>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "vr/BodyYaw.h"
#include "vr/HeadLean.h"
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

	// Real steps (#26): the step asked of the body, in the world, and the turn it was asked with
	Vec2f step = Vec2f(0.f);
	bool stepWanted = false;
	Vec2f stepAsked = Vec2f(0.f); //!< the step the player's movement took, until it answers
	float stepTurn = 0.f;
	PlatformInstant lastStepMove;
	float episodeWalked = 0.f;    //!< meters walked in the current stepping episode
	float farFade = 0.f;          //!< the far fade of the last applyHeadPose
	bool haveHead = false;        //!< the last applyHeadPose had a head

	FadeStatus fade;

};

HeadTrackingState g_head;

//! A stepping episode ends when the body has not moved after the head for this long
const PlatformDuration StepEpisodeGap = 500ms;

//! A step the collision cut to less than this part is a blocked body (game units below 1 are noise)
const float BlockedPart = 0.5f;
const float MinStepUnits = 1.f;

LeanParams leanParams() {
	LeanParams params;
	params.followRadius = config.video.vrBodyFollowRadius;
	params.farStart = config.video.vrHeadFarStart;
	params.farFull = config.video.vrHeadFarFull;
	return params;
}

void resetLean() {
	g_head.status.anchor = Vec2f(0.f);
	g_head.stepWanted = false;
	g_head.step = Vec2f(0.f);
	g_head.stepAsked = Vec2f(0.f);
}

//! The lean of this frame (#26): what the camera keeps, and the step that brings the body after the head
void updateLean(const Vec3f & moved, float turn) {

	HeadStatus & status = g_head.status;
	const HeadLean lean = leanHead(Vec2f(moved.x, moved.z), status.anchor, leanParams());
	status.lean = lean.lean;
	status.leanDistance = lean.distance;
	g_head.farFade = lean.farFade;

	g_head.step = turnToWorld(lean.step, turn) * HeadCameraParams().unitsPerMeter;
	g_head.stepWanted = glm::length(g_head.step) > 0.f;
	g_head.stepTurn = turn;

	if(!g_head.stepWanted && status.bodyBlocked) {
		// Back within the radius: nothing holds the body any more
		status.bodyBlocked = false;
		gameevent::Event("vr.body.blocked", 100)("state", "end")("leanM", lean.distance, 3);
	}

	if(status.stepping && g_platformTime.frameStart() - g_head.lastStepMove > StepEpisodeGap) {
		status.stepping = false;
		gameevent::Event("vr.body.step", 100)("state", "end")("walkedM", g_head.episodeWalked, 3)
			("leanM", lean.distance, 3);
	}
}

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
		resetBody(bodyYaw);
		resetLean();
		status.lean = Vec2f(0.f);
		status.leanDistance = 0.f;
		g_head.farFade = 0.f;
		g_head.haveHead = false;
		return false;
	}
	g_head.haveHead = true;

	if(g_head.resetPending || !status.referenced) {
		status.reference = makeHeadReference(head);
		status.referenced = true;
		status.references++;
		g_head.resetPending = false;
		// The new forward is where the body faces (#25), the head is over the body again (#26)
		resetBody(bodyYaw);
		resetLean();
		gameevent::Event("vr.head.reference")("reason", g_head.resetReason)("source", source)
			("yaw", status.reference.yaw, 1)("bodyYaw", bodyYaw, 1)
			("x", status.reference.position.x, 3)("y", status.reference.position.y, 3)
			("z", status.reference.position.z, 3);
	}

	// The body's part of the head's movement (#26) is the anchor the camera leaves out
	HeadCamera camera = headToCamera(head, status.reference, bodyYaw, status.anchor);
	updateLean(camera.moved, bodyYaw - status.reference.yaw);

	status.head = headAngles(head.orientation);
	status.position = head.position;
	status.offset = camera.offset;

	updateBody(wrapSignedDegrees(status.head.getYaw() - status.reference.yaw), bodyYaw, camera.angle.getYaw());

	position += camera.offset;
	angle = camera.angle;

	return true;
}

void resetHeadReference(const char * reason) {
	g_head.resetPending = true;
	g_head.resetReason = reason;
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

Vec2f takeBodyStep() {
	if(!g_head.stepWanted) {
		return Vec2f(0.f);
	}
	g_head.stepWanted = false;
	g_head.stepAsked = g_head.step;
	return g_head.step;
}

void bodyStepped(const Vec2f & moved) {

	HeadStatus & status = g_head.status;
	const float unitsPerMeter = HeadCameraParams().unitsPerMeter;
	status.anchor = anchorAfterStep(status.anchor, moved, g_head.stepTurn, unitsPerMeter);

	const float asked = glm::length(g_head.stepAsked);
	const float walked = glm::length(moved);
	g_head.stepAsked = Vec2f(0.f);

	if(walked > 0.f) {
		status.walked += walked / unitsPerMeter;
		g_head.episodeWalked += walked / unitsPerMeter;
		g_head.lastStepMove = g_platformTime.frameStart();
		if(!status.stepping) {
			status.stepping = true;
			status.steps++;
			g_head.episodeWalked = walked / unitsPerMeter;
			// An episode, not a frame: the body starts walking after the head (it ends after a pause)
			gameevent::Event("vr.body.step", 100)("state", "start")("leanM", status.leanDistance, 3);
		}
	}

	if(asked >= MinStepUnits) {
		const bool blocked = walked < asked * BlockedPart;
		if(blocked != status.bodyBlocked) {
			if(blocked) {
				status.blocks++;
			}
			// The collision holds the body (a wall, a table, a corner) while the head goes on - or lets it go
			gameevent::Event("vr.body.blocked", 100)("state", blocked ? "start" : "end")
				("askedCm", asked, 1)("movedCm", walked, 1)("leanM", status.leanDistance, 3);
			status.bodyBlocked = blocked;
		}
	}
}

void updateWallFade(const char * blocked, float seconds) {

	FadeStatus & fade = g_head.fade;

	if(!g_head.haveHead) {
		// No head, no fade: flat play is never darkened
		fade = FadeStatus{ 0.f, nullptr, fade.walls, 0.f, fade.fars };
		return;
	}

	const bool wasWall = (fade.wall != nullptr);
	if((blocked != nullptr) != wasWall || (blocked && std::strcmp(blocked, fade.wall) != 0)) {
		if(blocked && !wasWall) {
			fade.walls++;
		}
		// An episode, not a frame: the head went into geometry (inside a wall, or behind one seen from the
		// character's eye), or came out
		gameevent::Event("vr.head.wall", 100)("state", blocked ? blocked : "clear")
			("leanM", g_head.status.leanDistance, 3)("fade", fade.fade, 2);
	}
	fade.wall = blocked;

	const bool tooFar = (g_head.farFade > 0.f);
	if(tooFar != (fade.farFade > 0.f)) {
		if(tooFar) {
			fade.fars++;
		}
		// The head is so far from a body that cannot follow that the world fades
		gameevent::Event("vr.head.far", 100)("state", tooFar ? "start" : "end")
			("leanM", g_head.status.leanDistance, 3)("startM", config.video.vrHeadFarStart, 2);
	}
	fade.farFade = g_head.farFade;

	FadeParams params;
	params.outSeconds = config.video.vrWallFadeOut;
	params.inSeconds = config.video.vrWallFadeIn;
	const float target = blocked ? 1.f : fade.farFade;
	fade.fade = stepFade(fade.fade, target, seconds, params);
}

const FadeStatus & fadeStatus() {
	return g_head.fade;
}

} // namespace vr
