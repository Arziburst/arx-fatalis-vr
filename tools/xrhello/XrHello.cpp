/*
 * xr_hello - OpenXR runtime probe (64-bit, OpenGL)
 *
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * Adapted from the xr_hello32 probe of Dishonored VR
 * (src/tools/xr_hello32/main.cpp, https://github.com/VR-Stereo-Hub/Dishonored-VR),
 * MIT License, Copyright (c) 2026 bioshock-vr contributors - see LICENSE.xr_hello.
 * Changes: 64-bit, an OpenGL (WGL) binding instead of D3D11, a guard that does not start
 * SteamVR, every session state printed with a timestamp, an orderly session exit, options.
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

/*
 * Answers "is the OpenXR runtime the problem, or the game?" without the game: extensions,
 * instance, system, OpenGL requirements, session, N empty frames, orderly exit. Every session
 * state is printed with a timestamp, so it also measures what the runtime does with an idle
 * headset (does the session reach FOCUSED with nobody wearing it?).
 *
 * Exit codes:
 *   0  full pass - the session ran N frames
 *   1  loader, runtime or API failure
 *   2  partial - the runtime answers but there is no headset, SteamVR is not running,
 *      or the session never became READY (headset idle / standby)
 *
 * Options:
 *   --frames N          frames to submit once the session runs (default 60)
 *   --timeout SECONDS   how long to wait for READY, for the frames and for the exit (default 15)
 *   --start-runtime     allow xrCreateInstance to start SteamVR when it is not running
 *   --require-focused   exit 2 unless the session reached FOCUSED
 *   --no-session        stop after the system query (no OpenGL context, no session)
 */

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <windows.h>
#include <unknwn.h> // IUnknown, used by openxr_platform.h and left out by WIN32_LEAN_AND_MEAN
#include <tlhelp32.h>
#include <GL/gl.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

XrInstance g_instance = XR_NULL_HANDLE;
ULONGLONG g_start = 0;

// Milliseconds from the performance counter (GetTickCount64 is not declared with the build's _WIN32_WINNT)
ULONGLONG nowMs() {
	static LARGE_INTEGER frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return ULONGLONG(counter.QuadPart) * 1000 / ULONGLONG(frequency.QuadPart);
}

const char * res(XrResult r) {
	static char buffer[XR_MAX_RESULT_STRING_SIZE];
	if(g_instance != XR_NULL_HANDLE && xrResultToString(g_instance, r, buffer) == XR_SUCCESS) {
		return buffer;
	}
	std::snprintf(buffer, sizeof(buffer), "XrResult(%d)", int(r));
	return buffer;
}

const char * stateName(XrSessionState state) {
	switch(state) {
		case XR_SESSION_STATE_UNKNOWN:      return "UNKNOWN";
		case XR_SESSION_STATE_IDLE:         return "IDLE";
		case XR_SESSION_STATE_READY:        return "READY";
		case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
		case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
		case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
		case XR_SESSION_STATE_STOPPING:     return "STOPPING";
		case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
		case XR_SESSION_STATE_EXITING:      return "EXITING";
		default:                            return "(invalid)";
	}
}

double seconds() {
	return double(nowMs() - g_start) / 1000.0;
}

void stamp() {
	std::printf("[%7.3f] ", seconds());
}

// The same lookup the loader does: XR_RUNTIME_JSON first, then the registry
std::string activeRuntimeManifest() {
	if(const char * env = std::getenv("XR_RUNTIME_JSON")) {
		if(*env) {
			return env;
		}
	}
	const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
	for(HKEY root : roots) {
		char buffer[2 * MAX_PATH];
		DWORD size = sizeof(buffer);
		if(RegGetValueA(root, "SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", RRF_RT_REG_SZ,
		                nullptr, buffer, &size) == ERROR_SUCCESS) {
			return buffer;
		}
	}
	return std::string();
}

