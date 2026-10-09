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

#ifndef ARX_VR_VRCOMFORT_H
#define ARX_VR_VRCOMFORT_H

/*!
 * \brief VR comfort policy (#76, F-31)
 *
 * The engine moves the first-person camera by itself in three ways, each with one source:
 *  - \b view \b bobbing: the camera follows the animated head of the player model
 *    (\ref ArxGame::updateFirstPersonCamera)
 *  - \b screen \b shake: quake effects jitter the camera position and angles (\ref ManageQuakeFX)
 *  - \b FOV \b zoom: the bow aim and the Magic Sight spell change the camera focal length
 *    (\ref ArxGame::updateLevel)
 *
 * On a monitor these are cues; in a headset the view is the player's own head, and every motion
 * or zoom that does not come from it is a motion-sickness trigger. The policy decides per kind:
 * in flat mode the player's cfg.ini choices decide as before (\c view_bobbing, \c screen_shake;
 * the zoom is always on), while the game runs in VR all three are off whatever cfg.ini says.
 * Each kind is removed at its source - never compensated afterwards - and cfg.ini is not modified.
 *
 * The test channel can force the VR rules on or off in a flat run (\c comfort \c on|off|auto),
 * so the result is measurable without a headset (docs/vr/TESTING.md).
 */
namespace vr {

//! What the engine may do to the first-person camera right now
struct ComfortPolicy {

	bool viewBobbing = true; //!< the camera may follow the animated head of the player model
	bool screenShake = true; //!< quake effects may jitter the camera
	bool fovZoom = true;     //!< the bow aim and Magic Sight may change the field of view

	bool operator==(const ComfortPolicy & other) const {
		return viewBobbing == other.viewBobbing && screenShake == other.screenShake && fovZoom == other.fovZoom;
	}
	bool operator!=(const ComfortPolicy & other) const { return !(*this == other); }

};

//! Who decides whether the VR rules apply
enum class ComfortMode {
	Auto, //!< the VR rules apply while an OpenXR session exists (the default)
	On,   //!< the VR rules apply, also in flat mode (test channel)
	Off   //!< the VR rules never apply, cfg.ini decides, also in VR (test channel)
};

/*!
 * \brief The policy for this moment
 *
 * Cheap enough to call at every source; logs one \c [vr] \c comfort: line whenever the answer
 * changes (first call, VR session created or lost, mode changed, cfg.ini option toggled).
 */
const ComfortPolicy & comfort();

//! \return true while the VR rules are applied (an OpenXR session exists, or the mode forces them on)
bool comfortActive();

void setComfortMode(ComfortMode mode);
ComfortMode comfortMode();

//! \return "auto", "on" or "off" - for the log and status.json
const char * comfortModeName(ComfortMode mode);

} // namespace vr

#endif // ARX_VR_VRCOMFORT_H
