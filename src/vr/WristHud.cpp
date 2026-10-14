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

#include "vr/WristHud.h"

#include <array>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/GameEvent.h"
#include "game/Camera.h"
#include "game/Player.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/Renderer.h"
#include "graphics/Vertex.h"
#include "graphics/data/Mesh.h"
#include "vr/EyeProjection.h"
#include "vr/VRBody.h"
#include "vr/VRControls.h"
#include "vr/VRHud.h"

namespace vr {

namespace {

//! The fill sits this far above its backing (meters), so the two never fight over the depth
const float FillLiftM = 0.0008f;

//! The empty part of a bar: dark, a little see-through
const Color4f BackColor = Color4f::rgba(0.04f, 0.03f, 0.03f, 0.7f);
//! Health red and mana blue, as the orbs of the flat HUD
const Color4f HealthColor = Color4f::rgba(0.85f, 0.08f, 0.06f, 1.f);
const Color4f ManaColor = Color4f::rgba(0.12f, 0.32f, 1.f, 1.f);
//! The level-up notice (#469): the gold of the flat game's level-up icon, a square this wide (meters)
const Color4f WristLevelUpColor = Color4f::rgba(1.f, 0.78f, 0.15f, 1.f);
const float WristLevelUpNoticeM = 0.014f;

struct WristState {
	WristVitalsStatus status;
	WristFrame frame;
	u64 poseCallsSeen = 0;
	bool shownBefore = false;
	const char * hiddenBefore = "";
};

WristState g_wrist;

float poolFraction(const ResourcePool & pool) {
	return (pool.max > 0.f) ? pool.current / pool.max : 0.f;
}

void noteShown(bool shown, const char * hidden) {
	WristVitalsStatus & status = g_wrist.status;
	status.shown = shown;
	status.hidden = hidden;
	if(shown != g_wrist.shownBefore || (!shown && std::strcmp(g_wrist.hiddenBefore, hidden) != 0)) {
		g_wrist.shownBefore = shown;
		g_wrist.hiddenBefore = hidden;
		gameevent::Event("vr.wrist", 200)("state", shown ? "wrist" : "orbs")("reason", hidden);
	}
}

void drawQuad(const std::array<Vec3f, 4> & corners, const Vec3f & lift, const Color4f & color, float alpha) {
	Color4f c = color;
	c.a *= alpha;
	const ColorRGBA rgba = c.toRGBA();
	TexturedVertex v[6];
	const size_t order[6] = { 0, 1, 2, 0, 2, 3 };
	for(size_t i = 0; i < 6; i++) {
		worldToClipSpace(corners[order[i]] + lift, v[i]);
		v[i].color = rgba;
	}
	EERIEDRAWPRIM(Renderer::TriangleList, v, 6);
}

} // anonymous namespace

void updateWristVitals() {

	const PlayerBodyStatus & body = playerBodyStatus();
	const PlayerArmStatus & arm = body.hands[size_t(Hand::Left)];

	// The posed arm is the one of this frame only if the player's skeleton was posed since the last
	// frame: a model not animated (not drawn) keeps the status of its last pose
	const bool fresh = (body.poseCalls != g_wrist.poseCallsSeen);
	g_wrist.poseCallsSeen = body.poseCalls;

	if(!fresh) {
		noteShown(false, "body not posed this frame");
		return;
	}
	if(!arm.posed) {
		noteShown(false, arm.tracked ? "left arm not posed" : "no left controller");
		return;
	}

	// The model's hand takes the grip's axes (HandFrame in vr/ArmIK.h): the palm normal is grip +X
	// (engine +X), the thumb's side grip -Z (engine +Z); the knuckles lie on grip -Y
	g_wrist.frame.elbow = arm.elbow;
	g_wrist.frame.wrist = arm.wrist;
	g_wrist.frame.palmNormal = arm.grip * Vec3f(1.f, 0.f, 0.f);
	g_wrist.frame.thumb = arm.grip * Vec3f(0.f, 0.f, 1.f);

	g_wrist.status.health = poolFraction(player.lifePool);
	g_wrist.status.mana = poolFraction(player.manaPool);
	noteShown(true, "");
}

bool wristVitalsShown() {
	return g_wrist.status.shown;
}

void drawWristVitals() {

	WristVitalsStatus & status = g_wrist.status;
	if(!status.shown || !g_camera) {
		return;
	}

	WristVitalsParams params;
	params.unitsPerMeter = EyeRigParams().unitsPerMeter;
	const WristVitalsLayout layout = layoutWristVitals(g_wrist.frame, g_camera->m_pos, status.health, status.mana,
	                                                   params);
	status.facingDeg = layout.facingDeg;
	status.alpha = layout.valid ? layout.alpha : 0.f;
	status.center = layout.center;
	status.normal = layout.normal;
	status.across = layout.across;
	status.along = layout.along;
	if(!layout.valid || layout.alpha <= 0.f) {
		return;
	}
	status.drawnViews++;

	// In the world, against the depth of the scene (a wall or the other hand covers them), on top
	// of the arm's skin they float just above; seen from either side, without fog
	UseRenderState state(render3D().fog(false).depthWrite(false).blend(BlendSrcAlpha, BlendInvSrcAlpha));
	GRenderer->ResetTexture(0);

	const Vec3f fillLift = layout.normal * (FillLiftM * params.unitsPerMeter);
	drawQuad(layout.health.back, Vec3f(0.f), BackColor, layout.alpha);
	drawQuad(layout.mana.back, Vec3f(0.f), BackColor, layout.alpha);
	if(layout.health.fraction > 0.f) {
		drawQuad(layout.health.fill, fillLift, HealthColor, layout.alpha);
	}
	if(layout.mana.fraction > 0.f) {
		drawQuad(layout.mana.fill, fillLift, ManaColor, layout.alpha);
	}

	// A level-up (#469): a gold square beyond the mana bar, towards the hand, pulsing for a few seconds
	const LevelUpIndicator & levelUp = levelUpIndicator();
	if(levelUp.wristNotice()) {
		const Vec3f manaMiddle = (layout.mana.back[0] + layout.mana.back[1] + layout.mana.back[2] + layout.mana.back[3])
		                         * 0.25f;
		const float half = WristLevelUpNoticeM * 0.5f * params.unitsPerMeter;
		const Vec3f middle = manaMiddle + layout.along * ((params.barWidthM * 0.5f + params.gapM) * params.unitsPerMeter
		                                                  + half);
		const std::array<Vec3f, 4> square = { {
			middle - layout.along * half - layout.across * half, middle + layout.along * half - layout.across * half,
			middle + layout.along * half + layout.across * half, middle - layout.along * half + layout.across * half,
		} };
		const float pulse = 0.65f + 0.35f * std::cos(levelUp.noticeProgress() * glm::two_pi<float>() * 4.f);
		drawQuad(square, fillLift, WristLevelUpColor, layout.alpha * pulse);
		noteLevelUpWristDrawn();
	}
}

const WristVitalsStatus & wristVitalsStatus() {
	return g_wrist.status;
}

} // namespace vr
