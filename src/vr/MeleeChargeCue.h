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

#ifndef ARX_VR_MELEECHARGECUE_H
#define ARX_VR_MELEECHARGECUE_H

#include <array>
#include <string_view>
#include <vector>

#include "math/Types.h"
#include "vr/WristVitals.h"

/*!
 * \brief How the melee charge is shown in VR (#322): the variants the owner compares in the headset
 *
 * The trigger held with a weapon charges the strike (vr/VRMelee.h, the engine's \c Full_AimTime). Each
 * variant shows that charge, filling from empty to full with it and empty again on the strike:
 *  - \c glow:  the held weapon brightens - a warm light term added to its lighting;
 *  - \c wrist: a ring at the weapon hand's wrist, turned to the eye, filling clockwise from 12 o'clock;
 *  - \c hud:   the HUD's own hit strength gauge, drawn in VR whenever the IK hand holds the weapon;
 *  - \c blade: a light strip along the blade, from the hilt to the tip.
 * \c all shows every variant at once, \c off none. cfg.ini \c vr_melee_charge_cue, the channel's \c melee-cue.
 *
 * The pure part: the fill and the layouts, from the posed arm, the weapon and the eye - no engine globals,
 * so it is covered by host tests. The engine side is vr/VRChargeCue.h.
 */
namespace vr {

enum class ChargeCue {
	Off,
	Glow,
	Wrist,
	Hud,
	Blade,
	All
};

//! "off", "glow", "wrist", "hud", "blade" or "all"
const char * chargeCueName(ChargeCue cue);

//! \return false for a name that is no variant (case sensitive, as cfg.ini writes them)
bool parseChargeCue(std::string_view name, ChargeCue & cue);

//! \return true when the setting \a mode shows the single variant \a variant
bool chargeCueShows(ChargeCue mode, ChargeCue variant);

/*!
 * \return the charge as the cues show it: the time held against the player's \c Full_AimTime, 0 to 1 -
 *         the engine's charge ratio (\c StrikeAimtime, StrikeCharge::ratio) without its floor of 0.1, so a
 *         cue starts empty; 0 when not charging (\a seconds 0) or without a full time
 */
float chargeFill(float seconds, float fullSeconds);

//! The look of the cues (first proposals; the owner picks in the headset)
struct ChargeCueParams {
	float unitsPerMeter = 100.f;
	// The wrist ring
	float ringInnerM = 0.016f;    //!< the ring's inner radius
	float ringOuterM = 0.024f;    //!< the ring's outer radius
	float ringLiftM = 0.05f;      //!< from the wrist joint out of the back of the hand
	float ringBackM = 0.02f;      //!< from the wrist joint towards the elbow
	size_t ringSegments = 36;     //!< of the whole ring
	// The blade strip
	float stripWidthM = 0.012f;
	float stripTowardEyeM = 0.015f; //!< in front of the blade, so the blade does not cover it
	// The glow: the light added to the weapon's lighting at a full charge (0 .. 255 per channel)
	Vec3f glowFull = Vec3f(230.f, 150.f, 60.f);
};

//! A quad: two triangles 012, 023
typedef std::array<Vec3f, 4> CueQuad;

struct ChargeRingLayout {
	bool valid = false;           //!< false for a degenerate arm or an eye on the ring
	Vec3f center = Vec3f(0.f);
	Vec3f normal = Vec3f(0.f);    //!< towards the eye
	Vec3f up = Vec3f(0.f);        //!< 12 o'clock as the eye sees the ring
	Vec3f right = Vec3f(0.f);     //!< 3 o'clock as the eye sees the ring
	float fill = 0.f;             //!< [0, 1]
	float filledDeg = 0.f;        //!< the arc filled, clockwise from 12 o'clock
	std::vector<CueQuad> track;   //!< the whole ring (the dark backing)
	std::vector<CueQuad> filled;  //!< the filled arc; corners inner start, outer start, outer end, inner end
};

/*!
 * \param frame the weapon hand's posed arm (the palm normal out of the palm: the back of the hand is opposite)
 * \param eye the eye (or the head camera) the ring is seen from, in the world
 */
ChargeRingLayout layoutChargeRing(const WristFrame & frame, const Vec3f & eye, float fill,
                                  const ChargeCueParams & params = ChargeCueParams());

struct BladeStripLayout {
	bool valid = false;            //!< false without a blade (tip on the hilt) or with the eye on its line
	float fill = 0.f;
	Vec3f end = Vec3f(0.f);        //!< where the fill ends on the blade (the hilt when empty)
	CueQuad strip;                 //!< hilt side (two corners), then end side
};

/*!
 * \param hilt, tip the held weapon's attach vertex and blade tip in the world
 * \param eye the eye the strip is turned to
 */
BladeStripLayout layoutBladeStrip(const Vec3f & hilt, const Vec3f & tip, const Vec3f & eye, float fill,
                                  const ChargeCueParams & params = ChargeCueParams());

//! \return the light added to the held weapon's lighting for \a fill (0 .. 255 per channel, red green blue)
Vec3f chargeGlow(float fill, const ChargeCueParams & params = ChargeCueParams());

} // namespace vr

#endif // ARX_VR_MELEECHARGECUE_H
