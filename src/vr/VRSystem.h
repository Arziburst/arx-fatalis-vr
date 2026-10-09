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

#ifndef ARX_VR_VRSYSTEM_H
#define ARX_VR_VRSYSTEM_H

#include <array>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/EyeProjection.h"
#include "vr/FramePacing.h"
#include "vr/HeadPose.h"
#include "vr/VRControls.h"

/*!
 * \brief VR runtime integration (OpenXR)
 *
 * The game starts in VR automatically when an OpenXR runtime with a head-mounted display is
 * available and \c --no-vr was not given; otherwise it runs in flat mode exactly as before.
 * Every function here is safe to call in a build without OpenXR support - it then does nothing
 * and reports that VR is unavailable.
 *
 * Startup order (see \ref ArxGame::initialize):
 *  1. \ref initialize - create the OpenXR instance and find the headset (no OpenGL needed yet)
 *  2. the window and the OpenGL context are created
 *  3. \ref createSession - bind the OpenXR session to that OpenGL context, create the swapchains
 *  4. every frame: \ref pollEvents (begins and ends the session as the runtime asks),
 *     \ref beginFrame at the start of the frame, \ref submitFrame before the buffer swap;
 *     \ref shutdown at exit
 *
 * The frame loop (#20): while the session runs, \ref beginFrame waits for the headset's next
 * display slot (\c xrWaitFrame - the only clock that paces the game in VR, D-024), begins the
 * frame and locates the eye poses for its predicted display time; the game simulates and renders;
 * \ref submitFrame hands the frame to the compositor with exactly those poses (pose-matched
 * submission, research rows F-21 and F-116). A frame shown from outside the game loop (the
 * loading screen) begins its own frame inside \ref submitFrame.
 *
 * Stereo (#19): in a frame the runtime wants rendered, \ref beginFrame makes the flat frame quad's
 * swapchain image the render target, so everything 2D the game draws (menus, HUD, loading screens,
 * cinematics) lands on the head-locked quad, composited over the world by its alpha; the 3D scene
 * is drawn once per eye into the eye swapchains between \ref beginEyeRender and \ref endEyeRender,
 * each eye with its own view and off-axis projection (vr/EyeProjection.h, F-115) relative to the
 * head camera. \ref submitFrame then submits the eyes as the projection layer and the quad in
 * front. In frames the runtime does not want rendered (an idle headset) the game draws into the
 * window as in flat mode and the frame is ended without layers.
 *
 * Every OpenXR call goes through the runtime interface (vr/XRFunctions.h): the OpenXR loader, or the
 * simulated headset of \c --vr-sim (vr/VRSimulator.h, #112), which runs all of this without a
 * runtime or a headset.
 */
