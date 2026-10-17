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

#include <string>
#include <string_view>

#include "vr/StableView.h"

/*!
 * \brief VR comfort policy (#76, F-31)
 *
 * The engine moves the first-person camera by itself in three ways, each with one source:
 *  - \b view \b bobbing: the camera follows the animated head of the player model
 *    (\ref ArxGame::updateFirstPersonCamera)
 *  - \b screen \b shake: quake effects jitter the camera position and angles (\ref ManageQuakeFX)
 *  - \b FOV \b zoom: the bow aim and the Magic Sight spell change the camera focal length
 *    (\ref ArxGame::updateLevel)
 * and a fourth that bobbing off left (#416): the first-person eye still sits on the model's paused animation,
 * which every stance, jump, landing, crouch, strike and the death fall move - in VR the \b stable \b view puts
 * the camera on the body's real travel instead and makes death a still ghost (vr/StableView.h).
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
	//! the first-person camera stands on the body's real travel, not on the animated model, and death is a
	//! still ghost (#416, vr/StableView.h) - off: the engine's eye and death camera
	bool stableView = false;

	bool operator==(const ComfortPolicy & other) const {
		return viewBobbing == other.viewBobbing && screenShake == other.screenShake && fovZoom == other.fovZoom
		       && stableView == other.stableView;
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

/*!
 * \brief Who decides the stable view (#416) on its own: \c auto follows the comfort policy (on with the VR
 *        rules), \c on / \c off force it - the test channel's \c view \c stable, which measures the engine's
 *        camera and the stable one in one run
 */
void setStableViewMode(ComfortMode mode);
ComfortMode stableViewMode();

//! The stable view's variants as cfg.ini (or the channel) sets them (#416); a value that is not one is the default
struct ViewVariants {
	JumpView jump = JumpView::Level;
	CrouchGlide crouch = CrouchGlide::Slow;
	DeathView death = DeathView::Headless;
};

//! \return the variants now; logs one \c [vr] \c view: line whenever they change
const ViewVariants & viewVariants();

//! \return the parameters of \ref updateStableView for the variants now
StableViewParams stableViewParams();

/*!
 * \brief Set one variant (\c jump, \c crouch or \c death) in the running game's config, as cfg.ini would
 * \return an error, empty when it was set
 */
std::string setViewVariant(std::string_view name, std::string_view value);

//! The view's height, carried from frame to frame by the first-person camera (ArxGame::updateFirstPersonCamera)
StableView & stableView();

//! \return true while the stable view placed this frame's first-person camera (not a ghost, not the engine's eye)
bool stableViewPlaced();
void setStableViewPlaced(bool placed);

/*!
 * \brief The still ghost at death (#416): the view stays where the eye was when the hero died, only the head and
 *        the stick turn move it - set by the death camera (ArxGame::handlePlayerDeath), cleared when alive
 */
bool deathGhost();
void setDeathGhost(bool ghost);

//! The camera's motion since the last \ref resetCameraMotion (status.json \c camera.motion, the channel's \c view \c meter)
CameraMotion & cameraMotion();

/*!
 * \brief The time this frame is shown after the last one - once per frame, before the first-person camera
 *
 * In VR the predicted display times of two frames (a whole number of display slots: 11.1 ms at 90 Hz, more for a
 * missed slot) - what the eye sees the step over, free of the game clock's jitter; in flat mode (or before the first
 * located frame) the game's frame time. The camera motion meter measures speeds with it.
 */
void noteViewFrame(float gameSeconds);
float viewFrameSeconds();

/*!
 * \return \ref viewFrameSeconds, but never more than one display slot (the shortest interval of the last 30 frames):
 *         the stable view's step - a frame shown late (a hitch) slows the view's glide instead of making it jump, so
 *         one shown frame moves it at most 2 cm at 90 Hz
 */
float viewStepSeconds();

//! Log the stable view's inputs and state for the next \a frames frames (at most 2000), one \c [vr] \c view \c trace: line each
void setViewTrace(int frames);
//! \return true when this frame is traced (counts it down)
bool takeViewTraceFrame();

//! Write the camera motion since the last reset as one \c vr.view.motion game event named \a name - a row of the catalog
void reportCameraMotion(std::string_view name);

} // namespace vr

#endif // ARX_VR_VRCOMFORT_H