bool isProcessRunning(const wchar_t * executable) {
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

bool icontains(const std::string & haystack, const char * needle) {
	std::string lower = haystack;
	for(char & c : lower) {
		c = char(std::tolower(static_cast<unsigned char>(c)));
	}
	return lower.find(needle) != std::string::npos;
}

// A hidden window with a legacy (compatibility profile) OpenGL context - enough for the binding
struct GLContext {

	HWND window = nullptr;
	HDC dc = nullptr;
	HGLRC glrc = nullptr;

	bool create() {

		WNDCLASSW wc = { };
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"xr_hello_gl";
		wc.style = CS_OWNDC;
		if(!RegisterClassW(&wc)) {
			std::printf("FAIL: RegisterClassW: %lu\n", GetLastError());
			return false;
		}

		window = CreateWindowExW(0, wc.lpszClassName, L"xr_hello", WS_OVERLAPPEDWINDOW,
		                         CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
		if(!window) {
			std::printf("FAIL: CreateWindowExW: %lu\n", GetLastError());
			return false;
		}

		dc = GetDC(window);
		PIXELFORMATDESCRIPTOR pfd = { };
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cDepthBits = 24;
		pfd.cStencilBits = 8;
		pfd.iLayerType = PFD_MAIN_PLANE;
		int format = ChoosePixelFormat(dc, &pfd);
		if(!format || !SetPixelFormat(dc, format, &pfd)) {
			std::printf("FAIL: no OpenGL pixel format: %lu\n", GetLastError());
			return false;
		}

		glrc = wglCreateContext(dc);
		if(!glrc || !wglMakeCurrent(dc, glrc)) {
			std::printf("FAIL: wglCreateContext / wglMakeCurrent: %lu\n", GetLastError());
			return false;
		}

		return true;
	}

	void destroy() {
		if(glrc) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(glrc);
			glrc = nullptr;
		}
		if(window) {
			if(dc) {
				ReleaseDC(window, dc);
				dc = nullptr;
			}
			DestroyWindow(window);
			window = nullptr;
		}
	}

};

struct Options {
	int frames = 60;
	int timeout = 15;
	bool startRuntime = false;
	bool requireFocused = false;
	bool noSession = false;
};

bool parseOptions(int argc, char ** argv, Options & options) {
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if(arg == "--frames" && i + 1 < argc) {
			options.frames = std::atoi(argv[++i]);
		} else if(arg == "--timeout" && i + 1 < argc) {
			options.timeout = std::atoi(argv[++i]);
		} else if(arg == "--start-runtime") {
			options.startRuntime = true;
		} else if(arg == "--require-focused") {
			options.requireFocused = true;
		} else if(arg == "--no-session") {
			options.noSession = true;
		} else {
			std::printf("usage: xr_hello [--frames N] [--timeout SECONDS] [--start-runtime]"
			            " [--require-focused] [--no-session]\n");
			return false;
		}
	}
	return true;
}

int finish(int code, XrSession session, GLContext & gl) {
	if(session != XR_NULL_HANDLE) {
		xrDestroySession(session);
	}
	gl.destroy();
	if(g_instance != XR_NULL_HANDLE) {
		xrDestroyInstance(g_instance);
		g_instance = XR_NULL_HANDLE;
	}
	std::printf("\nexit code %d\n", code);
	return code;
}

} // anonymous namespace

