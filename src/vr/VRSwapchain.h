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

#ifndef ARX_VR_VRSWAPCHAIN_H
#define ARX_VR_VRSWAPCHAIN_H

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <string>
#include <vector>

#include "math/Types.h"

namespace vr {

/*!
 * Uniform background of the eye swapchains, sRGB-encoded: dark grey, so it is told apart from
 * "no layer" - and the simulator's compositor tells "the eye got no picture" by it (#112)
 */
const GLfloat EyeBackgroundColor[4] = { 0.15f, 0.15f, 0.15f, 1.f };

/*!
 * \brief Pick the color format for all swapchains of a session
 *
 * Prefers \c GL_SRGB8_ALPHA8: the game writes sRGB-encoded colors (it renders for a monitor without
 * \c GL_FRAMEBUFFER_SRGB), so an sRGB swapchain shows them as on the monitor. A linear format would
 * be decoded twice by the compositor and look washed out.
 *
 * \return the OpenGL internal format, or 0 if the runtime offers none (logged)
 */
s64 chooseSwapchainFormat(XrSession session);

/*!
 * \brief An OpenXR swapchain of OpenGL 2D textures
 *
 * With \c framebuffers, every image also gets a framebuffer object (the image as color attachment
 * plus one shared depth buffer), so the game can render into the image directly. With \c samples > 1
 * as well (#226) the game renders into one shared multisampled framebuffer object instead
 * (\ref renderFramebuffer), which \ref resolve averages into the acquired image: the swapchain
 * images themselves stay single-sample, as the runtime recommends.
 *
 * Creating and destroying needs the OpenGL context the session is bound to. Not RAII on purpose:
 * \ref destroy must run while that context exists, not from a static destructor.
 */
class Swapchain {

public:

	/*!
	 * \param name what the swapchain is for, used in log lines ("left eye", "flat frame quad")
	 * \param samples multisampling of the render framebuffer (only with \c framebuffers); 0 or 1 = none.
	 *                Falls back to none (logged) if the multisampled framebuffer cannot be made.
	 * \return false on failure (logged); the swapchain is then empty
	 */
	bool create(XrSession session, s64 format, Vec2i size, bool framebuffers, const char * name,
	            int samples = 0);

	//! Destroy the swapchain and its OpenGL objects (if any)
	void destroy();

	bool isValid() const { return m_handle != XR_NULL_HANDLE; }
	XrSwapchain handle() const { return m_handle; }
	Vec2i size() const { return m_size; }

	/*!
	 * \brief Acquire the next image and wait until the compositor is done with it
	 *
	 * Must be followed by \ref release before the frame is submitted.
	 * \return false on failure (logged, first few only)
	 */
	bool acquire();

	//! The OpenGL texture of the acquired image
	GLuint texture() const;

	//! The framebuffer object of the acquired image (only for swapchains with framebuffers)
	GLuint framebuffer() const;

	/*!
	 * The framebuffer object the game renders into: the multisampled one if there is one, otherwise
	 * the acquired image's. Multisampled content reaches the image only through \ref resolve.
	 */
	GLuint renderFramebuffer() const;

	//! The multisampling of \ref renderFramebuffer: 0 when it is the image's own framebuffer object
	int samples() const { return m_msaaFramebuffer ? m_samples : 0; }

	/*!
	 * Average the multisampled framebuffer into the acquired image (a no-op without multisampling).
	 * Blits are scissored, so the caller disables the scissor test; the framebuffer bindings change.
	 */
	void resolve();

	//! Hand the acquired image back to the compositor
	void release();

	//! An image rect covering the whole swapchain, for composition layers
	XrRect2Di rect() const;

private:

	bool createFramebuffers();

	//! The shared framebuffer object with \ref m_samples samples; false (logged) if it is incomplete
	bool createMultisampledFramebuffer(s64 format);

	void destroyMultisampledFramebuffer();

	void logFailure(const char * call, XrResult result);

	XrSwapchain m_handle = XR_NULL_HANDLE;
	Vec2i m_size = Vec2i(0);
	std::string m_name;
	std::vector<GLuint> m_textures;
	std::vector<GLuint> m_framebuffers;
	GLuint m_depth = 0;
	GLuint m_msaaFramebuffer = 0;
	GLuint m_msaaColor = 0;
	GLuint m_msaaDepth = 0;
	int m_samples = 0;
	u32 m_acquired = u32(-1);
	u32 m_failures = 0;

};

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_VRSWAPCHAIN_H
