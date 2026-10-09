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

#include "vr/VRSwapchain.h"

#if ARX_HAVE_OPENXR

#include <sstream>

#include "io/log/Logger.h"

namespace vr {

namespace {

//! Failures logged per swapchain before it goes quiet (acquire / release run every frame)
const u32 MaxLoggedFailures = 5;

} // anonymous namespace

std::string glFormatName(s64 format) {
	switch(format) {
		case GL_SRGB8_ALPHA8:       return "GL_SRGB8_ALPHA8";
		case GL_SRGB8:              return "GL_SRGB8";
		case GL_RGBA8:              return "GL_RGBA8";
		case GL_RGB8:               return "GL_RGB8";
		case GL_RGB10_A2:           return "GL_RGB10_A2";
		case GL_RGBA16:             return "GL_RGBA16";
		case GL_RGBA16F:            return "GL_RGBA16F";
		case GL_RGB16F:             return "GL_RGB16F";
		case GL_R11F_G11F_B10F:     return "GL_R11F_G11F_B10F";
		case GL_DEPTH_COMPONENT16:  return "GL_DEPTH_COMPONENT16";
		case GL_DEPTH_COMPONENT24:  return "GL_DEPTH_COMPONENT24";
		case GL_DEPTH_COMPONENT32F: return "GL_DEPTH_COMPONENT32F";
		case GL_DEPTH24_STENCIL8:   return "GL_DEPTH24_STENCIL8";
		case GL_DEPTH32F_STENCIL8:  return "GL_DEPTH32F_STENCIL8";
		default: {
			std::ostringstream oss;
			oss << "0x" << std::hex << format;
			return oss.str();
		}
	}
}

s64 chooseSwapchainFormat(XrSession session) {

	uint32_t count = 0;
	XrResult result = xrEnumerateSwapchainFormats(session, 0, &count, nullptr);
	std::vector<s64> formats(count);
	if(XR_SUCCEEDED(result)) {
		result = xrEnumerateSwapchainFormats(session, count, &count, formats.data());
	}
	if(XR_FAILED(result)) {
		LogError << "Could not enumerate OpenXR swapchain formats: " << resultName(result);
		return 0;
	}
	formats.resize(count);

	std::ostringstream list;
	for(s64 format : formats) {
		list << ' ' << glFormatName(format);
	}
	LogDebug("OpenXR swapchain formats (runtime preference first):" << list.str());

	for(s64 format : formats) {
		if(format == GL_SRGB8_ALPHA8) {
			return format;
		}
	}

	for(s64 format : formats) {
		if(format == GL_RGBA8) {
			LogWarning << "OpenXR runtime has no GL_SRGB8_ALPHA8 swapchains, using GL_RGBA8:"
			              " colors in the headset will look washed out";
			return format;
		}
	}

	LogError << "OpenXR runtime offers no usable color swapchain format (have:" << list.str() << ")";
	return 0;
}

bool Swapchain::create(XrSession session, s64 format, Vec2i size, bool framebuffers, const char * name) {

	arx_assert(m_handle == XR_NULL_HANDLE);

	m_name = name;
	m_failures = 0;

	XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
	// Color attachment: the game renders into eye images; transfer: the flat frame is copied into the
	// quad, and the left eye and the quad are copied out for the desktop mirror (#21)
	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT
	                  | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
	info.format = format;
	info.sampleCount = 1;
	info.width = u32(size.x);
	info.height = u32(size.y);
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;

	XrResult result = xrCreateSwapchain(session, &info, &m_handle);
	if(XR_FAILED(result)) {
		m_handle = XR_NULL_HANDLE;
		LogError << "Could not create the OpenXR swapchain for the " << m_name << " (" << size.x << 'x' << size.y
		         << ' ' << glFormatName(format) << "): " << resultName(result);
		return false;
	}
	m_size = size;

	uint32_t count = 0;
	result = xrEnumerateSwapchainImages(m_handle, 0, &count, nullptr);
	std::vector<XrSwapchainImageOpenGLKHR> images(count, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
	if(XR_SUCCEEDED(result)) {
		result = xrEnumerateSwapchainImages(m_handle, count, &count,
		                                    reinterpret_cast<XrSwapchainImageBaseHeader *>(images.data()));
	}
	if(XR_FAILED(result) || count == 0) {
		LogError << "Could not get the images of the OpenXR swapchain for the " << m_name << ": "
		         << resultName(result) << ", " << count << " images";
		destroy();
		return false;
	}
	for(uint32_t i = 0; i < count; i++) {
		m_textures.push_back(images[i].image);
	}

	if(framebuffers && !createFramebuffers()) {
		destroy();
		return false;
	}

	LogInfo << "VR swapchain for the " << m_name << ": " << size.x << 'x' << size.y << ' ' << glFormatName(format)
	        << ", " << m_textures.size() << " images" << (framebuffers ? ", framebuffers complete" : "");

	return true;
}

bool Swapchain::createFramebuffers() {

	GLint drawFramebuffer = 0, readFramebuffer = 0, renderbuffer = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);

	// One depth buffer for all images: only one image is rendered at a time
	glGenRenderbuffers(1, &m_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_size.x, m_size.y);

	m_framebuffers.resize(m_textures.size(), 0);
	glGenFramebuffers(GLsizei(m_framebuffers.size()), m_framebuffers.data());

	bool complete = true;
	for(size_t i = 0; i < m_framebuffers.size(); i++) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[i], 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if(status != GL_FRAMEBUFFER_COMPLETE) {
			LogError << "Framebuffer for image " << i << " of the " << m_name << " swapchain is incomplete: 0x"
			         << std::hex << status << std::dec;
			complete = false;
			break;
		}
	}

