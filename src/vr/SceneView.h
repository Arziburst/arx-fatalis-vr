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

#ifndef ARX_VR_SCENEVIEW_H
#define ARX_VR_SCENEVIEW_H

#include <optional>
#include <string_view>

#include "math/Angle.h"
#include "math/Types.h"
#include "platform/Platform.h"

/*!
 * \file
 * \brief What the eyes see in a scripted scene (#29, D-NEW)
 *
 * A scripted scene owns the game's camera: a \c speak \c -c line moves it every frame (hard cuts between
 * lines, dollies and pans within one), a camera entity (\c cameraactivate) glides it on a path, and the
 * letterbox (\c cinemascope) frames it. In a headset each of these jerks the world. The scene view picks
 * what the eyes get instead (\ref SceneViewSetting, cfg.ini \c vr_scene_view):
 *  - \c first (a): the eyes stay at the hero's head, the game's scene cameras are ignored;
 *  - \c ghost (c): the game's camera positions, each shot held where it starts (no dolly, no pan), yaw
 *    only (pitch and roll come from the head), never lower than \ref GhostMinAboveFloor;
 *  - \c observer (b) and \c hybrid (d): #29.2 - until then they show \c first.
 *
 * Every change of the shot the eyes see is a cut behind a fade (\ref SceneFade): the previous shot is held
 * while the world fades out, the new one is taken at black, then the world fades in. A shot is the
 * scene's identity (a new \c speak \c -c line, another camera entity, the scene's start and end) plus,
 * for a view that follows the hero, a jump of its position (a teleport). \c playerlookat either changes
 * nothing (\c ignore) or turns the hero behind a short fade (\c fade, cfg.ini \c vr_scene_lookat).
 *
 * \ref SceneMeter measures the result for the tests (status.json \c scene.meter).
 *
 * Pure logic - no engine globals, no clock reads - covered by tests/vr/SceneViewTest. The game side
 * (the engine's cameras, the head pose, the drawing) is in ArxGame.cpp.
 */
namespace vr {

//! The player's choice of view for scripted scenes (cfg.ini \c vr_scene_view, channel \c scene \c view)
enum class SceneViewSetting : u8 {
	First,    //!< (a) the hero's head
	Observer, //!< (b) #29.2
	Ghost,    //!< (c) the game's camera positions, held per shot
	Hybrid    //!< (d) #29.2
};

//! What the eyes get this frame (status.json \c scene.shown)
enum class SceneShown : u8 {
	None,     //!< no scene: the game's own first-person view
	First,
	Observer,
	Ghost,
	Screen    //!< the hybrid's screen (#29.2)
};

//! What \c playerlookat does in VR (cfg.ini \c vr_scene_lookat, channel \c scene \c lookat)
enum class SceneLookAt : u8 {
	Ignore, //!< counted, the hero's yaw stays
	Fade    //!< the hero turns behind a short fade
};

//! What owns the game's camera (status.json \c scene.kind)
enum class SceneKind : u8 {
	None,   //!< no scene camera (also a scene with only the letterbox on)
	Speech, //!< a \c speak \c -c line
	Entity  //!< a camera entity (\c cameraactivate)
};

const char * sceneViewName(SceneViewSetting view);
std::optional<SceneViewSetting> parseSceneView(std::string_view name);
const char * sceneShownName(SceneShown shown);
const char * sceneLookAtName(SceneLookAt lookAt);
std::optional<SceneLookAt> parseSceneLookAt(std::string_view name);
const char * sceneKindName(SceneKind kind);

//! The defaults (owner's answers on #29, 2026-10-13; the final pick is in the headset)
constexpr SceneViewSetting DefaultSceneView = SceneViewSetting::Hybrid;
constexpr SceneLookAt DefaultSceneLookAt = SceneLookAt::Ignore;

//! A change of the eyes' camera bigger than this in one frame is a cut (SPEC.md): 50 cm, 10 degrees
constexpr float CutJumpUnits = 50.f;
constexpr float CutJumpDegrees = 10.f;

//! The ghost's camera never lower above the floor under it (game units): 1.2 m, and 1 mm against rounding
constexpr float GhostMinAboveFloor = 120.1f;

//! A frame drawn with the scene fade at least this black is behind the fade (SPEC.md)
constexpr float BehindFade = 0.99f;

/*!
 * The comfort numbers of the fades (proposals for the owner to tune in the headset): a cut fades out in
 * 0.15 s while the previous shot is held - the head-in-wall fade's speed - and back in 0.25 s; a turn of
 * \c playerlookat takes about 0.2 s; black is held two frames after a switch so the hero's model and its
 * eye catch up with a teleport or a turn before anything is seen again
 */
struct SceneFadeParams {
	float cutOut = 0.15f;
	float cutIn = 0.25f;
	float lookAtOut = 0.1f;
	float lookAtIn = 0.1f;
	int blackFrames = 2;
};

/*!
 * The scene's fade to black and back: \ref request starts it, \ref update advances it once per frame,
 * \ref isBlack says when the switch may happen, \ref holdBlack keeps it black a few frames after one
 */
class SceneFade {

public:

