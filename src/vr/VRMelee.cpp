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
#include <iomanip>
#include <sstream>
#include <string_view>

#include "animation/Animation.h"
#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "gui/Speech.h"
#include "io/log/Logger.h"
#include "script/Script.h"
#include "vr/HeadTracking.h"
#include "vr/VRBody.h"
#include "vr/VRInput.h"
#include "vr/VRSystem.h"
#include "vr/VRWeapon.h"
#include "vr/WeaponGrip.h"

namespace vr {

namespace {

//! A motion that peaks this close below the swing speed is a near miss (F-60)
const float NearMissShare = 0.8f;

//! A hit this strong says the strike speech, as the engine's strike of a nearly full charge does
const float SpeechAim = 0.8f;

struct MeleeState {
	SwingDetector detector;
	DamageParams damage;             //!< of the weapon's class (#465)
	TargetCooldown cooldown;
	StrikeRecovery recovery;         //!< the weapon hand's, after a swing's hit (#323)
	StrikeRecovery touchRecovery;    //!< after any hit: the next touch outside a swing waits for it (#465)
	RecoveryTime recoveryTime;       //!< of the weapon type in the hand
	std::string recoveryNoted;       //!< the last melee.recovery event's values
	// The swing in progress
	SwingKind swingKind = SwingKind::None;
	float swingAim = 0.f;            //!< the strongest aim ratio of its hits
	bool swingSpoke = false;         //!< it said the strike speech
	std::vector<StrikeSphere> swingSpheres; //!< the hit spheres of its last check (diagnostics, #465)
	bool swingBackground = false;    //!< a hit sphere of it reached the world's geometry
	bool swingRecovering = false;    //!< it started within the recovery: it hits nothing
	u64 swingHits = 0;
	double swingStart = 0.;
	float humpPeak = 0.f;            //!< the peak of a motion that has not become a swing yet
	std::string refusedBefore = "-";
	EntityHandle touchRefused;       //!< the NPC the last melee.touch event refused (#465)
	MeleeStatus status;
};

MeleeState g_melee;

/*!
 * The melee clock, seconds (#23): the predicted display time the frame's controller and head were located
 * for, so a swing's speed is its distance over the time between those poses - the frame clock's jitter
 * (a millisecond is 14 % of a 144 Hz frame) never enters it. Where no display time exists (flat mode, the
 * channel's handpose) the frame clock; across a switch it runs on from where it was. The same value for
 * every call within a frame
 */
double meleeClock() {
	static double clock = 0.0;
	static PlatformInstant frame = PlatformInstant(0);
	static s64 display = 0; // the last display time counted, 0 = none
	if(g_platformTime.frameStart() == frame) {
		return clock;
	}
	const FrameViews & views = frameViews();
	if(views.valid && display != 0 && views.displayTime > display) {
		clock += double(views.displayTime - display) * 1e-9;
	} else if(frame != PlatformInstant(0)) {
		clock += double(toUs(g_platformTime.frameStart() - frame)) * 1e-6;
	}
	frame = g_platformTime.frameStart();
	display = views.valid ? views.displayTime : 0;
	return clock;
}

SwingParams paramsFromConfig() {
	SwingParams params;
	params.startSpeed = config.input.vrMeleeSwingSpeed;
	params.endSpeed = std::min(params.endSpeed, params.startSpeed * 0.6f);
	params.targetCooldown = config.input.vrMeleeTargetCooldown;
	return params;
}

/*!
 * The damage class of the weapon (#502): a dagger or a two-handed weapon by its type, an axe, club, hammer or mace by
 * its item class or material (vr/WeaponGrip.h's rule, applied to two-handed weapons too), anything else a sword
 */
MeleeClass meleeClassOf(const Entity & weapon) {
	if(weapon.type_flags & OBJECT_TYPE_DAGGER) {
		return MeleeClass::Dagger;
	}
	const WeaponClass shape = weaponClassOf(false, false, weapon.className(), weapon.weaponmaterial);
	const bool blunt = shape == WeaponClass::Axe || shape == WeaponClass::Mace;
	if(weapon.type_flags & OBJECT_TYPE_2H) {
		return blunt ? MeleeClass::TwoHandedBlunt : MeleeClass::TwoHandedSword;
	}
	return blunt ? MeleeClass::Blunt : MeleeClass::Sword;
}

//! The damage parameters for the weapon's class: its cfg.ini ranges, or the class's defaults
DamageParams damageFromConfig(MeleeClass meleeClass) {
	const MeleeRanges defaults = defaultRanges(meleeClass);
	DamageParams params;
	params.slash = parseDamageRange(config.input.vrMeleeSlash[size_t(meleeClass)], defaults.slash);
	params.stab = parseDamageRange(config.input.vrMeleeStab[size_t(meleeClass)], defaults.stab);
	params.pathScale = config.input.vrMeleePathScale;
	params.quality = parseQuality(config.input.vrMeleeQuality);
	params.noSwing = parseNoSwing(config.input.vrMeleeNoSwing);
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
 * The game's own strike for a weapon type, seconds - diagnostics next to the recovery (#323): the player's strike
 * start and strike animations, what the attack button plays from the weapon at rest to the weapon at rest
 * (Core.cpp), in the default (left) direction, without the charge held in between; 0 while they are not loaded.
 * \a anims gets their file names
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
	m.recoveryTime = recoveryTime(weapon, configuredRecovery(weapon));
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

void noteRefusal(const char * refused) {
	const std::string now = refused ? refused : "";
	if(now == g_melee.refusedBefore) {
		return;
	}
	g_melee.refusedBefore = now;
	gameevent::Event("melee.state", 200)("state", refused ? "off" : "on")("reason", refused ? refused : "allowed");
}

void startSwing() {

	MeleeState & m = g_melee;
	m.swingKind = m.detector.kind();
	m.swingAim = 0.f;
	m.swingSpoke = false;
	m.swingSpheres.clear();
	m.swingBackground = false;
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
}

void endSwing(SwingChange change) {
	MeleeState & m = g_melee;
	const SwingDetector & d = m.detector;
	const char * result = change == SwingChange::Lost ? "lost"
	                      : m.swingRecovering ? "recovering"
	                      : m.swingHits > 0 ? "hit" : "miss";
	gameevent::Event("melee.swing", 1000)("kind", swingKindName(m.swingKind))("result", result)
		("peak", d.peak(), 2)("aim", m.swingAim, 2)("hits", m.swingHits)
		("ms", int((meleeClock() - m.swingStart) * 1000.));
	if(!m.swingSpheres.empty()) {
		// The weapon's hit spheres of the swing's last check, world x,y,z/radius, and whether any of its checks reached
		// the world's geometry: why a swing does or does not ring on a wall (#465)
		std::ostringstream at;
		at << std::fixed << std::setprecision(1);
		for(const StrikeSphere & sphere : m.swingSpheres) {
			at << (at.tellp() > 0 ? ";" : "") << sphere.origin.x << ',' << sphere.origin.y << ',' << sphere.origin.z
			   << '/' << sphere.radius << (sphere.background ? "!" : "");
		}
		gameevent::Event("melee.spheres", 200)("count", m.swingSpheres.size())("background", m.swingBackground)
			("at", at.str());
	}
}

/*!
 * A melee.touch event when a touch reaches an NPC that does not fight the player (and so does nothing to it) - once
 * per NPC until the touch reaches another or none: why a resting blade did not hit (#465)
 */
void noteRefusedTouch(const std::vector<EntityHandle> & refused) {
	MeleeState & m = g_melee;
	const EntityHandle first = refused.empty() ? EntityHandle() : refused.front();
	if(first == m.touchRefused) {
		return;
	}
	m.touchRefused = first;
	const Entity * npc = entities.get(first);
	if(!npc || !npc->_npcdata) {
		return;
	}
	gameevent::Event("melee.touch", 200)("target", gameevent::entityName(npc))("result", "refused")
		("fight", (npc->_npcdata->behavior & BEHAVIOUR_FIGHT) != 0)("targetsPlayer", npc->targetinfo == EntityHandle_Player);
}

//! The engine's strike speech, as its strike of a nearly full charge says it
void strikeSpeech(Entity * weapon) {
	Entity * io = entities.player();
	std::string_view speech = io->strikespeech;
	if(!weapon->strikespeech.empty()) {
		speech = weapon->strikespeech;
	}
	ARX_SPEECH_AddSpeech(*io, speech, ANIM_TALK_NEUTRAL, ARX_SPEECH_FLAG_NOTEXT);
}

/*!
 * The engine's strike check on the weapon where the hand holds it, with the aim of the stroke so far (#465,
 * \ref measureContact, \ref contactAim): \a swing true for the swing in progress, false for a touch outside a swing
 */
void contact(Entity * weapon, double now, bool swing) {

	MeleeState & m = g_melee;
	MeleeStatus & s = m.status;
	const SwingParams & params = m.detector.params();
	const u64 number = swing ? m.detector.swings() : TargetCooldown::Touch;

	// The stroke as research #502 measures it: the hand's path (a thrust's lunge) and the lever point's speed
	ContactMeasure measure = measureContact(m.detector);
	measure.swing = swing;
	const float aim = contactAim(measure, m.damage);
	if(aim <= 0.f) {
		return; // vr_melee_noswing=none: below a range or outside a swing the blade touches nothing
	}

	StrikeTargets targets;
	for(size_t target : m.cooldown.blocked(number, now, params.targetCooldown)) {
		targets.skip.push_back(EntityHandle(target));
	}
	// A touch is the original's uncharged click on an enemy: only an NPC that fights the player - the blade resting
	// on an item, a crate, a friend, a merchant, the floor or a wall does nothing (the coordinator's decision until the
	// owner's, D-054)
	targets.touch = !swing;
	targets.speed = measure.speed;
	targets.size = measure.size;
	targets.kind = contactKindName(measure.kind);
	ARX_EQUIPMENT_Strike_Check(entities.player(), weapon, aim, 0, EntityHandle(), &targets);
	noteRefusedTouch(targets.refused);
	if(swing) {
		m.swingSpheres = targets.spheres;
		for(const StrikeSphere & sphere : targets.spheres) {
			m.swingBackground = m.swingBackground || sphere.background;
		}
	}
	if(targets.touched.empty()) {
		return;
	}
	// A swing's hit is the hand's recovery for swings and touches alike (#323); a touch only spaces the next touch -
	// a blade resting in an enemy must not keep a real swing from hitting it
	if(swing) {
		m.recovery.hit(now, m.recoveryTime.seconds);
	}
	m.touchRecovery.hit(now, m.recoveryTime.seconds);

	for(const StrikeContact & touched : targets.touched) {
		if(swing) {
			m.cooldown.hit(size_t(touched.target.handleData()), number, now);
		}
		s.contacts++;
		if(!touched.npc) {
			continue;
		}
		if(swing) {
			s.hits++;
		} else {
			s.touches++;
		}
		s.lastTarget = gameevent::entityName(entities.get(touched.target));
		s.lastAim = aim;
		s.lastDamage = touched.damage;
		s.lastSpeed = measure.speed;
		s.lastSize = measure.size;
		s.lastKind = targets.kind;
		s.lastSwing = swing ? number : 0;
		if(swing) {
			m.swingHits++;
			m.swingAim = std::max(m.swingAim, aim);
			if(aim > SpeechAim && !m.swingSpoke) {
				m.swingSpoke = true;
				strikeSpeech(weapon);
			}
		}
	}
}

} // anonymous namespace

void updateMelee(const char * refusal) {

	MeleeState & m = g_melee;
	MeleeStatus & s = m.status;

	m.detector.setParams(paramsFromConfig());
	s.params = m.detector.params();

	Entity * weapon = weaponInHand();
	const PlayerArmStatus & arm = playerBodyStatus().hands[size_t(Hand::Right)];
	HeadSample head;
	const bool haveHead = peekHead(head);
	const char * refused = refusal;
	// The casting stance held (A): the weapon hand's trigger is the pen - no strike (#377, the owner)
	if(!refused && controls().active(Event::MagicMode)) {
		refused = "casting stance";
	}
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
		s.active = false;
		s.refused = refused;
		s.swinging = false;
		s.placed = false;
		s.trigger = false;
		return;
	}
	s.active = true;
	s.refused.clear();
	updateRecoveryTime();
	const MeleeClass meleeClass = meleeClassOf(*weapon);
	m.damage = damageFromConfig(meleeClass);
	s.meleeClass = meleeClassName(meleeClass);
	s.damage = m.damage;

