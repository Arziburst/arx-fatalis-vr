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

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "platform/ProgramOptions.h"
#include "core/TimeTypes.h"
#include "game/magic/SpellRecognition.h"
#include "vr/HeadTracking.h"
#include "vr/VRBody.h"
#include "vr/VRInput.h"
#include "vr/VRSystem.h"

//! Where the flying spells go from the command line ("view" or "hand"; empty: cfg.ini's vr_spell_aim decides)
static std::string g_spellAimOverride;

static void setSpellAim(const std::string & mode) {
	g_spellAimOverride = mode;
}
ARX_PROGRAM_OPTION_ARG("vr-spell-aim", nullptr,
                       "Where the spells that fly go in VR: view (the centre of the view) or hand (overrides vr_spell_aim in cfg.ini)",
                       &setSpellAim, "MODE")

namespace vr {

namespace {

struct RunesState {
	RuneStroke stroke;
	RuneSampler sampler;
	RuneTrailStatus status;
	bool stance = false;
	std::array<bool, HandCount> aimLocated = { { false, false } };
	std::array<Anglef, HandCount> aims = { };
};

RunesState g_runes;

//! The hand's point this frame goes into the sampler; what it emits goes into the engine's point list
void feedRecogniser(RuneSampler & sampler, const Vec3f & point) {
	const size_t added = sampler.add(point);
	const std::vector<Vec2s> & points = sampler.points();
	for(size_t i = points.size() - added; i < points.size(); i++) {
		ARX_SPELLS_AddPoint(points[i]);
	}
}

} // anonymous namespace

bool handsDrawRunes() {
	if(isEnabled()) {
		return true;
	}
	HandSample sample;
	return controllerSample(Hand::Left, sample) || controllerSample(Hand::Right, sample);
}

const RuneStroke & updateRuneTrail(bool stance, const Vec3f & headCamera, float bodyYaw, float lookYaw) {

	RuneTrailStatus & status = g_runes.status;
	status.handsDraw = handsDrawRunes();

	// The stance as a state change: the button is held for seconds, the event says when it began and ended
	if(stance != g_runes.stance) {
		g_runes.stance = stance;
		gameevent::Event("magic.stance", 200)("state", stance ? "on" : "off")("hands", status.handsDraw);
	}
	status.stance = stance;

	// Each hand's point: the controller placed rigid with the head camera, the glow ahead of the grip; and
	// where its axis points, for the spells (F-75)
	std::array<StrokeHand, HandCount> hands;
	HeadSample head;
	const HeadStatus & headState = headStatus();
	const bool haveHead = status.handsDraw && peekHead(head) && headState.referenced;
	const ControlResolver & input = controls();
	for(size_t hand = 0; hand < HandCount; hand++) {
		StrokeHand & current = hands[hand];
		current.trigger = input.active(Hand(hand) == Hand::Left ? Event::InteractLeft : Event::InteractRight);
		HandSample sample;
		g_runes.aimLocated[hand] = false;
		if(haveHead && controllerSample(Hand(hand), sample)) {
			const HandPlacement placement = handToWorld(sample, head, headState.reference, bodyYaw);
			current.located = true;
			current.point = runeTrailPoint(headCamera, placement);
			g_runes.aims[hand] = aimFromPlacement(placement);
			g_runes.aimLocated[hand] = true;
		}
		status.located[hand] = current.located;
		status.points[hand] = current.point;
		status.aims[hand] = g_runes.aims[hand];
	}

	// Only the hands draw in VR: without them nothing is drawn, the engine's cursor trail stays off
	RuneStroke & stroke = g_runes.stroke;
	stroke.update(toMsi(g_platformTime.frameStart()), stance && status.handsDraw, hands);

	// The recogniser's points (#44): the stroke projected onto the plane of the look at its start, one
	// point per step along the path, into the engine's own list in place of the cursor's
	RuneSampler & sampler = g_runes.sampler;
	if(stroke.started()) {
		sampler.begin(makeRunePlane(lookYaw, stroke.point()));
		spellRecognitionPointsReset();
		feedRecogniser(sampler, stroke.point());
	} else if(stroke.drawing()) {
		feedRecogniser(sampler, stroke.point());
	}

	if(stroke.started()) {
		gameevent::Event("magic.stroke", 500)("state", "start")("hand", handName(stroke.hand()))
			("planeYaw", sampler.plane().yaw, 1);
	} else if(stroke.ended() != StrokeEnd::None) {
		// The view gate (F-202 rule 1): a stroke behind the look, at the knee or over the head is no rune -
		// the engine gets nothing to analyse, and the log says why with the numbers
		const RuneGateResult gate = gateRuneStroke(headCamera, sampler.plane().yaw, sampler.centre());
		status.gate = runeGateName(gate.gate);
		gameevent::Event("magic.stroke", 500)("state", "end")("hand", handName(stroke.hand()))
			("reason", strokeEndName(stroke.ended()))("lengthCm", stroke.length(), 1)("ms", stroke.durationMs())
			("points", u32(sampler.points().size()))("gate", status.gate);
		if(gate.gate != RuneGate::Ok) {
			spellRecognitionPointsReset();
			status.refused++;
			gameevent::Event("magic.stroke.refused", 200)("hand", handName(stroke.hand()))("reason", status.gate)
				("offYaw", gate.offYawDegrees, 1)("belowHeadM", gate.belowHeadMeters, 2)
				("points", u32(sampler.points().size()));
		}
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
	status.recognizerPoints = u32(sampler.points().size());
	status.planeYaw = sampler.plane().yaw;
	status.size2D = sampler.size();
	status.aim = spellAim(Anglef(0.f, lookYaw, 0.f));

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

SpellAim spellAim(const Anglef & view) {

	SpellAim aim;
	aim.angle = view;
	aim.source = "view";
	aim.hand = g_runes.stroke.strokes() ? g_runes.stroke.hand() : Hand::Right;

	// The default (D-NEW, owner 2026-10-10): the spell flies where the centre of the view looks; the hand is opt-in
	if(!g_runes.status.handsDraw || (g_spellAimOverride.empty() ? config.video.vrSpellAim : g_spellAimOverride) != "hand") {
		return aim;
	}

	// The casting hand is the one that drew the last stroke; if the tracking lost it, the other one
	const Hand other = (aim.hand == Hand::Right) ? Hand::Left : Hand::Right;
	for(Hand hand : { aim.hand, other }) {
		if(g_runes.aimLocated[size_t(hand)]) {
			aim.angle = g_runes.aims[size_t(hand)];
			aim.source = "hand";
			aim.hand = hand;
			return aim;
		}
	}

	return aim;
}

const RuneTrailStatus & runeTrailStatus() {
	return g_runes.status;
}

} // namespace vr