	glBindRenderbuffer(GL_RENDERBUFFER, GLuint(renderbuffer));
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFramebuffer));
	glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFramebuffer));

	return complete;
}

void Swapchain::destroy() {

	if(!m_framebuffers.empty()) {
		glDeleteFramebuffers(GLsizei(m_framebuffers.size()), m_framebuffers.data());
		m_framebuffers.clear();
	}

	if(m_depth) {
		glDeleteRenderbuffers(1, &m_depth);
		m_depth = 0;
	}

	// The textures belong to the swapchain
	m_textures.clear();

	if(m_handle != XR_NULL_HANDLE) {
		xrDestroySwapchain(m_handle);
		m_handle = XR_NULL_HANDLE;
	}

	m_size = Vec2i(0);
	m_acquired = u32(-1);
}

bool Swapchain::acquire() {

	arx_assert(m_handle != XR_NULL_HANDLE);
	arx_assert(m_acquired == u32(-1));

	XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	uint32_t index = 0;
	XrResult result = xrAcquireSwapchainImage(m_handle, &acquireInfo, &index);
	if(XR_FAILED(result)) {
		logFailure("xrAcquireSwapchainImage", result);
		return false;
	}

	XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	waitInfo.timeout = XR_INFINITE_DURATION;
	result = xrWaitSwapchainImage(m_handle, &waitInfo);
	if(XR_FAILED(result)) {
		logFailure("xrWaitSwapchainImage", result);
		return false;
	}

	if(index >= m_textures.size()) {
		logFailure("xrAcquireSwapchainImage (image index out of range)", result);
		release();
		return false;
	}

	m_acquired = index;

	return true;
}

GLuint Swapchain::texture() const {
	arx_assert(m_acquired < m_textures.size());
	return m_textures[m_acquired];
}

GLuint Swapchain::framebuffer() const {
	arx_assert(m_acquired < m_framebuffers.size());
	return m_framebuffers[m_acquired];
}

void Swapchain::release() {

	arx_assert(m_handle != XR_NULL_HANDLE);

	XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	XrResult result = xrReleaseSwapchainImage(m_handle, &releaseInfo);
	if(XR_FAILED(result)) {
		logFailure("xrReleaseSwapchainImage", result);
	}

	m_acquired = u32(-1);
}

XrRect2Di Swapchain::rect() const {
	XrRect2Di rect;
	rect.offset.x = 0;
	rect.offset.y = 0;
	rect.extent.width = m_size.x;
	rect.extent.height = m_size.y;
	return rect;
}

void Swapchain::logFailure(const char * call, XrResult result) {
	m_failures++;
	if(m_failures <= MaxLoggedFailures) {
		LogError << call << " failed for the " << m_name << " swapchain: " << resultName(result)
		         << (m_failures == MaxLoggedFailures ? " (further failures of this swapchain are not logged)" : "");
	}
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
