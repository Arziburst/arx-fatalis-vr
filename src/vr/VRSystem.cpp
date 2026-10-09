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

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>

#include "platform/Platform.h"
#include "Configure.h"

#include "core/Version.h"
#include "gui/Credits.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/log/Logger.h"
#include "platform/CrashHandler.h"
#include "platform/ProgramOptions.h"
#include "platform/profiler/Profiler.h"
#include "util/String.h"

#if ARX_HAVE_OPENXR

// OpenXR with the OpenGL binding (and windows.h on Windows)
#include "vr/OpenXRUtil.h"
#include "vr/VRActions.h"
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

#if ARX_HAVE_OPENXR

bool isSupported() {
	return true;
}

namespace {

//! The head-locked quad that shows the flat frame (F-20): its width and distance in meters
const float QuadWidth = 2.4f;
const float QuadDistance = 1.75f;

//! Uniform background of the eye swapchains, sRGB-encoded: dark grey, so it is told apart from "no layer"
const GLfloat BackgroundColor[4] = { 0.15f, 0.15f, 0.15f, 1.f };

//! Frame loop failures logged per session before it goes quiet (they repeat every frame)
const u32 MaxLoggedFrameFailures = 5;

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

	XrSpace viewSpace = XR_NULL_HANDLE;
	s64 colorFormat = 0;
	std::array<Swapchain, 2> eyes;
	Swapchain quad;
	Vec2i quadFailedSize = Vec2i(0); //!< a window size the quad swapchain could not be created for

	FrameCounters frames;
	u32 frameFailures = 0;
	bool loggedInvalidViews = false;
	//! The display time of the last frame - for locating the controllers outside the frame (0: none yet)
	XrTime lastDisplayTime = 0;

	//! One action per physical control (#30); created with the instance, attached to the session
	ActionSet actions;

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

bool hasExtension(const char * name) {

	uint32_t count = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
	if(XR_FAILED(result)) {
		LogWarning << "OpenXR runtime unavailable (" << resultName(result) << "), starting in flat mode";
		return false;
	}

	std::vector<XrExtensionProperties> extensions(count, { XR_TYPE_EXTENSION_PROPERTIES });
	result = xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data());
	if(XR_FAILED(result)) {
		LogWarning << "Could not enumerate OpenXR extensions (" << resultName(result) << "), starting in flat mode";
		return false;
	}

	for(size_t i = 0; i < count; i++) {
		if(std::strcmp(extensions[i].extensionName, name) == 0) {
			return true;
		}
	}

	LogWarning << "OpenXR runtime does not support " << name << ", starting in flat mode";
	return false;
}

