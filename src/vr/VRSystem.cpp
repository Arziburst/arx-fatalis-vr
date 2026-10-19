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

#include "vr/VRSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include "platform/Platform.h"
#include "Configure.h"

#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/Version.h"
#include "graphics/Renderer.h"
#include "graphics/opengl/GLTexture.h"
#include "gui/Credits.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/log/Logger.h"
#include "platform/CrashHandler.h"
#include "platform/DumpHelper.h"
#include "platform/ExitTrace.h"
#include "platform/ProgramOptions.h"
#include "platform/Thread.h"
#include "platform/Time.h"
#include "platform/profiler/Profiler.h"
#include "util/String.h"

#if ARX_HAVE_OPENXR

// OpenXR with the OpenGL binding (and windows.h on Windows)
#include "vr/HandTracking.h"
#include "vr/HandReticle.h"
#include "vr/HeadTracking.h"
#include "vr/InterfaceSize.h"
#include "vr/MenuPointer.h"
#include "vr/OpenXRUtil.h"
#include "vr/PerfLog.h"
#include "vr/UIPanel.h"
#include "vr/VRActions.h"
#include "vr/VRMirror.h"
#include "vr/VRSimulator.h"
#include "vr/VRSwapchain.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <tlhelp32.h>
#include "platform/WindowsUtils.h"
#endif

#endif // ARX_HAVE_OPENXR


namespace vr {

static bool g_disableVR = false;

static void disableVR() {
	g_disableVR = true;
}
ARX_PROGRAM_OPTION("no-vr", nullptr, "Force flat (non-VR) mode even if a VR headset is available",
                   &disableVR)

//! The even-cadence cap from the command line (0: cfg.ini decides), see beginFrame()
static u32 g_frameDivisorOverride = 0;

static void setFrameDivisor(u32 divisor) {
	g_frameDivisorOverride = std::clamp(divisor, u32(1), u32(8));
}
ARX_PROGRAM_OPTION_ARG("vr-frame-divisor", nullptr,
                       "Submit every n-th display slot in VR (even-cadence cap, overrides vr_frame_divisor in cfg.ini)",
                       &setFrameDivisor, "N")

//! The desktop mirror switched off on the command line, see DesktopMirror
static bool g_disableMirror = false;

static void disableMirror() {
	g_disableMirror = true;
}
ARX_PROGRAM_OPTION("no-vr-mirror", nullptr,
                   "In VR, keep the flat frame in the desktop window instead of what one eye sees (overrides vr_mirror in cfg.ini)",
                   &disableMirror)

//! The eyes' multisampling from the command line (-1: cfg.ini decides), see createSessionObjects()
static int g_msaaOverride = -1;

static void setMsaa(u32 samples) {
	g_msaaOverride = int(std::min(samples, u32(MaxEyeMsaa)));
}
ARX_PROGRAM_OPTION_ARG("vr-msaa", nullptr,
                       "Multisampling of the VR eye images: samples per pixel, 0 = off (overrides vr_msaa in cfg.ini)",
                       &setMsaa, "N")

#if ARX_HAVE_OPENXR

bool isSupported() {
	return true;
}

namespace {

//! The head-locked quad that shows the flat frame (F-20): its width and distance in meters
const float QuadWidth = 2.4f;
const float QuadDistance = 1.75f;

//! Frame loop failures logged per session before it goes quiet (they repeat every frame)
const u32 MaxLoggedFrameFailures = 5;

//! Frame gaps logged per run before they are only counted (level loads produce them by the dozen)
const u64 MaxLoggedFrameGaps = 10;

//! The rate / cadence report window (F-07)
const s64 PacingWindowUs = 3000000;

//! How long an orderly exit may take: xrRequestExitSession to EXITING
const std::chrono::milliseconds ExitTimeout(2000);

//! A radial menu's image (#376), made with the session
const Vec2i RadialImageSize(RadialImagePixels, RadialImagePixels);

struct OpenXRState {

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

	//! Between xrBeginSession and xrEndSession: every shown game frame must be submitted
	bool running = false;

	PFN_xrGetOpenGLGraphicsRequirementsKHR getOpenGLGraphicsRequirements = nullptr;

	//! Per eye (left, right), from xrEnumerateViewConfigurationViews
	std::array<XrViewConfigurationView, 2> views = { };
	Vec2i maxSwapchainSize = Vec2i(0);

	XrSpace viewSpace = XR_NULL_HANDLE;  //!< head-locked: the flat frame quad lives here
	XrSpace localSpace = XR_NULL_HANDLE; //!< world-locked: the eyes are located and submitted here
	XrSpace stageSpace = XR_NULL_HANDLE; //!< the room, floor at y = 0: how high LOCAL is (#27); null without one
	bool loggedStage = false;            //!< the first located floor was logged
	s64 colorFormat = 0;
	std::array<Swapchain, 2> eyes;
	Swapchain quad;
	Vec2i quadFailedSize = Vec2i(0); //!< a size the quad swapchain could not be created for
	Swapchain beam;                  //!< the menu pointer's beam image (#34), made the first time it is shown
	bool beamFailed = false;         //!< the beam swapchain could not be made: not retried this session
	std::array<Swapchain, 2> reticleImages; //!< the hand reticle's ring, dim and bright (#37), made when first shown
	bool reticleFailed = false;      //!< the reticle swapchains could not be made: not retried this session

	/*!
	 * The interface's own resolution (#73): from the session's first located eyes on, the 2D
	 * interface is laid out on the quad at the headset's pixel density instead of the window's size
	 */
	struct Interface {
		bool known = false; //!< the eyes were located once this session: \c fov holds their field of view
		EyeFov fov;         //!< the left eye's, which sets the density
		Vec2i logged = Vec2i(0); //!< the size last named in the log
	} ui;
	DesktopMirror mirror;            //!< the window shows what the left eye sees (#21)

	FrameCounters frames;
	u32 frameFailures = 0;
	bool loggedInvalidViews = false;
	//! The display time of the last frame - for locating the controllers outside the frame (0: none yet)
	XrTime lastDisplayTime = 0;

	//! One action per physical control (#30); created with the instance, attached to the session
	ActionSet actions;

	//! XR_EXT_hand_tracking (#305): bound with the instance, a tracker per hand on the session
	HandTracking handTracking;

	//! The current OpenXR frame, from beginFrame (xrWaitFrame) to its xrEndFrame
	struct Frame {
		bool begun = false;
		XrFrameState state = { XR_TYPE_FRAME_STATE };
		FrameTiming timing;
		s64 period = 0;                     //!< the predicted display period in microseconds
		bool haveLastWait = false;
		PlatformInstant lastWaitReturn = PlatformInstant(0); //!< when xrWaitFrame last returned: the cap sleeps relative to it
		std::array<XrView, 2> located = { };
		FrameViews views;                   //!< the located eyes, for the renderer and for the layer
		FrameHands hands;                   //!< the located controllers, for the body (#36)

		//! Stereo (#19): the scene is drawn per eye into the eye swapchains, everything 2D into the quad
		bool stereo = false;
		std::array<EyeRig, 2> rig;          //!< the eyes relative to the head camera
		EyeFov unionFov;                    //!< the frustum both eyes are culled with (F-163)
		std::array<bool, 2> eyeRendered = { { false, false } }; //!< the eye image was drawn and released
		bool quadAcquired = false;          //!< the flat frame image is acquired and bound as the 2D target
		size_t eyeInRender = size_t(-1);    //!< the eye whose image is bound right now, or none
		Vec2i windowSize = Vec2i(0);        //!< the window, i.e. the flat frame, this frame
	} frame;

	//! The floating UI panel (#49): the engine's menu state and where the 2D interface's quad is shown
	struct Panel {
		std::string menu;      //!< the open menu (setMenuOpen), empty for none
		PanelAnchor anchor;
		PanelParams params;
		PanelPlacement lastPlacement = PanelPlacement::None;
		PanelMode loggedMode = PanelMode::HeadLocked;
		Vec2f size = Vec2f(0.f); //!< the panel as last submitted, meters
		//! The screen of the logos and the cinematics (#394): the same anchor rules, its own place and size
		PanelAnchor screen;
		PanelMode loggedScreenMode = PanelMode::HeadLocked;
		//! The hybrid's screen (#29): wanted by the scene view, and the frame being drawn for it
		bool sceneScreenWanted = false;
		bool sceneScreen = false;
	} panel;

	//! The menu pointer's beam (#34), set by the input module every frame
	struct PointerBeam {
		bool requested = false;
		Vec3f from = Vec3f(0.f); //!< the hand, LOCAL space
		Vec3f to = Vec3f(0.f);   //!< its hit on the panel
		bool submitted = false;  //!< part of the last frame's layers
	} pointer;

	//! The hand reticles (#37), set by the input module every frame
	struct HandReticle {
		bool requested = false;
		Vec3f hit = Vec3f(0.f); //!< LOCAL space
		bool bright = false;
		bool submitted = false; //!< part of the last frame's layers
	};
	std::array<HandReticle, HandCount> reticles;

	//! The radial menus (#376): an image per hand drawn by the game while its menu is open, set every frame
	struct RadialMenu {
		Swapchain image;
		bool acquired = false;  //!< bound as the render target between beginRadialImage and endRadialImage
		bool drawn = false;     //!< an image was released this frame
		bool requested = false;
		Vec3f center = Vec3f(0.f); //!< LOCAL space
		glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
		float size = 0.f;
		bool submitted = false; //!< part of the last frame's layers
		const char * refusal = ""; //!< why the last beginRadialImage drew nothing, empty when it did (#377)
	};
	std::array<RadialMenu, HandCount> radials;
	bool radialFailed = false; //!< an image could not be made: not retried this session

	//! A scene line's subtitle panel (#29): its image, the engine's own texture drawn by the game while a subtitle shows
	//! and sampled into each eye as a quad fixed in the world (#519; a compositor quad layer stayed invisible on SteamVR)
	struct SubtitlePanel {
		Texture * texture = nullptr;
		GLuint framebuffer = 0;
		bool acquired = false;
	};
	SubtitlePanel subtitle;

	//! The stereo render as status.json reports it; \c active is the current frame's, the rest the last stereo frame's
	StereoStatus stereo;
	bool stereoLogged = false; //!< the stereo banner was logged for this session

	//! The layers of the last frame that was shown, kept for frames ended without a new picture
	struct Layers {
		std::array<XrCompositionLayerProjectionView, 2> projectionViews = { };
		XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
		XrCompositionLayerQuad beam = { XR_TYPE_COMPOSITION_LAYER_QUAD }; //!< the menu pointer, above the panel (#34)
		std::array<XrCompositionLayerQuad, HandCount> reticles = { }; //!< the hand reticles (#37)
		std::array<XrCompositionLayerQuad, HandCount> radials = { };  //!< the radial menus, on top (#376)
		std::array<const XrCompositionLayerBaseHeader *, 3 + 2 * HandCount> list = { };
		u32 count = 0;
	} layers;

	//! The rate / cadence instrument (F-07) and the even-cadence cap (F-26)
	PacingWindow pacing { PacingWindowUs };
	PacingReport lastReport;
	u32 divisor = 1;        //!< expected display slots per frame
	u32 gapsInWindow = 0;   //!< frame gaps since the last report

	//! Orderly exit: xrRequestExitSession was sent, and the runtime answered with EXITING
	bool exitRequested = false;
	bool exited = false;