	//! Fade out in \a outSeconds, then back in \a inSeconds; the faster of two pending requests wins
	void request(float outSeconds, float inSeconds, int blackFrames);

	//! One frame of \a seconds
	void update(float seconds);

	//! Keep black for the black frames from now (a switch happened)
	void holdBlack();

	float value() const { return m_value; }
	bool isBlack() const { return m_value >= 1.f; }
	bool busy() const { return m_phase != Phase::Idle; }

private:

	enum class Phase : u8 { Idle, Out, Black, In };

	Phase m_phase = Phase::Idle;
	float m_value = 0.f;
	float m_outSpeed = 0.f;
	float m_inSpeed = 0.f;
	int m_blackFrames = 0;
	int m_hold = 0;

};

/*!
 * \brief The view a scene is shown in: the place where the shown view is chosen per frame
 *
 * \c observer and \c hybrid show \c first until #29.2 builds them. A scene without a scene camera (only the
 * letterbox) has nothing to hold: \c first in every view.
 */
SceneShown chooseShownView(SceneViewSetting setting, SceneKind kind);

//! \return the hero shown in third person in this view: the whole model drawn, the IK arms off (#29)
bool showsHeroOutside(SceneShown shown);

//! eturn the cinematic bars are drawn into the eyes in this view - in none: in a headset they are black bars
//!         glued to the face; the cinemascope state stays for the game (#29)
bool letterboxInEyes(SceneShown shown);

//! \return \a pos raised to \ref GhostMinAboveFloor above \a floorY (game units, y down); no floor: unchanged
Vec3f clampAboveFloor(const Vec3f & pos, float floorY, bool floorFound, float minAbove = GhostMinAboveFloor);

//! \return the angle between the yaws \a a and \a b, degrees in [0, 180]
float yawDistance(float a, float b);

//! One frame of the game's cameras, game units and engine degrees
struct SceneInput {
	bool active = false;            //!< a scripted scene owns the camera (speech, camera entity or letterbox)
	SceneKind kind = SceneKind::None;
	u64 shot = 0;                   //!< the scene camera's identity: changes with a new line or camera entity
	SceneViewSetting setting = DefaultSceneView;
	Vec3f enginePos = Vec3f(0.f);   //!< the camera the game chose this frame
	float engineYaw = 0.f;
	float engineFloorY = 0.f;       //!< the floor under \c enginePos (y down)
	bool engineFloor = false;       //!< a floor was found under it
	Vec3f firstPos = Vec3f(0.f);    //!< the hero's eye (the first-person camera)
	float playerYaw = 0.f;          //!< the hero's yaw now
	float headYaw = 0.f;            //!< the head's own yaw since its reference, degrees
	float seconds = 0.f;            //!< the frame's duration
};

//! What the eyes' camera is this frame, before the head: \c pos and the hero's yaw plus \c yawDelta
struct SceneOutput {

	SceneShown shown = SceneShown::None; //!< the shot the eyes see
	SceneKind kind = SceneKind::None;    //!< its kind
	bool drive = false;                  //!< the scene view places the camera (else the game's own first person)
	Vec3f pos = Vec3f(0.f);
	float yawDelta = 0.f;                //!< added to the hero's yaw (the ghost's shot); the head goes on top
	float fade = 0.f;                    //!< the scene fade drawn this frame, 0..1

	bool cut = false;                    //!< the shot changed this frame (at black)
	const char * cutReason = "";         //!< line, camera, jump, start, end
	bool started = false;                //!< a scene shot replaced the game's view, or a scene of another kind
	bool ended = false;                  //!< a scene shot was left (the game's view, or a scene of another kind)
	SceneKind endedKind = SceneKind::None;
	bool applyLookAt = false;            //!< turn the hero to the pending \c playerlookat now (at black)

};

/*!
 * \brief Decides per frame which shot the eyes see and when it changes
 *
 * The shot the eyes should see (the target) follows the game: no scene - the game's first person; a scene -
 * \ref chooseShownView. A different target, or a jump of a following view, starts a cut: the shot shown is
 * held until the fade is black, then the target is taken. A ghost shot is the game's camera where the shot
 * starts, and a new one when the game's camera jumps within it; its yaw is set at the switch so that where
 * the player looks now is where the game's camera looked.
 */
class SceneDirector {

public:

	explicit SceneDirector(const SceneFadeParams & params = SceneFadeParams()) : m_params(params) { }

	SceneOutput update(const SceneInput & in);

	/*!
	 * A \c playerlookat in VR. \return true when the hero should be turned later, at black (\c fade): the
	 * game then keeps the angle until \ref SceneOutput::applyLookAt; \c ignore turns nothing
	 */
	bool lookAt(SceneLookAt mode);

	//! Forget the scene state (a level load, the VR session gone)
	void reset();

	float fade() const { return m_fade.value(); }
	SceneShown shown() const { return m_shown.shown; }

private:

