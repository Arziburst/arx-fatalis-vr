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

#ifndef ARX_VR_MELEESWING_H
#define ARX_VR_MELEESWING_H

#include <array>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief The decision core of physical melee (#39, D-038): is the weapon swung, how hard, is the strike charged
 *
 * The owner's design (2026-10-10): a physical swing of the weapon hits; a plain swing is a weak, fast
 * hit; holding the trigger charges the strike with the game's own charge (the time the attack button
 * is held against the player's \c Full_AimTime, the engine's \c StrikeAimtime), and a swing then hits
 * much harder; after a charged hit the charge is gone until the trigger is released and pressed again.
 *
 *  - \ref SwingDetector: the controller's motion -> a swing in progress, its kind (slash or stab) and
 *    its speed factor. The speed is the one of a point \ref SwingParams::lever along the blade from the
 *    hand (F-61: a wrist flick turns a long blade's tip fast but this point slowly), measured relative
 *    to the head (F-59: leaning or stepping with a still arm is no swing; the room-space position
 *    already ignores the stick's walk and turn, F-178), from the median of the last three positions (F-59) over
 *    the time between the middle samples (the step the median really moved over, #352), and samples with a time
 *    step outside 4-100 ms or a speed above 20 m/s - raw or filtered - (a tracking jump) start it over.
 *    A swing starts at \ref SwingParams::startSpeed - or as a stab: a thrust of \ref SwingParams::stabDistance
 *    along the blade within \ref SwingParams::stabWindow at \ref SwingParams::stabSpeed (F-61) - and
 *    ends below \ref SwingParams::endSpeed or after \ref SwingParams::maxDuration.
 *  - \ref StrikeCharge: the trigger -> the charge ratio, spent by a charged hit, re-armed by a release.
 *  - \ref strikeAim: the ratio the engine's damage is scaled by (\c ARX_EQUIPMENT_ComputeDamages'
 *    \c ratioaim): the charge (or \ref SwingParams::weakAim without one) times the speed factor (F-63).
 *  - \ref TargetCooldown: one hit per target per swing, and not within \ref SwingParams::targetCooldown
 *    of the last one (F-62).
 *  - \ref StrikeRecovery: after a hit the hand recovers - a swing started within \ref recoveryTime of the hit
 *    hits nothing (#323: waving the weapon fast is no flurry of hits); the time is the game's own strike
 *    for the weapon type (the player's strike start and strike animations), a cfg.ini key overrides it.
 *
 * Pure logic: positions in meters, times in seconds, no engine globals, no clock - covered by
 * tests/vr/MeleeSwingTest. Thresholds against prior art: F-74, F-178, F-195 (RESEARCH-reference-mods.md).
 */
namespace vr {

//! The thresholds (the speeds in m/s, the cfg.ini keys in brackets)
struct SwingParams {
	float lever = 0.125f;        //!< m along the blade from the grip where the speed is measured (F-61)
	float startSpeed = 2.5f;     //!< a swing starts at this speed [vr_melee_swing_speed]
	float endSpeed = 1.5f;       //!< and ends below this one (hysteresis)
	float fullSpeed = 6.f;       //!< the speed factor reaches 1 here [vr_melee_full_speed]
	float minFactor = 0.5f;      //!< the speed factor of a swing at \ref startSpeed
	float stabSpeed = 2.f;       //!< a thrust along the blade at this speed ...
	float stabDistance = 0.2f;   //!< ... covering this distance ...
	float stabWindow = 0.25f;    //!< ... within this time is a stab, below \ref startSpeed too (F-61)
	float maxDuration = 1.f;     //!< s: a longer swing (circling the blade) ends and a new one may start
	float jumpSpeed = 20.f;      //!< faster (raw or filtered) is a tracking jump, not a swing (F-59, #352)
	float minStep = 0.004f;      //!< s: samples closer than this are skipped
	float maxStep = 0.1f;        //!< s: a longer gap starts over
	float weakAim = 0.25f;       //!< the aim ratio of an uncharged swing [vr_melee_weak_aim]
	float targetCooldown = 0.25f; //!< s before a target can be hit again, by a new swing [vr_melee_target_cooldown]
};

//! The weapon hand in one frame
struct SwingSample {
	double time = 0.;          //!< seconds (the frame's display time)
	Vec3f hand = Vec3f(0.f);   //!< the grip relative to the head, meters, OpenXR LOCAL axes (x right, y up, -z forward)
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< grip to LOCAL; the blade lies along the grip's -Z (#75)
};

enum class SwingKind : u8 {
	None,
	Slash, //!< the lever point moved fast
	Stab   //!< a thrust along the blade (F-61)
};

//! "none", "slash", "stab"
const char * swingKindName(SwingKind kind);

//! What one sample changed
enum class SwingChange : u8 {
	None,
	Started, //!< a swing started with this sample: hits count from now
	Ended,   //!< the swing in progress ended with this sample
	Lost     //!< the swing in progress was dropped: tracking lost, a jump, a gap (no hits after it)
};

class SwingDetector {

public:

	explicit SwingDetector(const SwingParams & params = SwingParams()) : m_params(params) { }

	void setParams(const SwingParams & params) { m_params = params; }
	const SwingParams & params() const { return m_params; }

	/*!
	 * \brief One sample of the weapon hand, once per frame
	 * \param tracked false while the controller is not located: the detector starts over
	 */
	SwingChange update(const SwingSample & sample, bool tracked);

	//! Start over: the next sample only seeds (a menu opened, the weapon put away)
	SwingChange reset();

	bool swinging() const { return m_kind != SwingKind::None; }
	SwingKind kind() const { return m_kind; }
	//! The filtered speed of the lever point (m/s), 0 before two samples
	float speed() const { return m_speed; }
	//! The thrust along the blade within the stab window (m)
	float thrust() const { return m_thrust; }
	//! The fastest filtered speed of the swing in progress (or of the last one)
	float peak() const { return m_peak; }
	//! The speed factor of the swing in progress (or of the last one): \ref SwingParams::minFactor .. 1
	float factor() const;
	//! When the swing in progress (or the last one) started, seconds
	double startTime() const { return m_startTime; }
	//! Swings started since the start - a swing's number
	u64 swings() const { return m_swings; }
	//! Samples refused since the start: \c gaps a time step outside the window, \c jumps too fast
	u64 gaps() const { return m_gaps; }
	u64 jumps() const { return m_jumps; }

private:

	struct Thrust {
		double time;
		float along; //!< m moved along the blade by this step (positive = towards the tip)
	};

	SwingChange drop();

	SwingParams m_params;
	bool m_seeded = false;
	double m_lastTime = 0.;
	Vec3f m_lastPoint = Vec3f(0.f);
	Vec3f m_lastHand = Vec3f(0.f);
	std::array<Vec3f, 3> m_points = { };  //!< the last three lever points, newest first
	std::array<double, 3> m_times = { };  //!< and their times
	size_t m_pointCount = 0;
	Vec3f m_filtered = Vec3f(0.f);        //!< their median
	bool m_filteredValid = false;
	std::vector<Thrust> m_thrusts;
	float m_speed = 0.f;
	float m_thrust = 0.f;
	SwingKind m_kind = SwingKind::None;
	float m_peak = 0.f;
	double m_startTime = 0.;
	u64 m_swings = 0;
	u64 m_gaps = 0;
	u64 m_jumps = 0;

};

/*!
 * \brief The strike's charge from the trigger (the game's own: the time held against \c Full_AimTime)
 *
 * Held, the charge grows; let go, it is gone (the engine's strike also needs the button held until the
 * release). \ref spend after a charged hit empties it, and it stays empty until the trigger is released.
 */
class StrikeCharge {

public:

	/*!
	 * \param held the trigger is held this frame (and the swing is allowed: no menu, nothing dragged)
	 * \param seconds the frame's duration
	 */
	void update(bool held, float seconds);

	//! A charged hit used the charge: empty until the trigger is released
	void spend();

	bool held() const { return m_held; }
	//! Charging: held and not spent
	bool charging() const { return m_held && !m_spent; }
	bool spent() const { return m_spent; }
	//! Seconds charged (0 when not charging)
	float seconds() const { return charging() ? m_seconds : 0.f; }
	/*!
	 * \return the engine's charge ratio for \a fullSeconds (the player's \c Full_AimTime): the time held
	 *         against it, 0.1 to 1 like \c StrikeAimtime - 0 when not charging
	 */
	float ratio(float fullSeconds) const;
	//! Charged hits since the start
	u64 spends() const { return m_spends; }

private:

	bool m_held = false;
	bool m_spent = false;
	float m_seconds = 0.f;
	u64 m_spends = 0;

};

/*!
 * \return the aim ratio a hit is dealt with (the engine's damage = attack * ratio): the charge ratio of a
 *         charged swing (never below \ref SwingParams::weakAim), \ref SwingParams::weakAim otherwise,
 *         times the swing's speed factor
 */
float strikeAim(float chargeRatio, float speedFactor, const SwingParams & params);

/*!
 * \brief Who may be hit now: one hit per target per swing, and not again within the cooldown (F-62)
 *
 * Targets are the engine's entity indices.
 */
class TargetCooldown {

public:

	//! \return true when \a target may be hit by swing \a swing at \a time
	bool allowed(size_t target, u64 swing, double time, float cooldown) const;

	//! \a target was hit by swing \a swing at \a time
	void hit(size_t target, u64 swing, double time);

	//! The targets \ref allowed refuses now - for the engine's exception list
	std::vector<size_t> blocked(u64 swing, double time, float cooldown) const;

	void clear() { m_hits.clear(); }

private:

	struct Hit {
		size_t target;
		u64 swing;
		double time;
	};

	std::vector<Hit> m_hits;

};

//! The melee weapon types a recovery time is kept for (the engine's \c WeaponType without bare hands and bows)
enum class MeleeWeapon : u8 {
	Dagger,
	OneHanded,
	TwoHanded
};

//! "dagger", "1h", "2h" - the engine's names (the \c strike script event's parameter)
const char * meleeWeaponName(MeleeWeapon weapon);

//! Where a recovery time came from
enum class RecoverySource : u8 {
	Game,    //!< the game's own strike for the weapon type
	Config,  //!< the cfg.ini key
	Fallback //!< \ref fallbackRecovery: the game's strike is not known (its animations are not loaded)
};

//! "game", "config", "fallback"
const char * recoverySourceName(RecoverySource source);

/*!
 * \return seconds of recovery when the game's own strike is not known: the dagger shortest, the two-handed
 *         weapon longest, near the game's strike animations (#323)
 */
float fallbackRecovery(MeleeWeapon weapon);

struct RecoveryTime {
	float seconds = 0.f;
	RecoverySource source = RecoverySource::Fallback;
};

/*!
 * \brief The recovery after a hit for a weapon type
 * \param configured the cfg.ini key (\c vr_melee_recovery_dagger, \c _1h, \c _2h): seconds when 0 or more, the
 *        game's own when negative
 * \param game the game's own strike for the type, seconds: the player's strike start and strike animations
 *        (what the engine's attack button plays from the hand at rest to the hand at rest), 0 when not known
 */
RecoveryTime recoveryTime(MeleeWeapon weapon, float configured, float game);

/*!
 * \brief The weapon hand's recovery after a hit (#323): a swing that starts within it hits nothing
 *
 * The swing that hit goes on hitting what else it touches (one hit per target, \ref TargetCooldown); a new
 * swing started before the recovery ended is spent - it does not start hitting when the recovery ends.
 */
class StrikeRecovery {

public:

	//! A swing hit at \a time: swings starting before \a time + \a seconds hit nothing
	void hit(double time, float seconds);

	//! \return true when a swing starting at \a time may hit
	bool ready(double time) const { return !m_hit || time >= m_until; }

	//! \return seconds until a swing may hit again, 0 when it may now
	float remaining(double time) const;

	void clear() { m_hit = false; }

private:

	bool m_hit = false;
	double m_until = 0.;

};

} // namespace vr

#endif // ARX_VR_MELEESWING_H
