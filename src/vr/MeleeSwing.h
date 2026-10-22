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
 * \brief The decision core of physical melee (#39, D-038, D-054): is the weapon swung, how hard does it hit
 *
 * The owner's design (2026-10-17, #465): a physical swing of the weapon hits, and its damage comes from the
 * swing alone - its size and its speed where the blade touches: a touch or a tap the minimum (like the original's
 * uncharged click; nothing is a cfg.ini choice), a slow big swing or a fast tiny flick little, only a big and fast
 * slash or lunge the full damage of the engine's fully charged strike. The trigger charges nothing.
 *
 *  - \ref SwingDetector: the controller's motion -> a swing in progress and its kind (slash or stab), and the
 *    velocity of any point of the blade (\ref SwingDetector::pointVelocity). A swing's speed is the one of a
 *    point \ref SwingParams::lever along the blade from the
 *    hand (F-61: a wrist flick turns a long blade's tip fast but this point slowly), measured relative
 *    to the head (F-59: leaning or stepping with a still arm is no swing; the room-space position
 *    already ignores the stick's walk and turn, F-178), and read at one moment of display time,
 *    \ref SwingParams::speedLag before the frame's (D-063, #620): every step between neighbouring positions - the
 *    medians of three samples (F-59), the newest sample itself at the front - is the mean velocity over its span,
 *    placed at the span's middle, and the reading is the one between the two middles around that moment (an
 *    accelerating arm's parabola), or the newest step's when the moment is past its middle (a long frame). At 90 Hz
 *    that moment is the middle of the step between the median window's middle samples (the step the median really
 *    moved over, #352): the reading of the 1.5-frame median lag, now the same at every rate. Samples with a time
 *    step under 4 ms are skipped; a step over 100 ms or a speed above 20 m/s - raw or filtered - (a tracking jump)
 *    starts it over, except a long frame in a swing that visibly went on across it (its mean speed at least
 *    \ref SwingParams::endSpeed, #620).
 *    A swing starts at \ref SwingParams::startSpeed - or as a stab: a thrust of \ref SwingParams::stabDistance
 *    along the blade within \ref SwingParams::stabWindow at \ref SwingParams::stabSpeed (F-61) - and
 *    ends below \ref SwingParams::endSpeed or after \ref SwingParams::maxDuration.
 *  - \ref contactAim: the stroke's size and speed at contact (\ref ContactMeasure: the hand's path since the stroke
 *    started - for a thrust its lunge along the blade - and the lever point's speed, #502) -> the ratio the engine's
 *    damage is scaled by (\c ARX_EQUIPMENT_ComputeDamages' \c ratioaim, 1 = the fully charged strike):
 *    \ref MinStrikeAim (or nothing, \ref DamageParams::noSwing) outside a swing and below either range, else
 *    0.1 + 0.9 q, q the quality of the two factors (\ref Quality: by default the weaker one). The ranges are per
 *    weapon class (\ref MeleeClass, \ref defaultRanges: research #502), a slash's and a stab's; \ref contactKind
 *    tells a slash from a thrust.
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

//! What a hit outside a swing, or below a threshold, deals [vr_melee_noswing]
enum class NoSwing : u8 {
	None, //!< nothing: the blade touches, nothing happens
	Min   //!< the minimum, \ref MinStrikeAim - the default: a touch or a tap is the original's uncharged click
};

//! "none", "min"
const char * noSwingName(NoSwing noSwing);

//! "none" is \ref NoSwing::None, anything else the default \ref NoSwing::Min (\a ok false unless it is "min")
NoSwing parseNoSwing(std::string_view name, bool * ok = nullptr);

//! The engine's weakest strike: the floor of its charge ratio (\c StrikeAimtime's 0.1)
constexpr float MinStrikeAim = 0.1f;

//! The thresholds (the speeds in m/s, the cfg.ini keys in brackets)
struct SwingParams {
	float lever = 0.125f;        //!< m along the blade from the grip where the speed is measured (F-61)
	float startSpeed = 2.5f;     //!< a swing starts at this speed [vr_melee_swing_speed]
	float endSpeed = 1.5f;       //!< and ends below this one (hysteresis)
	float thrustCosine = 0.707f; //!< the lever point moving within 45 degrees of the blade's way is a thrust (F-178)
	float stabSpeed = 2.f;       //!< a thrust along the blade at this speed ...
	float stabDistance = 0.2f;   //!< ... covering this distance ...
	float stabWindow = 0.25f;    //!< ... within this time is a stab, below \ref startSpeed too (F-61)
	float maxDuration = 1.f;     //!< s: a longer swing (circling the blade) ends and a new one may start
	float jumpSpeed = 20.f;      //!< faster (raw or filtered) is a tracking jump, not a swing (F-59, #352)
	float minStep = 0.004f;      //!< s: samples closer than this are skipped
	float maxStep = 0.1f;        //!< s: a longer gap starts over (not a swing that went on across it at \ref endSpeed or more)
	float speedLag = 1.f / 60.f; //!< s before the frame's display time the speed is read at: 1.5 frames of 90 Hz (D-063)
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

//! The weapon classes the damage ranges are kept for (#502)
enum class MeleeClass : u8 {
	Dagger,
	Sword,          //!< one-handed
	Blunt,          //!< one-handed axe, club, hammer, mace
	TwoHandedSword, //!< two-handed sword, giant slayer
	TwoHandedBlunt  //!< two-handed axe, smith's club
};

//! The classes, in order
constexpr MeleeClass MeleeClasses[] = { MeleeClass::Dagger, MeleeClass::Sword, MeleeClass::Blunt,
                                        MeleeClass::TwoHandedSword, MeleeClass::TwoHandedBlunt };

//! "dagger", "sword", "blunt", "2h_sword", "2h_blunt" - the cfg.ini keys' suffixes
const char * meleeClassName(MeleeClass meleeClass);

//! A size and a speed range: each factor is 0 at its minimum and 1 at its maximum
struct DamageRange {
	float pathMin = 0.f;  //!< m of the hand's path (a stab: its lunge along the blade)
	float pathMax = 0.f;
	float speedMin = 0.f; //!< m/s of the lever point at contact
	float speedMax = 0.f;
	float cap = 1.f;      //!< the most quality a stroke of this kind reaches; 0: the weapon has no such strike (#502)
};

//! A weapon class's ranges
struct MeleeRanges {
	DamageRange slash;
	DamageRange stab;
};

//! \return research #502's provisional ranges of \a meleeClass (the owner tunes them)
MeleeRanges defaultRanges(MeleeClass meleeClass);

/*!
 * \return \a text as "<path min> <path max> <speed min> <speed max> [<cap>]" (a cfg.ini value; 0 to 20, the cap 0 to 1,
 *         1 when left out), each max not below its min; \a fallback for an empty text or a wrong one (\a ok false)
 */
DamageRange parseDamageRange(std::string_view text, const DamageRange & fallback, bool * ok = nullptr);

//! How the two factors combine [vr_melee_quality]
enum class Quality : u8 {
	Min,     //!< the weaker one (the default): each range keeps its own meaning
	GeoMean, //!< their geometric mean
	Product  //!< their product
};

//! "min", "geomean", "product"
const char * qualityName(Quality quality);

//! "geomean" and "product", anything else \ref Quality::Min
Quality parseQuality(std::string_view name);

//! How a hit's damage is decided (#465)
struct DamageParams {
	DamageRange slash = defaultRanges(MeleeClass::Sword).slash;
	DamageRange stab = defaultRanges(MeleeClass::Sword).stab;
	float pathScale = 1.f;          //!< the path ranges are scaled by it: seated play has shorter arms' paths [vr_melee_path_scale]
	Quality quality = Quality::Min;  //!< [vr_melee_quality]
	NoSwing noSwing = NoSwing::Min;  //!< a hit outside a swing or below a range [vr_melee_noswing]
};

//! A contact with a target, measured (#502's measures)
struct ContactMeasure {
	float speed = 0.f;     //!< m/s, the lever point's filtered speed relative to the head
	float size = 0.f;      //!< m, the stroke so far: the hand's path - a thrust's lunge along the blade
	ContactKind kind = ContactKind::Slash;
	bool swing = false;    //!< in a swing (false: a touch, a tap, a wrist flick - F-61)
};

/*!
 * \return the aim ratio a contact deals (the engine's damage = attack * ratio, 1 = the fully charged strike), whatever
 *         the trigger does: outside a swing, below the kind's speed or path minimum, or of a kind the weapon has not
 *         (its cap 0), \ref MinStrikeAim (or 0, \ref DamageParams::noSwing); else 0.1 + 0.9 q, q of the speed's and
 *         the path's factor by \ref DamageParams::quality, at most the kind's cap - a slow big swing and a fast tiny
 *         one both stay low
 */
float contactAim(const ContactMeasure & contact, const DamageParams & params);

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
	 *         contact point of a hit (#465), filtered like \ref speed (which is the one of \ref SwingParams::lever):
	 *         the velocity \ref SwingParams::speedLag before the newest sample, read between the steps of the
	 *         median-filtered points around that moment (D-063); 0 before four samples
	 */
	Vec3f pointVelocity(float distance) const;
	//! The blade's direction in the last sample (the grip's -Z), relative to the head
	Vec3f blade() const { return m_count > 0 ? m_blades[0] : Vec3f(0.f, 0.f, -1.f); }
	/*!
	 * \return the path (m) the blade's point \a distance m from the grip travelled in the stroke so far - since the
	 *         last sample before the lever point last moved slower than \ref SwingParams::endSpeed outside a swing, or
	 *         the hand last turned back (its way against three samples before): a slash's size (#465)
	 */
	float strokePath(float distance) const;
	//! \return how far (m) that point moved along the blade, towards the tip, in the stroke so far: a thrust's lunge
	float strokeLunge(float distance) const;
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

	struct StrokeSample {
		Vec3f hand;
		Vec3f blade;
	};

	//! The longest stroke kept, samples (\ref SwingParams::maxDuration at 250 Hz)
	static constexpr size_t MaxStroke = 256;
	/*!
	 * The samples kept for the velocity, newest first: enough steps of the median-filtered points to reach
	 * \ref SwingParams::speedLag back at 250 Hz (4 ms frames: the moment is 4 steps back, 7 samples)
	 */
	static constexpr size_t Samples = 8;

	SwingChange drop();

	SwingParams m_params;
	bool m_seeded = false;
	double m_lastTime = 0.;
	Vec3f m_lastPoint = Vec3f(0.f);
	Vec3f m_lastHand = Vec3f(0.f);
	// The last samples, newest first: the medians of three neighbours are the filtered points any point of the
	// blade moved through (F-59), their steps its velocity (\ref pointVelocity)
	std::array<Vec3f, Samples> m_hands = { };
	std::array<Vec3f, Samples> m_blades = { };
	std::array<double, Samples> m_times = { };
	size_t m_count = 0;
	std::vector<StrokeSample> m_stroke;   //!< the stroke so far, oldest first
	std::array<Vec3f, 4> m_handVelocities = { }; //!< the hand's filtered velocity of the last four samples, newest first
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
 * \return the contact the weapon would make now (#502's measures): the lever point's filtered speed and how it moves
 *         (a thrust or a slash), the hand's path in the stroke so far - for a thrust its lunge along the blade
 */
ContactMeasure measureContact(const SwingDetector & detector);

/*!
 * \brief Who may be hit now: one hit per target per swing, and not again within the cooldown (F-62)
 *
 * Targets are the engine's entity indices. A touch outside a swing (\ref NoSwing::Min) asks as swing \ref Touch and
 * records nothing: only the cooldown after a swing's hit holds it back.
 */
class TargetCooldown {

public:

	//! The swing number of a touch outside a swing: never a swing's
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