	//! The active runtime manifest the loader uses (empty: none installed) - kept for status.json
	std::string manifest;
	//! "<name> <version>" of the runtime that answered xrCreateInstance - kept after shutdown
	std::string runtime;

};

OpenXRState g_xr;

} // anonymous namespace

std::string resultName(XrResult result) {
	if(g_xr.instance != XR_NULL_HANDLE) {
		char buffer[XR_MAX_RESULT_STRING_SIZE];
		if(XR_SUCCEEDED(xrResultToString(g_xr.instance, result, buffer))) {
			return buffer;
		}
	}
	return "XrResult(" + std::to_string(int(result)) + ")";
}

namespace {

std::string versionString(XrVersion version) {
	std::ostringstream oss;
	oss << XR_VERSION_MAJOR(version) << '.' << XR_VERSION_MINOR(version) << '.' << XR_VERSION_PATCH(version);
	return oss.str();
}

const char * sessionStateName(XrSessionState state) {
	switch(state) {
		case XR_SESSION_STATE_UNKNOWN:      return "unknown";
		case XR_SESSION_STATE_IDLE:         return "idle";
		case XR_SESSION_STATE_READY:        return "ready";
		case XR_SESSION_STATE_SYNCHRONIZED: return "synchronized";
		case XR_SESSION_STATE_VISIBLE:      return "visible";
		case XR_SESSION_STATE_FOCUSED:      return "focused";
		case XR_SESSION_STATE_STOPPING:     return "stopping";
		case XR_SESSION_STATE_LOSS_PENDING: return "loss pending";
		case XR_SESSION_STATE_EXITING:      return "exiting";
		default:                            return "(invalid)";
	}
}

//! What a session state means for the game - logged with every change (research row F-118)
const char * sessionStateMeaning(XrSessionState state) {
	switch(state) {
		case XR_SESSION_STATE_IDLE:         return "no frames wanted yet";
		case XR_SESSION_STATE_READY:        return "the runtime wants the session to begin";
		case XR_SESSION_STATE_SYNCHRONIZED: return "frames are timed but not displayed - the runtime may pace the game as slowly as it likes";
		case XR_SESSION_STATE_VISIBLE:      return "frames are displayed, input goes elsewhere (dashboard or overlay)";
		case XR_SESSION_STATE_FOCUSED:      return "displayed and receiving input";
		case XR_SESSION_STATE_STOPPING:     return "the runtime asks the game to end the session";
		case XR_SESSION_STATE_LOSS_PENDING: return "the session is about to be lost (runtime or headset gone)";
		case XR_SESSION_STATE_EXITING:      return "the runtime asks the game to leave VR";
		default:                            return "unknown";
	}
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

/*!
 * Who has the desktop focus when the session leaves FOCUSED (F-118): another process means the
 * game window lost the desktop focus and the runtime followed; our own process means the runtime
 * took the focus for its own overlay (dashboard, system menu).
 */
void logFocusLost() {
	HWND window = GetForegroundWindow();
	DWORD pid = 0;
	WCHAR className[256] = L"";
	WCHAR title[256] = L"";
	if(window) {
		GetWindowThreadProcessId(window, &pid);
		GetClassNameW(window, className, DWORD(std::size(className)));
		GetWindowTextW(window, title, int(std::size(title)));
	}
	bool own = window && pid == GetCurrentProcessId();
	gameevent::Event("vr.focus")("state", "lost")("foregroundPid", u64(pid))("own", own)
		("class", platform::WideString::toUTF8(className))("title", platform::WideString::toUTF8(title))
		("hint", !window ? "no foreground window" : own ? "runtime overlay took the input"
		                                                : "another window has the desktop focus");
}

#else

void logFocusLost() {
	gameevent::Event("vr.focus")("state", "lost");
}

#endif

//! The interaction profile the runtime uses for \a userPath now (/user/hand/left), or why not
std::string currentInteractionProfile(const char * userPath) {

	XrPath path = XR_NULL_PATH;
	XrResult result = xrStringToPath(g_xr.instance, userPath, &path);
	if(XR_FAILED(result)) {
		return "error:" + resultName(result);
	}

	XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };
	result = xrGetCurrentInteractionProfile(g_xr.session, path, &state);
	if(XR_FAILED(result)) {
		return "error:" + resultName(result);
	}
	if(state.interactionProfile == XR_NULL_PATH) {
		return "none";
	}

	char buffer[XR_MAX_PATH_LENGTH];
	uint32_t count = 0;
	result = xrPathToString(g_xr.instance, state.interactionProfile, uint32_t(sizeof(buffer)), &count, buffer);
	if(XR_FAILED(result)) {
		return "error:" + resultName(result);
	}
	return buffer;
}

const char * referenceSpaceName(XrReferenceSpaceType type) {
	switch(type) {
		case XR_REFERENCE_SPACE_TYPE_VIEW:  return "view";
		case XR_REFERENCE_SPACE_TYPE_LOCAL: return "local";
		case XR_REFERENCE_SPACE_TYPE_STAGE: return "stage";
		default:                            return "other";
	}
}

/*!
 * Where the OpenXR loader will look for the runtime, found the way the loader finds it:
 * the XR_RUNTIME_JSON environment variable first, then (on Windows) the registry.
 */
struct RuntimeManifest {
	std::string path;     //!< the manifest file, empty when no runtime is installed
	std::string source;   //!< where \ref path was selected from, for the log
	std::string registry; //!< the registry's own ActiveRuntime when XR_RUNTIME_JSON overrides it
};

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

//! The registry's ActiveRuntime (HKLM first, then HKCU, like the loader) and the key it came from
std::string registryActiveRuntime(std::string & key) {

	const struct { HKEY root; const char * name; } roots[] = {
		{ HKEY_LOCAL_MACHINE, "HKLM" },
		{ HKEY_CURRENT_USER, "HKCU" },
	};
	for(const auto & root : roots) {
		WCHAR buffer[2 * MAX_PATH];
		DWORD size = sizeof(buffer);
		LSTATUS status = RegGetValueW(root.root, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime",
		                              RRF_RT_REG_SZ, nullptr, buffer, &size);
		if(status == ERROR_SUCCESS) {
			key = std::string(root.name) + "\\SOFTWARE\\Khronos\\OpenXR\\1\\ActiveRuntime";
			return platform::WideString::toUTF8(buffer);
		}
	}

	return std::string();
}

#endif

RuntimeManifest findRuntimeManifest() {

	RuntimeManifest manifest;

	std::string registry;
	std::string registryKey;
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	registry = registryActiveRuntime(registryKey);
	#endif

	const char * env = std::getenv("XR_RUNTIME_JSON");
	if(env && *env) {
		manifest.path = env;
		manifest.source = "XR_RUNTIME_JSON";
		if(!registry.empty() && !boost::iequals(registry, manifest.path)) {
			manifest.registry = registry;
		}
	} else if(!registry.empty()) {
		manifest.path = registry;
		manifest.source = registryKey;
	}

	return manifest;
}

/*!
 * One line per run naming the runtime manifest and where it was selected from - flat runs
 * included, so every log answers "which runtime, and why" (#115, research row F-98). A leftover
 * XR_RUNTIME_JSON is the classic reason for "it started flat": it is named, with what it overrides.
 */
void logRuntimeSelection(const RuntimeManifest & manifest) {

	if(manifest.path.empty()) {
		#if ARX_PLATFORM == ARX_PLATFORM_WIN32
		LogInfo << "No OpenXR runtime manifest: XR_RUNTIME_JSON is not set and the registry has no"
		           " SOFTWARE\\Khronos\\OpenXR\\1\\ActiveRuntime (HKLM or HKCU)";
		#else
		LogInfo << "OpenXR runtime manifest: XR_RUNTIME_JSON is not set, the loader searches its default locations";
		#endif
		return;
	}

	std::ostringstream oss;
	oss << "OpenXR runtime manifest: \"" << manifest.path << "\" (from " << manifest.source;
	if(!manifest.registry.empty()) {
		oss << ", overriding the registry's \"" << manifest.registry << '"';
	}
	oss << ')';
	LogInfo << oss.str();

	if(!fs::is_regular_file(fs::path(manifest.path))) {
		LogWarning << "OpenXR runtime manifest \"" << manifest.path << "\" does not exist - the loader will find"
		              " no runtime" << (manifest.source == "XR_RUNTIME_JSON"
		                                ? " (unset XR_RUNTIME_JSON to use the registry's runtime)" : "");
	}

}

/*!
 * What the name of the runtime that answers should contain, judged by its manifest path - only
 * for the runtimes we know; nullptr when the manifest does not tell.
 */
const char * expectedRuntimeToken(const std::string & manifest) {

	static const struct { const char * pathToken; const char * nameToken; } known[] = {
		{ "steamvr", "SteamVR" },
		{ "steamxr", "SteamVR" },
		{ "oculus", "Oculus" },
		{ "mixedreality", "Mixed Reality" },
		{ "windowsmr", "Mixed Reality" },
		{ "varjo", "Varjo" },
		{ "monado", "Monado" },
		{ "virtualdesktop", "VirtualDesktop" },
	};
	for(const auto & entry : known) {
		if(boost::icontains(manifest, entry.pathToken)) {
			return entry.nameToken;
		}
	}

	return nullptr;
}

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

bool isProcessRunning(const WCHAR * executable) {

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	PROCESSENTRY32W entry;
	entry.dwSize = sizeof(entry);
	if(Process32FirstW(snapshot, &entry)) {
		do {
			if(_wcsicmp(entry.szExeFile, executable) == 0) {
				found = true;
				break;
			}
		} while(Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);

	return found;
}

/*!
 * SteamVR's OpenXR runtime launches SteamVR itself as soon as an instance is created. We only want
 * to run in VR when the player has already started SteamVR (see D-004 in docs/vr/DECISIONS.md),
 * so OpenXR is skipped entirely when SteamVR is the active runtime but is not running.
 *
 * \return false if initializing OpenXR would start a VR runtime that is not running
 */
bool runtimeIsReady() {

	if(g_xr.manifest.empty()) {
		LogInfo << "No OpenXR runtime is installed, starting in flat mode";
		return false;
	}

	if(boost::icontains(g_xr.manifest, "steamvr") && !isProcessRunning(L"vrserver.exe")) {
		LogInfo << "SteamVR is the OpenXR runtime but is not running - start SteamVR first to play in VR."
		           " Starting in flat mode";
		return false;
	}

	return true;
}

#else

bool runtimeIsReady() {
	return true;
}

#endif

//! Whether the runtime lists an extension; \a failure gets the result of a failed enumeration (nothing is logged)
bool extensionListed(const char * name, XrResult & failure) {

	failure = XR_SUCCESS;

	uint32_t count = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
	if(XR_FAILED(result)) {
		failure = result;
		return false;
	}

	std::vector<XrExtensionProperties> extensions(count, { XR_TYPE_EXTENSION_PROPERTIES });
	result = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
	if(XR_FAILED(result)) {
		failure = result;
		return false;
	}

	for(size_t i = 0; i < count; i++) {
		if(std::strcmp(extensions[i].extensionName, name) == 0) {
			return true;
		}
	}

	return false;
}

//! An extension VR cannot start without
bool hasExtension(const char * name) {

	XrResult failure = XR_SUCCESS;
	if(extensionListed(name, failure)) {
		return true;
	}

	if(XR_FAILED(failure)) {
		LogWarning << "OpenXR runtime unavailable or its extensions could not be enumerated (" << resultName(failure)
		           << "), starting in flat mode";
	} else {
		LogWarning << "OpenXR runtime does not support " << name << ", starting in flat mode";
	}
	return false;
}

bool createInstance() {

	// The OpenGL binding is required; hand tracking (#305) is taken when the runtime lists it and
	// the fingers follow the controls otherwise
	std::vector<const char *> extensions = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrResult listFailure = XR_SUCCESS;
	const bool handTracking = extensionListed(HandTracking::extensionName(), listFailure);
	// The joints conforming to the controller (#385.7): asked for with the hand tracking when the runtime has it
	const bool motionRange = handTracking && extensionListed(HandTracking::motionRangeExtensionName(), listFailure);
	if(handTracking) {
		extensions.push_back(HandTracking::extensionName());
		if(motionRange) {
			extensions.push_back(HandTracking::motionRangeExtensionName());
		}
	} else {
		LogInfo << "[vr] hands: the runtime does not list " << HandTracking::extensionName()
		        << " - the fingers follow the controls (trigger, grip, thumb)";
	}

	XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	util::storeStringTerminated(createInfo.applicationInfo.applicationName, arx_name);
	createInfo.applicationInfo.applicationVersion = 1;
	util::storeStringTerminated(createInfo.applicationInfo.engineName, "Arx Libertatis");
	createInfo.applicationInfo.engineVersion = 1;
	// Everything we need is in OpenXR 1.0 - do not require a 1.1 runtime
	#ifdef XR_API_VERSION_1_0
	createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	#else
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	#endif
	createInfo.enabledExtensionCount = uint32_t(extensions.size());
	createInfo.enabledExtensionNames = extensions.data();

	XrResult result = xrCreateInstance(&createInfo, &g_xr.instance);
	if(XR_FAILED(result)) {
		g_xr.instance = XR_NULL_HANDLE;
		LogWarning << "Could not create OpenXR instance (" << resultName(result) << "), starting in flat mode";
		return false;
	}

	if(handTracking) {
		g_xr.handTracking.bind(g_xr.instance, motionRange);
	}

	XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };
	result = xrGetInstanceProperties(g_xr.instance, &properties);
	if(XR_FAILED(result)) {
		LogWarning << "Could not query OpenXR runtime properties: " << resultName(result);
		return true;
	}

	std::string runtime = std::string(util::loadString(properties.runtimeName)) + " "
	                      + versionString(properties.runtimeVersion);
	g_xr.runtime = runtime;
	LogInfo << "OpenXR runtime answered: " << runtime;
	CrashHandler::setVariable("OpenXR runtime", runtime);
	credits::setLibraryCredits("vr", "OpenXR (" + runtime + ")");

	// The runtime that answered should be the one the manifest names (research row F-98)
	if(const char * expected = expectedRuntimeToken(g_xr.manifest)) {
		if(!boost::icontains(runtime, expected)) {
			LogWarning << "OpenXR runtime that answered (" << runtime << ") is not the one the manifest"
			              " names (" << expected << ", \"" << g_xr.manifest << "\") - check XR_RUNTIME_JSON"
			              " and the registry";
		}
	}

	result = xrGetInstanceProcAddr(g_xr.instance, "xrGetOpenGLGraphicsRequirementsKHR",
	                               reinterpret_cast<PFN_xrVoidFunction *>(&g_xr.getOpenGLGraphicsRequirements));
	if(XR_FAILED(result) || !g_xr.getOpenGLGraphicsRequirements) {
		LogWarning << "OpenXR runtime does not provide xrGetOpenGLGraphicsRequirementsKHR: " << resultName(result);
		return false;
	}

	return true;
}

bool findSystem() {

	XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrResult result = xrGetSystem(g_xr.instance, &systemInfo, &g_xr.systemId);
	if(result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
		LogInfo << "No VR headset connected, starting in flat mode";
		return false;
	} else if(XR_FAILED(result)) {
		LogWarning << "Could not find a VR headset (" << resultName(result) << "), starting in flat mode";
		return false;
	}

	XrSystemProperties properties = { XR_TYPE_SYSTEM_PROPERTIES };
	// Whether this headset gives hand joints (#305): asked along with the rest when the extension is on
	XrSystemHandTrackingPropertiesEXT handProperties = { XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
	if(g_xr.handTracking.status().extension) {
		properties.next = &handProperties;
	}
	result = xrGetSystemProperties(g_xr.instance, g_xr.systemId, &properties);
	if(XR_SUCCEEDED(result)) {
		g_xr.handTracking.setSupported(handProperties.supportsHandTracking == XR_TRUE);
		LogInfo << "VR headset: " << util::loadString(properties.systemName)
		        << " (vendor " << properties.vendorId << ")"
		        << (properties.trackingProperties.positionTracking ? ", positional tracking" : ", orientation tracking only")
		        << ", max swapchain " << properties.graphicsProperties.maxSwapchainImageWidth
		        << 'x' << properties.graphicsProperties.maxSwapchainImageHeight
		        << ", " << properties.graphicsProperties.maxLayerCount << " layers"
		        << (g_xr.handTracking.status().extension
		            ? (handProperties.supportsHandTracking ? ", hand tracking" : ", no hand tracking") : "");
		CrashHandler::setVariable("VR headset", util::loadString(properties.systemName));
		g_xr.maxSwapchainSize = Vec2i(s32(properties.graphicsProperties.maxSwapchainImageWidth),
		                              s32(properties.graphicsProperties.maxSwapchainImageHeight));
	} else {
		LogWarning << "Could not query VR headset properties: " << resultName(result);
	}

	// Stereo rendering (task #19) requires the primary stereo view configuration
	uint32_t count = 0;
	result = xrEnumerateViewConfigurations(g_xr.instance, g_xr.systemId, 0, &count, nullptr);
	std::vector<XrViewConfigurationType> configurations(count);
	if(XR_SUCCEEDED(result)) {
		result = xrEnumerateViewConfigurations(g_xr.instance, g_xr.systemId, count, &count, configurations.data());
	}
	if(XR_FAILED(result)) {
		LogWarning << "Could not enumerate OpenXR view configurations (" << resultName(result) << "), starting in flat mode";
		return false;
	}
	bool stereo = false;
	for(size_t i = 0; i < count; i++) {
		if(configurations[i] == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
			stereo = true;
		}
	}
	if(!stereo) {
		LogWarning << "VR headset does not support stereo rendering, starting in flat mode";
		return false;
	}

	// The eye swapchains are sized from these
	g_xr.views.fill({ XR_TYPE_VIEW_CONFIGURATION_VIEW });
	count = 0;
	result = xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.systemId,
	                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
	if(XR_SUCCEEDED(result) && count != g_xr.views.size()) {
		LogWarning << "VR headset reports " << count << " stereo views instead of " << g_xr.views.size()
		           << ", starting in flat mode";
		return false;
	}
	if(XR_SUCCEEDED(result)) {
		result = xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		                                           count, &count, g_xr.views.data());
	}
	if(XR_FAILED(result)) {
		LogWarning << "Could not query OpenXR view sizes (" << resultName(result) << "), starting in flat mode";
		return false;
	}
	for(size_t i = 0; i < g_xr.views.size(); i++) {
		const XrViewConfigurationView & view = g_xr.views[i];
		LogInfo << "VR eye " << i << ": recommended " << view.recommendedImageRectWidth
		        << 'x' << view.recommendedImageRectHeight
		        << " (max " << view.maxImageRectWidth << 'x' << view.maxImageRectHeight << ")"
		        << ", " << view.recommendedSwapchainSampleCount << "x MSAA recommended";
	}

	return true;
}

bool endFrame(bool resubmit);

/*!
 * The last rate / cadence report of a session that ends before its first window closed: a short
 * run still leaves numbers in status.json. A session with full reports keeps its last full window
 * - a partial one would skew the verdict.
 */
void flushPacing() {
	if(g_xr.pacing.reports() == 0 && g_xr.pacing.flush()) {
		g_xr.lastReport = g_xr.pacing.report();
		LogInfo << "[vr] rate: " << formatReport(g_xr.lastReport) << " (partial window at session end)";
	}
}

/*!
 * The window is the render target again: after a frame was submitted, when the session goes away,
 * and whenever a target of ours is abandoned.
 */
//! The subtitle panel's texture and framebuffer (#29), with the session
static void destroySubtitleImage() {
	OpenXRState::SubtitlePanel & panel = g_xr.subtitle;
	if(panel.framebuffer) {
		glDeleteFramebuffers(1, &panel.framebuffer);
	}
	delete panel.texture;
	panel = OpenXRState::SubtitlePanel();
}

void restoreWindowTarget() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if(GRenderer) {
		GRenderer->setRenderTargetSize(Vec2i(0));
	}
}

//! Make a framebuffer object of \a size with \a samples (0 = single-sample) the render target for the engine
void bindTarget(GLuint framebuffer, const Vec2i & size, int samples = 0) {
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	if(GRenderer) {
		GRenderer->setRenderTargetSize(size, samples);
	}
}

/*!
 * Give up the images of the current frame without submitting them: an eye left bound and the flat
 * frame quad are released (the compositor then shows what they hold), and the window is the target
 * again. For frames that end without a picture of their own (a level load, a session stop).
 */
void abandonFrameTargets() {
	OpenXRState::Frame & frame = g_xr.frame;
	if(frame.eyeInRender != size_t(-1)) {
		g_xr.eyes[frame.eyeInRender].release();
		frame.eyeInRender = size_t(-1);
	}
	if(frame.quadAcquired) {
		g_xr.quad.release();
		frame.quadAcquired = false;
	}
	frame.stereo = false;
	frame.eyeRendered = { { false, false } };
	g_xr.stereo.active = false;
	restoreWindowTarget();
}

void destroySession() {

	abandonFrameTargets();

	// The swapchains' OpenGL objects go first, while the context the session is bound to is current
	for(Swapchain & eye : g_xr.eyes) {
		eye.destroy();
	}
	g_xr.quad.destroy();
	g_xr.quadFailedSize = Vec2i(0);
	g_xr.beam.destroy();
	g_xr.beamFailed = false;
	g_xr.pointer = OpenXRState::PointerBeam();
	for(Swapchain & image : g_xr.reticleImages) {
		image.destroy();
	}
	g_xr.reticleFailed = false;
	g_xr.reticles = { };
	for(OpenXRState::RadialMenu & radial : g_xr.radials) {
		radial.image.destroy();
		radial = OpenXRState::RadialMenu();
	}
	g_xr.radialFailed = false;
	destroySubtitleImage();
	g_xr.mirror.destroy();
	g_xr.colorFormat = 0;

	g_xr.handTracking.destroy();
	g_xr.actions.detach();
	g_xr.lastDisplayTime = 0;

	if(g_xr.localSpace != XR_NULL_HANDLE) {
		xrDestroySpace(g_xr.localSpace);
		g_xr.localSpace = XR_NULL_HANDLE;
	}

	if(g_xr.stageSpace != XR_NULL_HANDLE) {
		xrDestroySpace(g_xr.stageSpace);
		g_xr.stageSpace = XR_NULL_HANDLE;
	}
	g_xr.loggedStage = false;

	if(g_xr.viewSpace != XR_NULL_HANDLE) {
		xrDestroySpace(g_xr.viewSpace);
		g_xr.viewSpace = XR_NULL_HANDLE;
	}

	if(g_xr.session != XR_NULL_HANDLE) {
		flushPacing();
		xrDestroySession(g_xr.session);
		g_xr.session = XR_NULL_HANDLE;
		LogInfo << "OpenXR session destroyed" << (g_xr.running ? " while running" : "")
		        << " after " << g_xr.frames.submitted << " frames (" << g_xr.frames.shown << " shown, "
		        << g_xr.stereo.frames << " in stereo)";
	}
	g_xr.sessionState = XR_SESSION_STATE_UNKNOWN;
	g_xr.running = false;
	g_xr.frame = OpenXRState::Frame();
	g_xr.layers = OpenXRState::Layers();
	g_xr.exitRequested = false;
	g_xr.exited = false;
}

/*!
 * The spaces and the swapchains of the session: a head-locked VIEW space for the flat frame quad,
 * a world-locked LOCAL space the eyes are located and submitted in (STAGE, the room-scale space
 * with the floor at the origin, is only probed and logged - the camera takes the head from LOCAL,
 * #24; the standing height is #27); the per-eye swapchains are sized from the runtime's recommended view sizes and get a
 * framebuffer object per image to render into.
 */
bool createSessionResources() {

	XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	spaceInfo.poseInReferenceSpace.orientation.w = 1.f;
	XrResult result = xrCreateReferenceSpace(g_xr.session, &spaceInfo, &g_xr.viewSpace);
	if(XR_FAILED(result)) {
		g_xr.viewSpace = XR_NULL_HANDLE;
		LogError << "Could not create the OpenXR view space: " << resultName(result);
		return false;
	}

	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	result = xrCreateReferenceSpace(g_xr.session, &spaceInfo, &g_xr.localSpace);
	if(XR_FAILED(result)) {
		g_xr.localSpace = XR_NULL_HANDLE;
		LogError << "Could not create the OpenXR local space: " << resultName(result);
		return false;
	}

	{
		uint32_t count = 0;
		result = xrEnumerateReferenceSpaces(g_xr.session, 0, &count, nullptr);
		std::vector<XrReferenceSpaceType> spaces(count);
		if(XR_SUCCEEDED(result)) {
			result = xrEnumerateReferenceSpaces(g_xr.session, count, &count, spaces.data());
		}
		bool stage = XR_SUCCEEDED(result)
		             && std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end();
		if(stage) {
			// Only to measure the floor under LOCAL: the head's height above the floor is evidence for the eye
			// height (#27); the camera still takes the head from LOCAL
			spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
			result = xrCreateReferenceSpace(g_xr.session, &spaceInfo, &g_xr.stageSpace);
			if(XR_FAILED(result)) {
				g_xr.stageSpace = XR_NULL_HANDLE;
				LogWarning << "Could not create the OpenXR stage space: " << resultName(result)
				           << " - the head's height above the floor stays unknown";
			}
		}
		LogInfo << "OpenXR reference spaces: view (flat frame quad), local (eye poses)"
		        << (stage ? ", stage (the floor under the head)" : ", no stage space (seated runtime or no room setup)");
	}

	g_xr.colorFormat = chooseSwapchainFormat(g_xr.session);
	if(!g_xr.colorFormat) {
		return false;
	}

	// Multisampling of the eyes (#226): the command line first, then cfg.ini; 0 or 1 = off
	const int samples = g_msaaOverride >= 0 ? g_msaaOverride : std::clamp(config.video.vrMsaa, 0, MaxEyeMsaa);
	LogInfo << "[vr] eye MSAA: " << (samples > 1 ? samples : 0) << "x from "
	        << (g_msaaOverride >= 0 ? "--vr-msaa" : "vr_msaa in cfg.ini");

	const char * const names[] = { "left eye", "right eye" };
	for(size_t i = 0; i < g_xr.eyes.size(); i++) {
		Vec2i size(s32(g_xr.views[i].recommendedImageRectWidth), s32(g_xr.views[i].recommendedImageRectHeight));
		if(!g_xr.eyes[i].create(g_xr.session, g_xr.colorFormat, size, true, names[i], samples)) {
			return false;
		}
	}

	// The radial menus' images (#376), made here with no swapchain image acquired: SteamVR refused one made
	// in the frame while the flat frame's image was acquired (XR_ERROR_RUNTIME_FAILURE, TRAPS.md). Without
	// them the menus still work, unseen - said once here, and in status.json (vr.radial.images)
	const char * const radialNames[] = { "radial menu (left)", "radial menu (right)" };
	for(size_t hand = 0; hand < HandCount; hand++) {
		Swapchain & image = g_xr.radials[hand].image;
		if(!image.create(g_xr.session, g_xr.colorFormat, RadialImageSize, true, radialNames[hand])) {
			image.destroy();
			g_xr.radialFailed = true;
		}
	}
	if(g_xr.radialFailed) {
		LogWarning << "[vr] radial menu: no image of " << RadialImageSize.x << 'x' << RadialImageSize.y
		           << ", the radial menus work but are not drawn";
	}
	// The subtitle panel's image (#29, #519): the engine's own texture with a framebuffer to draw it, sampled into each
	// eye by the game - no swapchain: a compositor quad layer of it stayed invisible on SteamVR
	if(GRenderer) {
		OpenXRState::SubtitlePanel & panel = g_xr.subtitle;
		panel.texture = GRenderer->createTexture();
		bool complete = false;
		if(panel.texture
		   && panel.texture->create(size_t(SubtitleImageWidth), size_t(SubtitleImageHeight), Image::Format_R8G8B8A8)) {
			panel.texture->upload();
			GLint previous = 0;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
			glGenFramebuffers(1, &panel.framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, panel.framebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			                       static_cast<GLTexture *>(panel.texture)->glId(), 0);
			complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
			glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
		}
		if(complete) {
			LogInfo << "[vr] subtitle panel: image " << SubtitleImageWidth << 'x' << SubtitleImageHeight
			        << ", drawn into the eyes";
		} else {
			LogWarning << "[vr] subtitles: no image of " << SubtitleImageWidth << 'x' << SubtitleImageHeight
			           << ", the scene subtitles are not drawn";
			destroySubtitleImage();
		}
	}

	return true;
}

void beginSession() {

	if(g_xr.running) {
		return;
	}

	XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
	beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	XrResult result = xrBeginSession(g_xr.session, &beginInfo);
	if(XR_FAILED(result)) {
		LogError << "Could not begin the OpenXR session (" << resultName(result) << "), the headset stays dark";
		return;
	}

	g_xr.running = true;
	g_xr.frameFailures = 0;
	g_xr.loggedInvalidViews = false;
	g_xr.stereoLogged = false;
	g_xr.ui = OpenXRState::Interface();
	g_xr.frame = OpenXRState::Frame();
	g_xr.layers = OpenXRState::Layers();
	g_xr.gapsInWindow = 0;

	// The even-cadence cap (F-26): the command line first, then cfg.ini; 1 = every display slot
	g_xr.divisor = g_frameDivisorOverride ? g_frameDivisorOverride
	                                      : u32(std::clamp(config.video.vrFrameDivisor, 1, 8));
	g_xr.pacing.setDivisor(g_xr.divisor);
	g_xr.pacing.reset();

	// The desktop mirror (#21): the command line first, then cfg.ini
	g_xr.mirror.begin(!g_disableMirror && config.video.vrMirror,
	                  g_disableMirror ? "--no-vr-mirror" : "vr_mirror = 0 in cfg.ini",
	                  size_t(std::clamp(config.video.vrMirrorEye, 0, 1)));

	LogInfo << "OpenXR session running, submitting frames: xrWaitFrame paces the game";
	if(g_xr.divisor > 1) {
		LogInfo << "[vr] frame cap: every " << g_xr.divisor << ". display slot (vr_frame_divisor "
		        << g_xr.divisor << " from " << (g_frameDivisorOverride ? "--vr-frame-divisor" : "cfg.ini")
		        << "), the rate line must read EVEN at 1/" << g_xr.divisor << " of the refresh rate";
	}
}

void endSession() {

	if(!g_xr.running) {
		return;
	}

	// A frame begun before the runtime asked to stop is ended first, so the session stops clean
	if(g_xr.frame.begun) {
		abandonFrameTargets();
		endFrame(true);
	}

	flushPacing();
	g_xr.running = false;

	XrResult result = xrEndSession(g_xr.session);
	if(XR_FAILED(result)) {
		LogWarning << "Could not end the OpenXR session: " << resultName(result);
		return;
	}

	LogInfo << "OpenXR session stopped after " << g_xr.frames.submitted << " frames ("
	        << g_xr.frames.shown << " shown, " << g_xr.stereo.frames << " in stereo)";
}

void logFrameFailure(const char * call, XrResult result) {
	g_xr.frameFailures++;
	if(g_xr.frameFailures <= MaxLoggedFrameFailures) {
		LogError << call << " failed: " << resultName(result)
		         << (g_xr.frameFailures == MaxLoggedFrameFailures ? " (further frame failures are not logged)" : "");
	}
}

/*!
 * The OpenGL state the VR frame code changes, restored afterwards: the renderer caches its own
 * view of the scissor and the texture bindings and would not notice a change.
 */
class GLStateGuard {

