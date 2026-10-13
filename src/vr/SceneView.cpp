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

#include "vr/SceneView.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

#include "vr/BodyYaw.h"

namespace vr {

const char * sceneViewName(SceneViewSetting view) {
	switch(view) {
		case SceneViewSetting::First: return "first";
		case SceneViewSetting::Observer: return "observer";
		case SceneViewSetting::Ghost: return "ghost";
		case SceneViewSetting::Hybrid: return "hybrid";
	}
	return "first";
}

std::optional<SceneViewSetting> parseSceneView(std::string_view name) {
	for(SceneViewSetting view : { SceneViewSetting::First, SceneViewSetting::Observer, SceneViewSetting::Ghost,
	                              SceneViewSetting::Hybrid }) {
		if(name == sceneViewName(view)) {
			return view;
		}
	}
	return std::nullopt;
}

const char * sceneShownName(SceneShown shown) {
	switch(shown) {
		case SceneShown::None: return "none";
		case SceneShown::First: return "first";
		case SceneShown::Observer: return "observer";
		case SceneShown::Ghost: return "ghost";
		case SceneShown::Screen: return "screen";
	}
	return "none";
}

const char * sceneLookAtName(SceneLookAt lookAt) {
	return lookAt == SceneLookAt::Fade ? "fade" : "ignore";
}

std::optional<SceneLookAt> parseSceneLookAt(std::string_view name) {
	if(name == "ignore") {
		return SceneLookAt::Ignore;
	}
	if(name == "fade") {
		return SceneLookAt::Fade;
	}
	return std::nullopt;
}

const char * sceneKindName(SceneKind kind) {
	switch(kind) {
		case SceneKind::None: return "none";
		case SceneKind::Speech: return "speech";
		case SceneKind::Entity: return "entity";
	}
	return "none";
}

void SceneFade::request(float outSeconds, float inSeconds, int blackFrames) {

	const float outSpeed = 1.f / std::max(outSeconds, 0.001f);
	const float inSpeed = 1.f / std::max(inSeconds, 0.001f);

	switch(m_phase) {
		case Phase::Idle:
		case Phase::In: {
			m_phase = Phase::Out;
			m_outSpeed = outSpeed;
			m_inSpeed = inSpeed;
			m_blackFrames = blackFrames;
			break;
		}
		case Phase::Out:
		case Phase::Black: {
			m_outSpeed = std::max(m_outSpeed, outSpeed);
			m_inSpeed = std::max(m_inSpeed, inSpeed);
			m_blackFrames = std::max(m_blackFrames, blackFrames);
			break;
		}
	}

}

void SceneFade::update(float seconds) {

	seconds = std::max(seconds, 0.f);

	switch(m_phase) {
		case Phase::Idle: {
			break;
		}
		case Phase::Out: {
			m_value += seconds * m_outSpeed;
			if(m_value >= 1.f) {
				m_value = 1.f;
				m_phase = Phase::Black;
				m_hold = m_blackFrames;
			}
			break;
		}
		case Phase::Black: {
			if(m_hold > 0) {
				m_hold--;
				break;
			}
			m_phase = Phase::In;
			[[fallthrough]];
		}
		case Phase::In: {
			m_value -= seconds * m_inSpeed;
			if(m_value <= 0.f) {
				m_value = 0.f;
				m_phase = Phase::Idle;
			}
			break;
		}
	}

}

void SceneFade::holdBlack() {
	if(isBlack()) {
		m_phase = Phase::Black;
		m_hold = m_blackFrames;
	}
}

SceneShown chooseShownView(SceneViewSetting setting, SceneKind kind) {

	// Only the letterbox: the game's camera is the hero's own, there is no scene camera to hold
	if(kind == SceneKind::None) {
		return SceneShown::First;
	}

	switch(setting) {
		case SceneViewSetting::Ghost: return SceneShown::Ghost;
		// #29.2 places the observer and the hybrid's screen here; until then both show the hero's head
		case SceneViewSetting::Observer:
		case SceneViewSetting::Hybrid:
		case SceneViewSetting::First: return SceneShown::First;
	}

	return SceneShown::First;
}

bool letterboxInEyes(SceneShown /* shown */) {
	return false;
}

bool showsHeroOutside(SceneShown shown) {
	return shown == SceneShown::Ghost || shown == SceneShown::Observer;
}

Vec3f clampAboveFloor(const Vec3f & pos, float floorY, bool floorFound, float minAbove) {
	Vec3f result = pos;
	// y is down: the floor's y is larger than a point above it
	if(floorFound && floorY - pos.y < minAbove) {
		result.y = floorY - minAbove;
	}
	return result;
}

float yawDistance(float a, float b) {
	return std::abs(wrapSignedDegrees(a - b));
}

SceneOutput SceneDirector::update(const SceneInput & in) {

	SceneOutput out;

	// The shot the eyes should see now
	Shot target;
	if(in.active) {
		target.shown = chooseShownView(in.setting, in.kind);
		target.kind = in.kind;
		target.shot = in.shot;
	}
	Vec3f targetPos = in.firstPos;
	const bool following = (target.shown != SceneShown::Ghost);
	if(target.shown == SceneShown::Ghost) {
		// A ghost shot is the game's camera where the shot starts; a jump of the game's camera within it is
		// a new shot (a camera entity moved elsewhere)
		const bool jumped = m_ghost.valid
		                    && (glm::distance(in.enginePos, m_ghost.lastEnginePos) > CutJumpUnits
		                        || yawDistance(in.engineYaw, m_ghost.lastEngineYaw) > CutJumpDegrees);
		if(!m_ghost.valid || m_ghost.kind != in.kind || m_ghost.shot != in.shot || jumped) {
			m_ghost.valid = true;
			m_ghost.kind = in.kind;
			m_ghost.shot = in.shot;
			m_ghost.serial++;
			m_ghost.pos = clampAboveFloor(in.enginePos, in.engineFloorY, in.engineFloor);
			m_ghost.yaw = in.engineYaw;
		}
		m_ghost.lastEnginePos = in.enginePos;
		m_ghost.lastEngineYaw = in.engineYaw;
		target.ghostSerial = m_ghost.serial;
		targetPos = m_ghost.pos;
	} else {
		m_ghost.valid = false;
	}

	// A different shot, or a view that follows the hero jumping (a teleport in a scene), is a cut: the shot
	// shown is held until the fade is black. Without a scene the game's first person is followed as it is
	const bool differs = (target != m_shown);
	const bool jump = !differs && following && target.shown != SceneShown::None
	                  && glm::distance(targetPos, m_pos) > CutJumpUnits;
	if(differs || jump) {
		if(!m_cutPending) {
			m_cutPending = true;
			m_fade.request(m_params.cutOut, m_params.cutIn, m_params.blackFrames);
		}
	} else if(following) {
		m_pos = targetPos;
	}

	m_fade.update(in.seconds);

	if(m_fade.isBlack() && m_cutPending) {
		m_cutPending = false;
		// The target may have come back to the shot shown meanwhile: then there is nothing to cut
		const bool differsNow = (target != m_shown);
		const bool jumpNow = following && target.shown != SceneShown::None
		                     && glm::distance(targetPos, m_pos) > CutJumpUnits;
		if(differsNow || jumpNow) {
			out.cut = true;
			if(m_shown.shown == SceneShown::None && target.shown != SceneShown::None) {
				out.cutReason = "start";
				out.started = true;
			} else if(m_shown.shown != SceneShown::None && target.shown == SceneShown::None) {
				out.cutReason = "end";
				out.ended = true;
				out.endedKind = m_shown.kind;
			} else if(m_shown.kind != target.kind) {
				// A scene of another kind (the letterbox first, the camera entity a moment later): its own start
				out.cutReason = "start";
				out.ended = true;
				out.endedKind = m_shown.kind;
				out.started = true;
			} else if(!differsNow) {
				out.cutReason = "jump";
			} else if(target.kind == SceneKind::Speech && target.shot != m_shown.shot) {
				out.cutReason = "line";
			} else {
				out.cutReason = "camera";
			}
			m_shown = target;
			m_pos = targetPos;
			// The ghost's yaw: where the player looks now is where the game's camera looked when the shot began
			m_yawDelta = (target.shown == SceneShown::Ghost)
			             ? wrapSignedDegrees(m_ghost.yaw - in.headYaw - in.playerYaw) : 0.f;
			m_fade.holdBlack();
		}
	}

	if(m_fade.isBlack() && m_lookAtPending) {
		m_lookAtPending = false;
		out.applyLookAt = true;
		m_fade.holdBlack();
	}

	out.shown = m_shown.shown;
	out.kind = m_shown.kind;
	out.drive = in.active || m_shown.shown != SceneShown::None;
	out.pos = m_pos;
	out.yawDelta = m_yawDelta;
	out.fade = m_fade.value();

	return out;
}

bool SceneDirector::lookAt(SceneLookAt mode) {
	if(mode != SceneLookAt::Fade) {
		return false;
	}
	m_lookAtPending = true;
	m_fade.request(m_params.lookAtOut, m_params.lookAtIn, m_params.blackFrames);
	return true;
}

void SceneDirector::reset() {
	*this = SceneDirector(m_params);
}

SceneState & sceneState() {
	static SceneState s_state;
	return s_state;
}

void SceneMeter::add(const SceneMeterFrame & frame) {

	m_frames++;
	if(frame.active) {
		m_sceneFrames++;
	}
	if(frame.shown == SceneShown::Screen) {
		m_screenFrames++;
	}

	const bool behindFade = frame.fade >= BehindFade;

	// Cuts over all frames: the shot changed, or the camera jumped (the player's own turn is not one)
	const bool jumped = m_havePrevious
	                    && (glm::distance(frame.pos, m_previousPos) > CutJumpUnits
	                        || (!frame.ownTurn && yawDistance(frame.yaw, m_previousYaw) > CutJumpDegrees));
	if(frame.shotChanged || jumped) {
		m_cuts++;
		if(behindFade) {
			m_fadedCuts++;
		}
	}
	m_havePrevious = true;
	m_previousPos = frame.pos;
	m_previousYaw = frame.yaw;

	// Height and steps on the scene's frames that are seen: one game unit is a centimetre
	const bool measured = frame.shown != SceneShown::None && frame.shown != SceneShown::Screen && !behindFade;
	if(!measured) {
		m_haveMeasured = false;
		return;
	}
	const float above = std::max(frame.aboveFloor, 0.f);
	m_minAboveFloor = m_measured ? std::min(m_minAboveFloor, above) : above;
	m_measured++;
	if(m_haveMeasured) {
		m_maxStep = std::max(m_maxStep, glm::distance(frame.pos, m_measuredPos));
		if(!frame.ownTurn) {
			m_maxYawStep = std::max(m_maxYawStep, yawDistance(frame.yaw, m_measuredYaw));
		}
	}
	m_haveMeasured = true;
	m_measuredPos = frame.pos;
	m_measuredYaw = frame.yaw;

}

} // namespace vr
