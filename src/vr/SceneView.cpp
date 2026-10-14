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

const char * sceneFirstLowName(SceneFirstLow low) {
	switch(low) {
		case SceneFirstLow::Black: return "black";
		case SceneFirstLow::Clamp: return "clamp";
		case SceneFirstLow::Screen: return "screen";
	}
	return "clamp";
}

std::optional<SceneFirstLow> parseSceneFirstLow(std::string_view name) {
	if(name == "black") {
		return SceneFirstLow::Black;
	}
	if(name == "clamp") {
		return SceneFirstLow::Clamp;
	}
	if(name == "screen") {
		return SceneFirstLow::Screen;
	}
	return std::nullopt;
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
			// Asked for at black: it stays black long enough to be taken there, not fading in the same frame
			if(m_phase == Phase::Black) {
				m_hold = std::max(m_hold, std::max(blackFrames, 1));
			}
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

SceneShown chooseShownView(SceneViewSetting setting, SceneKind kind, bool hybridScreen) {

	// Only the letterbox: the game's camera is the hero's own, there is no scene camera to hold
	if(kind == SceneKind::None) {
		return SceneShown::First;
	}

	switch(setting) {
		case SceneViewSetting::Ghost: return SceneShown::Ghost;
		case SceneViewSetting::Observer: return SceneShown::Observer;
		case SceneViewSetting::Hybrid: {
			return (kind == SceneKind::Entity && hybridScreen) ? SceneShown::Screen : SceneShown::First;
		}
		case SceneViewSetting::First: return SceneShown::First;
	}

	return SceneShown::First;
}

bool hybridWantsScreen(const HybridShot & shot, float farUnits) {
	return shot.heroScripted || !shot.lineOfSight || glm::distance(shot.camera, shot.heroHead) > farUnits;
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

float yawTowards(const Vec3f & from, const Vec3f & to) {
	// The engine's forward for a yaw is (-sin, 0, cos) (angleToVectorXZ)
	const float yaw = glm::degrees(std::atan2(-(to.x - from.x), to.z - from.z));
	return yaw < 0.f ? yaw + 360.f : yaw;
}

ObserverPlace placeObserver(const ObserverInput & in, const ObserverWorld & world, const ObserverParams & params) {

	// The line between the heads, on the ground plane; heads too close span it along the hero's facing
	Vec3f other = in.otherHead;
	Vec3f line(other.x - in.heroHead.x, 0.f, other.z - in.heroHead.z);
	if(glm::length(line) < params.minHeadSpan) {
		const float t = glm::radians(in.heroYaw);
		line = Vec3f(-std::sin(t), 0.f, std::cos(t)) * params.minHeadSpan;
		other = Vec3f(in.heroHead.x + line.x, in.heroHead.y, in.heroHead.z + line.z);
	}
	const float span = glm::length(line);
	const Vec3f middle = (in.heroHead + other) * 0.5f;
	const Vec3f across = Vec3f(-line.z, 0.f, line.x) / span;
	const float offset = std::clamp(span * params.offsetRatio, params.minOffset, params.maxOffset);

	// Each side as far as the level lets it, short of the wall its ray meets
	float room[2];
	for(int side = 0; side < 2; side++) {
		const Vec3f dir = side == 0 ? across : -across;
		const float free = world.freeDistance ? world.freeDistance(middle, middle + dir * offset) : offset;
		room[side] = std::clamp(free >= offset ? offset : free - params.wallMargin, 0.f, offset);
	}
	int side = room[0] >= room[1] ? 0 : 1;
	if(room[0] >= offset && room[1] >= offset) {
		// Both fit: the side the game's camera films from
		side = glm::dot(in.gameCamera - middle, across) >= 0.f ? 0 : 1;
	}
	const Vec3f dir = side == 0 ? across : -across;

	// The player's real eye height above the floor there; no floor: back towards the middle until there is one
	ObserverPlace place;
	place.pos = middle;
	for(float part : { 1.f, 0.5f, 0.25f, 0.f }) {
		const Vec3f pos = middle + dir * (room[side] * part);
		const std::optional<float> floorY = world.floorBelow ? world.floorBelow(pos) : std::nullopt;
		if(floorY) {
			place.pos = Vec3f(pos.x, *floorY - in.eyeHeight, pos.z);
			break;
		}
	}
	place.yaw = yawTowards(place.pos, middle);

	return place;
}

SceneOutput SceneDirector::update(const SceneInput & in) {

	SceneOutput out;

	// The hybrid keeps the screen for a camera entity's shot once the shot wanted it: a camera that glides
	// out of the far range, or a hero whose scripted animation ends, does not flip the view mid-shot
	if(in.kind == SceneKind::Entity) {
		if(m_hybrid.shot != in.shot) {
			m_hybrid.shot = in.shot;
			m_hybrid.screen = false;
		}
		m_hybrid.screen = m_hybrid.screen || in.hybridScreen;
	} else {
		m_hybrid = HybridChoice();
	}

	// The hero's eye lower than a standing player's minimum above the floor under it (#29)
	const bool firstTooLow = in.firstOnFloor && in.firstFloorY - in.firstPos.y < GhostMinAboveFloor;

	// The shot the eyes should see now; the observer and the screen keep the eyes' camera through the
	// scene's camera entities and lines - only a change of view or kind is a cut
	Shot target;
	if(in.active) {
		target.shown = chooseShownView(in.setting, in.kind, m_hybrid.screen);
		target.kind = in.kind;
		// First person's eye too low (the hero lying or dragged by a script): the screen, for the rest of the scene
		if(target.shown == SceneShown::First && in.firstLow == SceneFirstLow::Screen && (firstTooLow || m_lowScreen)) {
			target.shown = SceneShown::Screen;
			m_lowScreen = true;
		}
		const bool keeps = (target.shown == SceneShown::Observer || target.shown == SceneShown::Screen);
		target.shot = keeps ? 0 : in.shot;
	} else {
		m_lowScreen = false;
	}
	if(target.shown != SceneShown::Observer) {
		m_observer.valid = false;
	}
	Vec3f targetPos = in.firstPos;
	if(target.shown == SceneShown::First && in.firstLow == SceneFirstLow::Clamp) {
		targetPos = clampAboveFloor(in.firstPos, in.firstFloorY, in.firstOnFloor);
	}
	const bool following = (target.shown == SceneShown::None || target.shown == SceneShown::First);
	// Black: the eye that was last high enough is held while the fade goes out - a low one is never shown - and
	// until the hero's eye is up again (#29)
	if(in.active && target.shown == SceneShown::First && in.firstLow == SceneFirstLow::Black) {
		const bool up = in.firstFloorY - in.firstPos.y >= GhostMinAboveFloor + FirstLowRise;
		m_lowHeld = firstTooLow || (m_lowHeld && !up);
	} else {
		m_lowHeld = false;
	}
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
	// shown is held - its place and its yaw - until the fade is black. Without a scene the game's first
	// person is followed as it is
	const bool differs = (target != m_shown);
	// A script turning the hero at once (teleport -a, a long frame of a scripted turn) is a jump too; the stick's own
	// turn is the player's and never one
	const float scriptTurn = m_haveHeroYaw
	                         ? wrapSignedDegrees(in.playerYaw - m_lastHeroYaw + (in.stickTurned - m_lastStick)) : 0.f;
	m_haveHeroYaw = true;
	m_lastHeroYaw = in.playerYaw;
	m_lastStick = in.stickTurned;
	const bool turnJump = std::abs(scriptTurn) > CutJumpDegrees;
	const bool jump = !differs && following && !m_lowHeld && target.shown != SceneShown::None
	                  && (glm::distance(targetPos, m_pos) > CutJumpUnits || turnJump);
	// A cut that waits with no fade under way would wait for ever: fade again (#29)
	if(m_cutPending && !m_fade.busy()) {
		m_cutPending = false;
		out.cutRestarted = true;
	}
	if(differs || jump) {
		if(!m_cutPending) {
			m_cutPending = true;
			m_turnCut = jump && turnJump;
			m_heldYaw = m_haveLastYaw ? m_lastYaw : in.playerYaw + m_yawDelta;
			m_fade.request(m_params.cutOut, m_params.cutIn, m_params.blackFrames);
		}
	} else if(following && !m_lowHeld) {
		m_pos = targetPos;
	}

	m_fade.update(in.seconds);

	if(m_fade.isBlack() && m_cutPending) {
		m_cutPending = false;
		// The target may have come back to the shot shown meanwhile: then there is nothing to cut
		const bool differsNow = (target != m_shown);
		const bool jumpNow = following && target.shown != SceneShown::None
		                     && (glm::distance(targetPos, m_pos) > CutJumpUnits || m_turnCut);
		m_turnCut = false;
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
			switch(target.shown) {
				case SceneShown::Ghost: {
					// Where the player looks now is where the game's camera looked when the shot began
					m_pos = targetPos;
					m_yawDelta = wrapSignedDegrees(m_ghost.yaw - in.headYaw - in.playerYaw);
					fixYaw(m_ghost.yaw - in.headYaw, in.stickTurned);
					break;
				}
				case SceneShown::Observer: {
					// Placed once for the scene, now that its first shot has set the speakers up
					if(!m_observer.valid) {
						m_observer.valid = true;
						if(in.placeObserver) {
							m_observer.place = in.placeObserver();
						} else {
							m_observer.place.pos = in.firstPos;
							m_observer.place.yaw = in.playerYaw;
						}
					}
					// Where the player looks now is the middle of the heads
					m_pos = m_observer.place.pos;
					m_yawDelta = wrapSignedDegrees(m_observer.place.yaw - in.headYaw - in.playerYaw);
					fixYaw(m_observer.place.yaw - in.headYaw, in.stickTurned);
					break;
				}
				case SceneShown::Screen: {
					// The eyes' camera stays where it was, at the yaw held: the game's camera is on the screen
					m_yawDelta = 0.f;
					m_fixedYaw = false;
					break;
				}
				case SceneShown::None:
				case SceneShown::First: {
					m_pos = targetPos;
					m_yawDelta = 0.f;
					m_fixedYaw = false;
					break;
				}
			}
			m_fade.holdBlack();
		}
	}

	// Every view shows nothing rather than an eye with no floor under it (the owner, #29): the fade stays black.
	// The hero's eye in first person (a new game's hero stands off the level before the prison intro), a ghost's
	// shot or the observer's point; the screen has no eye in the world
	bool onFloor = true;
	if(m_shown.shown == SceneShown::First) {
		onFloor = in.firstOnFloor && !m_lowHeld;
	} else if(m_shown.shown == SceneShown::Ghost || m_shown.shown == SceneShown::Observer) {
		onFloor = !in.hasFloor || in.hasFloor(m_pos);
	}
	const bool blind = in.active && m_shown == target && !onFloor;
	if(blind) {
		if(m_fade.isBlack()) {
			m_fade.holdBlack();
		} else {
			m_fade.request(m_params.cutOut, m_params.cutIn, m_params.blackFrames);
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
	// A script turning the hero within a ghost's or the observer's shot (the prison intro's teleport -pa) turns
	// the hero, not the eyes; the stick still does (#29)
	if(m_fixedYaw && (m_shown.shown == SceneShown::Ghost || m_shown.shown == SceneShown::Observer)) {
		m_yawDelta = wrapSignedDegrees(m_shotYaw - (in.stickTurned - m_turnedAtCut) - in.playerYaw);
	}
	const bool holdYaw = m_cutPending || m_shown.shown == SceneShown::Screen;
	out.yawDelta = holdYaw ? wrapSignedDegrees(m_heldYaw - in.playerYaw) : m_yawDelta;
	out.fade = m_fade.value();
	out.blind = blind;

	m_haveLastYaw = true;
	m_lastYaw = in.playerYaw + out.yawDelta;

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
	if(frame.blind) {
		m_blindFrames++;
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
