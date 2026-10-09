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

#include "vr/VRSimulator.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include <glm/gtc/quaternion.hpp>

#include "core/Application.h"
#include "core/GameEvent.h"
#include "graphics/image/Image.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "platform/ProgramOptions.h"
#include "platform/Thread.h"
#include "platform/Time.h"

#if ARX_HAVE_OPENXR
#include "vr/OpenXRUtil.h"
#include "vr/VRSwapchain.h"
#include <openxr/openxr_reflection.h>
#endif

namespace vr {

namespace {

std::string g_scriptPath;

void setSimScript(const std::string & path) {
	g_scriptPath = path;
}

} // anonymous namespace

ARX_PROGRAM_OPTION_ARG("vr-sim", nullptr,
                       "Run in VR on a simulated headset driven by SCRIPT - no runtime, no SteamVR, wins over --no-vr"
                       " (docs/vr/TESTING.md)",
                       &setSimScript, "SCRIPT")

bool simRequested() {
	return !g_scriptPath.empty();
}

#if ARX_HAVE_OPENXR

namespace {

//! STAGE (the room floor) below the LOCAL origin, where the head starts
const float FloorBelowHead = 1.7f;

//! Images per simulated swapchain
const u32 SwapchainImages = 3;

//! The two-eyes numbers are computed on luma grids this wide
const size_t GridWidth = 160;

//! How much of the eye image around where it looks straight ahead the verdict judges, each way
const float SceneRegionHalf = 0.15f;

//! The middle of the flat frame the quad's verdict judges - the HUD sits at the edges and the bottom
const ImageRegion QuadRegion = { 0.3f, 0.35f, 0.7f, 0.75f };

const char * const EyeNames[2] = { "left", "right" };

struct SimSwapchain {
	s64 format = 0;
	Vec2i size = Vec2i(0);
	std::vector<GLuint> textures;
	u32 next = 0;
	s32 acquired = -1;
	s32 released = -1;
	u64 releasedFrame = 0;
};

struct SimSpace {
	bool action = false;
	XrReferenceSpaceType type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	size_t hand = 0;
	XrPosef offset = { { 0.f, 0.f, 0.f, 1.f }, { 0.f, 0.f, 0.f } };
};

struct SimAction {
	std::string name;
	XrActionType type = XR_ACTION_TYPE_FLOAT_INPUT;
};

struct ShotRecord {
	SimShot shot;
	std::array<LumaGrid, 2> grids;
};

struct SimRuntime {

	Simulation sim;
	std::string script;
	std::string captureDir;

	bool instance = false;
	bool session = false;
	XrSessionState state = XR_SESSION_STATE_UNKNOWN;
	bool running = false;
	bool exitRequested = false;
	std::deque<XrEventDataBuffer> events;

	std::vector<std::string> paths; //!< XrPath n is paths[n - 1]
	u64 nextHandle = 1;
	std::map<u64, SimSwapchain> swapchains;
	std::map<u64, SimSpace> spaces;
	std::map<u64, SimAction> actions;
	std::set<u64> actionSets;
	bool attached = false;

	// The frame loop
	u64 frames = 0;
	bool waited = false;
	bool begun = false;
	s64 nextSlot = 0;    //!< nanoseconds
	s64 displayTime = 0; //!< of the current frame
	bool timelineStarted = false;
	u64 timelineStart = 0;
	bool timelineDoneLogged = false;

	// Input: the action values as of the last two syncs, by (action, hand)
	std::map<std::pair<u64, size_t>, Vec2f> synced;
	std::map<std::pair<u64, size_t>, Vec2f> previous;
	u64 syncs = 0;