int main(int argc, char ** argv) {

	Options options;
	if(!parseOptions(argc, argv, options)) {
		return 1;
	}

	g_start = nowMs();
	GLContext gl;
	XrSession session = XR_NULL_HANDLE;

	std::printf("xr_hello: OpenXR runtime probe (%zu-bit, OpenGL)\n\n", sizeof(void *) * 8);

	// 0. Which runtime, and would creating an instance start it?
	std::string manifest = activeRuntimeManifest();
	if(manifest.empty()) {
		std::printf("FAIL: no active OpenXR runtime (XR_RUNTIME_JSON unset, no ActiveRuntime in the registry)\n");
		return finish(1, session, gl);
	}
	std::printf("active runtime manifest: %s\n", manifest.c_str());
	if(icontains(manifest, "steamvr") && !isProcessRunning(L"vrserver.exe")) {
		if(options.startRuntime) {
			std::printf("SteamVR is not running; --start-runtime given, xrCreateInstance will start it\n");
		} else {
			std::printf("PARTIAL: SteamVR is the OpenXR runtime but is not running.\n"
			            "Start SteamVR first (the game does the same and starts flat), or pass --start-runtime.\n");
			return finish(2, session, gl);
		}
	}

	// 1. Extensions
	uint32_t extensionCount = 0;
	XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
	if(XR_FAILED(r)) {
		std::printf("FAIL: xrEnumerateInstanceExtensionProperties: %s (no runtime reachable by the loader)\n", res(r));
		return finish(1, session, gl);
	}
	std::vector<XrExtensionProperties> extensions(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
	bool hasOpenGL = false;
	for(const XrExtensionProperties & extension : extensions) {
		if(std::strcmp(extension.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0) {
			hasOpenGL = true;
		}
	}
	std::printf("runtime extensions: %u (%s: %s)\n", extensionCount, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,
	            hasOpenGL ? "YES" : "NO");
	if(!hasOpenGL) {
		std::printf("FAIL: the runtime has no OpenGL support\n");
		return finish(1, session, gl);
	}

	// 2. Instance
	const char * enabled[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
	XrInstanceCreateInfo instanceInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
	std::strncpy(instanceInfo.applicationInfo.applicationName, "xr_hello", XR_MAX_APPLICATION_NAME_SIZE - 1);
	instanceInfo.applicationInfo.applicationVersion = 1;
	std::strncpy(instanceInfo.applicationInfo.engineName, "Arx Fatalis VR", XR_MAX_ENGINE_NAME_SIZE - 1);
	instanceInfo.applicationInfo.engineVersion = 1;
	#ifdef XR_API_VERSION_1_0
	instanceInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	#else
	instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	#endif
	instanceInfo.enabledExtensionCount = 1;
	instanceInfo.enabledExtensionNames = enabled;
	r = xrCreateInstance(&instanceInfo, &g_instance);
	if(XR_FAILED(r)) {
		g_instance = XR_NULL_HANDLE;
		std::printf("FAIL: xrCreateInstance: %s\n", res(r));
		return finish(1, session, gl);
	}
	XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
	xrGetInstanceProperties(g_instance, &instanceProperties);
	std::printf("runtime: %s (version %u.%u.%u)\n", instanceProperties.runtimeName,
	            XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
	            XR_VERSION_MINOR(instanceProperties.runtimeVersion),
	            XR_VERSION_PATCH(instanceProperties.runtimeVersion));

	// 3. System (needs the headset)
	XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	r = xrGetSystem(g_instance, &systemInfo, &system);
	if(r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
		std::printf("\nPARTIAL: the loader and the runtime work, but no headset is connected.\n");
		return finish(2, session, gl);
	}
	if(XR_FAILED(r)) {
		std::printf("FAIL: xrGetSystem: %s\n", res(r));
		return finish(1, session, gl);
	}
	XrSystemProperties systemProperties = { XR_TYPE_SYSTEM_PROPERTIES };
	xrGetSystemProperties(g_instance, system, &systemProperties);
	std::printf("system: %s (vendor %u, %s, max layers %u, max swapchain %ux%u)\n",
	            systemProperties.systemName, systemProperties.vendorId,
	            systemProperties.trackingProperties.positionTracking ? "positional tracking" : "orientation only",
	            systemProperties.graphicsProperties.maxLayerCount,
	            systemProperties.graphicsProperties.maxSwapchainImageWidth,
	            systemProperties.graphicsProperties.maxSwapchainImageHeight);

	uint32_t viewCount = 0;
	xrEnumerateViewConfigurationViews(g_instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	std::vector<XrViewConfigurationView> views(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	if(XR_SUCCEEDED(xrEnumerateViewConfigurationViews(g_instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
	                                                  viewCount, &viewCount, views.data()))) {
		for(uint32_t i = 0; i < viewCount; i++) {
			std::printf("eye %u: recommended %ux%u, %ux MSAA\n", i, views[i].recommendedImageRectWidth,
			            views[i].recommendedImageRectHeight, views[i].recommendedSwapchainSampleCount);
		}
	}

	if(options.noSession) {
		std::printf("\n--no-session: stopping after the system query\n");
		return finish(0, session, gl);
	}

	// 4. OpenGL context and the runtime's requirements
	if(!gl.create()) {
		return finish(1, session, gl);
	}
	const char * glVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	const char * glRenderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	int glMajor = 0, glMinor = 0;
	if(glVersion) {
		std::sscanf(glVersion, "%d.%d", &glMajor, &glMinor);
	}
	std::printf("OpenGL: %s on %s\n", glVersion ? glVersion : "(null)", glRenderer ? glRenderer : "(null)");

	PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements = nullptr;
	xrGetInstanceProcAddr(g_instance, "xrGetOpenGLGraphicsRequirementsKHR",
	                      reinterpret_cast<PFN_xrVoidFunction *>(&getRequirements));
	if(!getRequirements) {
		std::printf("FAIL: xrGetOpenGLGraphicsRequirementsKHR not provided by the runtime\n");
		return finish(1, session, gl);
	}
	XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	r = getRequirements(g_instance, system, &requirements);
	if(XR_FAILED(r)) {
		std::printf("FAIL: xrGetOpenGLGraphicsRequirementsKHR: %s\n", res(r));
		return finish(1, session, gl);
	}
	std::printf("runtime supports OpenGL %u.%u to %u.%u, have %d.%d\n",
	            XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported),
	            XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported),
	            glMajor, glMinor);
	if(XR_MAKE_VERSION(glMajor, glMinor, 0) < requirements.minApiVersionSupported) {
		std::printf("FAIL: the OpenGL context is too old for the runtime\n");
		return finish(1, session, gl);
	}

	// 5. Session
	XrGraphicsBindingOpenGLWin32KHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	binding.hDC = gl.dc;
	binding.hGLRC = gl.glrc;
	XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
	sessionInfo.next = &binding;
	sessionInfo.systemId = system;
	r = xrCreateSession(g_instance, &sessionInfo, &session);
	if(XR_FAILED(r)) {
		session = XR_NULL_HANDLE;
		std::printf("FAIL: xrCreateSession: %s\n", res(r));
		return finish(1, session, gl);
	}
	stamp();
	std::printf("session created\n");

	// 6. Wait for READY, run the frame loop, request an exit, wait for EXITING
	bool ready = false;
	bool running = false;
	bool stopping = false;
	bool exited = false;
	bool focused = false;
	XrSessionState highest = XR_SESSION_STATE_UNKNOWN;
	int frames = 0;
	double firstFrame = 0.0, lastFrame = 0.0;
	XrDuration displayPeriod = 0;
	ULONGLONG deadline = nowMs() + ULONGLONG(options.timeout) * 1000;

	while(!exited) {

		XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
		while(xrPollEvent(g_instance, &event) == XR_SUCCESS) {
			if(event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
				const XrEventDataSessionStateChanged & changed
					= *reinterpret_cast<const XrEventDataSessionStateChanged *>(&event);
				stamp();
				std::printf("session state: %s\n", stateName(changed.state));
				if(changed.state >= XR_SESSION_STATE_IDLE && changed.state <= XR_SESSION_STATE_FOCUSED
				   && changed.state > highest) {
					highest = changed.state;
				}
				switch(changed.state) {
					case XR_SESSION_STATE_READY:        ready = true; break;
					case XR_SESSION_STATE_FOCUSED:      focused = true; break;
					case XR_SESSION_STATE_STOPPING:     stopping = true; break;
					case XR_SESSION_STATE_EXITING:
					case XR_SESSION_STATE_LOSS_PENDING: exited = true; break;
					default: break;
				}
			} else if(event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
				stamp();
				std::printf("instance loss pending: the runtime is shutting down\n");
				exited = true;
			}
			event = { XR_TYPE_EVENT_DATA_BUFFER };
		}
		if(exited) {
			break;
		}

		if(ready && !running && !stopping) {
			XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
			beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			r = xrBeginSession(session, &beginInfo);
			if(XR_FAILED(r)) {
				std::printf("FAIL: xrBeginSession: %s\n", res(r));
				return finish(1, session, gl);
			}
			running = true;
			stamp();
			std::printf("session running, submitting %d empty frames (the headset shows black)\n", options.frames);
			deadline = nowMs() + ULONGLONG(options.timeout) * 1000;
		}

		if(running && stopping) {
			r = xrEndSession(session);
			stamp();
			std::printf("xrEndSession: %s\n", res(r));
			running = false;
			deadline = nowMs() + ULONGLONG(options.timeout) * 1000;
		} else if(running && frames < options.frames) {
			XrFrameState frameState = { XR_TYPE_FRAME_STATE };
			XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
			r = xrWaitFrame(session, &waitInfo, &frameState);
			if(XR_FAILED(r)) {
				std::printf("FAIL: xrWaitFrame: %s\n", res(r));
				return finish(1, session, gl);
			}
			XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
			xrBeginFrame(session, &beginInfo);
			XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
			endInfo.displayTime = frameState.predictedDisplayTime;
			endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
			endInfo.layerCount = 0;
			r = xrEndFrame(session, &endInfo);
			if(XR_FAILED(r)) {
				std::printf("FAIL: xrEndFrame: %s\n", res(r));
				return finish(1, session, gl);
			}
			if(frames == 0) {
				firstFrame = seconds();
				displayPeriod = frameState.predictedDisplayPeriod;
			}
			frames++;
			lastFrame = seconds();
			if(frames == options.frames) {
				stamp();
				std::printf("%d frames submitted in %.3f s (runtime display period %.2f ms), requesting exit\n",
				            frames, lastFrame - firstFrame, double(displayPeriod) / 1e6);
				xrRequestExitSession(session);
				deadline = nowMs() + ULONGLONG(options.timeout) * 1000;
			}
		} else {
			Sleep(10);
		}

		if(nowMs() > deadline) {
			stamp();
			std::printf("timeout after %d s (ready=%d running=%d frames=%d)\n", options.timeout,
			            int(ready), int(running), frames);
			break;
		}
	}

	std::printf("\nhighest session state: %s, frames: %d/%d, focused: %s\n", stateName(highest), frames,
	            options.frames, focused ? "yes" : "no");

	int code = 0;
	if(frames >= options.frames) {
		std::printf("FULL PASS: an OpenXR session ran %d frames on '%s'.\n", frames, instanceProperties.runtimeName);
		if(options.requireFocused && !focused) {
			std::printf("PARTIAL: --require-focused and the session never reached FOCUSED"
			            " (headset idle, nobody wearing it, or another app has focus).\n");
			code = 2;
		}
	} else if(!ready) {
		std::printf("PARTIAL: the session was created but never reached READY in %d s (headset idle / standby?).\n",
		            options.timeout);
		code = 2;
	} else {
		std::printf("PARTIAL: the session became READY but only %d of %d frames ran.\n", frames, options.frames);
		code = 2;
	}

	return finish(code, session, gl);
}