namespace vr {

//! \return true if the game was built with OpenXR support
bool isSupported();

/*!
 * \brief Create the OpenXR instance and find the head-mounted display
 *
 * Does nothing and returns false when VR was disabled with \c --no-vr, when no OpenXR runtime or
 * headset is available, or when the active runtime is SteamVR and SteamVR is not running (creating
 * an instance would start SteamVR behind the player's back). The reason is logged.
 *
 * \return true if the game will run in VR
 */
bool initialize();

/*!
 * \brief Create the OpenXR session bound to the current OpenGL context
 *
 * Must be called from the thread that owns the window's OpenGL context, after the renderer has
 * been initialized. On failure VR is shut down and the game continues in flat mode.
 *
 * \return true if the session was created
 */
bool createSession();

/*!
 * \brief Poll OpenXR events (session state changes, instance loss) - call once per frame
 *
 * Begins the session when the runtime reports it ready and ends it when the runtime stops it.
 */
void pollEvents();

/*!
 * \brief Start a game frame in VR: wait for the headset's next display slot and locate the eyes
 *
 * Blocks in \c xrWaitFrame until the runtime wants the next frame rendered - this is what paces
 * the game in VR, so the caller skips its own frame limiter when this returns true. With the
 * even-cadence cap (\c vr_frame_divisor, F-26) the call first sleeps past the slots to skip, so
 * the runtime hands out every n-th slot. Then \c xrBeginFrame and \c xrLocateViews for the frame's
 * predicted display time: the poses in \ref frameViews are the ones this frame must render with.
 *
 * Then, if the runtime wants this frame rendered and the eyes were located, the flat frame quad's
 * image becomes the render target (see \ref stereoActive) and the renderer is told its size.
 *
 * \param windowSize size of the window's back buffer: the size of the flat frame
 * \return true while an OpenXR session runs and the frame was begun; false in flat mode, while
 *         the session is not running, or when the runtime refused (logged)
 */
bool beginFrame(const Vec2i & windowSize);

/*!
 * \brief Show the finished frame in the headset
 *
 * Call after the frame was rendered and before the window's buffers are swapped. Ends the OpenXR
 * frame begun by \ref beginFrame (or begins one first, for frames shown from outside the game
 * loop). In a stereo frame (\ref stereoActive) the eye images the scene was drawn into are
 * submitted as the projection layer with the poses located for this frame, and the flat frame quad
 * in front of them, blended by its alpha (F-20); when the scene was not drawn (a menu, a loading
 * screen) both eyes are cleared to a uniform dark background instead - both eyes or none (F-116).
 * Frames the runtime does not want rendered are ended without layers. Afterwards the window is the
 * render target again, and the desktop mirror (#21, F-24) draws into it what one eye (the right
 * one by default, \c vr_mirror_eye) sees: its image cropped to the window, the flat frame where
 * the quad appears in that eye; with the mirror off the window shows the flat frame alone.
 * Does nothing while no OpenXR session is running.
 *
 * \param windowSize size of the window's back buffer
 */
void submitFrame(const Vec2i & windowSize);

/*!
 * \brief The interface's virtual screen: what \c g_size is laid out on (#73)
 *
 * The window's size in flat mode. While a session runs, from its first located eyes on, the
 * head-locked quad's own resolution: the headset's pixel density at the quad's angle with the
 * window's aspect ratio (vr/InterfaceSize.h) - everything 2D is then drawn into the quad's image
 * at that size, whatever the window's size, and the window shows it scaled.
 */
Vec2i interfaceSize(const Vec2i & windowSize);

/*!
 * \brief Begin a frame of the 2D interface drawn from outside the game loop (the loading screen)
 *
 * Call before drawing it: the frame is begun (\c xrWaitFrame paces it like a game frame) and the
 * quad's image becomes the render target, cleared to opaque black, so the picture is drawn at the
 * interface's size; \ref submitFrame then shows it. Inside the game loop's frame nothing changes.
 *
 * \return true while a session runs and the frame is begun
 */
bool beginInterfaceFrame(const Vec2i & windowSize);

/*!
 * \return true while the current frame is drawn in stereo: the session runs, the runtime wants the
 *         frame rendered and the eyes were located - the scene must then be drawn once per eye with
 *         \ref beginEyeRender / \ref endEyeRender, and everything 2D goes to the flat frame quad
 */
bool stereoActive();

/*!
 * \return this frame's eyes relative to the head camera (left, right): offsets in game units,
 *         rotations and fields of view - valid while \ref stereoActive
 */
const std::array<EyeRig, 2> & eyeRigs();

//! \return the frustum both eyes are culled with, in the head camera's axes (F-163) - valid while \ref stereoActive
const EyeFov & unionEyeFov();

//! \return the size of the eye swapchain images in pixels (0 without a session)
Vec2i eyeRenderSize();

/*!
 * \brief Make an eye's swapchain image the render target for the scene
 *
 * Acquires the image, binds its framebuffer and names its size to the renderer; the caller
 * prepares the eye's camera and draws the scene, then calls \ref endEyeRender.
 *
 * \return false when the frame is not a stereo frame or the image could not be acquired (logged):
 *         the eye is then left out of this frame
 */
bool beginEyeRender(size_t eye);

//! Hand the eye's image back and make the flat frame the render target again
void endEyeRender(size_t eye);

//! The stereo render as status.json reports it (#19)
struct StereoStatus {
	bool active = false;        //!< the current frame is drawn per eye
	u64 frames = 0;             //!< frames whose scene was drawn into both eyes since the game started
	Vec2i eyeSize = Vec2i(0);   //!< the eye images in pixels
	std::array<EyeFov, 2> fov;  //!< the eyes' fields of view of the last stereo frame
	EyeFov unionFov;            //!< the culling frustum of the last stereo frame
	float eyeSeparationM = 0.f; //!< the distance between the eyes in meters
	float cantDeg = 0.f;        //!< the angle between the eyes' view directions in degrees
	Vec2i interfaceSize = Vec2i(0); //!< the interface's own resolution on the quad (#73), 0 before it is known
	float eyePixelsPerDegree = 0.f; //!< the eyes' pixel density straight ahead, which sets it
};

StereoStatus stereoStatus();

//! One eye as located for the current frame, in the world-locked reference space (meters)
struct EyeView {
	Vec3f position = Vec3f(0.f);
	glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	float fovLeft = 0.f;  //!< field of view half-angles in radians, OpenXR convention
	float fovRight = 0.f; //!< (left and down are negative)
	float fovUp = 0.f;
	float fovDown = 0.f;
};

//! The eye poses the current frame renders with and is submitted with (F-21, F-116)
struct FrameViews {
	bool valid = false;         //!< the runtime located both eyes with a valid pose
	s64 displayTime = 0;        //!< the frame's predicted display time, OpenXR nanoseconds
	std::array<EyeView, 2> eyes; //!< left, right
	bool floorValid = false;    //!< the runtime has a STAGE space and located the floor this frame (#27)
	float localAboveFloor = 0.f; //!< the reference space's origin above the room's floor, meters
};

/*!
 * \return the eye poses located by \ref beginFrame for the current frame - the sample the camera
 *         (#24) and the per-eye render (#19) take; \c valid is false in flat mode and before the
 *         first frame
 */
const FrameViews & frameViews();

//! The controllers' grip poses located for the current frame, in the same space and for the same display time as the eyes (#36)
struct FrameHands {
	std::array<bool, HandCount> tracked = { { false, false } }; //!< the runtime located the hand with a valid position
	std::array<HandSample, HandCount> hands;                   //!< left, right - in the OpenXR LOCAL space
};

/*!
 * \return the hands located by \ref beginFrame for the current frame (F-21: the body is posed with
 *         the sample the frame is submitted with); nothing tracked in flat mode, before the first
 *         frame, or while a controller is off
 */
const FrameHands & frameHands();

//! \return true while the game runs in VR, i.e. an OpenXR session exists
bool isEnabled();

//! \return the current OpenXR session state as text ("none" without a session) - for status.json
const char * sessionStateString();

/*!
 * \return true while the headset is on someone's head: the session is \c visible or \c focused (#27).
 *         An idle headset on the desk keeps the session \c synchronized (TESTING.md); the dashboard
 *         over the game is \c visible, still worn
 */
bool isWorn();

//! OpenXR frame counters since the game started - for status.json
struct FrameCounters {
	u64 submitted = 0; //!< frames ended with \c xrEndFrame
	u64 shown = 0;     //!< of those, frames the runtime wanted rendered and that carried layers
	u64 resubmitted = 0; //!< of those, frames ended with the previous picture because no new one was shown
	u64 discarded = 0; //!< frames the runtime discarded (\c xrBeginFrame called twice)
	u64 gaps = 0;      //!< frame gaps detected (see \ref FrameGap)
};

FrameCounters frameCounters();

/*!
 * \return the last rate / cadence report of the frame loop (F-07): one per 3 s window while the
 *         session runs, kept after it ended; \c valid is false before the first window closed
 */
const PacingReport & lastPacingReport();

//! \return how many rate / cadence reports were produced since the game started
u32 pacingReports();

//! \return the expected display slots per frame: 1 for every slot, 2 for every second slot, ...
u32 frameDivisor();

//! The desktop mirror (#21): the window shows what one eye sees - for status.json
struct MirrorStatus {
	std::string state = "flat mode"; //!< "on", or why the window shows the flat frame ("off: ...")
	std::string eye = "right";       //!< the mirrored eye (vr_mirror_eye; right by default)
	u64 frames = 0;                  //!< frames mirrored into the window since the game started
};

MirrorStatus mirrorStatus();

/*!
 * \brief The engine's menu state, once per frame, for the floating UI panel (#49)
 *
 * While a menu is open (the main or pause menu, the inventory, the book, a container, a note -
 * the engine's own state, never a timer: F-82) the 2D interface's quad is the floating panel: placed
 * ahead of the player at chest height where they look when it opens, then fixed in the play space
 * (vr/UIPanel.h). With no menu open it is the head-locked quad (the HUD, until #116 moves it to the
 * wrist).
 *
 * \param menu the open menu's name for the log ("inventory", "book", ...), or nullptr for none
 */
void setMenuOpen(const char * menu);

//! The floating UI panel as status.json reports it (#49)
struct PanelStatus {
	std::string mode = "head";     //!< "world" while the panel is placed for an open menu, else "head"
	std::string menu;              //!< the open menu's name, empty for none
	u64 placements = 0;            //!< how often the panel was placed since the game started
	std::string lastPlacement = "none"; //!< why it was placed last: "open", "recenter", "tracking"
	Vec3f position = Vec3f(0.f);   //!< the panel's centre in the LOCAL space, meters
	float yawDeg = 0.f;            //!< the panel's heading, degrees (0 = -z, + turns left)
	float tiltDeg = 0.f;           //!< how far its face is turned up towards the eyes, degrees
	Vec2f size = Vec2f(0.f);       //!< meters
	float distance = 0.f;          //!< the settings: ahead of the head, meters
	float drop = 0.f;              //!< below the eyes, meters
};

PanelStatus panelStatus();

//! The floating panel is shown for an open menu in a running session (#49) - the menu pointer is on (#34)
bool panelShown();

/*!
 * \brief Where a ray hits the floating panel - the hook for the menu pointer (#34)
 *
 * \param origin    in the OpenXR LOCAL space (the space the eyes are located in), meters
 * \param direction in the same space
 * \param pixel     receives the hit in the 2D interface's pixels (x right, y down)
 * \param point     if not null, receives the hit in the LOCAL space
 * \return false when no panel is shown for an open menu or the ray misses it
 */
bool panelHit(const Vec3f & origin, const Vec3f & direction, Vec2f & pixel, Vec3f * point = nullptr);

/*!
 * \brief The point on the shown floating panel at a pixel of the 2D interface, in the LOCAL space -
 *        the inverse of \ref panelHit, for aiming a simulated hand at a widget (the test channel)
 * \return false when no panel is shown for an open menu
 */
bool panelPoint(const Vec2f & pixel, Vec3f & point);

//! A hand's pointer ray (its aim pose, #34) in the LOCAL space
struct HandAim {
	bool valid = false;           //!< the hand is tracked and the session focused
	Vec3f origin = Vec3f(0.f);    //!< meters
	Vec3f direction = Vec3f(0.f); //!< unit
};

//! Both hands' pointer rays for this frame's display time (invalid outside a session)
std::array<HandAim, HandCount> handAims();

/*!
 * \brief The menu pointer's beam for this frame (#34): from the hand to its hit on the panel
 *
 * Drawn as a thin quad layer above the panel, turned towards the eyes and fading towards the panel;
 * only while the panel is shown. Set every frame - \c shown false hides it.
 */
void setPointerBeam(bool shown, const Vec3f & from = Vec3f(0.f), const Vec3f & to = Vec3f(0.f));

//! The beam was part of the last submitted frame
bool pointerBeamShown();

/*!
 * \brief A hand's reticle for this frame (#37): a small ring where its hidden ray hits the world
 *
 * Drawn as a quad layer at \a hit (LOCAL space, meters), facing the eyes, the same angular size at any
 * distance (vr/HandReticle.h); \a bright over an item that can be taken (D-029). Only in stereo frames,
 * where the world is drawn per eye. Set every frame - \c shown false hides it.
 */
void setHandReticle(Hand hand, bool shown, const Vec3f & hit = Vec3f(0.f), bool bright = false);

//! The hand's reticle was part of the last submitted frame
bool handReticleShown(Hand hand);

//! Place the panel anew at the head on the next frame while a menu is open (a recenter, F-180)
void recenterPanel();

/*!
 * \return the OpenXR runtime that answered this run, as "<name> <version>" - empty while no
 *         instance was created (for status.json; stays set after \ref shutdown)
 */
std::string runtimeName();

/*!
 * \return the active OpenXR runtime manifest the loader uses, found the way the loader finds it
 *         (\c XR_RUNTIME_JSON, then the registry on Windows) - empty when no runtime is installed
 */
std::string runtimeManifest();

/*!
 * \brief Sync the OpenXR actions and read every physical control (#30)
 *
 * For \ref updateInput, which turns the controls into gameplay events. Returns false with an
 * empty \a input while no session runs or the session is not focused.
 */
bool readControllers(ControllerInput & input);

//! The OpenXR interaction profile path per hand, empty while there is none - for status.json
std::array<std::string, HandCount> controllerProfiles();

//! How often the controllers were read while the session was focused
u64 controllerSyncs();

/*!
 * \brief End the session in order and destroy the OpenXR session and instance (if any)
 *
 * A running session is asked to exit (\c xrRequestExitSession) and given a moment to reach
 * \c STOPPING (then \c xrEndSession) and \c EXITING, so the runtime sees an orderly exit instead
 * of a session destroyed while running.
 */
void shutdown();

} // namespace vr

#endif // ARX_VR_VRSYSTEM_H