	// The compositor
	SimStatus counters;
	bool located = false;
	std::array<SimEye, 2> eyes;
	std::vector<std::string> pendingShots;
	std::vector<ShotRecord> shots;
	u32 failures = 0;

};

std::unique_ptr<SimRuntime> g_sim;

s64 nowNs() {
	return toUs(platform::getTime()) * 1000;
}

template <typename Handle>
Handle toHandle(u64 id) {
	return reinterpret_cast<Handle>(uintptr_t(id));
}

template <typename Handle>
u64 fromHandle(Handle handle) {
	return u64(reinterpret_cast<uintptr_t>(handle));
}

template <typename Handle>
Handle newHandle() {
	return toHandle<Handle>(g_sim->nextHandle++);
}

Vec3f toVec(const XrVector3f & v) {
	return Vec3f(v.x, v.y, v.z);
}

glm::quat toQuat(const XrQuaternionf & q) {
	return glm::quat(q.w, q.x, q.y, q.z);
}

XrPosef toPose(const Vec3f & position, const glm::quat & orientation) {
	XrPosef pose;
	pose.position = { position.x, position.y, position.z };
	pose.orientation = { orientation.x, orientation.y, orientation.z, orientation.w };
	return pose;
}

void queueEvent(const void * event, size_t size) {
	XrEventDataBuffer buffer = { XR_TYPE_EVENT_DATA_BUFFER };
	std::memcpy(&buffer, event, std::min(size, sizeof(buffer)));
	g_sim->events.push_back(buffer);
}

XrSession sessionHandle() {
	return toHandle<XrSession>(1);
}

void setState(XrSessionState state) {
	if(g_sim->state == state) {
		return;
	}
	g_sim->state = state;
	XrEventDataSessionStateChanged event = { XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED };
	event.session = sessionHandle();
	event.state = state;
	event.time = nowNs();
	queueEvent(&event, sizeof(event));
}

void profileChanged() {
	if(!g_sim->attached) {
		return;
	}
	XrEventDataInteractionProfileChanged event = { XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED };
	event.session = sessionHandle();
	queueEvent(&event, sizeof(event));
}

XrPath internPath(const std::string & text) {
	std::vector<std::string> & paths = g_sim->paths;
	auto it = std::find(paths.begin(), paths.end(), text);
	if(it == paths.end()) {
		paths.push_back(text);
		return XrPath(paths.size());
	}
	return XrPath(it - paths.begin() + 1);
}

std::string pathString(XrPath path) {
	if(path == XR_NULL_PATH || path > g_sim->paths.size()) {
		return std::string();
	}
	return g_sim->paths[size_t(path - 1)];
}

//! The hand a subaction or top-level path names: 0, 1, or 2 for none
size_t handOf(XrPath path) {
	const std::string text = pathString(path);
	if(text == "/user/hand/left") {
		return 0;
	} else if(text == "/user/hand/right") {
		return 1;
	}
	return 2;
}

bool handTracked(size_t hand) {
	return hand < 2 && g_sim->sim.rig.handTracked[hand] && !g_sim->sim.rig.profile.empty();
}

//! The pose of a space in the room (LOCAL); false when it is not tracked
bool roomPose(XrSpace space, Vec3f & position, glm::quat & orientation) {

	auto it = g_sim->spaces.find(fromHandle(space));
	if(it == g_sim->spaces.end()) {
		return false;
	}
	const SimSpace & s = it->second;
	const SimRig & rig = g_sim->sim.rig;

	if(s.action) {
		if(!handTracked(s.hand)) {
			return false;
		}
		position = rig.hands[s.hand].position;
		orientation = rig.hands[s.hand].orientation();
	} else if(s.type == XR_REFERENCE_SPACE_TYPE_VIEW) {
		position = rig.head.position;
		orientation = rig.head.orientation();
	} else if(s.type == XR_REFERENCE_SPACE_TYPE_STAGE) {
		position = Vec3f(0.f, -FloorBelowHead, 0.f);
		orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	} else {
		position = Vec3f(0.f);
		orientation = glm::quat(1.f, 0.f, 0.f, 0.f);
	}

	// The space's own pose in its parent
	position += orientation * toVec(s.offset.position);
	orientation = orientation * toQuat(s.offset.orientation);
	return true;
}

void logFailure(const std::string & what) {
	g_sim->failures++;
	if(g_sim->failures <= 10) {
		LogError << "[vr] sim: " << what << (g_sim->failures == 10 ? " (further failures are not logged)" : "");
	}
}

// --- Instance and system --------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetOpenGLGraphicsRequirementsKHR(XrInstance /* instance */, XrSystemId /* system */,
                                                                      XrGraphicsRequirementsOpenGLKHR * requirements) {
	requirements->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
	requirements->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetInstanceProcAddr(XrInstance instance, const char * name, PFN_xrVoidFunction * function);

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateInstanceExtensionProperties(const char * layerName, uint32_t capacity,
                                                                         uint32_t * count, XrExtensionProperties * properties) {
	if(layerName) {
		return XR_ERROR_API_LAYER_NOT_PRESENT;
	}
	*count = 1;
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	std::strncpy(properties[0].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_MAX_EXTENSION_NAME_SIZE - 1);
	properties[0].extensionVersion = XR_KHR_opengl_enable_SPEC_VERSION;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateInstance(const XrInstanceCreateInfo * info, XrInstance * instance) {
	for(uint32_t i = 0; i < info->enabledExtensionCount; i++) {
		if(std::strcmp(info->enabledExtensionNames[i], XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) != 0) {
			LogError << "[vr] sim: the game asks for the extension " << info->enabledExtensionNames[i]
			         << ", which the simulator does not have - add it to vr/VRSimulator.cpp";
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	g_sim->instance = true;
	*instance = toHandle<XrInstance>(1);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrDestroyInstance(XrInstance /* instance */) {
	g_sim->instance = false;
	g_sim->actionSets.clear();
	g_sim->actions.clear();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetInstanceProperties(XrInstance /* instance */, XrInstanceProperties * properties) {
	std::strncpy(properties->runtimeName, "Arx VR simulator", XR_MAX_RUNTIME_NAME_SIZE - 1);
	properties->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrPollEvent(XrInstance /* instance */, XrEventDataBuffer * event) {
	if(g_sim->events.empty()) {
		return XR_EVENT_UNAVAILABLE;
	}
	*event = g_sim->events.front();
	g_sim->events.pop_front();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrResultToString(XrInstance /* instance */, XrResult value,
                                                    char buffer[XR_MAX_RESULT_STRING_SIZE]) {
	const char * name = nullptr;
	switch(value) {
		#define ARX_XR_RESULT_CASE(n, v) case n: name = #n; break;
		XR_LIST_ENUM_XrResult(ARX_XR_RESULT_CASE)
		#undef ARX_XR_RESULT_CASE
		default: break;
	}
	if(name) {
		std::strncpy(buffer, name, XR_MAX_RESULT_STRING_SIZE - 1);
		buffer[XR_MAX_RESULT_STRING_SIZE - 1] = '\0';
	} else {
		std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_UNKNOWN_%d", int(value));
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetSystem(XrInstance /* instance */, const XrSystemGetInfo * info, XrSystemId * system) {
	if(info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	}
	*system = 1;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetSystemProperties(XrInstance /* instance */, XrSystemId /* system */,
                                                         XrSystemProperties * properties) {
	properties->systemId = 1;
	properties->vendorId = 0;
	std::snprintf(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, "Simulated headset (%s)",
	              std::string(fs::path(g_sim->script).filename()).c_str());
	properties->graphicsProperties.maxSwapchainImageWidth = 4096;
	properties->graphicsProperties.maxSwapchainImageHeight = 4096;
	properties->graphicsProperties.maxLayerCount = 16;
	properties->trackingProperties.orientationTracking = XR_TRUE;
	properties->trackingProperties.positionTracking = XR_TRUE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateViewConfigurations(XrInstance /* instance */, XrSystemId /* system */,
                                                                 uint32_t capacity, uint32_t * count,
                                                                 XrViewConfigurationType * types) {
	*count = 1;
	if(capacity > 0) {
		types[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateViewConfigurationViews(XrInstance /* instance */, XrSystemId /* system */,
                                                                     XrViewConfigurationType type, uint32_t capacity,
                                                                     uint32_t * count, XrViewConfigurationView * views) {
	if(type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	*count = 2;
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < 2) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	const Vec2i size = g_sim->sim.view.eyeSize;
	for(size_t i = 0; i < 2; i++) {
		views[i].recommendedImageRectWidth = u32(size.x);
		views[i].recommendedImageRectHeight = u32(size.y);
		views[i].maxImageRectWidth = 4096;
		views[i].maxImageRectHeight = 4096;
		views[i].recommendedSwapchainSampleCount = 1;
		views[i].maxSwapchainSampleCount = 1;
	}
	return XR_SUCCESS;
}

// --- Session ------------------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateSession(XrInstance /* instance */, const XrSessionCreateInfo * info,
                                                   XrSession * session) {

	const XrBaseInStructure * binding = reinterpret_cast<const XrBaseInStructure *>(info->next);
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	if(!binding || binding->type != XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR
	   || !reinterpret_cast<const XrGraphicsBindingOpenGLWin32KHR *>(binding)->hGLRC) {
		LogError << "[vr] sim: xrCreateSession without an OpenGL binding";
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	}
	#else
	ARX_UNUSED(binding);
	#endif

	g_sim->session = true;
	g_sim->running = false;
	g_sim->exitRequested = false;
	g_sim->state = XR_SESSION_STATE_IDLE;
	*session = sessionHandle();
	setState(XR_SESSION_STATE_READY);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrDestroySession(XrSession /* session */) {
	g_sim->session = false;
	g_sim->running = false;
	g_sim->attached = false;
	g_sim->state = XR_SESSION_STATE_UNKNOWN;
	g_sim->events.clear();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrBeginSession(XrSession /* session */, const XrSessionBeginInfo * info) {
	if(g_sim->state != XR_SESSION_STATE_READY) {
		return XR_ERROR_SESSION_NOT_READY;
	}
	if(info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	}
	g_sim->running = true;
	g_sim->sim.lockSetup();
	g_sim->nextSlot = 0;
	setState(XR_SESSION_STATE_SYNCHRONIZED);
	setState(XR_SESSION_STATE_VISIBLE);
	if(g_sim->sim.rig.focused) {
		setState(XR_SESSION_STATE_FOCUSED);
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEndSession(XrSession /* session */) {
	if(g_sim->state != XR_SESSION_STATE_STOPPING) {
		return XR_ERROR_SESSION_NOT_STOPPING;
	}
	g_sim->running = false;
	setState(XR_SESSION_STATE_IDLE);
	if(g_sim->exitRequested) {
		setState(XR_SESSION_STATE_EXITING);
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrRequestExitSession(XrSession /* session */) {
	if(!g_sim->running) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	g_sim->exitRequested = true;
	if(g_sim->state == XR_SESSION_STATE_FOCUSED) {
		setState(XR_SESSION_STATE_VISIBLE);
	}
	setState(XR_SESSION_STATE_SYNCHRONIZED);
	setState(XR_SESSION_STATE_STOPPING);
	return XR_SUCCESS;
}

// --- Spaces -------------------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateReferenceSpaces(XrSession /* session */, uint32_t capacity, uint32_t * count,
                                                              XrReferenceSpaceType * spaces) {
	const XrReferenceSpaceType types[] = { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL,
	                                       XR_REFERENCE_SPACE_TYPE_STAGE };
	*count = 3;
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < 3) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	std::copy(std::begin(types), std::end(types), spaces);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateReferenceSpace(XrSession /* session */, const XrReferenceSpaceCreateInfo * info,
                                                          XrSpace * space) {
	if(info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW && info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL
	   && info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE) {
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	}
	SimSpace s;
	s.type = info->referenceSpaceType;
	s.offset = info->poseInReferenceSpace;
	*space = newHandle<XrSpace>();
	g_sim->spaces[fromHandle(*space)] = s;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateActionSpace(XrSession /* session */, const XrActionSpaceCreateInfo * info,
                                                       XrSpace * space) {
	if(g_sim->actions.find(fromHandle(info->action)) == g_sim->actions.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	SimSpace s;
	s.action = true;
	s.hand = handOf(info->subactionPath);
	s.offset = info->poseInActionSpace;
	if(s.hand > 1) {
		return XR_ERROR_PATH_UNSUPPORTED;
	}
	*space = newHandle<XrSpace>();
	g_sim->spaces[fromHandle(*space)] = s;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrLocateSpace(XrSpace space, XrSpace base, XrTime /* time */, XrSpaceLocation * location) {
	location->locationFlags = 0;
	Vec3f position(0.f), basePosition(0.f);
	glm::quat orientation(1.f, 0.f, 0.f, 0.f), baseOrientation(1.f, 0.f, 0.f, 0.f);
	if(g_sim->spaces.find(fromHandle(space)) == g_sim->spaces.end()
	   || g_sim->spaces.find(fromHandle(base)) == g_sim->spaces.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	if(!roomPose(space, position, orientation) || !roomPose(base, basePosition, baseOrientation)) {
		return XR_SUCCESS; // not tracked
	}
	poseInSpace(basePosition, baseOrientation, position, orientation);
	location->pose = toPose(position, orientation);
	location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT
	                          | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrDestroySpace(XrSpace space) {
	return g_sim->spaces.erase(fromHandle(space)) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

// --- Swapchains ---------------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateSwapchainFormats(XrSession /* session */, uint32_t capacity, uint32_t * count,
                                                               int64_t * formats) {
	*count = 2;
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < 2) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	formats[0] = GL_SRGB8_ALPHA8;
	formats[1] = GL_RGBA8;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateSwapchain(XrSession /* session */, const XrSwapchainCreateInfo * info,
                                                     XrSwapchain * swapchain) {

	if((info->format != GL_SRGB8_ALPHA8 && info->format != GL_RGBA8) || info->width == 0 || info->height == 0
	   || info->width > 4096 || info->height > 4096 || info->arraySize != 1 || info->faceCount != 1) {
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	}

	SimSwapchain s;
	s.format = info->format;
	s.size = Vec2i(s32(info->width), s32(info->height));
	s.textures.resize(SwapchainImages, 0);

	GLint bound = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
	glGenTextures(GLsizei(s.textures.size()), s.textures.data());
	for(GLuint texture : s.textures) {
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GLint(info->format), s.size.x, s.size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	}
	glBindTexture(GL_TEXTURE_2D, GLuint(bound));

	*swapchain = newHandle<XrSwapchain>();
	g_sim->swapchains[fromHandle(*swapchain)] = std::move(s);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrDestroySwapchain(XrSwapchain swapchain) {
	auto it = g_sim->swapchains.find(fromHandle(swapchain));
	if(it == g_sim->swapchains.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	glDeleteTextures(GLsizei(it->second.textures.size()), it->second.textures.data());
	g_sim->swapchains.erase(it);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t capacity, uint32_t * count,
                                                              XrSwapchainImageBaseHeader * images) {
	auto it = g_sim->swapchains.find(fromHandle(swapchain));
	if(it == g_sim->swapchains.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	const std::vector<GLuint> & textures = it->second.textures;
	*count = u32(textures.size());
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < textures.size()) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	XrSwapchainImageOpenGLKHR * gl = reinterpret_cast<XrSwapchainImageOpenGLKHR *>(images);
	for(size_t i = 0; i < textures.size(); i++) {
		gl[i].image = textures[i];
	}
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo * /* info */,
                                                           uint32_t * index) {
	auto it = g_sim->swapchains.find(fromHandle(swapchain));
	if(it == g_sim->swapchains.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	SimSwapchain & s = it->second;
	if(s.acquired >= 0) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	s.acquired = s32(s.next);
	s.next = (s.next + 1) % u32(s.textures.size());
	*index = u32(s.acquired);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo * /* info */) {
	auto it = g_sim->swapchains.find(fromHandle(swapchain));
	if(it == g_sim->swapchains.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	return it->second.acquired >= 0 ? XR_SUCCESS : XR_ERROR_CALL_ORDER_INVALID;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo * /* info */) {
	auto it = g_sim->swapchains.find(fromHandle(swapchain));
	if(it == g_sim->swapchains.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	SimSwapchain & s = it->second;
	if(s.acquired < 0) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	s.released = s.acquired;
	s.releasedFrame = g_sim->frames;
	s.acquired = -1;
	return XR_SUCCESS;
}

// --- The frame loop -----------------------------------------------------------------------------

void runRequests() {

	SimRequests requests = g_sim->sim.takeRequests();
	if(requests.empty()) {
		return;
	}

	for(const std::string & mark : requests.marks) {
		LogInfo << "[mark] " << mark;
	}
	for(const std::string & shot : requests.shots) {
		g_sim->pendingShots.push_back(shot);
	}
	if(requests.focusChanged && g_sim->running) {
		const bool focused = g_sim->sim.rig.focused;
		LogInfo << "[vr] sim: the runtime " << (focused ? "gives the input focus back" : "takes the input focus (another app in front)");
		if(focused && g_sim->state == XR_SESSION_STATE_VISIBLE) {
			setState(XR_SESSION_STATE_FOCUSED);
		} else if(!focused && g_sim->state == XR_SESSION_STATE_FOCUSED) {
			setState(XR_SESSION_STATE_VISIBLE);
		}
	}
	if(requests.profileChanged) {
		profileChanged();
	}
	if(requests.quit) {
		LogInfo << "[vr] sim: the script asks the game to quit";
		if(mainApp) {
			mainApp->quit();
		}
	}
}

void advanceTimeline() {

	SimRuntime & s = *g_sim;
	if(!s.timelineStarted) {
		bool start = s.sim.timeline == SimTimeline::Session
		             || gameevent::detectGameState() == gameevent::GameState::Gameplay;
		if(!start) {
			return;
		}
		s.timelineStarted = true;
		s.timelineStart = s.frames;
		LogInfo << "[vr] sim: timeline starts at frame " << s.frames << " ("
		        << (s.sim.timeline == SimTimeline::Session ? "first frame of the session" : "the game is playable")
		        << "), " << s.sim.pending() << " timed lines";
	}

	s.sim.advance(s.frames - s.timelineStart);
	if(s.sim.pending() == 0 && !s.timelineDoneLogged) {
		s.timelineDoneLogged = true;
		LogInfo << "[vr] sim: timeline done at its frame " << s.sim.frame();
	} else if(s.sim.pending() != 0) {
		s.timelineDoneLogged = false;
	}
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrWaitFrame(XrSession /* session */, const XrFrameWaitInfo * /* info */, XrFrameState * state) {

	SimRuntime & s = *g_sim;
	if(!s.running) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}

	// The display clock: a slot every 1/rate seconds; a missed slot is skipped, as on a headset
	const float rate = s.sim.view.rate;
	const s64 period = s64(1e9 / double(rate > 0.f ? rate : 90.f));
	s64 now = nowNs();
	if(s.nextSlot == 0 || rate <= 0.f) {
		s.nextSlot = now;
	} else if(s.nextSlot < now) {
		s.nextSlot += ((now - s.nextSlot) / period + 1) * period;
	}
	if(rate > 0.f && s.nextSlot > now) {
		Thread::sleep(PlatformDuration(std::chrono::microseconds((s.nextSlot - now) / 1000)));
	}
	s.displayTime = s.nextSlot + period;
	s.nextSlot += period;

	s.frames++;
	s.waited = true;
	advanceTimeline();
	runRequests();

	state->predictedDisplayTime = s.displayTime;
	state->predictedDisplayPeriod = period;
	state->shouldRender = (s.state == XR_SESSION_STATE_VISIBLE || s.state == XR_SESSION_STATE_FOCUSED) ? XR_TRUE : XR_FALSE;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrBeginFrame(XrSession /* session */, const XrFrameBeginInfo * /* info */) {
	SimRuntime & s = *g_sim;
	if(!s.running) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if(!s.waited) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	s.waited = false;
	const bool discarded = s.begun;
	s.begun = true;
	return discarded ? XR_FRAME_DISCARDED : XR_SUCCESS;
}

/*!
 * \param withAlpha read the alpha plane too: for a layer the compositor blends by its alpha (the 2D
 *                  interface on the quad, #19)
 */
RgbImage readImage(const SimSwapchain & swapchain, bool withAlpha = false) {

	RgbImage image(size_t(swapchain.size.x), size_t(swapchain.size.y));
	if(swapchain.released < 0) {
		return image;
	}

	GLint bound = 0, alignment = 4, packBuffer = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
	glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glBindTexture(GL_TEXTURE_2D, swapchain.textures[size_t(swapchain.released)]);
	// The stored values, no sRGB decoding: they are sRGB-encoded like the window
	if(withAlpha) {
		std::vector<u8> rgba(image.width * image.height * 4);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		image.alpha.resize(image.width * image.height);
		for(size_t i = 0; i < image.alpha.size(); i++) {
			image.pixels[i * 3] = rgba[i * 4];
			image.pixels[i * 3 + 1] = rgba[i * 4 + 1];
			image.pixels[i * 3 + 2] = rgba[i * 4 + 2];
			image.alpha[i] = rgba[i * 4 + 3];
		}
	} else {
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, image.pixels.data());
	}
	glBindTexture(GL_TEXTURE_2D, GLuint(bound));
	glPixelStorei(GL_PACK_ALIGNMENT, alignment);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(packBuffer));

	return image;
}

std::string saveImage(const RgbImage & picture, const std::string & name) {

	fs::path dir = g_sim->captureDir.empty() ? fs::getUserDir() / "vrsim" : fs::path(g_sim->captureDir);
	if(!fs::create_directories(dir)) {
		logFailure("could not create the capture directory " + dir.string());
		return std::string();
	}

	Image image;
	image.create(picture.width, picture.height, Image::Format_R8G8B8);
	std::memcpy(image.getData(), picture.pixels.data(), picture.pixels.size());
	image.flipY();
	fs::path file = dir / (name + ".bmp");
	if(!image.save(file)) {
		logFailure("could not write " + file.string());
		return std::string();
	}
	return file.string();
}

void eyeBackground(u8 background[3]) {
	for(size_t i = 0; i < 3; i++) {
		background[i] = u8(std::lround(EyeBackgroundColor[i] * 255.f));
	}
}

//! Compose, save and judge the shot \a name from the layers of this frame
void takeShot(const std::string & name, const XrCompositionLayerProjection & projection,
              const XrCompositionLayerQuad * quadLayer) {

	SimRuntime & s = *g_sim;
	ShotRecord record;
	SimShot & shot = record.shot;
	shot.name = name;
	shot.frame = s.frames;
	shot.timelineRunning = s.timelineStarted;
	shot.timelineFrame = s.timelineStarted ? s.frames - s.timelineStart : 0;
	shot.swapEyes = s.sim.rig.swapEyes;

	u8 background[3];
	eyeBackground(background);

	RgbImage quad;
	QuadPlacement placement;
	Vec3f quadSpacePosition(0.f);
	glm::quat quadSpaceOrientation(1.f, 0.f, 0.f, 0.f);
	if(quadLayer) {
		auto it = s.swapchains.find(fromHandle(quadLayer->subImage.swapchain));
		if(it != s.swapchains.end() && roomPose(quadLayer->space, quadSpacePosition, quadSpaceOrientation)) {
			// The 2D interface over transparency (#19) is blended by its alpha; a copied window is opaque
			placement.blended = (quadLayer->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
			quad = readImage(it->second, placement.blended);
			placement.position = toVec(quadLayer->pose.position);
			placement.orientation = toQuat(quadLayer->pose.orientation);
			placement.width = quadLayer->size.width;
			placement.height = quadLayer->size.height;
			shot.quadShown = true;
			shot.quad = pixelStats(quad, QuadRegion, background);
			if(placement.blended && alphaCoverage(quad, QuadRegion) < 0.1f) {
				// Nothing drawn in the middle of a blended quad: the world shows through, not a black frame
				shot.quad.verdict = ImageVerdict::Transparent;
			}
			shot.quadFile = saveImage(quad, name + "-quad");
		}
	}

	Vec3f layerPosition(0.f);
	glm::quat layerOrientation(1.f, 0.f, 0.f, 0.f);
	roomPose(projection.space, layerPosition, layerOrientation);

	std::array<RgbImage, 2> composed;
	for(size_t eye = 0; eye < 2; eye++) {

		const XrCompositionLayerProjectionView & view = projection.views[eye];
		SimShotEye & out = shot.eyes[eye];
		auto it = s.swapchains.find(fromHandle(view.subImage.swapchain));
		if(it == s.swapchains.end()) {
			continue;
		}
		out.imageFrame = it->second.releasedFrame;

		MirrorFov fov;
		fov.left = view.fov.angleLeft;
		fov.right = view.fov.angleRight;
		fov.up = view.fov.angleUp;
		fov.down = view.fov.angleDown;
		const ImageRegion region = forwardRegion(fov, SceneRegionHalf);

		composed[eye] = readImage(it->second);
		out.layer = pixelStats(composed[eye], region, background);

		// The eye as submitted, in the room, and as seen from the quad's space
		Vec3f position = layerPosition + layerOrientation * toVec(view.pose.position);
		glm::quat orientation = layerOrientation * toQuat(view.pose.orientation);
		out.position = position;
		out.angles = simAngles(orientation);
		if(shot.quadShown) {
			poseInSpace(quadSpacePosition, quadSpaceOrientation, position, orientation);
			shot.quadPixels[eye] = composeQuad(composed[eye], fov, position, orientation, quad, placement);
		}
	}

	// The fault the two-eyes check must catch: each eye is shown the other eye's picture
	if(shot.swapEyes) {
		std::swap(composed[0], composed[1]);
	}

	for(size_t eye = 0; eye < 2; eye++) {
		const XrFovf & xrFov = projection.views[eye].fov;
		MirrorFov fov;
		fov.left = xrFov.angleLeft;
		fov.right = xrFov.angleRight;
		fov.up = xrFov.angleUp;
		fov.down = xrFov.angleDown;
		shot.eyes[eye].scene = pixelStats(composed[eye], forwardRegion(fov, SceneRegionHalf), background);
		shot.eyes[eye].file = saveImage(composed[eye], name + "-" + EyeNames[eye]);
		record.grids[eye] = lumaGrid(composed[eye], GridWidth);
	}

	shot.lrDiff = meanAbsDiff(record.grids[0], record.grids[1]);
	shot.lrShift = horizontalShift(record.grids[0], record.grids[1], int(GridWidth / 4));
	for(const ShotRecord & earlier : s.shots) {
		std::array<float, 2> diff = { meanAbsDiff(earlier.grids[0], record.grids[0]),
		                              meanAbsDiff(earlier.grids[1], record.grids[1]) };
		shot.versus.emplace_back(earlier.shot.name, diff);
	}

	LogInfo << "[vr] sim: shot " << name << " at frame " << shot.frame << ": left " << imageVerdictName(shot.eyes[0].scene.verdict)
	        << " (mean " << shot.eyes[0].scene.mean << "), right " << imageVerdictName(shot.eyes[1].scene.verdict)
	        << " (mean " << shot.eyes[1].scene.mean << "), flat frame "
	        << (shot.quadShown ? imageVerdictName(shot.quad.verdict) : "not shown")
	        << ", L/R difference " << shot.lrDiff << ", shift " << shot.lrShift.cells << " of " << GridWidth << " columns"
	        << (shot.swapEyes ? " (fault swap-eyes on)" : "");
	gameevent::Event("vr.sim.shot")("name", name)("left", imageVerdictName(shot.eyes[0].scene.verdict))
		("right", imageVerdictName(shot.eyes[1].scene.verdict))
		("flat", shot.quadShown ? imageVerdictName(shot.quad.verdict) : "none");

	s.shots.push_back(std::move(record));
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrEndFrame(XrSession /* session */, const XrFrameEndInfo * info) {

	SimRuntime & s = *g_sim;
	if(!s.running) {
		return XR_ERROR_SESSION_NOT_RUNNING;
	}
	if(!s.begun) {
		return XR_ERROR_CALL_ORDER_INVALID;
	}
	s.begun = false;
	s.counters.endedFrames++;

	const XrCompositionLayerProjection * projection = nullptr;
	const XrCompositionLayerQuad * quad = nullptr;
	for(uint32_t i = 0; i < info->layerCount; i++) {
		const XrCompositionLayerBaseHeader * layer = info->layers[i];
		if(layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
			projection = reinterpret_cast<const XrCompositionLayerProjection *>(layer);
		} else if(layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
			quad = reinterpret_cast<const XrCompositionLayerQuad *>(layer);
		}
	}

	if(projection) {
		if(projection->viewCount != 2) {
			logFailure("projection layer with " + std::to_string(projection->viewCount) + " views");
			return XR_ERROR_VALIDATION_FAILURE;
		}
		std::array<u64, 2> imageFrames = { 0, 0 };
		for(size_t eye = 0; eye < 2; eye++) {
			auto it = s.swapchains.find(fromHandle(projection->views[eye].subImage.swapchain));
			if(it == s.swapchains.end() || it->second.released < 0) {
				logFailure(std::string("projection layer: the ") + EyeNames[eye] + " eye's swapchain has no released image");
				return XR_ERROR_LAYER_INVALID;
			}
			imageFrames[eye] = it->second.releasedFrame;
		}
		s.counters.projectionFrames++;
		if(imageFrames[0] == imageFrames[1]) {
			s.counters.pairedFrames++;
			if(imageFrames[0] == s.frames) {
				s.counters.freshFrames++;
			}
		} else {
			s.counters.staleFrames++;
			if(s.counters.staleFrames <= 5) {
				LogWarning << "[vr] sim: frame " << s.frames << " shows eye images of different frames (left "
				           << imageFrames[0] << ", right " << imageFrames[1] << ") - a stale eye"
				           << (s.counters.staleFrames == 5 ? " (not logged again)" : "");
			}
		}
	}
	if(quad) {
		s.counters.quadFrames++;
	}

	if(projection && !s.pendingShots.empty()) {
		std::vector<std::string> shots = std::move(s.pendingShots);
		s.pendingShots.clear();
		for(const std::string & name : shots) {
			takeShot(name, *projection, quad);
		}
	}

	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrLocateViews(XrSession /* session */, const XrViewLocateInfo * info, XrViewState * state,
                                                 uint32_t capacity, uint32_t * count, XrView * views) {

	*count = 2;
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < 2) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}

	Vec3f basePosition(0.f);
	glm::quat baseOrientation(1.f, 0.f, 0.f, 0.f);
	if(!roomPose(info->space, basePosition, baseOrientation)) {
		return XR_ERROR_HANDLE_INVALID;
	}

	const std::array<SimEye, 2> eyes = simEyes(g_sim->sim.view, g_sim->sim.rig.head);
	for(size_t i = 0; i < 2; i++) {
		Vec3f position = eyes[i].position;
		glm::quat orientation = eyes[i].orientation;
		poseInSpace(basePosition, baseOrientation, position, orientation);
		views[i].pose = toPose(position, orientation);
		views[i].fov.angleLeft = eyes[i].fov.left;
		views[i].fov.angleRight = eyes[i].fov.right;
		views[i].fov.angleUp = eyes[i].fov.up;
		views[i].fov.angleDown = eyes[i].fov.down;
	}
	g_sim->eyes = eyes;
	g_sim->located = true;

	state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT
	                        | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
	return XR_SUCCESS;
}

// --- Actions ------------------------------------------------------------------------------------

XRAPI_ATTR XrResult XRAPI_CALL sim_xrStringToPath(XrInstance /* instance */, const char * text, XrPath * path) {
	if(!text || text[0] != '/') {
		return XR_ERROR_PATH_FORMAT_INVALID;
	}
	*path = internPath(text);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrPathToString(XrInstance /* instance */, XrPath path, uint32_t capacity, uint32_t * count,
                                                  char * buffer) {
	if(path == XR_NULL_PATH || path > g_sim->paths.size()) {
		return XR_ERROR_PATH_INVALID;
	}
	const std::string & text = g_sim->paths[size_t(path - 1)];
	*count = u32(text.size() + 1);
	if(capacity == 0) {
		return XR_SUCCESS;
	}
	if(capacity < text.size() + 1) {
		return XR_ERROR_SIZE_INSUFFICIENT;
	}
	std::memcpy(buffer, text.c_str(), text.size() + 1);
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateActionSet(XrInstance /* instance */, const XrActionSetCreateInfo * /* info */,
                                                     XrActionSet * set) {
	*set = newHandle<XrActionSet>();
	g_sim->actionSets.insert(fromHandle(*set));
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrDestroyActionSet(XrActionSet set) {
	g_sim->actionSets.erase(fromHandle(set));
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrCreateAction(XrActionSet set, const XrActionCreateInfo * info, XrAction * action) {
	if(g_sim->actionSets.find(fromHandle(set)) == g_sim->actionSets.end()) {
		return XR_ERROR_HANDLE_INVALID;
	}
	SimAction a;
	a.name = info->actionName;
	a.type = info->actionType;
	*action = newHandle<XrAction>();
	g_sim->actions[fromHandle(*action)] = a;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrSuggestInteractionProfileBindings(XrInstance /* instance */,
                                                                       const XrInteractionProfileSuggestedBinding * /* bindings */) {
	// The simulator drives the actions by name: every suggestion is fine
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrAttachSessionActionSets(XrSession /* session */, const XrSessionActionSetsAttachInfo * /* info */) {
	if(g_sim->attached) {
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	}
	g_sim->attached = true;

	// A script value for an action the game does not have would silently do nothing
	for(const auto & entry : g_sim->sim.rig.actions) {
		bool known = std::any_of(g_sim->actions.begin(), g_sim->actions.end(), [&](const auto & action) {
			return action.second.name == entry.first;
		});
		if(!known) {
			LogWarning << "[vr] sim: the script sets the action \"" << entry.first << "\", which the game does not create";
		}
	}

	profileChanged();
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetCurrentInteractionProfile(XrSession /* session */, XrPath topLevelPath,
                                                                  XrInteractionProfileState * state) {
	if(!g_sim->attached) {
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	}
	const size_t hand = handOf(topLevelPath);
	if(hand > 1) {
		return XR_ERROR_PATH_UNSUPPORTED;
	}
	state->interactionProfile = handTracked(hand) ? internPath(g_sim->sim.rig.profile) : XR_NULL_PATH;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrSyncActions(XrSession /* session */, const XrActionsSyncInfo * /* info */) {
	SimRuntime & s = *g_sim;
	if(!s.attached) {
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	}
	if(s.state != XR_SESSION_STATE_FOCUSED) {
		return XR_SESSION_NOT_FOCUSED;
	}
	s.previous = s.synced;
	for(const auto & action : s.actions) {
		auto values = s.sim.rig.actions.find(action.second.name);
		for(size_t hand = 0; hand < 2; hand++) {
			s.synced[{ action.first, hand }] = values == s.sim.rig.actions.end() ? Vec2f(0.f) : values->second[hand];
		}
	}
	s.syncs++;
	return XR_SUCCESS;
}

//! The synced value of an action for a hand; false when the hand gives no input
bool actionValue(const XrActionStateGetInfo * info, Vec2f & value, bool & changed) {
	const size_t hand = handOf(info->subactionPath);
	if(hand > 1 || !handTracked(hand)) {
		return false;
	}
	const std::pair<u64, size_t> key(fromHandle(info->action), hand);
	auto now = g_sim->synced.find(key);
	if(now == g_sim->synced.end()) {
		return false;
	}
	value = now->second;
	auto before = g_sim->previous.find(key);
	changed = before != g_sim->previous.end() && before->second != value;
	return true;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetActionStateBoolean(XrSession /* session */, const XrActionStateGetInfo * info,
                                                           XrActionStateBoolean * state) {
	Vec2f value(0.f);
	bool changed = false;
	state->isActive = actionValue(info, value, changed) ? XR_TRUE : XR_FALSE;
	state->currentState = (state->isActive && value.x >= 0.5f) ? XR_TRUE : XR_FALSE;
	state->changedSinceLastSync = changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = g_sim->displayTime;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetActionStateFloat(XrSession /* session */, const XrActionStateGetInfo * info,
                                                         XrActionStateFloat * state) {
	Vec2f value(0.f);
	bool changed = false;
	state->isActive = actionValue(info, value, changed) ? XR_TRUE : XR_FALSE;
	state->currentState = state->isActive ? std::clamp(value.x, 0.f, 1.f) : 0.f;
	state->changedSinceLastSync = changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = g_sim->displayTime;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetActionStateVector2f(XrSession /* session */, const XrActionStateGetInfo * info,
                                                            XrActionStateVector2f * state) {
	Vec2f value(0.f);
	bool changed = false;
	state->isActive = actionValue(info, value, changed) ? XR_TRUE : XR_FALSE;
	state->currentState.x = state->isActive ? std::clamp(value.x, -1.f, 1.f) : 0.f;
	state->currentState.y = state->isActive ? std::clamp(value.y, -1.f, 1.f) : 0.f;
	state->changedSinceLastSync = changed ? XR_TRUE : XR_FALSE;
	state->lastChangeTime = g_sim->displayTime;
	return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL sim_xrGetInstanceProcAddr(XrInstance /* instance */, const char * name, PFN_xrVoidFunction * function) {
	*function = nullptr;
	#define ARX_XR_FUNCTION(n) \
		if(std::strcmp(name, #n) == 0) { \
			*function = reinterpret_cast<PFN_xrVoidFunction>(&sim_##n); \
			return XR_SUCCESS; \
		}
	#include "vr/XRFunctions.h"
	#undef ARX_XR_FUNCTION
	if(std::strcmp(name, "xrGetOpenGLGraphicsRequirementsKHR") == 0) {
		*function = reinterpret_cast<PFN_xrVoidFunction>(&sim_xrGetOpenGLGraphicsRequirementsKHR);
		return XR_SUCCESS;
	}
	return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // anonymous namespace

bool installSimulator() {

	arx_assert(simRequested());

	fs::path file(g_scriptPath);
	if(!fs::is_regular_file(file)) {
		LogError << "[vr] sim: no script " << file << " (--vr-sim), starting in flat mode";
		return false;
	}

	std::unique_ptr<SimRuntime> runtime = std::make_unique<SimRuntime>();
	runtime->script = file.string();
	std::string error = runtime->sim.load(fs::read(file));
	if(!error.empty()) {
		LogError << "[vr] sim: " << file << " " << error << " - starting in flat mode";
		return false;
	}

	const SimView & view = runtime->sim.view;
	LogInfo << "[vr] sim: simulated headset from " << file << ": eyes " << view.eyeSize.x << 'x' << view.eyeSize.y
	        << ", ipd " << view.ipd * 1000.f << " mm, " << view.rate << " Hz"
	        << (view.rate > 0.f ? "" : " (no pacing)") << ", fov left eye " << glm::degrees(view.fov[0].left) << ' '
	        << glm::degrees(view.fov[0].right) << ' ' << glm::degrees(view.fov[0].up) << ' '
	        << glm::degrees(view.fov[0].down) << " deg, " << runtime->sim.pending() << " timed lines; no OpenXR runtime is used";

	std::string captureDir = g_sim ? g_sim->captureDir : std::string();
	g_sim = std::move(runtime);
	g_sim->captureDir = captureDir;

	#define ARX_XR_FUNCTION(name) name = &sim_##name;
	#include "vr/XRFunctions.h"
	#undef ARX_XR_FUNCTION

	return true;
}

bool isSimulated() {
	return g_sim != nullptr && !g_sim->script.empty();
}

std::string simCommand(std::string_view line) {
	if(!isSimulated()) {
		return "vr-sim: the game does not run on the simulated headset (start it with --vr-sim <script>)";
	}
	std::string error = g_sim->sim.execute(line);
	if(error.empty()) {
		LogInfo << "[vr] sim: " << line;
	}
	return error;
}

void setSimCaptureDir(const std::string & dir) {
	if(!g_sim) {
		g_sim = std::make_unique<SimRuntime>();
	}
	g_sim->captureDir = dir;
}

SimStatus simStatus() {

	SimStatus status;
	if(!isSimulated()) {
		return status;
	}

	const SimRuntime & s = *g_sim;
	status = s.counters;
	status.active = true;
	status.script = s.script;
	status.frame = s.frames;
	status.timelineFrame = s.timelineStarted ? s.sim.frame() : 0;
	status.pending = s.sim.pending();
	status.timeline = !s.timelineStarted ? (s.sim.timeline == SimTimeline::Session ? "waiting for the session"
	                                                                             : "waiting for gameplay")
	                  : (status.pending ? "running" : "done");
	status.view = s.sim.view;
	status.rig = s.sim.rig;
	status.located = s.located;
	status.eyes = s.eyes;
	status.syncs = s.syncs;
	status.pendingShots = s.pendingShots.size();
	for(const ShotRecord & record : s.shots) {
		status.shots.push_back(record.shot);
	}
	return status;
}

#else // !ARX_HAVE_OPENXR

bool installSimulator() {
	LogError << "[vr] sim: this build has no OpenXR support, --vr-sim is not available";
	return false;
}

bool isSimulated() {
	return false;
}

std::string simCommand(std::string_view /* line */) {
	return "vr-sim: this build has no OpenXR support";
}

void setSimCaptureDir(const std::string & /* dir */) { }

SimStatus simStatus() {
	return SimStatus();
}

#endif // ARX_HAVE_OPENXR

} // namespace vr
