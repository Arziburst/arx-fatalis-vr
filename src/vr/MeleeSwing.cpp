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

#include "vr/MeleeSwing.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include <glm/glm.hpp>

namespace vr {

const char * swingKindName(SwingKind kind) {
	switch(kind) {
		case SwingKind::None: return "none";
		case SwingKind::Slash: return "slash";
		case SwingKind::Stab: return "stab";
	}
	return "none";
}

const char * contactKindName(ContactKind kind) {
	switch(kind) {
		case ContactKind::Slash: return "slash";
		case ContactKind::Thrust: return "thrust";
	}
	return "slash";
}

const char * noSwingName(NoSwing noSwing) {
	switch(noSwing) {
		case NoSwing::None: return "none";
		case NoSwing::Min: return "min";
	}
	return "none";
}

NoSwing parseNoSwing(std::string_view name, bool * ok) {
	if(ok) {
		*ok = name == "none" || name == "min";
	}
	return name == "none" ? NoSwing::None : NoSwing::Min;
}

ContactKind contactKind(const Vec3f & velocity, const Vec3f & blade, const SwingParams & params) {
	const float speed = glm::length(velocity);
	if(speed <= 0.f) {
		return ContactKind::Slash;
	}
	return glm::dot(velocity, blade) >= params.thrustCosine * speed ? ContactKind::Thrust : ContactKind::Slash;
}

namespace {

//! 0 at \a low, 1 at \a high and above (1 from \a low on when they are the same)
float ramp(float value, float low, float high) {
	return high > low ? glm::clamp((value - low) / (high - low), 0.f, 1.f) : 1.f;
}

} // anonymous namespace

const char * meleeClassName(MeleeClass meleeClass) {
	switch(meleeClass) {
		case MeleeClass::Dagger: return "dagger";
		case MeleeClass::Sword: return "sword";
		case MeleeClass::Blunt: return "blunt";
		case MeleeClass::TwoHandedSword: return "2h_sword";
		case MeleeClass::TwoHandedBlunt: return "2h_blunt";
	}
	return "sword";
}

MeleeRanges defaultRanges(MeleeClass meleeClass) {
	// Research #502: a slash's hand path and lever speed from the minimum to the full strike, a stab's lunge and speed
	switch(meleeClass) {
		case MeleeClass::Dagger: return { { 0.25f, 0.6f, 2.f, 5.f }, { 0.2f, 0.45f, 2.f, 4.5f } };
		case MeleeClass::Sword: return { { 0.3f, 0.8f, 2.5f, 6.f }, { 0.2f, 0.5f, 2.f, 4.f } };
		// A one-handed blunt weapon's stab is a poke: the sword's ranges, half the strike at most; a two-handed one has
		// no stab (a thrust deals what a touch does)
		case MeleeClass::Blunt: return { { 0.35f, 0.9f, 2.5f, 5.5f }, { 0.2f, 0.5f, 2.f, 4.f, 0.5f } };
		case MeleeClass::TwoHandedSword: return { { 0.4f, 1.1f, 2.5f, 5.f }, { 0.25f, 0.5f, 2.f, 3.5f } };
		case MeleeClass::TwoHandedBlunt: return { { 0.5f, 1.2f, 2.5f, 4.5f }, { 0.25f, 0.5f, 2.f, 3.5f, 0.f } };
	}
	return defaultRanges(MeleeClass::Sword);
}

DamageRange parseDamageRange(std::string_view text, const DamageRange & fallback, bool * ok) {
	if(ok) {
		*ok = true;
	}
	if(text.find_first_not_of(" \t") == std::string_view::npos) {
		return fallback;
	}
	std::istringstream iss{std::string(text)};
	DamageRange range;
	std::string rest;
	bool good = bool(iss >> range.pathMin >> range.pathMax >> range.speedMin >> range.speedMax);
	if(good && !(iss >> range.cap)) {
		good = iss.eof(); // the cap left out
		range.cap = 1.f;
	} else if(good) {
		good = !(iss >> rest) && range.cap >= 0.f && range.cap <= 1.f;
	}
	for(float value : { range.pathMin, range.pathMax, range.speedMin, range.speedMax }) {
		good = good && value >= 0.f && value <= 20.f;
	}
	if(!good) {
		if(ok) {
			*ok = false;
		}
		return fallback;
	}
	range.pathMax = std::max(range.pathMax, range.pathMin);
	range.speedMax = std::max(range.speedMax, range.speedMin);
	return range;
}

const char * qualityName(Quality quality) {
	switch(quality) {
		case Quality::Min: return "min";
		case Quality::GeoMean: return "geomean";
		case Quality::Product: return "product";
	}
	return "min";
}

Quality parseQuality(std::string_view name) {
	if(name == "geomean") {
		return Quality::GeoMean;
	}
	if(name == "product") {
		return Quality::Product;
	}
	return Quality::Min;
}

float contactAim(const ContactMeasure & contact, const DamageParams & params) {
	const float floor = params.noSwing == NoSwing::Min ? MinStrikeAim : 0.f;
	const DamageRange & range = contact.kind == ContactKind::Thrust ? params.stab : params.slash;
	const float pathMin = range.pathMin * params.pathScale;
	const float pathMax = range.pathMax * params.pathScale;
	if(!contact.swing || range.cap <= 0.f || contact.speed < range.speedMin || contact.size < pathMin) {
		return floor;
	}
	const float speed = ramp(contact.speed, range.speedMin, range.speedMax);
	const float path = ramp(contact.size, pathMin, pathMax);
	float quality = std::min(speed, path); // only a big and fast stroke is the full strike
	if(params.quality == Quality::GeoMean) {
		quality = std::sqrt(speed * path);
	} else if(params.quality == Quality::Product) {
		quality = speed * path;
	}
	return MinStrikeAim + (1.f - MinStrikeAim) * std::min(quality, range.cap);
}

namespace {

//! The blade's direction: the grip's -Z (#75 lays the blade along the controller's handle)
Vec3f bladeDirection(const glm::quat & orientation) {
	return orientation * Vec3f(0.f, 0.f, -1.f);
}

float median(float a, float b, float c) {
	return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

Vec3f median(const Vec3f & a, const Vec3f & b, const Vec3f & c) {
	return Vec3f(median(a.x, b.x, c.x), median(a.y, b.y, c.y), median(a.z, b.z, c.z));
}

} // anonymous namespace

SwingChange SwingDetector::drop() {
	const bool was = swinging();
	m_kind = SwingKind::None;
	m_seeded = false;
	m_count = 0;
	m_stroke.clear();
	m_handVelocities = { };
	m_speed = 0.f;
	m_thrust = 0.f;
	m_thrusts.clear();
	return was ? SwingChange::Lost : SwingChange::None;
}

SwingChange SwingDetector::reset() {
	return drop();
}

SwingChange SwingDetector::update(const SwingSample & sample, bool tracked) {

	if(!tracked) {
		return drop();
	}

	const Vec3f blade = bladeDirection(sample.orientation);
	const Vec3f point = sample.hand + blade * m_params.lever;

	if(!m_seeded) {
		m_seeded = true;
		m_lastTime = sample.time;
		m_lastPoint = point;
		m_lastHand = sample.hand;
		m_hands[0] = sample.hand;
		m_blades[0] = blade;
		m_times[0] = sample.time;
		m_count = 1;
		m_stroke.assign(1, StrokeSample{ sample.hand, blade });
		return SwingChange::None;
	}

	const double step = sample.time - m_lastTime;
	if(step < double(m_params.minStep)) {
		return SwingChange::None; // the same pose again, or too close to measure
	}
	const float seconds = float(step);
	const float raw = glm::length(point - m_lastPoint) / seconds;
	// A long gap starts over: nobody knows the way the hand took across it - except in a swing that visibly went on
	// (#620: a hitch frame on a slow PC): its mean speed across the gap, as sound a measurement as any long step's
	// (#352), is at least the end speed, and the swing goes on from the hand's new place; a jump still ends it below
	if(step > double(m_params.maxStep) && !(swinging() && raw >= m_params.endSpeed)) {
		m_gaps++;
		const SwingChange change = drop();
		update(sample, true); // seed with it
		return change;
	}
	if(raw > m_params.jumpSpeed) {
		m_jumps++;
		const SwingChange change = drop();
		update(sample, true);
		return change;
	}

	// The speed of the lever point, read speedLag before this sample from the steps of the medians of three
	// positions (F-59's median of three, taken on the positions: a sample that is off and back gives two fast
	// speeds, but its position is never the median) - at 90 Hz the step between the median window's middle samples,
	// over its own time (a long frame followed by a short one must not read a speed the hand never had, #352); at
	// any other rate the velocity at that same moment (#620, pointVelocity)
	for(size_t i = m_hands.size() - 1; i > 0; i--) {
		m_hands[i] = m_hands[i - 1];
		m_blades[i] = m_blades[i - 1];
		m_times[i] = m_times[i - 1];
	}
	m_hands[0] = sample.hand;
	m_blades[0] = blade;
	m_times[0] = sample.time;
	m_count = std::min(m_count + 1, m_hands.size());
	m_speed = glm::length(pointVelocity(m_params.lever));
	if(m_speed > m_params.jumpSpeed) {
		// A guard that cannot fire (#352): the reading lies between two steps' velocities, and over its true span
		// a step of the medians never outruns the raw speed of that step, already checked when it came (on each
		// axis both medians lie between the two samples they share; the newest step, from the median to the newest
		// sample, is at most this frame's raw step). It stays as protection should the filter change; no test can
		// reach it
		m_jumps++;
		const SwingChange change = drop();
		update(sample, true);
		return change;
	}

	// The stroke (#465): it starts over from the sample before while the lever point is slow outside a swing - the
	// filtered speed lags a sample, so that one already moved - and when the hand turns back: a waggle is many small
	// strokes, not one big one. The hand's way now against three samples ago: at a sharp turn the median stands still
	// on the turning axis for two samples
	for(size_t i = m_handVelocities.size() - 1; i > 0; i--) {
		m_handVelocities[i] = m_handVelocities[i - 1];
	}
	m_handVelocities[0] = pointVelocity(0.f);
	const bool turnedBack = glm::dot(m_handVelocities[0], m_handVelocities.back()) < 0.f;
	if((!swinging() && m_speed < m_params.endSpeed) || turnedBack) {
		m_stroke.assign(1, StrokeSample{ m_hands[1], m_blades[1] });
	}
	if(m_stroke.size() >= MaxStroke) {
		m_stroke.erase(m_stroke.begin());
	}
	m_stroke.push_back(StrokeSample{ sample.hand, blade });

	// The thrust along the blade within the stab window (F-61)
	const float along = glm::dot(sample.hand - m_lastHand, blade);
	m_thrusts.push_back(Thrust{ sample.time, along });
	const double since = sample.time - double(m_params.stabWindow);
	m_thrusts.erase(std::remove_if(m_thrusts.begin(), m_thrusts.end(),
	                               [since](const Thrust & t) { return t.time < since; }),
	                m_thrusts.end());
	m_thrust = 0.f;
	for(const Thrust & t : m_thrusts) {
		m_thrust += t.along;
	}
	const float alongSpeed = along / seconds;

	m_lastTime = sample.time;
	m_lastPoint = point;
	m_lastHand = sample.hand;

	if(!swinging()) {
		SwingKind kind = SwingKind::None;
		if(m_speed >= m_params.startSpeed) {
			kind = SwingKind::Slash;
		} else if(m_thrust >= m_params.stabDistance && alongSpeed >= m_params.stabSpeed) {
			kind = SwingKind::Stab;
		}
		if(kind == SwingKind::None) {
			return SwingChange::None;
		}
		m_kind = kind;
		m_peak = std::max(m_speed, alongSpeed);
		m_startTime = sample.time;
		m_swings++;
		return SwingChange::Started;
	}

	m_peak = std::max(m_peak, m_speed);
	const bool slowed = m_speed < m_params.endSpeed;
	const bool tooLong = sample.time - m_startTime > double(m_params.maxDuration);
	if(slowed || tooLong) {
		m_kind = SwingKind::None;
		return SwingChange::Ended;
	}
	return SwingChange::None;
}

Vec3f SwingDetector::pointVelocity(float distance) const {
	if(m_count < 4) {
		return Vec3f(0.f);
	}
	const auto point = [&](size_t i) { return m_hands[i] + m_blades[i] * distance; };
	// The filtered points: the median of three neighbouring samples, at the middle one's time (F-59: for a sample
	// that is off and back the median is a neighbour's position)
	const auto filtered = [&](size_t k) { return median(point(k), point(k + 1), point(k + 2)); };
	// The steps between them, newest first - the newest sample itself from the newest filtered point at the front:
	// each is the mean velocity over its span (at least minStep: skipped samples never get here), read at the span's
	// middle. The velocity speedLag before the newest sample lies between the two middles around that moment; past
	// the newest middle (a long frame) it is the newest step's, before the oldest (short frames after a start) the
	// oldest's. At 90 Hz the moment is the middle of the step between the median window's middle samples: the
	// reading of the 1.5-frame median lag, the same at every rate (D-063, #620)
	const size_t steps = m_count - 2;
	const auto stepAt = [&](size_t i, Vec3f & velocity, double & middle) {
		const Vec3f from = filtered(i);
		const Vec3f to = i == 0 ? point(0) : filtered(i - 1);
		velocity = (to - from) / float(m_times[i] - m_times[i + 1]);
		middle = (m_times[i] + m_times[i + 1]) / 2.;
	};
	// A moment within a microsecond of a step's middle reads that step alone: at 90 Hz the moment and the middle are
	// the same time up to rounding (display times are nanosecond integers), and a 1e-16 share of the newest step would
	// flip the sign of a velocity that is exactly 0 - the median standing still at a turn - for the stroke's turn-back
	// rule, which read the plain step before
	constexpr double Snap = 1e-6;
	const double moment = m_times[0] - double(m_params.speedLag);
	Vec3f newer;
	double newerMiddle;
	stepAt(0, newer, newerMiddle);
	if(moment > newerMiddle - Snap) {
		return newer;
	}
	for(size_t i = 1; i < steps; i++) {
		Vec3f older;
		double olderMiddle;
		stepAt(i, older, olderMiddle);
		if(moment > olderMiddle + Snap) {
			const float share = float((moment - olderMiddle) / (newerMiddle - olderMiddle));
			return older + (newer - older) * share;
		}
		if(moment > olderMiddle - Snap) {
			return older;
		}
		newer = older;
		newerMiddle = olderMiddle;
	}
	return newer;
}

float SwingDetector::strokePath(float distance) const {
	float path = 0.f;
	for(size_t i = 1; i < m_stroke.size(); i++) {
		const Vec3f from = m_stroke[i - 1].hand + m_stroke[i - 1].blade * distance;
		const Vec3f to = m_stroke[i].hand + m_stroke[i].blade * distance;
		path += glm::length(to - from);
	}
	return path;
}

ContactMeasure measureContact(const SwingDetector & detector) {
	ContactMeasure contact;
	const Vec3f velocity = detector.pointVelocity(detector.params().lever);
	contact.speed = glm::length(velocity);
	contact.kind = contactKind(velocity, detector.blade(), detector.params());
	contact.size = contact.kind == ContactKind::Thrust ? detector.strokeLunge(0.f) : detector.strokePath(0.f);
	contact.swing = detector.swinging();
	return contact;
}

float SwingDetector::strokeLunge(float distance) const {
	if(m_stroke.size() < 2) {
		return 0.f;
	}
	const StrokeSample & first = m_stroke.front();
	const StrokeSample & last = m_stroke.back();
	const Vec3f moved = (last.hand + last.blade * distance) - (first.hand + first.blade * distance);
	return std::max(glm::dot(moved, last.blade), 0.f);
}

bool TargetCooldown::allowed(size_t target, u64 swing, double time, float cooldown) const {
	for(const Hit & hit : m_hits) {
		if(hit.target != target) {
			continue;
		}
		if(hit.swing == swing || time - hit.time < double(cooldown)) {
			return false;
		}
	}
	return true;
}

void TargetCooldown::hit(size_t target, u64 swing, double time) {
	// Forget hits long past any cooldown, and the target's own older hit
	m_hits.erase(std::remove_if(m_hits.begin(), m_hits.end(), [&](const Hit & h) {
		return h.target == target || time - h.time > 10.;
	}), m_hits.end());
	m_hits.push_back(Hit{ target, swing, time });
}

std::vector<size_t> TargetCooldown::blocked(u64 swing, double time, float cooldown) const {
	std::vector<size_t> result;
	for(const Hit & hit : m_hits) {
		if(!allowed(hit.target, swing, time, cooldown)) {
			result.push_back(hit.target);
		}
	}
	return result;
}

const char * meleeWeaponName(MeleeWeapon weapon) {
	switch(weapon) {
		case MeleeWeapon::Dagger: return "dagger";
		case MeleeWeapon::OneHanded: return "1h";
		case MeleeWeapon::TwoHanded: return "2h";
	}
	return "1h";
}

const char * recoverySourceName(RecoverySource source) {
	switch(source) {
		case RecoverySource::Default: return "default";
		case RecoverySource::Config: return "config";
	}
	return "default";
}

float defaultRecovery(MeleeWeapon weapon) {
	switch(weapon) {
		case MeleeWeapon::Dagger: return 0.6f;
		case MeleeWeapon::OneHanded: return 0.8f;
		case MeleeWeapon::TwoHanded: return 1.f;
	}
	return 0.8f;
}

RecoveryTime recoveryTime(MeleeWeapon weapon, float configured) {
	RecoveryTime result;
	if(configured >= 0.f) {
		result.seconds = configured;
		result.source = RecoverySource::Config;
	} else {
		result.seconds = defaultRecovery(weapon);
		result.source = RecoverySource::Default;
	}
	return result;
}

void StrikeRecovery::hit(double time, float seconds) {
	m_hit = true;
	m_until = time + double(std::max(seconds, 0.f));
}

float StrikeRecovery::remaining(double time) const {
	return ready(time) ? 0.f : float(m_until - time);
}

} // namespace vr
