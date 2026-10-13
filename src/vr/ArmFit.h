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

#ifndef ARX_VR_ARMFIT_H
#define ARX_VR_ARMFIT_H

/*!
 * \file
 * \brief The custom arm fitted to the player (#385.6): pure math, host-tested in tests/vr/ArmFitTest.cpp
 *
 * No engine globals: lengths in, lengths out (game units). The custom arm (vr/VRArms.cpp) takes its two
 * segment lengths from the player's real reach once it is measured, and from the character height setting
 * (#260) until then; it keeps the Arx arm's upper arm to forearm ratio. A controller beyond that reach is
 * met by stretching the arm by at most \ref MaxArmStretch (research row F-307: arm stretch scored best on
 * embodiment, CHI'24 arXiv 2407.08011) - not by sliding the shoulder (vr/ArmIK.h maxShoulderSlide), which
 * stays the logical Arx arm's rule.
 */
namespace vr {

//! How far the custom arm may stretch past its fitted length to reach the controller, a fraction of that length:
//! the default of cfg.ini vr_arm_max_stretch, which the owner tunes in the headset
constexpr float MaxArmStretch = 0.10f;
//! The largest stretch vr_arm_max_stretch may ask for
constexpr float MaxArmStretchLimit = 0.3f;

//! \return \a stretch within [0, MaxArmStretchLimit] (MaxArmStretch for a value that is not a number)
float clampArmMaxStretch(float stretch);

//! The two segments of an arm, units
struct ArmLengths {
	float upper = 0.f;   //!< shoulder to elbow
	float forearm = 0.f; //!< elbow to wrist
};

//! The upper arm's share of the Arx arm (its bones: upper arm 27.6, forearm 30.5 units)
constexpr float ArxUpperRatio = 27.6f / (27.6f + 30.5f);

/*!
 * The shoulder to the wrist against the body height: upper arm 0.186 + forearm 0.146 of the stature, the
 * segment lengths of Drillis and Contini (1966) as tabulated in D. A. Winter, Biomechanics and Motor Control
 * of Human Movement (4th ed., 2009), figure 4.1
 */
constexpr float ArmToStature = 0.186f + 0.146f;

/*!
 * The eye above the floor against the body height, from the same table (0.936): what turns the character's
 * eye height into a stature for \ref armLengthsFromHeight
 */
constexpr float EyeToStature = 0.936f;

/*!
 * A reach sample counts as the arm at full extension only within this band of the height's arm (shoulder to
 * wrist, \ref armLengthsFromHeight): nearer is a bent arm, farther is no human arm (a controller set down
 * away from the player, a simulated pose)
 */
constexpr float MinExtendedReach = 0.85f;
constexpr float MaxExtendedReach = 1.3f;

//! \return \a shoulderToWrist is a sample of the arm at full extension for the arm \a heightReach long
bool isExtendedReach(float shoulderToWrist, float heightReach);

/*!
 * \return the arm of a body \a heightUnits tall (\ref ArmToStature), split by \a upperRatio
 */
ArmLengths armLengthsFromHeight(float heightUnits, float upperRatio = ArxUpperRatio);

/*!
 * \return the arm whose shoulder to wrist is \a reach, split by \a upperRatio
 */
ArmLengths armLengthsFromReach(float reach, float upperRatio = ArxUpperRatio);

/*!
 * \brief The player's reach: the farthest the wrist was from the shoulder, robust to one tracking glitch
 *
 * Fed with the shoulder-to-wrist distance of the arm at full extension; the reach is the largest sample
 * after dropping the single largest one. It is a measurement after \ref MinSamples samples.
 */
class ReachTracker {

public:

	//! Samples before the reach counts (a second of the arm held out, in total, at 90 frames a second)
	static constexpr int MinSamples = 90;

	void add(float shoulderToWrist);
	bool measured() const { return m_samples >= MinSamples; }
	//! The second largest sample (0 before two samples)
	float reach() const { return m_second; }
	void reset();

private:

	float m_largest = 0.f;
	float m_second = 0.f;
	int m_samples = 0;

};

/*!
 * \return the stretch fraction the solve uses for a target \a shoulderToTarget away: 0 within the arm's
 *         length, the overshoot against that length beyond it, at most \a maxStretch
 */
float armStretch(float shoulderToTarget, const ArmLengths & lengths, float maxStretch = MaxArmStretch);

} // namespace vr

#endif // ARX_VR_ARMFIT_H
