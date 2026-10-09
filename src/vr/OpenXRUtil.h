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

#ifndef ARX_VR_OPENXRUTIL_H
#define ARX_VR_OPENXRUTIL_H

/*!
 * \file
 * \brief OpenXR headers with the OpenGL binding, and helpers shared by the files in src/vr
 *
 * Internal to the VR module: the rest of the game only includes vr/VRSystem.h.
 */

#include "platform/Platform.h"
#include "Configure.h"

#if ARX_HAVE_OPENXR

#include <string>

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
// The OpenGL binding for OpenXR on Windows is the window's device context and WGL context
#include <windows.h>
#define XR_USE_PLATFORM_WIN32
#endif

// OpenGL headers (GLEW or libepoxy), the same the renderer uses
#include "graphics/opengl/OpenGLUtil.h"

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace vr {

//! \return the name of an OpenXR result code, e.g. "XR_ERROR_SESSION_LOST"
std::string resultName(XrResult result);

//! \return the name of an OpenGL internal format used for swapchains, e.g. "GL_SRGB8_ALPHA8"
std::string glFormatName(s64 format);

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_OPENXRUTIL_H
