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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include <glm/glm.hpp>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "platform/Time.h"
#include "vr/BodyYaw.h"
#include "vr/HeadLean.h"
#include "vr/HeadReferenceGate.h"
#include "vr/PhysicalCrouch.h"
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
	bool eyeStanding = false;   //!< \c eyeAboveFeet is the standing eye
	float standingEye = -1.f;   //!< the character's last standing eye above its feet, game units; -1 unknown

	float raising = 0.f; //!< meters the reference was raised in the current auto-raise episode

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

void setCharacterEyeHeight(float units, bool standing) {
	g_head.eyeAboveFeet = units;
	g_head.eyeStanding = standing;
}

bool physicalCrouch() {
	return g_head.status.crouch;
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
		if(status.crouch) {
			gameevent::Event("vr.head.crouch")("state", "off")("reason", "no head");
		}
		status.crouch = false;
		status.worn = false;
		resetBody(bodyYaw);
		resetLean();
		status.lean = Vec2f(0.f);
		status.leanDistance = 0.f;
		g_head.farFade = 0.f;
		g_head.haveHead = false;
		return false;
	}
	g_head.haveHead = true;

	status.eyeAboveFeet = g_head.eyeAboveFeet;
	if(g_head.eyeStanding && status.eyeAboveFeet >= 0.f) {
		g_head.standingEye = status.eyeAboveFeet;
	}
	g_head.eyeAboveFeet = -1.f;
	g_head.eyeStanding = false;

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
			// The new forward is where the body faces (#25), the head is over the body again (#26)
			resetBody(bodyYaw);
			resetLean();
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

	// Physical crouch (#100, D-018): the head far enough below the calibrated eye crouches the character, back
	// up past the exit depth it stands (the game keeps it crouched under a low ceiling). A held pose keeps the
	// state; a new reference measures from the head where it is now, so a recenter stands up. A headset taken
	// off and put down on the desk is not a crouching player, nor is one who put it on again seated while the
	// new reference is still settling (the old one is from the standing head)
	status.crouchDepth = status.reference.position.y - head.position.y;
	if(std::strcmp(source, "held") != 0) {
		CrouchParams params;
		params.enabled = config.video.vrPhysicalCrouch && worn && !status.pending;
		params.enterMeters = config.video.vrCrouchEnter;
		params.exitMeters = config.video.vrCrouchExit;
		const bool crouch = updatePhysicalCrouch(status.crouch, status.crouchDepth, params);
		if(crouch != status.crouch) {
			if(crouch) {
				status.crouches++;
			}
			gameevent::Event("vr.head.crouch")("state", crouch ? "on" : "off")("depthM", status.crouchDepth, 3)
				("enterM", params.enterMeters, 2)("exitM", params.exitMeters, 2);
		}
		status.crouch = crouch;
	}

	// The body's part of the head's movement (#26) is the anchor the camera leaves out; the character height
	// setting (#260) applies at once: read every frame
	HeadCameraParams cameraParams;
	cameraParams.heightOffsetMeters = clampHeightOffset(config.input.vrHeightOffset);
	status.heightOffset = cameraParams.heightOffsetMeters;
	HeadCamera camera = headToCamera(head, status.reference, bodyYaw, status.anchor, cameraParams);
	updateLean(camera.moved, bodyYaw - status.reference.yaw);

	// The character's crouch - the head's, the button's - already lowered the eye the camera starts from: the
	// head's own drop moves the camera only below that (#100), so the view is never twice as low
	status.characterDrop = 0.f;
	if(g_head.standingEye >= 0.f && status.eyeAboveFeet >= 0.f) {
		status.characterDrop = std::max(0.f, g_head.standingEye - status.eyeAboveFeet);
	}
	camera.offset.y = headDropOnCamera(camera.offset.y, status.characterDrop);

	status.head = headAngles(head.orientation);
	status.position = head.position;
	status.aboveFloor = (localAboveFloor >= 0.f) ? localAboveFloor + head.position.y : -1.f;
	status.offset = camera.offset;

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

bool peekHead(HeadSample & head) {
	// The same choice as applyHeadPose, without its bookkeeping
	const FrameViews & views = frameViews();
	if(isEnabled() && views.valid) {
		head = headFromEyes(views.eyes[0].position, views.eyes[0].orientation,
		                    views.eyes[1].position, views.eyes[1].orientation);
		return true;
	}
	if(isEnabled() && g_head.haveHmd) {
		head = g_head.lastHmd;
		return true;
	}
	if(g_head.haveSim) {
		head = g_head.sim;
		return true;
	}
	return false;
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
