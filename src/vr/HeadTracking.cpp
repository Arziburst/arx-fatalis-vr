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

#include <cstring>
#include <string>

#include "core/GameEvent.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

struct HeadTrackingState {

	bool haveHmd = false;   //!< a headset sample was seen in this session
	HeadSample lastHmd;

	bool haveSim = false;
	HeadSample sim;

	float raising = 0.f;    //!< meters the reference rose in the current auto-raise episode

	bool resetPending = true;
	std::string resetReason = "startup";

	HeadStatus status;

};

HeadTrackingState g_head;

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
		return false;
	}

	if(g_head.resetPending || !status.referenced) {
		status.reference = makeHeadReference(head);
		status.referenced = true;
		status.references++;
		status.reason = g_head.resetReason;
		g_head.resetPending = false;
		g_head.raising = 0.f;
		gameevent::Event("vr.head.reference")("reason", g_head.resetReason)("source", source)
			("yaw", status.reference.yaw, 1)("bodyYaw", bodyYaw, 1)
			("x", status.reference.position.x, 3)("y", status.reference.position.y, 3)
			("z", status.reference.position.z, 3);
	}

	// Auto-raise (D-018): a head above the calibrated eye height takes it up with it. An episode (stood up)
	// is logged once, when the head stops rising, with how far it went
	const float raised = raiseHeadReference(status.reference, head);
	if(raised > 0.f) {
		if(g_head.raising == 0.f) {
			status.raises++;
		}
		g_head.raising += raised;
	} else if(g_head.raising > 0.f) {
		gameevent::Event("vr.head.raise")("source", source)("byM", g_head.raising, 3)
			("y", status.reference.position.y, 3);
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
	status.offset = camera.offset;
	status.clamped = camera.clamped;

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

} // namespace vr
