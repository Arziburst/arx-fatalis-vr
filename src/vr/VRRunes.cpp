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

#include "vr/VRRunes.h"

#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "core/TimeTypes.h"
#include "vr/HeadTracking.h"
#include "vr/VRBody.h"
#include "vr/VRInput.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

struct RunesState {
	RuneStroke stroke;
	RuneTrailStatus status;
	bool stance = false;
};

RunesState g_runes;

} // anonymous namespace

bool handsDrawRunes() {
	if(isEnabled()) {
		return true;
	}
	HandSample sample;
	return controllerSample(Hand::Left, sample) || controllerSample(Hand::Right, sample);
}

const RuneStroke & updateRuneTrail(bool stance, const Vec3f & headCamera, float bodyYaw) {

	RuneTrailStatus & status = g_runes.status;
	status.handsDraw = handsDrawRunes();

	// The stance as a state change: the button is held for seconds, the event says when it began and ended
	if(stance != g_runes.stance) {
		g_runes.stance = stance;
		gameevent::Event("magic.stance", 200)("state", stance ? "on" : "off")("hands", status.handsDraw);
	}
	status.stance = stance;

	// Each hand's point: the controller placed rigid with the head camera, the glow ahead of the grip
	std::array<StrokeHand, HandCount> hands;
	HeadSample head;
	const HeadStatus & headState = headStatus();
	const bool haveHead = status.handsDraw && peekHead(head) && headState.referenced;
	const ControlResolver & input = controls();
	for(size_t hand = 0; hand < HandCount; hand++) {
		StrokeHand & current = hands[hand];
		current.trigger = input.active(Hand(hand) == Hand::Left ? Event::InteractLeft : Event::InteractRight);
		HandSample sample;
		if(haveHead && controllerSample(Hand(hand), sample)) {
			current.located = true;
			current.point = runeTrailPoint(headCamera, handToWorld(sample, head, headState.reference, bodyYaw));
		}
		status.located[hand] = current.located;
		status.points[hand] = current.point;
	}

	// Only the hands draw in VR: without them nothing is drawn, the engine's cursor trail stays off
	RuneStroke & stroke = g_runes.stroke;
	stroke.update(toMsi(g_platformTime.frameStart()), stance && status.handsDraw, hands);

	if(stroke.started()) {
		gameevent::Event("magic.stroke", 500)("state", "start")("hand", handName(stroke.hand()));
	} else if(stroke.ended() != StrokeEnd::None) {
		gameevent::Event("magic.stroke", 500)("state", "end")("hand", handName(stroke.hand()))
			("reason", strokeEndName(stroke.ended()))("lengthCm", stroke.length(), 1)("ms", stroke.durationMs());
	}

	status.drawing = stroke.drawing();
	status.hand = stroke.strokes() ? handName(stroke.hand()) : "none";
	status.strokes = stroke.strokes();
	status.length = stroke.length();
	status.durationMs = stroke.durationMs();
	status.lastEnd = strokeEndName(stroke.lastEnd());
	status.point = stroke.point();
	for(size_t hand = 0; hand < HandCount; hand++) {
		status.ignored[hand] = stroke.ignored(Hand(hand));
	}

	return stroke;
}

const RuneStroke & runeStroke() {
	return g_runes.stroke;
}

void noteRuneFlares(size_t count, const Vec3f & point) {
	if(count == 0) {
		return;
	}
	g_runes.status.flares += count;
	g_runes.status.lastFlare = point;
}

const RuneTrailStatus & runeTrailStatus() {
	return g_runes.status;
}

} // namespace vr