bool createInstance() {

	const char * extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

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
	createInfo.enabledExtensionCount = uint32_t(std::size(extensions));
	createInfo.enabledExtensionNames = extensions;

	XrResult result = xrCreateInstance(&createInfo, &g_xr.instance);
	if(XR_FAILED(result)) {
		g_xr.instance = XR_NULL_HANDLE;
		LogWarning << "Could not create OpenXR instance (" << resultName(result) << "), starting in flat mode";
		return false;
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
	result = xrGetSystemProperties(g_xr.instance, g_xr.systemId, &properties);
	if(XR_SUCCEEDED(result)) {
		LogInfo << "VR headset: " << util::loadString(properties.systemName)
		        << " (vendor " << properties.vendorId << ")"
		        << (properties.trackingProperties.positionTracking ? ", positional tracking" : ", orientation tracking only")
		        << ", max swapchain " << properties.graphicsProperties.maxSwapchainImageWidth
		        << 'x' << properties.graphicsProperties.maxSwapchainImageHeight
		        << ", " << properties.graphicsProperties.maxLayerCount << " layers";
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

void destroySession() {

	// The swapchains' OpenGL objects go first, while the context the session is bound to is current
	for(Swapchain & eye : g_xr.eyes) {
		eye.destroy();
	}
	g_xr.quad.destroy();
	g_xr.quadFailedSize = Vec2i(0);
	g_xr.colorFormat = 0;

	g_xr.actions.detach();
	g_xr.lastDisplayTime = 0;

	if(g_xr.viewSpace != XR_NULL_HANDLE) {
		xrDestroySpace(g_xr.viewSpace);
		g_xr.viewSpace = XR_NULL_HANDLE;
	}

	if(g_xr.session != XR_NULL_HANDLE) {
		xrDestroySession(g_xr.session);
		g_xr.session = XR_NULL_HANDLE;
		LogInfo << "OpenXR session destroyed" << (g_xr.running ? " while running" : "")
		        << " after " << g_xr.frames.submitted << " frames (" << g_xr.frames.shown << " shown)";
	}
	g_xr.sessionState = XR_SESSION_STATE_UNKNOWN;
	g_xr.running = false;
}

/*!
 * The swapchains and the space the frames are submitted in; the per-eye swapchains are sized from
 * the runtime's recommended view sizes and get a framebuffer object per image to render into.
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

	g_xr.colorFormat = chooseSwapchainFormat(g_xr.session);
	if(!g_xr.colorFormat) {
		return false;
	}

	const char * const names[] = { "left eye", "right eye" };
	for(size_t i = 0; i < g_xr.eyes.size(); i++) {
		Vec2i size(s32(g_xr.views[i].recommendedImageRectWidth), s32(g_xr.views[i].recommendedImageRectHeight));
		if(!g_xr.eyes[i].create(g_xr.session, g_xr.colorFormat, size, true, names[i])) {
			return false;
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
	LogInfo << "OpenXR session running, submitting frames";
}

void endSession() {

	if(!g_xr.running) {
		return;
	}

	g_xr.running = false;

	XrResult result = xrEndSession(g_xr.session);
	if(XR_FAILED(result)) {
		LogWarning << "Could not end the OpenXR session: " << resultName(result);
		return;
	}

	LogInfo << "OpenXR session stopped after " << g_xr.frames.submitted << " frames ("
	        << g_xr.frames.shown << " shown)";
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
 * The quad shows the back buffer 1:1, so its swapchain follows the window size. A size the
 * swapchain could not be created for is not retried every frame.
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
		LogWarning << "Window " << size.x << 'x' << size.y << " is larger than the biggest OpenXR swapchain "
		           << g_xr.maxSwapchainSize.x << 'x' << g_xr.maxSwapchainSize.y << ", the flat frame is not shown in VR";
		g_xr.quadFailedSize = size;
		return;
	}

	if(!g_xr.quad.create(g_xr.session, g_xr.colorFormat, size, false, "flat frame quad")) {
		g_xr.quadFailedSize = size;
	}
}

/*!
 * Clear both eye images to the background color through their framebuffers and describe them as a
 * projection layer with this frame's eye poses (relative to the head: the background is head-locked).
 */
bool renderBackground(XrTime displayTime, std::array<XrCompositionLayerProjectionView, 2> & views,
                      XrCompositionLayerProjection & layer) {

	XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = displayTime;
	locateInfo.space = g_xr.viewSpace;
	XrViewState viewState = { XR_TYPE_VIEW_STATE };
	std::array<XrView, 2> located;
	located.fill({ XR_TYPE_VIEW });
	uint32_t count = 0;
	XrResult result = xrLocateViews(g_xr.session, &locateInfo, &viewState, u32(located.size()), &count,
	                                located.data());
	if(XR_FAILED(result)) {
		logFrameFailure("xrLocateViews", result);
		return false;
	}

	const XrViewStateFlags valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
	if(count != located.size() || (viewState.viewStateFlags & valid) != valid) {
		if(!g_xr.loggedInvalidViews) {
			LogWarning << "OpenXR eye poses not available (" << count << " views, flags 0x" << std::hex
			           << viewState.viewStateFlags << std::dec << "), the background layer is left out";
			g_xr.loggedInvalidViews = true;
		}
		return false;
	}

	for(size_t eye = 0; eye < views.size(); eye++) {

		Swapchain & swapchain = g_xr.eyes[eye];
		if(!swapchain.acquire()) {
			return false;
		}
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, swapchain.framebuffer());
		glClearBufferfv(GL_COLOR, 0, BackgroundColor);
		swapchain.release();

		views[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		views[eye].pose = located[eye].pose;
		views[eye].fov = located[eye].fov;
		views[eye].subImage.swapchain = swapchain.handle();
		views[eye].subImage.imageRect = swapchain.rect();
		views[eye].subImage.imageArrayIndex = 0;

	}

	layer.layerFlags = 0;
	layer.space = g_xr.viewSpace;
	layer.viewCount = u32(views.size());
	layer.views = views.data();

	return true;
}

/*!
 * Copy the window's back buffer into the quad swapchain and describe the head-locked quad.
 *
 * A copy from the default framebuffer, not a blit: the window is usually multisampled, and a
 * resolving blit requires identical source and destination formats (RGB8 vs. SRGB8_ALPHA8 here),
 * while glCopyTexSubImage2D resolves the default framebuffer and stores the values unchanged.
 */
bool copyFlatFrame(XrCompositionLayerQuad & layer) {

	Swapchain & swapchain = g_xr.quad;
	if(!swapchain.isValid() || !swapchain.acquire()) {
		return false;
	}

	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glReadBuffer(GL_BACK);
	glBindTexture(GL_TEXTURE_2D, swapchain.texture());
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, swapchain.size().x, swapchain.size().y);
	swapchain.release();

	layer.layerFlags = 0;
	layer.space = g_xr.viewSpace;
	layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	layer.subImage.swapchain = swapchain.handle();
	layer.subImage.imageRect = swapchain.rect();
	layer.subImage.imageArrayIndex = 0;
	layer.pose.orientation.x = 0.f;
	layer.pose.orientation.y = 0.f;
	layer.pose.orientation.z = 0.f;
	layer.pose.orientation.w = 1.f;
	layer.pose.position.x = 0.f;
	layer.pose.position.y = 0.f;
	layer.pose.position.z = -QuadDistance;
	layer.size.width = QuadWidth;
	layer.size.height = QuadWidth * float(swapchain.size().y) / float(swapchain.size().x);

	return true;
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

	if(g_disableVR) {
		LogInfo << "VR disabled on the command line (--no-vr), starting in flat mode";
		return false;
	}

	if(!runtimeIsReady()) {
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
				g_xr.sessionState = changed.state;
				LogInfo << "OpenXR session state: " << sessionStateName(changed.state);
				if(changed.state == XR_SESSION_STATE_READY) {
					beginSession();
				} else if(changed.state == XR_SESSION_STATE_STOPPING) {
					endSession();
				} else if(changed.state == XR_SESSION_STATE_LOSS_PENDING || changed.state == XR_SESSION_STATE_EXITING) {
					LogWarning << "OpenXR session ended by the runtime, continuing in flat mode";
					destroySession();
				}
				break;
			}

			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				LogWarning << "OpenXR runtime is shutting down, continuing in flat mode";
				shutdown();
				return;
			}

			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				// SteamVR binds the controllers a few frames after the session is focused
				LogInfo << "[vr] input: the runtime reports an interaction profile change";
				g_xr.actions.profilesChanged();
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

void submitFrame(const Vec2i & windowSize) {

	if(!g_xr.running) {
		return;
	}

	ARX_PROFILE_FUNC();

	updateQuadSwapchain(windowSize);

	XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState = { XR_TYPE_FRAME_STATE };
	XrResult result = xrWaitFrame(g_xr.session, &waitInfo, &frameState);
	if(XR_FAILED(result)) {
		logFrameFailure("xrWaitFrame", result);
		return;
	}
	g_xr.lastDisplayTime = frameState.predictedDisplayTime;

	XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
	result = xrBeginFrame(g_xr.session, &beginInfo);
	if(XR_FAILED(result)) {
		logFrameFailure("xrBeginFrame", result);
		return;
	}

	std::array<XrCompositionLayerProjectionView, 2> projectionViews;
	XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	std::array<const XrCompositionLayerBaseHeader *, 2> layers = { };
	u32 layerCount = 0;
	bool background = false;
	bool flatFrame = false;

	// Not visible (headset idle, another app in front): frames still keep the timing, without layers
	if(frameState.shouldRender) {
		GLStateGuard state;
		background = renderBackground(frameState.predictedDisplayTime, projectionViews, projection);
		if(background) {
			layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projection);
		}
		flatFrame = copyFlatFrame(quad);
		if(flatFrame) {
			layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad);
		}
	}

	XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layerCount;
	endInfo.layers = layerCount ? layers.data() : nullptr;
	result = xrEndFrame(g_xr.session, &endInfo);
	if(XR_FAILED(result)) {
		logFrameFailure("xrEndFrame", result);
		return;
	}

	g_xr.frames.submitted++;
	if(g_xr.frames.submitted == 1) {
		LogInfo << "First OpenXR frame submitted, headset display period "
		        << float(frameState.predictedDisplayPeriod) / 1e6f << " ms";
	}

	if(layerCount) {
		g_xr.frames.shown++;
		if(g_xr.frames.shown == 1 && flatFrame) {
			LogInfo << "First VR frame shown: " << (background ? "dark background, " : "no background, ")
			        << "flat frame " << g_xr.quad.size().x << 'x' << g_xr.quad.size().y << " on a head-locked quad "
			        << quad.size.width << " x " << quad.size.height << " m at " << QuadDistance << " m";
		} else if(g_xr.frames.shown == 1) {
			LogInfo << "First VR frame shown: dark background only, no flat frame";
		}
	}

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

FrameCounters frameCounters() {
	return g_xr.frames;
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

void shutdown() {

	destroySession();
	g_xr.actions.destroy();

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

void submitFrame(const Vec2i & /* windowSize */) { }

const char * sessionStateString() {
	return "none";
}

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

bool isEnabled() {
	return false;
}

FrameCounters frameCounters() {
	return FrameCounters();
}

void shutdown() { }

#endif // !ARX_HAVE_OPENXR

} // namespace vr