	GLint m_drawFramebuffer = 0;
	GLint m_readFramebuffer = 0;
	GLint m_readBuffer = 0;
	GLint m_texture = 0;
	GLboolean m_scissor = GL_FALSE;
	GLboolean m_colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };

public:

	GLStateGuard() {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
		glGetIntegerv(GL_READ_BUFFER, &m_readBuffer);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture);
		m_scissor = glIsEnabled(GL_SCISSOR_TEST);
		glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
		// Clears respect both
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	~GLStateGuard() {
		glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
		if(m_scissor) {
			glEnable(GL_SCISSOR_TEST);
		}
		glBindTexture(GL_TEXTURE_2D, GLuint(m_texture));
		glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(m_readFramebuffer));
		glReadBuffer(GLenum(m_readBuffer));
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(m_drawFramebuffer));
	}

	GLStateGuard(const GLStateGuard &) = delete;
	GLStateGuard & operator=(const GLStateGuard &) = delete;

};

/*!
 * The quad's image is the interface's virtual screen 1:1: the window's size until the eyes were
 * located, then the interface's own resolution (#73). A size the swapchain could not be created
 * for is not retried every frame.
 */
void updateQuadSwapchain(const Vec2i & size) {

	if((g_xr.quad.isValid() && g_xr.quad.size() == size) || size == g_xr.quadFailedSize) {
		return;
	}

	g_xr.quad.destroy();
	g_xr.quadFailedSize = Vec2i(0);

	if(size.x <= 0 || size.y <= 0) {
		return;
	}

	if(size.x > g_xr.maxSwapchainSize.x || size.y > g_xr.maxSwapchainSize.y) {
		LogWarning << "Flat frame " << size.x << 'x' << size.y << " is larger than the biggest OpenXR swapchain "
		           << g_xr.maxSwapchainSize.x << 'x' << g_xr.maxSwapchainSize.y << ", the flat frame is not shown in VR";
		g_xr.quadFailedSize = size;
		return;
	}

	// With framebuffers: in a stereo frame the 2D interface is drawn straight into the quad's image
	if(!g_xr.quad.create(g_xr.session, g_xr.colorFormat, size, true, "flat frame quad")) {
		g_xr.quadFailedSize = size;
	}
}

