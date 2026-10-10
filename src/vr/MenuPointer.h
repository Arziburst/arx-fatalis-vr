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

#ifndef ARX_VR_MENUPOINTER_H
#define ARX_VR_MENUPOINTER_H

#include <array>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/VRControls.h"

/*!
 * \file
 * \brief The menu pointer (#34): which hand moves the mouse over the floating panel, and where
 *
 * Pure logic: no OpenXR, no engine globals, no clock - covered by a host unit test
 * (tests/vr/MenuPointerTest.cpp). The input module (vr/VRInput.cpp) feeds it, once per frame, where
 * each hand's aim ray hits the panel (vr/UIPanel.h) and each hand's click / drag event; the pixel it
 * gives is the engine's mouse position, and the beam is drawn from the hand that owns it.
 *
 * The rules (D-010: a pointer only while a menu is open; D-016 / D-019: both hands work as the mouse;
 * F-81, F-175 - the pixel under the ray is computed in one place and feeds both the highlight and the
 * click):
 *  - Nothing while the panel is not shown: no cursor, no beam, the clicks are the game's.
 *  - **Owner:** the hand whose trigger was pressed last owns the cursor (owner rule 13g, D-019); a press
 *    of the other hand takes it over, and the cursor is under that hand in the same update, so the
 *    click lands where it points. While the owner's ray is off the panel and its trigger is up, the
 *    other hand's ray on the panel takes the cursor without a click.
 *  - **Click dead zone** (F-81): pulling the trigger moves the controller a little; the cursor holds
 *    the pixel of the press until the ray has moved \c clickDeadZone pixels away from it, then follows
 *    the ray again (a drag).
 *  - **A press off the panel never clicks:** the engine would click at the last cursor pixel, on
 *    whatever button is there. Such a press is muted until its trigger comes up.
 *
 * **The world (#37, D-016):** in the game, each hand's hidden ray also has a target in the world - its
 * reticle (vr/HandReticle.h). The same rules hold with "on a target" for "on the panel": the panel
 * wins where both are hit (it is drawn on top), a press with the reticle on the world clicks there,
 * a press on neither is muted. And while no trigger of the owner is held, a hand whose reticle moved
 * onto something new **claims** the cursor (D-019: the hand that moved last names what is under it).
 */
namespace vr {

//! Where one hand's aim ray points this frame
struct PointerRay {
	bool onPanel = false;     //!< the hand is tracked and its ray hits the shown panel
	bool onWorld = false;     //!< not on the panel: the hand's reticle is in the world (#37)
	Vec2f pixel = Vec2f(0.f); //!< the engine mouse for the hit (on the panel: the 2D interface's pixels)

	bool onTarget() const { return onPanel || onWorld; }
};

struct PointerParams {
	//! Pixels the ray must move from a press before the cursor follows it (VHVR: 16 px, F-81)
	float clickDeadZone = 16.f;
};

/*!
 * The beam's look (approved by the coordinator for the owner, 2026-10-10): a thin white line from the
 * hand to the panel, opaque at the hand and fading out towards the panel; the dot at its end is the
 * game's own cursor on the panel.
 */
constexpr float PointerBeamWidth = 0.003f; //!< meters
constexpr float PointerBeamAlpha = 0.9f;   //!< at the hand

//! The quad layer that draws a beam
struct BeamQuad {
	Vec3f center = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	Vec2f size = Vec2f(0.f); //!< width, length
};

/*!
 * \brief The quad of a beam from \a from (the hand) to \a to (its hit on the panel)
 *
 * The quad's +y runs from the hand to the panel (the image's bottom row is at the hand), and it is
 * turned about that axis so its face (+z) points as much towards \a eye as a quad on the line can.
 *
 * \return false for a beam shorter than a millimetre or an eye on its line - nothing to draw
 */
bool beamQuad(const Vec3f & from, const Vec3f & to, const Vec3f & eye, float width, BeamQuad & quad);

class MenuPointer {

public:

	/*!
	 * \param on         the pointer is on: the floating panel is shown for an open menu, or the hand
	 *                   reticles are on in the game (#37)
	 * \param rays       where each hand's ray hits the panel or the world
	 * \param trigger    each hand's click / drag event held now (the trigger by default)
	 * \param claims     each hand asks for an idle cursor (its reticle moved onto something new, or its
	 *                   use button fired) - granted while the owner's trigger is up and the hand is on a target
	 */
	void update(bool on, const std::array<PointerRay, HandCount> & rays,
	            const std::array<bool, HandCount> & trigger, const PointerParams & params = PointerParams(),
	            const std::array<bool, HandCount> & claims = { });

	//! The owner's ray is on a target: the cursor is \ref cursor
	bool active() const { return m_active; }

	//! The owner's ray is on the panel: the beam is shown (otherwise the cursor is a world reticle)
	bool onPanel() const { return m_active && m_onPanel; }

	//! The cursor holds the pixel of the owner's press (the click dead zone)
	bool holding() const { return m_holding; }

	Hand owner() const { return m_owner; }

	//! The engine's mouse position while \ref active, in the 2D interface's pixels
	Vec2f cursor() const { return m_cursor; }

	//! Every held click started off the panel: the engine must not see the button down
	bool suppressClick() const;

	//! The owner changed in the last update
	bool ownerChanged() const { return m_ownerChanged; }

	//! The hand pressed its trigger on the panel in the last update (a click the engine sees there)
	bool pressedOnPanel(Hand hand) const { return m_pressedOnPanel[size_t(hand)]; }

	//! The hand pressed its trigger with its reticle in the world in the last update (#37)
	bool pressedOnWorld(Hand hand) const { return m_pressedOnWorld[size_t(hand)]; }

	u64 clicks() const { return m_clicks; }                   //!< presses on the panel
	u64 worldClicks() const { return m_worldClicks; }         //!< presses with the reticle in the world
	u64 offPanelPresses() const { return m_offPanelPresses; } //!< presses on no target, muted
	u64 ownerChanges() const { return m_ownerChanges; }

private:

	void setOwner(Hand hand);

	bool m_active = false;
	bool m_onPanel = false;
	Hand m_owner = Hand::Right;
	Vec2f m_cursor = Vec2f(0.f);
	bool m_holding = false;         //!< the cursor holds the press pixel (the click dead zone)
	Vec2f m_pressPixel = Vec2f(0.f);
	std::array<bool, HandCount> m_trigger = { };
	std::array<bool, HandCount> m_muted = { };
	std::array<bool, HandCount> m_pressedOnPanel = { };
	std::array<bool, HandCount> m_pressedOnWorld = { };
	bool m_ownerChanged = false;
	u64 m_clicks = 0;
	u64 m_worldClicks = 0;
	u64 m_offPanelPresses = 0;
	u64 m_ownerChanges = 0;

};

} // namespace vr

#endif // ARX_VR_MENUPOINTER_H
