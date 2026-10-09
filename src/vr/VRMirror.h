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

#ifndef ARX_VR_VRMIRROR_H
#define ARX_VR_VRMIRROR_H

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <string>

#include "math/Types.h"
#include "vr/MirrorGeometry.h"

namespace vr {

class Swapchain;

/*!
 * \brief The desktop mirror (#21, research row F-24): the window shows what one eye sees
 *
 * Never a third render: while the mirrored eye image and the flat frame quad image are acquired, they
 * are copied raw (\c glCopyImageSubData, no sRGB conversion - the images hold sRGB-encoded values
 * like the window) into two textures of the mirror; after the frame was submitted, the mirrored eye is
 * drawn into the window's back buffer, cropped to the window's aspect (\ref mirrorCrop), and the
 * flat frame on top where the head-locked quad appears in that eye. The window then shows the
 * same picture as that eye, minus the lens distortion.
 *
 * The draw saves and restores the whole fixed-function OpenGL state it touches (\c glPushAttrib),
 * so the renderer's cached state stays true.
 *
 * Internal to the VR module; the game sees \ref mirrorStatus in VRSystem.h.
 */
class DesktopMirror {

public:

	/*!
	 * \brief Decide for a new session whether the window mirrors an eye
	 *
	 * \param wanted false when switched off (\c vr_mirror in cfg.ini, \c --no-vr-mirror)
	 * \param why    what switched it off, for the log and status.json
	 * \param eye    the eye to mirror: 0 = left, 1 = right (\c vr_mirror_eye in cfg.ini; right by
	 *               default - the owner's choice of 2026-10-10)
	 */
	void begin(bool wanted, const char * why, size_t eye);

	//! \return the mirrored eye: 0 = left, 1 = right
	size_t eye() const { return m_eye; }

	//! \return "left" or "right"
	const char * eyeName() const { return m_eye == 0 ? "left" : "right"; }

	//! Delete the OpenGL objects; needs the session's OpenGL context
	void destroy();

	//! \return true if this session mirrors an eye
	bool isOn() const { return m_on; }

	//! Copy the acquired image of the mirrored eye (\ref eye); call before it is released
	void copyEye(const Swapchain & eye);

	//! Copy the acquired flat frame quad image; call before it is released
	void copyFlatFrame(const Swapchain & quad);

	/*!
	 * The flat frame quad as submitted, in the space the eye pose is given in: the head (view) space
	 * for the head-locked quad, the world-locked space for the floating panel (#49)
	 */
	struct Quad {
		bool shown = false;
		bool blended = false; //!< the image is the 2D interface over transparency (#19), drawn with its alpha
		float width = 0.f;    //!< meters
		float height = 0.f;   //!< meters
		Vec3f position = Vec3f(0.f); //!< the quad's centre, meters
		glm::quat orientation = glm::quat(1.f, 0.f, 0.f, 0.f); //!< the quad faces its +z axis
	};

	/*!
	 * \brief Draw what the mirrored eye sees into the window's back buffer
	 *
	 * Call after the frame was submitted and before the buffers are swapped, only for frames that
	 * carried this frame's mirrored eye (\ref copyEye).
	 *
	 * \param fov            the mirrored eye's field of view for this frame
	 * \param eyePosition    the mirrored eye's position in the quad's space, for placing the quad
	 * \param eyeOrientation the mirrored eye's orientation in the quad's space
	 */
	void draw(const Vec2i & windowSize, const MirrorFov & fov, const Vec3f & eyePosition,
	          const glm::quat & eyeOrientation, const Quad & quad);

	//! \return "on", or why the window shows the flat frame instead
	const std::string & state() const { return m_state; }

	//! \return frames mirrored since the game started
	u64 frames() const { return m_frames; }

private:

	bool prepareTexture(GLuint & texture, Vec2i & size, const Vec2i & wanted);

	void stop(const std::string & why);

	bool m_on = false;
	size_t m_eye = 1;
	std::string m_state = "flat mode";
	u64 m_frames = 0;
	bool m_haveEye = false;   //!< the eye texture holds this frame's mirrored eye
	bool m_haveFlat = false;  //!< the flat texture holds this frame's flat frame
	bool m_logged = false;    //!< the geometry of the first mirrored frame was logged

	GLuint m_eyeTexture = 0;
	Vec2i m_eyeSize = Vec2i(0);
	GLuint m_flatTexture = 0;
	Vec2i m_flatSize = Vec2i(0);

};

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_VRMIRROR_H
