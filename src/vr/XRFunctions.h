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

/*!
 * \file
 * \brief The OpenXR functions the VR module calls: the runtime interface (#112)
 *
 * An X-macro list without include guard: define \c ARX_XR_FUNCTION(name) and include it.
 * vr/OpenXRUtil.h turns it into one function pointer per entry, \c vr::xrWaitFrame and so on,
 * with the names of the OpenXR functions, so the code in namespace \c vr calls through the table
 * without knowing it. vr/XRDispatch.cpp fills the table with the OpenXR loader (the real runtime),
 * vr/VRSimulator.cpp with the simulated runtime (\c --vr-sim).
 *
 * A function used in src/vr must be listed here: the global prototypes are hidden
 * (\c XR_NO_PROTOTYPES), so a call of an unlisted one does not compile - and the simulator
 * must implement every entry (\c sim_<name>), or it does not link.
 */

ARX_XR_FUNCTION(xrGetInstanceProcAddr)
ARX_XR_FUNCTION(xrEnumerateInstanceExtensionProperties)
ARX_XR_FUNCTION(xrCreateInstance)
ARX_XR_FUNCTION(xrDestroyInstance)
ARX_XR_FUNCTION(xrGetInstanceProperties)
ARX_XR_FUNCTION(xrPollEvent)
ARX_XR_FUNCTION(xrResultToString)
ARX_XR_FUNCTION(xrGetSystem)
ARX_XR_FUNCTION(xrGetSystemProperties)
ARX_XR_FUNCTION(xrEnumerateViewConfigurations)
ARX_XR_FUNCTION(xrEnumerateViewConfigurationViews)
ARX_XR_FUNCTION(xrCreateSession)
ARX_XR_FUNCTION(xrDestroySession)
ARX_XR_FUNCTION(xrBeginSession)
ARX_XR_FUNCTION(xrEndSession)
ARX_XR_FUNCTION(xrRequestExitSession)
ARX_XR_FUNCTION(xrEnumerateReferenceSpaces)
ARX_XR_FUNCTION(xrCreateReferenceSpace)
ARX_XR_FUNCTION(xrCreateActionSpace)
ARX_XR_FUNCTION(xrLocateSpace)
ARX_XR_FUNCTION(xrDestroySpace)
ARX_XR_FUNCTION(xrEnumerateSwapchainFormats)
ARX_XR_FUNCTION(xrCreateSwapchain)
ARX_XR_FUNCTION(xrDestroySwapchain)
ARX_XR_FUNCTION(xrEnumerateSwapchainImages)
ARX_XR_FUNCTION(xrAcquireSwapchainImage)
ARX_XR_FUNCTION(xrWaitSwapchainImage)
ARX_XR_FUNCTION(xrReleaseSwapchainImage)
ARX_XR_FUNCTION(xrWaitFrame)
ARX_XR_FUNCTION(xrBeginFrame)
ARX_XR_FUNCTION(xrEndFrame)
ARX_XR_FUNCTION(xrLocateViews)
ARX_XR_FUNCTION(xrStringToPath)
ARX_XR_FUNCTION(xrPathToString)
ARX_XR_FUNCTION(xrCreateActionSet)
ARX_XR_FUNCTION(xrDestroyActionSet)
ARX_XR_FUNCTION(xrCreateAction)
ARX_XR_FUNCTION(xrSuggestInteractionProfileBindings)
ARX_XR_FUNCTION(xrAttachSessionActionSets)
ARX_XR_FUNCTION(xrGetCurrentInteractionProfile)
ARX_XR_FUNCTION(xrGetActionStateBoolean)
ARX_XR_FUNCTION(xrGetActionStateFloat)
ARX_XR_FUNCTION(xrGetActionStateVector2f)
ARX_XR_FUNCTION(xrSyncActions)
