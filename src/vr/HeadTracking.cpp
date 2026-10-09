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
#include "platform/Time.h"
#include "vr/BodyYaw.h"
#include "vr/HeadReferenceGate.h"
#include "vr/VRSystem.h"
#include "vr/WorldScale.h"

namespace vr {

namespace {

struct HeadTrackingState {

	bool haveHmd = false;   //!< a headset sample was seen in this session
	HeadSample lastHmd;

	bool haveSim = false;
	HeadSample sim;

	HeadReferenceGate gate;
	std::string resetReason = "startup";

	float eyeAboveFeet = -1.f;

	float raising = 0.f; //!< meters the reference was raised in the current auto-raise episode

	HeadStatus status;
	BodyStatus body;

};

HeadTrackingState g_head;

double nowSeconds() {
	static const PlatformInstant start = platform::getTime();
	return double(toUs(platform::getTime() - start)) / 1e6;
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

void setCharacterEyeHeight(float units) {
	g_head.eyeAboveFeet = units;
}

bool applyHeadPose(Vec3f & position, Anglef & angle, float bodyYaw) {

	HeadStatus & status = g_head.status;
	const char * source = "none";
	HeadSample head;
	// The headset is on a head: the session is displayed (visible or focused) - an idle headset never is.
	// A simulated head always counts as worn
	bool worn = false;
	// The LOCAL origin above the room's floor (STAGE), meters; negative while unknown
	float localAboveFloor = -1.f;

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
		worn = isWorn();
		if(views.floorValid) {
			localAboveFloor = views.localAboveFloor;
		}
	} else if(isEnabled() && g_head.haveHmd) {
		head = g_head.lastHmd;
		source = "held";
	} else if(g_head.haveSim) {
		head = g_head.sim;
		source = "sim";
		worn = true;
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
		status.worn = false;
		resetBody(bodyYaw);
		return false;
	}

	status.eyeAboveFeet = g_head.eyeAboveFeet;
	g_head.eyeAboveFeet = -1.f;

	// The reference is the worn, settled head (#27): a held pose (tracking lost) feeds nothing into the gate
	if(std::strcmp(source, "held") != 0) {
		const double now = nowSeconds();
		if(worn != status.worn) {
			gameevent::Event("vr.head.worn")("state", worn ? "on" : "off")("session", sessionStateString())
				("source", source);
			if(!worn) {
				// The gate takes a new reference when it is put on again: say so in that reference's event
				g_head.resetReason = "put on again";
			}
		}
		ReferenceKind take = g_head.gate.update(now, head.position, worn, std::strcmp(source, "sim") == 0);
		if(take != ReferenceKind::None) {
			status.reference = makeHeadReference(head);
			status.referenceKind = take;
			status.referenced = true;
			status.references++;
			status.reason = g_head.resetReason;
			status.raised = 0.f;
			g_head.raising = 0.f;
			// The new forward is where the body faces (#25)
			resetBody(bodyYaw);
			status.referenceAboveFloor = (localAboveFloor >= 0.f) ? localAboveFloor + head.position.y : -1.f;
			// The head at the reference is the character's eye: the player's eye height above the floor
			// (STAGE, when the runtime has one) against the character's eye above its feet, in meters at the
			// world scale - what decides whether doors and tables keep their size
			gameevent::Event e("vr.head.reference");
			e("kind", referenceKindName(take))("reason", g_head.resetReason)("source", source)
				("wornS", float(g_head.gate.wornSeconds(now)), 2)
				("yaw", status.reference.yaw, 1)("bodyYaw", bodyYaw, 1)
				("x", status.reference.position.x, 3)("y", status.reference.position.y, 3)
				("z", status.reference.position.z, 3);
			if(status.referenceAboveFloor >= 0.f) {
				e("headAboveFloorM", status.referenceAboveFloor, 3);
			} else {
				e("headAboveFloorM", "unknown");
			}
			if(status.eyeAboveFeet >= 0.f) {
				e("characterEyeM", status.eyeAboveFeet / UnitsPerMeter, 3);
			} else {
				e("characterEyeM", "unknown");
			}
		}
		status.worn = worn;
		status.pending = g_head.gate.pending();
	}

	// Auto-raise (D-018): a head above the reference lifts it, so the view never rises above the character's
	// eye. Logged per episode - from the first raise until the head is back 2 cm below the reference
	const float raised = raiseHeadReference(status.reference, head);
	if(raised > 0.f) {
		g_head.raising += raised;
		status.raised += raised;
	} else if(g_head.raising > 0.f && head.position.y < status.reference.position.y - 0.02f) {
		if(g_head.raising >= 0.01f) {
			gameevent::Event("vr.head.raised", 100)("byM", g_head.raising, 3)("sinceRefM", status.raised, 3)
				("refY", status.reference.position.y, 3)("kind", referenceKindName(status.referenceKind));
		}
		g_head.raising = 0.f;
	}

	HeadCamera camera = headToCamera(head, status.reference, bodyYaw);

	if(camera.clamped != status.clamped) {
		// An episode, not a frame: logged when the lean reaches the interim limit and when it is back within
		gameevent::Event("vr.head.lean", 100)("state", camera.clamped ? "clamped" : "free")
			("limitM", HeadCameraParams().maxLeanMeters, 2);
	}

	status.head = headAngles(head.orientation);
	status.position = head.position;
	status.aboveFloor = (localAboveFloor >= 0.f) ? localAboveFloor + head.position.y : -1.f;
	status.offset = camera.offset;
	status.clamped = camera.clamped;

	updateBody(wrapSignedDegrees(status.head.getYaw() - status.reference.yaw), bodyYaw, camera.angle.getYaw());

	position += camera.offset;
	angle = camera.angle;

	return true;
}

void resetHeadReference(const char * reason) {
	g_head.gate.reset();
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
