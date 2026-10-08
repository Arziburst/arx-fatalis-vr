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
#include "io/log/Logger.h"
#include "platform/CrashHandler.h"
#include "platform/ProgramOptions.h"
#include "util/String.h"

#if ARX_HAVE_OPENXR

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
// The OpenGL binding for OpenXR on Windows is the window's device context and WGL context
#include <windows.h>
#include <tlhelp32.h>
#include "platform/WindowsUtils.h"
#define XR_USE_PLATFORM_WIN32
#endif

// OpenGL headers (GLEW) for querying the context version
#include "graphics/opengl/OpenGLUtil.h"

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

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

struct OpenXRState {

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

	PFN_xrGetOpenGLGraphicsRequirementsKHR getOpenGLGraphicsRequirements = nullptr;

};

OpenXRState g_xr;

std::string resultName(XrResult result) {
	if(g_xr.instance != XR_NULL_HANDLE) {
		char buffer[XR_MAX_RESULT_STRING_SIZE];
		if(XR_SUCCEEDED(xrResultToString(g_xr.instance, result, buffer))) {
			return buffer;
		}
	}
	return "XrResult(" + std::to_string(int(result)) + ")";
}

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

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

/*!
 * Path of the active OpenXR runtime manifest, the same way the loader finds it:
 * the XR_RUNTIME_JSON environment variable first, then the registry.
 */
std::string activeRuntimeManifest() {

	if(const char * env = std::getenv("XR_RUNTIME_JSON")) {
		if(*env) {
			return env;
		}
	}

	const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for(HKEY root : roots) {
		WCHAR buffer[2 * MAX_PATH];
		DWORD size = sizeof(buffer);
		LSTATUS status = RegGetValueW(root, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime",
		                              RRF_RT_REG_SZ, nullptr, buffer, &size);
		if(status == ERROR_SUCCESS) {
			return platform::WideString::toUTF8(buffer);
		}
	}

	return std::string();
}

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

	std::string manifest = activeRuntimeManifest();
	if(manifest.empty()) {
		LogInfo << "No OpenXR runtime is installed, starting in flat mode";
		return false;
	}
	LogDebug("Active OpenXR runtime: " << manifest);

	if(boost::icontains(manifest, "steamvr") && !isProcessRunning(L"vrserver.exe")) {
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
	LogInfo << "Using OpenXR runtime " << runtime;
	CrashHandler::setVariable("OpenXR runtime", runtime);
	credits::setLibraryCredits("vr", "OpenXR (" + runtime + ")");

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

	count = 0;
	result = xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.systemId,
	                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr);
	std::vector<XrViewConfigurationView> views(count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	if(XR_SUCCEEDED(result)) {
		result = xrEnumerateViewConfigurationViews(g_xr.instance, g_xr.systemId,
		                                           XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data());
	}
	if(XR_SUCCEEDED(result)) {
		for(size_t i = 0; i < count; i++) {
			LogInfo << "VR eye " << i << ": recommended " << views[i].recommendedImageRectWidth
			        << 'x' << views[i].recommendedImageRectHeight
			        << " (max " << views[i].maxImageRectWidth << 'x' << views[i].maxImageRectHeight << ")"
			        << ", " << views[i].recommendedSwapchainSampleCount << "x MSAA recommended";
		}
	} else {
		LogWarning << "Could not query OpenXR view sizes: " << resultName(result);
	}

	return true;
}

void destroySession() {
	if(g_xr.session != XR_NULL_HANDLE) {
		xrDestroySession(g_xr.session);
		g_xr.session = XR_NULL_HANDLE;
		LogInfo << "OpenXR session destroyed";
	}
	g_xr.sessionState = XR_SESSION_STATE_UNKNOWN;
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
				// TODO(#20) begin the session on ready and run the frame loop; end it on stopping
				if(changed.state == XR_SESSION_STATE_LOSS_PENDING || changed.state == XR_SESSION_STATE_EXITING) {
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

bool isEnabled() {
	return g_xr.session != XR_NULL_HANDLE;
}

const char * sessionStateString() {
	if(g_xr.session == XR_NULL_HANDLE) {
		return "none";
	}
	return sessionStateName(g_xr.sessionState);
}

void shutdown() {

	destroySession();

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

const char * sessionStateString() {
	return "none";
}

bool isEnabled() {
	return false;
}

void shutdown() { }

#endif // !ARX_HAVE_OPENXR

} // namespace vr