	// No charge (#465): the trigger of the weapon hand changes nothing - reported only, to prove it - and the
	// engine's charge (the HUD's hit strength gauge) stays empty
	s.trigger = controllerInput().down[control(Hand::Right, ControlKind::Trigger)];
	player.m_aimTime = 0;

	// The swing: the controller relative to the head, both in the LOCAL space
	const double now = meleeClock();
	SwingSample sample;
	sample.time = now;
	sample.hand = arm.sample.position - head.position;
	sample.orientation = arm.sample.orientation;
	const SwingChange change = m.detector.update(sample, true);
	if(change == SwingChange::Started) {
		startSwing();
	}

	// A swing checks its contacts unless it started within the recovery (#323); outside a swing the blade touches
	// once the recovery after the last hit of any kind is over - and deals the minimum, or nothing (vr_melee_noswing,
	// #465)
	s.placed = placeHeldWeapon(s.hilt, s.tip);
	if(s.placed) {
		if(m.detector.swinging()) {
			if(!m.swingRecovering) {
				contact(weapon, now, true);
			}
		} else if(m.recovery.ready(now) && m.touchRecovery.ready(now)) {
			contact(weapon, now, false);
		}
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
	s.swings = m.detector.swings();
	s.gaps = m.detector.gaps();
	s.jumps = m.detector.jumps();
	s.recoveryLeft = m.recovery.remaining(now);
	s.recovering = m.swingRecovering;
	s.worldHit = entities.player()->isHit;
}

bool meleeOwnsStrikes() {
	return weaponInHand() != nullptr;
}

const MeleeStatus & meleeStatus() {
	return g_melee.status;
}

} // namespace vr
