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

#include "vr/VRMirror.h"

#if ARX_HAVE_OPENXR

#include <array>

#include "core/GameEvent.h"
#include "io/log/Logger.h"
#include "vr/VRSwapchain.h"

namespace vr {

void DesktopMirror::begin(bool wanted, const char * why, size_t eye) {

	m_eye = eye == 0 ? 0 : 1;
	m_haveEye = false;
	m_haveFlat = false;
	m_logged = false;

	if(!wanted) {
		m_on = false;
		m_state = std::string("off: ") + why;
		LogInfo << "[vr] desktop mirror off (" << why << "): the window shows the flat frame";
		gameevent::Event("vr.mirror")("state", "off")("why", why);
		return;
	}

	// A raw copy between the sRGB swapchain format and our RGBA8 textures: no blit, which would
	// convert sRGB differently per driver, and no texture view of a runtime-owned texture
	OpenGLInfo gl;
	if(!gl.has("GL_ARB_copy_image", 4, 3)) {
		stop("OpenGL has no glCopyImageSubData (GL 4.3 / GL_ARB_copy_image)");
		return;
	}

	m_on = true;
	m_state = "on";
	LogInfo << "[vr] desktop mirror on: the window shows what the " << eyeName() << " eye sees (vr_mirror_eye "
	        << m_eye << ")";
	gameevent::Event("vr.mirror")("state", "on")("eye", eyeName());
}

void DesktopMirror::stop(const std::string & why) {
	m_on = false;
	m_state = "off: " + why;
	LogWarning << "[vr] desktop mirror off: " << why << " - the window shows the flat frame";
	gameevent::Event("vr.mirror")("state", "off")("why", why);
}

void DesktopMirror::destroy() {
	if(m_eyeTexture) {
		glDeleteTextures(1, &m_eyeTexture);
		m_eyeTexture = 0;
	}
	if(m_flatTexture) {
		glDeleteTextures(1, &m_flatTexture);
		m_flatTexture = 0;
	}
	m_eyeSize = Vec2i(0);
	m_flatSize = Vec2i(0);
	m_haveEye = false;
	m_haveFlat = false;
	if(m_on) {
		m_on = false;
		m_state = "off: the VR session ended";
	}
}

bool DesktopMirror::prepareTexture(GLuint & texture, Vec2i & size, const Vec2i & wanted) {

	if(texture && size == wanted) {
		return true;
	}

	if(!texture) {
		glGenTextures(1, &texture);
	}

	// The caller (GLStateGuard in VRSystem) restores the texture binding
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wanted.x, wanted.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	size = wanted;

	return true;
}

void DesktopMirror::copyEye(const Swapchain & eye) {

	if(!m_on) {
		return;
	}

	prepareTexture(m_eyeTexture, m_eyeSize, eye.size());
	glCopyImageSubData(eye.texture(), GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_eyeTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_eyeSize.x, m_eyeSize.y, 1);
	m_haveEye = true;
}

void DesktopMirror::copyFlatFrame(const Swapchain & quad) {

	if(!m_on) {
		return;
	}

	prepareTexture(m_flatTexture, m_flatSize, quad.size());
	glCopyImageSubData(quad.texture(), GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_flatTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_flatSize.x, m_flatSize.y, 1);
	m_haveFlat = true;
}

void DesktopMirror::draw(const Vec2i & windowSize, const MirrorFov & fov, const Vec3f & eyePosition,
                         const glm::quat & eyeOrientation, const Quad & quad) {

	bool haveEye = m_haveEye;
	bool haveFlat = m_haveFlat && quad.shown;
	m_haveEye = false;
	m_haveFlat = false;
	if(!m_on || !haveEye || windowSize.x <= 0 || windowSize.y <= 0) {
		return;
	}

	MirrorRect crop = mirrorCrop(fov, m_eyeSize, windowSize);

	// Where the head-locked quad appears in the mirrored eye: its corners projected, each with its depth
	// for a perspective-correct texture (q = 1 / depth)
	struct Corner {
		Vec2f window;
		float s, t, q;
	};
	std::array<Corner, 4> corners;
	if(haveFlat) {
		const float x = quad.width * 0.5f, y = quad.height * 0.5f;
		const Vec3f points[4] = {
			Vec3f(-x, -y, -quad.distance), Vec3f(x, -y, -quad.distance),
			Vec3f(-x, y, -quad.distance), Vec3f(x, y, -quad.distance),
		};
		for(size_t i = 0; i < corners.size() && haveFlat; i++) {
			Vec2f uv(0.f);
			float depth = 0.f;
			haveFlat = mirrorProject(points[i], eyePosition, eyeOrientation, fov, uv, depth);
			corners[i].window = mirrorToWindow(crop, uv);
			corners[i].q = 1.f / depth;
			corners[i].s = float(i % 2) * corners[i].q;
			corners[i].t = float(i / 2) * corners[i].q;
		}
	}

	if(!m_logged) {
		m_logged = true;
		LogInfo << "[vr] desktop mirror: " << eyeName() << " eye " << m_eyeSize.x << 'x' << m_eyeSize.y << ", crop u "
		        << crop.u0 << ".." << crop.u1 << " v " << crop.v0 << ".." << crop.v1 << " into the window "
		        << windowSize.x << 'x' << windowSize.y
		        << (haveFlat ? "" : ", no flat frame quad");
		if(haveFlat) {
			LogInfo << "[vr] desktop mirror: flat frame quad in the window from (" << corners[0].window.x << ", "
			        << corners[0].window.y << ") to (" << corners[3].window.x << ", " << corners[3].window.y
			        << ") (-1..1)";
		}
	}

	// The fixed-function state of the renderer, saved whole and restored below
	GLint activeTexture = GL_TEXTURE0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	GLint units = 1;
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
	glPushAttrib(GL_ALL_ATTRIB_BITS);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glDrawBuffer(GL_BACK);
	glViewport(0, 0, windowSize.x, windowSize.y);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	for(GLint unit = 1; unit < units; unit++) {
		glActiveTexture(GLenum(GL_TEXTURE0 + unit));
		glDisable(GL_TEXTURE_2D);
	}
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1.f, 1.f, 1.f, 1.f);

	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// The mirrored eye, cropped to the window
	glBindTexture(GL_TEXTURE_2D, m_eyeTexture);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(crop.u0, crop.v0);
	glVertex2f(-1.f, -1.f);
	glTexCoord2f(crop.u1, crop.v0);
	glVertex2f(1.f, -1.f);
	glTexCoord2f(crop.u0, crop.v1);
	glVertex2f(-1.f, 1.f);
	glTexCoord2f(crop.u1, crop.v1);
	glVertex2f(1.f, 1.f);
	glEnd();

	// The flat frame where the mirrored eye sees the quad; the 2D interface drawn into it (#19) is
	// premultiplied by its alpha and shows the eye through where nothing was drawn
	if(haveFlat) {
		if(quad.blended) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}
		glBindTexture(GL_TEXTURE_2D, m_flatTexture);
		glBegin(GL_TRIANGLE_STRIP);
		for(const Corner & corner : corners) {
			glTexCoord4f(corner.s, corner.t, 0.f, corner.q);
			glVertex2f(corner.window.x, corner.window.y);
		}
		glEnd();
		if(quad.blended) {
			glDisable(GL_BLEND);
		}
	}

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();

	glPopAttrib();
	glActiveTexture(GLenum(activeTexture));

	m_frames++;
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