/*!
 * The interface's virtual screen in VR (#73): the quad at the headset's pixel density with the
 * window's shape, once the session's eyes were located; the window's size before that, outside a
 * session, and when no quad of that size could be made (the interface then stays in the window).
 */
Vec2i interfaceTargetSize(const Vec2i & windowSize) {
	if(!g_xr.running || !g_xr.ui.known || !g_xr.eyes[0].isValid()) {
		return windowSize;
	}
	Vec2i size = interfaceResolution(g_xr.eyes[0].size(), g_xr.ui.fov, QuadWidth, QuadDistance, windowSize,
	                                 g_xr.maxSwapchainSize);
	return (size == g_xr.quadFailedSize) ? windowSize : size;
}

/*!
 * The interface size once per change: the log line, the game event and status.json say what the
 * quad is drawn at, against the window and the eye's pixel density
 */
void logInterfaceSize(const Vec2i & size, const Vec2i & windowSize) {

	if(size == g_xr.ui.logged) {
		return;
	}
	g_xr.ui.logged = size;

	const float ppd = eyePixelsPerDegree(g_xr.eyes[0].size().x, g_xr.ui.fov);
	g_xr.stereo.interfaceSize = size;
	g_xr.stereo.eyePixelsPerDegree = ppd;

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(1);
	oss << "[vr] interface: " << size.x << 'x' << size.y << " on the head-locked quad ("
	    << std::setprecision(2) << QuadWidth << " m at " << QuadDistance << " m, " << std::setprecision(1)
	    << ppd << " px per degree like the eyes) instead of the window's " << windowSize.x << 'x' << windowSize.y;
	LogInfo << oss.str();

	gameevent::Event("vr.interface")("width", u64(size.x))("height", u64(size.y))
		("windowWidth", u64(std::max(windowSize.x, s32(0))))("windowHeight", u64(std::max(windowSize.y, s32(0))))
		("pxPerDeg", ppd, 1);
}

/*!
 * Locate the eyes for the frame's predicted display time in the world-locked space: the poses this
 * frame renders with and is submitted with (F-21, F-116 - never the current locate at submission).
 * Without valid tracking the views are marked invalid and the projection layer is left out.
 */
