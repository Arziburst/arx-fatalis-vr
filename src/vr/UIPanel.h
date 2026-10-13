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

#ifndef ARX_VR_UIPANEL_H
#define ARX_VR_UIPANEL_H

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/HeadPose.h"

/*!
 * \file
 * \brief The floating UI panel (#49): where the 2D interface is shown while a menu is open
 *
 * Pure logic: no OpenXR, no OpenGL, no engine globals, no clock - covered by a host unit test
 * (tests/vr/UIPanelTest.cpp). The VR module (vr/VRSystem.cpp) feeds it the engine's menu state and
 * the head of each frame and submits the 2D interface's quad where it says.
 *
 * The owner's design (D-017, agreed 2026-10-10; research rows F-80, F-82, F-180): menus, the
 * inventory and the book open on a flat panel about 0.9 m ahead at chest height, placed where the
 * player looks when it opens and then fixed in the play space - it does not follow the head. With
 * no menu open the 2D interface (the HUD) stays on the head-locked quad until it moves to the wrist
 * (#116). "A menu is open" is the engine's menu state, never a timer (F-82).
 *
 * Coordinates are OpenXR's LOCAL space: meters, x right, y up, -z forward. A quad layer is seen
 * from its +z side, so the panel's +z axis points back at the player.
 */
namespace vr {

/*!
 * The panel's defaults - the owner's numbers (D-017, confirmed for #49 on 2026-10-10): 0.9 m ahead,
 * the centre 0.3 m below the eyes (chest height), 1 m wide. The one place to change them; cfg.ini
 * overrides them (F-180: the distance is the most-asked setting).
 */
constexpr float DefaultPanelDistance = 0.9f;
constexpr float DefaultPanelDrop = 0.3f;
constexpr float DefaultPanelWidth = 1.0f;

/*!
 * The screen the publisher logos and the cinematics play on (#394): a proposal for the owner to
 * judge in the headset (D-NEW) - 2.5 m ahead at eye height, 2.6 m wide (about 55 degrees of the
 * view, a cinema seat in the middle rows), placed where the player looks when one starts and fixed
 * in the play space while it plays, like the menu panel.
 */
constexpr float ScreenDistance = 2.5f;
constexpr float ScreenDrop = 0.f;
constexpr float ScreenWidth = 2.6f;

//! Where the panel goes; \c vr_panel_distance, \c vr_panel_drop and \c vr_panel_width in cfg.ini
struct PanelParams {
	float distance = DefaultPanelDistance; //!< horizontally ahead of the head, meters
	float drop = DefaultPanelDrop;         //!< the panel's centre below the eyes, meters (chest height)
	float width = DefaultPanelWidth;       //!< meters; the height follows the 2D interface's aspect
	//! A menu that opens again within this time after closing keeps the panel where it was
	//! (switching from the inventory to the book, F-80)
	s64 reopenGraceUs = 250000;
};

//! The panel's centre and orientation in the LOCAL space
struct PanelPose {
	Vec3f position = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
};

/*!
 * \return the horizontal direction the head faces (unit, y = 0): its view direction with the pitch
 *         and roll taken out. Looking (nearly) straight up or down, where the view direction has no
 *         heading left, the top of the head gives it instead - looking down at the floor still
 *         faces the way the player stands.
 */
Vec3f headHeading(const glm::quat & orientation);

/*!
 * \brief Place the panel for a head: on the head's heading only (yaw), \c distance ahead,
 *        \c drop below the eyes, turned to face the eyes
 *
 * Only the yaw places it, so a player who looks down when opening the inventory does not get the
 * panel on the floor. The panel is tilted back about its horizontal axis so its face points at the
 * eyes (for the defaults about 18 degrees).
 */
PanelPose placePanel(const HeadSample & head, const PanelParams & params = PanelParams());

//! \return the panel's size in meters: \a width wide, as tall as the image's aspect makes it
Vec2f panelSize(float width, const Vec2i & imageSize);

/*!
 * \brief Where a ray hits the panel - the hook for the menu pointer (#34, F-175)
 *
 * \param size      the panel's size in meters (\ref panelSize)
 * \param uv        receives the hit in the panel's image: u to the right, v \b down, 0..1 inside -
 *                  the engine's pixel is <tt>uv * image size</tt>
 * \param distance  receives the distance along the ray (in units of \a direction's length)
 *
 * \return false if the ray misses the panel, runs parallel to it, hits it from behind or points away
 */
bool panelHit(const PanelPose & pose, const Vec2f & size, const Vec3f & origin, const Vec3f & direction,
              Vec2f & uv, float & distance);

//! \return the point on the panel at \a uv (u right, v down, 0..1) - the inverse of \ref panelHit
Vec3f panelPoint(const PanelPose & pose, const Vec2f & size, const Vec2f & uv);

//! Where the 2D interface's quad is shown
enum class PanelMode {
	HeadLocked, //!< no menu open: in front of the eyes, moving with the head (the HUD)
	World,      //!< a menu is open: the panel, fixed in the play space
};

//! \return "head" or "world" - for status.json and the log
const char * panelModeName(PanelMode mode);

//! Why the panel was placed
enum class PanelPlacement {
	None,     //!< not placed in this update
	Open,     //!< a menu opened (the first time, or again after the grace time)
	Recenter, //!< a recenter while a menu was open
	Tracking, //!< a menu opened while the head was not tracked; placed when the head came back
};

//! \return "open", "recenter", "tracking" or "none"
const char * panelPlacementName(PanelPlacement placement);

/*!
 * \brief The panel's state over time: placed once when a menu opens, fixed while it stays open
 *
 * One \ref update per frame. A menu that opens places the panel at the head (\ref placePanel); while
 * the menu stays open the pose stays, whatever the head does; a menu that closes gives the 2D
 * interface back to the head-locked quad. A menu that opens again within \c reopenGraceUs keeps the
 * old pose; later it is placed anew. A recenter (\ref recenter) while a menu is open places it anew.
 * Without a tracked head the panel is not placed at some made-up origin (the trap of research row
 * F-128): it stays head-locked until the head is tracked again.
 */
class PanelAnchor {

public:

	/*!
	 * \param menuOpen  the engine's menu state this frame (F-82)
	 * \param nowUs     a monotonic time in microseconds (the frame's display time)
	 * \param head      the head this frame, or nullptr while it is not tracked
	 * \return why the panel was placed in this update, \c None if it was not
	 */
	PanelPlacement update(bool menuOpen, s64 nowUs, const HeadSample * head,
	                      const PanelParams & params = PanelParams());

	//! Place the panel anew at the next update while a menu is open (a recenter)
	void recenter() { m_recenter = true; }

	PanelMode mode() const { return (m_open && m_placed) ? PanelMode::World : PanelMode::HeadLocked; }

	//! \return the panel's pose; valid while \ref mode is \c World (and kept after it closes)
	const PanelPose & pose() const { return m_pose; }

	//! \return how often the panel was placed
	u64 placements() const { return m_placements; }

	//! \return true while the engine says a menu is open
	bool menuOpen() const { return m_open; }

private:

	bool m_open = false;
	bool m_placed = false;
	bool m_recenter = false;
	bool m_waitedForTracking = false;
	PanelPlacement m_pending = PanelPlacement::None; //!< a placement is due, for this reason
	s64 m_closedAt = 0;
	PanelPose m_pose;
	u64 m_placements = 0;

};

} // namespace vr

#endif // ARX_VR_UIPANEL_H
