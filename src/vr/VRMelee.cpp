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

#include "vr/VRMelee.h"

#include <algorithm>
#include <string_view>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Player.h"
#include "gui/Speech.h"
#include "io/log/Logger.h"
#include "script/Script.h"
#include "vr/HeadTracking.h"
#include "vr/VRBody.h"
#include "vr/VRInput.h"
#include "vr/VRWeapon.h"

namespace vr {

namespace {

//! A motion that peaks this close below the swing speed is a near miss (F-60)
const float NearMissShare = 0.8f;

struct MeleeState {
	SwingDetector detector;
	StrikeCharge charge;
	TargetCooldown cooldown;
	bool ownsAim = false;            //!< this module set player.m_aimTime
	// The swing in progress
	SwingKind swingKind = SwingKind::None;
	float swingCharge = 0.f;         //!< the charge ratio when it started
	float swingAim = -1.f;           //!< the aim ratio fixed by its first contact, -1 before
	bool swingCharged = false;
	u64 swingHits = 0;
	double swingStart = 0.;
	float humpPeak = 0.f;            //!< the peak of a motion that has not become a swing yet
	bool chargeFullNoted = false;
	std::string refusedBefore = "-";
	MeleeStatus status;
};

MeleeState g_melee;

double meleeClock() {
	return double(toUs(g_platformTime.frameStart() - PlatformInstant(0))) * 1e-6;
}

SwingParams paramsFromConfig() {
	SwingParams params;
	params.startSpeed = config.input.vrMeleeSwingSpeed;
	params.endSpeed = std::min(params.endSpeed, params.startSpeed * 0.6f);
	params.fullSpeed = std::max(config.input.vrMeleeFullSpeed, params.startSpeed + 0.01f);
	params.weakAim = config.input.vrMeleeWeakAim;
	params.targetCooldown = config.input.vrMeleeTargetCooldown;
	return params;
}

const char * weaponTypeName() {
	switch(ARX_EQUIPMENT_GetPlayerWeaponType()) {
		case WEAPON_DAGGER: return "dagger";
		case WEAPON_1H: return "1h";
		case WEAPON_2H: return "2h";
		case WEAPON_BOW: return "bow";
		case WEAPON_BARE: return "bare";
	}
	return "bare";
}

float fullAimSeconds() {
	const float seconds = toS(player.Full_AimTime);
	return seconds > 0.f ? seconds : 1.5f;
}

void noteRefusal(const char * refused) {
	const std::string now = refused ? refused : "";
	if(now == g_melee.refusedBefore) {
		return;
	}
	g_melee.refusedBefore = now;
	gameevent::Event("melee.state", 200)("state", refused ? "off" : "on")("reason", refused ? refused : "allowed");
}

void startSwing(Entity * weapon) {

	MeleeState & m = g_melee;
	m.swingKind = m.detector.kind();
	m.swingCharge = m.charge.ratio(fullAimSeconds());
	m.swingAim = -1.f;
	m.swingCharged = false;
	m.swingHits = 0;
	m.swingStart = meleeClock();
	m.humpPeak = 0.f;

	// What the engine does when a strike starts (Core.cpp, the release of the attack button)
	Entity * io = entities.player();
	io->isHit = false; // the wall sound and the durability loss once per swing
	ARX_PLAYER_Remove_Invisibility();
	SendIOScriptEvent(nullptr, io, SM_STRIKE, weaponTypeName());
	if(m.swingCharge > 0.8f) {
		std::string_view speech = io->strikespeech;
		if(!weapon->strikespeech.empty()) {
			speech = weapon->strikespeech;
		}
		ARX_SPEECH_AddSpeech(*io, speech, ANIM_TALK_NEUTRAL, ARX_SPEECH_FLAG_NOTEXT);
	}
}

void endSwing(SwingChange change) {
	MeleeState & m = g_melee;
	const SwingDetector & d = m.detector;
	const char * result = change == SwingChange::Lost ? "lost" : m.swingHits > 0 ? "hit" : "miss";
	gameevent::Event("melee.swing", 1000)("kind", swingKindName(m.swingKind))("result", result)
		("peak", d.peak(), 2)("factor", d.factor(), 2)("charge", m.swingCharge, 2)
		("aim", m.swingAim >= 0.f ? m.swingAim : 0.f, 2)("charged", m.swingCharged ? "yes" : "no")
		("hits", m.swingHits)("ms", int((meleeClock() - m.swingStart) * 1000.));
}

void contact(Entity * weapon, double now) {

	MeleeState & m = g_melee;
	MeleeStatus & s = m.status;
	const SwingParams & params = m.detector.params();
	const u64 swing = m.detector.swings();

	StrikeTargets targets;
	for(size_t target : m.cooldown.blocked(swing, now, params.targetCooldown)) {
		targets.skip.push_back(EntityHandle(target));
	}

	const float chargeRatio = m.charge.ratio(fullAimSeconds());
	const float aim = m.swingAim >= 0.f ? m.swingAim : strikeAim(chargeRatio, m.detector.factor(), params);
	ARX_EQUIPMENT_Strike_Check(entities.player(), weapon, aim, 0, EntityHandle(), &targets);
	if(targets.touched.empty()) {
		return;
	}

	if(m.swingAim < 0.f) {
		// The first contact fixes the swing's strength; a charged one spends the charge
		m.swingAim = aim;
		m.swingCharged = chargeRatio > params.weakAim;
		if(m.swingCharged) {
			m.charge.spend();
			s.chargedHits++;
			gameevent::Event("melee.charge", 1000)("state", "spent")("ratio", chargeRatio, 2);
		}
	}

	for(const StrikeContact & touched : targets.touched) {
		m.cooldown.hit(size_t(touched.target.handleData()), swing, now);
		s.contacts++;
		if(!touched.npc) {
			continue;
		}
		m.swingHits++;
		s.hits++;
		s.lastTarget = gameevent::entityName(entities.get(touched.target));
		s.lastAim = m.swingAim;
		s.lastDamage = touched.damage;
		s.lastCharged = m.swingCharged;
		s.lastSpeed = m.detector.peak();
		s.lastSwing = swing;
	}
}

} // anonymous namespace

void updateMelee(const char * refusal) {

	MeleeState & m = g_melee;
	MeleeStatus & s = m.status;

	m.detector.setParams(paramsFromConfig());
	s.params = m.detector.params();
	s.fullAimSeconds = fullAimSeconds();

	Entity * weapon = weaponInHand();
	const PlayerArmStatus & arm = playerBodyStatus().hands[size_t(Hand::Right)];
	HeadSample head;
	const bool haveHead = peekHead(head);
	const char * refused = refusal;
	if(!refused && !weapon) {
		refused = "no weapon in the hand";
	} else if(!refused && !arm.tracked) {
		refused = "the right controller is not tracked";
	} else if(!refused && !haveHead) {
		refused = "no head";
	}
	noteRefusal(refused);

	if(refused) {
		if(m.detector.swinging()) {
			endSwing(SwingChange::Lost);
		}
		m.detector.reset();
		m.charge.update(false, 0.f);
		if(m.ownsAim) {
			player.m_aimTime = 0;
			m.ownsAim = false;
		}
		s.active = false;
		s.refused = refused;
		s.swinging = false;
		s.placed = false;
		s.chargeHeld = s.charging = s.chargeSpent = false;
		s.chargeSeconds = s.chargeRatio = 0.f;
		return;
	}
	s.active = true;
	s.refused.clear();

	// The charge: the trigger of the weapon hand, the game's own charge time
	const bool held = controllerInput().down[control(Hand::Right, ControlKind::Trigger)];
	const bool wasCharging = m.charge.charging();
	const bool wasHeld = m.charge.held();
	m.charge.update(held, toS(g_platformTime.lastFrameDuration()));
	const float ratio = m.charge.ratio(s.fullAimSeconds);
	if(m.charge.charging() && !wasCharging) {
		m.chargeFullNoted = false;
		gameevent::Event("melee.charge", 1000)("state", "start")("full", s.fullAimSeconds, 2);
	} else if(!m.charge.held() && wasHeld && wasCharging) {
		gameevent::Event("melee.charge", 1000)("state", "released")("ratio", s.chargeRatio, 2);
	}
	if(m.charge.charging() && ratio >= 1.f && !m.chargeFullNoted) {
		m.chargeFullNoted = true;
		gameevent::Event("melee.charge", 1000)("state", "full")("seconds", m.charge.seconds(), 2);
	}
	// The HUD's hit strength gauge shows the charge, as for the engine's strike
	player.m_aimTime = m.charge.charging() ? PlatformDuration(std::chrono::duration<float>(m.charge.seconds()))
	                                       : PlatformDuration(0);
	m.ownsAim = true;

	// The swing: the controller relative to the head, both in the LOCAL space
	const double now = meleeClock();
	SwingSample sample;
	sample.time = now;
	sample.hand = arm.sample.position - head.position;
	sample.orientation = arm.sample.orientation;
	const SwingChange change = m.detector.update(sample, true);
	if(change == SwingChange::Started) {
		startSwing(weapon);
	}

	s.placed = placeHeldWeapon(s.hilt, s.tip);
	if(m.detector.swinging() && s.placed) {
		contact(weapon, now);
	}

	if(change == SwingChange::Ended || change == SwingChange::Lost) {
		endSwing(change);
	}

	// Near misses (F-60): motions that came close to a swing - the instrument to tune the swing speed on
	const SwingParams & params = m.detector.params();
	if(!m.detector.swinging() && change != SwingChange::Ended) {
		if(m.detector.speed() >= params.startSpeed * NearMissShare) {
			m.humpPeak = std::max(m.humpPeak, m.detector.speed());
		} else if(m.detector.speed() < params.endSpeed && m.humpPeak > 0.f) {
			s.nearMisses++;
			s.lastNearMiss = m.humpPeak;
			gameevent::Event("melee.nearMiss", 200)("peak", m.humpPeak, 2)("swingSpeed", params.startSpeed, 2);
			m.humpPeak = 0.f;
		}
	}

	s.swinging = m.detector.swinging();
	s.kind = swingKindName(s.swinging ? m.detector.kind() : m.swingKind);
	s.speed = m.detector.speed();
	s.peak = m.detector.peak();
	s.factor = m.detector.factor();
	s.swings = m.detector.swings();
	s.gaps = m.detector.gaps();
	s.jumps = m.detector.jumps();
	s.chargeHeld = m.charge.held();
	s.charging = m.charge.charging();
	s.chargeSpent = m.charge.spent();
	s.chargeSeconds = m.charge.seconds();
	s.chargeRatio = ratio;
}

bool meleeOwnsStrikes() {
	return weaponInHand() != nullptr;
}

const MeleeStatus & meleeStatus() {
	return g_melee.status;
}

} // namespace vr
