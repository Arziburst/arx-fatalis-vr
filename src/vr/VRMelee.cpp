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

#include "animation/Animation.h"
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
	StrikeRecovery recovery;         //!< the weapon hand's, after a hit (#323)
	RecoveryTime recoveryTime;       //!< of the weapon type in the hand
	std::string recoveryNoted;       //!< the last melee.recovery event's values
	bool ownsAim = false;            //!< this module set player.m_aimTime
	// The swing in progress
	SwingKind swingKind = SwingKind::None;
	float swingCharge = 0.f;         //!< the charge ratio when it started
	float swingAim = -1.f;           //!< the aim ratio fixed by its first contact, -1 before
	bool swingCharged = false;
	bool swingRecovering = false;    //!< it started within the recovery: it hits nothing
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

MeleeWeapon meleeWeapon() {
	switch(ARX_EQUIPMENT_GetPlayerWeaponType()) {
		case WEAPON_DAGGER: return MeleeWeapon::Dagger;
		case WEAPON_2H: return MeleeWeapon::TwoHanded;
		case WEAPON_1H:
		case WEAPON_BARE:
		case WEAPON_BOW: break; // bare hands and bows are never in the IK hand
	}
	return MeleeWeapon::OneHanded;
}

/*!
 * The game's own strike for a weapon type, seconds: the player's strike start and strike animations - what the
 * attack button plays from the weapon at rest to the weapon at rest (Core.cpp), in the default (left) direction,
 * without the charge held in between; 0 while they are not loaded. \a anims gets their file names
 */
float gameStrikeSeconds(MeleeWeapon weapon, std::string * anims = nullptr) {
	AnimationNumber start = ANIM_1H_STRIKE_LEFT_START;
	AnimationNumber strike = ANIM_1H_STRIKE_LEFT;
	switch(weapon) {
		case MeleeWeapon::Dagger: {
			start = ANIM_DAGGER_STRIKE_LEFT_START;
			strike = ANIM_DAGGER_STRIKE_LEFT;
			break;
		}
		case MeleeWeapon::OneHanded: break;
		case MeleeWeapon::TwoHanded: {
			start = ANIM_2H_STRIKE_LEFT_START;
			strike = ANIM_2H_STRIKE_LEFT;
			break;
		}
	}
	const Entity * io = entities.player();
	if(!io) {
		return 0.f;
	}
	float seconds = 0.f;
	for(AnimationNumber number : { start, strike }) {
		const ANIM_HANDLE * anim = io->anims[number];
		if(!anim || anim->anims.empty() || !anim->anims[0]) {
			return 0.f;
		}
		seconds += toS(anim->anims[0]->anim_time);
		if(anims) {
			*anims += (anims->empty() ? "" : "+") + std::string(anim->path.filename());
		}
	}
	return seconds;
}

float configuredRecovery(MeleeWeapon weapon) {
	switch(weapon) {
		case MeleeWeapon::Dagger: return config.input.vrMeleeRecoveryDagger;
		case MeleeWeapon::OneHanded: return config.input.vrMeleeRecovery1h;
		case MeleeWeapon::TwoHanded: return config.input.vrMeleeRecovery2h;
	}
	return -1.f;
}

//! The recovery of the weapon type in the hand, into the state and the status; a melee.recovery event when it changes
void updateRecoveryTime() {
	MeleeState & m = g_melee;
	MeleeStatus & s = m.status;
	const MeleeWeapon weapon = meleeWeapon();
	std::array<std::string, 3> anims;
	for(MeleeWeapon type : { MeleeWeapon::Dagger, MeleeWeapon::OneHanded, MeleeWeapon::TwoHanded }) {
		s.gameStrike[size_t(type)] = gameStrikeSeconds(type, &anims[size_t(type)]);
	}
	const float game = s.gameStrike[size_t(weapon)];
	m.recoveryTime = recoveryTime(weapon, configuredRecovery(weapon), game);
	s.weapon = meleeWeaponName(weapon);
	s.recovery = m.recoveryTime.seconds;
	s.recoverySource = recoverySourceName(m.recoveryTime.source);
	s.recoveryGame = game;
	const std::string noted = std::string(s.weapon) + ' ' + std::to_string(s.recovery) + ' ' + s.recoverySource;
	if(noted != m.recoveryNoted) {
		m.recoveryNoted = noted;
		// The animations of every type: what the game's timing was read from
		gameevent::Event("melee.recovery", 200)("weapon", s.weapon)("seconds", s.recovery, 2)
			("source", s.recoverySource)("game", game, 2)
			("dagger", anims[size_t(MeleeWeapon::Dagger)].empty() ? "-" : anims[size_t(MeleeWeapon::Dagger)])
			("1h", anims[size_t(MeleeWeapon::OneHanded)].empty() ? "-" : anims[size_t(MeleeWeapon::OneHanded)])
			("2h", anims[size_t(MeleeWeapon::TwoHanded)].empty() ? "-" : anims[size_t(MeleeWeapon::TwoHanded)]);
	}
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

	// Within the recovery after a hit the swing is no strike: it hits nothing, even once the recovery is over (#323)
	m.swingRecovering = !m.recovery.ready(m.swingStart);
	if(m.swingRecovering) {
		m.status.recoverySwings++;
		return;
	}

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
	const char * result = change == SwingChange::Lost ? "lost"
	                      : m.swingRecovering ? "recovering"
	                      : m.swingHits > 0 ? "hit" : "miss";
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
	m.recovery.hit(now, m.recoveryTime.seconds);

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
	updateRecoveryTime();

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
	if(m.detector.swinging() && s.placed && !m.swingRecovering) {
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
	s.recoveryLeft = m.recovery.remaining(now);
	s.recovering = m.swingRecovering;
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