void locateFrameViews(XrTime displayTime) {

	OpenXRState::Frame & frame = g_xr.frame;
	frame.views = FrameViews();
	frame.views.displayTime = displayTime;

	XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = displayTime;
	locateInfo.space = g_xr.localSpace;
	XrViewState viewState = { XR_TYPE_VIEW_STATE };
	frame.located.fill({ XR_TYPE_VIEW });
	uint32_t count = 0;
	XrResult result = xrLocateViews(g_xr.session, &locateInfo, &viewState, u32(frame.located.size()), &count,
	                                frame.located.data());
	if(XR_FAILED(result)) {
		logFrameFailure("xrLocateViews", result);
		return;
	}

	const XrViewStateFlags valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
	if(count != frame.located.size() || (viewState.viewStateFlags & valid) != valid) {
		if(!g_xr.loggedInvalidViews) {
			LogWarning << "OpenXR eye poses not available (" << count << " views, flags 0x" << std::hex
			           << viewState.viewStateFlags << std::dec << "), the background layer is left out";
			g_xr.loggedInvalidViews = true;
		}
		return;
	}

	for(size_t eye = 0; eye < frame.located.size(); eye++) {
		const XrView & view = frame.located[eye];
		EyeView & out = frame.views.eyes[eye];
		out.position = Vec3f(view.pose.position.x, view.pose.position.y, view.pose.position.z);
		out.orientation = glm::quat(view.pose.orientation.w, view.pose.orientation.x,
		                            view.pose.orientation.y, view.pose.orientation.z);
		out.fovLeft = view.fov.angleLeft;
		out.fovRight = view.fov.angleRight;
		out.fovUp = view.fov.angleUp;
		out.fovDown = view.fov.angleDown;
	}

	frame.views.valid = true;

	// How high the LOCAL origin is above the room's floor (#27): both spaces are gravity-aligned, so a head
	// at height y in LOCAL is y + localAboveFloor above the floor
	if(g_xr.stageSpace != XR_NULL_HANDLE) {
		XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
		result = xrLocateSpace(g_xr.localSpace, g_xr.stageSpace, displayTime, &location);
		if(XR_SUCCEEDED(result) && (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
			frame.views.floorValid = true;
			frame.views.localAboveFloor = location.pose.position.y;
			if(!g_xr.loggedStage) {
				LogInfo << "[vr] stage: the LOCAL origin is " << location.pose.position.y << " m above the floor";
				g_xr.loggedStage = true;
			}
		}
	}
}

/*!
 * Describe the eye swapchains as the projection layer with the poses and fields of view located for
 * this frame (F-21, F-116): whatever the most recently released image of each eye holds is shown
 * where that pose puts it.
 */
void describeProjectionLayer(OpenXRState::Layers & layers) {

	const OpenXRState::Frame & frame = g_xr.frame;

	std::array<XrCompositionLayerProjectionView, 2> & views = layers.projectionViews;
	for(size_t eye = 0; eye < views.size(); eye++) {
		const Swapchain & swapchain = g_xr.eyes[eye];
		views[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		views[eye].pose = frame.located[eye].pose;
		views[eye].fov = frame.located[eye].fov;
		views[eye].subImage.swapchain = swapchain.handle();
		views[eye].subImage.imageRect = swapchain.rect();
		views[eye].subImage.imageArrayIndex = 0;
	}

	XrCompositionLayerProjection & layer = layers.projection;
	layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	layer.layerFlags = 0;
	layer.space = g_xr.localSpace;
	layer.viewCount = u32(views.size());
	layer.views = views.data();

	layers.list[layers.count++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
}

/*!
 * Clear both eye images to the background color through their framebuffers and describe them as a
 * projection layer with the poses located for this frame - for frames whose scene was not drawn
 * (a menu, a loading screen, a cinematic). Both eyes or none (F-116): one stale eye against a live
 * one is the bug class the two-eyes check exists for.
 */
bool renderBackground(OpenXRState::Layers & layers) {

	const OpenXRState::Frame & frame = g_xr.frame;
	if(!frame.views.valid) {
		return false;
	}

	for(size_t eye = 0; eye < g_xr.eyes.size(); eye++) {
		Swapchain & swapchain = g_xr.eyes[eye];
		if(!swapchain.acquire()) {
			return false;
		}
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, swapchain.framebuffer());
		glClearBufferfv(GL_COLOR, 0, EyeBackgroundColor);
		if(eye == g_xr.mirror.eye()) {
			g_xr.mirror.copyEye(swapchain);
		}
		swapchain.release();
	}

	describeProjectionLayer(layers);

	return true;
}

/*!
 * The projection layer with the images the scene was drawn into this frame (#19). Only when both
 * eyes were drawn and released (F-116); the caller falls back to the background otherwise.
 */
bool describeEyeLayer(OpenXRState::Layers & layers) {

	const OpenXRState::Frame & frame = g_xr.frame;
	if(!frame.views.valid || !frame.stereo || !frame.eyeRendered[0] || !frame.eyeRendered[1]) {
		return false;
	}

	describeProjectionLayer(layers);

	return true;
}

//! The beam image: narrow and long, its bottom row at the hand (#34)
const Vec2i BeamImageSize(8, 64);

/*!
 * The menu pointer's beam image, made once per session: white, premultiplied by its alpha, which runs
 * from PointerBeamAlpha at the hand (the bottom row) to nothing at the panel (the top row), with
 * softer long edges. Filled once: the compositor keeps showing the last released image.
 */
bool makeBeamImage() {

	if(g_xr.beam.isValid()) {
		return true;
	}
	if(g_xr.beamFailed) {
		return false;
	}
	if(!g_xr.beam.create(g_xr.session, g_xr.colorFormat, BeamImageSize, false, "menu pointer beam")
	   || !g_xr.beam.acquire()) {
		g_xr.beam.destroy();
		g_xr.beamFailed = true;
		LogWarning << "[vr] menu pointer: no beam image, the pointer shows only the cursor";
		return false;
	}

	std::vector<u8> pixels(size_t(BeamImageSize.x * BeamImageSize.y) * 4);
	for(s32 y = 0; y < BeamImageSize.y; y++) {
		const float along = 1.f - (float(y) + 0.5f) / float(BeamImageSize.y);
		for(s32 x = 0; x < BeamImageSize.x; x++) {
			const bool edge = (x == 0 || x == BeamImageSize.x - 1);
			const float alpha = PointerBeamAlpha * along * (edge ? 0.4f : 1.f);
			const u8 value = u8(std::lround(alpha * 255.f));
			u8 * pixel = &pixels[size_t(y * BeamImageSize.x + x) * 4];
			pixel[0] = pixel[1] = pixel[2] = pixel[3] = value;
		}
	}

	// RGBA rows are a multiple of 4 at any GL_UNPACK_ALIGNMENT, so the engine's tight state (1) is kept: the
	// alignment 4 once set here leaked into every later upload and crashed on a cinematic tile (#495, TRAPS.md)
	g_xr.mirror.setOverlayImage(DesktopMirror::BeamImage, BeamImageSize, pixels.data());
	glBindTexture(GL_TEXTURE_2D, g_xr.beam.texture());
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BeamImageSize.x, BeamImageSize.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	g_xr.beam.release();

	LogInfo << "[vr] menu pointer: beam image " << BeamImageSize.x << 'x' << BeamImageSize.y << ", "
	        << PointerBeamWidth * 1000.f << " mm wide, fading towards the panel";
	return true;
}

/*!
 * The menu pointer's beam above the floating panel (#34), when the input module asked for one this
 * frame: a quad from the hand to its hit, turned towards the eyes. A later layer is drawn over the
 * panel, which would hide a beam drawn into the scene.
 */
void describeBeamLayer(OpenXRState::Layers & layers) {

	OpenXRState::PointerBeam & pointer = g_xr.pointer;
	const FrameViews & views = g_xr.frame.views;
	if(!pointer.requested || !views.valid || layers.count >= layers.list.size() || !makeBeamImage()) {
		return;
	}

	const Vec3f eye = (views.eyes[0].position + views.eyes[1].position) * 0.5f;
	BeamQuad beam;
	if(!beamQuad(pointer.from, pointer.to, eye, PointerBeamWidth, beam)) {
		return;
	}

	XrCompositionLayerQuad & layer = layers.beam;
	layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	layer.space = g_xr.localSpace;
	layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	layer.subImage.swapchain = g_xr.beam.handle();
	layer.subImage.imageRect = g_xr.beam.rect();
	layer.subImage.imageArrayIndex = 0;
	layer.pose.orientation.x = beam.orientation.x;
	layer.pose.orientation.y = beam.orientation.y;
	layer.pose.orientation.z = beam.orientation.z;
	layer.pose.orientation.w = beam.orientation.w;
	layer.pose.position.x = beam.center.x;
	layer.pose.position.y = beam.center.y;
	layer.pose.position.z = beam.center.z;
	layer.size.width = beam.size.x;
	layer.size.height = beam.size.y;

	layers.list[layers.count++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
	pointer.submitted = true;
}

//! The hand reticle's image: a ring with a dot in its middle (#37, D-029)
const s32 ReticleImageSize = 64;

/*!
 * The hand reticle's two images, made once per session: white, premultiplied by the alpha - a ring
 * (radius 0.32 to 0.46 of the image) and a dot (0.12) with soft edges, at ReticleDimAlpha and at
 * ReticleBrightAlpha. Filled once: the compositor keeps showing the last released image.
 */
bool makeReticleImages() {

	if(g_xr.reticleImages[0].isValid() && g_xr.reticleImages[1].isValid()) {
		return true;
	}
	if(g_xr.reticleFailed) {
		return false;
	}

	const Vec2i size(ReticleImageSize, ReticleImageSize);
	const char * const names[] = { "hand reticle (dim)", "hand reticle (bright)" };
	const float alphas[] = { ReticleDimAlpha, ReticleBrightAlpha };
	std::vector<u8> pixels(size_t(size.x * size.y) * 4);
	for(size_t i = 0; i < g_xr.reticleImages.size(); i++) {
		Swapchain & image = g_xr.reticleImages[i];
		if(!image.create(g_xr.session, g_xr.colorFormat, size, false, names[i]) || !image.acquire()) {
			for(Swapchain & failed : g_xr.reticleImages) {
				failed.destroy();
			}
			g_xr.reticleFailed = true;
			LogWarning << "[vr] reticle: no reticle image, the hands aim without a visible mark";
			return false;
		}
		for(s32 y = 0; y < size.y; y++) {
			for(s32 x = 0; x < size.x; x++) {
				const float dx = (float(x) + 0.5f) / float(size.x) - 0.5f;
				const float dy = (float(y) + 0.5f) / float(size.y) - 0.5f;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float soft = 1.5f / float(size.x); // the edges fade over about a pixel and a half
				const float ring = std::clamp((r - 0.32f) / soft + 0.5f, 0.f, 1.f) * std::clamp((0.46f - r) / soft + 0.5f, 0.f, 1.f);
				const float dot = std::clamp((0.12f - r) / soft + 0.5f, 0.f, 1.f);
				const float alpha = alphas[i] * std::max(ring, dot);
				const u8 value = u8(std::lround(alpha * 255.f));
				u8 * pixel = &pixels[size_t(y * size.x + x) * 4];
				pixel[0] = pixel[1] = pixel[2] = pixel[3] = value;
			}
		}
		g_xr.mirror.setOverlayImage(i == 0 ? DesktopMirror::ReticleDimImage : DesktopMirror::ReticleBrightImage, size,
		                            pixels.data());
		glBindTexture(GL_TEXTURE_2D, image.texture());
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); // tight state kept (#495)
		glBindTexture(GL_TEXTURE_2D, 0);
		image.release();
	}

	LogInfo << "[vr] reticle: ring images " << size.x << 'x' << size.y << ", " << ReticleAngleDegrees
	        << " deg wide, alpha " << ReticleDimAlpha << " / " << ReticleBrightAlpha;
	return true;
}

/*!
 * The hand reticles (#37): a small ring where each hand's hidden ray hits the world, as a quad layer
 * on top of everything (the ring sits on the first hit, so nothing in the world is in front of it).
 */
void describeReticleLayers(OpenXRState::Layers & layers) {

	const FrameViews & views = g_xr.frame.views;
	if(!views.valid) {
		return;
	}
	const Vec3f eye = (views.eyes[0].position + views.eyes[1].position) * 0.5f;

	for(size_t hand = 0; hand < HandCount; hand++) {
		OpenXRState::HandReticle & reticle = g_xr.reticles[hand];
		if(!reticle.requested || layers.count >= layers.list.size() || !makeReticleImages()) {
			continue;
		}
		ReticleQuad quad;
		if(!reticleQuad(reticle.hit, eye, ReticleAngleDegrees, quad)) {
			continue;
		}
		const Swapchain & image = g_xr.reticleImages[reticle.bright ? 1 : 0];
		XrCompositionLayerQuad & layer = layers.reticles[hand];
		layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
		layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		layer.space = g_xr.localSpace;
		layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		layer.subImage.swapchain = image.handle();
		layer.subImage.imageRect = image.rect();
		layer.subImage.imageArrayIndex = 0;
		layer.pose.orientation.x = quad.orientation.x;
		layer.pose.orientation.y = quad.orientation.y;
		layer.pose.orientation.z = quad.orientation.z;
		layer.pose.orientation.w = quad.orientation.w;
		layer.pose.position.x = quad.center.x;
		layer.pose.position.y = quad.center.y;
		layer.pose.position.z = quad.center.z;
		layer.size.width = quad.size;
		layer.size.height = quad.size;
		layers.list[layers.count++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
		reticle.submitted = true;
	}
}

/*!
 * The radial menus (#376): each hand's image, drawn by the game this frame, as a quad layer on top of
 * everything - a menu held in front of the hand is never hidden by the world or the hand itself.
 */
void describeRadialLayers(OpenXRState::Layers & layers) {

	for(size_t hand = 0; hand < HandCount; hand++) {
		OpenXRState::RadialMenu & radial = g_xr.radials[hand];
		const bool drawn = radial.drawn;
		radial.drawn = false;
		if(!radial.requested || !drawn || radial.size <= 0.f || layers.count >= layers.list.size()) {
			if(radial.requested && drawn && layers.count >= layers.list.size()) {
				radial.refusal = "the frame's layer list is full";
			}
			continue;
		}
		XrCompositionLayerQuad & layer = layers.radials[hand];
		layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
		layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		layer.space = g_xr.localSpace;
		layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		layer.subImage.swapchain = radial.image.handle();
		layer.subImage.imageRect = radial.image.rect();
		layer.subImage.imageArrayIndex = 0;
		layer.pose.orientation.x = radial.orientation.x;
		layer.pose.orientation.y = radial.orientation.y;
		layer.pose.orientation.z = radial.orientation.z;
		layer.pose.orientation.w = radial.orientation.w;
		layer.pose.position.x = radial.center.x;
		layer.pose.position.y = radial.center.y;
		layer.pose.position.z = radial.center.z;
		layer.size.width = radial.size;
		layer.size.height = radial.size;
		layers.list[layers.count++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);
		radial.submitted = true;
	}
}

/*!
 * Describe the quad that shows the flat frame swapchain's most recently released image: the
 * floating panel in the world-locked space while a menu is open (#49), otherwise the head-locked
 * quad in front of the eyes.
 *
 * \param layerFlags \c XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT when the image holds the
 *                   2D interface over a transparent background (a stereo frame): the renderer
 *                   accumulates coverage in the alpha channel, so the colors are premultiplied, as
 *                   the compositor expects; 0 for an opaque copy of the window
 */
void describeQuadLayer(OpenXRState::Layers & layers, XrCompositionLayerFlags layerFlags) {

	const Swapchain & swapchain = g_xr.quad;

	XrCompositionLayerQuad & layer = layers.quad;
	layer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	layer.layerFlags = layerFlags;
	layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	layer.subImage.swapchain = swapchain.handle();
	layer.subImage.imageRect = swapchain.rect();
	layer.subImage.imageArrayIndex = 0;

	// A logo or a cinematic on its screen (#394), else an open menu on the floating panel (#49)
	const bool screen = g_xr.panel.screen.mode() == PanelMode::World;
	if(screen || g_xr.panel.anchor.mode() == PanelMode::World) {
		const PanelPose & pose = screen ? g_xr.panel.screen.pose() : g_xr.panel.anchor.pose();
		const Vec2f size = panelSize(screen ? ScreenWidth : g_xr.panel.params.width, swapchain.size());
		layer.space = g_xr.localSpace;
		layer.pose.orientation.x = pose.orientation.x;
		layer.pose.orientation.y = pose.orientation.y;
		layer.pose.orientation.z = pose.orientation.z;
		layer.pose.orientation.w = pose.orientation.w;
		layer.pose.position.x = pose.position.x;
		layer.pose.position.y = pose.position.y;
		layer.pose.position.z = pose.position.z;
		layer.size.width = size.x;
		layer.size.height = size.y;
		g_xr.panel.size = size;
	} else {
		layer.space = g_xr.viewSpace;
		layer.pose.orientation.x = 0.f;
		layer.pose.orientation.y = 0.f;
		layer.pose.orientation.z = 0.f;
		layer.pose.orientation.w = 1.f;
		layer.pose.position.x = 0.f;
		layer.pose.position.y = 0.f;
		layer.pose.position.z = -QuadDistance;
		layer.size.width = QuadWidth;
		layer.size.height = QuadWidth * float(swapchain.size().y) / float(swapchain.size().x);
	}

	layers.list[layers.count++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer);

	if(layer.space == g_xr.localSpace && !screen) {
		describeBeamLayer(layers);
	}
}

//! The game shows a logo, the intro or a cinematic: on the screen in the room, not the eyes (#394)
bool onScreen(gameevent::GameState state) {
	return state == gameevent::GameState::Logo || state == gameevent::GameState::Intro
	       || state == gameevent::GameState::Cinematic;
}

//! The panel settings from cfg.ini (clamped when read)
PanelParams panelParams() {
	PanelParams params;
	params.distance = config.video.vrPanelDistance;
	params.drop = config.video.vrPanelDrop;
	params.width = config.video.vrPanelWidth;
	return params;
}

/*!
 * The floating UI panel for this frame (#49): the engine's menu state (setMenuOpen) and the head
 * this frame renders with place it or keep it (vr/UIPanel.h). Logged as game events on placement
 * and on every change between the panel and the head-locked quad - never per frame.
 */
void updatePanel() {

	OpenXRState::Panel & panel = g_xr.panel;
	const OpenXRState::Frame & frame = g_xr.frame;

	HeadSample head;
	const HeadSample * tracked = nullptr;
	if(frame.views.valid) {
		const EyeView & left = frame.views.eyes[0];
		const EyeView & right = frame.views.eyes[1];
		head = headFromEyes(left.position, left.orientation, right.position, right.orientation);
		tracked = &head;
	}

	panel.params = panelParams();
	const s64 nowUs = s64(frame.state.predictedDisplayTime / 1000); // OpenXR nanoseconds
	const PanelPlacement placed = panel.anchor.update(!panel.menu.empty(), nowUs, tracked, panel.params);

	if(placed != PanelPlacement::None) {
		panel.lastPlacement = placed;
		const PanelPose & pose = panel.anchor.pose();
		const Vec3f heading = pose.orientation * Vec3f(0.f, 0.f, -1.f);
		gameevent::Event("vr.panel.placed")("reason", panelPlacementName(placed))("menu", panel.menu)
			("x", pose.position.x, 2)("y", pose.position.y, 2)("z", pose.position.z, 2)
			("yawDeg", glm::degrees(std::atan2(-heading.x, -heading.z)), 1)
			("distanceM", panel.params.distance, 2)("dropM", panel.params.drop, 2)("widthM", panel.params.width, 2)
			("headY", head.position.y, 2);
	}

	const PanelMode mode = panel.anchor.mode();
	if(mode != panel.loggedMode) {
		panel.loggedMode = mode;
		gameevent::Event("vr.panel")("mode", panelModeName(mode))("menu", panel.menu.empty() ? "none" : panel.menu);
		platform::flightRecord(platform::RecordKind::State, std::string("panel ") + panelModeName(mode)
		                       + ' ' + (panel.menu.empty() ? "none" : panel.menu));
	}

	// The logos and the cinematics play on a screen fixed in the room (#394): placed where the player
	// looks when one starts, like a menu's panel, never head-locked (a picture glued to the face). The
	// hybrid's scene shots (#29) on the same screen, while this frame is drawn flat for it
	const gameevent::GameState state = gameevent::currentGameState();
	const bool sceneScreen = panel.sceneScreen && !frame.stereo;
	const bool playing = onScreen(state) || sceneScreen;
	PanelParams screenParams;
	screenParams.distance = ScreenDistance;
	screenParams.drop = ScreenDrop;
	screenParams.width = ScreenWidth;
	const PanelPlacement screenPlaced = panel.screen.update(playing, nowUs, tracked, screenParams);
	if(screenPlaced != PanelPlacement::None) {
		const PanelPose & pose = panel.screen.pose();
		const Vec3f heading = pose.orientation * Vec3f(0.f, 0.f, -1.f);
		gameevent::Event("vr.screen.placed")("reason", panelPlacementName(screenPlaced))
			("for", sceneScreen ? "scene" : gameevent::gameStateName(state))
			("x", pose.position.x, 2)("y", pose.position.y, 2)("z", pose.position.z, 2)
			("yawDeg", glm::degrees(std::atan2(-heading.x, -heading.z)), 1)
			("distanceM", ScreenDistance, 2)("widthM", ScreenWidth, 2)("headY", head.position.y, 2);
	}
	const PanelMode screenMode = panel.screen.mode();
	if(screenMode != panel.loggedScreenMode) {
		panel.loggedScreenMode = screenMode;
		gameevent::Event("vr.screen")("state", screenMode == PanelMode::World ? "on" : "off")
			("for", sceneScreen ? "scene" : gameevent::gameStateName(state));
	}
}

/*!
 * Copy the window's back buffer into the quad swapchain and describe the head-locked quad - for a
 * frame drawn into the window (one shown from outside the game loop, F-20).
 *
 * A copy from the default framebuffer, not a blit: the window is usually multisampled, and a
 * resolving blit requires identical source and destination formats (RGB8 vs. SRGB8_ALPHA8 here),
 * while glCopyTexSubImage2D resolves the default framebuffer and stores the values unchanged.
 */
bool copyFlatFrame(OpenXRState::Layers & layers) {

	// Once the interface has its own resolution (#73) the quad no longer has the window's size:
	// a frame drawn into the window anyway (one that ended early, a level load) goes without it
	Swapchain & swapchain = g_xr.quad;
	if(!swapchain.isValid() || swapchain.size() != g_xr.frame.windowSize || !swapchain.acquire()) {
		return false;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	glBindTexture(GL_TEXTURE_2D, swapchain.texture());
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, swapchain.size().x, swapchain.size().y);
	g_xr.mirror.copyFlatFrame(swapchain);
	swapchain.release();

	describeQuadLayer(layers, 0);

	return true;
}

/*!
 * The flat frame image, fitted into the window's back buffer with its aspect kept - what the window
 * shows in a stereo frame while the desktop mirror (#21) is off: the 2D interface over black, the
 * same picture the window held before the interface moved to the quad.
 */
void blitToWindow(const Swapchain & image, const Vec2i & windowSize) {

	if(windowSize.x <= 0 || windowSize.y <= 0) {
		return;
	}

	GLStateGuard state;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, image.framebuffer());
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	const GLfloat black[4] = { 0.f, 0.f, 0.f, 1.f };
	glClearBufferfv(GL_COLOR, 0, black);

	const Vec2i source = image.size();
	const float scale = std::min(float(windowSize.x) / float(source.x), float(windowSize.y) / float(source.y));
	const Vec2i size(std::max(1, int(float(source.x) * scale)), std::max(1, int(float(source.y) * scale)));
	const Vec2i offset = (windowSize - size) / Vec2i(2);
	glBlitFramebuffer(0, 0, source.x, source.y, offset.x, offset.y, offset.x + size.x, offset.y + size.y,
	                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

/*!
 * Hand back the quad image the 2D interface was drawn into this frame and describe the quad: in
 * a stereo frame blended by its alpha over the eyes (#19), otherwise opaque - the whole flat frame,
 * scene included, drawn straight into it at the interface's resolution (#73). The desktop mirror
 * takes its copy of the image first; without the mirror, or without eyes to mirror, the window
 * shows the image itself.
 */
bool finishFlatFrame(OpenXRState::Layers & layers) {

	OpenXRState::Frame & frame = g_xr.frame;
	if(!frame.quadAcquired) {
		return false;
	}

	if(g_xr.mirror.isOn()) {
		g_xr.mirror.copyFlatFrame(g_xr.quad);
	}
	if(!g_xr.mirror.isOn() || !frame.stereo) {
		blitToWindow(g_xr.quad, frame.windowSize);
	}

	g_xr.quad.release();
	frame.quadAcquired = false;

	describeQuadLayer(layers, frame.stereo ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0);
	if(frame.stereo) {
		describeReticleLayers(layers);
		describeRadialLayers(layers);
	}

	return true;
}

//! The stereo banner (research row F-115): the eyes, their fields of view, the cant and the near plane
void logStereoBanner(const OpenXRState::Frame & frame, const Vec2i & eyeSize) {

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(1);
	oss << "[vr] stereo: the scene is drawn per eye - ";
	const char * const names[] = { "left", "right" };
	for(size_t eye = 0; eye < frame.rig.size(); eye++) {
		const EyeFov & fov = frame.rig[eye].fov;
		oss << names[eye] << ' ' << eyeSize.x << 'x' << eyeSize.y << " fov L " << glm::degrees(fov.left) << " R "
		    << glm::degrees(fov.right) << " U " << glm::degrees(fov.up) << " D " << glm::degrees(fov.down)
		    << " deg, ";
	}
	const float cant = glm::degrees(cantAngle(frame.rig));
	oss << "eye separation " << eyeSeparationMeters(frame.rig) * 1000.f << " mm, "
	    << (cant > 0.05f ? "canted displays: " : "parallel displays: ") << cant << " deg between the eyes, "
	    << "union fov L " << glm::degrees(frame.unionFov.left) << " R " << glm::degrees(frame.unionFov.right)
	    << " U " << glm::degrees(frame.unionFov.up) << " D " << glm::degrees(frame.unionFov.down)
	    << " deg (one cull for both eyes), near " << std::setprecision(2) << NearClipMeters << " m = "
	    << std::setprecision(1) << NearClipMeters * EyeRigParams().unitsPerMeter << " units; the 2D interface on the head-locked quad, menus on the floating panel ("
	    << interfaceTargetSize(frame.windowSize).x << 'x' << interfaceTargetSize(frame.windowSize).y
	    << ", blended by its alpha)";
	LogInfo << oss.str();

	gameevent::Event("vr.stereo")("state", "on")("eyeWidth", u64(eyeSize.x))("eyeHeight", u64(eyeSize.y))
		("ipdMm", eyeSeparationMeters(frame.rig) * 1000.f, 1)("cantDeg", cant, 1)
		("hfovDeg", glm::degrees(horizontalFov(frame.rig[0].fov)), 1)
		("vfovDeg", glm::degrees(verticalFov(frame.rig[0].fov)), 1);
}

void beginStereo(OpenXRState::Frame & frame);

//! What a begun frame draws into
enum class FrameTargets {
	Game,      //!< the game loop: the scene per eye when it can be, everything 2D into the quad
	Interface, //!< a frame of the 2D interface alone (the loading screen): the quad, opaque
	Drawn      //!< a frame already drawn into the window: nothing is bound, the window is copied
};

/*!
 * Set up the frame's render targets. A game frame the runtime wants rendered, with located eyes,
 * is a stereo frame (#19): the eyes relative to the head camera and their union frustum, and the
 * flat frame quad's image as the target for everything 2D, cleared to transparent so the world
 * shows through. Any other frame, once the interface has its own resolution (#73), draws the flat
 * frame straight into the quad's image, cleared to opaque black, as the window would have shown it;
 * before that it draws into the window, which is then copied onto the quad.
 */
void beginFrameTargets(const Vec2i & windowSize, FrameTargets targets) {

	OpenXRState::Frame & frame = g_xr.frame;
	frame.stereo = false;
	frame.eyeRendered = { { false, false } };
	frame.quadAcquired = false;
	frame.eyeInRender = size_t(-1);
	frame.windowSize = windowSize;
	g_xr.stereo.active = false;

	// The session's first located eyes set the interface's resolution (#73)
	if(frame.views.valid && !g_xr.ui.known) {
		g_xr.ui.known = true;
		g_xr.ui.fov.left = frame.views.eyes[0].fovLeft;
		g_xr.ui.fov.right = frame.views.eyes[0].fovRight;
		g_xr.ui.fov.up = frame.views.eyes[0].fovUp;
		g_xr.ui.fov.down = frame.views.eyes[0].fovDown;
	}
	const Vec2i interfaceSize = interfaceTargetSize(windowSize);
	if(g_xr.ui.known && interfaceSize != windowSize) {
		logInterfaceSize(interfaceSize, windowSize);
	}

	if(targets == FrameTargets::Drawn) {
		return;
	}

	// The intro's scripted scene is a film on the screen (#394), and so is a scene shot the hybrid puts on
	// its screen (#29): drawn flat into the quad like the cinematics, never per eye (the state of the last
	// frame: this one has not been decided yet)
	// Only while the game is played: a scene left by the menu or a load (the in-game intro ends in the main menu)
	// takes its screen with it
	g_xr.panel.sceneScreen = targets == FrameTargets::Game && g_xr.panel.sceneScreenWanted
	                         && gameevent::currentGameState() == gameevent::GameState::Gameplay;
	if(targets == FrameTargets::Game && frame.state.shouldRender && frame.views.valid
	   && gameevent::currentGameState() != gameevent::GameState::Intro && !g_xr.panel.sceneScreen) {
		beginStereo(frame);
	} else if(interfaceSize == windowSize) {
		// The interface is the window: drawn there, copied onto the quad (#18)
		return;
	}

	// Everything 2D goes to the flat frame quad; without one (a size too large for a swapchain)
	// it stays in the window and the headset shows the eyes alone
	updateQuadSwapchain(interfaceSize);
	if(g_xr.quad.isValid() && g_xr.quad.acquire()) {
		frame.quadAcquired = true;
		bindTarget(g_xr.quad.framebuffer(), g_xr.quad.size());
		// Clears respect the scissor and the masks the renderer left behind
		GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
		GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
		GLboolean depthMask = GL_TRUE;
		glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);
		const GLfloat transparent[4] = { 0.f, 0.f, 0.f, 0.f };
		const GLfloat opaque[4] = { 0.f, 0.f, 0.f, 1.f };
		const GLfloat depth = 1.f;
		glClearBufferfv(GL_COLOR, 0, frame.stereo ? transparent : opaque);
		glClearBufferfv(GL_DEPTH, 0, &depth);
		glDepthMask(depthMask);
		glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
		if(scissor) {
			glEnable(GL_SCISSOR_TEST);
		}
	}
}

/*!
 * The stereo half of a game frame (#19): the eyes relative to the head camera and their union
 * frustum from the located views, and the stereo status
 */
void beginStereo(OpenXRState::Frame & frame) {

	std::array<LocatedEye, 2> located;
	for(size_t eye = 0; eye < located.size(); eye++) {
		const EyeView & view = frame.views.eyes[eye];
		located[eye].position = view.position;
		located[eye].orientation = view.orientation;
		located[eye].fov.left = view.fovLeft;
		located[eye].fov.right = view.fovRight;
		located[eye].fov.up = view.fovUp;
		located[eye].fov.down = view.fovDown;
	}
	frame.rig = eyeRig(located[0], located[1]);
	frame.unionFov = unionFov(frame.rig);
	frame.stereo = true;

	const Vec2i eyeSize = g_xr.eyes[0].size();
	StereoStatus & status = g_xr.stereo;
	status.active = true;
	status.eyeSize = eyeSize;
	status.fov = { { frame.rig[0].fov, frame.rig[1].fov } };
	status.unionFov = frame.unionFov;
	status.eyeSeparationM = eyeSeparationMeters(frame.rig);
	status.cantDeg = glm::degrees(cantAngle(frame.rig));

	if(!g_xr.stereoLogged) {
		g_xr.stereoLogged = true;
		logStereoBanner(frame, eyeSize);
	}
}

/*!
 * The desktop mirror (#21): draw what the mirrored eye (vr_mirror_eye, the right one by default)
 * saw in this frame into the window's back buffer. The head-locked flat frame quad lives in the head
 * (view) space, so the eye is located there for the frame's display time - the same eye the
 * projection layer was submitted with, seen from the head instead of the room; the floating panel
 * (#49) lives in the room, where the eye was located for the frame already.
 */
void drawMirror(const Vec2i & windowSize, bool flatFrame) {

	const OpenXRState::Frame & frame = g_xr.frame;
	const size_t eye = g_xr.mirror.eye();
	const XrView & mirrored = frame.located[eye];

	MirrorFov fov;
	fov.left = mirrored.fov.angleLeft;
	fov.right = mirrored.fov.angleRight;
	fov.up = mirrored.fov.angleUp;
	fov.down = mirrored.fov.angleDown;

	DesktopMirror::Quad quad;
	Vec3f position(0.f);
	glm::quat orientation(1.f, 0.f, 0.f, 0.f);
	// The 2D interface drawn into the quad (#19) is blended by its alpha, a copied window is opaque
	quad.blended = (g_xr.layers.quad.layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
	quad.width = g_xr.layers.quad.size.width;
	quad.height = g_xr.layers.quad.size.height;
	if(flatFrame && g_xr.layers.quad.space == g_xr.localSpace) {
		// The floating panel (#49) lives in the world-locked space the eyes were located in for this frame
		if(frame.views.valid) {
			const XrPosef & pose = g_xr.layers.quad.pose;
			position = Vec3f(mirrored.pose.position.x, mirrored.pose.position.y, mirrored.pose.position.z);
			orientation = glm::quat(mirrored.pose.orientation.w, mirrored.pose.orientation.x,
			                        mirrored.pose.orientation.y, mirrored.pose.orientation.z);
			quad.position = Vec3f(pose.position.x, pose.position.y, pose.position.z);
			quad.orientation = glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
			quad.shown = true;
		}
	} else if(flatFrame) {
		XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = frame.state.predictedDisplayTime;
		locateInfo.space = g_xr.viewSpace;
		XrViewState viewState = { XR_TYPE_VIEW_STATE };
		std::array<XrView, 2> inHead;
		inHead.fill({ XR_TYPE_VIEW });
		uint32_t count = 0;
		XrResult result = xrLocateViews(g_xr.session, &locateInfo, &viewState, u32(inHead.size()), &count,
		                                inHead.data());
		const XrViewStateFlags valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
		if(XR_SUCCEEDED(result) && count == inHead.size() && (viewState.viewStateFlags & valid) == valid) {
			const XrPosef & pose = inHead[eye].pose;
			position = Vec3f(pose.position.x, pose.position.y, pose.position.z);
			orientation = glm::quat(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
			quad.shown = true;
			quad.position = Vec3f(0.f, 0.f, -QuadDistance);
		} else if(XR_FAILED(result)) {
			logFrameFailure("xrLocateViews (head space, desktop mirror)", result);
		}
	}

	// The quad layers after the flat frame (#271): the menu pointer's beam, then the hand reticles, in the
	// order they were submitted. They live in the room, where the eye was located for the frame.
	std::vector<DesktopMirror::Overlay> overlays;
	const Vec3f roomEye(mirrored.pose.position.x, mirrored.pose.position.y, mirrored.pose.position.z);
	const glm::quat roomOrientation(mirrored.pose.orientation.w, mirrored.pose.orientation.x,
	                                mirrored.pose.orientation.y, mirrored.pose.orientation.z);
	if(frame.views.valid) {
		const auto add = [&](DesktopMirror::OverlayImage image, const XrCompositionLayerQuad & layer) {
			DesktopMirror::Overlay overlay;
			overlay.image = image;
			overlay.width = layer.size.width;
			overlay.height = layer.size.height;
			overlay.position = Vec3f(layer.pose.position.x, layer.pose.position.y, layer.pose.position.z);
			overlay.orientation = glm::quat(layer.pose.orientation.w, layer.pose.orientation.x,
			                                layer.pose.orientation.y, layer.pose.orientation.z);
			overlays.push_back(overlay);
		};
		if(g_xr.pointer.submitted) {
			add(DesktopMirror::BeamImage, g_xr.layers.beam);
		}
		for(size_t hand = 0; hand < HandCount; hand++) {
			const OpenXRState::HandReticle & reticle = g_xr.reticles[hand];
			if(reticle.submitted) {
				add(reticle.bright ? DesktopMirror::ReticleBrightImage : DesktopMirror::ReticleDimImage,
				    g_xr.layers.reticles[hand]);
			}
		}
	}

	g_xr.mirror.draw(windowSize, fov, position, orientation, quad, overlays, roomEye, roomOrientation);
}

/*!
 * Hand the current frame to the compositor (xrEndFrame) and feed the pacing instrument.
 *
 * With \a resubmit no new picture was drawn for this frame: the layers of the last shown frame
 * are sent again, with the poses their images were rendered with (F-116) - the compositor shows
 * the most recently released image of each swapchain, so nothing is acquired. Zero layers would
 * mean "show nothing", not "hold the picture" (F-117). Frames the runtime did not want rendered
 * carry no layers either way.
 *
 * \return false when xrEndFrame failed (logged)
 */
bool endFrame(bool resubmit) {

	OpenXRState::Frame & frame = g_xr.frame;
	OpenXRState::Layers & layers = g_xr.layers;
	arx_assert(frame.begun);

	u32 count = frame.state.shouldRender ? layers.count : 0;

	XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = frame.state.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = count;
	endInfo.layers = count ? layers.list.data() : nullptr;

	frame.timing.endStart = toUs(platform::getTime());
	XrResult result;
	{
		perf::Scope perfEnd(PerfPhase::VrEnd);
		result = xrEndFrame(g_xr.session, &endInfo);
	}
	frame.timing.endEnd = toUs(platform::getTime());
	frame.begun = false;
	if(XR_FAILED(result)) {
		logFrameFailure("xrEndFrame", result);
		return false;
	}
	frame.timing.shown = count != 0;

	g_xr.frames.submitted++;
	if(g_xr.frames.submitted == 1) {
		LogInfo << "First OpenXR frame submitted, headset display period "
		        << float(frame.state.predictedDisplayPeriod) / 1e6f << " ms";
	}
	if(count) {
		g_xr.frames.shown++;
		if(resubmit) {
			g_xr.frames.resubmitted++;
			if(g_xr.frames.resubmitted <= 3) {
				LogInfo << "OpenXR frame ended with the previous picture: no new frame was shown"
				        << (g_xr.frames.resubmitted == 3 ? " (not logged again)" : "");
			}
		}
	}

	// The rate / cadence instrument (F-07): one line per window, gaps as they happen
	FrameGap gap;
	bool report = g_xr.pacing.add(frame.timing, &gap);
	if(gap.detected) {
		g_xr.frames.gaps++;
		g_xr.gapsInWindow++;
		if(g_xr.frames.gaps <= MaxLoggedFrameGaps) {
			LogInfo << "[vr] frame gap: " << gap.ms << " ms between frames (mean " << gap.meanMs << " ms), in the "
			        << gap.phase << " phase" << (g_xr.frames.gaps == MaxLoggedFrameGaps ? " (further gaps are only counted)" : "");
		}
	}
	if(report) {
		g_xr.lastReport = g_xr.pacing.report();
		LogInfo << "[vr] rate: " << formatReport(g_xr.lastReport)
		        << (g_xr.gapsInWindow ? ", " + std::to_string(g_xr.gapsInWindow) + " frame gaps" : "");
		g_xr.gapsInWindow = 0;
	}

	return true;
}

/*!
 * Ask the runtime to end the session in order (xrRequestExitSession) and wait, briefly, for it to
 * do so: STOPPING (then xrEndSession, from pollEvents) and EXITING. The runtime then sees a normal
 * exit instead of an application that vanished with its session running.
 */
void requestExit() {

	if(g_xr.session == XR_NULL_HANDLE || !g_xr.running) {
		return;
	}

	if(g_xr.frame.begun) {
		abandonFrameTargets();
		endFrame(true);
	}

	XrResult result = xrRequestExitSession(g_xr.session);
	if(XR_FAILED(result)) {
		LogWarning << "Could not request the OpenXR session exit (" << resultName(result)
		           << "), destroying the session while running";
		return;
	}
	g_xr.exitRequested = true;

	PlatformInstant start = platform::getTime();
	while(!g_xr.exited && g_xr.session != XR_NULL_HANDLE
	      && platform::getTime() - start < PlatformDuration(ExitTimeout)) {
		pollEvents();
		Thread::sleep(std::chrono::milliseconds(1));
	}

	PlatformDuration took = platform::getTime() - start;
	if(g_xr.exited) {
		LogInfo << "OpenXR session exited in order after " << toMsi(took) << " ms";
	} else if(g_xr.session != XR_NULL_HANDLE) {
		LogWarning << "OpenXR session did not reach exiting within " << toMsi(took) << " ms (state "
		           << sessionStateName(g_xr.sessionState) << "), destroying it"
		           << (g_xr.running ? " while running" : "");
	}
}

XrVersion currentOpenGLVersion() {

	GLint major = 0, minor = 0;

	// GL_MAJOR_VERSION / GL_MINOR_VERSION exist since OpenGL 3.0
	while(glGetError() != GL_NO_ERROR) { }
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	if(glGetError() != GL_NO_ERROR || major == 0) {
		major = minor = 0;
		const char * version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
		if(version) {
			std::sscanf(version, "%d.%d", &major, &minor);
		}
	}

	return XR_MAKE_VERSION(major, minor, 0);
}

} // anonymous namespace

bool initialize() {

	arx_assert(g_xr.instance == XR_NULL_HANDLE);

	// Which runtime the loader would use and where that was decided - on every run, flat included
	RuntimeManifest manifest = findRuntimeManifest();
	g_xr.manifest = manifest.path;
	logRuntimeSelection(manifest);

	if(simRequested()) {
		// The simulated headset (#112) never contacts a runtime, so --no-vr - which the launch scripts
		// add while SteamVR is off (D-014) - does not stop it; a broken script means flat, never the runtime
		if(g_disableVR) {
			LogInfo << "[vr] sim: --vr-sim wins over --no-vr: the simulator runs, no OpenXR runtime is used";
		}
		if(!installSimulator()) {
			return false;
		}
		g_xr.manifest = "--vr-sim";
	} else if(g_disableVR) {
		LogInfo << "VR disabled on the command line (--no-vr), starting in flat mode";
		return false;
	} else if(!runtimeIsReady()) {
		return false;
	}

	if(!hasExtension(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
		return false;
	}

	if(!createInstance() || !findSystem()) {
		shutdown();
		return false;
	}

	// Actions and their suggested bindings must exist before the session; VR works without them
	if(!g_xr.actions.create(g_xr.instance)) {
		LogWarning << "[vr] input: no OpenXR actions, the controllers do nothing";
	}

	LogInfo << "Starting in VR";

	return true;
}

bool createSession() {

	if(g_xr.instance == XR_NULL_HANDLE) {
		return false;
	}

	arx_assert(g_xr.session == XR_NULL_HANDLE);

	XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	XrResult result = g_xr.getOpenGLGraphicsRequirements(g_xr.instance, g_xr.systemId, &requirements);
	if(XR_FAILED(result)) {
		LogError << "Could not query OpenXR OpenGL requirements (" << resultName(result) << "), continuing in flat mode";
		shutdown();
		return false;
	}

	XrVersion glVersion = currentOpenGLVersion();
	LogInfo << "OpenXR runtime supports OpenGL " << versionString(requirements.minApiVersionSupported)
	        << " to " << versionString(requirements.maxApiVersionSupported)
	        << ", have OpenGL " << versionString(glVersion);
	if(glVersion < requirements.minApiVersionSupported) {
		LogError << "OpenGL context too old for the OpenXR runtime, continuing in flat mode";
		shutdown();
		return false;
	}
	if(glVersion > requirements.maxApiVersionSupported) {
		LogWarning << "OpenGL context is newer than what the OpenXR runtime was tested with";
	}

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	XrGraphicsBindingOpenGLWin32KHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
	if(!binding.hDC || !binding.hGLRC) {
		LogError << "No current OpenGL context to bind the OpenXR session to, continuing in flat mode";
		shutdown();
		return false;
	}

	XrSessionCreateInfo createInfo = { XR_TYPE_SESSION_CREATE_INFO };
	createInfo.next = &binding;
	createInfo.systemId = g_xr.systemId;
	result = xrCreateSession(g_xr.instance, &createInfo, &g_xr.session);
	if(XR_FAILED(result)) {
		g_xr.session = XR_NULL_HANDLE;
		LogError << "Could not create OpenXR session (" << resultName(result) << "), continuing in flat mode";
		shutdown();
		return false;
	}

	g_xr.sessionState = XR_SESSION_STATE_IDLE;
	LogInfo << "OpenXR session created";

	if(!createSessionResources()) {
		LogError << "Could not set up OpenXR rendering, continuing in flat mode";
		shutdown();
		return false;
	}

	g_xr.actions.attach(g_xr.session);
	g_xr.handTracking.create(g_xr.session);

	return true;

	#else

	LogError << "OpenXR OpenGL binding is not implemented for this platform, continuing in flat mode";
	shutdown();
	return false;

	#endif
}

void pollEvents() {

	if(g_xr.instance == XR_NULL_HANDLE) {
		return;
	}

	for(;;) {

		XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
		XrResult result = xrPollEvent(g_xr.instance, &event);
		if(result == XR_EVENT_UNAVAILABLE) {
			break;
		}
		if(XR_FAILED(result)) {
			LogWarning << "Could not poll OpenXR events: " << resultName(result);
			break;
		}

		switch(event.type) {

			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged & changed
					= *reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
				if(changed.session != g_xr.session) {
					break;
				}
				XrSessionState previous = g_xr.sessionState;
				g_xr.sessionState = changed.state;
				gameevent::Event("vr.session")("state", sessionStateName(changed.state))
					("was", sessionStateName(previous))("meaning", sessionStateMeaning(changed.state));
				platform::flightRecord(platform::RecordKind::State, std::string("vr ") + sessionStateName(changed.state));
				if(previous == XR_SESSION_STATE_FOCUSED) {
					logFocusLost();
				}
				if(changed.state == XR_SESSION_STATE_READY) {
					beginSession();
				} else if(changed.state == XR_SESSION_STATE_STOPPING) {
					if(!g_xr.exitRequested) {
						// The runtime ends the session on its own: the exit report names it if the process follows (#146)
						platform::noteExitCause("OpenXR session stopping: the runtime asks the game to end the session");
					}
					endSession();
				} else if(changed.state == XR_SESSION_STATE_EXITING && g_xr.exitRequested) {
					// Our own exit (requestExit): shutdown destroys the session
					g_xr.exited = true;
				} else if(changed.state == XR_SESSION_STATE_LOSS_PENDING || changed.state == XR_SESSION_STATE_EXITING) {
					const std::string cause = std::string("OpenXR session ended by the runtime (")
					                          + sessionStateName(changed.state) + ")";
					LogWarning << cause << ", continuing in flat mode";
					platform::noteExitCause(cause);
					destroySession();
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				LogWarning << "OpenXR runtime is shutting down (instance loss pending), continuing in flat mode";
				platform::noteExitCause("OpenXR runtime is shutting down (instance loss pending)");
				// No orderly exit from a dying runtime: the session goes first, then the instance
				destroySession();
				shutdown();
				return;
			}

			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				// SteamVR binds the controllers a few frames after the session is focused
				LogInfo << "[vr] input: the runtime reports an interaction profile change";
				g_xr.actions.profilesChanged();
				// Which controllers the runtime binds now ("error:XR_ERROR_ACTIONSET_NOT_ATTACHED" without
				// the action set)
				if(g_xr.session != XR_NULL_HANDLE) {
					gameevent::Event("vr.profile")("left", currentInteractionProfile("/user/hand/left"))
						("right", currentInteractionProfile("/user/hand/right"));
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				// The runtime moves a space - its own recenter, or the play area changed (#28 re-seeds)
				const XrEventDataReferenceSpaceChangePending & change
					= *reinterpret_cast<const XrEventDataReferenceSpaceChangePending *>(&event);
				gameevent::Event("vr.refspace")("space", referenceSpaceName(change.referenceSpaceType))
					("poseValid", change.poseValid != XR_FALSE);
				if(change.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
					// The runtime's own recenter moved the space the head is located in (F-120)
					resetHeadReference("runtime");
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				const XrEventDataEventsLost & lost = *reinterpret_cast<const XrEventDataEventsLost *>(&event);
				LogWarning << "Lost " << lost.lostEventCount << " OpenXR events";
				break;
			}

			default: {
				LogDebug("Ignoring OpenXR event " << int(event.type));
				break;
			}

		}

	}

}

namespace {

/*!
 * The frame start shared by the game loop and the frames shown from outside it; \a targets says
 * what the frame draws into (beginFrameTargets): a game frame the eyes and the quad, the loading
 * screen the quad, a frame already drawn into the window when it is begun nothing.
 */
bool beginFrameFor(const Vec2i & windowSize, FrameTargets targets) {

	if(!g_xr.running) {
		return false;
	}

	ARX_PROFILE_FUNC();

	OpenXRState::Frame & frame = g_xr.frame;

	if(frame.begun) {
		// A frame begun but never shown (a level load that drew no frame): ended with the previous
		// picture, so the compositor is not left waiting for it
		abandonFrameTargets();
		endFrame(true);
	}

	XrResult result;
	PlatformInstant waited;
	{
		// The cap's sleep and xrWaitFrame: the time the headset paces the game (#327's vrwait)
		perf::Scope perfWait(PerfPhase::VrWait);

		// The even-cadence cap (F-26): sleep past the slots to skip, so that xrWaitFrame - the only
		// display clock, a timer of our own slips (F-117) - hands out every n-th slot, not the next one
		if(g_xr.divisor > 1 && frame.haveLastWait && frame.period > 0) {
			s64 skipUs = s64((double(g_xr.divisor) - 0.5) * double(frame.period));
			PlatformInstant deadline = frame.lastWaitReturn + PlatformDuration(std::chrono::microseconds(skipUs));
			PlatformDuration left = deadline - platform::getTime();
			if(left > 0) {
				Thread::sleep(left);
			}
		}

		frame.timing = FrameTiming();
		frame.timing.waitStart = toUs(platform::getTime());

		XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
		frame.state = { XR_TYPE_FRAME_STATE };
		result = xrWaitFrame(g_xr.session, &waitInfo, &frame.state);
		waited = platform::getTime();
	}
	frame.timing.waitEnd = toUs(waited);
	if(XR_FAILED(result)) {
		logFrameFailure("xrWaitFrame", result);
		return false;
	}
	// Missed frames as OpenXR sees them: display slots the predicted display time skipped beyond the
	// expected ones (the performance log, #327)
	if(g_xr.lastDisplayTime != 0 && frame.state.predictedDisplayPeriod > 0
	   && frame.state.predictedDisplayTime > g_xr.lastDisplayTime) {
		XrTime slots = (frame.state.predictedDisplayTime - g_xr.lastDisplayTime + frame.state.predictedDisplayPeriod / 2)
		               / frame.state.predictedDisplayPeriod;
		if(slots > XrTime(g_xr.divisor)) {
			perf::addMissed(u32(slots - XrTime(g_xr.divisor)));
		}
	}
	perf::setDisplay(frame.state.predictedDisplayPeriod / 1000, g_xr.divisor, true);
	g_xr.lastDisplayTime = frame.state.predictedDisplayTime;
	frame.period = frame.state.predictedDisplayPeriod / 1000; // OpenXR nanoseconds
	frame.timing.displayPeriod = frame.period;
	frame.lastWaitReturn = waited;
	frame.haveLastWait = true;

	XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
	result = xrBeginFrame(g_xr.session, &beginInfo);
	if(XR_FAILED(result)) {
		logFrameFailure("xrBeginFrame", result);
		return false;
	}
	if(result == XR_FRAME_DISCARDED) {
		g_xr.frames.discarded++;
		if(g_xr.frames.discarded <= MaxLoggedFrameFailures) {
			LogWarning << "OpenXR runtime discarded the previous frame (xrBeginFrame before its xrEndFrame)"
			           << (g_xr.frames.discarded == MaxLoggedFrameFailures ? " (not logged again)" : "");
		}
	}
	frame.begun = true;

	locateFrameViews(frame.state.predictedDisplayTime);
	// The hands for the same display time in the same space as the eyes: the body is posed with the
	// sample the frame is submitted with (#36, F-21)
	g_xr.actions.locateHands(g_xr.localSpace, frame.state.predictedDisplayTime, frame.hands);
	g_xr.handTracking.locate(g_xr.localSpace, frame.state.predictedDisplayTime, frame.hands);

	beginFrameTargets(windowSize, targets);

	return true;
}

} // anonymous namespace

bool beginFrame(const Vec2i & windowSize) {
	return beginFrameFor(windowSize, FrameTargets::Game);
}

bool beginInterfaceFrame(const Vec2i & windowSize) {
	if(!g_xr.running) {
		return false;
	}
	// Inside the game loop's frame its targets are already bound
	return g_xr.frame.begun || beginFrameFor(windowSize, FrameTargets::Interface);
}

Vec2i interfaceSize(const Vec2i & windowSize) {
	return interfaceTargetSize(windowSize);
}

void submitFrame(const Vec2i & windowSize) {

	if(!g_xr.running) {
		return;
	}

	ARX_PROFILE_FUNC();

	OpenXRState::Frame & frame = g_xr.frame;

	// A frame shown from outside the game loop that did not begin its own (beginInterfaceFrame)
	// begins it now; its picture is already in the window, so it is copied onto the quad as the flat
	// frame of #18 was - while the quad has the window's size
	bool lateFrame = !frame.begun;
	if(lateFrame && !beginFrameFor(windowSize, FrameTargets::Drawn)) {
		return;
	}

	// An eye left bound by a caller that gave up mid-way holds no finished picture
	if(frame.eyeInRender != size_t(-1)) {
		g_xr.eyes[frame.eyeInRender].release();
		frame.eyeInRender = size_t(-1);
	}

	if(!frame.quadAcquired && interfaceTargetSize(windowSize) == windowSize) {
		updateQuadSwapchain(windowSize);
	}

	// Where the 2D interface goes this frame: the floating panel while a menu is open (#49)
	updatePanel();

	// Not visible: the window still shows the flat frame drawn into the quad
	if(!frame.state.shouldRender && frame.quadAcquired) {
		blitToWindow(g_xr.quad, frame.windowSize);
	}

	bool eyes = false;
	bool background = false;
	bool flatFrame = false;

	// Not visible (headset idle, another app in front): frames still keep the timing, without layers
	if(frame.state.shouldRender) {
		GLStateGuard state;
		g_xr.layers.count = 0;
		g_xr.pointer.submitted = false;
		for(OpenXRState::HandReticle & reticle : g_xr.reticles) {
			reticle.submitted = false;
		}
		for(OpenXRState::RadialMenu & radial : g_xr.radials) {
			radial.submitted = false;
		}
		eyes = describeEyeLayer(g_xr.layers);
		if(!eyes) {
			background = renderBackground(g_xr.layers);
		}
		flatFrame = frame.quadAcquired ? finishFlatFrame(g_xr.layers) : copyFlatFrame(g_xr.layers);
	}

	// Whatever happened, the window is the render target again before the buffers are swapped
	abandonFrameTargets();

	u64 shownBefore = g_xr.frames.shown;
	if(!endFrame(false)) {
		return;
	}

	if(eyes) {
		g_xr.stereo.frames++;
	}

	// The desktop mirror (#21) draws its copies of the mirrored eye and the flat frame into the window
	if((eyes || background) && g_xr.mirror.isOn()) {
		GLStateGuard state;
		drawMirror(windowSize, flatFrame);
	}

	if(shownBefore == 0 && g_xr.frames.shown == 1) {
		std::ostringstream oss;
		oss << "First VR frame shown: " << (eyes ? "the scene per eye" : background ? "dark background" : "no background");
		if(flatFrame) {
			const bool world = g_xr.layers.quad.space == g_xr.localSpace;
			const bool screen = g_xr.panel.screen.mode() == PanelMode::World;
			oss << ", flat frame " << g_xr.quad.size().x << 'x' << g_xr.quad.size().y
			    << (!world ? " on a head-locked quad " : screen ? " on the logo / cinematic screen " : " on the floating panel ")
			    << g_xr.layers.quad.size.width << " x " << g_xr.layers.quad.size.height << " m at "
			    << (!world ? QuadDistance : screen ? ScreenDistance : g_xr.panel.params.distance)
			    << " m" << (g_xr.layers.quad.layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
			                ? " (2D interface, blended by its alpha)" : " (a copy of the window)");
		} else {
			oss << ", no flat frame";
		}
		LogInfo << oss.str();
	}

}

bool stereoActive() {
	return g_xr.running && g_xr.frame.begun && g_xr.frame.stereo;
}

const std::array<EyeRig, 2> & eyeRigs() {
	return g_xr.frame.rig;
}

const EyeFov & unionEyeFov() {
	return g_xr.frame.unionFov;
}

Vec2i eyeRenderSize() {
	return g_xr.eyes[0].isValid() ? g_xr.eyes[0].size() : Vec2i(0);
}

bool beginEyeRender(size_t eye) {

	OpenXRState::Frame & frame = g_xr.frame;
	if(!stereoActive() || eye >= g_xr.eyes.size()) {
		return false;
	}
	arx_assert(frame.eyeInRender == size_t(-1));

	Swapchain & swapchain = g_xr.eyes[eye];
	if(!swapchain.isValid() || !swapchain.acquire()) {
		return false;
	}

	frame.eyeInRender = eye;
	bindTarget(swapchain.renderFramebuffer(), swapchain.size(), swapchain.samples());

	return true;
}

void endEyeRender(size_t eye) {

	OpenXRState::Frame & frame = g_xr.frame;
	if(frame.eyeInRender != eye || eye >= g_xr.eyes.size()) {
		return;
	}

	Swapchain & swapchain = g_xr.eyes[eye];
	if(swapchain.samples() > 1) {
		// The multisampled picture reaches the image before anyone reads it (#226)
		GLStateGuard state;
		swapchain.resolve();
	}
	if(eye == g_xr.mirror.eye()) {
		// The desktop mirror (#21) takes its copy while the image is ours
		g_xr.mirror.copyEye(swapchain);
	}
	swapchain.release();
	frame.eyeRendered[eye] = true;
	frame.eyeInRender = size_t(-1);

	// Back to the flat frame for the 2D interface, or to the window when there is no quad
	if(frame.quadAcquired) {
		bindTarget(g_xr.quad.framebuffer(), g_xr.quad.size());
	} else {
		restoreWindowTarget();
	}
}

StereoStatus stereoStatus() {
	StereoStatus status = g_xr.stereo;
	status.active = stereoActive();
	return status;
}

const FrameViews & frameViews() {
	return g_xr.frame.views;
}

const FrameHands & frameHands() {
	return g_xr.frame.hands;
}

bool isEnabled() {
	return g_xr.session != XR_NULL_HANDLE;
}

const char * sessionStateString() {
	if(g_xr.session == XR_NULL_HANDLE) {
		return "none";
	}
	return sessionStateName(g_xr.sessionState);
}

bool isWorn() {
	return g_xr.session != XR_NULL_HANDLE
	       && (g_xr.sessionState == XR_SESSION_STATE_VISIBLE || g_xr.sessionState == XR_SESSION_STATE_FOCUSED);
}

FrameCounters frameCounters() {
	return g_xr.frames;
}

const PacingReport & lastPacingReport() {
	return g_xr.lastReport;
}

u32 pacingReports() {
	return g_xr.pacing.reports();
}

u32 frameDivisor() {
	return g_xr.divisor;
}

MirrorStatus mirrorStatus() {
	MirrorStatus status;
	status.state = g_xr.mirror.state();
	status.eye = g_xr.mirror.eyeName();
	status.frames = g_xr.mirror.frames();
	status.overlays = g_xr.mirror.overlaysDrawn();
	return status;
}

void setMenuOpen(const char * menu) {
	g_xr.panel.menu = menu ? menu : "";
}

void setSceneScreen(bool on) {
	g_xr.panel.sceneScreenWanted = on;
}

bool sceneScreenFrame() {
	return g_xr.panel.sceneScreen;
}

PanelStatus panelStatus() {
	const OpenXRState::Panel & panel = g_xr.panel;
	PanelStatus status;
	// While a logo or a cinematic plays the quad is its screen (#394): mode "screen", its pose and size
	const bool screen = panel.screen.mode() == PanelMode::World;
	status.mode = screen ? "screen" : panelModeName(panel.anchor.mode());
	status.menu = panel.menu;
	status.placements = panel.anchor.placements();
	status.lastPlacement = panelPlacementName(panel.lastPlacement);
	const PanelPose & pose = screen ? panel.screen.pose() : panel.anchor.pose();
	const Vec3f heading = pose.orientation * Vec3f(0.f, 0.f, -1.f);
	const Vec3f face = pose.orientation * Vec3f(0.f, 0.f, 1.f);
	status.position = pose.position;
	status.yawDeg = glm::degrees(std::atan2(-heading.x, -heading.z));
	status.tiltDeg = glm::degrees(std::asin(std::clamp(face.y, -1.f, 1.f)));
	status.size = panel.size;
	status.distance = screen ? ScreenDistance : panel.params.distance;
	status.drop = screen ? ScreenDrop : panel.params.drop;
	return status;
}

bool panelHit(const Vec3f & origin, const Vec3f & direction, Vec2f & pixel, Vec3f * point) {
	const OpenXRState::Panel & panel = g_xr.panel;
	if(!g_xr.running || panel.anchor.mode() != PanelMode::World || !g_xr.quad.isValid()) {
		return false;
	}
	Vec2f uv(0.f);
	float distance = 0.f;
	if(!vr::panelHit(panel.anchor.pose(), panelSize(panel.params.width, g_xr.quad.size()), origin, direction,
	                 uv, distance)) {
		return false;
	}
	pixel = uv * Vec2f(g_xr.quad.size());
	if(point) {
		*point = origin + direction * distance;
	}
	return true;
}

bool panelShown() {
	return g_xr.running && g_xr.panel.anchor.mode() == PanelMode::World && g_xr.quad.isValid();
}

bool panelPoint(const Vec2f & pixel, Vec3f & point) {
	const OpenXRState::Panel & panel = g_xr.panel;
	if(!g_xr.running || panel.anchor.mode() != PanelMode::World || !g_xr.quad.isValid()) {
		return false;
	}
	point = vr::panelPoint(panel.anchor.pose(), panelSize(panel.params.width, g_xr.quad.size()),
	                       pixel / Vec2f(g_xr.quad.size()));
	return true;
}

std::array<HandAim, HandCount> handAims() {
	std::array<HandAim, HandCount> aims;
	if(!g_xr.running) {
		return aims;
	}
	const std::array<ActionSet::AimPose, HandCount> poses = g_xr.actions.locateAim(g_xr.localSpace, g_xr.lastDisplayTime);
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(poses[hand].valid) {
			aims[hand].valid = true;
			aims[hand].origin = poses[hand].position;
			aims[hand].direction = glm::normalize(poses[hand].orientation * Vec3f(0.f, 0.f, -1.f));
		}
	}
	return aims;
}

void setPointerBeam(bool shown, const Vec3f & from, const Vec3f & to) {
	g_xr.pointer.requested = shown;
	g_xr.pointer.from = from;
	g_xr.pointer.to = to;
}

bool pointerBeamShown() {
	return g_xr.pointer.submitted;
}

void setHandReticle(Hand hand, bool shown, const Vec3f & hit, bool bright) {
	OpenXRState::HandReticle & reticle = g_xr.reticles[size_t(hand)];
	reticle.requested = shown;
	reticle.hit = hit;
	reticle.bright = bright;
}

bool handReticleShown(Hand hand) {
	return g_xr.reticles[size_t(hand)].submitted;
}

bool beginRadialImage(Hand hand) {

	OpenXRState::Frame & frame = g_xr.frame;
	OpenXRState::RadialMenu & radial = g_xr.radials[size_t(hand)];
	radial.drawn = false;
	// The images are made with the session (createSwapchains), never in a frame
	if(!stereoActive() || frame.eyeInRender != size_t(-1) || !radial.image.isValid()) {
		radial.refusal = !stereoActive() ? (frame.begun ? "not a stereo frame" : "no frame begun")
		                 : frame.eyeInRender != size_t(-1) ? "an eye in render" : "no image";
		return false;
	}
	if(!radial.image.acquire()) {
		radial.refusal = "the image acquire failed";
		return false;
	}
	radial.refusal = "";

	bindTarget(radial.image.framebuffer(), radial.image.size());
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	const GLfloat transparent[4] = { 0.f, 0.f, 0.f, 0.f };
	const GLfloat depth = 1.f;
	glClearBufferfv(GL_COLOR, 0, transparent);
	glClearBufferfv(GL_DEPTH, 0, &depth);
	if(scissor) {
		glEnable(GL_SCISSOR_TEST);
	}
	radial.acquired = true;

	return true;
}

void endRadialImage(Hand hand) {

	OpenXRState::RadialMenu & radial = g_xr.radials[size_t(hand)];
	if(!radial.acquired) {
		return;
	}
	radial.image.release();
	radial.acquired = false;
	radial.drawn = true;

	// Back to the flat frame for the 2D interface, or to the window when there is no quad
	if(g_xr.frame.quadAcquired) {
		bindTarget(g_xr.quad.framebuffer(), g_xr.quad.size());
	} else {
		restoreWindowTarget();
	}
}

void setRadialLayer(Hand hand, bool shown, const Vec3f & center, const glm::quat & orientation, float size) {
	OpenXRState::RadialMenu & radial = g_xr.radials[size_t(hand)];
	radial.requested = shown;
	radial.center = center;
	radial.orientation = orientation;
	radial.size = size;
}

bool radialLayerShown(Hand hand) {
	return g_xr.radials[size_t(hand)].submitted;
}

const char * radialImageRefusal(Hand hand) {
	return g_xr.radials[size_t(hand)].refusal;
}

bool radialImagesMade() {
	return g_xr.radials[0].image.isValid() && g_xr.radials[1].image.isValid();
}

bool beginSubtitleImage() {

	OpenXRState::SubtitlePanel & panel = g_xr.subtitle;
	if(!stereoActive() || g_xr.frame.eyeInRender != size_t(-1) || !panel.texture || !panel.framebuffer) {
		return false;
	}
	bindTarget(panel.framebuffer, panel.texture->getSize());
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	const GLfloat transparent[4] = { 0.f, 0.f, 0.f, 0.f };
	glClearBufferfv(GL_COLOR, 0, transparent);
	if(scissor) {
		glEnable(GL_SCISSOR_TEST);
	}
	panel.acquired = true;
	return true;
}

void endSubtitleImage() {

	OpenXRState::SubtitlePanel & panel = g_xr.subtitle;
	if(!panel.acquired) {
		return;
	}
	panel.acquired = false;
	if(g_xr.frame.quadAcquired) {
		bindTarget(g_xr.quad.framebuffer(), g_xr.quad.size());
	} else {
		restoreWindowTarget();
	}
}

Texture * subtitleTexture() {
	return g_xr.subtitle.framebuffer ? g_xr.subtitle.texture : nullptr;
}
void recenterPanel() {
	g_xr.panel.anchor.recenter();
	g_xr.panel.screen.recenter();
}

std::string runtimeName() {
	return g_xr.runtime;
}

std::string runtimeManifest() {
	return g_xr.manifest;
}

bool readControllers(ControllerInput & input) {
	if(!g_xr.running || !g_xr.actions.isAttached()) {
		input = ControllerInput();
		return false;
	}
	return g_xr.actions.sync(input, g_xr.viewSpace, g_xr.lastDisplayTime);
}

std::array<std::string, HandCount> controllerProfiles() {
	return g_xr.actions.profiles();
}

u64 controllerSyncs() {
	return g_xr.actions.syncs();
}

const HandJointsStatus & handJointsStatus() {
	return g_xr.handTracking.status();
}

void shutdown() {

	requestExit();
	destroySession();
	g_xr.actions.destroy();
	g_xr.handTracking.unbind();

	if(g_xr.instance != XR_NULL_HANDLE) {
		xrDestroyInstance(g_xr.instance);
		g_xr.instance = XR_NULL_HANDLE;
		LogInfo << "OpenXR instance destroyed";
	}

	g_xr.systemId = XR_NULL_SYSTEM_ID;
	g_xr.getOpenGLGraphicsRequirements = nullptr;
}

#else // !ARX_HAVE_OPENXR

bool isSupported() {
	return false;
}

bool initialize() {
	if(!g_disableVR) {
		LogInfo << "Built without OpenXR support, starting in flat mode";
	}
	return false;
}

bool createSession() {
	return false;
}

void pollEvents() { }

bool beginFrame(const Vec2i & /* windowSize */) {
	return false;
}

void submitFrame(const Vec2i & /* windowSize */) { }

bool beginInterfaceFrame(const Vec2i & /* windowSize */) {
	return false;
}

Vec2i interfaceSize(const Vec2i & windowSize) {
	return windowSize;
}

bool stereoActive() {
	return false;
}

const std::array<EyeRig, 2> & eyeRigs() {
	static const std::array<EyeRig, 2> none;
	return none;
}

const EyeFov & unionEyeFov() {
	static const EyeFov none;
	return none;
}

Vec2i eyeRenderSize() {
	return Vec2i(0);
}

bool beginEyeRender(size_t /* eye */) {
	return false;
}

void endEyeRender(size_t /* eye */) { }

StereoStatus stereoStatus() {
	return StereoStatus();
}

const FrameViews & frameViews() {
	static const FrameViews none;
	return none;
}

const FrameHands & frameHands() {
	static const FrameHands none;
	return none;
}

const char * sessionStateString() {
	return "none";
}

bool isWorn() {
	return false;
}

const PacingReport & lastPacingReport() {
	static const PacingReport none;
	return none;
}

u32 pacingReports() {
	return 0;
}

u32 frameDivisor() {
	return 1;
}

MirrorStatus mirrorStatus() {
	return MirrorStatus();
}

void setMenuOpen(const char * /* menu */) { }

void setSceneScreen(bool /* on */) { }

bool sceneScreenFrame() {
	return false;
}

PanelStatus panelStatus() {
	return PanelStatus();
}

bool panelHit(const Vec3f & /* origin */, const Vec3f & /* direction */, Vec2f & /* pixel */, Vec3f * /* point */) {
	return false;
}

bool panelShown() {
	return false;
}

bool panelPoint(const Vec2f & /* pixel */, Vec3f & /* point */) {
	return false;
}

std::array<HandAim, HandCount> handAims() {
	return std::array<HandAim, HandCount>();
}

void setPointerBeam(bool /* shown */, const Vec3f & /* from */, const Vec3f & /* to */) { }

bool pointerBeamShown() {
	return false;
}

void setHandReticle(Hand /* hand */, bool /* shown */, const Vec3f & /* hit */, bool /* bright */) { }

bool handReticleShown(Hand /* hand */) {
	return false;
}

bool beginRadialImage(Hand /* hand */) {
	return false;
}

bool radialImagesMade() {
	return false;
}

void endRadialImage(Hand /* hand */) { }

void setRadialLayer(Hand /* hand */, bool /* shown */, const Vec3f & /* center */, const glm::quat & /* orientation */,
                    float /* size */) { }

bool radialLayerShown(Hand /* hand */) {
	return false;
}

bool beginSubtitleImage() {
	return false;
}

void endSubtitleImage() { }

Texture * subtitleTexture() {
	return nullptr;
}

const char * radialImageRefusal(Hand /* hand */) {
	return "no VR";
}

void recenterPanel() { }

std::string runtimeName() {
	return std::string();
}

std::string runtimeManifest() {
	return std::string();
}

bool readControllers(ControllerInput & input) {
	input = ControllerInput();
	return false;
}

std::array<std::string, HandCount> controllerProfiles() {
	return { };
}

u64 controllerSyncs() {
	return 0;
}

const HandJointsStatus & handJointsStatus() {
	static const HandJointsStatus none;
	return none;
}

bool isEnabled() {
	return false;
}

FrameCounters frameCounters() {
	return FrameCounters();
}

void shutdown() { }

#endif // !ARX_HAVE_OPENXR

} // namespace vr
