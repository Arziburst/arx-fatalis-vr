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
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief The decision core of physical melee (#39, D-038, D-NEW): is the weapon swung, how hard does it hit
 *
 * The owner's design (2026-10-17, #465): a physical swing of the weapon hits, and its damage comes from the
 * swing alone - the speed of the blade where it touches: no swing, no damage (or the minimum, a cfg.ini choice),
 * a fast slash or thrust the full damage of the engine's fully charged strike. The trigger charges nothing.
 *
 *  - \ref SwingDetector: the controller's motion -> a swing in progress and its kind (slash or stab), and the
 *    velocity of any point of the blade (\ref SwingDetector::pointVelocity). A swing's speed is the one of a
 *    point \ref SwingParams::lever along the blade from the
 *    hand (F-61: a wrist flick turns a long blade's tip fast but this point slowly), measured relative
 *    to the head (F-59: leaning or stepping with a still arm is no swing; the room-space position
 *    already ignores the stick's walk and turn, F-178), from the median of the last three positions (F-59) over
 *    the time between the middle samples (the step the median really moved over, #352), and samples with a time
 *    step outside 4-100 ms or a speed above 20 m/s - raw or filtered - (a tracking jump) start it over.
 *    A swing starts at \ref SwingParams::startSpeed - or as a stab: a thrust of \ref SwingParams::stabDistance
 *    along the blade within \ref SwingParams::stabWindow at \ref SwingParams::stabSpeed (F-61) - and
 *    ends below \ref SwingParams::endSpeed or after \ref SwingParams::maxDuration.
 *  - \ref contactAim: the speed of the blade's point that touches -> the ratio the engine's damage is scaled by
 *    (\c ARX_EQUIPMENT_ComputeDamages' \c ratioaim, 1 = the fully charged strike): nothing or \ref MinStrikeAim
 *    below \ref SwingParams::speedMin and outside a swing (\ref SwingParams::noSwing), rising to 1 at
 *    \ref SwingParams::speedMax (F-178's strength curve, F-63); \ref contactKind: a slash or a thrust, which
 *    count alike at the same speed.
 *  - \ref TargetCooldown: one hit per target per swing, and not within \ref SwingParams::targetCooldown
 *    of the last one (F-62).
 *  - \ref StrikeRecovery: after a hit the hand recovers - a swing started within \ref recoveryTime of the hit
 *    hits nothing (#323: waving the weapon fast is no flurry of hits); a time per weapon type, the dagger
 *    shortest, a cfg.ini key overrides it.
 *
 * Pure logic: positions in meters, times in seconds, no engine globals, no clock - covered by
 * tests/vr/MeleeSwingTest. Thresholds against prior art: F-74, F-178, F-195 (RESEARCH-reference-mods.md).
 */
namespace vr {

//! What a hit below \ref SwingParams::speedMin, or outside a swing, deals [vr_melee_noswing]
enum class NoSwing : u8 {
	None, //!< nothing: the blade touches, nothing happens
	Min   //!< the minimum, \ref MinStrikeAim
};

//! "none", "min"
const char * noSwingName(NoSwing noSwing);

//! "min" is \ref NoSwing::Min, anything else \ref NoSwing::None
NoSwing parseNoSwing(std::string_view name);

//! The engine's weakest strike: the floor of its charge ratio (\c StrikeAimtime's 0.1)
constexpr float MinStrikeAim = 0.1f;

//! The thresholds (the speeds in m/s, the cfg.ini keys in brackets)
struct SwingParams {
	float lever = 0.125f;        //!< m along the blade from the grip where the speed is measured (F-61)
	float startSpeed = 2.5f;     //!< a swing starts at this speed [vr_melee_swing_speed]
	float endSpeed = 1.5f;       //!< and ends below this one (hysteresis)
	float speedMin = 1.5f;       //!< m/s at the contact point a hit damages from [vr_melee_speed_min]
	float speedMax = 4.5f;       //!< m/s at the contact point a hit has the full damage at [vr_melee_speed_max]
	NoSwing noSwing = NoSwing::None; //!< a hit below \ref speedMin or outside a swing [vr_melee_noswing]
	float thrustCosine = 0.707f; //!< the contact point moving within 45 degrees of the blade's way is a thrust (F-178)
	float stabSpeed = 2.f;       //!< a thrust along the blade at this speed ...
	float stabDistance = 0.2f;   //!< ... covering this distance ...
	float stabWindow = 0.25f;    //!< ... within this time is a stab, below \ref startSpeed too (F-61)
	float maxDuration = 1.f;     //!< s: a longer swing (circling the blade) ends and a new one may start
	float jumpSpeed = 20.f;      //!< faster (raw or filtered) is a tracking jump, not a swing (F-59, #352)
	float minStep = 0.004f;      //!< s: samples closer than this are skipped
	float maxStep = 0.1f;        //!< s: a longer gap starts over
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

//! How the blade's point moved when it touched (a swing's \ref SwingKind is how it started)
enum class ContactKind : u8 {
	Slash, //!< across the blade
	Thrust //!< along the blade, towards the tip (within \ref SwingParams::thrustCosine)
};

//! "slash", "thrust"
const char * contactKindName(ContactKind kind);

//! \return the kind of a contact whose point moved at \a velocity with the blade along \a blade (a unit vector)
ContactKind contactKind(const Vec3f & velocity, const Vec3f & blade, const SwingParams & params);

/*!
 * \return the aim ratio a contact deals (the engine's damage = attack * ratio, 1 = the fully charged strike), from
 *         the speed of the blade's point that touched, \a speed m/s, whatever the trigger does: below
 *         \ref SwingParams::speedMin - and for a contact outside a swing (\a swing false: a touch, a wrist flick,
 *         F-61) - 0 or \ref MinStrikeAim (\ref SwingParams::noSwing); from \ref SwingParams::speedMin to
 *         \ref SwingParams::speedMax linearly from \ref MinStrikeAim to 1; faster 1
 */
float contactAim(float speed, bool swing, const SwingParams & params);

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
	/*!
	 * \return the filtered velocity (m/s, relative to the head) of the blade's point \a distance m from the grip - the
	 *         contact point of a hit (#465), filtered like \ref speed (which is the one of \ref SwingParams::lever);
	 *         0 before four samples
	 */
	Vec3f pointVelocity(float distance) const;
	//! The blade's direction in the last sample (the grip's -Z), relative to the head
	Vec3f blade() const { return m_count > 0 ? m_blades[0] : Vec3f(0.f, 0.f, -1.f); }
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
	// The last four samples, newest first: the median of the newest three against the one before it is the
	// filtered velocity of any point of the blade (F-59)
	std::array<Vec3f, 4> m_hands = { };
	std::array<Vec3f, 4> m_blades = { };
	std::array<double, 4> m_times = { };
	size_t m_count = 0;
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
 * \brief Who may be hit now: one hit per target per swing, and not again within the cooldown (F-62)
 *
 * Targets are the engine's entity indices. A contact outside a swing (\ref NoSwing::Min) is swing \ref Touch:
 * only the cooldown holds it back.
 */
class TargetCooldown {

public:

	//! The swing number of a contact outside a swing
	static constexpr u64 Touch = ~u64(0);

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
	Default, //!< \ref defaultRecovery
	Config   //!< the cfg.ini key
};

//! "default", "config"
const char * recoverySourceName(RecoverySource source);

/*!
 * \return the default recovery after a hit, seconds: the dagger shortest, the two-handed weapon longest (#323).
 *         Not the game's own strike: the hero's strike start + strike animations take 1.79 s with the dagger,
 *         1.5 s one-handed and 1.79 s two-handed - the dagger is not the fastest, and every one is slow for a
 *         swing of the arm (VRMelee reports them as diagnostics)
 */
float defaultRecovery(MeleeWeapon weapon);

struct RecoveryTime {
	float seconds = 0.f;
	RecoverySource source = RecoverySource::Default;
};

/*!
 * \brief The recovery after a hit for a weapon type
 * \param configured the cfg.ini key (\c vr_melee_recovery_dagger, \c _1h, \c _2h): seconds when 0 or more, the
 *        default when negative
 */
RecoveryTime recoveryTime(MeleeWeapon weapon, float configured);

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
