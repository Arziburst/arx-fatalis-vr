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

#include "vr/VRChargeCue.h"

#include <glm/glm.hpp>

#include "core/Config.h"
#include "core/GameEvent.h"
#include "game/Camera.h"
#include "game/Entity.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/Renderer.h"
#include "graphics/Vertex.h"
#include "graphics/data/Mesh.h"
#include "io/log/Logger.h"
#include "vr/EyeProjection.h"
#include "vr/MeleeChargeCue.h"
#include "vr/VRBody.h"
#include "vr/VRControls.h"
#include "vr/VRMelee.h"
#include "vr/VRWeapon.h"

namespace vr {

namespace {

//! The empty ring: dark, a little see-through
const Color4f CueTrackColor = Color4f::rgba(0.04f, 0.03f, 0.03f, 0.6f);

struct ChargeCueState {
	ChargeCueStatus status;
	ChargeCue mode = ChargeCue::Off;
	bool modeKnown = false;
	std::string modeSetting;       //!< the setting the mode was parsed from
	WristFrame frame;              //!< the weapon hand's arm this frame
	bool armPosed = false;
	Vec3f hilt = Vec3f(0.f);
	Vec3f tip = Vec3f(0.f);
	bool placed = false;
};

ChargeCueState g_cue;

//! Warm orange when the charge starts, a bright yellow at full
Color4f cueFillColor(float fill, float alpha) {
	return Color4f::rgba(1.f, 0.45f + 0.45f * fill, 0.1f + 0.5f * fill, alpha);
}

void drawCueQuad(const CueQuad & corners, const Color4f & color) {
	const ColorRGBA rgba = color.toRGBA();
	TexturedVertex v[6];
	const size_t order[6] = { 0, 1, 2, 0, 2, 3 };
	for(size_t i = 0; i < 6; i++) {
		worldToClipSpace(corners[order[i]], v[i]);
		v[i].color = rgba;
	}
	EERIEDRAWPRIM(Renderer::TriangleList, v, 6);
}

void readCueMode() {
	if(g_cue.modeKnown && g_cue.modeSetting == config.input.vrMeleeChargeCue) {
		return;
	}
	g_cue.modeKnown = true;
	g_cue.modeSetting = config.input.vrMeleeChargeCue;
	ChargeCue mode = ChargeCue::All;
	if(!parseChargeCue(g_cue.modeSetting, mode)) {
		LogWarning << "[vr] melee charge cue: unknown variant \"" << g_cue.modeSetting << "\" - showing all";
		mode = ChargeCue::All;
	}
	g_cue.mode = mode;
	g_cue.status.mode = chargeCueName(mode);
	g_cue.status.changes++;
	gameevent::Event("melee.cue", 200)("mode", chargeCueName(mode));
}

} // anonymous namespace

void updateChargeCue() {

	readCueMode();

	ChargeCueStatus & status = g_cue.status;
	const MeleeStatus & melee = meleeStatus();
	const Hand hand = meleeHand();
	status.hand = handName(hand);
	status.active = melee.active;
	status.fill = melee.active ? chargeFill(melee.chargeSeconds, melee.fullAimSeconds) : 0.f;

	status.glowShown = status.active && chargeCueShows(g_cue.mode, ChargeCue::Glow);
	status.wristShown = status.active && chargeCueShows(g_cue.mode, ChargeCue::Wrist);
	status.hudShown = status.active && chargeCueShows(g_cue.mode, ChargeCue::Hud);
	status.bladeShown = status.active && chargeCueShows(g_cue.mode, ChargeCue::Blade);

	// The glow: added to the light the weapon is drawn with this frame (the highlight is reset every frame)
	status.glow = 0.f;
	Entity * weapon = weaponInHand();
	if(status.glowShown && weapon) {
		const Vec3f glow = chargeGlow(status.fill);
		weapon->highlightColor += Color3f(glow.x, glow.y, glow.z);
		status.glow = status.fill;
	}

	// The weapon hand's arm and blade for the views of this frame. The palm normal is out of the palm:
	// the grip's +X points into the right palm and out of the left one (vr/ArmIK.h)
	const PlayerArmStatus & arm = playerBodyStatus().hands[size_t(hand)];
	g_cue.armPosed = arm.posed;
	g_cue.frame.elbow = arm.elbow;
	g_cue.frame.wrist = arm.wrist;
	g_cue.frame.palmNormal = arm.grip * Vec3f(hand == Hand::Left ? 1.f : -1.f, 0.f, 0.f);
	g_cue.frame.thumb = arm.grip * Vec3f(0.f, 0.f, 1.f);
	g_cue.placed = melee.placed;
	g_cue.hilt = melee.hilt;
	g_cue.tip = melee.tip;
	if(!status.wristShown) {
		status.wristFill = 0.f;
	}
	if(!status.bladeShown) {
		status.bladeFill = 0.f;
	}
}

void drawChargeCue() {

	ChargeCueStatus & status = g_cue.status;
	if((!status.wristShown && !status.bladeShown) || !g_camera) {
		return;
	}

	ChargeCueParams params;
	params.unitsPerMeter = EyeRigParams().unitsPerMeter;
	const Vec3f eye = g_camera->m_pos;

	// In the world, against the depth of the scene, without fog, seen from either side
	GRenderer->ResetTexture(0);

	if(status.wristShown && g_cue.armPosed) {
		const ChargeRingLayout ring = layoutChargeRing(g_cue.frame, eye, status.fill, params);
		status.wristFill = ring.valid ? ring.filledDeg / 360.f : 0.f;
		if(ring.valid) {
			UseRenderState state(render3D().fog(false).depthWrite(false).blend(BlendSrcAlpha, BlendInvSrcAlpha));
			for(const CueQuad & quad : ring.track) {
				drawCueQuad(quad, CueTrackColor);
			}
			// The fill a little nearer to the eye than its backing, so the two never fight over the depth
			const Vec3f lift = ring.normal * (0.0008f * params.unitsPerMeter);
			for(CueQuad quad : ring.filled) {
				for(Vec3f & corner : quad) {
					corner += lift;
				}
				drawCueQuad(quad, cueFillColor(status.fill, 1.f));
			}
			status.wristViews++;
		}
	}

	if(status.bladeShown && g_cue.placed && status.fill > 0.f) {
		const BladeStripLayout strip = layoutBladeStrip(g_cue.hilt, g_cue.tip, eye, status.fill, params);
		status.bladeFill = strip.valid ? strip.fill : 0.f;
		if(strip.valid) {
			// Light added to the scene: a glow along the blade
			UseRenderState state(render3D().fog(false).depthWrite(false).blendAdditive());
			drawCueQuad(strip.strip, cueFillColor(status.fill, 1.f));
			status.bladeViews++;
		}
	} else if(status.bladeShown) {
		status.bladeFill = 0.f;
	}
}

bool chargeCueHudShown() {
	return g_cue.status.hudShown;
}

const ChargeCueStatus & chargeCueStatus() {
	return g_cue.status;
}

} // namespace vr