	struct Shot {
		SceneShown shown = SceneShown::None;
		SceneKind kind = SceneKind::None;
		u64 shot = 0;
		u64 ghostSerial = 0;
		bool operator==(const Shot & o) const {
			return shown == o.shown && kind == o.kind && shot == o.shot && ghostSerial == o.ghostSerial;
		}
		bool operator!=(const Shot & o) const { return !(*this == o); }
	};

	struct Ghost {
		bool valid = false;
		SceneKind kind = SceneKind::None;
		u64 shot = 0;
		u64 serial = 0;
		Vec3f pos = Vec3f(0.f);
		float yaw = 0.f;
		Vec3f lastEnginePos = Vec3f(0.f);
		float lastEngineYaw = 0.f;
	};

	SceneFadeParams m_params;
	SceneFade m_fade;
	Shot m_shown;
	Vec3f m_pos = Vec3f(0.f);
	float m_yawDelta = 0.f;
	Ghost m_ghost;
	bool m_cutPending = false;
	bool m_lookAtPending = false;

};

//! One frame for \ref SceneMeter
struct SceneMeterFrame {
	bool active = false;                //!< a scripted scene owns the game's camera
	SceneShown shown = SceneShown::None;
	float fade = 0.f;                   //!< the scene fade drawn this frame
	Vec3f pos = Vec3f(0.f);             //!< the eyes' camera before the head, game units
	float yaw = 0.f;                    //!< its yaw before the head
	float aboveFloor = 0.f;             //!< it above the floor under it, meters; no floor: 0
	bool shotChanged = false;           //!< the shot changed this frame (\ref SceneOutput::cut)
	bool ownTurn = false;               //!< the player's stick turned the hero since the last frame: no yaw step
};

/*!
 * \brief The comfort of a stretch of VR frames (status.json \c scene.meter, SPEC.md)
 *
 * Height and steps are measured on the frames that show a scene (not the screen) and are not behind the
 * fade; a step is from the last such frame, so a fade or a gap ends the comparison. Cuts are counted over
 * all frames: a shot change, or a jump of the eyes' camera (\ref CutJumpUnits, \ref CutJumpDegrees).
 */
class SceneMeter {

public:

	void reset() { *this = SceneMeter(); }

	void add(const SceneMeterFrame & frame);

	//! A \c playerlookat: seen, and when it was applied behind the fade
	void lookAtSeen() { m_lookats++; }
	void lookAtFaded() { m_fadedLookats++; }

	//! Cinematic bars were drawn into an eye (must never happen in VR)
	void letterboxDrawn() { m_letterboxFrames++; }

	u64 frames() const { return m_frames; }
	u64 sceneFrames() const { return m_sceneFrames; }
	u64 screenFrames() const { return m_screenFrames; }
	//! -1 while no frame was measured
	float minAboveFloorM() const { return m_measured ? m_minAboveFloor : -1.f; }
	float maxStepCm() const { return m_maxStep; }
	float maxYawStepDeg() const { return m_maxYawStep; }
	u64 cuts() const { return m_cuts; }
	u64 fadedCuts() const { return m_fadedCuts; }
	u64 lookats() const { return m_lookats; }
	u64 fadedLookats() const { return m_fadedLookats; }
	u64 letterboxFrames() const { return m_letterboxFrames; }

private:

	u64 m_frames = 0;
	u64 m_sceneFrames = 0;
	u64 m_screenFrames = 0;
	u64 m_measured = 0;
	float m_minAboveFloor = 0.f;
	float m_maxStep = 0.f;
	float m_maxYawStep = 0.f;
	u64 m_cuts = 0;
	u64 m_fadedCuts = 0;
	u64 m_lookats = 0;
	u64 m_fadedLookats = 0;
	u64 m_letterboxFrames = 0;

	bool m_havePrevious = false;  //!< the last frame, for jumps
	Vec3f m_previousPos = Vec3f(0.f);
	float m_previousYaw = 0.f;
	bool m_haveMeasured = false;  //!< the last measured frame, for steps
	Vec3f m_measuredPos = Vec3f(0.f);
	float m_measuredYaw = 0.f;

};

/*!
 * The scene view of the running game: updated by the camera each VR frame (ArxGame.cpp), read by
 * status.json, the body (the IK arms), the turn and \c playerlookat
 */
struct SceneState {

	SceneDirector director;
	SceneMeter meter;

	//! The switches now: read from cfg.ini at the start, changed live by the test channel (not saved)
	SceneViewSetting view = DefaultSceneView;
	SceneLookAt lookAt = DefaultSceneLookAt;

	bool active = false;             //!< a scripted scene owns the game's camera (last VR frame)
	SceneKind kind = SceneKind::None; //!< what owns it
	bool armsIk = true;               //!< the IK arms are on: the hero is not shown in third person

	bool lookAtPending = false;       //!< a \c playerlookat waits for the fade's black
	Anglef lookAtAngle;               //!< the angle it turns the hero to

	//! A scene is on or still shown: walking is blocked, the stick turns the hero (#29)
	bool inScene() const { return active || director.shown() != SceneShown::None; }

};

SceneState & sceneState();

} // namespace vr

#endif // ARX_VR_SCENEVIEW_H
