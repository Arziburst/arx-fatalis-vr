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

#ifndef ARX_VR_HEADREFERENCEGATE_H
#define ARX_VR_HEADREFERENCEGATE_H

#include "math/Types.h"

/*!
 * \brief When the head's reference is taken: from the worn head, settled after putting it on (#27, D-018)
 *
 * The head at the reference is the character's eye (HeadPose.h), so the reference decides the eye
 * height and the heading: it must be the head of the player standing (or sitting) in the headset, not
 * a headset lying on the desk while the level loads. The owner's logs show exactly that: the game loads
 * with the headset idle (session \c synchronized) and the headset is put on 50-80 s later (\c visible /
 * \c focused) - a reference from the first tracked sample put the camera far above the body.
 *
 * Rules:
 *  - The headset is \b worn while the session is \c visible or \c focused; an idle headset never gets
 *    there (TESTING.md).
 *  - While it is not worn, a reset gets a \b provisional reference (the first sample after it), so the
 *    head always turns the camera; nobody sees that view.
 *  - When it is \b put on, the head moves while the straps are adjusted: the \b settled reference is
 *    taken once the head has stayed within \c settleMeters for \c settleSeconds, or after
 *    \c maxWaitSeconds when it keeps moving (F-30: TF2VR settles over a few samples within 1 cm).
 *  - Taking it off re-arms the gate: the next time it is put on, a new settled reference is taken - the
 *    standing or seated head of whoever wears it now (D-018, seated play the same).
 *  - A reset while it is worn (level load, a new head source, the runtime's or the player's recenter)
 *    is settled at once: the player is in the headset, and a second reference later would turn the
 *    view again if the head turned meanwhile. A reset during the settle after putting it on waits for it.
 *  - An \c instant head (simulated through the test channel) never waits.
 *
 * Pure logic: positions in meters and a clock in seconds come in, no engine globals, no OpenXR.
 * Host-tested in tests/vr/HeadReferenceGateTest.cpp.
 */
namespace vr {

enum class ReferenceKind {
	None,        //!< no reference yet
	Provisional, //!< from a headset that is not worn, until it is put on and settles
	Settled      //!< the worn head: the standing (or seated) head = the character's eye
};

//! \return none, provisional or settled
const char * referenceKindName(ReferenceKind kind);

struct ReferenceGateParams {
	float settleSeconds = 0.5f;  //!< after putting it on: how long the head stays within settleMeters
	float settleMeters = 0.03f;  //!< how far it may move meanwhile
	float maxWaitSeconds = 3.f;  //!< after putting it on: taken even if the head keeps moving
};

class HeadReferenceGate {

public:

	explicit HeadReferenceGate(const ReferenceGateParams & params = ReferenceGateParams())
		: m_params(params)
	{ }

	//! A new reference is wanted (level load, new head source, recenter)
	void reset();

	/*!
	 * \brief Feed one head sample
	 *
	 * \param seconds a monotonic clock
	 * \param position the head in the runtime's space (meters)
	 * \param worn the headset is on someone's head (session visible or focused, or a simulated head)
	 * \param instant the head needs no settling (a simulated head)
	 * \return the kind of reference to take from this sample, \c None to keep the current one
	 */
	ReferenceKind update(double seconds, const Vec3f & position, bool worn, bool instant = false);

	//! The kind of the reference in use (the last one returned by \ref update)
	ReferenceKind current() const { return m_current; }

	//! A settled reference is wanted and not taken yet
	bool pending() const { return m_wanted; }

	bool worn() const { return m_worn; }

	//! \return how long the headset has been worn at \a seconds; 0 while it is not
	double wornSeconds(double seconds) const { return m_worn ? seconds - m_wornSince : 0.0; }

private:

	ReferenceGateParams m_params;

	ReferenceKind m_current = ReferenceKind::None;
	bool m_wanted = true;          //!< a settled reference is wanted
	bool m_needProvisional = true; //!< a reset asked for a reference now

	bool m_worn = false;
	bool m_settling = false;       //!< put on and not settled yet
	double m_wornSince = 0.0;

	bool m_anchorValid = false;    //!< where the worn head has stayed since m_anchorSince
	Vec3f m_anchor = Vec3f(0.f);
	double m_anchorSince = 0.0;

};

} // namespace vr

#endif // ARX_VR_HEADREFERENCEGATE_H
